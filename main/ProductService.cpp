#include "ProductService.h"
#include "esp_log.h"
#include "esp_tls.h"
#include <random>
#include <sstream>
#include <iomanip>

static const char *TAG = "ProductService";

ProductService::ProductService(const std::string &key)
    : apiKey(key)
{
}

std::string ProductService::urlEncode(const std::string &s)
{
    std::ostringstream out;

    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out << c;
        }
        else
        {
            out << '%' << std::uppercase << std::setw(2) << std::setfill('0') << int(c);
        }
    }
    return out.str();
}

std::string ProductService::httpGet(const std::string &url, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        status = -1;
        esp_http_client_cleanup(client);
        return "";
    }

    status = esp_http_client_fetch_headers(client);
    int content_len = esp_http_client_get_content_length(client);

    std::string body;
    body.resize(content_len > 0 ? content_len : 4096);

    int read = esp_http_client_read(client, body.data(), body.size());
    if (read > 0)
        body.resize(read);

    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return body;
}

std::string ProductService::httpPost(const std::string &url, const std::string &body, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_http_client_set_post_field(client, body.c_str(), body.size());

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        status = -1;
        esp_http_client_cleanup(client);
        return "";
    }

    status = esp_http_client_get_status_code(client);

    char buffer[4096];
    int read = esp_http_client_read(client, buffer, sizeof(buffer));
    std::string response = (read > 0) ? std::string(buffer, read) : "";

    esp_http_client_cleanup(client);
    return response;
}

std::string ProductService::httpPatch(const std::string &url, const std::string &body, int &status)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_http_client_set_post_field(client, body.c_str(), body.size());

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        status = -1;
        esp_http_client_cleanup(client);
        return "";
    }

    status = esp_http_client_get_status_code(client);

    char buffer[4096];
    int read = esp_http_client_read(client, buffer, sizeof(buffer));
    std::string response = (read > 0) ? std::string(buffer, read) : "";

    esp_http_client_cleanup(client);
    return response;
}

int ProductService::httpDelete(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);

    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return -1;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return status;
}

std::vector<Product> ProductService::getProducts(const std::vector<std::string> &queries)
{
    std::vector<Product> result;

    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CollectionId + "/rows";

    if (!queries.empty())
    {
        url += "?";
        for (size_t i = 0; i < queries.size(); i++)
        {
            url += "queries[" + std::to_string(i) + "]=" + urlEncode(queries[i]);
            if (i < queries.size() - 1)
                url += "&";
        }
    }

    int status;
    std::string body = httpGet(url, status);

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

bool ProductService::addOrUpdateProduct(Product &product)
{
    std::string q = "{\"method\":\"equal\",\"attribute\":\"name\",\"values\":[\"" + product.name + "\"]}";
    std::vector<std::string> queries = {q};

    auto existing = getProducts(queries);

    if (!existing.empty())
    {
        Product p = existing[0];
        p.quantity += product.quantity;
        return updateProduct(p);
    }

    return addProduct(product);
}

bool ProductService::addProduct(Product &product)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CollectionId + "/rows";

    std::string rowId = generateId();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "rowId", rowId.c_str());

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddStringToObject(data, "name", product.name.c_str());
    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());

    char *json = cJSON_PrintUnformatted(root);

    int status;
    std::string response = httpPost(url, json, status);

    cJSON_Delete(root);
    free(json);

    if (status == 200 || status == 201)
    {
        cJSON *doc = cJSON_Parse(response.c_str());
        if (doc)
        {
            product.rowId = cJSON_GetObjectItem(doc, "$id")->valuestring;
            cJSON_Delete(doc);
        }
        return true;
    }

    return false;
}

bool ProductService::updateProduct(Product &product)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CollectionId + "/rows/" + product.rowId;

    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddNumberToObject(data, "quantity", product.quantity);
    cJSON_AddStringToObject(data, "category", product.category.c_str());

    char *json = cJSON_PrintUnformatted(root);

    int status;
    httpPatch(url, json, status);

    cJSON_Delete(root);
    free(json);

    return status == 200;
}

bool ProductService::deleteProduct(const std::string &rowId)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CollectionId + "/rows/" + rowId;
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