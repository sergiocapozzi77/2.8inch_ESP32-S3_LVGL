
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class BarcodeReader
{
public:
    BarcodeReader(QueueHandle_t queue);
    esp_err_t init();
    static void task(void *arg);
    void off();
    void on();

private:
    QueueHandle_t barcode_queue;
};
