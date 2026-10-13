#pragma once

#include <stdint.h>

#include <atomic>

// Counters every task bumps when something is dropped, retried or fails.
// They are 32-bit atomics so any task (or ISR, for the relaxed increments)
// can touch them without a lock, and the ui task prints them once a minute
// next to the stack high-water marks. The numbers are what the README's
// "Results" section and the defense's "edge cases" question are answered
// with.
struct Diagnostics
{
    std::atomic<uint32_t> sentencesReceived{0};
    std::atomic<uint32_t> sentencesRejected{0};
    std::atomic<uint32_t> gnssSilences{0};

    std::atomic<uint32_t> recordsLogged{0};
    std::atomic<uint32_t> recordsDroppedLogQueue{0};
    std::atomic<uint32_t> storageErrors{0};
    std::atomic<uint32_t> storageRemounts{0};

    std::atomic<uint32_t> recordsUploaded{0};
    std::atomic<uint32_t> recordsDroppedUplinkQueue{0};
    std::atomic<uint32_t> uplinkFailures{0};
    std::atomic<uint32_t> modemPowerCycles{0};

    std::atomic<uint32_t> displayErrors{0};
    std::atomic<uint32_t> busTimeouts{0};
    std::atomic<uint32_t> batteryReadErrors{0};
    std::atomic<uint32_t> buttonEdgesDropped{0};

    static void increment(std::atomic<uint32_t> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
};
