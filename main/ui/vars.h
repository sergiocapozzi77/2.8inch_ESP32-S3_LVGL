#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

typedef enum {
    AddOrDelType_Add = 0,
    AddOrDelType_Del = 1
} AddOrDelType;

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_ADD_OR_DEL = 0
};

// Native global variables

extern AddOrDelType get_var_add_or_del();
extern void set_var_add_or_del(AddOrDelType value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/