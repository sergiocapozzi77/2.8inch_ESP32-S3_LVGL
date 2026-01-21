#pragma once

#include <string>
#include <map>
#include "product_types.h"
#include "esp_err.h"

class ProductCache
{
public:
    ProductCache();
    ~ProductCache();

    // Initialize cache (loads from NVS)
    esp_err_t init();

    // Check if product is in cache
    bool contains(const std::string &barcode) const;

    // Get product from cache
    bool get(const std::string &barcode, ProductCacheItem &out) const;

    // Add product to cache and persist to NVS
    esp_err_t add(const ProductCacheItem &item);

    // Clear all cache
    void clear();

    // Get cache size
    size_t size() const;

private:
    std::map<std::string, ProductCacheItem> cache_map;
    bool initialized;

    // NVS namespace for cache storage
    static constexpr const char *NVS_NAMESPACE = "product_cache";

    // Save entire cache to NVS
    esp_err_t saveCacheToNVS();

    // Load cache from NVS
    esp_err_t loadCacheFromNVS();
};
