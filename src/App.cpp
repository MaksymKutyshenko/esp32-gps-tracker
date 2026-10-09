#include "App.hpp"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "config.hpp"
#include "periph/GnssProfile.hpp"
#include "reliability/BusLock.hpp"
#include "reliability/TaskWatchdog.hpp"
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
}

App::App()
    : values_(Settings::defaults()),
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
      button_(Config::buttonPin),
      led_(Config::statusLedPin),
      speedMotion_({Config::Defaults::motionStartCmPerS, Config::Defaults::motionStopCmPerS,
                    Config::Defaults::motionStopDelayMs}),
      motion_(&speedMotion_),
      policy_({Config::Defaults::movingIntervalMs, Config::Defaults::stationaryIntervalMs,
               Config::Defaults::minDistanceM}),
      buttonDecoder_(Config::buttonDebounceUs, Config::buttonLongPressUs),
      view_(display_),
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
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(logTag, "Boot: reset reason %s (%d)", resetReasonName(reason), static_cast<int>(reason));

    settings_.begin();
    loadSettings();
    createQueues();

    led_.begin();
    showStateOnLed(TrackerState::Booting);

    i2c_.begin();
    display_.begin();

    gpsUart_.begin();
    gnss_.begin(GnssProfiles::byIndex(values_.gnssProfile));

    gsmUart_.begin();
    modem_.begin();
    modem_.setIdleHook(feedWatchdogFromModem);

    sdCard_.begin();
    storage_.begin(); // a missing card is reported, not fatal

    battery_.begin();
    button_.begin(buttonEdgeQueue_);

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

    policy_.setParams({values_.movingIntervalMs, values_.stationaryIntervalMs, values_.minDistanceM});
    speedMotion_.setParams({values_.motionStartCmPerS, values_.motionStopCmPerS, values_.motionStopDelayMs});
    scheduler_.setParams({values_.uplinkMinIntervalMs, Config::uplinkBackoffInitialMs,
                          Config::uplinkBackoffMaxMs, Config::gsmFailuresBeforePowerCycle});
    uplink_ = values_.uplinkEnabled ? static_cast<IUplinkTransport *>(&modemTransport_)
                                    : static_cast<IUplinkTransport *>(&nullTransport_);
}

void App::createQueues()
{
    displayQueue_ = xQueueCreate(Config::displayQueueDepth, sizeof(DisplayMessage));
    logQueue_ = xQueueCreate(Config::logQueueDepth, sizeof(TrackRecord));
    uplinkQueue_ = xQueueCreate(Config::uplinkQueueDepth, sizeof(TrackRecord));
    buttonEdgeQueue_ = xQueueCreate(Config::buttonEdgeQueueDepth, sizeof(ButtonEdge));
    batteryQueue_ = xQueueCreate(1, sizeof(uint16_t));
    i2cMutex_ = xSemaphoreCreateMutex();
    sdMutex_ = xSemaphoreCreateMutex();
    configASSERT(displayQueue_ && logQueue_ && uplinkQueue_ && buttonEdgeQueue_ &&
                 batteryQueue_ && i2cMutex_ && sdMutex_);
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

    if (policy_.shouldLog(fix, nowMs, motion_->isMoving()))
    {
        policy_.markLogged(fix, nowMs);
        publishRecord(fix);
    }

    publishFix(fix);
}

void App::applyFsmEvent(TrackerEvent event)
{
    const TrackerState before = fsm_.state();
    if (fsm_.handle(event))
    {
        ESP_LOGI(logTag, "State %s -> %s", TrackerFsm::name(before), TrackerFsm::name(fsm_.state()));
        showStateOnLed(fsm_.state());
    }
}

void App::publishRecord(const Fix &fix)
{
    const TrackRecord record = TrackRecord::from(fix, lastBatteryMv());
    enqueueDroppingOldest(logQueue_, record, diagnostics_.recordsDroppedLogQueue);
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
            Diagnostics::increment(diagnostics_.displayErrors);
            continue;
        }
        dirty = false;
    }
}

// ---------------------------------------------------------------------------
// logger task: the only writer to the card.

void App::loggerLoop()
{
    WatchdogSubscription watchdog;
    bool lastReady = storage_.isReady();

    while (true)
    {
        watchdog.feed();

        TrackRecord record = {};
        if (xQueueReceive(logQueue_, &record, pdMS_TO_TICKS(Config::queueWaitMs)) != pdTRUE)
        {
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

        DisplayMessage message = {};
        message.kind = DisplayMessage::Kind::StorageHealth;
        message.storage.ready = ready;
        message.storage.recordsWritten = logger_.written();
        message.storage.recordsLost = logger_.lost();
        postDisplay(message);
    }
}

// ---------------------------------------------------------------------------
// uplink task: the only user of the modem.

void App::uplinkLoop()
{
    WatchdogSubscription watchdog;
    char url[Config::gsmCommandBytes];
    uint32_t failures = 0;

    auto report = [&]()
    {
        DisplayMessage message = {};
        message.kind = DisplayMessage::Kind::UplinkHealth;
        message.uplink.connected = uplink_->isConnected();
        message.uplink.recordsUploaded = diagnostics_.recordsUploaded.load();
        message.uplink.failures = failures;
        message.uplink.signalQuality = uplink_->signalQuality();
        postDisplay(message);
    };

    while (!uplink_->isConnected())
    {
        watchdog.feed();
        if (uplink_->connect() != ESP_OK)
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

        TrackRecord record = {};
        if (xQueuePeek(uplinkQueue_, &record, pdMS_TO_TICKS(Config::uplinkQueueWaitMs)) != pdTRUE)
        {
            continue;
        }
        if (!scheduler_.canAttempt(nowMs()))
        {
            vTaskDelay(pdMS_TO_TICKS(Config::uiTickMs));
            continue;
        }

        if (OsmAndProtocol::buildUrl(values_.serverUrl, values_.deviceId, record, url, sizeof url) == 0)
        {
            // Cannot ever be sent: drop it rather than wedge the queue.
            xQueueReceive(uplinkQueue_, &record, 0);
            Diagnostics::increment(diagnostics_.recordsDroppedUplinkQueue);
            continue;
        }

        uint16_t status = 0;
        const esp_err_t err = uplink_->httpGet(url, status);
        if (err == ESP_OK && status == 200)
        {
            // Pop only now: a record is removed from the queue only once
            // the server has acknowledged it.
            xQueueReceive(uplinkQueue_, &record, 0);
            Diagnostics::increment(diagnostics_.recordsUploaded);
            scheduler_.onSuccess(nowMs());
        }
        else
        {
            ++failures;
            Diagnostics::increment(diagnostics_.uplinkFailures);
            scheduler_.onFailure(nowMs());
            ESP_LOGW(logTag, "Upload failed (%s, HTTP %u), next try in %" PRIu32 " ms",
                     esp_err_to_name(err), status, scheduler_.currentWaitMs());
            if (scheduler_.needsReset())
            {
                Diagnostics::increment(diagnostics_.modemPowerCycles);
                uplink_->reset();
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

        if (now - lastDiagnosticsMs >= Config::diagnosticsPeriodMs)
        {
            lastDiagnosticsMs = now;
            logDiagnostics();
        }
    }
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
             "nmea rx %" PRIu32 " rej %" PRIu32 " silences %" PRIu32 " | "
             "log ok %" PRIu32 " drop %" PRIu32 " err %" PRIu32 " | "
             "up ok %" PRIu32 " drop %" PRIu32 " fail %" PRIu32 " cycles %" PRIu32 " | "
             "disp err %" PRIu32 " bus to %" PRIu32 " batt err %" PRIu32 " btn drop %" PRIu32,
             d.sentencesReceived.load(), d.sentencesRejected.load(), d.gnssSilences.load(),
             d.recordsLogged.load(), d.recordsDroppedLogQueue.load(), d.storageErrors.load(),
             d.recordsUploaded.load(), d.recordsDroppedUplinkQueue.load(),
             d.uplinkFailures.load(), d.modemPowerCycles.load(),
             d.displayErrors.load(), d.busTimeouts.load(), d.batteryReadErrors.load(),
             button_.droppedEdges());

    // Stack headroom in bytes (ESP-IDF reports words of one byte); the
    // sizes in Config are trimmed against these numbers.
    ESP_LOGI(logTag, "stack free: gps %u display %u logger %u uplink %u ui %u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskGps])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskDisplay])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskLogger])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskUplink])),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(tasks_[TaskUi])));
}
