#pragma once

#include <stdio.h>
#include "product_types.h"
#include "product_cache.h"

bool fetchProductInfo(const std::string &barcode, ProductCacheItem &out, ProductCache *cache = nullptr);