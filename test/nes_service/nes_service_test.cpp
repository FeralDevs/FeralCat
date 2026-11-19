// Include production service to exercise its real private snapshot lifecycle
// without adding a test-only firmware API. Peripheral boundaries are doubles.
#include "../../src/system/emulation/nes_service.cpp"
#include <iostream>

namespace meow::nes {
bool audioAllowsDisplay(bool,size_t,size_t,uint32_t,uint32_t,uint32_t){return true;}
Av::~Av(){}
bool Av::begin(DEVICES*,bool){service_test::av=true;return true;}
bool Av::stop(uint32_t){service_test::av=false;return true;}
void Av::setVolume(unsigned){}
bool Av::active()const{return service_test::av;}
AvStats Av::stats()const{return {};}
bool Av::submitFrame(const uint8_t*,size_t,const uint16_t*){return true;}
size_t Av::submitAudio(const int16_t*,size_t count,uint32_t){return count;}
size_t Av::audioPendingSamples()const{return 2048;}
bool Av::audioPrefilling()const{return false;}
}
extern "C" {
bool meow_nes_mapper_supported(uint16_t mapper){return mapper==0;}
meow_nes_result_t meow_nes_open(const uint8_t*,size_t,const meow_nes_options_t*) {
    service_test::core=true; std::fill(service_test::ram.begin(),service_test::ram.end(),uint8_t(0));
    service_test::info={};service_test::info.sram_bytes=8192;service_test::info.battery=true;
    service_test::info.frames_per_second=60; return MEOW_NES_OK;
}
void meow_nes_close(){service_test::core=false;}
meow_nes_result_t meow_nes_step(uint8_t,meow_nes_frame_t*){return MEOW_NES_NOT_OPEN;}
meow_nes_result_t meow_nes_reset(bool){return MEOW_NES_OK;}
const meow_nes_info_t* meow_nes_info(){return &service_test::info;}
size_t meow_nes_sram_size(){return service_test::ram.size();}
meow_nes_result_t meow_nes_sram_read(void* out,size_t bytes) {
    service_test::check(bytes==service_test::ram.size(),"incorrect SRAM read size");
    std::memcpy(out,service_test::ram.data(),bytes);return MEOW_NES_OK;
}
meow_nes_result_t meow_nes_sram_write(const void* in,size_t bytes) {
    service_test::check(bytes==service_test::ram.size(),"incorrect SRAM write size");
    std::memcpy(service_test::ram.data(),in,bytes);return MEOW_NES_OK;
}
const char* meow_nes_error_string(meow_nes_result_t){return "Core error";}
}

using service_test::check;
static DEVICES device;
template<class Predicate> static void until(Predicate ready) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(ready(),"async save failed to make progress");
}
static void reset() {
    using namespace service_test;
    mk_nes_end();until([]{return tasks==0;});check(allocations.empty(),"service allocation leak");
    files.clear();allocAttempt=failAllocation=wifiStops=wifiStarts=0;
    holdWrites=writing=failWrites=false;failTask=msc=false;psram=true;
    std::vector<uint8_t> rom(16+16384,0);std::memcpy(rom.data(),"NES\x1a",4);rom[4]=1;rom[6]=2;
    add("/roms",true);add("/roms/nes",true);add("/roms/nes/Game.nes",false,rom);
    nes_service_attach(&device);check(mk_nes_begin()==MK_NES_OK,"service begin failed");
}
static void load(){check(mk_nes_open("/roms/nes/Game.nes")==MK_NES_OK,"ROM open failed");}
static void fill(uint8_t value){std::fill(service_test::ram.begin(),service_test::ram.end(),value);}
static void saved(const char* path,uint32_t crc,uint8_t expected) {
    auto found=service_test::files.find(path);check(found!=service_test::files.end(),"save file missing");
    meow::nes::SaveView view;
    check(meow::nes::decodeSave(crc,found->second->bytes.data(),found->second->bytes.size(),view)==meow::nes::SaveResult::Ok,"save corrupt");
    check(view.payloadBytes==8192,"save payload wrong size");
    for(size_t i=0;i<view.payloadBytes;++i)check(view.payload[i]==expected,"save mixed/stale snapshot");
}
static void asyncSnapshotAndCloseJoin() {
    using namespace service_test;
    reset();check(mk_nes_catalog_open(MK_NES_ROOT)==1,"catalog did not find ROM");
    mk_nes_entry_t entry{};check(mk_nes_catalog_entry(0,&entry)==0&&(entry.flags&MK_NES_BATTERY),"catalog/header contract");
    load();fill(0x11);check(mk_nes_command(MK_NES_SAVE,0)==0,"initial save failed");
    char base[64],temp[64],backup[64];savePaths(base,temp,backup);uint32_t crc=s.romInfo.romCrc32;
    fill(0x22);holdWrites=true;check(autoSave(),"async start failed");until([]{return writing.load();});
    fill(0x33);check(collectSave(false)&&s.savePending,"running save was collected too early");
    // Close must join the in-flight copy, then capture the *new* cartridge RAM.
    std::thread unblock([]{std::this_thread::sleep_for(std::chrono::milliseconds(25));holdWrites=false;});
    check(mk_nes_command(MK_NES_CLOSE,0)==0,"close failed after background save");unblock.join();
    until([]{return tasks==0;});saved(base,crc,0x33);saved(backup,crc,0x22);
    check(!core&&!s.saveBuffer&&!s.savePending&&!s.saveRunning,"close retained ROM/save worker");
    mk_nes_end();check(allocations.empty()&&wifiStops==1&&wifiStarts==1,"end did not restore resources/radio");
}
static void failuresAndRecovery() {
    using namespace service_test;
    reset();load();fill(0x44);failWrites=true;
    check(mk_nes_command(MK_NES_CLOSE,0)==MK_NES_ERROR,"failed close should retain ROM");
    mk_nes_state_t state{};mk_nes_state(&state);check(state.loaded&&core&&s.saveBuffer,"failed close discarded SRAM");
    failWrites=false;check(mk_nes_command(MK_NES_CLOSE,0)==0,"close retry failed");
    load();check(ram[0]==0x44,"saved SRAM did not reload");
    fill(0x55);failTask=true;check(!autoSave()&&!s.savePending&&!s.saveRunning,"task creation failure retained worker ownership");
    failTask=false;failWrites=true;check(autoSave(),"async failure start failed");
    until([]{return !s.saveRunning.load();});check(!collectSave(false),"async write failure hidden");
    failWrites=false;check(mk_nes_command(MK_NES_CLOSE,0)==0,"async failure could not retry");
    load();check(ram[0]==0x55,"retry persisted wrong SRAM");
    mk_nes_end();until([]{return tasks==0;});check(allocations.empty(),"failure cleanup leak");
}
static void allocationAndBadSave() {
    using namespace service_test;
    reset();failAllocation=2;check(mk_nes_open("/roms/nes/Game.nes")==MK_NES_ERROR,"save-buffer OOM ignored");
    check(!core&&!s.opened&&!s.rom&&!s.saveBuffer&&allocations.empty(),"partial open leaked resources");
    failAllocation=0;load();fill(0x66);check(mk_nes_command(MK_NES_CLOSE,0)==0,"save for corruption test failed");
    for(auto& pair:files)if(pair.first.find(".sav")!=std::string::npos)pair.second->bytes[0]=0;
    check(mk_nes_open("/roms/nes/Game.nes")==MK_NES_ERROR,"corrupt-only SRAM silently reset");
    check(!core&&!s.opened&&allocations.empty(),"corrupt save open leaked resources");
    mk_nes_end();
}
int main() {
    asyncSnapshotAndCloseJoin();failuresAndRecovery();allocationAndBadSave();
    std::cout<<"NES service: real ABI/catalog, async snapshot/join/rotation, save retry, OOM and corruption cleanup passed\n";
}
