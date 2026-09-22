#pragma once
#ifdef _WIN32
#include <stdint.h>
#include <sys/types.h>
#include <io.h>
typedef intptr_t ssize_t;
#define open _open
#define close _close
#define read _read
#define lseek _lseek
int asprintf(char** out, const char* format, ...);
#else
#include_next <unistd.h>
#endif
