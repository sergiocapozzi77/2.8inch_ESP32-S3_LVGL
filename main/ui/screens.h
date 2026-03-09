#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *addremove_matrix;
    lv_obj_t *topbar;
    lv_obj_t *status_lbl;
    lv_obj_t *wifi_img;
    lv_obj_t *battery_lbl;
    lv_obj_t *snackbar;
    lv_obj_t *snackbar__panel;
    lv_obj_t *snackbar__product_lbl;
    lv_obj_t *snackbar__action_lbl;
    lv_obj_t *snackbar__category_lbl;
    lv_obj_t *snackbar_error;
    lv_obj_t *snackbar_error__panel;
    lv_obj_t *snackbar_error__product_lbl;
    lv_obj_t *snackbar_error__action_lbl;
    lv_obj_t *snackbar_error__category_lbl;
    lv_obj_t *expiry_matrix;
    lv_obj_t *expired_pnl;
    lv_obj_t *expired_lbl;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void create_user_widget_snack_bar(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_snack_bar(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/