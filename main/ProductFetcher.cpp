#include "ProductFetcher.h"
#include "BarcodeReader.h"
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include "vars.h"
#include "LVGLManager.h"
#include "esp_crt_bundle.h"

static const char *TAG = "ProductFetcher";

// Helper struct to hold state during expiry selection
struct ExpirySelectionState
{
    Product product;
    int dayOffset;
    ProductService *service;
};

ProductFetcher::ProductFetcher(
    QueueHandle_t barcode_q,
    QueueHandle_t product_q,
    ProductCache *cache,
    ProductService *service)
    : barcode_queue(barcode_q),
      product_queue(product_q),
      product_cache(cache),
      product_service(service)
{
    persist_queue = xQueueCreate(8, sizeof(ProductPersistItem));
}

esp_err_t ProductFetcher::start()
{
    xTaskCreate(ProductFetcher::task, "product_fetch", 8192, this, 5, nullptr);
    xTaskCreate(ProductFetcher::persistTask, "product_persist", 8192, this, 4, nullptr);
    return ESP_OK;
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

            ProductCacheItem item;
            if (self->fetchProductInfo(std::string(barcode), item, self->product_cache))
            {
                ESP_LOGI(TAG, "Product: %s (%s)",
                         item.name.c_str(),
                         item.category.c_str());
                LVGLManager::updateStatusLabel("Product found: " + item.name);

                xQueueSend(self->product_queue, item.name.c_str(), 0);

                ProductPersistItem persist_item{};
                strncpy(persist_item.name, item.name.c_str(), MAX_PRODUCT_NAME_LEN - 1);
                strncpy(persist_item.category, item.category.c_str(), MAX_CATEGORY_LEN - 1);
                persist_item.quantity = 1;

                if (xQueueSend(self->persist_queue, &persist_item, pdMS_TO_TICKS(100)) != pdTRUE)
                {
                    ESP_LOGW(TAG, "Persist queue full");
                }
            }
            else
            {
                LVGLManager::showErrorSnackbar("Failed to fetch product :(");
                ESP_LOGW(TAG, "Failed to fetch product info");
            }
        }
    }
}

static void showExpiryDateSelection(ExpirySelectionState *state);

// Forward declaration
static void handleExpirySelection(int selectedIndex, void *user_data)
{
    auto *state = static_cast<ExpirySelectionState *>(user_data);
    ESP_LOGI(TAG, "User selected index: %d", selectedIndex);

    if (selectedIndex == 5) // "<<"
    {
        if (state->dayOffset > 0)
            state->dayOffset -= 5;
        showExpiryDateSelection(state);
    }
    else if (selectedIndex == 6) // ">>"
    {
        state->dayOffset += 5;
        showExpiryDateSelection(state);
    }
    else if (selectedIndex == 7) // "X" skip
    {
        xTaskCreate(ProductFetcher::saveProductTask, "save_product", 8192, state, 4, nullptr);
    }
    else if (selectedIndex >= 0 && selectedIndex < 6 && selectedIndex != 4)
    {
        time_t now = time(nullptr);
        tm date_tm{};
        localtime_r(&now, &date_tm);

        int dayIndex = (selectedIndex < 3) ? selectedIndex : (selectedIndex - 1);
        date_tm.tm_mday += state->dayOffset + dayIndex + 1;
        mktime(&date_tm);

        char expiryDate[16];
        strftime(expiryDate, sizeof(expiryDate), "%Y-%m-%d", &date_tm);
        state->product.expiry = expiryDate;
        ESP_LOGI(TAG, "Selected expiry date: %s", state->product.expiry.c_str());

        xTaskCreate(ProductFetcher::saveProductTask, "save_product", 8192, state, 4, nullptr);
    }
    else
    {
        LVGLManager::showErrorSnackbar("Invalid selection");
        showExpiryDateSelection(state);
    }
}

static void showExpiryDateSelection(ExpirySelectionState *state)
{
    // 9 labels + 1 terminator
    char **labels = new char *[10];

    time_t now = time(nullptr);

    for (int i = 0; i < 9; ++i)
    {
        if (i == 4)
        {
            // Row break
            labels[i] = strdup("\n");
        }
        else if (i == 6)
        {
            labels[i] = strdup("<<");
        }
        else if (i == 7)
        {
            labels[i] = strdup(">>");
        }
        else if (i == 8)
        {
            labels[i] = strdup("X");
        }
        else
        {
            // Date cells: indices 0,1,2,3,5 → 5 dates
            tm date_tm{};
            localtime_r(&now, &date_tm);

            int dayIndex = (i < 4) ? i : (i - 1); // skip the "\n" slot at 4
            date_tm.tm_mday += state->dayOffset + dayIndex + 1;
            mktime(&date_tm);

            char buf[16];
            strftime(buf, sizeof(buf), "%d/%m", &date_tm);
            labels[i] = strdup(buf);
        }

        if (!labels[i])
            labels[i] = strdup("?");
    }

    labels[9] = nullptr; // terminator

    LVGLManager::updateExpiryMatrixButton(labels);
    LVGLManager::showExpiryMatrix(handleExpirySelection, state);
}

void ProductFetcher::saveProductTask(void *arg)
{
    auto *state = static_cast<ExpirySelectionState *>(arg);

    bool ok = state->service->manageUpdateProduct(state->product);

    if (!ok)
    {
        LVGLManager::showErrorSnackbar("Failed to save product: " + state->product.name);
    }
    else
    {
        ESP_LOGI("ProductFetcher", "Product saved: %s", state->product.name.c_str());
    }

    // DO NOT delete state here – LVGL still holds pointers into it
    delete state;

    vTaskDelete(nullptr);
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

            if (product.category == "Meat & Fish")
            {
                auto *state = new ExpirySelectionState{
                    product,               // product
                    0,                     // dayOffset
                    self->product_service, // service
                };

                showExpiryDateSelection(state);
            }
            else
            {
                if (self->product_service->manageUpdateProduct(product))
                {
                    ESP_LOGI(TAG, "Product saved: %s", product.name.c_str());
                }
                else
                {
                    LVGLManager::showErrorSnackbar("Failed to save product: " + product.name);
                    ESP_LOGW(TAG, "Failed to save product: %s", product.name.c_str());
                }
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

std::string ProductFetcher::mapUkSupermarketCategory(cJSON *tagsArray)
{
#ifdef DEBUG_MEAT
    ESP_LOGI(TAG, "DEBUG_MEAT is defined - categorizing as Meat & Fish");
    return "Meat & Fish";
#endif

    if (!cJSON_IsArray(tagsArray))
        return "Other";

    cJSON *tagItem = nullptr;
    cJSON_ArrayForEach(tagItem, tagsArray)
    {
        if (!cJSON_IsString(tagItem))
            continue;

        std::string t = toLower(tagItem->valuestring);

        if (t.find("vegetable") != std::string::npos || t.find("veg") != std::string::npos || t.find("vegetables") != std::string::npos || t.find("fruit") != std::string::npos || t.find("fruits") != std::string::npos)
            return "Fruit & Veg";

        if (t.find("meat") != std::string::npos || t.find("meats") != std::string::npos || t.find("poultry") != std::string::npos || t.find("poultries") != std::string::npos || t.find("beef") != std::string::npos ||
            t.find("chicken") != std::string::npos || t.find("fish") != std::string::npos || t.find("fishes") != std::string::npos || t.find("seafood") != std::string::npos || t.find("seafoods") != std::string::npos)
            return "Meat & Fish";

        if (t.find("dairy") != std::string::npos || t.find("milk") != std::string::npos || t.find("cheese") != std::string::npos || t.find("cheeses") != std::string::npos ||
            t.find("yogurt") != std::string::npos || t.find("yogurts") != std::string::npos || t.find("egg") != std::string::npos || t.find("eggs") != std::string::npos)
            return "Dairy & Eggs";

        if (t.find("bread") != std::string::npos || t.find("breads") != std::string::npos || t.find("bakery") != std::string::npos || t.find("pastry") != std::string::npos || t.find("pastries") != std::string::npos)
            return "Bakery";

        if (t.find("frozen") != std::string::npos)
            return "Frozen";

        if (t.find("beverage") != std::string::npos || t.find("beverages") != std::string::npos || t.find("drink") != std::string::npos || t.find("drinks") != std::string::npos || t.find("juice") != std::string::npos || t.find("juices") != std::string::npos || t.find("water") != std::string::npos)
            return "Drinks";

        if (t.find("snack") != std::string::npos || t.find("snacks") != std::string::npos || t.find("crisps") != std::string::npos || t.find("chocolate") != std::string::npos || t.find("chocolates") != std::string::npos || t.find("sweets") != std::string::npos)
            return "Snacks";

        if (t.find("cereal") != std::string::npos || t.find("cereals") != std::string::npos || t.find("breakfast") != std::string::npos || t.find("oats") != std::string::npos)
            return "Cereal & Breakfast";

        if (t.find("canned") != std::string::npos || t.find("tinned") != std::string::npos || t.find("jarred") != std::string::npos)
            return "Tins & Jars";

        if (t.find("pasta") != std::string::npos || t.find("pastas") != std::string::npos || t.find("rice") != std::string::npos || t.find("grains") != std::string::npos || t.find("grain") != std::string::npos || t.find("noodle") != std::string::npos || t.find("noodles") != std::string::npos)
            return "Pasta, Rice & Grains";

        if (t.find("sauce") != std::string::npos || t.find("sauces") != std::string::npos || t.find("condiment") != std::string::npos || t.find("condiments") != std::string::npos || t.find("spread") != std::string::npos || t.find("spreads") != std::string::npos)
            return "Condiments & Sauces";

        if (t.find("household") != std::string::npos || t.find("cleaning") != std::string::npos)
            return "Household";
    }

    return "Other";
}

bool ProductFetcher::fetchProductInfo(const std::string &barcode, ProductCacheItem &out, ProductCache *cache)
{
#ifndef DEBUG_MEAT
    // Check cache first
    if (cache && cache->contains(barcode))
    {
        LVGLManager::updateStatusLabel("Loading from cache...");
        return cache->get(barcode, out);
    }
#endif

    LVGLManager::updateStatusLabel("Fetching product info...");

    std::string url = "https://world.openfoodfacts.org/api/v0/product/" + barcode + ".json?fields=product_name,categories_tags";

    ESP_LOGI(TAG, "Fetching product info: %s", url.c_str());

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.buffer_size = 4096;

    ESP_LOGI(TAG, "Sending client request to OpenFoodFacts");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        LVGLManager::updateStatusLabel("Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        LVGLManager::updateStatusLabel("Failed to connect to server");
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        LVGLManager::updateStatusLabel("Server returned error: " + std::to_string(status_code));
        esp_http_client_cleanup(client);
        return false;
    }

    std::string payload;
    payload.reserve(4096);

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
            return false;
        }
    }

    esp_http_client_cleanup(client);

    if (payload.empty())
    {
        LVGLManager::updateStatusLabel("Empty payload");
        return false;
    }

    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root)
    {
        LVGLManager::updateStatusLabel("Failed to parse JSON");
        return false;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || (status->valueint != 1 && status->valueint != 200))
    {
        cJSON_Delete(root);
        LVGLManager::updateStatusLabel("Product not found");
        return false;
    }

    cJSON *product = cJSON_GetObjectItem(root, "product");
    if (cJSON_IsObject(product))
    {
        LVGLManager::updateStatusLabel("Parsing product info...");
        cJSON *nameItem = cJSON_GetObjectItem(product, "product_name");
        out.name = (nameItem && cJSON_IsString(nameItem)) ? nameItem->valuestring : "Unknown";

        cJSON *tags = cJSON_GetObjectItem(product, "categories_tags");
        out.category = mapUkSupermarketCategory(tags);
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
    return true;
}
