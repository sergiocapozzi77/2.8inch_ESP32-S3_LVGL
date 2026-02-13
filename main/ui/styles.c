#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: MainStyle_Lbl
//

void init_style_main_style_lbl_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xfffcfcfc));
};

lv_style_t *get_style_main_style_lbl_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_main_style_lbl_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_main_style_lbl(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_main_style_lbl_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_main_style_lbl(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_main_style_lbl_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: RedStyle_matrix
//

void init_style_red_style_matrix_ITEMS_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xffc41111));
};

lv_style_t *get_style_red_style_matrix_ITEMS_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_red_style_matrix_ITEMS_DEFAULT(style);
    }
    return style;
};

void init_style_red_style_matrix_ITEMS_CHECKED(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xffe41010));
    lv_style_set_text_color(style, lv_color_hex(0xffffffff));
};

lv_style_t *get_style_red_style_matrix_ITEMS_CHECKED() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_red_style_matrix_ITEMS_CHECKED(style);
    }
    return style;
};

void add_style_red_style_matrix(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_red_style_matrix_ITEMS_DEFAULT(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_red_style_matrix_ITEMS_CHECKED(), LV_PART_ITEMS | LV_STATE_CHECKED);
};

void remove_style_red_style_matrix(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_red_style_matrix_ITEMS_DEFAULT(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_red_style_matrix_ITEMS_CHECKED(), LV_PART_ITEMS | LV_STATE_CHECKED);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_main_style_lbl,
        add_style_red_style_matrix,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_main_style_lbl,
        remove_style_red_style_matrix,
    };
    remove_style_funcs[styleIndex](obj);
}

