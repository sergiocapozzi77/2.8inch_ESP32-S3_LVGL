#include "product_cache.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>
#include <ctime>
#include <algorithm>

static const char *TAG = "PRODUCT_CACHE";

ProductCache::ProductCache() : initialized(false) {}

ProductCache::~ProductCache()
{
    // No cleanup needed, NVS is managed by ESP-IDF
}

esp_err_t ProductCache::init()
{
    if (initialized)
        return ESP_OK;

    esp_err_t err = loadCacheFromNVS();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load cache from NVS: %s", esp_err_to_name(err));
        // Continue even if loading fails - cache will be empty
        err = ESP_OK;
    }

    initialized = true;
    ESP_LOGI(TAG, "Product cache initialized with %d items (%d bytes)",
             cache_map.size(), getMemoryUsage());

    return ESP_OK;
}

bool ProductCache::contains(const std::string &barcode) const
{
    return cache_map.find(barcode) != cache_map.end();
}

bool ProductCache::get(const std::string &barcode, ProductCacheItem &out)
{
    auto it = cache_map.find(barcode);
    if (it != cache_map.end())
    {
        out = it->second.item;
        updateAccessTime(barcode);
        ESP_LOGI(TAG, "Cache hit for barcode: %s", barcode.c_str());
        return true;
    }
    return false;
}

esp_err_t ProductCache::add(const ProductCacheItem &item)
{
    if (item.barcode.empty())
    {
        ESP_LOGW(TAG, "Cannot cache item with empty barcode");
        return ESP_ERR_INVALID_ARG;
    }

    // Estimate size of this item
    size_t item_size = estimateItemSize(item);

    // Check if adding this item would exceed limits
    size_t current_usage = getMemoryUsage();
    ESP_LOGI(TAG, "Current cache usage: %d bytes, adding item of size %d bytes",
             current_usage, item_size);
    if (current_usage + item_size > MAX_CACHE_SIZE_BYTES)
    {
        ESP_LOGW(TAG, "Cache would exceed limit (%d + %d > %d bytes), evicting entries",
                 current_usage, item_size, MAX_CACHE_SIZE_BYTES);

        // Remove expired entries first
        removeExpiredEntries();
        current_usage = getMemoryUsage();

        // Then evict LRU entries until there's space
        while (current_usage + item_size > MAX_CACHE_SIZE_BYTES && !cache_map.empty())
        {
            evictIfNecessary();
            current_usage = getMemoryUsage();
        }

        if (current_usage + item_size > MAX_CACHE_SIZE_BYTES)
        {
            ESP_LOGE(TAG, "Failed to make space for new item, skipping add");
            return ESP_ERR_INVALID_SIZE;
        }
    }

    // Add the item
    CacheEntry entry;
    entry.item = item;
    entry.metadata.last_access = time(nullptr);
    entry.metadata.serialized_size = item_size;

    cache_map[item.barcode] = entry;
    ESP_LOGI(TAG, "Added to cache: %s (%d bytes, total: %d bytes)",
             item.barcode.c_str(), item_size, getMemoryUsage());

    // Persist to NVS
    esp_err_t err = saveCacheToNVS();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to persist cache to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

void ProductCache::clear()
{
    cache_map.clear();

    // Clear NVS namespace
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Cache cleared");
    }
}

size_t ProductCache::size() const
{
    return cache_map.size();
}

size_t ProductCache::getMemoryUsage() const
{
    size_t total = 0;
    for (const auto &pair : cache_map)
    {
        total += pair.second.metadata.serialized_size;
    }
    return total;
}

size_t ProductCache::estimateItemSize(const ProductCacheItem &item) const
{
    // Estimate JSON serialized size of an item
    // Format: {"barcode":"...", "name":"...", "category":"..."}
    // Add overhead for JSON structure (~50 bytes) + string lengths
    size_t size = 50;                    // JSON overhead
    size += item.barcode.length() + 12;  // "barcode" key + quotes
    size += item.name.length() + 8;      // "name" key + quotes
    size += item.category.length() + 12; // "category" key + quotes
    return size;
}

void ProductCache::updateAccessTime(const std::string &barcode)
{
    auto it = cache_map.find(barcode);
    if (it != cache_map.end())
    {
        it->second.metadata.last_access = time(nullptr);
    }
}

void ProductCache::removeExpiredEntries()
{
    time_t now = time(nullptr);
    auto it = cache_map.begin();
    int removed = 0;

    while (it != cache_map.end())
    {
        if ((now - it->second.metadata.last_access) > MAX_ENTRY_AGE_SECONDS)
        {
            ESP_LOGI(TAG, "Removing expired cache entry: %s", it->first.c_str());
            it = cache_map.erase(it);
            removed++;
        }
        else
        {
            ++it;
        }
    }

    if (removed > 0)
    {
        ESP_LOGI(TAG, "Removed %d expired entries, cache now %d bytes",
                 removed, getMemoryUsage());
    }
}

void ProductCache::evictIfNecessary()
{
    if (cache_map.empty())
        return;

    // Find least recently used entry
    auto lru_it = cache_map.begin();
    for (auto it = cache_map.begin(); it != cache_map.end(); ++it)
    {
        if (it->second.metadata.last_access < lru_it->second.metadata.last_access)
        {
            lru_it = it;
        }
    }

    ESP_LOGI(TAG, "Evicting LRU entry: %s (%d bytes)",
             lru_it->first.c_str(), lru_it->second.metadata.serialized_size);

    cache_map.erase(lru_it);
}

esp_err_t ProductCache::saveCacheToNVS()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Create JSON array of cache items
    cJSON *json_array = cJSON_CreateArray();
    if (!json_array)
    {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    for (const auto &pair : cache_map)
    {
        cJSON *json_item = cJSON_CreateObject();
        if (!json_item)
        {
            cJSON_Delete(json_array);
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(json_item, "barcode", pair.second.item.barcode.c_str());
        cJSON_AddStringToObject(json_item, "name", pair.second.item.name.c_str());
        cJSON_AddStringToObject(json_item, "category", pair.second.item.category.c_str());
        cJSON_AddNumberToObject(json_item, "last_access", (double)pair.second.metadata.last_access);

        cJSON_AddItemToArray(json_array, json_item);
    }

    char *json_str = cJSON_Print(json_array);
    if (!json_str)
    {
        cJSON_Delete(json_array);
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_str);

    // Ensure we don't exceed NVS blob limits
    // NVS blob max is 4000 bytes, but we check against our cache limit
    if (json_len > MAX_CACHE_SIZE_BYTES)
    {
        ESP_LOGE(TAG, "Cache too large (%d bytes > %d bytes limit), this shouldn't happen!",
                 json_len, MAX_CACHE_SIZE_BYTES);
        free(json_str);
        cJSON_Delete(json_array);
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    // Check NVS blob size limit (typically 4000 bytes)
    if (json_len > 4000)
    {
        ESP_LOGW(TAG, "Serialized cache exceeds NVS blob limit (%d > 4000 bytes)", json_len);
        // This should not happen if MAX_CACHE_SIZE_BYTES is set correctly
        // But we'll try to write it anyway
    }

    err = nvs_set_blob(handle, "cache_data", json_str, json_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write to NVS: %s", esp_err_to_name(err));
    }
    else
    {
        err = nvs_commit(handle);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Cache persisted to NVS (%d bytes, %d items, %.1f%% of limit)",
                     json_len, cache_map.size(),
                     (float)json_len / MAX_CACHE_SIZE_BYTES * 100.0f);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        }
    }

    free(json_str);
    cJSON_Delete(json_array);
    nvs_close(handle);

    return err;
}

esp_err_t ProductCache::loadCacheFromNVS()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGI(TAG, "No cached data found in NVS");
            return ESP_OK; // This is not an error - first time initialization
        }
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Get the size of the blob
    size_t blob_size = 0;
    err = nvs_get_blob(handle, "cache_data", NULL, &blob_size);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGI(TAG, "No cache data in NVS");
            nvs_close(handle);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to get blob size: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // Allocate buffer and read
    char *json_str = (char *)malloc(blob_size + 1);
    if (!json_str)
    {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, "cache_data", json_str, &blob_size);
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read blob: %s", esp_err_to_name(err));
        free(json_str);
        return err;
    }

    json_str[blob_size] = '\0';

    // Parse JSON
    cJSON *json_array = cJSON_Parse(json_str);
    free(json_str);

    if (!json_array)
    {
        ESP_LOGE(TAG, "Failed to parse cache JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!cJSON_IsArray(json_array))
    {
        cJSON_Delete(json_array);
        ESP_LOGE(TAG, "Invalid cache format (not an array)");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Load items into cache
    cache_map.clear();
    cJSON *item = nullptr;
    int count = 0;
    cJSON_ArrayForEach(item, json_array)
    {
        if (!cJSON_IsObject(item))
            continue;

        CacheEntry entry;
        entry.item = ProductCacheItem();

        cJSON *barcode_obj = cJSON_GetObjectItem(item, "barcode");
        if (cJSON_IsString(barcode_obj))
            entry.item.barcode = barcode_obj->valuestring;

        cJSON *name_obj = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(name_obj))
            entry.item.name = name_obj->valuestring;

        cJSON *category_obj = cJSON_GetObjectItem(item, "category");
        if (cJSON_IsString(category_obj))
            entry.item.category = category_obj->valuestring;

        cJSON *last_access_obj = cJSON_GetObjectItem(item, "last_access");
        if (cJSON_IsNumber(last_access_obj))
            entry.metadata.last_access = (time_t)last_access_obj->valuedouble;
        else
            entry.metadata.last_access = time(nullptr);

        if (!entry.item.barcode.empty())
        {
            entry.metadata.serialized_size = estimateItemSize(entry.item);
            cache_map[entry.item.barcode] = entry;
            count++;
        }
    }

    cJSON_Delete(json_array);
    ESP_LOGI(TAG, "Loaded %d items from NVS cache (%d bytes)", count, getMemoryUsage());
    return ESP_OK;
}
