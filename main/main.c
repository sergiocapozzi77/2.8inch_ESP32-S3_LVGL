#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_timer.h"
#include "ui.h"
#include "esp_log.h"

static void inc_lvgl_tick(void *arg)
{
    lv_tick_inc(1);   // 1 ms tick
}

void app_main(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    ESP_LOGI("LVGL", "LVGL initialised");

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &inc_lvgl_tick,
        .name = "lvgl_tick"
    };

    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();

    while (1)
    {
        ui_tick();        // EEZ Studio tick
        lv_timer_handler();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}