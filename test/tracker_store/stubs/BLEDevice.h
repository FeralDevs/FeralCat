#pragma once
class BLEDevice {
public:
    static void init(const char* name);
    static void deinit(bool release_memory);
    static bool getInitialized();
};
