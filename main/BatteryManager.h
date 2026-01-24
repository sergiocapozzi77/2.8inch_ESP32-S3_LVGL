#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"

class BatteryManager
{
public:
    BatteryManager(adc1_channel_t channel = ADC1_CHANNEL_8); // GPIO 9 is Channel 8

    void init();
    float getVoltage();
    int getPercentage();

private:
    adc1_channel_t _channel;
    const float _divider_ratio = 2.0;
    const float _reference_voltage = 3.3;
};

#endif