#include "periph/ConsoleInput.hpp"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#if defined(BOARD_SUPERMINI)
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

namespace
{
    constexpr const char *logTag = "console";
    constexpr size_t driverBufferBytes = 256;
}

void ConsoleInput::begin()
{
#if defined(BOARD_SUPERMINI)
    usb_serial_jtag_driver_config_t config = {};
    config.rx_buffer_size = driverBufferBytes;
    config.tx_buffer_size = driverBufferBytes;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(logTag, "Commands on USB Serial/JTAG; type 'help'");
#else
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, driverBufferBytes, 0, 0, nullptr, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);
    ESP_LOGI(logTag, "Commands on UART0; type 'help'");
#endif
}

size_t ConsoleInput::read(uint8_t *buffer, size_t capacity, uint32_t timeoutMs)
{
#if defined(BOARD_SUPERMINI)
    const int count = usb_serial_jtag_read_bytes(buffer, capacity, pdMS_TO_TICKS(timeoutMs));
#else
    const int count = uart_read_bytes(UART_NUM_0, buffer, capacity, pdMS_TO_TICKS(timeoutMs));
#endif
    return count > 0 ? static_cast<size_t>(count) : 0;
}

bool ConsoleInput::readLine(char *line, size_t capacity, uint32_t timeoutMs)
{
    uint8_t chunk[32];
    const size_t n = read(chunk, sizeof chunk, timeoutMs);
    for (size_t i = 0; i < n; ++i)
    {
        const char c = static_cast<char>(chunk[i]);
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            const size_t length = pendingLength_ < capacity - 1 ? pendingLength_ : capacity - 1;
            memcpy(line, pending_, length);
            line[length] = '\0';
            pendingLength_ = 0;
            return true;
        }
        if (pendingLength_ < sizeof pending_ - 1)
        {
            pending_[pendingLength_++] = c;
        }
        // An overlong line is truncated; the parser rejects what is left.
    }
    return false;
}
