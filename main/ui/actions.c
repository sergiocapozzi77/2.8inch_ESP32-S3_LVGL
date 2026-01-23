#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"

void action_product_action(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);

    /* Get the button index */
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(obj);

    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        // No valid button (shouldn't happen for a click)
        return;
    }

    /* Get the button text */
    const char * txt = lv_btnmatrix_get_btn_text(obj, btn_id);

    ESP_LOGI("actions", "Button index: %d\n", btn_id);
    ESP_LOGI("actions", "Button text: %s\n", txt ? txt : "(no text)");
}