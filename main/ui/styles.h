#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: MainStyle_Lbl
lv_style_t *get_style_main_style_lbl_MAIN_DEFAULT();
void add_style_main_style_lbl(lv_obj_t *obj);
void remove_style_main_style_lbl(lv_obj_t *obj);



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/