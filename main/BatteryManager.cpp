#include "BatteryManager.h"
#include "esp_adc_cal.h"
#include <algorithm>

static esp_adc_cal_characteristics_t adc_chars;

BatteryManager::BatteryManager(adc1_channel_t channel)
    : _channel(channel)
{
}

void BatteryManager::init()
{
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(_channel, ADC_ATTEN_DB_11);

    // Characterize ADC (per-chip calibration)
    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100, // default Vref in mV
        &adc_chars);
}

float BatteryManager::getVoltage()
{
    uint32_t sum = 0;

    // Average multiple samples to reduce noise
    for (int i = 0; i < 16; i++)
    {
        sum += adc1_get_raw(_channel);
    }

    uint32_t raw = sum / 16;

    // Convert ADC reading to millivolts (calibrated)
    uint32_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

    // Apply voltage divider
    float voltage = (mv / 1000.0f) * _divider_ratio;

    return voltage;
}

int BatteryManager::getPercentage()
{
    float v = getVoltage();

    if (v >= 4.20f)
        return 100;
    if (v >= 4.10f)
        return 95;
    if (v >= 4.00f)
        return 85;
    if (v >= 3.90f)
        return 70;
    if (v >= 3.80f)
        return 55;
    if (v >= 3.70f)
        return 40;
    if (v >= 3.60f)
        return 25;
    if (v >= 3.50f)
        return 10;

    return 0;
}
