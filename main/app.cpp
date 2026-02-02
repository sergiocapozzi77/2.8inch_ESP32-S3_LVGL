#include "app.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "BarcodeReader.h"
#include "ProductFetcher.h"
#include "product_cache.h"
#include "esp_wifi.h"
#include "ili9341.h"

static const char *TAG = "APP";

// Configuration - Move to Kconfig or separate config file
#define SLEEP_TIMEOUT_MS 60000
#define QUEUE_SIZE 8
#define WAKE_GPIO GPIO_NUM_17
#define BACKLIGHT_GPIO GPIO_NUM_45
#define LCD_SLEEP_DELAY_MS 120

// ============================================================
// Application Implementation
// ============================================================

void Application::initQueues()
{
    // Prevent queue recreation on wake
    if (barcode_queue == NULL)
    {
        barcode_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);
        if (barcode_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create barcode queue");
            abort();
        }
    }

    if (product_queue == NULL)
    {
        product_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);
        if (product_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create product queue");
            abort();
        }
    }

    ESP_LOGI(TAG, "Queues initialized");
}

void Application::initTasks()
{
    // Initialize barcode reader
    if (barcode_reader == NULL)
    {
        barcode_reader = new BarcodeReader(barcode_queue);
    }

    esp_err_t err = barcode_reader->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize barcode reader: %s", esp_err_to_name(err));
        abort();
    }

    // Initialize product fetcher
    if (product_fetcher == NULL)
    {
        product_fetcher = new ProductFetcher(
            barcode_queue,
            product_queue,
            &product_cache,
            &product_service);
    }

    err = product_fetcher->start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start product fetcher: %s", esp_err_to_name(err));
        abort();
    }

    ESP_LOGI(TAG, "Tasks initialized");
}

void Application::enterDeepSleep()
{
    ESP_LOGI(TAG, "Preparing for deep sleep...");

    // 1. Blank screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(NULL);

    // 2. Stop LVGL timers
    lv_timer_enable(false);
    vTaskDelay(pdMS_TO_TICKS(50)); // Allow LVGL to finish

    // 3. Turn off backlight
    gpio_set_level(BACKLIGHT_GPIO, 0);
    ESP_LOGI(TAG, "Backlight off");

    // 4. Turn off barcode reader
    barcode_reader->off();
    ESP_LOGI(TAG, "Barcode reader off");

    // 5. Put LCD to sleep
    ili9341_sleep_in();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));
    ESP_LOGI(TAG, "LCD sleep mode enabled");

    // 6. Stop WiFi
    esp_wifi_stop();
    ESP_LOGI(TAG, "WiFi stopped");

    // 7. Configure wake source (touch screen on GPIO17)
    esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0);
    ESP_LOGI(TAG, "Wake source configured: GPIO%d", WAKE_GPIO);

    // 8. Enter deep sleep
    ESP_LOGI(TAG, "Entering deep sleep...");
    esp_deep_sleep_start();
}

void Application::wakeFromSleep()
{
    ESP_LOGI(TAG, "Waking from deep sleep...");

    // Wake LCD
    ili9341_sleep_out();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));

    // Turn on backlight
    gpio_set_level(BACKLIGHT_GPIO, 1);

    // Re-enable LVGL
    lv_timer_enable(true);
    lv_disp_trig_activity(NULL);

    // Restart barcode reader
    barcode_reader->init();

    ESP_LOGI(TAG, "Wake sequence complete");
}

void Application::mainLoop()
{
    while (1)
    {
        // Reset inactivity timer on barcode activity
        if (uxQueueMessagesWaiting(barcode_queue) > 0)
        {
            lv_disp_trig_activity(NULL);
        }

        // Check for sleep timeout
        uint32_t inactive_time = lv_disp_get_inactive_time(NULL);
        if (inactive_time > SLEEP_TIMEOUT_MS)
        {
            enterDeepSleep();
            // Never returns - deep sleep resets ESP32
        }

        // Update LVGL
        lvgl_manager.tick();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Application::run()
{
    ESP_LOGI(TAG, "Initializing application...");

    // Check wake up reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool is_wake_from_sleep = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);

    if (is_wake_from_sleep)
    {
        ESP_LOGI(TAG, "Wake reason: Touch screen (GPIO%d)", WAKE_GPIO);
    }
    else
    {
        ESP_LOGI(TAG, "Wake reason: Power on / Reset");
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize product cache
    ESP_ERROR_CHECK(product_cache.init());

    // Initialize managers
    lvgl_manager.init();

    // Get WiFi credentials from NVS or Kconfig instead of hardcoding
    wifi_manager.init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

    // Initialize queues and tasks
    initQueues();
    initTasks();

    // Handle wake-specific initialization
    if (is_wake_from_sleep)
    {
        wakeFromSleep();
    }

    // Run main loop
    mainLoop();
}