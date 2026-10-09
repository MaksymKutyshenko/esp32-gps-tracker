#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// GPS tracker application: owns the peripherals and the FreeRTOS tasks.
//
// This is the project skeleton. The planned task layout (see
// REQUIREMENTS.md) is:
//   - gps_rx   — parks in a blocking UART read, assembles and validates
//                NMEA sentences, publishes the latest fix;
//   - display  — peeks the latest fix and redraws only on change;
//   - logger   — receives every fix and appends it to storage;
//   - ui       — button events (GPIO interrupt -> queue), buzzer.
// Tasks talk through queues; shared buses are guarded by one mutex each.
//
// run() starts the tasks and returns; the object must outlive them (main
// keeps it static).
class App
{
public:
    void run();

private:
    void begin();
    [[noreturn]] void heartbeatLoop();

    static void heartbeatTaskEntry(void *self);
};
