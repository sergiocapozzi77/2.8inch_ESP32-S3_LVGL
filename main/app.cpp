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
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
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

    wifi_manager.init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

    // // Disable WiFi power saving to keep WiFi always on
    // esp_err_t ret = esp_wifi_set_ps(WIFI_PS_NONE);
    // if (ret == ESP_OK)
    // {
    //     ESP_LOGI(TAG, "WiFi power saving disabled - WiFi will stay always on");
    // }
    // else
    // {
    //     ESP_LOGW(TAG, "Failed to disable WiFi power saving: %s", esp_err_to_name(ret));
    // }

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Light Sleep (WiFi stays connected)
// ============================================================

void Application::enterLightSleep()
{
    ESP_LOGI(TAG, "Entering light sleep (WiFi stays on)...");

    // 1. Blank screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(NULL);

    // 2. Stop LVGL timers
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

    // 6. Configure wake source (GPIO low level)
    esp_sleep_enable_ext1_wakeup(
        1ULL << WAKE_GPIO,
        ESP_EXT1_WAKEUP_ANY_LOW);

    // 7. Enable automatic light sleep with WiFi
    // esp_sleep_enable_wifi_wakeup();

    ESP_LOGI(TAG, "Entering light sleep (WiFi maintained)...");

    int level = gpio_get_level(WAKE_GPIO);
    ESP_LOGI(TAG, "Wake pin level before sleep: %d", level);

    // Enter light sleep - WiFi stays connected
    esp_light_sleep_start();

    // ============================================================
    // WAKE UP - Resume from here
    // ============================================================

    ESP_LOGI(TAG, "Waking from light sleep...");

    // Wake up LCD
    ili9341_sleep_out();
    vTaskDelay(pdMS_TO_TICKS(120));

    // Turn on backlight
    gpio_set_level(BACKLIGHT_GPIO, 1);

    // Restart barcode reader
    if (barcode_reader)
        barcode_reader->on();

    // Resume LVGL
    lv_timer_enable(true);

    // Clear screen and refresh
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_refr_now(NULL);

    // Reset inactivity timer
    lv_disp_trig_activity(NULL);

    ESP_LOGI(TAG, "Resumed from light sleep - WiFi still connected");
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

        uint32_t inactive = lv_disp_get_inactive_time(NULL);
        if (inactive > SLEEP_TIMEOUT_MS)
        {
            enterLightSleep(); // Use light sleep instead of deep sleep
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

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wake cause: %d", cause);

    initNVS();
    ESP_ERROR_CHECK(product_cache.init());
    initHardware();
    initQueues();
    initTasks();

    mainLoop();
}