#include "BatteryManager.h"
#include <algorithm>

BatteryManager::BatteryManager(adc1_channel_t channel) : _channel(channel) {}

void BatteryManager::init()
{
    // 12-bit resolution (0-4095)
    adc1_config_width(ADC_WIDTH_BIT_12);
    // 11dB attenuation allows reading up to ~3.1V on the pin
    adc1_config_channel_atten(_channel, ADC_ATTEN_DB_11);
}

float BatteryManager::getVoltage()
{
    // Take 10 samples and average them to smooth out noise
    int samples = 0;
    for (int i = 0; i < 10; i++)
    {
        samples += adc1_get_raw(_channel);
    }
    float raw_avg = (float)samples / 10.0f;

    // Convert to voltage: (Raw / Max Steps) * Vref * Divider
    // On this board: (Raw / 4095) * 3.3V * 2
    float voltage = (raw_avg / 4095.0f) * _reference_voltage * _divider_ratio;

    return voltage;
}

int BatteryManager::getPercentage()
{
    float voltage = getVoltage();

    // Li-Po discharge curve is not linear, but this is a good approximation:
    // 4.2V = 100%, 3.5V = 0%
    if (voltage >= 4.2f)
        return 100;
    if (voltage <= 3.5f)
        return 0;

    int percentage = (int)((voltage - 3.5f) * 100.0f / (4.2f - 3.5f));
    return std::max(0, std::min(100, percentage));
}