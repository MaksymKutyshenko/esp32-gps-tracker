#include "periph/StatusLed.hpp"

#include "esp_log.h"

namespace
{
    constexpr const char *logTag = "led";

    // 10 MHz tick: WS2812 timing in 100 ns units.
    constexpr uint32_t resolutionHz = 10'000'000;
    constexpr uint16_t bit0HighTicks = 3;  // 0.3 us
    constexpr uint16_t bit0LowTicks = 9;   // 0.9 us
    constexpr uint16_t bit1HighTicks = 9;  // 0.9 us
    constexpr uint16_t bit1LowTicks = 3;   // 0.3 us
}

StatusLed::StatusLed(gpio_num_t pin) : pin_(pin)
{
}

void StatusLed::begin()
{
    rmt_tx_channel_config_t channelConfig = {};
    channelConfig.gpio_num = pin_;
    channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    channelConfig.resolution_hz = resolutionHz;
    channelConfig.mem_block_symbols = 64;
    channelConfig.trans_queue_depth = 2;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channelConfig, &channel_));

    rmt_bytes_encoder_config_t encoderConfig = {};
    encoderConfig.bit0.level0 = 1;
    encoderConfig.bit0.duration0 = bit0HighTicks;
    encoderConfig.bit0.level1 = 0;
    encoderConfig.bit0.duration1 = bit0LowTicks;
    encoderConfig.bit1.level0 = 1;
    encoderConfig.bit1.duration0 = bit1HighTicks;
    encoderConfig.bit1.level1 = 0;
    encoderConfig.bit1.duration1 = bit1LowTicks;
    encoderConfig.flags.msb_first = 1;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoderConfig, &encoder_));

    ESP_ERROR_CHECK(rmt_enable(channel_));
    ESP_ERROR_CHECK(off());
    ESP_LOGI(logTag, "WS2812 on GPIO%d", pin_);
}

esp_err_t StatusLed::set(uint8_t red, uint8_t green, uint8_t blue)
{
    // Wait for the previous frame so the buffer can be rewritten; also
    // guarantees the >50 us reset gap the LED needs between frames.
    esp_err_t err = rmt_tx_wait_all_done(channel_, 10);
    if (err != ESP_OK)
    {
        return err;
    }
    frame_[0] = green;
    frame_[1] = red;
    frame_[2] = blue;

    rmt_transmit_config_t transmitConfig = {};
    transmitConfig.loop_count = 0;
    transmitConfig.flags.eot_level = 0;
    return rmt_transmit(channel_, encoder_, frame_, sizeof frame_, &transmitConfig);
}
