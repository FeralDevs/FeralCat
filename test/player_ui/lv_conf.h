#pragma once
#include "../../lib/lv_conf.h"
#undef LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_FAST_MEM
#undef LV_TICK_CUSTOM
#define LV_TICK_CUSTOM 0
#undef LV_USE_PNG
#define LV_USE_PNG 0
#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER abort();
#include <stddef.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ui_test_malloc(size_t);
void* ui_test_realloc(void*, size_t);
void ui_test_free(void*);
#ifdef __cplusplus
}
#endif
#undef LV_MEM_CUSTOM_ALLOC
#undef LV_MEM_CUSTOM_REALLOC
#undef LV_MEM_CUSTOM_FREE
#define LV_MEM_CUSTOM_ALLOC ui_test_malloc
#define LV_MEM_CUSTOM_REALLOC ui_test_realloc
#define LV_MEM_CUSTOM_FREE ui_test_free
