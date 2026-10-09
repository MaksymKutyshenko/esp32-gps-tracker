#include "reliability/TaskWatchdog.hpp"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr const char *logTag = "wdt";
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
