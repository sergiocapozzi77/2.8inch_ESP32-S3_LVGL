#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "product_cache.h"
#include "ProductService.h"
#include "vars.h"

// #define DEBUG_MEAT
//   Product persist queue item (fixed-size for FreeRTOS safety)
#define MAX_PRODUCT_NAME_LEN 128
#define MAX_CATEGORY_LEN 64
#define MAX_BARCODE_LEN 20

struct ProductPersistItem
{
    char name[MAX_PRODUCT_NAME_LEN];
    char category[MAX_CATEGORY_LEN];
    int quantity;
    char barcode[MAX_BARCODE_LEN];
};

class ProductFetcher
{
public:
    ProductFetcher(
        QueueHandle_t barcode_q,
        QueueHandle_t product_q,
        ProductCache *cache);
    QueueHandle_t save_queue;
    esp_err_t start();

    // TaskHandle_t getTaskHandle() const { return task_handle; }
    static void saveProductTask(void *arg);
    int fetchProductInfoWithRetry(const std::string &barcode, ProductCacheItem &out, ProductCache *cache);

private:
    static void task(void *arg);
    static void persistTask(void *arg);

    QueueHandle_t barcode_queue;
    QueueHandle_t product_queue;
    QueueHandle_t persist_queue;

    static std::string toLower(const std::string &s);
    ProductCache *product_cache;
    int fetchProductInfo(const std::string &barcode, ProductCacheItem &out, ProductCache *cache, int retry_count);
    std::string mapUkSupermarketCategory(cJSON *tagsArray, const std::string &productName);
};
