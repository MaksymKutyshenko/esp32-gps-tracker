#pragma once

#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

// Cell voltage through a resistor divider on an ADC pin. Raw counts go
// through the chip's eFuse curve-fitting calibration so readings are
// comparable between boards, then the divider ratio restores the cell
// voltage.
class BatteryMonitor
{
public:
    BatteryMonitor(adc_unit_t unit, adc_channel_t channel, adc_atten_t attenuation,
                   uint32_t dividerRatio);

    void begin();

    // One calibrated reading in millivolts at the cell. A conversion can
    // fail at runtime, so the error is returned for the caller to count.
    esp_err_t readMillivolts(uint32_t &millivolts);

private:
    adc_unit_t unit_;
    adc_channel_t channel_;
    adc_atten_t attenuation_;
    uint32_t dividerRatio_;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
};
