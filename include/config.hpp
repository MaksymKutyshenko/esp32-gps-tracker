#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "hal/adc_types.h"

// Pins, bus parameters and task layout for the GPS tracker. Every constant
// the firmware tunes lives here; driver code never carries a literal pin.
// The wiring rationale is in docs/HARDWARE.md — keep the two in step.
//
// Two boards are supported through the build environment in platformio.ini:
//   -DBOARD_SUPERMINI  ESP32-S3 SuperMini (primary)
//   -DBOARD_DEVKITC    ESP32-S3 DevKitC-1 (fallback)
// GPIO1..13 are on both headers, so only the GSM UART and the status LED
// differ. Kept off on both: GPIO0 (BOOT), GPIO3 (JTAG strapping), GPIO19/20
// (USB), GPIO26..32 (flash), GPIO45/46 (strapping). Development happens on
// the DevKitC with the console on UART0 (CP2102 port); the SuperMini build
// moves the console to USB Serial/JTAG so its UART0 pins are free.
#if !defined(BOARD_SUPERMINI) && !defined(BOARD_DEVKITC)
#error "Select a board: build with -DBOARD_SUPERMINI or -DBOARD_DEVKITC (see platformio.ini)"
#endif

namespace Config
{
    // Reported in every health report so the web app can tell which build
    // produced a number.
    constexpr const char *firmwareVersion = "0.2.0";

    // ---- Battery -------------------------------------------------------
    // Cell voltage from the shield's B+ through a 100 k / 100 k divider:
    // 4.2 V at the cell is 2.1 V at the pin, inside the 12 dB range.
    constexpr gpio_num_t batteryAdcPin = GPIO_NUM_1;
    constexpr adc_unit_t batteryAdcUnit = ADC_UNIT_1;
    constexpr adc_channel_t batteryAdcChannel = ADC_CHANNEL_0; // GPIO1
    constexpr adc_atten_t batteryAdcAttenuation = ADC_ATTEN_DB_12;
    constexpr uint32_t batteryDividerRatio = 2;

    // ---- Button --------------------------------------------------------
    // To GND, internal pull-up. GPIO interrupt feeds the UI queue; RTC GPIO
    // so it can also wake the chip from deep sleep.
    constexpr gpio_num_t buttonPin = GPIO_NUM_2;
    constexpr int64_t buttonDebounceUs = 20'000;

    // ---- GNSS receiver (UART1) ------------------------------------------
    // Foxeer M10Q-250 defaults to 115200 baud; the Beitian BE-220 to 9600.
    // Set this to the module in use until autodetection exists.
    constexpr uart_port_t gpsUart = UART_NUM_1;
    constexpr gpio_num_t gpsRxPin = GPIO_NUM_5; // <- GNSS TX
    constexpr gpio_num_t gpsTxPin = GPIO_NUM_4; // -> GNSS RX
    // uart_config_t.baud_rate and uart_driver_install()'s buffer size are
    // bare ints in the IDF API; the UART wrapper casts at the call, the way
    // SpiBus does for clock_speed_hz, so the constants stay fixed-width.
    constexpr uint32_t gpsBaudRate = 115200;
    // A 10 Hz receiver emits up to ~1 KiB of NMEA per burst; two bursts of
    // slack keep a late reader from dropping bytes.
    constexpr size_t gpsRxBufferBytes = 2048;
    // Longest NMEA sentence is 82 characters including CR LF.
    constexpr size_t gpsLineBytes = 96;
    // No sentence for this long means the receiver is dead or unplugged.
    constexpr uint32_t gpsSilenceTimeoutMs = 5000;
    // The receiver answers a configuration frame within milliseconds.
    constexpr uint32_t gpsAckTimeoutMs = 300;
    constexpr uint32_t gpsWakeDelayMs = 100; // after the wake byte, before the setup frames

    // ---- Accelerometer (BMA400 on the CJMCU-400 breakout) --------------
    constexpr gpio_num_t accelInterruptPin = GPIO_NUM_6; // INT1, RTC GPIO
    constexpr uint8_t accelAddress = 0x14;               // 0x15 if SDO high; both are probed
    // Activity = a change between consecutive 100 Hz samples above this
    // on any axis, for this many samples in a row. Consecutive samples are
    // 10 ms apart, so even a hand lifting the tracker changes by only a
    // few tens of mg per sample: 40 mg over 2 samples (Bosch's reference
    // setting) catches that and a car pulling away, while sensor noise
    // (a few mg) and the 24 mg hysteresis keep a parked tracker silent.
    constexpr uint16_t accelActivityThresholdMg = 40;    // 8 mg steps
    constexpr uint16_t accelActivityDurationSamples = 2;
    // Pulses within one second needed to leave "parked": a desk bump or
    // typing nearby gives 1-2 (seen on the bench), lifting the tracker or
    // a car pulling away gives dozens. Once moving, any pulse keeps it so.
    constexpr uint32_t accelStartPulses = 5;

    // ---- GSM modem (SIM800C, UART2) -------------------------------------
    // PWRKEY ("BOOT" on the BIGGA GSM silkscreen): open-drain, held low
    // >= 1 s to toggle the modem's power. The board has no auto power-on
    // circuit, so the firmware pulses this when the modem stays silent.
    constexpr gpio_num_t gsmPowerKeyPin = GPIO_NUM_7;
    constexpr uart_port_t gsmUart = UART_NUM_2;
#if defined(BOARD_SUPERMINI)
    // UART0's pins are unused on the SuperMini (console is USB Serial/JTAG).
    constexpr gpio_num_t gsmTxPin = GPIO_NUM_43; // -> SIM800C RXD
    constexpr gpio_num_t gsmRxPin = GPIO_NUM_44; // <- SIM800C TXD
#else
    // On the DevKitC GPIO43/44 are wired to the CP2102, so use UART1's
    // homework pins instead.
    constexpr gpio_num_t gsmTxPin = GPIO_NUM_17;
    constexpr gpio_num_t gsmRxPin = GPIO_NUM_18;
#endif
    // The modem is driven at this rate; breakout boards ship either in
    // autobaud mode or fixed at 9600, so power-on probes the candidates in
    // order and then pins the modem to gsmBaudRate with AT+IPR.
    constexpr uint32_t gsmBaudRate = 115200;
    constexpr uint32_t gsmProbeBaudRates[] = {115200, 9600, 57600, 38400};
    constexpr size_t gsmRxBufferBytes = 2048;

    // ---- I2C bus: OLED + accelerometer (+ GNSS compass) ----------------
    constexpr i2c_port_num_t i2cPort = I2C_NUM_0;
    constexpr gpio_num_t i2cSdaPin = GPIO_NUM_8;
    constexpr gpio_num_t i2cSclPin = GPIO_NUM_9;
    // Breadboard wiring with module pull-ups only: stay at 100 kHz (the
    // 1 KiB OLED frame NACKed at 400 kHz in module 4.3).
    constexpr uint32_t i2cSpeedHz = 100'000;
    // i2c_master_transmit() takes the timeout as a bare int of milliseconds;
    // the I2C wrapper casts at the call.
    constexpr uint32_t i2cTimeoutMs = 100;
    constexpr uint8_t displayAddress = 0x3C;
    constexpr uint8_t compassAddress = 0x0D; // QMC5883L on the Foxeer

    // ---- MicroSD (SPI2, DMA) --------------------------------------------
    // The adapter is the 5 V type (onboard regulator + level shifter); its
    // VCC goes to the 5 V rail, the signals are 3.3 V.
    // SCK and MOSI are swapped from SPI2's IOMUX pins (12/11) to make the
    // breadboard wiring straight (10-11-12-13 = CS-SCK-MOSI-MISO); they go
    // through the GPIO matrix, which is fine at the 10 MHz used here.
    constexpr spi_host_device_t sdSpiHost = SPI2_HOST;
    constexpr gpio_num_t sdSclkPin = GPIO_NUM_11;
    constexpr gpio_num_t sdMosiPin = GPIO_NUM_12;
    constexpr gpio_num_t sdMisoPin = GPIO_NUM_13;
    constexpr gpio_num_t sdCsPin = GPIO_NUM_10;
    // SD cards initialise at <= 400 kHz; the sdspi host handles that itself.
    // 10 MHz keeps the edges clean on jumper wires.
    constexpr uint32_t sdSpiHz = 10'000'000;

    // ---- Status LED (onboard WS2812) -----------------------------------
#if defined(BOARD_SUPERMINI)
    constexpr gpio_num_t statusLedPin = GPIO_NUM_48;
#else
    // DevKitC-1 v1.1; v1.0 boards use GPIO48.
    constexpr gpio_num_t statusLedPin = GPIO_NUM_38;
#endif

    // ---- Modem timing --------------------------------------------------
    constexpr uint32_t gsmPowerKeyPulseMs = 1200;   // PWRKEY low >= 1 s toggles power
    constexpr uint32_t gsmBootWaitMs = 4000;        // network registration starts after this
    // After a cold boot the network takes a few more seconds to accept the
    // registration; a GPRS attach asked before that is refused.
    constexpr uint32_t gsmRegistrationWaitMs = 20'000;
    constexpr uint8_t gsmPowerOnAttempts = 3;
    constexpr uint32_t gsmHttpTimeoutMs = 30'000;
    // Longest report URL: server URL (95) + device id (23) + the health
    // query with its 36 fields, every counter at seven digits (~410; the
    // position report is ~130). The AT line wraps it in
    // AT+HTTPPARA="URL","..." which adds 20 more; both are sized so no
    // report is refused as too long (the host test proves the fit). The
    // SIM800C accepts URLs well past this length. Both buffers live on
    // the uplink task's stack, which is sized for them.
    constexpr size_t gsmUrlBytes = 640;
    constexpr size_t gsmCommandBytes = gsmUrlBytes + 24;
    // Consecutive uplink failures before the modem is power-cycled.
    constexpr uint8_t gsmFailuresBeforePowerCycle = 3;
    // Exponential back-off between failed uplink attempts, capped.
    constexpr uint32_t uplinkBackoffInitialMs = 5000;
    constexpr uint32_t uplinkBackoffMaxMs = 300'000;

    // ---- Storage -------------------------------------------------------
    constexpr const char *sdMountPoint = "/sdcard";
    // FAT without long-file-name support: 8.3 names only.
    constexpr const char *trackDirectory = "/sdcard/TRACKS";
    // The firmware's own log, mirrored line by line (see EventLog).
    constexpr const char *eventLogFile = "/sdcard/TRACKS/EVENTS.LOG";
    constexpr UBaseType_t eventLogDepth = 48; // lines held between two flushes
    constexpr uint8_t storageRetries = 3;
    constexpr size_t trackLineBytes = 128;

    // ---- NVS -----------------------------------------------------------
    constexpr const char *nvsNamespace = "tracker";
    constexpr const char *nvsSettingsKey = "settings";

    // First-boot values for the Settings blob; the working copy lives in
    // NVS and can be edited on the console, but these are the numbers the
    // tracker is designed around. Bump Settings::layoutVersion when they
    // change so boards with an older saved blob pick them up.
    namespace Defaults
    {
        // Recording, sized for a car. The receiver reports once a second,
        // so while moving every position is recorded: at 90 km/h that is
        // one point per 25 m, enough to draw a roundabout. The distance
        // trigger only matters if the moving interval is raised again.
        // Parked, one heartbeat a minute proves the device is alive
        // without filling the card.
        constexpr uint32_t movingIntervalMs = 1000;    // every receiver epoch
        constexpr uint32_t stationaryIntervalMs = 60'000;
        constexpr uint32_t minDistanceM = 20;
        // A fix is recorded only with this many satellites in the solution
        // and HDOP at or below this value: at a window a 4-satellite fix
        // with HDOP 5 wanders 50 m and would be logged as a walk.
        constexpr uint8_t minSatellites = 5;
        constexpr uint16_t maxHdopCenti = 300; // HDOP 3.0
        // 150 cm/s = 5.4 km/h, 50 cm/s = 1.8 km/h, 30 s below that = parked.
        constexpr uint32_t motionStartCmPerS = 150;
        constexpr uint32_t motionStopCmPerS = 50;
        constexpr uint32_t motionStopDelayMs = 30'000;
        constexpr uint8_t gnssProfile = 0; // GnssProfiles::foxeerM10q250
        constexpr bool uplinkEnabled = true;
        // Live trail: one upload per 5 s. Each upload is one HTTP request
        // over 2G and takes 2-5 s, so this is close to what the modem can
        // sustain; the card keeps every point, the map one in five.
        constexpr uint32_t uplinkMinIntervalMs = 5000;
        // Deep sleep after this long parked (Stationary with nothing left
        // to upload): modem to minimum functionality, GNSS to backup mode,
        // display off, ESP32 in deep sleep until the BMA400 reports motion
        // or the button is pressed. 0 disables sleep. The battery shield's
        // own converter still draws a few mA, so the gain is days of
        // standby, not the ESP32's microamps alone.
        // Bench value while the sleep cycle is being verified; 10 min for use.
        constexpr uint32_t sleepAfterStationaryMs = 1 * 60'000;
        // Health report (counters, sky view, card, modem, battery, heap)
        // once a minute, fix or no fix: this is how the web app knows the
        // device is alive and which module is in trouble.
        constexpr uint32_t healthIntervalMs = 60'000;
        // Kyivstar prepaid; contract SIMs use www.kyivstar.net, Vodafone UA and
        // lifecell use "internet". Overridable with `set apn` on the console.
        constexpr const char *apn = "www.ab.kyivstar.net";
        // The project's own server (web/ on branch tracker-web, deployed on
        // Fly.io). Plain HTTP on port 80, no port in the URL: the SIM800C
        // has no usable TLS. It takes the OsmAnd position report and the
        // health report. Traccar works too: "http://demo.traccar.org:5055".
        constexpr const char *serverUrl = "http://gps-tracker-mk.fly.dev";
        constexpr const char *deviceId = "gps-tracker-01";
    }

    // ---- UI ------------------------------------------------------------
    // Clock on the screen only; the card and the server keep UTC. Kyiv is
    // UTC+2 with European summer time (UTC+3 from the last Sunday of March
    // to the last Sunday of October).
    constexpr int32_t timeZoneStandardOffsetMinutes = 120;
    constexpr bool timeZoneEuropeanSummerTime = true;
    constexpr int64_t buttonLongPressUs = 1'000'000;
    constexpr uint32_t batteryReadPeriodMs = 10'000;
    constexpr uint32_t diagnosticsPeriodMs = 60'000;
    // How often the ui task refreshes the HealthSnapshot the uplink task
    // reports from; a report is therefore at most this stale.
    constexpr uint32_t healthPublishPeriodMs = 10'000;
    // The display redraws at most this often even if updates arrive faster
    // (a 10 Hz receiver would otherwise flush 1 KiB over I2C ten times a second).
    constexpr uint32_t displayMinRedrawMs = 200;
    // Consecutive failed frames before the controller is re-initialised: a
    // bus glitch can leave the SSD1306 ignoring data until it is set up again.
    constexpr uint8_t displayReinitAfterFailures = 5;
    constexpr uint8_t ledBrightness = 24; // out of 255: visible, not blinding

    // ---- Queues and locks ----------------------------------------------
    // Depths are in records. The single-slot queues overwrite; the deep
    // ones drop the oldest record and count it rather than block the
    // producer.
    constexpr UBaseType_t displayQueueDepth = 8;
    constexpr UBaseType_t logQueueDepth = 32;
    constexpr UBaseType_t uplinkQueueDepth = 64;
    // While the link is up the queue is kept this short, so the map shows
    // the latest position rather than one from ten minutes ago when the
    // modem delivers fewer requests than are offered (a 2G request takes
    // 5-15 s). While the link is down the whole depth buffers the outage.
    constexpr UBaseType_t uplinkLiveBacklog = 3;
    constexpr UBaseType_t buttonEdgeQueueDepth = 8;
    constexpr uint32_t busLockTimeoutMs = 200;
    // A modem transaction (connect attempt, HTTP request) can take tens of
    // seconds; the uplink task waits this long for the console to release it.
    constexpr uint32_t modemLockTimeoutMs = 15'000;
    // Before sleeping: how long to wait for the display or the card's owner
    // to finish its current transfer (a card write with fsync can take a
    // few hundred ms).
    constexpr uint32_t sleepLockWaitMs = 5000;

    // ---- Task layout ---------------------------------------------------
    // One task per blocking peripheral; priorities put the receiver first
    // so no NMEA byte is lost, the ui next so a press is never missed, and
    // the slow I/O (display flush, card write, modem dialogue) below them.
    // Stack sizes are in bytes (ESP-IDF); the ui task logs every task's
    // high-water mark once a minute so these can be trimmed from a
    // measurement. Periods stay multiples of 10 ms (100 Hz tick).
    constexpr uint32_t gpsTaskStackBytes = 6144; // the sleep sequence (health URL, AT) runs here
    constexpr UBaseType_t gpsTaskPriority = 5;
    constexpr uint32_t gpsReadTimeoutMs = 1000;

    constexpr uint32_t uiTaskStackBytes = 4096;
    constexpr UBaseType_t uiTaskPriority = 4;
    constexpr uint32_t uiTickMs = 100;

    constexpr uint32_t displayTaskStackBytes = 4096;
    constexpr UBaseType_t displayTaskPriority = 3;

    constexpr uint32_t loggerTaskStackBytes = 6144; // FATFS works on the caller's stack
    constexpr UBaseType_t loggerTaskPriority = 3;

    constexpr uint32_t uplinkTaskStackBytes = 7168; // holds the URL and the AT line
    constexpr UBaseType_t uplinkTaskPriority = 2;
    constexpr uint32_t uplinkQueueWaitMs = 1000;

    constexpr uint32_t consoleTaskStackBytes = 4096;
    constexpr UBaseType_t consoleTaskPriority = 1;
    constexpr size_t consoleLineBytes = 128;

    // Every blocking wait inside a task stays below the 5 s task watchdog.
    constexpr uint32_t queueWaitMs = 1000;
}
