#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "driver/uart.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_timer.h"
#include "ui.h"
#include "esp_log.h"

// UART config for GM865
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 9600
#define UART_TX_PIN 44 // adjust to your wiring
#define UART_RX_PIN 43
#define UART_BUF_SIZE 256

#define TAG "MAIN"

static QueueHandle_t barcode_queue;

static void inc_lvgl_tick(void *arg)
{
    lv_tick_inc(1); // 1 ms tick
}

#define MAX_BARCODE_LEN 64

static void barcode_task(void *arg)
{
    static char line_buf[MAX_BARCODE_LEN];
    static int line_pos = 0;
    uint8_t byte;

    while (1)
    {
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(50));
        if (len > 0)
        {
            if (byte == '\r' || byte == '\n')
            { // end of barcode
                if (line_pos > 0)
                {
                    line_buf[line_pos] = 0; // null-terminate
                    ESP_LOGI(TAG, "Barcode: %s", line_buf);
                    // Send to UI thread
                    xQueueSend(barcode_queue, line_buf, 0);

                    line_pos = 0; // reset buffer
                }
            }
            else
            {
                if (line_pos < MAX_BARCODE_LEN - 1)
                {
                    line_buf[line_pos++] = byte;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    ESP_LOGI("LVGL", "LVGL initialised");

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &inc_lvgl_tick,
        .name = "lvgl_tick"};

    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();

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

    // Start FreeRTOS task for barcode reading
    barcode_queue = xQueueCreate(4, MAX_BARCODE_LEN);
    xTaskCreate(barcode_task, "barcode_task", 4096, NULL, 5, NULL);

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