#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"

// The board's WS2812 RGB LED, driven by the RMT peripheral: the hardware
// shifts out the 800 kHz bit stream from a 3-byte buffer, so the CPU never
// bit-bangs timing-critical pulses. One colour per tracker state is the
// at-a-glance status when the display is off or unreadable.
class StatusLed
{
public:
    explicit StatusLed(gpio_num_t pin);

    void begin();

    // Non-blocking: queues the 24-bit frame and returns.
    esp_err_t set(uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t off() { return set(0, 0, 0); }

private:
    gpio_num_t pin_;
    rmt_channel_handle_t channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    // WS2812 wants GRB, most significant bit first.
    uint8_t frame_[3] = {};
};
