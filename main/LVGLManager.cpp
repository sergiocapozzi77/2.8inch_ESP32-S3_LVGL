#include "LVGLManager.h"
#include "freertos/FreeRTOS.h" // MUST be first FreeRTOS header
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui.h"
#include "esp_log.h"
#include "WiFiManager.h"

static const char *TAG = "LVGL";
lv_timer_t *LVGLManager::snackbar_timer = nullptr;
lv_timer_t *LVGLManager::snackbar_error_timer = nullptr;
// Static member initialization
void (*LVGLManager::expiry_selection_callback)(int, void *) = nullptr;
void *LVGLManager::expiry_callback_user_data = nullptr;

void LVGLManager::lvglTickCallback(void *arg)
{
    lv_tick_inc(1);
}

void LVGLManager::init()
{
    battery_manager.init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    const esp_timer_create_args_t timer_args = {
        .callback = &LVGLManager::lvglTickCallback,
        .arg = nullptr,
        .name = "lvgl_tick"};

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000)); // 1 ms

    ui_init();

    ESP_LOGI(TAG, "LVGL initialized");
}

void LVGLManager::tick()
{
    ui_tick();
    updateBatteryUI();
    updateWiFiUI();
    lv_timer_handler();
}

void LVGLManager::updateStatusLabel(const std::string &status)
{
    // Allocate a copy on the heap for thread safety
    auto *statusCopy = new std::string(status);

    // Schedule execution in LVGL context
    lv_async_call(updateStatusLabelAsync, statusCopy);
}

void LVGLManager::updateStatusLabelAsync(void *arg)
{
    auto *status = static_cast<std::string *>(arg);

    // Update the label
    lv_label_set_text(objects.status_lbl, status->c_str());

    delete status;
}

void LVGLManager::updateWiFiUI()
{
    // This method should be called from the LVGL thread (main task)
    if (WiFiManager::isConnected())
    {
        lv_obj_clear_flag(objects.wifi_img, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(objects.wifi_img, LV_OBJ_FLAG_HIDDEN);
    }
}

void LVGLManager::updateBatteryUI()
{
    static bool first_update = true;
    static int last_battery = -1;
    static uint32_t last_update_ms = 0;

    uint32_t now = lv_tick_get();
    const uint32_t interval = 5000;

    if (!first_update && (now - last_update_ms < interval))
        return;

    first_update = false;
    last_update_ms = now;

    int battery = battery_manager.getPercentage();

    if (battery != last_battery)
    {
        lv_label_set_text_fmt(objects.battery_lbl, "%d%%", battery);
        last_battery = battery;
    }
}

struct SnackbarData
{
    std::string product;
    std::string category;
    ProductAction action;
};

void LVGLManager::showProductSnackbar(const std::string &product, const std::string &category,
                                      ProductAction action)
{
    // Allocate data to pass safely across tasks
    auto *data = new SnackbarData{product, category, action};

    // Schedule execution in LVGL context
    lv_async_call(snackbarAsync, data);
}

void LVGLManager::showErrorSnackbar(const std::string &error)
{
    // Allocate a copy on the heap
    auto *errorCopy = new std::string(error);

    lv_async_call(snackbarErrorAsync, errorCopy);
}

void LVGLManager::snackbarAsync(void *arg)
{
    auto *data = static_cast<SnackbarData *>(arg);

    const char *action_txt = "";
    switch (data->action)
    {
    case ProductAction::Added:
        action_txt = "added";
        break;
    case ProductAction::Updated:
        action_txt = "updated";
        break;
    case ProductAction::Deleted:
        action_txt = "deleted";
        break;
    }

    // Update labels
    lv_label_set_text(objects.snackbar__product_lbl, data->product.c_str());
    lv_label_set_text(objects.snackbar__category_lbl, data->category.c_str());
    lv_label_set_text(objects.snackbar__action_lbl, action_txt);

    // Make snackbar visible
    lv_obj_clear_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);

    // Create timer once, or reset if already exists
    if (!snackbar_timer)
    {
        snackbar_timer = lv_timer_create(
            [](lv_timer_t *timer)
            {
                lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(timer); // keep timer but stop it
            },
            5000,
            nullptr);
    }

    // Reset countdown and ensure correct period
    lv_timer_set_period(snackbar_timer, 5000);
    lv_timer_reset(snackbar_timer);
    lv_timer_resume(snackbar_timer);

    delete data;
}

void LVGLManager::snackbarErrorAsync(void *arg)
{
    auto *data = static_cast<std::string *>(arg);

    // Update label
    lv_label_set_text(objects.snackbar_error__action_lbl, data->c_str());
    lv_label_set_text(objects.snackbar_error__category_lbl, "");
    lv_label_set_text(objects.snackbar_error__product_lbl, "");

    // Make snackbar visible
    lv_obj_clear_flag(objects.snackbar_error, LV_OBJ_FLAG_HIDDEN);

    // Create timer once, or reset if already exists
    if (!snackbar_error_timer)
    {
        snackbar_error_timer = lv_timer_create(
            [](lv_timer_t *timer)
            {
                lv_obj_add_flag(objects.snackbar_error, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(timer); // keep timer allocated but stopped
            },
            5000,
            nullptr);
    }

    // Reset countdown and ensure correct period
    lv_timer_set_period(snackbar_error_timer, 5000);
    lv_timer_reset(snackbar_error_timer);
    lv_timer_resume(snackbar_error_timer);

    delete data;
}

// Create a struct to pass the matrix data
struct ExpiryMatrixData
{
    const char **labels;
    void (*callback)(int, void *);
    void *user_data;
};

void LVGLManager::showExpiryMatrix(void (*callback)(int, void *), void *user_data)
{
    // We don't need a wrapper struct if we only pass the callback/data
    // but for simplicity and thread safety, let's wrap the logic
    expiry_selection_callback = callback;
    expiry_callback_user_data = user_data;

    lv_async_call([](void *arg)
                  {
        lv_btnmatrix_set_selected_btn(objects.expiry_matrix, LV_BTNMATRIX_BTN_NONE);
        lv_obj_clear_flag(objects.expiry_matrix, LV_OBJ_FLAG_HIDDEN);
        
        // Ensure event callback is only added ONCE or cleared before adding
        lv_obj_remove_event_cb(objects.expiry_matrix, expiryMatrixEventCallback);
        lv_obj_add_event_cb(objects.expiry_matrix, expiryMatrixEventCallback, LV_EVENT_VALUE_CHANGED, nullptr); }, nullptr);
}

// Add a static variable to LVGLManager to track the active map
static const char **active_button_map = nullptr;

void LVGLManager::updateExpiryMatrixButton(char **new_labels)
{
    // Use async to ensure we are in the LVGL task context
    lv_async_call([](void *arg)
                  {
        char **labels = static_cast<char **>(arg);

        // 1. Get the old map so we can delete it AFTER setting the new one
        const char **old_map = active_button_map;

        // 2. Set the new map
        active_button_map = (const char**)labels;
        lv_btnmatrix_set_map(objects.expiry_matrix, active_button_map);

        // 3. Now it is safe to delete the old map strings
        if (old_map != nullptr) {
            for (int i = 0; old_map[i] != nullptr; i++) {
                // Don't free the "\n" if it's a literal, but since we used strdup, we free all
                free((void*)old_map[i]); 
            }
            delete[] old_map;
        } }, new_labels);
}

void LVGLManager::expiryMatrixEventCallback(lv_event_t *e)
{
    lv_obj_t *btn_matrix = lv_event_get_target(e);
    int selected = lv_btnmatrix_get_selected_btn(btn_matrix);

    // Hide the matrix
    lv_obj_add_flag(objects.expiry_matrix, LV_OBJ_FLAG_HIDDEN);

    // Call the callback with the result and user data
    if (expiry_selection_callback != nullptr)
    {
        void *user_data = expiry_callback_user_data;
        auto callback = expiry_selection_callback;

        // Clear before calling to allow re-entry
        expiry_selection_callback = nullptr;
        expiry_callback_user_data = nullptr;

        callback(selected, user_data);
    }
}