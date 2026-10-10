#include "periph/GnssReceiver.hpp"

#include "esp_log.h"
#include "esp_timer.h"

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

    // Rate and sentence selection for this module. Sent blind: the UBX
    // acknowledgements come back in the NMEA stream, where the assembler
    // skips them, and the effect shows up as the per-minute 'nmea rx'
    // count (about 180 at 1 Hz with three sentences, 7200 untouched).
    size_t sent = 0;
    for (size_t i = 0; i < profile.setupCount; ++i)
    {
        if (uart_.write(profile.setup[i].bytes, profile.setup[i].length) == ESP_OK)
        {
            ++sent;
        }
    }
    ESP_ERROR_CHECK(uart_.discardInput());
    ESP_LOGI(logTag, "Receiver profile: %s at %" PRIu32 " baud, %u/%u setup frames sent",
             profile.name, profile.baudRate, static_cast<unsigned>(sent),
             static_cast<unsigned>(profile.setupCount));
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
