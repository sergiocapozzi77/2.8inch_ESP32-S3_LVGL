#pragma once

#include <string>
#include "esp_timer.h"
#include "BatteryManager.h"
#include "lvgl.h"

enum class ProductAction
{
    Added,
    Updated,
    Deleted
};

class LVGLManager
{
public:
    void init();
    void tick();

    static void showProductSnackbar(const std::string &product, const std::string &category, ProductAction action);
    static void showErrorSnackbar(const std::string &error);

    static void updateExpiryMatrixButton(char **new_labels);
    static int waitForExpiryMatrixSelection();
    // Add this public method
    static void updateStatusLabel(const std::string &status);
    static void showExpiryMatrix(void (*callback)(int selected_index, void *user_data), void *user_data);
    static void updateExpiredProductsLabel(const std::string &expiredProductsText);

private:
    static void expiryMatrixEventCallback(lv_event_t *e);
    static void (*expiry_selection_callback)(int, void *);
    static void *expiry_callback_user_data;

    static void updateStatusLabelAsync(void *arg);
    static void snackbarAsync(void *arg);
    static void snackbarErrorAsync(void *arg);
    static void lvglTickCallback(void *arg);
    BatteryManager battery_manager;
    void updateBatteryUI();
    void updateWiFiUI();

    esp_timer_handle_t lvgl_tick_timer;
    static lv_timer_t *snackbar_timer;
    static lv_timer_t *snackbar_error_timer;
};
