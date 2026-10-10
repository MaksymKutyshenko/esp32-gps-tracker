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
    constexpr gpio_num_t gpsRxPin = GPIO_NUM_4; // <- GNSS TX
    constexpr gpio_num_t gpsTxPin = GPIO_NUM_5; // -> GNSS RX
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

    // ---- Accelerometer (BMA400, optional until delivered) --------------
    constexpr gpio_num_t accelInterruptPin = GPIO_NUM_6; // INT1, RTC GPIO
    constexpr uint8_t accelAddress = 0x14;               // 0x15 if SDO high

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
    constexpr uint8_t gsmPowerOnAttempts = 3;
    constexpr uint32_t gsmHttpTimeoutMs = 30'000;
    constexpr size_t gsmCommandBytes = 160;         // longest AT line we build (URL)
    // Consecutive uplink failures before the modem is power-cycled.
    constexpr uint8_t gsmFailuresBeforePowerCycle = 3;
    // Exponential back-off between failed uplink attempts, capped.
    constexpr uint32_t uplinkBackoffInitialMs = 5000;
    constexpr uint32_t uplinkBackoffMaxMs = 300'000;

    // ---- Storage -------------------------------------------------------
    constexpr const char *sdMountPoint = "/sdcard";
    // FAT without long-file-name support: 8.3 names only.
    constexpr const char *trackDirectory = "/sdcard/TRACKS";
    constexpr uint8_t storageRetries = 3;
    constexpr size_t trackLineBytes = 128;

    // ---- NVS -----------------------------------------------------------
    constexpr const char *nvsNamespace = "tracker";
    constexpr const char *nvsSettingsKey = "settings";

    // First-boot values for the Settings blob; the working copy lives in
    // NVS and is edited in the field.
    namespace Defaults
    {
        constexpr uint32_t movingIntervalMs = 5000;
        constexpr uint32_t stationaryIntervalMs = 60'000;
        constexpr uint32_t minDistanceM = 20;
        // 150 cm/s = 5.4 km/h, 50 cm/s = 1.8 km/h, 30 s below that = parked.
        constexpr uint32_t motionStartCmPerS = 150;
        constexpr uint32_t motionStopCmPerS = 50;
        constexpr uint32_t motionStopDelayMs = 30'000;
        constexpr uint8_t gnssProfile = 0; // GnssProfiles::foxeerM10q250
        constexpr bool uplinkEnabled = true;
        constexpr uint32_t uplinkMinIntervalMs = 10'000;
        constexpr const char *apn = "internet";
        // Traccar's public demo server, OsmAnd protocol on port 5055.
        constexpr const char *serverUrl = "http://demo.traccar.org:5055";
        constexpr const char *deviceId = "gps-tracker-01";
    }

    // ---- UI ------------------------------------------------------------
    constexpr int64_t buttonLongPressUs = 1'000'000;
    constexpr uint32_t batteryReadPeriodMs = 10'000;
    constexpr uint32_t diagnosticsPeriodMs = 60'000;
    // The display redraws at most this often even if updates arrive faster
    // (a 10 Hz receiver would otherwise flush 1 KiB over I2C ten times a second).
    constexpr uint32_t displayMinRedrawMs = 200;
    constexpr uint8_t ledBrightness = 24; // out of 255: visible, not blinding

    // ---- Queues and locks ----------------------------------------------
    // Depths are in records. The single-slot queues overwrite; the deep
    // ones drop the oldest record and count it rather than block the
    // producer.
    constexpr UBaseType_t displayQueueDepth = 8;
    constexpr UBaseType_t logQueueDepth = 32;
    constexpr UBaseType_t uplinkQueueDepth = 64;
    constexpr UBaseType_t buttonEdgeQueueDepth = 8;
    constexpr uint32_t busLockTimeoutMs = 200;
    // A modem transaction (connect attempt, HTTP request) can take tens of
    // seconds; the uplink task waits this long for the console to release it.
    constexpr uint32_t modemLockTimeoutMs = 15'000;

    // ---- Task layout ---------------------------------------------------
    // One task per blocking peripheral; priorities put the receiver first
    // so no NMEA byte is lost, the ui next so a press is never missed, and
    // the slow I/O (display flush, card write, modem dialogue) below them.
    // Stack sizes are in bytes (ESP-IDF); the ui task logs every task's
    // high-water mark once a minute so these can be trimmed from a
    // measurement. Periods stay multiples of 10 ms (100 Hz tick).
    constexpr uint32_t gpsTaskStackBytes = 4096;
    constexpr UBaseType_t gpsTaskPriority = 5;
    constexpr uint32_t gpsReadTimeoutMs = 1000;

    constexpr uint32_t uiTaskStackBytes = 4096;
    constexpr UBaseType_t uiTaskPriority = 4;
    constexpr uint32_t uiTickMs = 100;

    constexpr uint32_t displayTaskStackBytes = 4096;
    constexpr UBaseType_t displayTaskPriority = 3;

    constexpr uint32_t loggerTaskStackBytes = 6144; // FATFS works on the caller's stack
    constexpr UBaseType_t loggerTaskPriority = 3;

    constexpr uint32_t uplinkTaskStackBytes = 6144;
    constexpr UBaseType_t uplinkTaskPriority = 2;
    constexpr uint32_t uplinkQueueWaitMs = 1000;

    constexpr uint32_t consoleTaskStackBytes = 4096;
    constexpr UBaseType_t consoleTaskPriority = 1;
    constexpr size_t consoleLineBytes = 128;

    // Every blocking wait inside a task stays below the 5 s task watchdog.
    constexpr uint32_t queueWaitMs = 1000;
}
