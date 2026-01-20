#include <stdio.h>
#include <cstring>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

extern "C" void app_main(void);

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_timer.h"
#include "ui.h"
#include "esp_log.h"
#include "fetchproducts.h"
#include "product_types.h"

// UART config for GM865
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 9600
#define UART_TX_PIN 44 // adjust to your wiring
#define UART_RX_PIN 43
#define UART_BUF_SIZE 256

#define TAG "MAIN"

static QueueHandle_t barcode_queue; // raw barcodes from UART
static QueueHandle_t product_queue; // processed product info

// ---------------------------------------------------------
// LVGL tick
// ---------------------------------------------------------

static void inc_lvgl_tick(void *arg)
{
    lv_tick_inc(1); // 1 ms tick
}

#define MAX_BARCODE_LEN 64

#define WIFI_SSID "CommunityFibre10Gb_1206C"
#define WIFI_PASSWORD "4kF3zadv5@"

static const char *WIFI_TAG = "WIFI";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(WIFI_TAG, "Connecting to WiFi...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(WIFI_TAG, "Disconnected. Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "WiFi init complete");
}

// ---------------------------------------------------------
// UART barcode reader task (FAST, NON-BLOCKING)
// ---------------------------------------------------------
static void barcode_task(void *arg)
{
    static char line_buf[MAX_BARCODE_LEN];
    static int line_pos = 0;
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

                    xQueueSend(barcode_queue, line_buf, 0);
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

// ---------------------------------------------------------
// Worker task: fetchProductInfo() (SLOW, BLOCKING OK)
// ---------------------------------------------------------
static void product_fetch_task(void *arg)
{
    char barcode[MAX_BARCODE_LEN];

    while (1)
    {
        if (xQueueReceive(barcode_queue, barcode, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Fetching product info for: %s", barcode);

            ProductCacheItem item;
            bool ok = fetchProductInfo(barcode, item);

            if (ok)
            {
                ESP_LOGI(TAG, "Product fetched: %s (%s)",
                         item.name.c_str(), item.category.c_str());

                // Send product name to UI
                xQueueSend(product_queue, item.name.c_str(), 0);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to fetch product info");
            }
        }
    }
}

extern "C" void app_main(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    ESP_LOGI("LVGL", "LVGL initialised");

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &inc_lvgl_tick,
        .arg = NULL,
        .name = "lvgl_tick"};

    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();

    // Init NVS (required for WiFi)
    ESP_ERROR_CHECK(nvs_flash_init());

    // Connect to WiFi
    wifi_init_sta();

    // -----------------------------
    // Initialize UART for GM865
    // -----------------------------
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // -----------------------------
    // Create queues
    // -----------------------------
    barcode_queue = xQueueCreate(8, MAX_BARCODE_LEN);
    product_queue = xQueueCreate(8, MAX_BARCODE_LEN);

    // -----------------------------
    // Start tasks
    // -----------------------------
    xTaskCreate(barcode_task, "barcode_task", 4096, NULL, 10, NULL);
    xTaskCreate(product_fetch_task, "product_fetch_task", 8192, NULL, 5, NULL);

    char barcode[MAX_BARCODE_LEN];

    while (1)
    {
        // Check for new barcode
        if (xQueueReceive(barcode_queue, barcode, 0) == pdTRUE)
        {
            // Update LVGL label
            lv_label_set_text(objects.hello_lbl, barcode);
        }

        ui_tick(); // EEZ Studio tick
        lv_timer_handler();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}