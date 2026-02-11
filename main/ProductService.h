#pragma once

#include <string>
#include <vector>
#include "esp_http_client.h"
#include "cJSON.h"
#include "secrets.h"

struct Product
{
    std::string name;
    int quantity = 0;
    std::string category;
    std::string rowId;
    std::string expiry;
};

class ProductService
{
public:
    ProductService();

    // Public API methods
    std::vector<Product> getProducts(const std::vector<std::string> &queries = {});
    bool manageUpdateProduct(Product &product);
    bool addProduct(Product &product);
    bool updateProduct(Product &product);
    bool deleteProduct(Product &product);

private:
    // Configuration (from secrets.h)
    const std::string apiKey = APPWRITE_API_KEY;
    const std::string Endpoint = "https://fra.cloud.appwrite.io/v1";
    const std::string ProjectId = "6954045e003c75c1c3bf";
    const std::string DatabaseId = "695404ac0021bf7d9707";
    const std::string CollectionId = "products";

    // HTTP helper methods
    esp_http_client_handle_t createHttpClient(const std::string &url);
    std::string httpGet(const std::string &url, int &status);
    std::string httpPost(const std::string &url, const std::string &body, int &status);
    std::string httpPatch(const std::string &url, const std::string &body, int &status);
    int httpDelete(const std::string &url);

    // Utility methods
    std::string urlEncode(const std::string &s);
    std::string generateId(int length = 12);
};