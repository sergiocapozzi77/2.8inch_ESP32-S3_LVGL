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

// Optional: track highest seen voltage
static float v_max_seen = 3.50f;

int BatteryManager::getPercentage()
{
    float v = getVoltage();

    // Track highest observed voltage (slowly)
    if (v > v_max_seen)
        v_max_seen = v;

    const float v_min = 3.50f;
    float v_max = std::max(v_max_seen, 3.90f); // safety floor

    float pct = (v - v_min) / (v_max - v_min) * 100.0f;

    pct = std::clamp(pct, 0.0f, 100.0f);
    return static_cast<int>(pct);
}