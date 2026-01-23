#include "app.h"

#include "driver/uart.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui.h"
#include "fetchproducts.h"
#include "ProductService.h"

static const char *TAG = "APP";

// UART Configuration
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 9600
#define UART_TX_PIN 44
#define UART_RX_PIN 43
#define UART_BUF_SIZE 256
#define MAX_BARCODE_LEN 64

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
// BarcodeReader Implementation
// ============================================================
BarcodeReader::BarcodeReader(QueueHandle_t queue) : barcode_queue(queue) {}

void BarcodeReader::init()
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART initialized");
}

void BarcodeReader::task(void *arg)
{
    BarcodeReader *self = (BarcodeReader *)arg;
    char line_buf[MAX_BARCODE_LEN];
    int line_pos = 0;
    uint8_t byte;

    while (1)
    {
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(20));

        if (len > 0)
        {
            if (byte == '\r' || byte == '\n')
            {
                if (line_pos > 0)
                {
                    line_buf[line_pos] = 0;
                    ESP_LOGI(TAG, "Scanned barcode: %s", line_buf);
                    xQueueSend(self->barcode_queue, line_buf, 0);
                    line_pos = 0;
                }
            }
            else if (line_pos < MAX_BARCODE_LEN - 1)
            {
                line_buf[line_pos++] = byte;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

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
    xTaskCreate(BarcodeReader::task, "barcode_reader", 4096, barcode_reader, 10, NULL);

    product_fetcher = new ProductFetcher(barcode_queue, product_queue, &product_cache, &product_service);
    product_fetcher->start();

    ESP_LOGI(TAG, "Tasks created");
}

void Application::mainLoop()
{
    char barcode[MAX_BARCODE_LEN];

    while (1)
    {
        // Check for new barcode
        // if (xQueueReceive(barcode_queue, barcode, 0) == pdTRUE)
        // {
        //     lv_label_set_text(objects.debug_lbl, barcode);
        // }

        lvgl_manager.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Application::run()
{
    ESP_LOGI(TAG, "Initializing application...");

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
