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
    ExpiryDateSelector();

    void show(Product product, int dayOffset);

private:
    QueueHandle_t save_queue; // Public so it can be accessed
    static void handleSelection(int selectedIndex, void *user_data);
    void updateLabels(int dayOffset);
    static void saveTask(void *arg);
};

extern ExpiryDateSelector expiryDateSelector;
#endif // EXPIRY_DATE_SELECTOR_H