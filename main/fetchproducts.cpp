#include <string>
#include <algorithm>
#include <vector>
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "fetchproducts.h"

static const char *TAG = "PRODUCT";

static std::string toLower(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string mapUkSupermarketCategory(cJSON *tagsArray)
{
    if (!cJSON_IsArray(tagsArray))
        return "Other";

    cJSON *tagItem = nullptr;
    cJSON_ArrayForEach(tagItem, tagsArray)
    {
        if (!cJSON_IsString(tagItem))
            continue;

        std::string t = toLower(tagItem->valuestring);

        if (t.find("vegetable") != std::string::npos || t.find("veg") != std::string::npos || t.find("fruit") != std::string::npos)
            return "Fruit & Veg";

        if (t.find("meat") != std::string::npos || t.find("poultry") != std::string::npos || t.find("beef") != std::string::npos ||
            t.find("chicken") != std::string::npos || t.find("fish") != std::string::npos || t.find("seafood") != std::string::npos)
            return "Meat & Fish";

        if (t.find("dairy") != std::string::npos || t.find("milk") != std::string::npos || t.find("cheese") != std::string::npos ||
            t.find("yogurt") != std::string::npos || t.find("egg") != std::string::npos)
            return "Dairy & Eggs";

        if (t.find("bread") != std::string::npos || t.find("bakery") != std::string::npos || t.find("pastry") != std::string::npos)
            return "Bakery";

        if (t.find("frozen") != std::string::npos)
            return "Frozen";

        if (t.find("beverage") != std::string::npos || t.find("drink") != std::string::npos || t.find("juice") != std::string::npos || t.find("water") != std::string::npos)
            return "Drinks";

        if (t.find("snack") != std::string::npos || t.find("crisps") != std::string::npos || t.find("chocolate") != std::string::npos || t.find("sweets") != std::string::npos)
            return "Snacks";

        if (t.find("cereal") != std::string::npos || t.find("breakfast") != std::string::npos || t.find("oats") != std::string::npos)
            return "Cereal & Breakfast";

        if (t.find("canned") != std::string::npos || t.find("tinned") != std::string::npos || t.find("jarred") != std::string::npos)
            return "Tins & Jars";

        if (t.find("pasta") != std::string::npos || t.find("rice") != std::string::npos || t.find("grain") != std::string::npos || t.find("noodle") != std::string::npos)
            return "Pasta, Rice & Grains";

        if (t.find("sauce") != std::string::npos || t.find("condiment") != std::string::npos || t.find("spread") != std::string::npos)
            return "Condiments & Sauces";

        if (t.find("household") != std::string::npos || t.find("cleaning") != std::string::npos)
            return "Household";
    }

    return "Other";
}

bool fetchProductInfo(const std::string &barcode, ProductCacheItem &out, ProductCache *cache)
{
    // Check cache first
    if (cache && cache->contains(barcode))
    {
        return cache->get(barcode, out);
    }

    std::string url = "https://world.openfoodfacts.org/api/v0/product/" + barcode + ".json";
    ESP_LOGI(TAG, "Fetching product info: %s", url.c_str());

    // Initialize to zero and assign fields individually to avoid C++ ordering errors
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.skip_cert_common_name_check = true;
    config.buffer_size = 2048; // Increased slightly for JSON safety

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        esp_http_client_cleanup(client);
        return false;
    }

    std::string payload;
    char buffer[1024];
    int bytes_read;
    int total_read = 0;

    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        total_read += bytes_read;
        if (total_read > 131072) // 128KB Safety Cap
        {
            ESP_LOGE(TAG, "Response too large");
            esp_http_client_cleanup(client);
            return false;
        }
        payload.append(buffer, bytes_read);
    }

    esp_http_client_cleanup(client);

    if (payload.empty())
        return false;

    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root)
        return false;

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || (status->valueint != 1 && status->valueint != 200))
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON *product = cJSON_GetObjectItem(root, "product");
    if (cJSON_IsObject(product))
    {
        cJSON *nameItem = cJSON_GetObjectItem(product, "product_name");
        out.name = (nameItem && cJSON_IsString(nameItem)) ? nameItem->valuestring : "Unknown";

        cJSON *tags = cJSON_GetObjectItem(product, "categories_tags");
        out.category = mapUkSupermarketCategory(tags);
        out.barcode = barcode;

        // Add to cache if cache is provided
        if (cache)
        {
            cache->add(out);
        }
    }

    cJSON_Delete(root);
    return true;
}