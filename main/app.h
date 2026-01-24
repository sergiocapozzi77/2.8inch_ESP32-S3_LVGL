#pragma once

#include "freertos/FreeRTOS.h" // MUST be first FreeRTOS header
#include "freertos/task.h"
#include "freertos/queue.h"

#include "WiFiManager.h"
#include "LVGLManager.h"
#include "product_cache.h"
#include "ProductService.h"
#include "BarcodeReader.h"
#include "ProductFetcher.h"

class Application
{
public:
    void run();

private:
    WiFiManager wifi_manager;
    LVGLManager lvgl_manager;
    ProductCache product_cache;
    ProductService product_service;
    BarcodeReader *barcode_reader;
    ProductFetcher *product_fetcher;
    QueueHandle_t barcode_queue;
    QueueHandle_t product_queue;

    void initQueues();
    void initTasks();
    void mainLoop();
};