#include "app.h"
#include "esp_lcd_panel_ops.h"
#include "driver/uart.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui.h"
#include "fetchproducts.h"
#include "ProductService.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "BarcodeReader.h"

static const char *TAG = "APP";

// Barcode power control (PNP transistor base)
#define BARCODE_PWR_GPIO GPIO_NUM_2 // example, change if needed

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
// ProductFetcher Implementation
// ============================================================
ProductFetcher::ProductFetcher(QueueHandle_t barcode_q, QueueHandle_t product_q, ProductCache *cache, ProductService *service)
    : barcode_queue(barcode_q), product_queue(product_q), product_cache(cache), product_service(service)
{
    persist_queue = xQueueCreate(8, sizeof(ProductPersistItem));
}

void ProductFetcher::start()
{
    xTaskCreate(ProductFetcher::task, "product_fetch", 8192, this, 5, NULL);
    xTaskCreate(ProductFetcher::persistTask, "product_persist", 8192, this, 4, NULL);
}

void ProductFetcher::task(void *arg)
{
    ProductFetcher *self = (ProductFetcher *)arg;
    char barcode[MAX_BARCODE_LEN];

    while (1)
    {
        if (xQueueReceive(self->barcode_queue, barcode, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Fetching product info for: %s", barcode);

            ProductCacheItem item;
            if (fetchProductInfo(std::string(barcode), item, self->product_cache))
            {
                ESP_LOGI(TAG, "Product: %s (%s)", item.name.c_str(), item.category.c_str());
                xQueueSend(self->product_queue, item.name.c_str(), 0);

                // Send to persist queue for database storage
                ProductPersistItem persist_item;
                strncpy(persist_item.name, item.name.c_str(), MAX_PRODUCT_NAME_LEN - 1);
                persist_item.name[MAX_PRODUCT_NAME_LEN - 1] = '\0';
                strncpy(persist_item.category, item.category.c_str(), MAX_CATEGORY_LEN - 1);
                persist_item.category[MAX_CATEGORY_LEN - 1] = '\0';
                persist_item.quantity = 1; // Default quantity

                if (xQueueSend(self->persist_queue, &persist_item, pdMS_TO_TICKS(100)) != pdTRUE)
                {
                    ESP_LOGW(TAG, "Failed to queue product for persistence");
                }
            }
            else
            {
                ESP_LOGW(TAG, "Failed to fetch product info");
            }
        }
    }
}

void ProductFetcher::persistTask(void *arg)
{
    ProductFetcher *self = (ProductFetcher *)arg;
    ProductPersistItem item;

    while (1)
    {
        if (xQueueReceive(self->persist_queue, &item, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Persisting product to database: %s", item.name);

            // Create Product struct for the service
            Product product;
            product.name = std::string(item.name);
            product.category = std::string(item.category);
            product.quantity = item.quantity;

            // Call addOrUpdateProduct (long-running operation)
            if (self->product_service->addOrUpdateProduct(product))
            {
                ESP_LOGI(TAG, "Successfully added/updated product in database: %s", product.name.c_str());
            }
            else
            {
                ESP_LOGW(TAG, "Failed to add/update product in database: %s", product.name.c_str());
            }
        }
    }
}

// ============================================================
// LVGLManager Implementation
// ============================================================
void LVGLManager::lvglTickCallback(void *arg)
{
    lv_tick_inc(1);
}

void LVGLManager::init()
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    const esp_timer_create_args_t timer_args = {
        .callback = &LVGLManager::lvglTickCallback,
        .arg = NULL,
        .name = "lvgl_tick"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();
    ESP_LOGI(TAG, "LVGL initialized");
}

void LVGLManager::tick()
{
    ui_tick();
    lv_timer_handler();
}

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
            gpio_set_level(BARCODE_PWR_GPIO, 0);

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

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << BARCODE_PWR_GPIO;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Barcode ON at boot (PNP = LOW)
    gpio_set_level(BARCODE_PWR_GPIO, 1);

    // Initialize queues and tasks
    initQueues();
    initTasks();

    // Run main loop
    mainLoop();
}
