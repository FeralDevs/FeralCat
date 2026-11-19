#include "nes_service.h"
#include "nes_av.h"
#include "nes_input.h"
#include "nes_rom.h"
#include "nes_save.h"
#include "nes_path.h"
#include "../mk_nes_abi.h"
#include <atomic>
#include <meow_nes.h>
#ifdef MEOW_NES_SERVICE_HOST_TEST
#include "nes_service_test_platform.h"
#else
#include "../../bsp/devices.h"
#include "../app_sdk.h"
#include "../mk_events.h"
#include "../power_mgmt.h"
#include "../settings_bridge.h"
#include "../usb_msc.h"
#include "../../ui/ui_wifi_bridge.h"
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#endif
#include <algorithm>

namespace {
using namespace meow::nes;
constexpr uint16_t Bg = 0x0843, White = 0xffff, Accent = 0x5ffa;

struct Session {
    DEVICES* dev = nullptr;
    Av av;
    Input input;
    uint8_t* rom = nullptr;
    uint8_t* saveBuffer = nullptr;
    size_t saveBytes = 0;
    RomInfo romInfo;
    uint32_t savedCrc = 0, lastPower = 0, lastPek = 0;
    bool playing = false, wifiPaused = false, wifiReconnect = false;
    char loadedPath[MK_NES_PATH_BYTES]{};
    std::atomic<bool> saveRunning{false};
    bool savePending = false, saveOk = false;
    uint32_t snapshotCrc = 0;
    char saveMessage[128]{};
    bool hasSavedCrc = false, opened = false, active = false, sound = true;
    bool touching = false, touchEdge = false;
    int touchX = 0, touchY = 0;
    uint8_t raw = 0, held = 0, edge = 0, previous = 0;
    char message[128]{};
    struct CatalogEntry {
        char name[256];
        uint32_t bytes;
        bool directory;
    };
    CatalogEntry* catalog = nullptr;
    size_t catalogCount = 0, catalogSkipped = 0;
    bool catalogTruncated = false, catalogUnavailable = false, catalogOom = false;
    char catalogDir[256] = "/roms/nes";
} s;

void* allocate(size_t bytes, void*) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void release(void* p, void*) { heap_caps_free(p); }
void coreLog(int, const char* msg, void*) { Serial.printf("[NES core] %s\n", msg); }
void status(const char* msg) {
    snprintf(s.message, sizeof(s.message), "%s", msg);
    Serial.printf("[NES] %s\n", s.message);
}
void text(int x, int y, const char* value, uint16_t color = White) {
    auto& lcd = s.dev->Lcd;
    lcd.setTextSize(1); lcd.setTextColor(color, Bg); lcd.setCursor(x,y); lcd.print(value);
}
void stopAv() {
    // A failed stop keeps ownership. Never hand shared I2S/LCD to another app
    // while the worker can still touch it. Each retry yields to the watchdog.
    while (!s.av.stop(1000)) { status(s.av.error()); delay(20); }
}

bool readSave(const char* path, bool load) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file || file.isDirectory() || file.size() != s.saveBytes + SaveHeaderBytes) return false;
    const size_t total = file.size();
    bool ok = file.read(s.saveBuffer,total) == total;
    file.close();
    SaveView view;
    ok = ok && decodeSave(s.romInfo.romCrc32,s.saveBuffer,total,view) == SaveResult::Ok
         && view.payloadBytes == s.saveBytes;
    if (ok && load) {
        ok = meow_nes_sram_write(view.payload,view.payloadBytes) == MEOW_NES_OK;
        if (ok) { s.savedCrc = crc32(view.payload,view.payloadBytes); s.hasSavedCrc = true; }
    }
    return ok;
}
void savePaths(char* base, char* temp, char* backup) {
    snprintf(base,64,"/saves/nes/%08lx.sav",(unsigned long)s.romInfo.romCrc32);
    snprintf(temp,64,"/saves/nes/%08lx.tmp",(unsigned long)s.romInfo.romCrc32);
    snprintf(backup,64,"/saves/nes/%08lx.bak",(unsigned long)s.romInfo.romCrc32);
}
// The emulation task snapshots SRAM; a low-priority task commits that copy.
// While it runs, only that worker touches saveBuffer and the save files. Joining
// precedes load/reset/close/sleep, and release/acquire publishes its result.
bool writeSnapshot() {
    auto fail=[](const char* message) {
        snprintf(s.saveMessage,sizeof(s.saveMessage),"%s",message); return false;
    };
    if ((!SD_MMC.exists("/saves") && !SD_MMC.mkdir("/saves")) ||
        (!SD_MMC.exists("/saves/nes") && !SD_MMC.mkdir("/saves/nes")))
        return fail("Cannot create /saves/nes");
    size_t written=0;
    if (encodeSave(s.romInfo.romCrc32,s.saveBuffer+SaveHeaderBytes,s.saveBytes,
                   s.saveBuffer,s.saveBytes+SaveHeaderBytes,written)!=SaveResult::Ok)
        return fail("Cannot encode SRAM");
    char base[64],temp[64],backup[64]; savePaths(base,temp,backup);
    File file=SD_MMC.open(temp,"w");
    if (!file) return fail("Cannot open SRAM temporary file");
    bool complete=file.write(s.saveBuffer,written)==written;
    file.flush(); file.close();
    if (!complete || !readSave(temp,false)) return fail("SRAM write verification failed");
    if (SD_MMC.exists(base)) {
        if (readSave(base,false)) {
            if (SD_MMC.exists(backup) && !SD_MMC.remove(backup)) return fail("Cannot rotate SRAM backup");
            if (!SD_MMC.rename(base,backup)) return fail("Cannot preserve previous SRAM");
        } else {
            char bad[80]; snprintf(bad,sizeof(bad),"%s.invalid",base);
            if (SD_MMC.exists(bad) || !SD_MMC.rename(base,bad))
                return fail("Invalid old SRAM retained; rename .invalid file first");
        }
    }
    if (!SD_MMC.rename(temp,base)) return fail("SRAM commit failed; backup retained");
    snprintf(s.saveMessage,sizeof(s.saveMessage),"SRAM saved");
    return true;
}
bool collectSave(bool wait) {
    if (!s.savePending) return true;
    if (!wait && s.saveRunning.load(std::memory_order_acquire)) return true;
    while (s.saveRunning.load(std::memory_order_acquire)) delay(1);
    s.savePending=false;
    if (s.saveOk) { s.savedCrc=s.snapshotCrc; s.hasSavedCrc=true; }
    status(s.saveMessage);
    return s.saveOk;
}
bool captureSave() {
    if (!s.saveBuffer || usb_msc_is_active()) { status("SRAM save unavailable"); return false; }
    if (meow_nes_sram_read(s.saveBuffer+SaveHeaderBytes,s.saveBytes)!=MEOW_NES_OK) {
        status("Cannot snapshot SRAM"); return false;
    }
    s.snapshotCrc=crc32(s.saveBuffer+SaveHeaderBytes,s.saveBytes);
    return true;
}
bool saveRam() {
    collectSave(true); // Retry a failed background save from current cartridge RAM.
    if (!s.opened || !s.saveBytes) return true;
    if (!captureSave()) return false;
    if (s.hasSavedCrc && s.snapshotCrc==s.savedCrc) { status("SRAM already saved"); return true; }
    s.saveOk=writeSnapshot();
    status(s.saveMessage);
    if (s.saveOk) { s.savedCrc=s.snapshotCrc; s.hasSavedCrc=true; }
    return s.saveOk;
}
bool autoSave() {
    if (!s.saveBytes || s.savePending) return true;
    if (!captureSave()) return false;
    if (s.hasSavedCrc && s.snapshotCrc==s.savedCrc) return true;
    s.saveRunning.store(true,std::memory_order_release); s.savePending=true;
    if (xTaskCreatePinnedToCore([](void*) {
        s.saveOk=writeSnapshot();
        s.saveRunning.store(false,std::memory_order_release);
        vTaskDelete(nullptr);
    },"nes_save",4096,nullptr,1,nullptr,0)!=pdPASS) {
        s.saveRunning.store(false,std::memory_order_release); s.savePending=false;
        status("Cannot start SRAM save worker"); return false;
    }
    return true;
}
void servicePower() {
    if (millis() - s.lastPower >= 1000) {
        s.lastPower = millis();
        power_tick(); // Existing shutdown callback calls prepare_shutdown first.
        settings_tick();
    }
    s.dev->led.update();
}
void poll() {
    uint8_t raw = 0;
    if (!digitalRead(HAL_A)) raw |= MEOW_NES_A;
    if (!digitalRead(HAL_B)) raw |= MEOW_NES_B;
    if (!digitalRead(HAL_JOYSTICK_UP)) raw |= MEOW_NES_UP;
    if (!digitalRead(HAL_JOYSTICK_DOWN)) raw |= MEOW_NES_DOWN;
    if (!digitalRead(HAL_JOYSTICK_LEFT)) raw |= MEOW_NES_LEFT;
    if (!digitalRead(HAL_JOYSTICK_RIGHT)) raw |= MEOW_NES_RIGHT;
    s.raw = raw;
    bool touched = s.dev->ctp.isTouched();
    s.touchEdge = touched && !s.touching; s.touching = touched;
    if (touched) s.dev->ctp.getPos(s.touchX,s.touchY);
    s.held = s.input.update(raw,millis());
    s.edge = s.held & ~s.previous; s.previous = s.held;
    if (s.held || touched) power_reset_sleep_timer();
    servicePower();
}
void releaseInput() {
    // Consume the launch/pause/wake gesture so it never enters the next screen.
    do { delay(8); poll(); } while (s.raw || s.held || s.touching);
    s.input.reset(); s.previous = s.held = s.edge = 0; s.touchEdge = false;
}
void closeRom() {
    stopAv(); collectSave(true);
    meow_nes_close(); s.opened = false;
    heap_caps_free(s.rom); s.rom = nullptr;
    heap_caps_free(s.saveBuffer); s.saveBuffer = nullptr;
    s.saveBytes = 0; s.hasSavedCrc = false; s.loadedPath[0]=0;
}
bool openRom(const char* path) {
    File file = SD_MMC.open(path,FILE_READ);
    if (!file || file.isDirectory()) { status("Cannot read ROM from SD"); return false; }
    uint8_t header[16]; const size_t bytes = file.size();
    size_t got = file.read(header,sizeof(header));
    auto result = inspectRomHeader(header,got,bytes,s.romInfo);
    if (result != RomResult::Ok) { status(romResultMessage(result)); return false; }
    if (!meow_nes_mapper_supported(s.romInfo.mapper)) {
        snprintf(s.message,sizeof(s.message),"Mapper %u is not supported by this core",s.romInfo.mapper);
        return false;
    }
    s.rom = static_cast<uint8_t*>(allocate(bytes,nullptr));
    if (!s.rom) { status("Not enough PSRAM for ROM"); return false; }
    if (!file.seek(0) || file.read(s.rom,bytes) != bytes) { status("ROM read incomplete"); return false; }
    file.close();
    result = parseRom(s.rom,bytes,s.romInfo);
    if (result != RomResult::Ok) { status(romResultMessage(result)); return false; }
    meow_nes_options_t options{};
    options.sample_rate = Av::SampleRate; options.region = MEOW_NES_AUTO;
    options.alloc = allocate; options.free = release; options.log = coreLog;
    auto opened = meow_nes_open(s.rom,bytes,&options);
    if (opened != MEOW_NES_OK) { status(meow_nes_error_string(opened)); return false; }
    s.opened = true; s.saveBytes = meow_nes_sram_size();
    if (s.saveBytes) {
        s.saveBuffer = static_cast<uint8_t*>(allocate(s.saveBytes+SaveHeaderBytes,nullptr));
        if (!s.saveBuffer) { status("Not enough PSRAM for cartridge save"); return false; }
        char base[64],temp[64],backup[64]; savePaths(base,temp,backup);
        bool exists = SD_MMC.exists(base) || SD_MMC.exists(backup) || SD_MMC.exists(temp);
        // A completed .tmp is also recoverable after power loss before rename.
        bool loaded = readSave(base,true) || readSave(temp,true) || readSave(backup,true);
        if (exists && !loaded) { status("Stored SRAM is invalid. Files retained; repair or move them before retrying."); return false; }
    }
    Serial.printf("[NES] Open mapper=%u PRG=%u CHR=%u SRAM=%u CRC=%08lx PSRAMfree=%u\n",
        s.romInfo.mapper,(unsigned)s.romInfo.prgBytes,(unsigned)s.romInfo.chrBytes,
        (unsigned)s.saveBytes,(unsigned long)s.romInfo.romCrc32,(unsigned)ESP.getFreePsram());
    return true;
}
void sidebars() {
    s.dev->Lcd.fillRect(0,0,32,240,Bg); s.dev->Lcd.fillRect(288,0,32,240,Bg);
    text(5,34,"SEL",Accent); text(289,34,"START",Accent);
    text(291,200,"MENU",Accent);
}
bool beginAv() {
    s.av.setVolume(static_cast<unsigned>(sys_get_volume()));
    if (s.av.begin(s.dev,s.sound)) return true;
    status(s.av.error()); stopAv(); return false;
}
bool sleepGame() {
    stopAv();
    if (!saveRam()) return false;
    settings_flush();
    // Do not enter level-triggered GPIO sleep until buttons have been released.
    releaseInput(); s.dev->Lcd.setBrightness(0);
    int wake = power_light_sleep(60,POWER_SLEEP_MIN_PCT,false);
    s.dev->Lcd.setBrightness(uint8_t(sys_get_brightness()*255/100));
    power_reset_sleep_timer();
    if (wake == PWR_WAKE_LOWBAT) power_shutdown();
    releaseInput(); return true;
}
bool sleepRequested() {
    if (millis()-s.lastPek<200) return false;
    s.lastPek=millis();
    const bool pressed = power_consume_pek_short();
    return pressed && settings_get_sleep_mode();
}
int play() {
    const auto* info=meow_nes_info();
    releaseInput(); sidebars();
    if (!beginAv()) return MK_NES_ERROR;
    const int64_t period=1000000/info->frames_per_second;
    int64_t deadline=esp_timer_get_time();
    uint32_t lastSave=millis(),lastStats=millis(),lastShown=millis(),frames=0,shown=0,skips=0;
    uint32_t averageCoreUs=0,drawUs=13000;
    int outcome=MK_NES_ERROR;
    for (;;) {
        poll();
        const bool menu=s.touchEdge && s.touchX>=288 && s.touchY>=176;
        const bool sleep=sleepRequested();
        if (menu || sleep) { outcome=sleep?MK_NES_SLEEP_REQUESTED:MK_NES_PAUSED; break; }
        if (s.av.stats().audioErrors) { status("Audio driver stopped; retry or disable sound in pause menu"); break; }
        if (!collectSave(false)) break;
        uint8_t pad=s.held;
        if (s.touching && s.touchY<96) {
            if (s.touchX<32) pad|=MEOW_NES_SELECT;
            if (s.touchX>=288) pad|=MEOW_NES_START;
        }
        meow_nes_frame_t frame{};
        int64_t before=esp_timer_get_time();
        auto result=meow_nes_step(Input::clean(pad),&frame);
        if (result!=MEOW_NES_OK) { status(meow_nes_error_string(result)); break; }
        const uint32_t coreUs=uint32_t(esp_timer_get_time()-before);
        averageCoreUs=averageCoreUs?(averageCoreUs*7+coreUs)/8:coreUs;
        ++frames;
        if (s.sound) {
            // Copy ALL samples before LCD transfer or another core step. The
            // bounded queue blocks on I2S consumption and supplies our clock.
            size_t accepted=0;
            const uint32_t started=millis();
            while (accepted<frame.pcm_samples && millis()-started<150) {
                accepted+=s.av.submitAudio(frame.pcm+accepted,frame.pcm_samples-accepted,50);
                if (s.av.stats().audioErrors) break;
            }
            if (accepted!=frame.pcm_samples) { status("Audio queue stalled; playback paused"); break; }
        }
        const unsigned divider=(averageCoreUs+drawUs+1000>uint32_t(period))?2:1;
        // Initial/recovery prefill takes precedence over synchronous SPI work.
        // The queue's reserve excludes hardware DMA, so this is conservative.
        const uint32_t sinceLastDraw=millis()-lastShown;
        const bool audioReady=!s.sound || audioAllowsDisplay(s.av.audioPrefilling(),
            s.av.audioPendingSamples(),frame.pcm_samples,averageCoreUs,drawUs,sinceLastDraw);
        if ((frames%divider==0 || sinceLastDraw>=250) && audioReady) {
            if (!s.av.submitFrame(frame.pixels,frame.pitch,frame.palette_rgb565)) { status(s.av.error()); break; }
            drawUs=s.av.stats().lastFrameUs; lastShown=millis(); ++shown;
        } else ++skips;
        if (millis()-lastSave>=30000) { lastSave=millis(); if (!autoSave()) break; }
        if (millis()-lastStats>=5000) {
            auto stats=s.av.stats();
            Serial.printf("[NES perf] elapsed=%lu emu=%lu display=%lu skip=%lu core_us=%lu lcd_us=%lu audio_under=%lu queue_timeout=%lu queued=%lu written=%lu pending=%u\n",
                (unsigned long)(millis()-lastStats),(unsigned long)frames,(unsigned long)shown,(unsigned long)skips,
                (unsigned long)averageCoreUs,(unsigned long)drawUs,(unsigned long)stats.audioUnderruns,
                (unsigned long)stats.audioQueueTimeouts,(unsigned long)stats.audioSamplesQueued,
                (unsigned long)stats.audioSamplesWritten,(unsigned)s.av.audioPendingSamples());
            frames=shown=skips=0; lastStats=millis();
        }
        if (!s.sound) {
            deadline+=period;
            const int64_t left=deadline-esp_timer_get_time();
            if (left>1000) delay(uint32_t(left/1000)); else delay(1);
            if (esp_timer_get_time()-deadline>period*4) deadline=esp_timer_get_time();
        }
    }
    stopAv();
    // Preserve the playback error if a successful save would overwrite status.
    char error[sizeof(s.message)]; memcpy(error,s.message,sizeof(error));
    if (!saveRam()) return MK_NES_ERROR;
    if (outcome<0) memcpy(s.message,error,sizeof(error));
    return outcome;
}
void clearCatalog() {
    heap_caps_free(s.catalog); s.catalog = nullptr;
    s.catalogCount = s.catalogSkipped = 0;
    s.catalogTruncated = s.catalogUnavailable = s.catalogOom = false;
    snprintf(s.catalogDir,sizeof(s.catalogDir),"/roms/nes");
}
void scanFiles() {
    constexpr size_t Capacity = 512, ScanLimit = 4096;
    s.catalogCount = s.catalogSkipped = 0;
    s.catalogTruncated = s.catalogUnavailable = s.catalogOom = false;
    if (!s.catalog) {
        s.catalog = static_cast<Session::CatalogEntry*>(heap_caps_malloc(
            Capacity*sizeof(Session::CatalogEntry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!s.catalog) { s.catalogOom = true; return; }
    }
    // Validate again at the SD boundary, including calls from an ELF app.
    if (!validPath(s.catalogDir,false)) {
        s.catalogUnavailable = true; return;
    }
    File dir=SD_MMC.open(s.catalogDir);
    if (!dir || !dir.isDirectory()) { s.catalogUnavailable = true; return; }
    size_t inspected = 0;
    for (File file=dir.openNextFile();file;file=dir.openNextFile()) {
        if (++inspected>ScanLimit) { s.catalogTruncated=true; file.close(); break; }
        const char* fullName=file.name();
        const char* leaf=fullName ? strrchr(fullName,'/') : nullptr;
        leaf=leaf ? leaf+1 : fullName;
        const bool directory=file.isDirectory();
        const size_t length=leaf ? strlen(leaf) : 0;
        bool valid=length>0 && length<sizeof(Session::CatalogEntry::name) &&
            strcmp(leaf,".") && strcmp(leaf,"..");
        for(size_t i=0;valid && i<length;++i)
            if (static_cast<unsigned char>(leaf[i])<32 || leaf[i]=='/' || leaf[i]=='\\' || leaf[i]==':') valid=false;
        if(valid && strlen(s.catalogDir)+1+length>=sizeof(s.catalogDir)) valid=false;
        bool nes=valid && length>=4 && leaf[length-4]=='.' &&
            (leaf[length-3]=='n'||leaf[length-3]=='N') &&
            (leaf[length-2]=='e'||leaf[length-2]=='E') &&
            (leaf[length-1]=='s'||leaf[length-1]=='S');
        if (!valid) ++s.catalogSkipped;
        else if(directory || nes) {
            if(s.catalogCount==Capacity) { s.catalogTruncated=true; file.close(); break; }
            auto& entry=s.catalog[s.catalogCount++];
            memcpy(entry.name,leaf,length+1);
            entry.directory=directory;
            entry.bytes=static_cast<uint32_t>(file.size());
        }
        file.close();
        if((inspected&15u)==0) { servicePower(); delay(1); }
    }
    dir.close();
    std::sort(s.catalog,s.catalog+s.catalogCount,[](const Session::CatalogEntry& a,const Session::CatalogEntry& b) {
        if(a.directory!=b.directory) return a.directory;
        return strcmp(a.name,b.name)<0;
    });
}
} // namespace

void nes_service_attach(DEVICES* device) { if (!s.active) s.dev=device; }
bool nes_service_active() { return s.active; }
void nes_service_prepare_shutdown() {
    if (s.active) { stopAv(); saveRam(); }
}
extern "C" {
uint32_t mk_nes_version() { return MK_NES_ABI_VERSION; }
int mk_nes_begin() {
    if (s.active) return MK_NES_BUSY;
    if (!s.dev) { status("NES device unavailable"); return MK_NES_ERROR; }
    s.active=true; s.sound=true; s.lastPower=s.lastPek=millis();
    s.message[0]=0; s.input.reset();
    s.dev->Lcd.waitDMA();
    mk_media_end(); mk_tracker_stop(); s.dev->mic.end(); s.dev->speaker.end();
    s.dev->io_exp.digitalWrite(HAL_IOEXP_PA_EN,LOW);
    if (usb_msc_is_active()) { status("Eject USB storage before opening NES"); return MK_NES_ERROR; }
    if (SD_MMC.cardType()==CARD_NONE) { status("SD card missing; ROMs belong in /roms/nes"); return MK_NES_ERROR; }
    if (!psramFound()) { status("NES requires external PSRAM"); return MK_NES_ERROR; }
    const uint32_t started=millis();
    while (ui_wifi_bridge_scan_busy() && millis()-started<6500) { poll(); delay(20); }
    if (ui_wifi_bridge_scan_busy()) { status("Wait for the WiFi scan to finish, then reopen NES"); return MK_NES_ERROR; }
    const bool radio=WiFi.getMode()!=WIFI_OFF;
    s.wifiReconnect=WiFi.isConnected() || ui_wifi_bridge_get_status()==WIFI_BRIDGE_CONNECTING;
    if (radio && esp_wifi_stop()!=ESP_OK) { status("Could not pause WiFi; retry NES"); return MK_NES_ERROR; }
    s.wifiPaused=radio;
    return MK_NES_OK;
}
void mk_nes_end() {
    if (!s.active || s.playing) return;
    stopAv(); saveRam(); closeRom(); clearCatalog();
    if (s.wifiPaused) {
        if (esp_wifi_start()!=ESP_OK) status("WiFi restore failed; re-enable WiFi in Settings");
        else if (s.wifiReconnect) esp_wifi_connect();
    }
    s.wifiPaused=s.wifiReconnect=false;
    releaseInput();
    s.dev->button.update(); s.dev->button.tick();
    s.dev->button.A.hasChanged(); s.dev->button.B.hasChanged();
    s.dev->button.Up.hasChanged(); s.dev->button.Down.hasChanged();
    s.dev->button.Left.hasChanged(); s.dev->button.Right.hasChanged();
    mk_events_flush(); power_reset_sleep_timer(); s.active=false;
}
int mk_nes_catalog_open(const char* path) {
    if (!s.active || s.opened || s.playing) return MK_NES_BUSY;
    if (!validPath(path,false)) { status("Invalid ROM folder path"); return MK_NES_INVALID; }
    snprintf(s.catalogDir,sizeof(s.catalogDir),"%s",path); scanFiles();
    if (s.catalogOom) { status("Not enough PSRAM for ROM catalog"); return MK_NES_ERROR; }
    if (s.catalogUnavailable) { status("Folder unavailable or SD removed"); return MK_NES_ERROR; }
    return int(s.catalogCount);
}
void mk_nes_catalog_info(mk_nes_catalog_t* out) {
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!s.active) return;
    out->count=static_cast<uint32_t>(s.catalogCount);
    out->flags=(s.catalogTruncated?MK_NES_LIST_TRUNCATED:0) | (s.catalogSkipped?MK_NES_LIST_SKIPPED:0);
    snprintf(out->path,sizeof(out->path),"%s",s.catalogDir);
}
int mk_nes_catalog_entry(uint32_t index,mk_nes_entry_t* out) {
    if (!out) return MK_NES_INVALID;
    memset(out,0,sizeof(*out));
    if (!s.active || s.playing || s.opened || index>=s.catalogCount) return MK_NES_INVALID;
    const auto& item=s.catalog[index];
    snprintf(out->name,sizeof(out->name),"%s",item.name); out->bytes=item.bytes;
    if (item.directory) { out->flags=MK_NES_DIRECTORY; return MK_NES_OK; }
    char path[MK_NES_PATH_BYTES]; snprintf(path,sizeof(path),"%s/%s",s.catalogDir,item.name);
    File file=SD_MMC.open(path,FILE_READ);
    uint8_t header[16]; RomInfo preview;
    const size_t got=file && !file.isDirectory()?file.read(header,sizeof(header)):0;
    const auto result=inspectRomHeader(header,got,file?file.size():item.bytes,preview);
    file.close();
    if (result==RomResult::Ok) {
        out->flags=MK_NES_HEADER_VALID | (meow_nes_mapper_supported(preview.mapper)?MK_NES_SUPPORTED:0) |
            (preview.battery?MK_NES_BATTERY:0) |
            ((preview.mapper==5 || preview.mapper==23 || preview.mapper==24)?MK_NES_EXPERIMENTAL:0);
        out->mapper=preview.mapper; out->prg_bytes=static_cast<uint32_t>(preview.prgBytes);
        out->chr_bytes=static_cast<uint32_t>(preview.chrBytes);
        out->region=preview.region==Region::Pal?50:60;
    }
    return MK_NES_OK;
}
int mk_nes_open(const char* path) {
    if (!s.active || s.opened || s.playing) return MK_NES_BUSY;
    if (!validPath(path,true)) { status("Invalid .nes path"); return MK_NES_INVALID; }
    if (usb_msc_is_active()) { status("SD card in USB storage mode"); return MK_NES_BUSY; }
    if (!openRom(path)) { closeRom(); return MK_NES_ERROR; }
    snprintf(s.loadedPath,sizeof(s.loadedPath),"%s",path);
    return MK_NES_OK;
}
int mk_nes_run() {
    if (!s.active || !s.opened || s.playing) return MK_NES_BUSY;
    s.playing=true; int result=play(); s.playing=false;
    return result;
}
int mk_nes_command(int command,int value) {
    if (!s.active || s.playing) return MK_NES_BUSY;
    if (command==MK_NES_SLEEP) return sleepGame()?MK_NES_OK:MK_NES_ERROR;
    if (command==MK_NES_SOUND) { s.sound=value!=0; return MK_NES_OK; }
    if (!s.opened) { status("No ROM loaded"); return MK_NES_INVALID; }
    if (command==MK_NES_SAVE) {
        if (!s.saveBytes) status("This cartridge has no battery SRAM");
        return saveRam()?MK_NES_OK:MK_NES_ERROR;
    }
    if (command==MK_NES_RESET) {
        if (!saveRam()) return MK_NES_ERROR;
        const auto result=meow_nes_reset(false);
        if (result!=MEOW_NES_OK) { status(meow_nes_error_string(result)); return MK_NES_ERROR; }
        return MK_NES_OK;
    }
    if (command==MK_NES_CLOSE) {
        if (!saveRam()) return MK_NES_ERROR;
        closeRom(); return MK_NES_OK;
    }
    status("Unknown NES command"); return MK_NES_INVALID;
}
void mk_nes_state(mk_nes_state_t* out) {
    if (!out) return;
    memset(out,0,sizeof(*out)); out->sound=s.sound;
    if (!s.opened) return;
    const auto* info=meow_nes_info(); out->loaded=1;
    out->mapper=info->mapper; out->region=info->region==MEOW_NES_PAL?50:60;
    out->fps=info->frames_per_second; out->sram_bytes=static_cast<uint32_t>(info->sram_bytes);
    out->flags=MK_NES_HEADER_VALID | MK_NES_SUPPORTED | (info->battery?MK_NES_BATTERY:0) |
        (info->experimental_mapper?MK_NES_EXPERIMENTAL:0);
    snprintf(out->rom,sizeof(out->rom),"%s",s.loadedPath);
}
void mk_nes_error(char* out,uint32_t bytes) {
    if (out && bytes) snprintf(out,bytes,"%s",s.message);
}
void mk_nes_poll(mk_nes_input_t* out) {
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!s.active || s.playing) return;
    poll(); out->held=s.held; out->pressed=s.edge;
    out->touching=s.touching; out->touch_pressed=s.touchEdge;
    out->touch_x=s.touchX; out->touch_y=s.touchY; out->sleep_requested=sleepRequested();
}
void mk_nes_wait_release() { if (s.active && !s.playing) releaseInput(); }
} // extern C
