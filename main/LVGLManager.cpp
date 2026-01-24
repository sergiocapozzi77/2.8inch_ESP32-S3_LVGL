#include "LVGLManager.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui.h"
#include "esp_log.h"

static const char *TAG = "LVGL";

void LVGLManager::lvglTickCallback(void *arg)
{
    lv_tick_inc(1);
}

void LVGLManager::init()
{
    battery_manager.init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    const esp_timer_create_args_t timer_args = {
        .callback = &LVGLManager::lvglTickCallback,
        .arg = nullptr,
        .name = "lvgl_tick"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();

    ESP_LOGI(TAG, "LVGL initialized");
}

void LVGLManager::tick()
{
    ui_tick();
    updateBatteryUI();
    lv_timer_handler();
}

void LVGLManager::updateBatteryUI()
{
    static bool first_update = true;
    static int last_battery = -1;
    static uint32_t last_update_ms = 0;

    uint32_t now = lv_tick_get();
    const uint32_t interval = 5000;

    if (!first_update && (now - last_update_ms < interval))
        return;

    first_update = false;
    last_update_ms = now;

    int battery = battery_manager.getPercentage();

    if (battery != last_battery)
    {
        lv_label_set_text_fmt(objects.battery_lbl, "%d%%", battery);
        last_battery = battery;
    }
}