#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// One UART with the driver's interrupt-fed receive ring buffer. read()
// parks only the calling task; write() queues into the TX FIFO without
// waiting for the bytes to leave. The GNSS receiver and the modem each own
// one instance, so no locking is needed on top.
class UartPort
{
public:
    UartPort(uart_port_t port, gpio_num_t txPin, gpio_num_t rxPin,
             uint32_t baudRate, size_t rxBufferBytes);

    void begin();

    esp_err_t setBaudRate(uint32_t baudRate);

    // Bytes copied out, 0 on timeout. Returns as soon as at least one byte
    // is available, so a caller that wants a full line keeps calling.
    size_t read(uint8_t *buffer, size_t capacity, TickType_t ticksToWait);

    esp_err_t write(const uint8_t *data, size_t length);
    esp_err_t write(const char *text);

    esp_err_t discardInput();

    uart_port_t port() const { return port_; }

private:
    uart_port_t port_;
    gpio_num_t txPin_;
    gpio_num_t rxPin_;
    uint32_t baudRate_;
    size_t rxBufferBytes_;
};
