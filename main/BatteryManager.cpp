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
