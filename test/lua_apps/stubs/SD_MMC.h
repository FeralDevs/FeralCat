#pragma once
#include "FS.h"

struct TestSdMmc {
    File open(const char* path, const char* mode = FILE_READ);
};
extern TestSdMmc SD_MMC;
