#include "reliability/EventLog.hpp"

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    const int formatted = vsnprintf(line.text, sizeof line.text, format, copy);
    va_end(copy);
    if (formatted <= 0 || xPortInIsrContext())
    {
        return written;
    }
    size_t length = static_cast<size_t>(formatted) < sizeof line.text
                        ? static_cast<size_t>(formatted)
                        : sizeof line.text - 1;
    length = stampDate(line.text, sizeof line.text, length);
    if (line.text[length - 1] != '\n')
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

size_t EventLog::stampDate(char *text, size_t capacity, size_t length)
{
    // Every ESP-IDF line starts "<level> (<time>) "; anything else (a raw
    // printf through the same hook) is kept as it is.
    if (length < 3 || text[1] != ' ' || text[2] != '(')
    {
        return length;
    }
    const time_t now = time(nullptr);
    struct tm utc = {};
    gmtime_r(&now, &utc);
    // Each field bounded to two digits, so the buffer is provably enough.
    const unsigned year = static_cast<unsigned>(utc.tm_year + 1900) % 100;
    const unsigned month = static_cast<unsigned>(utc.tm_mon + 1) % 100;
    const unsigned day = static_cast<unsigned>(utc.tm_mday) % 100;
    char date[16];
    snprintf(date, sizeof date, "%02u-%02u-%02u ", year, month, day);
    constexpr size_t dateLength = 9;
    constexpr size_t at = 3; // right after "I ("

    // Make room, cutting the tail if the line is already at capacity.
    size_t tail = length - at;
    if (at + dateLength + tail >= capacity)
    {
        tail = capacity - 1 - at - dateLength;
    }
    memmove(text + at + dateLength, text + at, tail);
    memcpy(text + at, date, dateLength);
    length = at + dateLength + tail;
    text[length] = '\0';
    return length;
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
