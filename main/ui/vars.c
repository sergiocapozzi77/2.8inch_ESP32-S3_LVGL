#include "vars.h"

AddOrDelType add_or_del;

AddOrDelType get_var_add_or_del() {
    return add_or_del;
}

void set_var_add_or_del(AddOrDelType value) {
    add_or_del = value;
}
