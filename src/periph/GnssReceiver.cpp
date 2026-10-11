#include "periph/GnssReceiver.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include "logic/UbxAck.hpp"

namespace
{
    constexpr const char *logTag = "gnss";
}

GnssReceiver::GnssReceiver(UartPort &uart) : uart_(uart)
{
}

void GnssReceiver::begin(const GnssProfile &profile)
{
    ESP_ERROR_CHECK(uart_.setBaudRate(profile.baudRate));

    // Rate, sentence selection, static hold and accuracy mask for this
    // module. Each frame is answered with UBX-ACK-ACK or -NAK inside the
    // NMEA stream; a NAK means the receiver's firmware does not know a
    // key, and the log says which frame so the profile can be adjusted.
    size_t acknowledged = 0;
    for (size_t i = 0; i < profile.setupCount; ++i)
    {
        const UbxCommand &command = profile.setup[i];
        if (uart_.write(command.bytes, command.length) != ESP_OK)
        {
            ESP_LOGW(logTag, "Setup frame %u: UART write failed", static_cast<unsigned>(i + 1));
            continue;
        }
        const UbxAckScanner::Result answer = awaitAck(command.bytes[2], command.bytes[3]);
        if (answer == UbxAckScanner::Result::Ack)
        {
            ++acknowledged;
        }
        else
        {
            ESP_LOGW(logTag, "Setup frame %u (class 0x%02X id 0x%02X): %s", static_cast<unsigned>(i + 1),
                     command.bytes[2], command.bytes[3],
                     answer == UbxAckScanner::Result::Nak ? "rejected (NAK)" : "no answer");
        }
    }
    ESP_ERROR_CHECK(uart_.discardInput());
    ESP_LOGI(logTag, "Receiver profile: %s at %" PRIu32 " baud, %u/%u setup frames acknowledged",
             profile.name, profile.baudRate, static_cast<unsigned>(acknowledged),
             static_cast<unsigned>(profile.setupCount));
}

UbxAckScanner::Result GnssReceiver::awaitAck(uint8_t messageClass, uint8_t messageId)
{
    UbxAckScanner scanner;
    const int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(Config::gpsAckTimeoutMs) * 1000;
    while (esp_timer_get_time() < deadlineUs)
    {
        const size_t count = uart_.read(chunk_, sizeof chunk_, pdMS_TO_TICKS(20));
        for (size_t i = 0; i < count; ++i)
        {
            const UbxAckScanner::Result result = scanner.push(chunk_[i], messageClass, messageId);
            if (result != UbxAckScanner::Result::None)
            {
                return result;
            }
        }
    }
    return UbxAckScanner::Result::None;
}

bool GnssReceiver::readSentence(const char *&line, uint32_t timeoutMs)
{
    const int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;

    while (true)
    {
        while (chunkIndex_ < chunkLength_)
        {
            if (assembler_.push(static_cast<char>(chunk_[chunkIndex_++])))
            {
                line = assembler_.line();
                return true;
            }
        }

        const int64_t remainingUs = deadlineUs - esp_timer_get_time();
        if (remainingUs <= 0)
        {
            return false;
        }
        // Round up so a sub-tick remainder still waits one tick rather
        // than spinning.
        const TickType_t ticks = pdMS_TO_TICKS((remainingUs + 999) / 1000) + 1;
        chunkLength_ = uart_.read(chunk_, sizeof chunk_, ticks);
        chunkIndex_ = 0;
        if (chunkLength_ == 0)
        {
            return false;
        }
    }
}
