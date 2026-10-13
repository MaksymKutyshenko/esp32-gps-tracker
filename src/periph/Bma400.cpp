#include "periph/Bma400.hpp"

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.hpp"

namespace
{
    constexpr const char *logTag = "bma400";

    // Register map (BMA400 datasheet, rev 1.5).
    constexpr uint8_t regChipId = 0x00;
    constexpr uint8_t regAccConfig0 = 0x19;   // [1:0] power mode: 0 sleep, 1 low power, 2 normal
    constexpr uint8_t regAccConfig1 = 0x1A;   // [7:6] range, [5:4] OSR, [3:0] ODR
    constexpr uint8_t regIntConfig0 = 0x1F;   // bit 2: gen1_int_en
    constexpr uint8_t regIntConfig1 = 0x20;   // bit 7: latch_int
    constexpr uint8_t regInt1Map = 0x21;      // bit 2: gen1 -> INT1
    constexpr uint8_t regInt12IoCtrl = 0x24;  // bit 1: int1 active high, bit 2: int1 open drain
    constexpr uint8_t regGen1Config0 = 0x3F;  // [7:5] axes z y x, [4] data source, [3:2] ref update, [1:0] hysteresis
    constexpr uint8_t regGen1Config1 = 0x40;  // bit 1: criterion 1 = activity, bit 0: 1 = AND of axes
    constexpr uint8_t regGen1Config2 = 0x41;  // threshold, 8 mg per LSB
    constexpr uint8_t regGen1Config3 = 0x42;  // duration [15:8], in samples
    constexpr uint8_t regGen1Config31 = 0x43; // duration [7:0]
    constexpr uint8_t regCmd = 0x7E;

    constexpr uint8_t cmdSoftReset = 0xB6;
    constexpr uint8_t powerModeNormal = 0x02;
    constexpr uint8_t range4g = 0x01 << 6;
    constexpr uint8_t odr100Hz = 0x08;
    constexpr uint8_t int1ActiveHighPushPull = 0x02;
    constexpr uint8_t gen1Enable = 0x04;
    constexpr uint8_t gen1ToInt1 = 0x04;
    // All three axes, acc_filt2 (fixed 100 Hz) as data source, reference
    // updated with every sample (so "activity" means a change since the
    // last sample, whatever the orientation), 24 mg hysteresis.
    constexpr uint8_t gen1AllAxesEveryTime = 0xE0 | 0x10 | 0x08 | 0x01;
    constexpr uint8_t gen1ActivityAnyAxis = 0x02;

    constexpr uint32_t i2cTimeoutMs = Config::i2cTimeoutMs;
}

Bma400::Bma400(I2cBus &bus, gpio_num_t interruptPin)
    : bus_(bus), interruptPin_(interruptPin)
{
}

esp_err_t Bma400::begin()
{
    const uint8_t candidates[] = {Config::accelAddress, static_cast<uint8_t>(Config::accelAddress ^ 0x01)};
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (uint8_t address : candidates)
    {
        err = probe(address);
        if (err == ESP_OK)
        {
            break;
        }
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "No BMA400 at 0x%02X or 0x%02X (%s); motion from GNSS speed", candidates[0],
                 candidates[1], esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    err = configure();
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "Configuration failed: %s; motion from GNSS speed", esp_err_to_name(err));
        return err;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << interruptPin_;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE; // INT1 drives high on activity
    config.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&config));
    // The GPIO ISR service is installed once by App::begin.
    ESP_ERROR_CHECK(gpio_isr_handler_add(interruptPin_, isrEntry, this));

    present_ = true;
    ESP_LOGI(logTag, "BMA400 at 0x%02X: activity > %u mg for %u samples at 100 Hz on INT1 (GPIO%d)",
             address_, static_cast<unsigned>(Config::accelActivityThresholdMg),
             static_cast<unsigned>(Config::accelActivityDurationSamples), interruptPin_);
    return ESP_OK;
}

esp_err_t Bma400::probe(uint8_t address)
{
    i2c_master_dev_handle_t device = bus_.addDevice(address, Config::i2cSpeedHz);
    if (device == nullptr)
    {
        return ESP_ERR_NO_MEM;
    }
    device_ = device;
    uint8_t id = 0;
    const esp_err_t err = readRegister(regChipId, id);
    if (err == ESP_OK && id == chipId)
    {
        address_ = address;
        return ESP_OK;
    }
    i2c_master_bus_rm_device(device);
    device_ = nullptr;
    return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
}

esp_err_t Bma400::configure()
{
    esp_err_t err = writeRegister(regCmd, cmdSoftReset);
    if (err != ESP_OK)
    {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5)); // reset completes within 2 ms; the chip wakes in sleep mode

    const struct
    {
        uint8_t reg;
        uint8_t value;
    } sequence[] = {
        {regAccConfig1, static_cast<uint8_t>(range4g | odr100Hz)},
        {regAccConfig0, powerModeNormal},
        {regGen1Config0, gen1AllAxesEveryTime},
        {regGen1Config1, gen1ActivityAnyAxis},
        {regGen1Config2, static_cast<uint8_t>(Config::accelActivityThresholdMg / 8)},
        {regGen1Config3, static_cast<uint8_t>(Config::accelActivityDurationSamples >> 8)},
        {regGen1Config31, static_cast<uint8_t>(Config::accelActivityDurationSamples & 0xFF)},
        {regInt12IoCtrl, int1ActiveHighPushPull},
        {regIntConfig1, 0x00}, // not latched: one pulse per qualifying sample
        {regInt1Map, gen1ToInt1},
        {regIntConfig0, gen1Enable},
    };
    for (const auto &step : sequence)
    {
        err = writeRegister(step.reg, step.value);
        if (err != ESP_OK)
        {
            return err;
        }
        if (step.reg == regAccConfig0)
        {
            vTaskDelay(pdMS_TO_TICKS(2)); // sleep -> normal mode settles in 1.5 ms
        }
    }
    return ESP_OK;
}

esp_err_t Bma400::writeRegister(uint8_t reg, uint8_t value)
{
    const uint8_t frame[2] = {reg, value};
    return i2c_master_transmit(device_, frame, sizeof frame, static_cast<int>(i2cTimeoutMs));
}

esp_err_t Bma400::readRegister(uint8_t reg, uint8_t &value)
{
    return i2c_master_transmit_receive(device_, &reg, 1, &value, 1, static_cast<int>(i2cTimeoutMs));
}

uint32_t Bma400::takeActivityCount()
{
    return activity_.exchange(0, std::memory_order_relaxed);
}

void IRAM_ATTR Bma400::isrEntry(void *self)
{
    static_cast<Bma400 *>(self)->activity_.fetch_add(1, std::memory_order_relaxed);
}
