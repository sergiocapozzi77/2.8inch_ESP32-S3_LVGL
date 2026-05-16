#include "BarcodeReader.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "ProductFetcher.h"

static const char *TAG = "BarcodeReader";
// UART Configuration
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 9600
static constexpr gpio_num_t UART_TX_PIN = GPIO_NUM_44;
static constexpr gpio_num_t UART_RX_PIN = GPIO_NUM_43;
#define UART_BUF_SIZE 256
// Barcode power control (PNP transistor base)
#define BARCODE_PWR_GPIO GPIO_NUM_2 // example, change if needed

// ============================================================
// BarcodeReader Implementation
// ============================================================
BarcodeReader::BarcodeReader(QueueHandle_t queue) : barcode_queue(queue) {}

esp_err_t BarcodeReader::init()
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

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << BARCODE_PWR_GPIO;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Barcode ON at boot (PNP = LOW)
    gpio_set_level(BARCODE_PWR_GPIO, 1);

    ESP_LOGI(TAG, "UART initialized");
    xTaskCreate(BarcodeReader::task, "barcode_reader", 4096, this, 10, NULL);

    return ESP_OK;
}

void BarcodeReader::on()
{
    // Power on the barcode reader (opposite of off)
    gpio_set_level(BARCODE_PWR_GPIO, 1);

    // Reconfigure UART pins
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "Barcode reader powered on");
}

void BarcodeReader::off()
{

    gpio_set_direction(UART_TX_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(UART_RX_PIN, GPIO_MODE_INPUT);
    gpio_pullup_dis(UART_RX_PIN);
    gpio_pulldown_dis(UART_RX_PIN);

    gpio_set_level(BARCODE_PWR_GPIO, 0);
    ESP_LOGI(TAG, "Barcode reader powered off");
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
                    lv_disp_trig_activity(NULL); // Reset inactivity timer
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
