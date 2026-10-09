#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

// Pins, bus parameters and task layout for the GPS tracker. Every constant
// the firmware tunes lives here; driver code never carries a literal pin.
//
// Pin assignments are filled in once the component list is final. Keep off
// GPIO19/20 (USB) and GPIO26..32 (flash) on the ESP32-S3, and remember that
// UART0 is the console, so the GPS receiver goes on UART1 or UART2.
namespace Config
{
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
