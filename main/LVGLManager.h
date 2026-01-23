#pragma once

#include "esp_timer.h"

class LVGLManager
{
public:
    void init();
    void tick();

private:
    static void lvglTickCallback(void *arg);

    esp_timer_handle_t lvgl_tick_timer;
};
