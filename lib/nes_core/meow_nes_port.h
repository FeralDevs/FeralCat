#pragma once
#include <stddef.h>
void *meow_core_alloc(size_t bytes);
void *meow_core_calloc(size_t count, size_t bytes);
void meow_core_free(void *ptr);
char *meow_core_strdup(const char *value);
void meow_core_log(int level, const char *fmt, ...);
