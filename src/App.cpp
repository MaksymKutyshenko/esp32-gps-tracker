#include "App.hpp"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"

#include "config.hpp"

namespace
{
    constexpr const char *logTag = "tracker";
}

void App::begin()
{
    // The reset reason is the first thing a reliability review asks for:
    // a task watchdog reset here means a task stopped feeding it.
    ESP_LOGI(logTag, "Boot, reset reason %d", static_cast<int>(esp_reset_reason()));
}

void App::run()
{
    begin();

    // ESP-IDF takes the stack depth in bytes (vanilla FreeRTOS: words).
    BaseType_t created = xTaskCreate(heartbeatTaskEntry, "heartbeat",
                                     Config::heartbeatTaskStackBytes, this,
                                     Config::heartbeatTaskPriority, nullptr);
    configASSERT(created == pdPASS);
}

void App::heartbeatTaskEntry(void *self)
{
    static_cast<App *>(self)->heartbeatLoop();
}

void App::heartbeatLoop()
{
    constexpr uint32_t beatsPerReport = Config::stackReportPeriodMs / Config::heartbeatPeriodMs;
    uint32_t beats = 0;
    TickType_t lastWake = xTaskGetTickCount();

    while (true)
    {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::heartbeatPeriodMs));

        if (++beats % beatsPerReport == 0)
        {
            ESP_LOGI(logTag, "Alive %" PRIu32 " s, stack headroom %" PRIu32 " bytes",
                     beats, static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
        }
    }
}
