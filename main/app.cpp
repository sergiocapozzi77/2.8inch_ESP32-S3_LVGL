#include "app.h"
#include "esp_lcd_panel_ops.h"
#include "driver/uart.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui.h"
#include "ProductService.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "BarcodeReader.h"

static const char *TAG = "APP";

// WiFi Configuration
#define WIFI_SSID "CommunityFibre10Gb_1206C"
#define WIFI_PASSWORD "4kF3zadv5@"

// Product persist queue item (using fixed-size arrays instead of std::string for safe queue passing)
#define MAX_PRODUCT_NAME_LEN 128
#define MAX_CATEGORY_LEN 64

struct ProductPersistItem
{
    char name[MAX_PRODUCT_NAME_LEN];
    char category[MAX_CATEGORY_LEN];
    int quantity;
};

// ============================================================
// Application Implementation
// ============================================================
void Application::initQueues()
{
    barcode_queue = xQueueCreate(8, MAX_BARCODE_LEN);
    product_queue = xQueueCreate(8, MAX_BARCODE_LEN);
    ESP_LOGI(TAG, "Queues created");
}

void Application::initTasks()
{
    barcode_reader = new BarcodeReader(barcode_queue);
    barcode_reader->init();

    product_fetcher = new ProductFetcher(barcode_queue, product_queue, &product_cache, &product_service);
    product_fetcher->start();

    ESP_LOGI(TAG, "Tasks created");
}

void Application::mainLoop()
{
    const uint32_t sleep_timeout_ms = 60000;

    while (1)
    {
        // Reset inactivity timer if barcode received
        if (uxQueueMessagesWaiting(barcode_queue) > 0)
        {
            lv_disp_trig_activity(NULL);
        }

        // Check inactivity timeout
        if (lv_disp_get_inactive_time(NULL) > sleep_timeout_ms)
        {
            ESP_LOGI(TAG, "Inactivity detected. Entering deep sleep...");

            // 1. Blank screen
            lv_obj_t *scr = lv_scr_act();
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
            lv_refr_now(NULL);

            // 2. Optional panel sleep (if you control it)
            // 2. Put the LCD Controller into Deep Sleep
            // This sends the SPI command 0x10 (Sleep In) to the ILI9341V
            // extern esp_lcd_panel_handle_t panel_handle; // Ensure this is accessible
            // if (panel_handle != NULL)
            // {
            //     esp_lcd_panel_disp_on_off(panel_handle, false); // Turns off display output
            //     esp_lcd_panel_disp_sleep(panel_handle, true);   // Puts driver IC to sleep
            //     ESP_LOGI(TAG, "LCD panel put to sleep");
            // }
            // else
            // {
            //     ESP_LOGW(TAG, "Panel handle is NULL, cannot put panel to sleep");
            // }

            // 3. Backlight OFF
            gpio_set_level(GPIO_NUM_45, 0);

            // 4. Barcode OFF
            barcode_reader->off();

            // 5. Wake source
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_17, 0);

            // 6. Deep sleep
            esp_deep_sleep_start();
        }

        lvgl_manager.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Application::run()
{
    ESP_LOGI(TAG, "Initializing application...");

    // Check wake up cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
    {
        ESP_LOGI(TAG, "Woke up from touch screen!");
    }

    // Initialize NVS (required for WiFi and cache)
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize product cache
    ESP_ERROR_CHECK(product_cache.init());

    // Initialize managers
    lvgl_manager.init();
    wifi_manager.init(WIFI_SSID, WIFI_PASSWORD);

    // Initialize queues and tasks
    initQueues();
    initTasks();

    // Run main loop
    mainLoop();
}
