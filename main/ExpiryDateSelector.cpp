#include "ExpiryDateSelector.h"
#include "esp_log.h"
#include <cstdlib>

ExpiryDateSelector expiryDateSelector;

struct ExpirySelectionState
{
    Product product;
    int dayOffset;
};

static const char *TAG = "ExpiryDateSelector";

ExpiryDateSelector::ExpiryDateSelector()
{
    // CRITICAL: Queue holds POINTERS, not objects with std::string
    save_queue = xQueueCreate(5, sizeof(Product *));

    // Create worker task to process the queue
    xTaskCreate(ExpiryDateSelector::saveTask, "product_save", 32768, 8192, 4, nullptr);
}

void ExpiryDateSelector::show(Product product, int dayOffset)
{
    auto *state = new ExpirySelectionState{
        product,   // product
        dayOffset, // dayOffset
    };
    updateLabels(dayOffset);

    LVGLManager::showExpiryMatrix(handleSelection, state);
}

void ExpiryDateSelector::handleSelection(int selectedIndex, void *user_data)
{
    auto *state = static_cast<ExpirySelectionState *>(user_data);
    ESP_LOGI(TAG, "User selected index: %d", selectedIndex);

    if (selectedIndex == 5)
    { // "<<"
        if (state->dayOffset > 0)
            state->dayOffset -= 5;
        expiryDateSelector.show(state->product, state->dayOffset);
        delete state; // Clean up old state
    }
    else if (selectedIndex == 6)
    { // ">>"
        state->dayOffset += 5;
        expiryDateSelector.show(state->product, state->dayOffset);
        delete state; // Clean up old state
    }
    else if (selectedIndex == 7)
    { // "X" skip - don't save
        ESP_LOGI(TAG, "User skipped expiry selection");
        delete state; // Just clean up
    }
    else if (selectedIndex >= 0 && selectedIndex < 5)
    {
        time_t now = time(nullptr);
        tm date_tm{};
        localtime_r(&now, &date_tm);

        int dayIndex = selectedIndex;
        date_tm.tm_mday += state->dayOffset + dayIndex + 1;
        mktime(&date_tm);

        char expiryDate[16];
        strftime(expiryDate, sizeof(expiryDate), "%Y-%m-%d", &date_tm);
        state->product.expiry = expiryDate;
        ESP_LOGI(TAG, "Selected expiry date: %s", state->product.expiry.c_str());

        // Create heap copy of product (survives state deletion)
        Product *productCopy = new Product(state->product);

        // Queue the POINTER (not the object itself)
        if (xQueueSend(expiryDateSelector.save_queue, &productCopy, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to queue product for saving");
            LVGLManager::showErrorSnackbar("Save queue full");
            delete productCopy; // Clean up if queue fails
        }

        delete state; // Clean up
    }
    else
    {
        ESP_LOGW(TAG, "Invalid selection: %d", selectedIndex);
        LVGLManager::showErrorSnackbar("Invalid selection");
        delete state; // Clean up
    }
}

void ExpiryDateSelector::saveTask(void *arg)
{
    auto *self = static_cast<ExpiryDateSelector *>(arg);
    Product *product = nullptr; // Now receiving POINTER

    ESP_LOGI(TAG, "Save task started, waiting for products...");

    while (true)
    {
        // Block waiting for product pointers
        if (xQueueReceive(self->save_queue, &product, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Processing product save: %s", product->name.c_str());

            // Check stack health
            UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "Stack remaining: %d bytes", stackLeft * sizeof(StackType_t));

            if (!productService.manageUpdateProduct(*product))
            {
                ESP_LOGE(TAG, "Failed to save product: %s", product->name.c_str());
                LVGLManager::showErrorSnackbar("Failed to save: " + product->name);
            }
            else
            {
                ESP_LOGI(TAG, "Successfully saved: %s", product->name.c_str());
            }

            delete product; // Clean up heap-allocated copy
        }
    }
}

void ExpiryDateSelector::updateLabels(int dayOffset)
{
    char **labels = new char *[10]; // labels
    time_t now = time(nullptr);

    for (int i = 0; i < 9; ++i)
    {
        if (i == 4)
        {
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
            tm date_tm{};
            localtime_r(&now, &date_tm);

            int dayIndex = (i < 4) ? i : (i - 1);
            date_tm.tm_mday += dayOffset + dayIndex + 1;
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
}