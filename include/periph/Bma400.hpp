#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "driver/gpio.h"
#include "esp_err.h"

#include "logic/MotionDetector.hpp"
#include "periph/I2cBus.hpp"

// Bosch BMA400 accelerometer on the CJMCU-400 breakout. The chip does the
// motion detection itself: its generic interrupt 1 is configured for
// "activity", a change between consecutive samples above a threshold on
// any axis, and pulses INT1 for every sample that qualifies. The ISR only
// counts those pulses; AccelMotionDetector reads the count once per epoch
// through IActivitySource and applies the stop delay, so no I2C traffic
// happens after begin() and nothing shares the bus with the display at
// run time.
//
// The address follows the SDO pin (0x14 low, 0x15 high); begin() probes
// both. A silent chip is reported, not fatal: the tracker falls back to
// GNSS speed for motion.
class Bma400 final : public IActivitySource
{
public:
    static constexpr uint8_t chipId = 0x90;

    Bma400(I2cBus &bus, gpio_num_t interruptPin);

    // Probes, soft-resets and configures the chip, then arms the INT1
    // interrupt. ESP_ERR_NOT_FOUND when no chip answers at either address.
    esp_err_t begin();

    bool present() const { return present_; }
    uint8_t address() const { return address_; }

    // IActivitySource: activity pulses since the previous call.
    uint32_t takeActivityCount() override;

private:
    static void isrEntry(void *self);

    esp_err_t probe(uint8_t address);
    esp_err_t configure();
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t &value);

    I2cBus &bus_;
    gpio_num_t interruptPin_;
    i2c_master_dev_handle_t device_ = nullptr;
    uint8_t address_ = 0;
    bool present_ = false;
    std::atomic<uint32_t> activity_{0};
};
