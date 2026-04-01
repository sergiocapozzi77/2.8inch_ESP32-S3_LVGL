#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_product_action(lv_event_t * e);
extern void action_screen_loading(lv_event_t * e);
extern void action_add_remove_draw_begin(lv_event_t * e);
extern void action_expiry_close_btn_clicked(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/