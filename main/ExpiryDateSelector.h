#ifndef EXPIRY_DATE_SELECTOR_H
#define EXPIRY_DATE_SELECTOR_H

#include "ProductService.h"
#include "LVGLManager.h"
#include <ctime>
#include <cstring>
#include <string>

class ExpiryDateSelector
{
public:
    ExpiryDateSelector(Product &product, ProductService *service)
        : product(product), service(service), dayOffset(0) {}

    void show();

private:
    Product &product;
    ProductService *service;
    int dayOffset;

    static void handleSelection(int selectedIndex, void *user_data);
    void updateLabels(char **labels);
};

#endif // EXPIRY_DATE_SELECTOR_H