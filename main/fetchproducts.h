#pragma once

#include <stdio.h>
#include "product_types.h"

bool fetchProductInfo(const std::string &barcode, ProductCacheItem &out);