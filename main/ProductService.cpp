#include "ProductService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>
#include "vars.h"
#include "LVGLManager.h"

static const char *TAG = "ProductService";

// Configuration - move to Kconfig or NVS
#ifndef APPWRITE_ENDPOINT
#define APPWRITE_ENDPOINT CONFIG_APPWRITE_ENDPOINT
#endif

ProductService::ProductService()
{
}

/* =========================================================
 * URL ENCODING
 * ========================================================= */
std::string ProductService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;

    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out << c;
        }
        else
        {
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return out.str();
}

/* =========================================================
 * HTTP HELPER - DRY PRINCIPLE
 * ========================================================= */
esp_http_client_handle_t ProductService::createHttpClient(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 8192;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    // Set common headers
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    return client;
}

/* =========================================================
 * HTTP GET
 * ========================================================= */
std::string ProductService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    int content_len = esp_http_client_get_content_length(client);

    std::string body;
    body.reserve(content_len > 0 ? content_len : 512);

    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        body.append(buffer, bytes_read);
    }

    if (bytes_read < 0)
    {
        ESP_LOGE(TAG, "HTTP read error");
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Status: %d, Body length: %d", status, body.length());

    esp_http_client_cleanup(client);
    return body;
}

/* =========================================================
 * HTTP POST
 * ========================================================= */
std::string ProductService::httpPost(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "POST: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != body.size())
    {
        ESP_LOGE(TAG, "Write error: wrote %d of %d bytes", bytes_written, body.size());
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "POST Status: %d. Reading response", status);
    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "POST Status: %d", status);
    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * HTTP PATCH
 * ========================================================= */
std::string ProductService::httpPatch(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "PATCH: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != body.size())
    {
        ESP_LOGE(TAG, "Write error: wrote %d of %d bytes", bytes_written, body.size());
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "PATCH Status: %d", status);
    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * HTTP DELETE
 * ========================================================= */
int ProductService::httpDelete(const std::string &url)
{
    ESP_LOGI(TAG, "DELETE: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
        return -1;

    esp_http_client_set_method(client, HTTP_METHOD_DELETE);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "DELETE Status: %d", status);
    esp_http_client_cleanup(client);
    return status;
}

/* =========================================================
 * BUSINESS LOGIC
 * ========================================================= */
std::vector<Product> ProductService::getProducts(const std::vector<std::string> &queries, int &out)
{
    out = -1;
    std::vector<Product> result;

    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows";

    if (!queries.empty())
    {
        url += "?";
        for (size_t i = 0; i < queries.size(); i++)
        {
            url += "queries[" + std::to_string(i) + "]=" + urlEncode(queries[i]);
            if (i + 1 < queries.size())
                url += "&";
        }
    }

    int status;
    std::string body = httpGet(url, status);

    if (status != 200)
    {
        ESP_LOGE(TAG, "GET failed: %d, Reason: %s", status, body.c_str());
        return result;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "JSON parse error");
        return result;
    }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (!rows || !cJSON_IsArray(rows))
    {
        ESP_LOGW(TAG, "No 'rows' array in response");
        cJSON_Delete(root);
        return result;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, rows)
    {
        // Safe JSON extraction with null checks
        cJSON *nameItem = cJSON_GetObjectItem(item, "name");
        cJSON *qtyItem = cJSON_GetObjectItem(item, "quantity");
        cJSON *catItem = cJSON_GetObjectItem(item, "category");
        cJSON *idItem = cJSON_GetObjectItem(item, "$id");
        cJSON *expiryItem = cJSON_GetObjectItem(item, "expiry");

        if (!nameItem || !cJSON_IsString(nameItem) ||
            !qtyItem || !cJSON_IsNumber(qtyItem) ||
            !idItem || !cJSON_IsString(idItem))
        {
            ESP_LOGW(TAG, "Skipping malformed product");
            continue;
        }

        Product p;
        p.name = nameItem->valuestring;
        p.quantity = qtyItem->valueint;
        p.category = (catItem && cJSON_IsString(catItem)) ? catItem->valuestring : "";
        p.rowId = idItem->valuestring;
        p.expiry = (expiryItem && cJSON_IsString(expiryItem)) ? expiryItem->valuestring : "";
        result.push_back(p);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Retrieved %d products", result.size());

    out = 0;
    return result;
}

bool ProductService::manageUpdateProduct(Product &product)
{
    LVGLManager::updateStatusLabel("Saving product...");
    std::vector<Product> existing;
    const auto mode = get_var_add_or_del();
    int queryResult;

    if (product.expiry.empty())
    {
        // Build query
        std::string q =
            "{\"method\":\"equal\",\"attribute\":\"name\",\"values\":[\"" +
            product.name + "\"]}";

        existing = getProducts({q}, queryResult);
    }

    // Case 1: Product exists
    if (!existing.empty())
    {
        Product p = existing[0];

        switch (mode)
        {
        case AddOrDelType_Add:
            ESP_LOGI(TAG, "Adding %d to existing %s (current: %d)",
                     product.quantity, p.name.c_str(), p.quantity);
            p.quantity += product.quantity;
            break;

        case AddOrDelType_Del:
            ESP_LOGI(TAG, "Subtracting %d from %s (current: %d)",
                     product.quantity, p.name.c_str(), p.quantity);
            p.quantity -= product.quantity;

            if (p.quantity <= 0)
            {
                ESP_LOGI(TAG, "Quantity <= 0, deleting %s", p.name.c_str());
                return deleteProduct(p); // ✅ Fixed: use p.rowId
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown mode: %d", (int)mode);
            return false;
        }

        return updateProduct(p);
    }

    // Case 2: Product doesn't exist
    switch (mode)
    {
    case AddOrDelType_Add:
        ESP_LOGI(TAG, "Creating new product: %s", product.name.c_str());
        return addProduct(product);

    case AddOrDelType_Del:
        ESP_LOGW(TAG, "Cannot delete non-existent product: %s", product.name.c_str());
        return true; // Not an error

    default:
        ESP_LOGE(TAG, "Unknown mode: %d", (int)mode);
        return false;
    }
}

bool ProductService::addProduct(Product &product)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows";

    std::string rowId = generateId();

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "JSON alloc failed");
        return false;
    }

    cJSON_AddStringToObject(root, "rowId", rowId.c_str());

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data)
    {
        ESP_LOGE(TAG, "JSON data alloc failed");
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddStringToObject(data, "name", product.name.c_str());
    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());
    cJSON_AddStringToObject(data, "expiry", product.expiry.c_str());

    char *json = cJSON_PrintUnformatted(root);
    if (!json)
    {
        ESP_LOGE(TAG, "JSON print failed");
        cJSON_Delete(root);
        return false;
    }

    int status = -1;
    std::string response = httpPost(url, json, status);

    cJSON_Delete(root);
    free(json);

    if (status != 200 && status != 201)
    {
        ESP_LOGE(TAG, "addProduct failed: %d", status);
        return false;
    }

    // Parse response to get rowId
    cJSON *doc = cJSON_Parse(response.c_str());
    if (!doc)
    {
        ESP_LOGW(TAG, "Could not parse response");
        return true; // Product might still be created
    }

    cJSON *idItem = cJSON_GetObjectItem(doc, "$id");
    if (idItem && cJSON_IsString(idItem))
    {
        product.rowId = idItem->valuestring;
        ESP_LOGI(TAG, "Product created: %s", product.rowId.c_str());
    }

    cJSON_Delete(doc);

    LVGLManager::updateStatusLabel("Product added");
    LVGLManager::showProductSnackbar(product.name, product.category, ProductAction::Added);
    return true;
}

bool ProductService::updateProduct(Product &product)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows/" + product.rowId;

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return false;

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data)
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());

    char *json = cJSON_PrintUnformatted(root);
    if (!json)
    {
        cJSON_Delete(root);
        return false;
    }

    int status = -1;
    std::string response = httpPatch(url, json, status);

    cJSON_Delete(root);
    free(json);

    if (status != 200)
    {
        ESP_LOGE(TAG, "updateProduct failed: %d", status);
        return false;
    }

    LVGLManager::updateStatusLabel("Product updated");
    LVGLManager::showProductSnackbar(product.name, product.category, ProductAction::Updated);
    return true;
}

bool ProductService::deleteProduct(Product &product)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows/" + product.rowId;

    int status = httpDelete(url);
    bool success = (status == 200 || status == 204);

    if (success)
    {
        LVGLManager::updateStatusLabel("Product deleted");
        LVGLManager::showProductSnackbar(product.name, product.category, ProductAction::Deleted);
    }
    else
    {
        ESP_LOGE(TAG, "deleteProduct failed: %d", status);
    }

    return success;
}

std::string ProductService::generateId(int length)
{
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string id;
    id.reserve(length);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    for (int i = 0; i < length; i++)
    {
        id += chars[dist(gen)];
    }

    return id;
}

std::vector<Product> ProductService::getExpiringProducts()
{
    std::vector<Product> result;

    // Get current datetime and tomorrow's datetime in ISO 8601 format
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char today[25], tomorrow[25];
    strftime(today, sizeof(today), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    timeinfo.tm_mday += 1; // Add one day
    mktime(&timeinfo);     // Normalize the time structure
    strftime(tomorrow, sizeof(tomorrow), "%Y-%m-%dT23:59:59Z", &timeinfo);

    // Build queries for today and tomorrow
    std::string queryTomorrow = "{\"method\":\"lessThanEqual\",\"attribute\":\"expiry\",\"values\":[\"" + std::string(tomorrow) + "\"]}";

    // Fetch products
    int queryResult;
    result = getProducts({queryTomorrow, queryTomorrow}, queryResult);

    if (queryResult != 0)
    {
        ESP_LOGE(TAG, "Failed to fetch expiring products");
    }
    else
    {
        ESP_LOGI(TAG, "Fetched %d products expiring today or tomorrow", result.size());
    }

    return result;
}