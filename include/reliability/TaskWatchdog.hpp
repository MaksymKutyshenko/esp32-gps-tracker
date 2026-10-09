#pragma once

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
