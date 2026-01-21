#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "product_types.h"
#include "product_cache.h"
#include "WiFiManager.h"
#include "ProductService.h"

class BarcodeReader
{
public:
    BarcodeReader(QueueHandle_t queue);
    void init();
    static void task(void *arg);

private:
    QueueHandle_t barcode_queue;
};

class ProductFetcher
{
public:
    ProductFetcher(QueueHandle_t barcode_q, QueueHandle_t product_q, ProductCache *cache, ProductService *service);
    void start();
    static void task(void *arg);
    static void persistTask(void *arg);

private:
    QueueHandle_t barcode_queue;
    QueueHandle_t product_queue;
    QueueHandle_t persist_queue; // Queue for products to persist to database
    ProductCache *product_cache;
    ProductService *product_service;
};

class LVGLManager
{
public:
    void init();
    void tick();

private:
    esp_timer_handle_t lvgl_tick_timer;
    static void lvglTickCallback(void *arg);
};

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
