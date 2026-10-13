#include "App.hpp"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "driver/rtc_io.h"

#include "config.hpp"
#include "logic/CommandParser.hpp"
#include "periph/GnssProfile.hpp"
#include "reliability/BusLock.hpp"
#include "reliability/TaskWatchdog.hpp"
#include "transport/HealthReport.hpp"
#include "transport/OsmAndProtocol.hpp"

namespace
{
    constexpr const char *logTag = "tracker";

    // Sends to a bounded queue without blocking; if it is full the oldest
    // item makes room and is counted as dropped. The producer (the gps
    // task) must never wait on a slow consumer (the card, the modem).
    template <typename T>
    bool enqueueDroppingOldest(QueueHandle_t queue, const T &item, std::atomic<uint32_t> &dropped)
    {
        if (xQueueSend(queue, &item, 0) == pdTRUE)
        {
            return true;
        }
        T discarded;
        xQueueReceive(queue, &discarded, 0);
        Diagnostics::increment(dropped);
        return xQueueSend(queue, &item, 0) == pdTRUE;
    }

    // Sleeps in slices so a wait longer than the task watchdog period
    // (back-off between uplink attempts can reach minutes) keeps feeding it.
    void sleepFeeding(const WatchdogSubscription &watchdog, uint32_t ms)
    {
        while (ms > 0)
        {
            const uint32_t slice = ms < 1000 ? ms : 1000;
            vTaskDelay(pdMS_TO_TICKS(slice));
            watchdog.feed();
            ms -= slice;
        }
    }

    const char *resetReasonName(esp_reset_reason_t reason)
    {
        switch (reason)
        {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "other";
        }
    }

    ResetCause resetCauseOf(esp_reset_reason_t reason)
    {
        switch (reason)
        {
        case ESP_RST_POWERON: return ResetCause::PowerOn;
        case ESP_RST_SW: return ResetCause::Software;
        case ESP_RST_PANIC: return ResetCause::Panic;
        case ESP_RST_INT_WDT: return ResetCause::InterruptWatchdog;
        case ESP_RST_TASK_WDT: return ResetCause::TaskWatchdog;
        case ESP_RST_WDT: return ResetCause::OtherWatchdog;
        case ESP_RST_DEEPSLEEP: return ResetCause::DeepSleep;
        case ESP_RST_BROWNOUT: return ResetCause::Brownout;
        default: return ResetCause::Other;
        }
    }
}

App::App()
    : values_(Settings::defaults()),
      eventLog_(Config::eventLogDepth),
      gpsUart_(Config::gpsUart, Config::gpsTxPin, Config::gpsRxPin,
               Config::gpsBaudRate, Config::gpsRxBufferBytes),
      gnss_(gpsUart_),
      gsmUart_(Config::gsmUart, Config::gsmTxPin, Config::gsmRxPin,
               Config::gsmBaudRate, Config::gsmRxBufferBytes),
      modem_(gsmUart_, Config::gsmPowerKeyPin),
      i2c_(Config::i2cPort, Config::i2cSdaPin, Config::i2cSclPin),
      display_(i2c_),
      sdCard_(Config::sdSpiHost, Config::sdSclkPin, Config::sdMosiPin, Config::sdMisoPin,
              Config::sdCsPin, Config::sdSpiHz, Config::sdMountPoint),
      battery_(Config::batteryAdcUnit, Config::batteryAdcChannel,
               Config::batteryAdcAttenuation, Config::batteryDividerRatio),
      accel_(i2c_, Config::accelInterruptPin),
      button_(Config::buttonPin),
      led_(Config::statusLedPin),
      speedMotion_({Config::Defaults::motionStartCmPerS, Config::Defaults::motionStopCmPerS,
                    Config::Defaults::motionStopDelayMs}),
      accelMotion_(accel_, {Config::Defaults::motionStopDelayMs, Config::accelStartPulses}),
      motion_(&speedMotion_),
      policy_({Config::Defaults::movingIntervalMs, Config::Defaults::stationaryIntervalMs,
               Config::Defaults::minDistanceM, Config::Defaults::minSatellites,
               Config::Defaults::maxHdopCenti}),
      sleepPolicy_({Config::Defaults::sleepAfterStationaryMs}),
      timeZone_({Config::timeZoneStandardOffsetMinutes, Config::timeZoneEuropeanSummerTime}),
      buttonDecoder_(Config::buttonDebounceUs, Config::buttonLongPressUs),
      view_(display_, timeZone_),
      storage_(sdCard_, Config::trackDirectory),
      logger_(storage_, Config::trackDirectory, Config::storageRetries),
      modemTransport_(modem_, values_.apn),
      uplink_(&modemTransport_),
      scheduler_({Config::Defaults::uplinkMinIntervalMs, Config::uplinkBackoffInitialMs,
                  Config::uplinkBackoffMaxMs, Config::gsmFailuresBeforePowerCycle})
{
}

// ---------------------------------------------------------------------------
// Start-up

void App::begin()
{
    eventLog_.begin(); // from here on every log line is also queued for the card
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(logTag, "Boot: reset reason %s (%d)", resetReasonName(reason), static_cast<int>(reason));
    resetCause_ = resetCauseOf(reason);
    if (reason == ESP_RST_DEEPSLEEP)
    {
        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(logTag, "Woke from deep sleep: %s",
                 cause == ESP_SLEEP_WAKEUP_EXT1 ? "motion" : cause == ESP_SLEEP_WAKEUP_EXT0 ? "button" : "other");
    }

    settings_.begin();
    loadSettings();
    createQueues();

    led_.begin();
    showStateOnLed(TrackerState::Booting);

    // One ISR service for every GPIO that raises interrupts (button,
    // accelerometer INT1); the drivers only add their handlers.
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    i2c_.begin();
    display_.begin(); // a missing or glitched display is reported, not fatal
    // The accelerometer is the better motion source (no fix needed, no
    // position noise); without it motion comes from GNSS speed.
    if (accel_.begin() == ESP_OK)
    {
        motion_ = &accelMotion_;
    }

    gpsUart_.begin();
    gnss_.begin(GnssProfiles::byIndex(values_.gnssProfile));

    gsmUart_.begin();
    modem_.begin();
    modem_.setIdleHook(feedWatchdogFromModem);

    sdCard_.begin();
    storage_.begin(); // a missing card is reported, not fatal

    battery_.begin();
    button_.begin(buttonEdgeQueue_);
    console_.begin();

    ESP_LOGI(logTag, "Ready. Device %s, log %" PRIu32 "/%" PRIu32 " ms, uplink %s -> %s",
             values_.deviceId, values_.movingIntervalMs, values_.stationaryIntervalMs,
             values_.uplinkEnabled ? "on" : "off", values_.serverUrl);
}

void App::loadSettings()
{
    if (settings_.load(values_))
    {
        ESP_LOGI(logTag, "Settings loaded from NVS");
    }
    else
    {
        // First boot on this flash: write the defaults so the field
        // procedure has a record to edit in place.
        const esp_err_t err = settings_.save(values_);
        ESP_LOGI(logTag, "No settings in NVS, saved defaults: %s", esp_err_to_name(err));
    }

    applyLiveSettings(values_);
    uplink_ = values_.uplinkEnabled ? static_cast<IUplinkTransport *>(&modemTransport_)
                                    : static_cast<IUplinkTransport *>(&nullTransport_);
}

void App::applyLiveSettings(const Settings::Values &values)
{
    // The transport copied the compile-time default when it was
    // constructed, before NVS was read; hand it the loaded value.
    modemTransport_.setApn(values.apn);
    policy_.setParams({values.movingIntervalMs, values.stationaryIntervalMs, values.minDistanceM,
                       Config::Defaults::minSatellites, Config::Defaults::maxHdopCenti});
    uplinkSampleIntervalMs_ = values.uplinkMinIntervalMs;
    speedMotion_.setParams({values.motionStartCmPerS, values.motionStopCmPerS, values.motionStopDelayMs});
    accelMotion_.setParams({values.motionStopDelayMs, Config::accelStartPulses});
    scheduler_.setParams({values.uplinkMinIntervalMs, Config::uplinkBackoffInitialMs,
                          Config::uplinkBackoffMaxMs, Config::gsmFailuresBeforePowerCycle});
}

void App::createQueues()
{
    displayQueue_ = xQueueCreate(Config::displayQueueDepth, sizeof(DisplayMessage));
    logQueue_ = xQueueCreate(Config::logQueueDepth, sizeof(TrackRecord));
    uplinkQueue_ = xQueueCreate(Config::uplinkQueueDepth, sizeof(TrackRecord));
    buttonEdgeQueue_ = xQueueCreate(Config::buttonEdgeQueueDepth, sizeof(ButtonEdge));
    batteryQueue_ = xQueueCreate(1, sizeof(uint16_t));
    healthQueue_ = xQueueCreate(1, sizeof(HealthSnapshot));
    gpsSettingsQueue_ = xQueueCreate(1, sizeof(Settings::Values));
    uplinkSettingsQueue_ = xQueueCreate(1, sizeof(Settings::Values));
    i2cMutex_ = xSemaphoreCreateMutex();
    sdMutex_ = xSemaphoreCreateMutex();
    modemMutex_ = xSemaphoreCreateMutex();
    configASSERT(displayQueue_ && logQueue_ && uplinkQueue_ && buttonEdgeQueue_ &&
                 batteryQueue_ && healthQueue_ && gpsSettingsQueue_ && uplinkSettingsQueue_ &&
                 i2cMutex_ && sdMutex_ && modemMutex_);
}

void App::run()
{
    begin();
    startTasks();
}

void App::startTasks()
{
    struct Spec
    {
        TaskIndex index;
        TaskFunction_t entry;
        const char *name;
        uint32_t stackBytes;
        UBaseType_t priority;
    };
    const Spec specs[] = {
        {TaskGps, gpsTaskEntry, "gps", Config::gpsTaskStackBytes, Config::gpsTaskPriority},
        {TaskDisplay, displayTaskEntry, "display", Config::displayTaskStackBytes, Config::displayTaskPriority},
        {TaskLogger, loggerTaskEntry, "logger", Config::loggerTaskStackBytes, Config::loggerTaskPriority},
        {TaskUplink, uplinkTaskEntry, "uplink", Config::uplinkTaskStackBytes, Config::uplinkTaskPriority},
        {TaskUi, uiTaskEntry, "ui", Config::uiTaskStackBytes, Config::uiTaskPriority},
        {TaskConsole, consoleTaskEntry, "console", Config::consoleTaskStackBytes, Config::consoleTaskPriority},
    };
    for (const Spec &spec : specs)
    {
        // ESP-IDF takes the stack depth in bytes (vanilla FreeRTOS: words).
        const BaseType_t created = xTaskCreate(spec.entry, spec.name, spec.stackBytes, this,
                                               spec.priority, &tasks_[spec.index]);
        configASSERT(created == pdPASS);
    }
}

void App::gpsTaskEntry(void *self) { static_cast<App *>(self)->gpsLoop(); }
void App::displayTaskEntry(void *self) { static_cast<App *>(self)->displayLoop(); }
void App::loggerTaskEntry(void *self) { static_cast<App *>(self)->loggerLoop(); }
void App::uplinkTaskEntry(void *self) { static_cast<App *>(self)->uplinkLoop(); }
void App::uiTaskEntry(void *self) { static_cast<App *>(self)->uiLoop(); }
void App::consoleTaskEntry(void *self) { static_cast<App *>(self)->consoleLoop(); }

uint32_t App::nowMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void App::feedWatchdogFromModem()
{
    // The modem's longest single wait (GPRS attach, HTTP action) exceeds
    // the 5 s task watchdog; its read loop calls this between chunks.
    esp_task_wdt_reset();
}

// ---------------------------------------------------------------------------
// gps task: the only reader of UART1, runs the parser, FSM and log policy.

void App::gpsLoop()
{
    WatchdogSubscription watchdog;
    uint32_t lastSentenceMs = nowMs();
    bool silent = false;

    publishFix(parser_.fix()); // paints the boot screen

    while (true)
    {
        watchdog.feed();

        Settings::Values updated = {};
        if (xQueueReceive(gpsSettingsQueue_, &updated, 0) == pdTRUE)
        {
            policy_.setParams({updated.movingIntervalMs, updated.stationaryIntervalMs, updated.minDistanceM,
                               Config::Defaults::minSatellites, Config::Defaults::maxHdopCenti});
            uplinkSampleIntervalMs_ = updated.uplinkMinIntervalMs;
            speedMotion_.setParams({updated.motionStartCmPerS, updated.motionStopCmPerS,
                                    updated.motionStopDelayMs});
            accelMotion_.setParams({updated.motionStopDelayMs, Config::accelStartPulses});
            ESP_LOGI(logTag, "gps task applied new settings");
        }

        const char *line = nullptr;
        if (!gnss_.readSentence(line, Config::gpsReadTimeoutMs))
        {
            if (!silent && nowMs() - lastSentenceMs >= Config::gpsSilenceTimeoutMs)
            {
                silent = true;
                Diagnostics::increment(diagnostics_.gnssSilences);
                ESP_LOGW(logTag, "No NMEA for %" PRIu32 " ms: receiver unplugged or dead",
                         Config::gpsSilenceTimeoutMs);
                applyFsmEvent(TrackerEvent::GnssSilent);
                publishFix(parser_.fix());
            }
            continue;
        }

        lastSentenceMs = nowMs();
        silent = false;
        Diagnostics::increment(diagnostics_.sentencesReceived);
        applyFsmEvent(TrackerEvent::SentenceReceived);

        const NmeaParser::Sentence kind = parser_.parse(line);
        if (kind == NmeaParser::Sentence::Invalid)
        {
            Diagnostics::increment(diagnostics_.sentencesRejected);
            continue;
        }
        if (kind == NmeaParser::Sentence::Rmc)
        {
            onEpoch(parser_.fix(), lastSentenceMs);
        }
    }
}

void App::onEpoch(const Fix &fix, uint32_t nowMs)
{
    applyFsmEvent(fix.valid ? TrackerEvent::FixValid : TrackerEvent::FixLost);

    switch (motion_->update(fix, nowMs))
    {
    case MotionChange::Started: applyFsmEvent(TrackerEvent::MotionDetected); break;
    case MotionChange::Stopped: applyFsmEvent(TrackerEvent::MotionStopped); break;
    case MotionChange::None: break;
    }
    // The motion source reports changes and starts out "not moving", so a
    // tracker switched on while already parked would otherwise stay in
    // TRACKING, logging at the moving rate, until it had moved once and
    // stopped. Keep the FSM in step with what the source says now.
    if (fsm_.state() == TrackerState::Tracking && !motion_->isMoving())
    {
        applyFsmEvent(TrackerEvent::MotionStopped);
    }

    if (policy_.shouldLog(fix, nowMs, motion_->isMoving()))
    {
        policy_.markLogged(fix, nowMs);
        publishRecord(fix, nowMs);
    }

    publishFix(fix);

    if (sleepPolicy_.due(nowMs, uxQueueMessagesWaiting(uplinkQueue_) == 0))
    {
        enterSleep();
    }
}

void App::applyFsmEvent(TrackerEvent event)
{
    const TrackerState before = fsm_.state();
    if (fsm_.handle(event))
    {
        ESP_LOGI(logTag, "State %s -> %s", TrackerFsm::name(before), TrackerFsm::name(fsm_.state()));
        showStateOnLed(fsm_.state());
        sleepPolicy_.onState(fsm_.state(), nowMs());
    }
}

void App::publishRecord(const Fix &fix, uint32_t nowMs)
{
    const TrackRecord record = TrackRecord::from(fix, lastBatteryMv());
    enqueueDroppingOldest(logQueue_, record, diagnostics_.recordsDroppedLogQueue);

    // Moving, the policy produces a record a second and the modem delivers
    // one every uplinkMinIntervalMs at best; handing it every record would
    // only fill the queue and drop the oldest. Offer one per interval, and
    // while the link is up skip the offer when the modem is already behind:
    // a live map wants the newest point, not every point ten minutes late.
    // The card has them all. With the link down the queue buffers the
    // outage to its full depth.
    if (haveUplinkSample_ && nowMs - lastUplinkSampleMs_ < uplinkSampleIntervalMs_)
    {
        return;
    }
    if (uplink_->isConnected() && uxQueueMessagesWaiting(uplinkQueue_) >= Config::uplinkLiveBacklog)
    {
        return;
    }
    haveUplinkSample_ = true;
    lastUplinkSampleMs_ = nowMs;
    enqueueDroppingOldest(uplinkQueue_, record, diagnostics_.recordsDroppedUplinkQueue);
}

void App::publishFix(const Fix &fix)
{
    DisplayMessage message = {};
    message.kind = DisplayMessage::Kind::FixUpdate;
    message.fixUpdate.state = fsm_.state();
    message.fixUpdate.fix = fix;
    if (fix.valid)
    {
        // Smoothed for the eye only; records and uploads carry raw values.
        message.fixUpdate.fix.speedCmPerS =
            static_cast<uint32_t>(displaySpeed_.push(static_cast<int32_t>(fix.speedCmPerS)));
        message.fixUpdate.fix.altitudeCm = displayAltitude_.push(fix.altitudeCm);
    }
    postDisplay(message);
}

void App::showStateOnLed(TrackerState state)
{
    if (state == ledState_)
    {
        return;
    }
    ledState_ = state;
    const uint8_t b = Config::ledBrightness;
    esp_err_t err = ESP_OK;
    switch (state)
    {
    case TrackerState::Booting: err = led_.set(b, b, b); break;        // white
    case TrackerState::NoSignal: err = led_.set(b, 0, 0); break;       // red
    case TrackerState::Acquiring: err = led_.set(b, b / 2, 0); break;  // amber
    case TrackerState::Tracking: err = led_.set(0, b, 0); break;       // green
    case TrackerState::Stationary: err = led_.set(0, 0, b); break;     // blue
    case TrackerState::Sleeping: err = led_.off(); break;
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "LED update failed: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// display task: merges messages into a snapshot, redraws when it changed.

void App::displayLoop()
{
    WatchdogSubscription watchdog;
    StatusSnapshot snapshot = {};
    snapshot.state = TrackerState::Booting;
    snapshot.signalQuality = 99;
    uint8_t page = 0;
    bool dirty = true;
    uint32_t lastRedrawMs = 0;
    uint8_t failedFrames = 0;

    while (true)
    {
        watchdog.feed();

        // Wake early when a redraw is pending so the rate limit, not the
        // next message, decides when the screen refreshes.
        const uint32_t waitMs = dirty ? Config::displayMinRedrawMs : Config::queueWaitMs;
        DisplayMessage message = {};
        if (xQueueReceive(displayQueue_, &message, pdMS_TO_TICKS(waitMs)) == pdTRUE)
        {
            switch (message.kind)
            {
            case DisplayMessage::Kind::FixUpdate:
                snapshot.state = message.fixUpdate.state;
                snapshot.fix = message.fixUpdate.fix;
                snapshot.haveFix = message.fixUpdate.fix.valid;
                break;
            case DisplayMessage::Kind::Battery:
                snapshot.batteryMv = message.battery.millivolts;
                break;
            case DisplayMessage::Kind::StorageHealth:
                snapshot.storageReady = message.storage.ready;
                snapshot.recordsWritten = message.storage.recordsWritten;
                snapshot.recordsLost = message.storage.recordsLost;
                break;
            case DisplayMessage::Kind::UplinkHealth:
                snapshot.uplinkConnected = message.uplink.connected;
                snapshot.recordsUploaded = message.uplink.recordsUploaded;
                snapshot.uplinkFailures = message.uplink.failures;
                snapshot.signalQuality = message.uplink.signalQuality;
                break;
            case DisplayMessage::Kind::NextPage:
                page = static_cast<uint8_t>((page + 1) % StatusView::pageCount);
                break;
            }
            dirty = true;
        }

        if (!dirty || nowMs() - lastRedrawMs < Config::displayMinRedrawMs)
        {
            continue;
        }

        BusLock lock(i2cMutex_, Config::busLockTimeoutMs);
        if (!lock.locked())
        {
            Diagnostics::increment(diagnostics_.busTimeouts);
            continue; // stays dirty, retried next pass
        }
        const esp_err_t err = view_.render(snapshot, page);
        lastRedrawMs = nowMs();
        if (err != ESP_OK)
        {
            // A flaky wire: keep the last frame, count it, try again later.
            // After several in a row the controller itself may have lost
            // its configuration, so set it up again.
            Diagnostics::increment(diagnostics_.displayErrors);
            if (++failedFrames >= Config::displayReinitAfterFailures)
            {
                failedFrames = 0;
                ESP_LOGW(logTag, "Display not responding (%s); re-initialising", esp_err_to_name(err));
                display_.reinit();
            }
            continue;
        }
        failedFrames = 0;
        dirty = false;
    }
}

// ---------------------------------------------------------------------------
// logger task: the only writer to the card.

void App::loggerLoop()
{
    WatchdogSubscription watchdog;
    bool lastReady = storage_.isReady();

    // The card is mounted (or not) long before the first fix; tell the
    // display now instead of after the first record.
    publishStorageHealth();

    while (true)
    {
        watchdog.feed();

        TrackRecord record = {};
        const bool haveRecord =
            xQueueReceive(logQueue_, &record, pdMS_TO_TICKS(Config::queueWaitMs)) == pdTRUE;

        if (eventLog_.pending())
        {
            BusLock lock(sdMutex_, Config::busLockTimeoutMs);
            if (lock.locked())
            {
                eventLog_.flush(storage_, Config::eventLogFile);
            }
        }

        if (!haveRecord)
        {
            // Idle: a console 'mount' or a recovery elsewhere may have
            // changed the card state since the last record.
            const bool ready = storage_.isReady();
            if (ready != lastReady)
            {
                ESP_LOGI(logTag, "Storage %s", ready ? "ready" : "unavailable");
                lastReady = ready;
                publishStorageHealth();
            }
            continue;
        }

        esp_err_t err = ESP_ERR_TIMEOUT;
        {
            BusLock lock(sdMutex_, Config::busLockTimeoutMs);
            if (lock.locked())
            {
                err = logger_.log(record);
            }
            else
            {
                Diagnostics::increment(diagnostics_.busTimeouts);
            }
        }

        if (err == ESP_OK)
        {
            Diagnostics::increment(diagnostics_.recordsLogged);
        }
        else
        {
            Diagnostics::increment(diagnostics_.storageErrors);
            ESP_LOGW(logTag, "Record not written: %s", esp_err_to_name(err));
        }

        const bool ready = storage_.isReady();
        if (ready != lastReady)
        {
            ESP_LOGI(logTag, "Storage %s", ready ? "ready" : "unavailable");
            lastReady = ready;
        }
        publishStorageHealth();
    }
}

void App::publishStorageHealth()
{
    DisplayMessage message = {};
    message.kind = DisplayMessage::Kind::StorageHealth;
    message.storage.ready = storage_.isReady();
    message.storage.recordsWritten = logger_.written();
    message.storage.recordsLost = logger_.lost();
    postDisplay(message);
}

// ---------------------------------------------------------------------------
// uplink task: the only user of the modem.

void App::uplinkLoop()
{
    WatchdogSubscription watchdog;
    char url[Config::gsmUrlBytes];
    uint32_t failures = 0;
    // The task's own copy of the settings; the console publishes a new one
    // through uplinkSettingsQueue_ and it is applied here, never shared.
    Settings::Values live = values_;
    // 0 means "never sent": the first health report goes out as soon as the
    // bearer is up, so the server sees the device before the first fix.
    uint32_t lastHealthMs = 0;

    auto pollSettings = [&]()
    {
        Settings::Values updated = {};
        if (xQueueReceive(uplinkSettingsQueue_, &updated, 0) != pdTRUE)
        {
            return;
        }
        live = updated;
        scheduler_.setParams({live.uplinkMinIntervalMs, Config::uplinkBackoffInitialMs,
                              Config::uplinkBackoffMaxMs, Config::gsmFailuresBeforePowerCycle});
        uplink_->setApn(live.apn);
        ESP_LOGI(logTag, "uplink task applied new settings (url %s, id %s)", live.serverUrl, live.deviceId);
    };

    auto report = [&]()
    {
        DisplayMessage message = {};
        message.kind = DisplayMessage::Kind::UplinkHealth;
        message.uplink.connected = uplink_->isConnected();
        message.uplink.recordsUploaded = diagnostics_.recordsUploaded.load();
        message.uplink.failures = failures;
        {
            BusLock lock(modemMutex_, Config::busLockTimeoutMs);
            message.uplink.signalQuality = lock.locked() ? uplink_->signalQuality() : 99;
        }
        postDisplay(message);
    };

    // Every modem transaction runs under modemMutex_ so the console's raw
    // 'at' command can slip in between them. A timeout here only delays
    // the attempt; the lock is held by the console for one command at most.
    auto connectLocked = [&]() -> esp_err_t
    {
        BusLock lock(modemMutex_, Config::modemLockTimeoutMs);
        return lock.locked() ? uplink_->connect() : ESP_ERR_TIMEOUT;
    };

    while (!uplink_->isConnected())
    {
        watchdog.feed();
        pollSettings();
        if (connectLocked() != ESP_OK)
        {
            ++failures;
            scheduler_.onFailure(nowMs());
            report();
            sleepFeeding(watchdog, scheduler_.currentWaitMs());
        }
    }
    report();

    while (true)
    {
        watchdog.feed();

        pollSettings();
        if (!uplink_->isConnected())
        {
            // The bearer dropped (e.g. an APN change failed): back to the
            // connect loop with the scheduler's back-off.
            if (connectLocked() != ESP_OK)
            {
                ++failures;
                scheduler_.onFailure(nowMs());
                report();
                sleepFeeding(watchdog, scheduler_.currentWaitMs());
                continue;
            }
            report();
        }

        // What goes out this round: a health report when one is due, else
        // the oldest position waiting in the queue.
        const bool healthDue = lastHealthMs == 0 || nowMs() - lastHealthMs >= live.healthIntervalMs;
        TrackRecord record = {};
        bool sendingRecord = false;
        if (healthDue)
        {
            HealthSnapshot snapshot = {};
            if (xQueuePeek(healthQueue_, &snapshot, 0) != pdTRUE)
            {
                // The ui task has not published one yet (first seconds after
                // boot): try again next round.
                vTaskDelay(pdMS_TO_TICKS(Config::uiTickMs));
                continue;
            }
            snapshot.uplinkBacklog = static_cast<uint8_t>(uxQueueMessagesWaiting(uplinkQueue_));
            {
                BusLock lock(modemMutex_, Config::busLockTimeoutMs);
                snapshot.signalQuality = lock.locked() ? uplink_->signalQuality() : 99;
            }
            if (HealthReport::buildUrl(live.serverUrl, live.deviceId, Config::firmwareVersion,
                                       snapshot, url, sizeof url) == 0)
            {
                ESP_LOGW(logTag, "Health report does not fit in %u bytes, skipped",
                         static_cast<unsigned>(sizeof url));
                lastHealthMs = nowMs();
                continue;
            }
        }
        else
        {
            if (xQueuePeek(uplinkQueue_, &record, pdMS_TO_TICKS(Config::uplinkQueueWaitMs)) != pdTRUE)
            {
                continue;
            }
            if (OsmAndProtocol::buildUrl(live.serverUrl, live.deviceId, record, url, sizeof url) == 0)
            {
                // Cannot ever be sent: drop it rather than wedge the queue.
                xQueueReceive(uplinkQueue_, &record, 0);
                Diagnostics::increment(diagnostics_.recordsDroppedUplinkQueue);
                continue;
            }
            sendingRecord = true;
        }
        if (!scheduler_.canAttempt(nowMs()))
        {
            vTaskDelay(pdMS_TO_TICKS(Config::uiTickMs));
            continue;
        }

        uint16_t status = 0;
        esp_err_t err = ESP_ERR_TIMEOUT;
        {
            BusLock lock(modemMutex_, Config::modemLockTimeoutMs);
            if (lock.locked())
            {
                err = uplink_->httpGet(url, status);
            }
        }
        if (err == ESP_OK && status == 200)
        {
            if (sendingRecord)
            {
                // Pop only now: a record is removed from the queue only once
                // the server has acknowledged it.
                xQueueReceive(uplinkQueue_, &record, 0);
                Diagnostics::increment(diagnostics_.recordsUploaded);
            }
            else
            {
                lastHealthMs = nowMs();
            }
            scheduler_.onSuccess(nowMs());
        }
        else
        {
            ++failures;
            Diagnostics::increment(diagnostics_.uplinkFailures);
            if (err == ESP_OK && status != 0)
            {
                // The server answered: the link is fine, the data was not
                // wanted (Traccar says 400 for an unregistered id).
                scheduler_.onRejected(nowMs());
                ESP_LOGW(logTag, "Upload rejected (HTTP %u), next try in %" PRIu32 " ms",
                         status, scheduler_.currentWaitMs());
            }
            else
            {
                scheduler_.onFailure(nowMs());
                ESP_LOGW(logTag, "Upload failed (%s, HTTP %u), next try in %" PRIu32 " ms",
                         esp_err_to_name(err), status, scheduler_.currentWaitMs());
            }
            if (scheduler_.needsReset())
            {
                Diagnostics::increment(diagnostics_.modemPowerCycles);
                BusLock lock(modemMutex_, Config::modemLockTimeoutMs);
                if (lock.locked())
                {
                    uplink_->reset();
                }
                scheduler_.onReset();
            }
        }
        report();
    }
}

// ---------------------------------------------------------------------------
// ui task: button events, battery, periodic diagnostics.

void App::uiLoop()
{
    WatchdogSubscription watchdog;
    uint32_t lastBatteryMs = 0;
    uint32_t lastHealthMs = 0;
    uint32_t lastDiagnosticsMs = nowMs();

    while (true)
    {
        watchdog.feed();

        ButtonEdge edge = {};
        if (xQueueReceive(buttonEdgeQueue_, &edge, pdMS_TO_TICKS(Config::uiTickMs)) == pdTRUE)
        {
            switch (buttonDecoder_.feed(edge.pressed, edge.timestampUs))
            {
            case ButtonPress::Short:
            {
                DisplayMessage message = {};
                message.kind = DisplayMessage::Kind::NextPage;
                postDisplay(message);
                break;
            }
            case ButtonPress::Long:
                logDiagnostics();
                break;
            case ButtonPress::None:
                break;
            }
        }

        const uint32_t now = nowMs();
        if (lastBatteryMs == 0 || now - lastBatteryMs >= Config::batteryReadPeriodMs)
        {
            lastBatteryMs = now;
            uint32_t millivolts = 0;
            const esp_err_t err = battery_.readMillivolts(millivolts);
            if (err != ESP_OK)
            {
                Diagnostics::increment(diagnostics_.batteryReadErrors);
            }
            else
            {
                const uint16_t mv = static_cast<uint16_t>(millivolts > 65535 ? 65535 : millivolts);
                xQueueOverwrite(batteryQueue_, &mv);
                DisplayMessage message = {};
                message.kind = DisplayMessage::Kind::Battery;
                message.battery.millivolts = mv;
                postDisplay(message);
            }
        }
        if (now - lastHealthMs >= Config::healthPublishPeriodMs)
        {
            lastHealthMs = now;
            publishHealth();
        }

        if (now - lastDiagnosticsMs >= Config::diagnosticsPeriodMs)
        {
            lastDiagnosticsMs = now;
            logDiagnostics();
        }
    }
}

// ---------------------------------------------------------------------------
// console task: field settings without a rebuild (checklist 4.3).

void App::consoleLoop()
{
    WatchdogSubscription watchdog;
    // Working copy: 'set' edits it, 'save' persists and publishes it.
    Settings::Values draft = values_;
    char line[Config::consoleLineBytes];

    while (true)
    {
        watchdog.feed();
        if (!console_.readLine(line, sizeof line, Config::queueWaitMs))
        {
            continue;
        }

        const CommandParser::Parsed parsed = CommandParser::parse(line);
        switch (parsed.command)
        {
        case CommandParser::Command::None:
            break;
        case CommandParser::Command::Help:
            ESP_LOGI(logTag, "help | show | diag | defaults | save | mount | set <key> <value> | at <AT command>");
            ESP_LOGI(logTag, "keys: moving_ms stationary_ms min_distance_m motion_start motion_stop "
                             "motion_stop_delay_ms gnss(0=%s,1=%s) uplink(0/1) uplink_min_ms health_ms apn url id",
                     GnssProfiles::byIndex(0).name, GnssProfiles::byIndex(1).name);
            ESP_LOGI(logTag, "everything applies on save; only gnss needs a restart");
            break;
        case CommandParser::Command::Show:
            printSettings(draft);
            break;
        case CommandParser::Command::Diag:
            logDiagnostics();
            logCpuLoad();
            break;
        case CommandParser::Command::Defaults:
            draft = Settings::defaults();
            ESP_LOGI(logTag, "Draft reset to defaults (not saved)");
            break;
        case CommandParser::Command::Set:
            if (applySetting(draft, parsed.key, parsed.value))
            {
                ESP_LOGI(logTag, "%s = %s (draft, 'save' to apply)", parsed.key, parsed.value);
            }
            else
            {
                ESP_LOGW(logTag, "Unknown key or bad value: %s %s", parsed.key, parsed.value);
            }
            break;
        case CommandParser::Command::Save:
        {
            Settings::validate(draft);
            const esp_err_t err = settings_.save(draft);
            if (err != ESP_OK)
            {
                ESP_LOGE(logTag, "Save failed: %s", esp_err_to_name(err));
                break;
            }
            publishSettings(draft);
            ESP_LOGI(logTag, "Settings saved and applied");
            break;
        }
        case CommandParser::Command::Mount:
        {
            // Bench helper: retry the card without waiting for a record.
            BusLock lock(sdMutex_, Config::busLockTimeoutMs);
            if (!lock.locked())
            {
                ESP_LOGW(logTag, "Storage busy, try again");
                break;
            }
            const esp_err_t err = storage_.isReady() ? ESP_OK : storage_.recover();
            ESP_LOGI(logTag, "Storage %s (%s)", storage_.isReady() ? "ready" : "unavailable",
                     esp_err_to_name(err));
            publishStorageHealth();
            break;
        }
        case CommandParser::Command::At:
        {
            // Borrow the modem: the uplink task may be mid-transaction for
            // up to a connect attempt, so wait in slices and keep feeding.
            bool locked = false;
            for (uint8_t i = 0; i < 60 && !locked; ++i)
            {
                watchdog.feed();
                locked = xSemaphoreTake(modemMutex_, pdMS_TO_TICKS(1000)) == pdTRUE;
            }
            if (!locked)
            {
                ESP_LOGW(logTag, "Modem busy; try again");
                break;
            }
            static char response[Sim800Modem::responseCapacity];
            const AtCommand command = {parsed.value, "OK", 10'000};
            const esp_err_t err = modem_.execute(command, response, sizeof response);
            xSemaphoreGive(modemMutex_);
            for (char *c = response; *c != '\0'; ++c)
            {
                if (*c == '\r' || *c == '\n') *c = ' ';
            }
            ESP_LOGI(logTag, "%s -> %s [%s]", parsed.value, response, esp_err_to_name(err));
            break;
        }
        case CommandParser::Command::Unknown:
            ESP_LOGW(logTag, "Unknown command; type 'help'");
            break;
        }
    }
}

void App::printSettings(const Settings::Values &v) const
{
    ESP_LOGI(logTag, "moving_ms=%" PRIu32 " stationary_ms=%" PRIu32 " min_distance_m=%" PRIu32,
             v.movingIntervalMs, v.stationaryIntervalMs, v.minDistanceM);
    ESP_LOGI(logTag, "motion_start=%" PRIu32 " motion_stop=%" PRIu32 " motion_stop_delay_ms=%" PRIu32,
             v.motionStartCmPerS, v.motionStopCmPerS, v.motionStopDelayMs);
    ESP_LOGI(logTag, "gnss=%u (%s) uplink=%u uplink_min_ms=%" PRIu32 " health_ms=%" PRIu32, v.gnssProfile,
             GnssProfiles::byIndex(v.gnssProfile).name, v.uplinkEnabled ? 1 : 0, v.uplinkMinIntervalMs,
             v.healthIntervalMs);
    ESP_LOGI(logTag, "apn=%s url=%s id=%s", v.apn, v.serverUrl, v.deviceId);
}

bool App::applySetting(Settings::Values &v, const char *key, const char *value) const
{
    uint32_t number = 0;
    const bool numeric = CommandParser::parseUnsigned(value, number);

    if (strcmp(key, "moving_ms") == 0 && numeric) v.movingIntervalMs = number;
    else if (strcmp(key, "stationary_ms") == 0 && numeric) v.stationaryIntervalMs = number;
    else if (strcmp(key, "min_distance_m") == 0 && numeric) v.minDistanceM = number;
    else if (strcmp(key, "motion_start") == 0 && numeric) v.motionStartCmPerS = number;
    else if (strcmp(key, "motion_stop") == 0 && numeric) v.motionStopCmPerS = number;
    else if (strcmp(key, "motion_stop_delay_ms") == 0 && numeric) v.motionStopDelayMs = number;
    else if (strcmp(key, "gnss") == 0 && numeric && number < GnssProfiles::count)
        v.gnssProfile = static_cast<uint8_t>(number);
    else if (strcmp(key, "uplink") == 0 && numeric && number <= 1) v.uplinkEnabled = (number == 1);
    else if (strcmp(key, "uplink_min_ms") == 0 && numeric) v.uplinkMinIntervalMs = number;
    else if (strcmp(key, "health_ms") == 0 && numeric) v.healthIntervalMs = number;
    else if (strcmp(key, "apn") == 0) strlcpy(v.apn, value, sizeof v.apn);
    else if (strcmp(key, "url") == 0) strlcpy(v.serverUrl, value, sizeof v.serverUrl);
    else if (strcmp(key, "id") == 0) strlcpy(v.deviceId, value, sizeof v.deviceId);
    else return false;
    return true;
}

void App::publishSettings(const Settings::Values &values)
{
    xQueueOverwrite(gpsSettingsQueue_, &values);
    xQueueOverwrite(uplinkSettingsQueue_, &values);
}

// ---------------------------------------------------------------------------
// shared helpers

void App::postDisplay(const DisplayMessage &message)
{
    // The display is best-effort: a full queue means it is behind, and the
    // next update will carry newer data anyway.
    xQueueSend(displayQueue_, &message, 0);
}

uint16_t App::lastBatteryMv() const
{
    uint16_t millivolts = 0;
    xQueuePeek(batteryQueue_, &millivolts, 0);
    return millivolts;
}

void App::logDiagnostics()
{
    const Diagnostics &d = diagnostics_;
    ESP_LOGI(logTag,
             "nmea rx %" PRIu32 " rej %" PRIu32 " (cks %" PRIu32 " fld %" PRIu32 ") silences %" PRIu32
             " sky %u sig %u cn0 %u | "
             "log ok %" PRIu32 " drop %" PRIu32 " err %" PRIu32 " | "
             "up ok %" PRIu32 " drop %" PRIu32 " fail %" PRIu32 " cycles %" PRIu32 " | "
             "disp err %" PRIu32 " bus to %" PRIu32 " batt err %" PRIu32 " btn drop %" PRIu32
             " evt drop %" PRIu32 " | motion %s pulses %" PRIu32,
             d.sentencesReceived.load(), d.sentencesRejected.load(),
             parser_.checksumFailures(), parser_.fieldFailures(), d.gnssSilences.load(),
             parser_.fix().satellitesInView, parser_.fix().satellitesTracked, parser_.fix().maxSnrDbHz,
             d.recordsLogged.load(), d.recordsDroppedLogQueue.load(), d.storageErrors.load(),
             d.recordsUploaded.load(), d.recordsDroppedUplinkQueue.load(),
             d.uplinkFailures.load(), d.modemPowerCycles.load(),
             d.displayErrors.load(), d.busTimeouts.load(), d.batteryReadErrors.load(),
             button_.droppedEdges(), eventLog_.dropped(),
             motion_ == &accelMotion_ ? "bma400" : "gnss", accelMotion_.totalPulses());

    // Stack headroom in bytes (ESP-IDF reports words of one byte); the
    // sizes in Config are trimmed against these numbers.
    ESP_LOGI(logTag, "stack free: gps %u display %u logger %u uplink %u ui %u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskGps])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskDisplay])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskLogger])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskUplink])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskUi])));
}

// FreeRTOS run-time statistics: CPU time per task since boot, as a
// percentage of the total (both cores). The idle tasks' share is the
// headroom; the checklist asks for the application to stay under 70 %.
void App::logCpuLoad()
{
    static char table[1024]; // console task; printed, not kept
    vTaskGetRunTimeStats(table);
    ESP_LOGI(logTag, "CPU time per task (abs ticks, %% of total):\n%s", table);
}

void App::publishHealth()
{
    const HealthSnapshot s = buildHealthSnapshot();
    xQueueOverwrite(healthQueue_, &s);
}

HealthSnapshot App::buildHealthSnapshot()
{
    const Diagnostics &d = diagnostics_;
    const Fix &fix = parser_.fix();
    HealthSnapshot s = {};
    s.uptimeS = nowMs() / 1000;
    s.resetCause = resetCause_;
    s.state = fsm_.state();

    s.fixValid = fix.valid;
    s.satellites = fix.satellites;
    s.satellitesInView = fix.satellitesInView;
    s.satellitesTracked = fix.satellitesTracked;
    s.maxSnrDbHz = fix.maxSnrDbHz;
    s.quality = fix.quality;
    s.sentencesReceived = d.sentencesReceived.load();
    s.sentencesRejected = d.sentencesRejected.load();
    s.checksumFailures = parser_.checksumFailures();
    s.fieldFailures = parser_.fieldFailures();
    s.gnssSilences = d.gnssSilences.load();

    s.storageReady = storage_.isReady();
    s.recordsLogged = d.recordsLogged.load();
    s.recordsDroppedLogQueue = d.recordsDroppedLogQueue.load();
    s.storageErrors = d.storageErrors.load();
    s.storageRemounts = d.storageRemounts.load();

    s.recordsUploaded = d.recordsUploaded.load();
    s.recordsDroppedUplinkQueue = d.recordsDroppedUplinkQueue.load();
    s.uplinkFailures = d.uplinkFailures.load();
    s.modemPowerCycles = d.modemPowerCycles.load();
    s.signalQuality = 99; // the uplink task, the modem's only user, fills these two in
    s.uplinkBacklog = 0;

    s.displayErrors = d.displayErrors.load();
    s.busTimeouts = d.busTimeouts.load();
    s.batteryReadErrors = d.batteryReadErrors.load();
    s.buttonEdgesDropped = button_.droppedEdges();

    s.batteryMv = lastBatteryMv();
    s.freeHeapBytes = esp_get_free_heap_size();
    s.minFreeHeapBytes = esp_get_minimum_free_heap_size();
    static_assert(HealthSnapshot::taskCount == 5, "the stack list follows the task layout");
    const TaskIndex reported[HealthSnapshot::taskCount] = {TaskGps, TaskDisplay, TaskLogger, TaskUplink, TaskUi};
    for (size_t i = 0; i < HealthSnapshot::taskCount; ++i)
    {
        s.stackFreeBytes[i] = tasks_[reported[i]] != nullptr
                                  ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(tasks_[reported[i]]))
                                  : 0;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Deep sleep. Runs on the gps task, which owns the FSM and so knows when
// the tracker has been parked long enough. Each module is put into its own
// low-power state (its supply stays on), the wake sources are armed and the
// chip sleeps; the BMA400 stays awake as the alarm clock. Nothing returns
// from here: the next boot re-initialises everything from the top.

void App::enterSleep()
{
    applyFsmEvent(TrackerEvent::SleepRequested); // LED off, state logged
    ESP_LOGI(logTag, "Parked for %" PRIu32 " min: sleeping until motion or the button",
             Config::Defaults::sleepAfterStationaryMs / 60'000);

    // Wait for every bus to be free, then stop the other tasks: from here
    // on this task alone touches the hardware, and nothing is left waiting
    // on a mutex it would never get.
    takeForSleep(modemMutex_);
    takeForSleep(i2cMutex_);
    takeForSleep(sdMutex_);
    quiesceTasks();

    sendSleepReport();
    parkModem();
    parkReceiver();
    parkDisplay();
    parkStorage();
    armWakeSources();
    esp_deep_sleep_start();
}

// The owner may be mid-transfer (an HTTP request takes up to 30 s): wait
// in slices, feeding the watchdog. Nothing is released again; the chip
// sleeps with the locks held.
bool App::takeForSleep(SemaphoreHandle_t mutex)
{
    for (uint32_t waited = 0; waited < Config::sleepLockWaitMs; waited += 1000)
    {
        esp_task_wdt_reset();
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            return true;
        }
    }
    ESP_LOGW(logTag, "A bus stayed busy for %" PRIu32 " s; sleeping anyway", Config::sleepLockWaitMs / 1000);
    return false;
}

// Suspend the other application tasks and take them off the watchdog, so
// a task blocked on a bus we now hold cannot trip it while we park the
// hardware. Deep sleep ends them all anyway.
void App::quiesceTasks()
{
    const TaskIndex others[] = {TaskDisplay, TaskLogger, TaskUplink, TaskUi, TaskConsole};
    for (TaskIndex index : others)
    {
        if (tasks_[index] == nullptr)
        {
            continue;
        }
        esp_task_wdt_delete(tasks_[index]);
        vTaskSuspend(tasks_[index]);
    }
}

// Last health report with state "sleeping", so the dashboard shows a
// device that chose to go quiet rather than one that died.
void App::sendSleepReport()
{
    if (!uplink_->isConnected())
    {
        return;
    }
    HealthSnapshot snapshot = buildHealthSnapshot();
    snapshot.state = TrackerState::Sleeping;
    snapshot.signalQuality = uplink_->signalQuality();
    snapshot.uplinkBacklog = 0;

    static char url[Config::gsmUrlBytes]; // static: the gps task's stack is small, and we never return
    if (HealthReport::buildUrl(values_.serverUrl, values_.deviceId, Config::firmwareVersion, snapshot,
                               url, sizeof url) == 0)
    {
        return;
    }
    uint16_t status = 0;
    const esp_err_t err = uplink_->httpGet(url, status);
    ESP_LOGI(logTag, "Sleep report: %s, HTTP %u", esp_err_to_name(err), status);
}

void App::parkModem()
{
    const esp_err_t err = modem_.sleep();
    ESP_LOGI(logTag, "Modem to minimum functionality: %s", esp_err_to_name(err));
}

void App::parkReceiver()
{
    const esp_err_t err = gnss_.sleep();
    ESP_LOGI(logTag, "Receiver to backup mode: %s", esp_err_to_name(err));
}

void App::parkDisplay()
{
    const esp_err_t err = display_.sleep();
    ESP_LOGI(logTag, "Display off: %s", esp_err_to_name(err));
}

// Everything logged so far goes to the card, then the volume is unmounted
// so the FAT is consistent whatever happens while asleep.
void App::parkStorage()
{
    ESP_LOGI(logTag, "Card unmounted for sleep");
    eventLog_.flush(storage_, Config::eventLogFile);
    sdCard_.unmount();
}

// INT1 pulses high on activity: EXT1 wake on a high level, with the RTC
// pull-down holding the line low meanwhile. The button pulls GPIO2 low:
// EXT0 wake on a low level, with the RTC pull-up holding it high.
void App::armWakeSources()
{
    rtc_gpio_pullup_dis(Config::accelInterruptPin);
    rtc_gpio_pulldown_en(Config::accelInterruptPin);
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1ULL << Config::accelInterruptPin, ESP_EXT1_WAKEUP_ANY_HIGH));

    rtc_gpio_pulldown_dis(Config::buttonPin);
    rtc_gpio_pullup_en(Config::buttonPin);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(Config::buttonPin, 0));
}
