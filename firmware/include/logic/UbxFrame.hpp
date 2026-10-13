#pragma once

#include <stddef.h>
#include <stdint.h>

// u-blox UBX binary frame: sync (0xB5 0x62), class, id, 16-bit little-endian
// payload length, payload, and an 8-bit Fletcher checksum over everything
// between the sync bytes and the checksum. The frames this tracker sends are
// fixed at compile time, so the whole frame, checksum included, is built by
// a constexpr constructor and lives in flash; nothing is computed at run
// time and nothing can be mis-typed.
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host against frames
// computed independently.
template <size_t PayloadSize>
class UbxFrame
{
public:
    static constexpr size_t size = PayloadSize + 8;

    constexpr UbxFrame(uint8_t messageClass, uint8_t messageId,
                       const uint8_t (&payload)[PayloadSize])
        : bytes_{}
    {
        bytes_[0] = 0xB5;
        bytes_[1] = 0x62;
        bytes_[2] = messageClass;
        bytes_[3] = messageId;
        bytes_[4] = static_cast<uint8_t>(PayloadSize & 0xFF);
        bytes_[5] = static_cast<uint8_t>(PayloadSize >> 8);
        for (size_t i = 0; i < PayloadSize; ++i)
        {
            bytes_[6 + i] = payload[i];
        }
        uint8_t a = 0;
        uint8_t b = 0;
        for (size_t i = 2; i < 6 + PayloadSize; ++i)
        {
            a = static_cast<uint8_t>(a + bytes_[i]);
            b = static_cast<uint8_t>(b + a);
        }
        bytes_[6 + PayloadSize] = a;
        bytes_[7 + PayloadSize] = b;
    }

    constexpr const uint8_t *data() const { return bytes_; }
    constexpr size_t length() const { return size; }

private:
    uint8_t bytes_[size];
};

// A frame to send, as the receiver driver sees it: bytes and length only.
struct UbxCommand
{
    const uint8_t *bytes;
    size_t length;
};
