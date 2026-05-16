#include "ProductFetcher.h"
#include "BarcodeReader.h"
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include "vars.h"
#include "LVGLManager.h"
#include "esp_crt_bundle.h"
#include "ExpiryDateSelector.h"
#include "WiFiManager.h"

static const char *TAG = "ProductFetcher";

ProductFetcher::ProductFetcher(
    QueueHandle_t barcode_q,
    QueueHandle_t product_q,
    ProductCache *cache)
    : barcode_queue(barcode_q),
      product_queue(product_q),
      product_cache(cache)
{
    persist_queue = xQueueCreate(8, sizeof(ProductPersistItem));
}

esp_err_t ProductFetcher::start()
{
    xTaskCreate(ProductFetcher::task, "product_fetch", 8192, this, 5, nullptr);
    xTaskCreate(ProductFetcher::persistTask, "product_persist", 12192, this, 4, nullptr);
    return ESP_OK;
}

static std::string sanitizeBarcode(const char *raw)
{
    std::string out;
    for (const char *p = raw; *p; ++p)
    {
        // barcodes are digits only (EAN-13, UPC-A etc.)
        if (*p >= '0' && *p <= '9')
            out += *p;
    }
    return out;
}

void ProductFetcher::task(void *arg)
{
    auto *self = static_cast<ProductFetcher *>(arg);
    char barcode[MAX_BARCODE_LEN];

    while (true)
    {
        if (xQueueReceive(self->barcode_queue, barcode, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Fetching product info for: %s", barcode);
            std::string cleanBarcode = sanitizeBarcode(barcode);

            if (cleanBarcode.empty())
            {
                ESP_LOGW(TAG, "Barcode empty after sanitization, discarding");
                continue;
            }

            ProductCacheItem item;
            int res = self->fetchProductInfoWithRetry(cleanBarcode, item, self->product_cache);
            if (res > 0)
            {
                ESP_LOGI(TAG, "Product: %s (%s)",
                         item.name.c_str(),
                         item.category.c_str());
                LVGLManager::updateStatusLabel("Product found: " + item.name);

                xQueueSend(self->product_queue, item.name.c_str(), 0);

                ProductPersistItem persist_item{};
                strncpy(persist_item.name, item.name.c_str(), MAX_PRODUCT_NAME_LEN - 1);
                strncpy(persist_item.category, item.category.c_str(), MAX_CATEGORY_LEN - 1);
                strncpy(persist_item.barcode, item.barcode.c_str(), MAX_BARCODE_LEN - 1);
                persist_item.quantity = 1;

                if (xQueueSend(self->persist_queue, &persist_item, pdMS_TO_TICKS(100)) != pdTRUE)
                {
                    ESP_LOGW(TAG, "Persist queue full");
                }
            }
            else if (res == 0)
            {
                LVGLManager::updateStatusLabel("Product not found");
                LVGLManager::showErrorSnackbar("Product not found :(");
                ESP_LOGI(TAG, "Product not found in database");
            }
            else
            {
                LVGLManager::showErrorSnackbar("Failed to fetch product :(");
                ESP_LOGW(TAG, "Failed to fetch product info");
            }
        }
    }
}

void ProductFetcher::persistTask(void *arg)
{
    auto *self = static_cast<ProductFetcher *>(arg);
    ProductPersistItem item;

    while (true)
    {
        if (xQueueReceive(self->persist_queue, &item, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Persisting product: %s %s", item.name, item.category);

            Product product;
            product.name = item.name;
            product.category = item.category;
            product.quantity = item.quantity;
            product.barcode = item.barcode;

            // Check if in remove mode
            const auto mode = get_var_add_or_del();
            if (mode == AddOrDelType_Del)
            {
                // Remove mode: skip expiry selector, directly remove product
                ESP_LOGI(TAG, "Remove mode: deleting product %s directly", product.name.c_str());
                if (productService.manageUpdateProduct(product))
                {
                    ESP_LOGI(TAG, "Product removed: %s", product.name.c_str());
                }
                else
                {
                    LVGLManager::showErrorSnackbar("Failed to remove product: " + product.name);
                    ESP_LOGW(TAG, "Failed to remove product: %s", product.name.c_str());
                }
            }
            else
            {
                // Add mode: show expiry selector
                // if (product.category == "Meat & Fish")
                // {
                expiryDateSelector.show(product, 0);
                // }
            }
        }
    }
}

std::string ProductFetcher::toLower(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}
std::string ProductFetcher::mapUkSupermarketCategory(cJSON *tagsArray, const std::string &productName)
{
#ifdef DEBUG_MEAT
    ESP_LOGI(TAG, "DEBUG_MEAT is defined - categorizing as Meat");
    return "Meat";
#endif

    if (!cJSON_IsArray(tagsArray))
        return "Other";

    std::string productNameLow = productName;
    std::transform(productNameLow.begin(), productNameLow.end(), productNameLow.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });

    // Category scores map
    std::map<std::string, int> categoryScores;
    std::vector<std::string> categoryOrder = {
        "Baby", "Pet Supplies", "Wine, Beer & Spirit", "Produce", "Meat", "Seafood",
        "Deli", "Dairy", "Bakery", "Frozen Foods", "Beverages", "Snacks",
        "Breakfast & Cereal", "Soups & Canned Food", "Grains, Pasta & Sides",
        "Cooking & Baking", "Condiments & Dressing", "Health & Personal Care",
        "Household & Cleaning"};

    cJSON *tagItem = nullptr;
    cJSON_ArrayForEach(tagItem, tagsArray)
    {
        if (!cJSON_IsString(tagItem))
            continue;

        std::string t = toLower(tagItem->valuestring);

        // Baby
        if (t.find("baby") != std::string::npos || t.find("infant") != std::string::npos ||
            t.find("formula") != std::string::npos || t.find("baby-foods") != std::string::npos ||
            productNameLow.find("baby") != std::string::npos)
            categoryScores["Baby"]++;

        // Pet Supplies
        if (t.find("pet") != std::string::npos || t.find("dog") != std::string::npos ||
            t.find("cat") != std::string::npos || t.find("pet-foods") != std::string::npos ||
            productNameLow.find("dog food") != std::string::npos || productNameLow.find("cat food") != std::string::npos)
            categoryScores["Pet Supplies"]++;

        // Wine, Beer & Spirit
        if (t.find("alcoholic") != std::string::npos || t.find("wine") != std::string::npos ||
            t.find("wines") != std::string::npos || t.find("beer") != std::string::npos ||
            t.find("beers") != std::string::npos || t.find("spirit") != std::string::npos ||
            t.find("spirits") != std::string::npos || t.find("liquor") != std::string::npos ||
            t.find("alcohol") != std::string::npos || t.find("cider") != std::string::npos)
            categoryScores["Wine, Beer & Spirit"]++;

        // Produce (Fruit & Veg)
        if (t.find("vegetable") != std::string::npos || t.find("veg") != std::string::npos ||
            t.find("vegetables") != std::string::npos || t.find("fruit") != std::string::npos ||
            t.find("fruits") != std::string::npos || t.find("produce") != std::string::npos ||
            t.find("fresh-vegetables") != std::string::npos || t.find("fresh-fruits") != std::string::npos)
            categoryScores["Produce"]++;

        // Meat
        if (t.find("meat") != std::string::npos || t.find("meats") != std::string::npos ||
            t.find("poultry") != std::string::npos || t.find("beef") != std::string::npos ||
            t.find("chicken") != std::string::npos || t.find("pork") != std::string::npos ||
            t.find("lamb") != std::string::npos || t.find("sausage") != std::string::npos ||
            t.find("bacon") != std::string::npos || t.find("ham") != std::string::npos ||
            productNameLow.find("meat") != std::string::npos || productNameLow.find("chicken") != std::string::npos ||
            productNameLow.find("pork") != std::string::npos || productNameLow.find("lamb") != std::string::npos ||
            productNameLow.find("sausage") != std::string::npos || productNameLow.find("beef") != std::string::npos)
            categoryScores["Meat"]++;

        // Seafood
        if (t.find("fish") != std::string::npos || t.find("fishes") != std::string::npos ||
            t.find("seafood") != std::string::npos || t.find("seafoods") != std::string::npos ||
            t.find("salmon") != std::string::npos || t.find("tuna") != std::string::npos ||
            t.find("shellfish") != std::string::npos || t.find("shrimp") != std::string::npos ||
            productNameLow.find("fish") != std::string::npos || productNameLow.find("salmon") != std::string::npos ||
            productNameLow.find("seafood") != std::string::npos)
            categoryScores["Seafood"]++;

        // Deli
        if (t.find("deli") != std::string::npos || t.find("prepared") != std::string::npos ||
            t.find("ready-meal") != std::string::npos || t.find("charcuterie") != std::string::npos ||
            t.find("sliced-meats") != std::string::npos || t.find("sandwiches") != std::string::npos)
            categoryScores["Deli"]++;

        // Dairy (without eggs)
        if (t.find("dairy") != std::string::npos || t.find("milk") != std::string::npos ||
            t.find("cheese") != std::string::npos || t.find("cheeses") != std::string::npos ||
            t.find("yogurt") != std::string::npos || t.find("yogurts") != std::string::npos ||
            t.find("yoghurt") != std::string::npos || t.find("butter") != std::string::npos ||
            t.find("cream") != std::string::npos)
            categoryScores["Dairy"]++;

        // Bakery
        if (t.find("bread") != std::string::npos || t.find("breads") != std::string::npos ||
            t.find("bakery") != std::string::npos || t.find("pastry") != std::string::npos ||
            t.find("pastries") != std::string::npos || t.find("baked-goods") != std::string::npos ||
            t.find("cake") != std::string::npos || t.find("cakes") != std::string::npos)
            categoryScores["Bakery"]++;

        // Frozen Foods
        if (t.find("frozen") != std::string::npos || t.find("frozen-foods") != std::string::npos)
            categoryScores["Frozen Foods"]++;

        // Beverages (non-alcoholic)
        if (t.find("beverage") != std::string::npos || t.find("beverages") != std::string::npos ||
            t.find("drink") != std::string::npos || t.find("drinks") != std::string::npos ||
            t.find("juice") != std::string::npos || t.find("juices") != std::string::npos ||
            t.find("water") != std::string::npos || t.find("soda") != std::string::npos ||
            t.find("soft-drink") != std::string::npos || t.find("tea") != std::string::npos ||
            t.find("coffee") != std::string::npos)
            categoryScores["Beverages"]++;

        // Snacks
        if (t.find("snack") != std::string::npos || t.find("snacks") != std::string::npos ||
            t.find("crisps") != std::string::npos || t.find("chips") != std::string::npos ||
            t.find("chocolate") != std::string::npos || t.find("chocolates") != std::string::npos ||
            t.find("sweets") != std::string::npos || t.find("candy") != std::string::npos ||
            t.find("biscuit") != std::string::npos || t.find("cookies") != std::string::npos)
            categoryScores["Snacks"]++;

        // Breakfast & Cereal
        if (t.find("cereal") != std::string::npos || t.find("cereals") != std::string::npos ||
            t.find("breakfast") != std::string::npos || t.find("oats") != std::string::npos ||
            t.find("porridge") != std::string::npos || t.find("muesli") != std::string::npos ||
            t.find("granola") != std::string::npos || t.find("egg") != std::string::npos ||
            t.find("eggs") != std::string::npos)
            categoryScores["Breakfast & Cereal"]++;

        // Soups & Canned Food
        if (t.find("canned") != std::string::npos || t.find("tinned") != std::string::npos ||
            t.find("jarred") != std::string::npos || t.find("soup") != std::string::npos ||
            t.find("soups") != std::string::npos || t.find("canned-foods") != std::string::npos)
            categoryScores["Soups & Canned Food"]++;

        // Grains, Pasta & Sides
        if (t.find("pasta") != std::string::npos || t.find("pastas") != std::string::npos ||
            t.find("rice") != std::string::npos || t.find("grains") != std::string::npos ||
            t.find("grain") != std::string::npos || t.find("noodle") != std::string::npos ||
            t.find("noodles") != std::string::npos || t.find("couscous") != std::string::npos ||
            t.find("quinoa") != std::string::npos)
            categoryScores["Grains, Pasta & Sides"]++;

        // Cooking & Baking
        if (t.find("flour") != std::string::npos || t.find("sugar") != std::string::npos ||
            t.find("baking") != std::string::npos || t.find("oil") != std::string::npos ||
            t.find("oils") != std::string::npos || t.find("spice") != std::string::npos ||
            t.find("spices") != std::string::npos || t.find("seasoning") != std::string::npos ||
            t.find("herbs") != std::string::npos || t.find("vanilla") != std::string::npos ||
            t.find("yeast") != std::string::npos || t.find("baking-powder") != std::string::npos)
            categoryScores["Cooking & Baking"]++;

        // Condiments & Dressing
        if (t.find("sauce") != std::string::npos || t.find("sauces") != std::string::npos ||
            t.find("condiment") != std::string::npos || t.find("condiments") != std::string::npos ||
            t.find("spread") != std::string::npos || t.find("spreads") != std::string::npos ||
            t.find("dressing") != std::string::npos || t.find("dressings") != std::string::npos ||
            t.find("ketchup") != std::string::npos || t.find("mustard") != std::string::npos ||
            t.find("mayonnaise") != std::string::npos || t.find("vinegar") != std::string::npos)
            categoryScores["Condiments & Dressing"]++;

        // Health & Personal Care
        if (t.find("health") != std::string::npos || t.find("personal-care") != std::string::npos ||
            t.find("vitamin") != std::string::npos || t.find("vitamins") != std::string::npos ||
            t.find("supplement") != std::string::npos || t.find("toiletries") != std::string::npos ||
            t.find("hygiene") != std::string::npos || t.find("cosmetic") != std::string::npos)
            categoryScores["Health & Personal Care"]++;

        // Household & Cleaning
        if (t.find("household") != std::string::npos || t.find("cleaning") != std::string::npos ||
            t.find("detergent") != std::string::npos || t.find("cleaner") != std::string::npos ||
            t.find("laundry") != std::string::npos || t.find("dishwashing") != std::string::npos)
            categoryScores["Household & Cleaning"]++;
    }

    // Find the category with the highest score
    std::string bestCategory = "Other";
    int maxScore = 0;

    for (const auto &category : categoryOrder)
    {
        auto it = categoryScores.find(category);
        if (it != categoryScores.end() && it->second > maxScore)
        {
            maxScore = it->second;
            bestCategory = category;
        }
    }

    return bestCategory;
}

int ProductFetcher::fetchProductInfoWithRetry(const std::string &barcode, ProductCacheItem &out, ProductCache *cache)
{
    const int max_retries = 5;

    for (int attempt = 0; attempt <= max_retries; ++attempt)
    {
        int result = fetchProductInfo(barcode, out, cache, attempt);
        if (result >= 0)
        {
            LVGLManager::updateStatusLabel("Returning result: " + std::to_string(result));
            return result;
        }

        if (attempt < max_retries)
        {
            ESP_LOGW(TAG, "Retrying fetch (%d/%d) for: %s", attempt + 1, max_retries, barcode.c_str());
            LVGLManager::updateStatusLabel("Retrying fetch...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    LVGLManager::updateStatusLabel("Max retries reached. Failed to fetch product.");

    return -1;
}

int ProductFetcher::fetchProductInfo(const std::string &barcode, ProductCacheItem &out, ProductCache *cache, int retry_count)
{
#ifndef DEBUG_MEAT
    // Check cache first
    if (cache && cache->contains(barcode))
    {
        LVGLManager::updateStatusLabel("Loading from cache...");
        return cache->get(barcode, out) ? 1 : -1;
    }
#endif

    while (!wifi_manager.isConnected())
    {
        ESP_LOGW(TAG, "Waiting for WiFi...");
        LVGLManager::updateStatusLabel("Waiting for WiFi...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    LVGLManager::updateStatusLabel("Fetching product info x" + std::to_string(retry_count + 1) + "...");
    vTaskDelay(pdMS_TO_TICKS(50));
    std::string url = "https://world.openfoodfacts.org/api/v0/product/" + barcode + ".json?fields=product_name,categories_tags";

    ESP_LOGI(TAG, "Fetching product info: %s", url.c_str());

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.buffer_size = 512;

    ESP_LOGI(TAG, "Sending client request to OpenFoodFacts");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        LVGLManager::updateStatusLabel("Failed to init HTTP client: " + std::to_string(esp_get_free_heap_size()));
        return -1;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        LVGLManager::updateStatusLabel("Failed to connect to server");
        esp_http_client_cleanup(client);
        return -1;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        LVGLManager::updateStatusLabel("Server returned error: " + std::to_string(status_code));
        esp_http_client_cleanup(client);
        return -1;
    }

    std::string payload;
    payload.reserve(1024);

    char buffer[512];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        payload.append(buffer, bytes_read);
        if (payload.size() > 8192)
        {
            ESP_LOGE(TAG, "Response unexpectedly large");
            LVGLManager::updateStatusLabel("Response too large");
            esp_http_client_cleanup(client);
            return -1;
        }
    }

    esp_http_client_cleanup(client);

    if (payload.empty())
    {
        LVGLManager::updateStatusLabel("Empty payload");
        return -1;
    }

    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root)
    {
        LVGLManager::updateStatusLabel("Failed to parse JSON");
        return -1;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || (status->valueint != 1 && status->valueint != 200))
    {
        cJSON_Delete(root);
        LVGLManager::updateStatusLabel("Product not found");

        out.name = barcode;
        out.category = "Other";
        out.barcode = barcode;

        if (cache)
        {
            cache->add(out);
        }

        cJSON_Delete(root);
        return 1;
    }

    cJSON *product = cJSON_GetObjectItem(root, "product");
    if (cJSON_IsObject(product))
    {
        LVGLManager::updateStatusLabel("Parsing product info...");
        cJSON *nameItem = cJSON_GetObjectItem(product, "product_name");
        out.name = (nameItem && cJSON_IsString(nameItem)) ? nameItem->valuestring : "Unknown";

        cJSON *tags = cJSON_GetObjectItem(product, "categories_tags");
        out.category = mapUkSupermarketCategory(tags, out.name);
        out.barcode = barcode;

        if (cache)
        {
            cache->add(out);
        }
    }
    else
    {
        LVGLManager::updateStatusLabel("Invalid product data");
    }

    cJSON_Delete(root);
    return 1;
}
