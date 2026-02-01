#pragma once

#include <string>
#include "esp_timer.h"
#include "BatteryManager.h"

enum class ProductAction
{
    Added,
    Updated,
    Deleted
};

class LVGLManager
{
public:
    void init();
    void tick();

    static void showProductSnackbar(const std::string &product,
                                    ProductAction action);
    static void showErrorSnackbar(const std::string &error);

private:
    static void snackbarAsync(void *arg);
    static void snackbarErrorAsync(void *arg);
    static void lvglTickCallback(void *arg);
    BatteryManager battery_manager;
    void updateBatteryUI();

    esp_timer_handle_t lvgl_tick_timer;
};
