#pragma once

#include <stddef.h>
#include <stdint.h>

// Turns the receiver's byte stream into complete NMEA sentences. A sentence
// starts at '$' and ends at LF; CR is dropped. A '$' in the middle of a
// sentence restarts it (the receiver was reset, or bytes were lost), and a
// sentence longer than the buffer is discarded and counted rather than
// truncated into something that might still pass a field count check.
//
// Pure logic, no ESP-IDF dependency.
template <size_t Capacity>
class NmeaLineAssembler
{
    static_assert(Capacity > 8, "a sentence needs room for '$GPxxx*HH'");

public:
    // Feeds one byte; true when line() now holds a complete sentence
    // (without CR/LF, NUL-terminated). The line stays valid until the next
    // '$' arrives.
    bool push(char c)
    {
        if (c == '$')
        {
            length_ = 0;
            buffer_[length_++] = c;
            active_ = true;
            return false;
        }
        if (!active_)
        {
            return false;
        }
        if (c == '\r')
        {
            return false;
        }
        if (c == '\n')
        {
            buffer_[length_] = '\0';
            active_ = false;
            return true;
        }
        if (length_ >= Capacity - 1)
        {
            active_ = false;
            ++overflows_;
            return false;
        }
        buffer_[length_++] = c;
        return false;
    }

    const char *line() const { return buffer_; }
    size_t length() const { return length_; }
    uint32_t overflows() const { return overflows_; }

private:
    char buffer_[Capacity] = {};
    size_t length_ = 0;
    bool active_ = false;
    uint32_t overflows_ = 0;
};
