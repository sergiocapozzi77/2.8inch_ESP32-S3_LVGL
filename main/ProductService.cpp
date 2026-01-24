#include "ProductService.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>
#include "vars.h"

static const char *TAG = "ProductService";

ProductService::ProductService()
{
}

/* =========================================================
 * URL ENCODING (FIXED: HEX, NOT DECIMAL)
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
 * HTTP GET
 * ========================================================= */
std::string ProductService::httpGet(const std::string &url, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 49152;
    cfg.buffer_size_tx = 4096;
    cfg.skip_cert_common_name_check = false; // unchanged

    ESP_LOGI(TAG, "HTTP ProjectId: %s", ProjectId.c_str());
    ESP_LOGI(TAG, "HTTP apiKey: %s", apiKey.c_str());
    ESP_LOGI(TAG, "HTTP Client opening URL: %s", url.c_str());

    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Connection", "keep-alive");
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP Client open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    ESP_LOGI(TAG, "HTTP Client fetch headers");
    esp_http_client_fetch_headers(client);

    ESP_LOGI(TAG, "HTTP Client fetch content length");
    int content_len = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "Content Length: %d", content_len);

    std::string body;
    body.reserve(content_len > 0 ? content_len : 512);

    char buffer[1024];
    int r;
    while ((r = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        body.append(buffer, r);
    }

    ESP_LOGI(TAG, "HTTP Client fetched response:\n%s", body.c_str());

    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP Status Code: %d", status);

    esp_http_client_cleanup(client);
    return body;
}

/* =========================================================
 * HTTP POST
 * ========================================================= */
std::string ProductService::httpPost(const std::string &url, const std::string &body, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 49152;
    cfg.buffer_size_tx = 4096;
    cfg.skip_cert_common_name_check = false; // unchanged

    ESP_LOGI(TAG, "HTTP POST Client opening URL: %s", url.c_str());

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP Client open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written < 0)
    {
        ESP_LOGE(TAG, "HTTP Client write error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    ESP_LOGI(TAG, "HTTP POST Client fetch headers");
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP POST Status Code: %d", status);

    std::string response;
    char buffer[1024];
    int r;
    while ((r = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, r);
    }

    ESP_LOGI(TAG, "HTTP POST Client fetched response:\n%s", response.c_str());

    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * HTTP PATCH
 * ========================================================= */
std::string ProductService::httpPatch(const std::string &url, const std::string &body, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 49152;
    cfg.buffer_size_tx = 4096;
    cfg.skip_cert_common_name_check = false; // Match httpGet for safer TLS handling

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);

    // Force the Host header to match the TLS SAN
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP Client open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written < 0)
    {
        ESP_LOGE(TAG, "HTTP Client write error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    char buffer[1024];
    int r;
    while ((r = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, r);
    }

    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * HTTP DELETE
 * ========================================================= */
int ProductService::httpDelete(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 49152;
    cfg.buffer_size_tx = 4096;
    cfg.skip_cert_common_name_check = false; // unchanged

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);

    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP Client open error: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return status;
}

/* =========================================================
 * BUSINESS LOGIC
 * ========================================================= */
std::vector<Product> ProductService::getProducts(const std::vector<std::string> &queries)
{
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

    ESP_LOGI(TAG, "GET URL: %s", url.c_str());

    int status;
    std::string body = httpGet(url, status);

    ESP_LOGI(TAG, "GET response: %s", body.c_str());
    if (status != 200)
    {
        ESP_LOGE(TAG, "GET failed: %d", status);
        return result;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root)
        return result;

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (!rows || !cJSON_IsArray(rows))
    {
        cJSON_Delete(root);
        return result;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, rows)
    {
        Product p;
        p.name = cJSON_GetObjectItem(item, "name")->valuestring;
        p.quantity = cJSON_GetObjectItem(item, "quantity")->valueint;
        p.category = cJSON_GetObjectItem(item, "category")->valuestring;
        p.rowId = cJSON_GetObjectItem(item, "$id")->valuestring;
        result.push_back(p);
    }

    cJSON_Delete(root);
    return result;
}

bool ProductService::manageUpdateProduct(Product &product)
{
    const auto mode = get_var_add_or_del();

    // Build query
    std::string q =
        "{\"method\":\"equal\",\"attribute\":\"name\",\"values\":[\"" +
        product.name + "\"]}";

    auto existing = getProducts({q});

    // Case 1: Product already exists → update quantity or delete
    if (!existing.empty())
    {
        Product p = existing[0];

        switch (mode)
        {
        case AddOrDelType_Add:
            ESP_LOGI(TAG, "Adding quantity %d to existing product %s",
                     product.quantity, p.name.c_str());
            p.quantity += product.quantity;
            break;

        case AddOrDelType_Del:
            p.quantity -= product.quantity;
            if (p.quantity <= 0)
            {
                ESP_LOGI(TAG, "Quantity <= 0, deleting product %s", p.name.c_str());
                return deleteProduct(product.rowId);
            }
            else
            {
                ESP_LOGI(TAG, "Subtracting quantity %d from existing product %s",
                         product.quantity, p.name.c_str());
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown AddOrDelType: %d", (int)mode);
            return false;
        }

        return updateProduct(p);
    }

    // Case 2: Product does not exist → add or delete
    switch (mode)
    {
    case AddOrDelType_Add:
        ESP_LOGI(TAG, "Product %s does not exist, adding it", product.name.c_str());
        return addProduct(product);

    case AddOrDelType_Del:
        ESP_LOGI(TAG, "Product %s does not exist, nothing to delete", product.name.c_str());
        return true; // Nothing to delete

    default:
        ESP_LOGE(TAG, "Unknown AddOrDelType: %d", (int)mode);
        return false;
    }
}

bool ProductService::addProduct(Product &product)
{
    ESP_LOGI(TAG, "addProduct() called");

    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows";

    ESP_LOGI(TAG, "POST URL: %s", url.c_str());

    std::string rowId = generateId();
    ESP_LOGI(TAG, "Generated rowId: %s", rowId.c_str());

    /* ---------- Build JSON ---------- */
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to create root JSON object");
        return false;
    }

    cJSON_AddStringToObject(root, "rowId", rowId.c_str());

    cJSON *data = cJSON_CreateObject();
    if (!data)
    {
        ESP_LOGE(TAG, "Failed to create data JSON object");
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddStringToObject(data, "name", product.name.c_str());
    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());

    char *json = cJSON_PrintUnformatted(root);
    if (!json)
    {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return false;
    }

    ESP_LOGI(TAG, "POST JSON payload: %s", json);

    /* ---------- HTTP POST ---------- */
    int status = -1;
    ESP_LOGI(TAG, "Calling httpPost()");
    std::string response = httpPost(url, json, status);

    ESP_LOGI(TAG, "HTTP POST returned status: %d", status);
    ESP_LOGI(TAG, "HTTP response body: %s", response.c_str());

    cJSON_Delete(root);
    free(json);

    /* ---------- Status handling ---------- */
    if (status != 200 && status != 201)
    {
        ESP_LOGE(TAG, "addProduct failed with HTTP status %d", status);
        return false;
    }

    /* ---------- Parse response ---------- */
    cJSON *doc = cJSON_Parse(response.c_str());
    if (!doc)
    {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        return false;
    }

    cJSON *idItem = cJSON_GetObjectItem(doc, "$id");
    if (!idItem || !cJSON_IsString(idItem))
    {
        ESP_LOGE(TAG, "Response JSON missing $id field");
        cJSON_Delete(doc);
        return false;
    }

    product.rowId = idItem->valuestring;
    ESP_LOGI(TAG, "Product created successfully with rowId: %s",
             product.rowId.c_str());

    cJSON_Delete(doc);
    return true;
}

bool ProductService::updateProduct(Product &product)
{
    ESP_LOGI(TAG, "updateProduct() called for rowId: %s", product.rowId.c_str());

    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows/" + product.rowId;

    ESP_LOGI(TAG, "PATCH URL: %s", url.c_str());

    /* ---------- Build JSON ---------- */
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to create root JSON object");
        return false;
    }

    cJSON *data = cJSON_CreateObject();
    if (!data)
    {
        ESP_LOGE(TAG, "Failed to create data JSON object");
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());

    char *json = cJSON_PrintUnformatted(root);
    if (!json)
    {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return false;
    }

    ESP_LOGI(TAG, "PATCH JSON payload: %s", json);

    /* ---------- HTTP PATCH ---------- */
    int status = -1;
    ESP_LOGI(TAG, "Calling httpPatch()");
    std::string response = httpPatch(url, json, status);

    ESP_LOGI(TAG, "HTTP PATCH returned status: %d", status);
    ESP_LOGI(TAG, "HTTP response body: %s", response.c_str());

    cJSON_Delete(root);
    free(json);

    if (status != 200)
    {
        ESP_LOGE(TAG, "updateProduct failed with HTTP status %d", status);
        return false;
    }

    ESP_LOGI(TAG, "Product updated successfully for rowId: %s", product.rowId.c_str());
    return true;
}

bool ProductService::deleteProduct(const std::string &rowId)
{
    ESP_LOGI(TAG, "deleteProduct() called for rowId: %s", rowId.c_str());
    std::string url = Endpoint + "/tablesdb/" + DatabaseId +
                      "/tables/" + CollectionId + "/rows/" + rowId;

    int status = httpDelete(url);
    return status == 200 || status == 204;
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
