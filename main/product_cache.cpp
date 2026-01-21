#include "product_cache.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>

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
    ESP_LOGI(TAG, "Product cache initialized with %d items", cache_map.size());
    return ESP_OK;
}

bool ProductCache::contains(const std::string &barcode) const
{
    return cache_map.find(barcode) != cache_map.end();
}

bool ProductCache::get(const std::string &barcode, ProductCacheItem &out) const
{
    auto it = cache_map.find(barcode);
    if (it != cache_map.end())
    {
        out = it->second;
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

    cache_map[item.barcode] = item;
    ESP_LOGI(TAG, "Added to cache: %s", item.barcode.c_str());

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

        cJSON_AddStringToObject(json_item, "barcode", pair.second.barcode.c_str());
        cJSON_AddStringToObject(json_item, "name", pair.second.name.c_str());
        cJSON_AddStringToObject(json_item, "category", pair.second.category.c_str());

        cJSON_AddItemToArray(json_array, json_item);
    }

    char *json_str = cJSON_Print(json_array);
    if (!json_str)
    {
        cJSON_Delete(json_array);
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    // Store in NVS with key "cache_data"
    size_t json_len = strlen(json_str);
    if (json_len > 4000) // NVS blob size limit is typically 4000 bytes
    {
        ESP_LOGW(TAG, "Cache too large (%d bytes), skipping NVS save", json_len);
        free(json_str);
        cJSON_Delete(json_array);
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
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
            ESP_LOGI(TAG, "Cache persisted to NVS (%d bytes, %d items)", json_len, cache_map.size());
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

        ProductCacheItem cache_item;

        cJSON *barcode_obj = cJSON_GetObjectItem(item, "barcode");
        if (cJSON_IsString(barcode_obj))
            cache_item.barcode = barcode_obj->valuestring;

        cJSON *name_obj = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(name_obj))
            cache_item.name = name_obj->valuestring;

        cJSON *category_obj = cJSON_GetObjectItem(item, "category");
        if (cJSON_IsString(category_obj))
            cache_item.category = category_obj->valuestring;

        if (!cache_item.barcode.empty())
        {
            cache_map[cache_item.barcode] = cache_item;
            count++;
        }
    }

    cJSON_Delete(json_array);
    ESP_LOGI(TAG, "Loaded %d items from NVS cache", count);
    return ESP_OK;
}
