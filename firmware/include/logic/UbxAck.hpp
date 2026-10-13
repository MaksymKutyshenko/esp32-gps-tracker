#pragma once

#include <stddef.h>
#include <stdint.h>

// Finds the receiver's answer to a configuration frame in the raw byte
// stream: UBX-ACK-ACK (class 0x05, id 0x01) or UBX-ACK-NAK (0x05, 0x00),
// both carrying the class and id of the frame they answer. The answer is
// interleaved with NMEA text, so the scanner works on a sliding window
// of the last ten bytes and insists on the checksum, which no NMEA text
// can satisfy by accident.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
class UbxAckScanner
{
public:
    enum class Result : uint8_t
    {
        None,
        Ack,
        Nak,
    };

    // Feeds one byte; reports when the window holds a complete answer to
    // the frame (messageClass, messageId).
    Result push(uint8_t byte, uint8_t messageClass, uint8_t messageId)
    {
        for (size_t i = 0; i + 1 < frameLength; ++i)
        {
            window_[i] = window_[i + 1];
        }
        window_[frameLength - 1] = byte;

        if (window_[0] != 0xB5 || window_[1] != 0x62 || window_[2] != 0x05 ||
            window_[3] > 0x01 || window_[4] != 0x02 || window_[5] != 0x00 ||
            window_[6] != messageClass || window_[7] != messageId)
        {
            return Result::None;
        }
        uint8_t a = 0;
        uint8_t b = 0;
        for (size_t i = 2; i < 8; ++i)
        {
            a = static_cast<uint8_t>(a + window_[i]);
            b = static_cast<uint8_t>(b + a);
        }
        if (a != window_[8] || b != window_[9])
        {
            return Result::None;
        }
        return window_[3] == 0x01 ? Result::Ack : Result::Nak;
    }

private:
    static constexpr size_t frameLength = 10;
    uint8_t window_[frameLength] = {};
};
