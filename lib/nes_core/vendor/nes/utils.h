#pragma once
/* MeowKit portability adaptation; see ../../PROVENANCE.md. */
#include <stdio.h>
#include "meow_nes.h"
#include "meow_nes_port.h"
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define LOG_PRINTF(level, ...) meow_core_log(level, __VA_ARGS__)
#define CRC32(a,b,c) meow_nes_crc32(a,b,c)
#define MESSAGE_ERROR(...) LOG_PRINTF(1, __VA_ARGS__)
#define MESSAGE_WARN(...) LOG_PRINTF(2, __VA_ARGS__)
#define MESSAGE_INFO(...) LOG_PRINTF(3, __VA_ARGS__)
#ifdef NOFRENDO_DEBUG
#define MESSAGE_DEBUG(...) LOG_PRINTF(4, __VA_ARGS__)
#else
#define MESSAGE_DEBUG(...) ((void)0)
#endif
#define MESSAGE_TRACE(...) LOG_PRINTF(4, __VA_ARGS__)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ASSERT(expr) assert(expr)
#define UNUSED(x) (void)(x)
#define malloc meow_core_alloc
#define calloc meow_core_calloc
#define free meow_core_free
#define strdup meow_core_strdup
