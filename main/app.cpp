#include "app.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "ili9341.h"
#include "sdkconfig.h"

static const char *TAG = "APP";

// Configuration
#define SLEEP_TIMEOUT_MS 60000
#define QUEUE_SIZE 8
#define WAKE_GPIO GPIO_NUM_17
#define BACKLIGHT_GPIO GPIO_NUM_45
#define LCD_SLEEP_DELAY_MS 120

// ============================================================
// Initialization
// ============================================================

void Application::initNVS()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing truncated NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

void Application::initQueues()
{
    barcode_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);
    product_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);

    if (!barcode_queue || !product_queue)
    {
        ESP_LOGE(TAG, "Queue creation failed");
        abort();
    }

    ESP_LOGI(TAG, "Queues created");
}

void Application::initTasks()
{
    barcode_reader = new BarcodeReader(barcode_queue);
    ESP_ERROR_CHECK(barcode_reader->init());

    product_fetcher = new ProductFetcher(
        barcode_queue,
        product_queue,
        &product_cache,
        &product_service);

    ESP_ERROR_CHECK(product_fetcher->start());

    ESP_LOGI(TAG, "Tasks started");
}

void Application::initHardware()
{
    gpio_set_direction(BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BACKLIGHT_GPIO, 1);

    lvgl_manager.init();

    wifi_manager.init(CONFIG_WIFI_SSID,
                      CONFIG_WIFI_PASSWORD);

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Deep Sleep
// ============================================================

void Application::enterDeepSleep()
{
    ESP_LOGI(TAG, "Entering deep sleep...");

    // 1. Blank screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(NULL);

    // 2. Stop LVGL
    lv_timer_enable(false);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. Turn off backlight
    gpio_set_level(BACKLIGHT_GPIO, 0);
    ESP_LOGI(TAG, "Backlight off");

    // 4. Shutdown barcode
    if (barcode_reader)
        barcode_reader->off();

    // 5. Put LCD to sleep
    ili9341_sleep_in();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));

    // 6. Clean WiFi shutdown
    esp_wifi_stop();
    esp_wifi_deinit();

    // 7. Configure wake source
    esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0);

    ESP_LOGI(TAG, "Entering deep sleep...");
    esp_deep_sleep_start();
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    while (true)
    {
        // Reset inactivity timer if barcode activity
        if (uxQueueMessagesWaiting(barcode_queue) > 0)
        {
            lv_disp_trig_activity(NULL);
        }

        uint32_t inactive =
            lv_disp_get_inactive_time(NULL);

        if (inactive > SLEEP_TIMEOUT_MS)
        {
            enterDeepSleep();
        }

        lvgl_manager.tick();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ============================================================
// Run
// ============================================================

void Application::run()
{
    ESP_LOGI(TAG, "Booting application...");

    esp_sleep_wakeup_cause_t cause =
        esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "Wake cause: %d", cause);

    initNVS();

    ESP_ERROR_CHECK(product_cache.init());

    initHardware();
    initQueues();
    initTasks();

    mainLoop();
}
