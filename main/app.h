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
private:
    QueueHandle_t barcode_queue = NULL;
    QueueHandle_t product_queue = NULL;

    BarcodeReader *barcode_reader = NULL;
    ProductFetcher *product_fetcher = NULL;

    WiFiManager wifi_manager;
    LVGLManager lvgl_manager;
    ProductCache product_cache;
    ProductService product_service;

    void initQueues();
    void initTasks();
    void enterDeepSleep();
    void wakeFromSleep();
    void mainLoop();

public:
    void run();
    ~Application(); // Add destructor for cleanup
};