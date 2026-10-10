#include "periph/Sim800Modem.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.hpp"

namespace
{
    constexpr const char *logTag = "sim800";

    constexpr AtCommand probe = {"AT", "OK", 500};
    constexpr AtCommand echoOff = {"ATE0", "OK", 1000};
    constexpr AtCommand fixBaud = {"AT+IPR=115200", "OK", 1000};
    static_assert(Config::gsmBaudRate == 115200, "fixBaud must match Config::gsmBaudRate");
}

Sim800Modem::Sim800Modem(UartPort &uart, gpio_num_t powerKeyPin)
    : uart_(uart), powerKeyPin_(powerKeyPin)
{
}

void Sim800Modem::begin()
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << powerKeyPin_;
    // Open drain: PWRKEY is pulled up inside the modem to its own rail, so
    // the GPIO only ever pulls it down and never drives 3.3 V into it.
    config.mode = GPIO_MODE_OUTPUT_OD;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
    // Released (high impedance) until a power pulse is needed.
    ESP_ERROR_CHECK(gpio_set_level(powerKeyPin_, 1));
}

void Sim800Modem::wait(uint32_t ms)
{
    // Sliced so the owning task's watchdog is fed through the modem's
    // multi-second boot time.
    while (ms > 0)
    {
        const uint32_t slice = ms < 500 ? ms : 500;
        vTaskDelay(pdMS_TO_TICKS(slice));
        if (idleHook_ != nullptr)
        {
            idleHook_();
        }
        ms -= slice;
    }
}

void Sim800Modem::pulsePowerKey()
{
    gpio_set_level(powerKeyPin_, 0);
    wait(Config::gsmPowerKeyPulseMs);
    gpio_set_level(powerKeyPin_, 1);
}

bool Sim800Modem::probeBaudRates()
{
    for (uint32_t baud : Config::gsmProbeBaudRates)
    {
        if (uart_.setBaudRate(baud) != ESP_OK)
        {
            continue;
        }
        uart_.discardInput();
        // Several probes per rate: in autobaud mode the modem locks onto
        // the "AT" pattern itself and may swallow the first one.
        for (uint8_t i = 0; i < 3; ++i)
        {
            if (execute(probe) == ESP_OK)
            {
                ESP_LOGI(logTag, "Modem answered at %" PRIu32 " baud", baud);
                return true;
            }
        }
    }
    return false;
}

esp_err_t Sim800Modem::powerOn()
{
    for (uint8_t attempt = 0; attempt < Config::gsmPowerOnAttempts; ++attempt)
    {
        if (probeBaudRates())
        {
            // Pin the rate: from now on the modem and the UART agree
            // regardless of how the board shipped.
            esp_err_t err = execute(fixBaud);
            if (err == ESP_OK)
            {
                err = uart_.setBaudRate(Config::gsmBaudRate);
            }
            if (err == ESP_OK)
            {
                uart_.discardInput();
                err = execute(echoOff);
            }
            return err;
        }
        ESP_LOGW(logTag, "Modem silent at every baud rate, pulsing PWRKEY (%u/%u)", attempt + 1,
                 Config::gsmPowerOnAttempts);
        pulsePowerKey();
        wait(Config::gsmBootWaitMs);
        uart_.discardInput();
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t Sim800Modem::powerCycle()
{
    // AT+CPOWD is the graceful way; if the modem is wedged the key pulse
    // below does it anyway.
    const AtCommand powerDown = {"AT+CPOWD=1", "POWER DOWN", 3000};
    if (execute(powerDown) != ESP_OK)
    {
        pulsePowerKey();
    }
    wait(Config::gsmBootWaitMs);
    uart_.discardInput();
    return powerOn();
}

esp_err_t Sim800Modem::execute(const AtCommand &command, char *response, size_t capacity)
{
    if (response == nullptr)
    {
        response = scratch_;
        capacity = sizeof scratch_;
    }

    uart_.discardInput();
    esp_err_t err = uart_.write(command.command);
    if (err == ESP_OK)
    {
        err = uart_.write("\r");
    }
    if (err != ESP_OK)
    {
        return err;
    }

    err = readUntil(command.expect, command.timeoutMs, response, capacity);
    if (err != ESP_OK)
    {
        ESP_LOGD(logTag, "%s -> %s: %s", command.command, esp_err_to_name(err), response);
    }
    return err;
}

esp_err_t Sim800Modem::executeSequence(const AtCommand *commands, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        const esp_err_t err = execute(commands[i]);
        if (err != ESP_OK)
        {
            ESP_LOGW(logTag, "'%s' failed: %s", commands[i].command, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t Sim800Modem::readUntil(const char *expect, uint32_t timeoutMs,
                                 char *response, size_t capacity)
{
    const int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
    size_t length = 0;
    response[0] = '\0';

    while (true)
    {
        if (idleHook_ != nullptr)
        {
            idleHook_();
        }
        const int64_t remainingUs = deadlineUs - esp_timer_get_time();
        if (remainingUs <= 0)
        {
            return ESP_ERR_TIMEOUT;
        }
        // Chunks of at most one second so the idle hook runs often enough.
        const int64_t sliceUs = remainingUs < 1'000'000 ? remainingUs : 1'000'000;
        const TickType_t ticks = pdMS_TO_TICKS((sliceUs + 999) / 1000) + 1;

        // Keep one byte for the terminator; a response that outgrows the
        // buffer is kept truncated, and the search still runs on what fit.
        const size_t room = capacity - 1 - length;
        uint8_t chunk[64];
        const size_t n = uart_.read(chunk, sizeof chunk, ticks);
        if (n == 0)
        {
            continue;
        }
        const size_t keep = n < room ? n : room;
        memcpy(response + length, chunk, keep);
        length += keep;
        response[length] = '\0';

        if (strstr(response, expect) != nullptr)
        {
            return ESP_OK;
        }
        if (strstr(response, "ERROR") != nullptr)
        {
            return ESP_FAIL;
        }
    }
}

esp_err_t Sim800Modem::openBearer(const char *apn)
{
    char apnCommand[Config::gsmCommandBytes];
    snprintf(apnCommand, sizeof apnCommand, "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);

    const AtCommand sequence[] = {
        {"AT+CFUN=1", "OK", 2000},
        {"AT+CGATT=1", "OK", 10'000},
        {"AT+SAPBR=3,1,\"Contype\",\"GPRS\"", "OK", 2000},
        {apnCommand, "OK", 2000},
        {"AT+SAPBR=1,1", "OK", 30'000},
        {"AT+SAPBR=2,1", "OK", 2000},
    };
    return executeSequence(sequence, sizeof sequence / sizeof sequence[0]);
}

esp_err_t Sim800Modem::closeBearer()
{
    const AtCommand close = {"AT+SAPBR=0,1", "OK", 5000};
    return execute(close);
}

esp_err_t Sim800Modem::httpGet(const char *url, uint16_t &status)
{
    char urlCommand[Config::gsmCommandBytes];
    const int n = snprintf(urlCommand, sizeof urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"", url);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof urlCommand)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    // HTTPTERM first clears a stack left open by an earlier failure; its
    // own error is expected and ignored.
    const AtCommand terminate = {"AT+HTTPTERM", "OK", 1000};
    execute(terminate);

    const AtCommand setup[] = {
        {"AT+HTTPINIT", "OK", 2000},
        {"AT+HTTPPARA=\"CID\",1", "OK", 2000},
        {urlCommand, "OK", 2000},
    };
    esp_err_t err = executeSequence(setup, sizeof setup / sizeof setup[0]);
    if (err != ESP_OK)
    {
        execute(terminate);
        return err;
    }

    // "+HTTPACTION: 0,<status>,<length>" arrives unsolicited after OK.
    const AtCommand action = {"AT+HTTPACTION=0", "+HTTPACTION:", Config::gsmHttpTimeoutMs};
    err = execute(action, scratch_, sizeof scratch_);
    if (err == ESP_OK)
    {
        const char *marker = strstr(scratch_, "+HTTPACTION:");
        // Wait for the rest of the line if the marker arrived alone.
        if (marker != nullptr && strchr(marker, ',') == nullptr)
        {
            char tail[64];
            if (readUntil("\n", 2000, tail, sizeof tail) == ESP_OK)
            {
                strncat(scratch_, tail, sizeof scratch_ - strlen(scratch_) - 1);
                marker = strstr(scratch_, "+HTTPACTION:");
            }
        }
        const char *comma = marker != nullptr ? strchr(marker, ',') : nullptr;
        if (comma == nullptr)
        {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        else
        {
            status = static_cast<uint16_t>(strtoul(comma + 1, nullptr, 10));
        }
    }

    execute(terminate);
    return err;
}

void Sim800Modem::logNetworkStatus()
{
    const AtCommand queries[] = {
        {"AT+CPIN?", "OK", 2000},   // READY, or SIM PIN / no SIM
        {"AT+CREG?", "OK", 2000},   // +CREG: 0,<stat>: 1 home, 5 roaming, 2 searching, 3 denied, 0 none
        {"AT+CGATT?", "OK", 2000},  // +CGATT: 1 attached
        {"AT+CSQ", "OK", 2000},     // +CSQ: <rssi 0..31>,<ber>
        {"AT+COPS?", "OK", 5000},   // operator name
    };
    for (const AtCommand &query : queries)
    {
        const esp_err_t err = execute(query, scratch_, sizeof scratch_);
        // Collapse the response onto one log line.
        for (char *c = scratch_; *c != '\0'; ++c)
        {
            if (*c == '\r' || *c == '\n')
            {
                *c = ' ';
            }
        }
        ESP_LOGW(logTag, "%s -> %s%s", query.command, scratch_,
                 err == ESP_OK ? "" : esp_err_to_name(err));
    }
}

esp_err_t Sim800Modem::signalQuality(uint8_t &csq)
{
    const AtCommand query = {"AT+CSQ", "OK", 1000};
    const esp_err_t err = execute(query, scratch_, sizeof scratch_);
    if (err != ESP_OK)
    {
        return err;
    }
    const char *marker = strstr(scratch_, "+CSQ:");
    if (marker == nullptr)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    csq = static_cast<uint8_t>(strtoul(marker + 5, nullptr, 10));
    return ESP_OK;
}
