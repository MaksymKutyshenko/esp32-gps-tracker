#include "transport/ModemTransport.hpp"

#include "esp_log.h"

namespace
{
    constexpr const char *logTag = "uplink";
}

ModemTransport::ModemTransport(Sim800Modem &modem, const char *apn)
    : modem_(modem), apn_(apn)
{
}

esp_err_t ModemTransport::connect()
{
    connected_ = false;
    esp_err_t err = modem_.powerOn();
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "Modem power-on failed: %s", esp_err_to_name(err));
        return err;
    }
    err = modem_.openBearer(apn_);
    if (err != ESP_OK)
    {
        ESP_LOGW(logTag, "GPRS bearer failed (APN %s): %s", apn_, esp_err_to_name(err));
        modem_.logNetworkStatus();
        return err;
    }
    connected_ = true;
    ESP_LOGI(logTag, "GPRS up on APN %s", apn_);
    return ESP_OK;
}

esp_err_t ModemTransport::reset()
{
    connected_ = false;
    ESP_LOGW(logTag, "Power-cycling the modem");
    const esp_err_t err = modem_.powerCycle();
    if (err != ESP_OK)
    {
        return err;
    }
    return connect();
}

esp_err_t ModemTransport::httpGet(const char *url, uint16_t &status)
{
    if (!connected_)
    {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = modem_.httpGet(url, status);
    if (err != ESP_OK)
    {
        // A failed request may mean the bearer dropped; the scheduler's
        // failure count decides when to reset.
        ESP_LOGW(logTag, "HTTP GET failed: %s", esp_err_to_name(err));
    }
    return err;
}

uint8_t ModemTransport::signalQuality()
{
    uint8_t csq = 99;
    if (modem_.signalQuality(csq) != ESP_OK)
    {
        return 99;
    }
    return csq;
}
