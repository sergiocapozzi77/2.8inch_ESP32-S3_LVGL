#include "BarcodeReader.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "BarcodeReader";
// UART Configuration
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 9600
#define UART_TX_PIN 44
#define UART_RX_PIN 43
#define UART_BUF_SIZE 256

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
    xTaskCreate(BarcodeReader::task, "barcode_reader", 4096, this, 10, NULL);
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
