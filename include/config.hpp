#pragma once

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
// (USB), GPIO26..32 (flash), GPIO45/46 (strapping). The console runs over
// USB Serial/JTAG (sdkconfig.defaults), so UART0's pins are free.
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
    // uart_config_t.baud_rate is a bare int in the IDF API.
    constexpr int gpsBaudRate = 115200;
    // A 10 Hz receiver emits up to ~1 KiB of NMEA per burst; two bursts of
    // slack keep a late reader from dropping bytes.
    constexpr int gpsRxBufferBytes = 2048;
    // Longest NMEA sentence is 82 characters including CR LF.
    constexpr size_t gpsLineBytes = 96;
    // No sentence for this long means the receiver is dead or unplugged.
    constexpr uint32_t gpsSilenceTimeoutMs = 5000;

    // ---- Accelerometer (BMA400, optional until delivered) --------------
    constexpr gpio_num_t accelInterruptPin = GPIO_NUM_6; // INT1, RTC GPIO
    constexpr uint8_t accelAddress = 0x14;               // 0x15 if SDO high

    // ---- GSM modem (SIM800C, UART2) -------------------------------------
    // PWRKEY: hold low >= 1 s to toggle the modem's power state. Only wired
    // if the breakout exposes it; otherwise leave the pin unconnected.
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
    // SIM800C autobauds; the firmware pins it with AT+IPR on first contact.
    constexpr int gsmBaudRate = 115200;
    constexpr int gsmRxBufferBytes = 2048;

    // ---- I2C bus: OLED + accelerometer (+ GNSS compass) ----------------
    constexpr i2c_port_num_t i2cPort = I2C_NUM_0;
    constexpr gpio_num_t i2cSdaPin = GPIO_NUM_8;
    constexpr gpio_num_t i2cSclPin = GPIO_NUM_9;
    // Breadboard wiring with module pull-ups only: stay at 100 kHz (the
    // 1 KiB OLED frame NACKed at 400 kHz in module 4.3).
    constexpr uint32_t i2cSpeedHz = 100'000;
    // i2c_master_transmit() takes the timeout as a bare int of milliseconds.
    constexpr int i2cTimeoutMs = 100;
    constexpr uint8_t displayAddress = 0x3C;
    constexpr uint8_t compassAddress = 0x0D; // QMC5883L on the Foxeer

    // ---- MicroSD (SPI2 / FSPI IOMUX pins, DMA) --------------------------
    constexpr spi_host_device_t sdSpiHost = SPI2_HOST;
    constexpr gpio_num_t sdSclkPin = GPIO_NUM_12;
    constexpr gpio_num_t sdMosiPin = GPIO_NUM_11;
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

    // ---- Task layout ---------------------------------------------------
    // Heartbeat task: proves the RTOS skeleton is alive and reports the
    // stack high-water mark so the stack sizes below can be sized from a
    // measurement rather than a guess. FreeRTOS ticks at 100 Hz, so periods
    // stay multiples of 10 ms.
    constexpr uint32_t heartbeatPeriodMs = 1000;
    constexpr uint32_t heartbeatTaskStackBytes = 3072;
    constexpr UBaseType_t heartbeatTaskPriority = 1;

    // Stack headroom is logged this often; ESP_LOG dominates the profile.
    constexpr uint32_t stackReportPeriodMs = 60'000;
}
