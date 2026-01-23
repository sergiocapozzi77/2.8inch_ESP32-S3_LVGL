
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MAX_BARCODE_LEN 64

class BarcodeReader
{
public:
    BarcodeReader(QueueHandle_t queue);
    void init();
    static void task(void *arg);
    void off();

private:
    QueueHandle_t barcode_queue;
};
