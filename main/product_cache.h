#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <ctime>
#include "product_types.h"
#include "esp_err.h"

// Cache entry with timestamp for LRU eviction
struct CacheEntryMetadata
{
    time_t last_access;
    size_t serialized_size;
};

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
    bool get(const std::string &barcode, ProductCacheItem &out);

    // Add product to cache and persist to NVS
    esp_err_t add(const ProductCacheItem &item);

    // Clear all cache
    void clear();

    // Get number of items in cache
    size_t size() const;

    // Get current cache memory usage in bytes
    size_t getMemoryUsage() const;

    // Get max allowed cache size in bytes
    size_t getMaxCacheSize() const { return MAX_CACHE_SIZE_BYTES; }

private:
    struct CacheEntry
    {
        ProductCacheItem item;
        CacheEntryMetadata metadata;
    };

    std::map<std::string, CacheEntry> cache_map;
    bool initialized;

    // NVS namespace for cache storage
    static constexpr const char *NVS_NAMESPACE = "product_cache";

    // Maximum cache size in bytes (100KB out of 256KB NVS)
    // This leaves ~150KB for other system data and NVS overhead
    static constexpr size_t MAX_CACHE_SIZE_BYTES = 102400; // 100KB

    // Maximum age for cache entries (24 hours)
    static constexpr uint32_t MAX_ENTRY_AGE_SECONDS = 86400;

    // Save entire cache to NVS
    esp_err_t saveCacheToNVS();

    // Load cache from NVS
    esp_err_t loadCacheFromNVS();

    // Estimate serialized size of a cache item
    size_t estimateItemSize(const ProductCacheItem &item) const;

    // Evict least recently used items if cache exceeds limit
    void evictIfNecessary();

    // Remove expired entries
    void removeExpiredEntries();

    // Update access timestamp for an entry
    void updateAccessTime(const std::string &barcode);
};
