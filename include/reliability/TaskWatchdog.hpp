#pragma once

#include <stddef.h>

#include "esp_err.h"

// The task watchdog, scoped to a task. Every long-running task constructs a
// WatchdogSubscription at the top of its loop function and calls feed()
// once per iteration. If an iteration blocks for longer than
// CONFIG_ESP_TASK_WDT_TIMEOUT_S (5 s, sdkconfig.defaults) the chip panics
// and resets, and the next boot logs ESP_RST_TASK_WDT — a silent hang is
// turned into a visible, recoverable event.
//
// Waits inside the loop therefore have to be shorter than the timeout:
// queue receives and UART reads use bounded timeouts and feed between them.
class WatchdogSubscription
{
public:
    // Subscribes the calling task. A failure is logged and the task keeps
    // running unguarded rather than aborting: a watchdog is a safety net,
    // not a reason to refuse to run.
    WatchdogSubscription();
    ~WatchdogSubscription();

    WatchdogSubscription(const WatchdogSubscription &) = delete;
    WatchdogSubscription &operator=(const WatchdogSubscription &) = delete;

    void feed() const;
    bool subscribed() const { return subscribed_; }

private:
    bool subscribed_ = false;
};

// What the watchdog caught. ESP-IDF names the tasks that missed the
// deadline on the console only, and in the field nobody is reading it. The
// interrupt hook in TaskWatchdog.cpp copies the names into RTC memory,
// which a panic reset keeps, so the next boot can log them and send them
// in the health report ("rst=taskwdt:logger").
namespace TaskWatchdog
{
    constexpr size_t culpritBytes = 24; // "gps,uplink" and the like

    // The names of the tasks that tripped the watchdog before the last
    // reset, comma-separated; an empty string when the last reset was not
    // a task-watchdog panic or the record did not survive. One-shot: the
    // record is cleared by the call.
    size_t takeLastCulprits(char *out, size_t capacity);
}
