#include "reliability/EventLog.hpp"

#include <stdio.h>
#include <string.h>

#include "freertos/task.h"

EventLog *EventLog::instance_ = nullptr;

EventLog::EventLog(UBaseType_t depth)
{
    queue_ = xQueueCreate(depth, sizeof(Line));
}

void EventLog::begin()
{
    instance_ = this;
    console_ = esp_log_set_vprintf(hook);
}

bool EventLog::pending() const
{
    return uxQueueMessagesWaiting(queue_) > 0;
}

int EventLog::hook(const char *format, va_list args)
{
    EventLog &self = *instance_;
    va_list copy;
    va_copy(copy, args);
    const int written = self.console_ != nullptr ? self.console_(format, args) : 0;

    Line line;
    const int length = vsnprintf(line.text, sizeof line.text, format, copy);
    va_end(copy);
    if (length <= 0 || xPortInIsrContext())
    {
        return written;
    }
    if (static_cast<size_t>(length) >= sizeof line.text)
    {
        // Truncated: keep the line terminated so the file stays one line per entry.
        line.text[sizeof line.text - 2] = '\n';
        line.text[sizeof line.text - 1] = '\0';
    }
    if (xQueueSend(self.queue_, &line, 0) != pdTRUE)
    {
        self.dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    return written;
}

esp_err_t EventLog::flush(IStorage &storage, const char *path)
{
    Line line;
    if (!storage.isReady())
    {
        while (xQueueReceive(queue_, &line, 0) == pdTRUE)
        {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    size_t used = 0;
    while (xQueueReceive(queue_, &line, 0) == pdTRUE)
    {
        const size_t length = strlen(line.text);
        if (used + length > sizeof batch_)
        {
            err = storage.append(path, "", batch_, used);
            used = 0;
        }
        memcpy(batch_ + used, line.text, length);
        used += length;
    }
    if (used > 0)
    {
        err = storage.append(path, "", batch_, used);
    }
    return err;
}
