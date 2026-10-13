#include "reliability/TaskWatchdog.hpp"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr const char *logTag = "wdt";

    // Written by the watchdog interrupt just before the panic, read at the
    // next boot. RTC slow memory keeps its contents across a panic reset
    // but holds garbage after a power-up, hence the magic word.
    struct CulpritRecord
    {
        uint32_t magic;
        char names[TaskWatchdog::culpritBytes];
    };
    constexpr uint32_t culpritMagic = 0x54574431; // "TWD1"
    RTC_NOINIT_ATTR CulpritRecord culprits;

    // esp_task_wdt_print_triggered_tasks() hands its text over in pieces:
    // the caption once, then "\n - ", the task name and " (CPU n)" for
    // every task that missed the deadline. Only the names are kept.
    struct Collector
    {
        size_t pieces;
        size_t used;
    };

    void collectName(void *opaque, const char *piece)
    {
        Collector &c = *static_cast<Collector *>(opaque);
        const size_t index = c.pieces++;
        const bool isName = index > 0 && (index - 1) % 3 == 1;
        if (!isName)
        {
            return;
        }
        char *out = culprits.names;
        constexpr size_t last = sizeof culprits.names - 1;
        if (c.used > 0 && c.used < last)
        {
            out[c.used++] = ',';
        }
        for (const char *p = piece; *p != '\0' && c.used < last; ++p)
        {
            out[c.used++] = *p;
        }
        out[c.used] = '\0';
    }
}

// Called by the task-watchdog ISR before it panics; weak in ESP-IDF, so
// defining it here is the whole registration. No logging allowed in here.
extern "C" void esp_task_wdt_isr_user_handler(void)
{
    Collector collector = {};
    culprits.names[0] = '\0';
    int cpusFail = 0;
    esp_task_wdt_print_triggered_tasks(collectName, &collector, &cpusFail);
    culprits.magic = culpritMagic;
}

size_t TaskWatchdog::takeLastCulprits(char *out, size_t capacity)
{
    if (culprits.magic != culpritMagic)
    {
        out[0] = '\0';
        return 0;
    }
    culprits.magic = 0;
    culprits.names[sizeof culprits.names - 1] = '\0';
    return strlcpy(out, culprits.names, capacity);
}

WatchdogSubscription::WatchdogSubscription()
{
    // nullptr means the calling task.
    const esp_err_t err = esp_task_wdt_add(nullptr);
    subscribed_ = (err == ESP_OK);
    if (!subscribed_)
    {
        ESP_LOGE(logTag, "Task %s not watched: %s", pcTaskGetName(nullptr), esp_err_to_name(err));
    }
}

WatchdogSubscription::~WatchdogSubscription()
{
    if (subscribed_)
    {
        esp_task_wdt_delete(nullptr);
    }
}

void WatchdogSubscription::feed() const
{
    if (subscribed_)
    {
        esp_task_wdt_reset();
    }
}
