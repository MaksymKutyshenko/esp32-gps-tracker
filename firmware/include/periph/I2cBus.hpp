#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"

// One shared I2C master bus. Owns the bus handle; peripherals get their own
// device handle (with their own SCL speed) through addDevice() and do their
// transfers themselves.
class I2cBus
{
public:
    I2cBus(i2c_port_num_t port, gpio_num_t sdaPin, gpio_num_t sclPin);

    void begin();
    i2c_master_dev_handle_t addDevice(uint8_t address, uint32_t sclSpeedHz);

private:
    i2c_port_num_t port_;
    gpio_num_t sdaPin_;
    gpio_num_t sclPin_;
    i2c_master_bus_handle_t bus_ = nullptr;
};
