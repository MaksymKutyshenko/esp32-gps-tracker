#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#include "periph/I2cBus.hpp"

// SSD1306 128x64 OLED over I2C. All drawing happens in a local framebuffer;
// nothing reaches the panel until flush() sends the whole frame, so a caller
// composes a complete picture and pushes it atomically.
class Ssd1306
{
public:
    static constexpr int32_t width = 128;
    static constexpr int32_t height = 64;

    explicit Ssd1306(I2cBus &bus);

    void begin();

    void clear();
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, bool on);
    // Text in the 5x7 font; scale stretches every font pixel to a
    // scale x scale block, so scale 2 gives bold 10x14 glyphs.
    void drawText(int32_t x, int32_t y, const char *text, uint8_t scale);
    // Width in pixels the text would occupy, for centring.
    static int32_t textWidth(const char *text, uint8_t scale);

    esp_err_t flush();

private:
    static constexpr size_t pageCount = height / 8;
    static constexpr size_t frameBytes = static_cast<size_t>(width) * pageCount;

    void setPixel(int32_t x, int32_t y, bool on);
    void drawChar(int32_t x, int32_t y, char c, uint8_t scale);

    I2cBus &bus_;
    i2c_master_dev_handle_t device_ = nullptr;
    // frame_[0] is the I2C data control byte so the buffer can be sent as-is
    // in a single transfer; pixels live in frame_[1..].
    uint8_t frame_[frameBytes + 1] = {};
};
