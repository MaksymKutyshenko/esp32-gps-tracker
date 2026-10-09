#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#include "periph/AtCommand.hpp"
#include "periph/UartPort.hpp"

// SIM800C over its AT-command UART. This class knows the modem's
// mechanics: power key timing, command framing, final result codes and
// the GPRS bearer. It knows nothing about tracks or servers — the uplink
// transport builds on top of it.
//
// Every wait is bounded by the command's timeout, and a modem that stops
// answering is power-cycled by the caller through powerCycle().
class Sim800Modem
{
public:
    static constexpr size_t responseCapacity = 256;

    Sim800Modem(UartPort &uart, gpio_num_t powerKeyPin);

    void begin();

    // Called between read chunks while waiting for a response, so the
    // owning task can feed its watchdog through waits longer than the
    // watchdog period (GPRS attach, HTTP action).
    using IdleHook = void (*)();
    void setIdleHook(IdleHook hook) { idleHook_ = hook; }

    // Probes with "AT"; if silent, pulses PWRKEY and probes again. Fixes
    // the baud rate (the modem autobauds) and turns echo off.
    esp_err_t powerOn();

    esp_err_t powerCycle();

    // Sends the command and collects the response until `expect`, "ERROR"
    // or the timeout. `response` (optional) receives everything read.
    esp_err_t execute(const AtCommand &command, char *response = nullptr,
                      size_t capacity = 0);

    // Stops at the first failure and returns its error.
    esp_err_t executeSequence(const AtCommand *commands, size_t count);

    // Attaches GPRS and opens the bearer used by the HTTP stack.
    esp_err_t openBearer(const char *apn);
    esp_err_t closeBearer();

    // HTTP GET through the modem's HTTP stack; `status` is the HTTP code.
    esp_err_t httpGet(const char *url, uint16_t &status);

    // +CSQ: 0..31, 99 = unknown.
    esp_err_t signalQuality(uint8_t &csq);

private:
    esp_err_t readUntil(const char *expect, uint32_t timeoutMs, char *response,
                        size_t capacity);
    void pulsePowerKey();
    void wait(uint32_t ms);

    UartPort &uart_;
    gpio_num_t powerKeyPin_;
    char scratch_[responseCapacity] = {};
    IdleHook idleHook_ = nullptr;
};
