#include "periph/Ssd1306.hpp"

#include <string.h>

#include "esp_log.h"

#include "periph/Font5x7.hpp"
#include "config.hpp"

namespace
{
    constexpr const char *logTag = "ssd1306";

    // First byte of every transfer tells the controller what follows:
    constexpr uint8_t controlCommands = 0x00;
    constexpr uint8_t controlData = 0x40;

    constexpr uint8_t initSequence[] = {
        controlCommands,
        0xAE,        // display off while configuring
        0xD5, 0x80,  // clock divide ratio / oscillator, datasheet default
        0xA8, 0x3F,  // multiplex ratio 64
        0xD3, 0x00,  // no display offset
        0x40,        // start line 0
        0x8D, 0x14,  // charge pump on (module has no external Vcc)
        0x20, 0x00,  // horizontal addressing: flush streams the whole frame
        0xA1,        // segment remap
        0xC8,        // COM scan direction remapped
        0xDA, 0x12,  // COM pins: alternative, no left/right remap (128x64)
        0x81, 0xCF,  // contrast
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOMH deselect level
        0xA4,        // resume from RAM content
        0xA6,        // normal (not inverted)
        0xAF,        // display on
    };
}

Ssd1306::Ssd1306(I2cBus &bus) : bus_(bus)
{
    frame_[0] = controlData;
}

esp_err_t Ssd1306::begin()
{
    device_ = bus_.addDevice(Config::displayAddress, Config::i2cSpeedHz);
    const esp_err_t err = reinit();
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "No display at address 0x%02X (%s); running without it",
                 Config::displayAddress, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(logTag, "Display ready at address 0x%02X", Config::displayAddress);
    return ESP_OK;
}

esp_err_t Ssd1306::reinit()
{
    const esp_err_t err = i2c_master_transmit(device_, initSequence, sizeof initSequence,
                                              static_cast<int>(Config::i2cTimeoutMs));
    if (err != ESP_OK)
    {
        return err;
    }
    clear();
    return flush();
}

void Ssd1306::clear()
{
    memset(frame_ + 1, 0, frameBytes);
}

void Ssd1306::setPixel(int32_t x, int32_t y, bool on)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return;
    }

    uint8_t &cell = frame_[1 + static_cast<size_t>(y / 8) * width + x];
    uint8_t mask = static_cast<uint8_t>(1u << (y % 8));
    if (on)
    {
        cell |= mask;
    }
    else
    {
        cell &= static_cast<uint8_t>(~mask);
    }
}

void Ssd1306::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, bool on)
{
    for (int32_t dy = 0; dy < h; ++dy)
    {
        for (int32_t dx = 0; dx < w; ++dx)
        {
            setPixel(x + dx, y + dy, on);
        }
    }
}

void Ssd1306::drawChar(int32_t x, int32_t y, char c, uint8_t scale)
{
    if (c < Font5x7::firstChar ||
        c >= Font5x7::firstChar + static_cast<char>(Font5x7::charCount))
    {
        c = '?';
    }
    const uint8_t *columns = Font5x7::data[c - Font5x7::firstChar];

    for (size_t col = 0; col < Font5x7::glyphColumns; ++col)
    {
        for (size_t row = 0; row < Font5x7::glyphRows; ++row)
        {
            if ((columns[col] & (1u << row)) == 0)
            {
                continue;
            }
            fillRect(x + static_cast<int32_t>(col) * scale,
                     y + static_cast<int32_t>(row) * scale, scale, scale,
                     true);
        }
    }
}

void Ssd1306::drawText(int32_t x, int32_t y, const char *text, uint8_t scale)
{
    // One blank column between glyphs, scaled like the glyph itself.
    int32_t advance = static_cast<int32_t>(Font5x7::glyphColumns + 1) * scale;
    for (; *text != '\0'; ++text, x += advance)
    {
        drawChar(x, y, *text, scale);
    }
}

int32_t Ssd1306::textWidth(const char *text, uint8_t scale)
{
    int32_t count = static_cast<int32_t>(strlen(text));
    if (count == 0)
    {
        return 0;
    }
    // No trailing inter-glyph gap after the last character.
    return count * static_cast<int32_t>(Font5x7::glyphColumns + 1) * scale -
           scale;
}

esp_err_t Ssd1306::flush()
{
    // Re-arm the addressing window every frame so a glitched controller
    // resynchronises instead of drawing shifted forever.
    const uint8_t window[] = {
        controlCommands,
        0x21, 0x00, width - 1,      // column span
        0x22, 0x00, pageCount - 1,  // page span
    };
    esp_err_t err = i2c_master_transmit(device_, window, sizeof window,
                                        static_cast<int>(Config::i2cTimeoutMs));
    if (err != ESP_OK)
    {
        return err;
    }
    return i2c_master_transmit(device_, frame_, sizeof frame_,
                               static_cast<int>(Config::i2cTimeoutMs));
}
