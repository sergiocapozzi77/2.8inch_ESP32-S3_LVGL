#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "product_types.h"
#include "WiFiManager.h"

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
    ProductFetcher(QueueHandle_t barcode_q, QueueHandle_t product_q);
    void start();
    static void task(void *arg);

private:
    QueueHandle_t barcode_queue;
    QueueHandle_t product_queue;
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
    BarcodeReader *barcode_reader;
    ProductFetcher *product_fetcher;
    QueueHandle_t barcode_queue;
    QueueHandle_t product_queue;

    void initQueues();
    void initTasks();
    void mainLoop();
};
