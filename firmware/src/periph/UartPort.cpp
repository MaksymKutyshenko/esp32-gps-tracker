#include "periph/UartPort.hpp"

#include <string.h>

#include "esp_log.h"

namespace
{
    constexpr const char *logTag = "uart";
}

UartPort::UartPort(uart_port_t port, gpio_num_t txPin, gpio_num_t rxPin,
                   uint32_t baudRate, size_t rxBufferBytes)
    : port_(port), txPin_(txPin), rxPin_(rxPin),
      baudRate_(baudRate), rxBufferBytes_(rxBufferBytes)
{
}

void UartPort::begin()
{
    uart_config_t config = {};
    // The IDF struct speaks bare int; the constant stays uint32_t.
    config.baud_rate = static_cast<int>(baudRate_);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    // No TX ring buffer: write() blocks only for the FIFO, which is fine for
    // the short strings this project sends.
    ESP_ERROR_CHECK(uart_driver_install(port_, static_cast<int>(rxBufferBytes_), 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(port_, &config));
    ESP_ERROR_CHECK(uart_set_pin(port_, txPin_, rxPin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(logTag, "UART%d ready: TX=GPIO%d RX=GPIO%d %" PRIu32 " baud",
             port_, txPin_, rxPin_, baudRate_);
}

esp_err_t UartPort::setBaudRate(uint32_t baudRate)
{
    const esp_err_t err = uart_set_baudrate(port_, baudRate);
    if (err == ESP_OK)
    {
        baudRate_ = baudRate;
    }
    return err;
}

size_t UartPort::read(uint8_t *buffer, size_t capacity, TickType_t ticksToWait)
{
    // uart_read_bytes returns the count, or -1 on a driver error that the
    // boot-time ESP_ERROR_CHECKs would already have caught.
    const int count = uart_read_bytes(port_, buffer, capacity, ticksToWait);
    return count > 0 ? static_cast<size_t>(count) : 0;
}

esp_err_t UartPort::write(const uint8_t *data, size_t length)
{
    const int written = uart_write_bytes(port_, data, length);
    return written == static_cast<int>(length) ? ESP_OK : ESP_FAIL;
}

esp_err_t UartPort::write(const char *text)
{
    return write(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

esp_err_t UartPort::discardInput()
{
    return uart_flush_input(port_);
}
