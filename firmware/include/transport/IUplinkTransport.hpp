#pragma once

#include <stdint.h>

#include "esp_err.h"

// How a position report leaves the device. The uplink task talks only to
// this interface: connect once, send requests, reset when the link looks
// dead. ModemTransport implements it on the SIM800C; NullUplinkTransport
// stands in when no modem is fitted so the rest of the pipeline keeps
// running on the bench.
class IUplinkTransport
{
public:
    virtual ~IUplinkTransport() = default;

    virtual esp_err_t connect() = 0;
    virtual bool isConnected() const = 0;

    // Full power cycle of the link; called after repeated failures.
    virtual esp_err_t reset() = 0;

    virtual esp_err_t httpGet(const char *url, uint16_t &status) = 0;

    // Link quality for the display, 0..31; 99 when unknown.
    virtual uint8_t signalQuality() = 0;

    // New access point name from Settings. A connected link re-opens its
    // bearer on the new APN; a disconnected one just remembers it.
    virtual esp_err_t setApn(const char *apn) = 0;
};

// Null Object: pretends every upload succeeded. Lets the uplink queue
// drain and the status page show the pipeline working when the modem is
// absent or disabled — the records are still on the card.
class NullUplinkTransport final : public IUplinkTransport
{
public:
    esp_err_t connect() override
    {
        connected_ = true;
        return ESP_OK;
    }
    bool isConnected() const override { return connected_; }
    esp_err_t reset() override { return ESP_OK; }
    esp_err_t httpGet(const char *, uint16_t &status) override
    {
        status = 200;
        return ESP_OK;
    }
    uint8_t signalQuality() override { return 99; }
    esp_err_t setApn(const char *) override { return ESP_OK; }

private:
    bool connected_ = false;
};
