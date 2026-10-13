#include "transport/ModemTransport.hpp"

#include <string.h>

#include "esp_log.h"

namespace
{
    constexpr const char *logTag = "uplink";
}

ModemTransport::ModemTransport(Sim800Modem &modem, const char *apn)
    : modem_(modem)
{
    strlcpy(apn_, apn, sizeof apn_);
}

esp_err_t ModemTransport::setApn(const char *apn)
{
    if (strcmp(apn, apn_) == 0)
    {
        return ESP_OK;
    }
    strlcpy(apn_, apn, sizeof apn_);
    ESP_LOGI(logTag, "APN changed to %s", apn_);
    if (!connected_)
    {
        return ESP_OK;
    }
    // Re-open the bearer on the new APN without a power cycle.
    connected_ = false;
    modem_.closeBearer();
    const esp_err_t err = modem_.openBearer(apn_);
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
        // No answer at all: the scheduler's failure count decides when
        // the modem is power-cycled.
        ESP_LOGW(logTag, "HTTP GET failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status >= Sim800Modem::firstLocalHttpStatus)
    {
        // 6xx is the modem reporting its own failure (601 network error,
        // 603 DNS, 604 stack busy...), not a server answer: the bearer is
        // gone, usually after a radio restart or a network hiccup. Drop
        // to disconnected so the uplink task re-opens it, which takes a
        // few seconds and no power cycle.
        ESP_LOGW(logTag, "Bearer lost (modem HTTP status %u); reconnecting", status);
        connected_ = false;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
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
