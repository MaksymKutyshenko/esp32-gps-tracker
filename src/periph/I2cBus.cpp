#include "periph/I2cBus.hpp"

#include "esp_check.h"

I2cBus::I2cBus(i2c_port_num_t port, gpio_num_t sdaPin, gpio_num_t sclPin)
    : port_(port), sdaPin_(sdaPin), sclPin_(sclPin)
{
}

void I2cBus::begin()
{
    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = port_;
    busConfig.sda_io_num = sdaPin_;
    busConfig.scl_io_num = sclPin_;
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    // Both breakout modules carry their own pull-ups; the internal ones only
    // help when a module is unplugged, keeping the bus from floating.
    busConfig.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &bus_));
}

i2c_master_dev_handle_t I2cBus::addDevice(uint8_t address, uint32_t sclSpeedHz)
{
    i2c_device_config_t deviceConfig = {};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = address;
    deviceConfig.scl_speed_hz = sclSpeedHz;

    i2c_master_dev_handle_t device = nullptr;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &deviceConfig, &device));
    return device;
}
