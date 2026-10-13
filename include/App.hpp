#pragma once

#include <stdint.h>

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_system.h"

#include "logic/ButtonDecoder.hpp"
#include "logic/Calendar.hpp"
#include "logic/EmaFilter.hpp"
#include "logic/LogPolicy.hpp"
#include "logic/MotionDetector.hpp"
#include "logic/NmeaParser.hpp"
#include "logic/SleepPolicy.hpp"
#include "logic/TrackRecord.hpp"
#include "logic/TrackerFsm.hpp"
#include "periph/BatteryMonitor.hpp"
#include "periph/Bma400.hpp"
#include "periph/Button.hpp"
#include "periph/ConsoleInput.hpp"
#include "periph/GnssReceiver.hpp"
#include "periph/I2cBus.hpp"
#include "periph/SdCard.hpp"
#include "periph/Sim800Modem.hpp"
#include "periph/Ssd1306.hpp"
#include "periph/StatusLed.hpp"
#include "periph/UartPort.hpp"
#include "reliability/Diagnostics.hpp"
#include "reliability/EventLog.hpp"
#include "reliability/Settings.hpp"
#include "transport/DisplayMessage.hpp"
#include "transport/HealthReport.hpp"
#include "transport/IUplinkTransport.hpp"
#include "transport/ModemTransport.hpp"
#include "transport/SdStorage.hpp"
#include "transport/StatusView.hpp"
#include "transport/TrackLogger.hpp"
#include "transport/UplinkScheduler.hpp"

// GPS tracker composition root. Constructs every object once, wires the
// strategies chosen by Settings, and owns the FreeRTOS tasks and the
// queues between them:
//
//   gps      UART1 -> NmeaParser -> TrackerFsm/LogPolicy
//            -> displayQueue (FixUpdate), logQueue, uplinkQueue, LED
//   display  displayQueue -> StatusSnapshot -> StatusView -> SSD1306 (I2C, BusLock)
//   logger   logQueue -> TrackLogger -> SdStorage (SPI, BusLock)
//   uplink   uplinkQueue (peek, pop on success) -> OsmAnd URL -> IUplinkTransport
//   ui       button edges (ISR queue) -> ButtonDecoder; battery; diagnostics
//   console  serial lines -> CommandParser -> Settings (NVS) and, through
//            single-slot queues, the live parameters of the gps and uplink tasks
//
// Tasks exchange values through queues only. The deep queues drop their
// oldest record and count it when a consumer falls behind; the producer
// never blocks. Every task feeds the task watchdog once per loop.
//
// run() starts the tasks and returns; the object must outlive them (main
// keeps it static).
class App
{
public:
    App();

    void run();

private:
    enum TaskIndex : uint8_t
    {
        TaskGps,
        TaskDisplay,
        TaskLogger,
        TaskUplink,
        TaskUi,
        TaskConsole,
        TaskCount
    };

    void begin();
    void loadSettings();
    void createQueues();
    void startTasks();

    [[noreturn]] void gpsLoop();
    [[noreturn]] void displayLoop();
    [[noreturn]] void loggerLoop();
    [[noreturn]] void uplinkLoop();
    [[noreturn]] void uiLoop();
    [[noreturn]] void consoleLoop();

    static void gpsTaskEntry(void *self);
    static void displayTaskEntry(void *self);
    static void loggerTaskEntry(void *self);
    static void uplinkTaskEntry(void *self);
    static void uiTaskEntry(void *self);
    static void consoleTaskEntry(void *self);

    // gps task helpers
    void onEpoch(const Fix &fix, uint32_t nowMs);
    void applyFsmEvent(TrackerEvent event);
    // Every record goes to the card; the uplink gets one per upload
    // interval, so its queue buffers outages instead of overflowing on
    // every drive (gps task only).
    void publishRecord(const Fix &fix, uint32_t nowMs);
    void publishFix(const Fix &fix);
    // Sets the system clock (log timestamps, event log) from the receiver's UTC.
    void syncClock(const UtcTime &utc);
    void showStateOnLed(TrackerState state);

    // console helpers
    void printSettings(const Settings::Values &values) const;
    bool applySetting(Settings::Values &values, const char *key, const char *value) const;
    void applyLiveSettings(const Settings::Values &values);
    void publishSettings(const Settings::Values &values);

    // shared helpers
    void postDisplay(const DisplayMessage &message);
    // Card state and record counters for the status page.
    void publishStorageHealth();
    uint16_t lastBatteryMv() const;
    void logDiagnostics();
    void logCpuLoad();
    // Fills a HealthSnapshot from the counters and overwrites healthQueue_.
    void publishHealth();
    HealthSnapshot buildHealthSnapshot();

    // Deep sleep (gps task): parks every module, arms the wake sources and
    // never returns; the next boot starts from the top.
    [[noreturn]] void enterSleep();
    bool takeForSleep(SemaphoreHandle_t mutex);
    void quiesceTasks();
    void sendSleepReport();
    void parkModem();
    void parkReceiver();
    void parkDisplay();
    void parkStorage();
    void armWakeSources();
    static uint32_t nowMs();
    static void feedWatchdogFromModem();

    Settings settings_;
    Settings::Values values_;
    Diagnostics diagnostics_;
    EventLog eventLog_;

    // Periph
    UartPort gpsUart_;
    GnssReceiver gnss_;
    UartPort gsmUart_;
    Sim800Modem modem_;
    I2cBus i2c_;
    Ssd1306 display_;
    SdCard sdCard_;
    BatteryMonitor battery_;
    Bma400 accel_;
    Button button_;
    StatusLed led_;
    ConsoleInput console_;

    // Logic
    NmeaParser parser_;
    TrackerFsm fsm_;
    SpeedMotionDetector speedMotion_;
    AccelMotionDetector accelMotion_;
    IMotionDetector *motion_;
    LogPolicy policy_;
    SleepPolicy sleepPolicy_;
    bool clockSynced_ = false;
    TimeZone timeZone_;
    uint32_t uplinkSampleIntervalMs_ = 0;
    uint32_t lastUplinkSampleMs_ = 0;
    bool haveUplinkSample_ = false;
    EmaFilter<int32_t, 1, 4> displaySpeed_;
    EmaFilter<int32_t, 1, 4> displayAltitude_;
    ButtonDecoder buttonDecoder_;

    // Transport
    StatusView view_;
    SdStorage storage_;
    TrackLogger logger_;
    ModemTransport modemTransport_;
    NullUplinkTransport nullTransport_;
    IUplinkTransport *uplink_;
    UplinkScheduler scheduler_;

    // RTOS plumbing
    QueueHandle_t displayQueue_ = nullptr;
    QueueHandle_t logQueue_ = nullptr;
    QueueHandle_t uplinkQueue_ = nullptr;
    QueueHandle_t buttonEdgeQueue_ = nullptr;
    QueueHandle_t batteryQueue_ = nullptr;  // single slot, latest millivolts
    QueueHandle_t healthQueue_ = nullptr;   // single slot, latest HealthSnapshot
    // Single slot each: the console publishes a whole Settings::Values and
    // the gps / uplink tasks apply it at the top of their loops, so live
    // parameters change without any shared object between tasks.
    QueueHandle_t gpsSettingsQueue_ = nullptr;
    QueueHandle_t uplinkSettingsQueue_ = nullptr;
    SemaphoreHandle_t i2cMutex_ = nullptr;
    SemaphoreHandle_t sdMutex_ = nullptr;
    // The modem has one owner (the uplink task); the console's raw 'at'
    // command borrows it between the task's own transactions.
    SemaphoreHandle_t modemMutex_ = nullptr;
    TaskHandle_t tasks_[TaskCount] = {};

    TrackerState ledState_ = TrackerState::Sleeping; // forces the first LED write
    ResetCause resetCause_ = ResetCause::Other;
    // Tasks named by the watchdog before a task-watchdog reset, else "".
    char resetDetail_[HealthSnapshot::resetDetailBytes] = {};
};
