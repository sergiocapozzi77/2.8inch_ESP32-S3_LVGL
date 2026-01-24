#pragma once

#include "esp_timer.h"
#include "BatteryManager.h"

class LVGLManager
{
public:
    void init();
    void tick();

private:
    static void lvglTickCallback(void *arg);
    BatteryManager battery_manager;
    void updateBatteryUI();

    esp_timer_handle_t lvgl_tick_timer;
};
