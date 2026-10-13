#pragma once

// A sampler that keeps running when nothing else does.
//
// A hang that holds off every interrupt except the tick leaves no trace:
// the task watchdog's own interrupt cannot run, so its 10 s hardware
// backstop resets the chip without a word. This hook runs inside the tick
// interrupt on both cores, from IRAM, and every 250 ms records what the
// core was executing when the tick arrived: the interrupted program
// counter, the running task, the interrupt-enable mask and whether the
// flash cache was on. The samples live in RTC memory, which the reset
// keeps, so the next boot can print the last seconds before it.
namespace FreezeTrace
{
    // Prints the samples kept from the previous run, if any, then clears
    // them. Call once at boot, before begin(). `wanted` says whether the
    // reset reason makes them worth printing (a watchdog reset does, a
    // power-on or a deep-sleep wake does not).
    void logLastTrace(bool wanted);

    // True once if the previous run ended in the storm guard's own reset:
    // the tick interrupt had run away and the chip was reset before the
    // watchdog's backstop. Call before logLastTrace().
    bool tookStormReset();

    // Arms the tick hooks on both cores.
    void begin();
}
