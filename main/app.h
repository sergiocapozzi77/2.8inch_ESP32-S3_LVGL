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
    // Queues
    QueueHandle_t barcode_queue = nullptr;
    QueueHandle_t product_queue = nullptr;

    // Task objects
    BarcodeReader *barcode_reader = nullptr;
    ProductFetcher *product_fetcher = nullptr;
    void wakeScreen();
    bool screen_sleeping = false;
    bool wake_flag = false;
    TaskHandle_t mainTaskHandle = nullptr;

    // Initialization steps
    void initNVS();
    void initHardware();
    void initQueues();
    void initTasks();

    static void fetchExpiringProductsAndUpdateCacheTask(void *param);
    // Runtime
    void enterSleep();
    void enterDeepSleep();
    void mainLoop();
    void fetchExpiringProducts();
    void updateProductsCache();

    TaskHandle_t fetchTaskHandle = NULL;

public:
    // Managers / Services
    WiFiManager wifi_manager;
    LVGLManager lvgl_manager;
    ProductCache product_cache;

    void wakeScreenFromISR();
    Application() = default;
    ~Application() = default; // Not strictly needed in embedded reset model

    void run();
};
