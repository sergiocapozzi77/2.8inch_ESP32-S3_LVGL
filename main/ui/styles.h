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

// Style: RedStyle_matrix
lv_style_t *get_style_red_style_matrix_ITEMS_DEFAULT();
lv_style_t *get_style_red_style_matrix_ITEMS_CHECKED();
void add_style_red_style_matrix(lv_obj_t *obj);
void remove_style_red_style_matrix(lv_obj_t *obj);



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/