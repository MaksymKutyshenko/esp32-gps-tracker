#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "transport/IStorage.hpp"

// Mirrors the firmware's own log output to a file on the card, so a run
// without a monitor attached still shows what happened before a reset and
// the boot line after it names the reason.
//
// The ESP-IDF log hook may be called from any task, so it only copies the
// formatted line into a queue and returns; the task that owns the card
// (the logger) drains the queue into the file in one append per batch.
// A full queue drops the newest line and counts it rather than blocking
// whoever is logging.
class EventLog
{
public:
    static constexpr size_t lineCapacity = 120;

    explicit EventLog(UBaseType_t depth);

    // Installs the log hook. Lines keep going to the console as before.
    void begin();

    bool pending() const;
    uint32_t dropped() const { return dropped_.load(); }

    // Appends every queued line to `path`. Call under the card's lock from
    // the task that owns it. With the storage not ready the lines are
    // discarded, so the queue always holds the newest ones.
    esp_err_t flush(IStorage &storage, const char *path);

private:
    struct Line
    {
        char text[lineCapacity];
    };

    static int hook(const char *format, va_list args);

    static EventLog *instance_;
    QueueHandle_t queue_ = nullptr;
    vprintf_like_t console_ = nullptr;
    std::atomic<uint32_t> dropped_{0};
    char batch_[1024] = {};
};
