#pragma once
// Deterministic SD/core/peripheral doubles for the production service. The
// async writer is a real host thread; allocations and writes can fail.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace service_test {
struct Node { bool directory=false; std::string path; std::vector<uint8_t> bytes; };
inline std::map<std::string,std::shared_ptr<Node>> files;
inline std::mutex filesMutex;
inline std::atomic<bool> holdWrites{false}, writing{false}, failWrites{false};
inline std::atomic<unsigned> tasks{0}, clockMs{0};
inline std::map<void*,size_t> allocations;
inline unsigned allocAttempt=0, failAllocation=0, wifiStops=0, wifiStarts=0;
inline bool failTask=false, msc=false, psram=true, av=false, core=false;
inline std::vector<uint8_t> ram(8192);
inline meow_nes_info_t info{};
inline void check(bool condition,const char* message) {
    if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
inline void add(const std::string& path,bool directory,const std::vector<uint8_t>& bytes={}) {
    auto node=std::make_shared<Node>(); node->path=path; node->directory=directory; node->bytes=bytes;
    files[path]=node;
}
}
constexpr int FILE_READ=0, CARD_NONE=0, LOW=0, HAL_IOEXP_PA_EN=1;
constexpr int HAL_A=0,HAL_B=1,HAL_JOYSTICK_UP=2,HAL_JOYSTICK_DOWN=3,HAL_JOYSTICK_LEFT=4,HAL_JOYSTICK_RIGHT=5;
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
constexpr int WIFI_OFF=0,WIFI_BRIDGE_CONNECTING=1,ESP_OK=0,pdPASS=1;
constexpr int POWER_SLEEP_MIN_PCT=5,PWR_WAKE_LOWBAT=-1;
inline unsigned millis() { return service_test::clockMs.load(); }
inline int64_t esp_timer_get_time() { return int64_t(millis())*1000; }
inline void delay(unsigned ms) { service_test::clockMs.fetch_add(ms); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
inline int digitalRead(int) { return 1; }
inline bool psramFound() { return service_test::psram; }
inline void* heap_caps_malloc(size_t bytes,unsigned) {
    if(++service_test::allocAttempt==service_test::failAllocation) return nullptr;
    void* p=std::malloc(bytes); if(p) service_test::allocations[p]=bytes; return p;
}
inline void heap_caps_free(void* p) {
    if(!p)return;
    service_test::check(service_test::allocations.erase(p)==1,"invalid/free-twice service allocation"); std::free(p);
}
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned,void* context,int,void*,int) {
    if(service_test::failTask)return 0;
    ++service_test::tasks;
    std::thread([=]{ fn(context); --service_test::tasks; }).detach(); return pdPASS;
}
inline void vTaskDelete(void*) {}
class File {
    std::shared_ptr<service_test::Node> node;
    size_t position=0,entry=0;
public:
    File()=default;
    explicit File(std::shared_ptr<service_test::Node> n):node(std::move(n)){}
    explicit operator bool() const { return bool(node); }
    bool isDirectory() const { return node&&node->directory; }
    const char* name() const { return node?node->path.c_str():""; }
    size_t size() const { std::lock_guard<std::mutex> lock(service_test::filesMutex); return node?node->bytes.size():0; }
    size_t read(uint8_t* out,size_t bytes) {
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        if(!node||position>node->bytes.size())return 0;
        bytes=std::min(bytes,node->bytes.size()-position); std::memcpy(out,node->bytes.data()+position,bytes); position+=bytes; return bytes;
    }
    size_t write(const uint8_t* source,size_t bytes) {
        service_test::writing=true;
        while(service_test::holdWrites)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(service_test::failWrites){ service_test::writing=false; return 0; }
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        node->bytes.assign(source,source+bytes); service_test::writing=false; return bytes;
    }
    bool seek(size_t p){ position=p; return bool(node); }
    void flush(){}
    void close(){node.reset();}
    File openNextFile() {
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        if(!node)return File();
        size_t index=0; const std::string prefix=node->path+"/";
        for(const auto& pair:service_test::files) {
            if(pair.first.compare(0,prefix.size(),prefix)==0 && pair.first.find('/',prefix.size())==std::string::npos) {
                if(index++==entry){++entry;return File(pair.second);}
            }
        }
        return File();
    }
};
struct FakeSd {
    int cardType()const{return 1;}
    File open(const char* path,int=FILE_READ) {
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        auto it=service_test::files.find(path);return it==service_test::files.end()?File():File(it->second);
    }
    File open(const char* path,const char*) {
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        service_test::add(path,false);return File(service_test::files[path]);
    }
    bool exists(const char* path){std::lock_guard<std::mutex> lock(service_test::filesMutex);return service_test::files.count(path)!=0;}
    bool mkdir(const char* path){std::lock_guard<std::mutex> lock(service_test::filesMutex);service_test::add(path,true);return true;}
    bool remove(const char* path){std::lock_guard<std::mutex> lock(service_test::filesMutex);return service_test::files.erase(path)==1;}
    bool rename(const char* from,const char* to) {
        std::lock_guard<std::mutex> lock(service_test::filesMutex);
        auto it=service_test::files.find(from);if(it==service_test::files.end()||service_test::files.count(to))return false;
        auto node=it->second;service_test::files.erase(it);node->path=to;service_test::files[to]=node;return true;
    }
};
inline FakeSd SD_MMC;
struct FakeLcd { void waitDMA(){} void setTextSize(int){} void setTextColor(uint16_t,uint16_t){} void setCursor(int,int){} void print(const char*){} void fillRect(int,int,int,int,uint16_t){} void setBrightness(uint8_t){} };
struct FakePeripheral { void end(){} void update(){} void tick(){} void hasChanged(){} bool digitalWrite(int,int){return true;} };
struct FakeButtons:FakePeripheral { FakePeripheral A,B,Up,Down,Left,Right; };
struct FakeTouch { bool isTouched(){return false;} void getPos(int&,int&){} };
class DEVICES { public: FakeLcd Lcd;FakePeripheral mic,speaker,led,io_exp;FakeButtons button;FakeTouch ctp; };
struct FakeSerial { template<class... Args> void printf(const char*,Args...){} };
inline FakeSerial Serial;
struct FakeEsp { unsigned getFreePsram(){return 8000000;} };
inline FakeEsp ESP;
struct FakeWifi { int getMode(){return 1;} bool isConnected(){return true;} };
inline FakeWifi WiFi;
inline int esp_wifi_stop(){++service_test::wifiStops;return ESP_OK;}
inline int esp_wifi_start(){++service_test::wifiStarts;return ESP_OK;}
inline int esp_wifi_connect(){return ESP_OK;}
inline bool ui_wifi_bridge_scan_busy(){return false;}
inline int ui_wifi_bridge_get_status(){return 0;}
inline bool usb_msc_is_active(){return service_test::msc;}
inline void mk_media_end(){}
inline void mk_tracker_stop(){}
inline void mk_events_flush(){}
inline void power_tick(){}
inline void settings_tick(){}
inline void settings_flush(){}
inline bool settings_get_sleep_mode(){return true;}
inline int sys_get_volume(){return 50;}
inline int sys_get_brightness(){return 50;}
inline void power_reset_sleep_timer(){}
inline bool power_consume_pek_short(){return false;}
inline int power_light_sleep(int,int,bool){return 0;}
inline void power_shutdown(){}
