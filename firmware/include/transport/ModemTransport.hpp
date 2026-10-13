#pragma once

#include "periph/Sim800Modem.hpp"
#include "reliability/Settings.hpp"
#include "transport/IUplinkTransport.hpp"

// IUplinkTransport on the SIM800C: connect = power on + GPRS bearer,
// reset = power cycle + bearer again.
class ModemTransport final : public IUplinkTransport
{
public:
    ModemTransport(Sim800Modem &modem, const char *apn);

    esp_err_t connect() override;
    bool isConnected() const override { return connected_; }
    esp_err_t reset() override;
    esp_err_t httpGet(const char *url, uint16_t &status) override;
    uint8_t signalQuality() override;
    esp_err_t setApn(const char *apn) override;

private:
    Sim800Modem &modem_;
    char apn_[Settings::apnCapacity] = {};
    bool connected_ = false;
};
