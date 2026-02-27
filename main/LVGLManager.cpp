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

void LVGLManager::updateExpiryMatrixButton(const char *labels[9])
{
    if (labels == nullptr)
        return; // Ensure labels are valid

    // Update the entire button matrix at once
    lv_btnmatrix_set_map(objects.expiry_matrix, labels);
}

int LVGLManager::waitForExpiryMatrixSelection()
{
    // Reset selection state
    lv_btnmatrix_set_selected_btn(objects.expiry_matrix, LV_BTNMATRIX_BTN_NONE);

    // Show expiry matrix
    lv_obj_clear_flag(objects.expiry_matrix, LV_OBJ_FLAG_HIDDEN);

    // Wait for user interaction
    while (lv_btnmatrix_get_selected_btn(objects.expiry_matrix) == LV_BTNMATRIX_BTN_NONE)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to avoid busy-waiting
    }

    // Hide expiry matrix after selection
    lv_obj_add_flag(objects.expiry_matrix, LV_OBJ_FLAG_HIDDEN);

    return lv_btnmatrix_get_selected_btn(objects.expiry_matrix);
}
