#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "styles.h"
#include "esp_log.h"
#include "vars.h"

void action_product_action(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);

    /* Get the button index */
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(obj);

    if (btn_id == LV_BTNMATRIX_BTN_NONE)
    {
        // No valid button (shouldn't happen for a click)
        return;
    }

    /* Get the button text */
    const char *txt = lv_btnmatrix_get_btn_text(obj, btn_id);
    if (btn_id == 0)
    {
        set_var_add_or_del(AddOrDelType_Add);
    }
    else if (btn_id == 1)
    {
        set_var_add_or_del(AddOrDelType_Del);
    }
    else
    {
        // Unknown button
        return;
    }

    ESP_LOGI("actions", "Button index: %d\n", btn_id);
    ESP_LOGI("actions", "Button text: %s\n", txt ? txt : "(no text)");
}

void action_screen_loading(lv_event_t *e)
{
    // This function can be used to perform actions when the loading screen is shown
    ESP_LOGI("actions", "Loading screen shown");
    lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(objects.snackbar_error__panel, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_add_flag(objects.snackbar_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.expiry_matrix, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.wifi_img, LV_OBJ_FLAG_HIDDEN);

    // // Set control flags to differentiate buttons
    // lv_btnmatrix_set_btn_ctrl(objects.addremove_matrix, 0, LV_BTNMATRIX_CTRL_CUSTOM_1); // Blue button
    // lv_btnmatrix_set_btn_ctrl(objects.addremove_matrix, 1, LV_BTNMATRIX_CTRL_CUSTOM_2); // Red button

    // // Red button (button 1) - DEFAULT state
    // lv_obj_add_style(objects.addremove_matrix, get_style_red_style_matrix_ITEMS_DEFAULT(),
    //                  LV_PART_ITEMS | LV_STATE_DEFAULT | LV_BTNMATRIX_CTRL_CUSTOM_2);

    // // Red button (button 1) - PRESSED state
    // lv_obj_add_style(objects.addremove_matrix, get_style_red_style_matrix_ITEMS_CHECKED(),
    //                  LV_PART_ITEMS | LV_STATE_CHECKED | LV_BTNMATRIX_CTRL_CUSTOM_2);
}