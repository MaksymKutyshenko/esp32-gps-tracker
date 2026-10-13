#include "periph/BatteryMonitor.hpp"

#include "esp_adc/adc_cali_scheme.h"

BatteryMonitor::BatteryMonitor(adc_unit_t unit, adc_channel_t channel,
                               adc_atten_t attenuation, uint32_t dividerRatio)
    : unit_(unit), channel_(channel), attenuation_(attenuation), dividerRatio_(dividerRatio)
{
}

void BatteryMonitor::begin()
{
    adc_oneshot_unit_init_cfg_t unitConfig = {};
    unitConfig.unit_id = unit_;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitConfig, &adc_));

    adc_oneshot_chan_cfg_t channelConfig = {};
    channelConfig.atten = attenuation_;
    channelConfig.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, channel_, &channelConfig));

    adc_cali_curve_fitting_config_t calibrationConfig = {};
    calibrationConfig.unit_id = unit_;
    calibrationConfig.chan = channel_;
    calibrationConfig.atten = attenuation_;
    calibrationConfig.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&calibrationConfig, &calibration_));
}

esp_err_t BatteryMonitor::readMillivolts(uint32_t &millivolts)
{
    // The ADC APIs speak bare int; read into that and widen once.
    int raw = 0;
    esp_err_t err = adc_oneshot_read(adc_, channel_, &raw);
    if (err != ESP_OK)
    {
        return err;
    }
    int pinMv = 0;
    err = adc_cali_raw_to_voltage(calibration_, raw, &pinMv);
    if (err != ESP_OK)
    {
        return err;
    }
    millivolts = static_cast<uint32_t>(pinMv < 0 ? 0 : pinMv) * dividerRatio_;
    return ESP_OK;
}
