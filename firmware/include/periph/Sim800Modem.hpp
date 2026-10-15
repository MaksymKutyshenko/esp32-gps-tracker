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
    // +HTTPACTION statuses from 600 up are the modem's own error codes
    // (600 not HTTP PDU, 601 network error, 602 no memory, 603 DNS error,
    // 604 stack busy), not answers from a server.
    static constexpr uint16_t firstLocalHttpStatus = 600;
    static constexpr size_t responseCapacity = 256;

    // powerKeyPin GPIO_NUM_NC: PWRKEY is tied to GND, the modem is always
    // on, and a restart goes through AT+CFUN=1,1 instead of the key.
    // keyViaTransistor: the pin drives an NPN whose collector pulls PWRKEY
    // down, so a press is the pin high; otherwise the pin pulls the line
    // down itself (open drain), so a press is the pin low.
    Sim800Modem(UartPort &uart, gpio_num_t powerKeyPin, bool keyViaTransistor);

    void begin();

    // Called between read chunks while waiting for a response, so the
    // owning task can feed its watchdog through waits longer than the
    // watchdog period (GPRS attach, HTTP action).
    using IdleHook = void (*)();
    void setIdleHook(IdleHook hook) { idleHook_ = hook; }

    // Probes with "AT" at each candidate baud rate; if silent at all of
    // them, pulses PWRKEY and probes again. Once the modem answers it is
    // pinned to Config::gsmBaudRate with AT+IPR and echo is turned off.
    esp_err_t powerOn();

    esp_err_t powerCycle();

    // Sends the command and collects the response until `expect`, "ERROR"
    // or the timeout. `response` (optional) receives everything read.
    esp_err_t execute(const AtCommand &command, char *response = nullptr,
                      size_t capacity = 0);

    // Stops at the first failure and returns its error.
    esp_err_t executeSequence(const AtCommand *commands, size_t count);

    // Attaches GPRS and opens the bearer used by the HTTP stack. Looks at
    // the bearer's state first: one left open or still opening by an
    // earlier attempt (or by the modem surviving an ESP reset) is reused or
    // waited for, since AT+SAPBR=1,1 answers ERROR while it exists.
    esp_err_t openBearer(const char *apn);
    esp_err_t closeBearer();
    // Minimum functionality: radio off, SIM kept, ~2 mA. openBearer()
    // sends CFUN=1 first, so the next connect wakes it without a power
    // cycle and without the cold-boot registration burst.
    esp_err_t sleep();

    // +SAPBR: 1,<status>: 0 connecting, 1 connected, 2 closing, 3 closed.
    esp_err_t bearerStatus(uint8_t &status);
    // +CREG stat: 1 registered home, 5 registered roaming; anything else
    // (0 idle, 2 searching, 3 denied) is not registered.
    esp_err_t isRegistered(bool &registered);

    // HTTP GET through the modem's HTTP stack; `status` is the HTTP code.
    esp_err_t httpGet(const char *url, uint16_t &status);

    // +CSQ: 0..31, 99 = unknown.
    esp_err_t signalQuality(uint8_t &csq);

    // Logs SIM state, network registration, GPRS attach and signal in one
    // place, for bring-up and for the "why did the bearer fail" question.
    void logNetworkStatus();

private:
    esp_err_t readUntil(const char *expect, uint32_t timeoutMs, char *response,
                        size_t capacity);
    void pulsePowerKey();
    void wait(uint32_t ms);
    // True when the modem answered "AT" at one of the candidate rates; the
    // UART is left at that rate.
    bool probeBaudRates();

    UartPort &uart_;
    gpio_num_t powerKeyPin_;
    uint32_t keyPressedLevel_;
    char scratch_[responseCapacity] = {};
    IdleHook idleHook_ = nullptr;
};
