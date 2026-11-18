#include "meow_nes.h"
#include "meow_nes_port.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vendor/nes/nes.h"
#include "vendor/palettes.h"
#undef malloc
#undef calloc
#undef free
#undef strdup

static meow_nes_options_t port;
static meow_nes_info_t info;
static uint8_t *video;
static uint16_t palette[256];
static bool core_started, opened;
static uint64_t frame_no;

void *meow_core_alloc(size_t bytes) {
    if (!bytes) bytes = 1;
    return port.alloc ? port.alloc(bytes, port.user) : malloc(bytes);
}
void *meow_core_calloc(size_t count, size_t bytes) {
    if (bytes && count > SIZE_MAX / bytes) return NULL;
    size_t n = count * bytes;
    void *p = meow_core_alloc(n);
    if (p) memset(p, 0, n);
    return p;
}
void meow_core_free(void *p) {
    if (!p) return;
    if (port.free) port.free(p, port.user); else free(p);
}
char *meow_core_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = meow_core_alloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
void meow_core_log(int level, const char *fmt, ...) {
    if (!port.log) return;
    char text[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    port.log(level, text, port.user);
}
uint32_t meow_nes_crc32(uint32_t seed, const void *data, size_t bytes) {
    const uint8_t *p = data;
    uint32_t c = ~seed;
    while (bytes--) {
        c ^= *p++;
        for (unsigned bit=0; bit<8; ++bit)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}
bool meow_nes_mapper_supported(uint16_t m) {
    switch (m) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 7:
        case 9: case 10: case 11: case 23: case 24: case 66: return true;
        default: return false;
    }
}
static meow_nes_result_t validate(const uint8_t *rom, size_t bytes, size_t *used) {
    if (!rom || bytes < 16 || bytes > MEOW_NES_MAX_ROM_BYTES || memcmp(rom,"NES\x1a",4))
        return MEOW_NES_BAD_ROM;
    if ((rom[7] & 0x0c) == 0x08) return MEOW_NES_UNSUPPORTED; /* NES 2.0 */
    bool diskdude = memcmp(rom + 7, "DiskDude!", 9) == 0;
    if (!diskdude && (rom[7] & 0x0f)) return MEOW_NES_UNSUPPORTED; /* VS/PlayChoice */
    if (!diskdude && (rom[12] || rom[13] || rom[14] || rom[15])) return MEOW_NES_BAD_ROM;
    if (!rom[4]) return MEOW_NES_BAD_ROM;
    if (!diskdude && rom[8] > 8) return MEOW_NES_UNSUPPORTED;
    uint16_t mapper = (rom[6] >> 4) | (diskdude ? 0 : (rom[7] & 0xf0));
    if (!meow_nes_mapper_supported(mapper)) return MEOW_NES_UNSUPPORTED;
    size_t size = 16u + ((rom[6] & 4) ? 512u : 0u) + (size_t)rom[4]*16384u + (size_t)rom[5]*8192u;
    if (size > bytes) return MEOW_NES_BAD_ROM;
    *used = size; /* Trailing bytes are not passed to loader or CRC database. */
    return MEOW_NES_OK;
}
void meow_nes_close(void) {
    if (core_started) nes_shutdown();
    core_started = opened = false;
    meow_core_free(video);
    video = NULL;
    frame_no = 0;
    memset(&info, 0, sizeof(info));
    memset(&port, 0, sizeof(port));
}
meow_nes_result_t meow_nes_open(const uint8_t *rom, size_t bytes, const meow_nes_options_t *options) {
    if (opened || core_started) return MEOW_NES_BUSY;
    meow_nes_options_t chosen = {0};
    if (options) chosen = *options;
    if (!chosen.sample_rate) chosen.sample_rate = 44100;
    if ((chosen.sample_rate != 44100 && chosen.sample_rate != 48000)
        || chosen.region < MEOW_NES_AUTO || chosen.region > MEOW_NES_PAL
        || (!!chosen.alloc != !!chosen.free)) return MEOW_NES_BAD_ARGUMENT;
    size_t used;
    meow_nes_result_t result = validate(rom, bytes, &used);
    if (result != MEOW_NES_OK) return result;
    port = chosen;
    video = meow_core_calloc(NES_SCREEN_PITCH * NES_SCREEN_HEIGHT, 1);
    if (!video) { meow_nes_close(); return MEOW_NES_NO_MEMORY; }
    nes_type_t region = chosen.region == MEOW_NES_PAL ? SYS_NES_PAL :
        (chosen.region == MEOW_NES_NTSC ? SYS_NES_NTSC : SYS_DETECT);
    core_started = true;
    nes_t *n = nes_init(region, (int)chosen.sample_rate, false, NULL);
    if (!n) { meow_nes_close(); return MEOW_NES_NO_MEMORY; }
    rom_t *cart = rom_loadmem((uint8_t *)rom, used);
    if (!cart) { meow_nes_close(); return MEOW_NES_NO_MEMORY; }
    if (!meow_nes_mapper_supported(cart->mapper_number)) {
        meow_nes_close(); return MEOW_NES_UNSUPPORTED;
    }
    if (cart->system == SYS_UNKNOWN && memcmp(rom+7,"DiskDude!",9) && (rom[9]&1))
        cart->system = SYS_NES_PAL;
    if (nes_insertcart(cart) != 0) { meow_nes_close(); return MEOW_NES_UNSUPPORTED; }
    nes_setvidbuf(video);
    input_connect(0, NES_JOYPAD);
    input_connect(1, NES_NOTHING);
    /* Replicate priority-tagged colors, and define every palette entry. */
    for (unsigned i=0; i<256; ++i) {
        const uint8_t *rgb = &nes_palettes[0][(i & 63u)*3];
        palette[i] = (uint16_t)(((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3));
    }
    info.mapper = cart->mapper_number;
    info.region = n->system == SYS_NES_PAL ? MEOW_NES_PAL : MEOW_NES_NTSC;
    info.sample_rate = chosen.sample_rate;
    info.frames_per_second = n->refresh_rate;
    info.battery = cart->battery;
    info.sram_bytes = cart->battery ? (size_t)cart->prg_ram_banks * ROM_PRG_BANK_SIZE : 0;
    info.rom_crc32 = cart->checksum;
    info.experimental_mapper = info.mapper == 5 || info.mapper == 24 || info.mapper == 23;
    opened = true;
    return MEOW_NES_OK;
}
meow_nes_result_t meow_nes_step(uint8_t held_pad, meow_nes_frame_t *f) {
    if (!opened) return MEOW_NES_NOT_OPEN;
    if (!f) return MEOW_NES_BAD_ARGUMENT;
    input_update(0, held_pad);
    nes_emulate(true);
    nes_t *n = nes_getptr();
    *f = (meow_nes_frame_t){video + NES_SCREEN_OVERDRAW, palette, 256, 240,
        NES_SCREEN_PITCH, n->apu->buffer, (size_t)n->apu->samples_per_frame, ++frame_no};
    return MEOW_NES_OK;
}
meow_nes_result_t meow_nes_reset(bool hard) {
    if (!opened) return MEOW_NES_NOT_OPEN;
    nes_reset(hard);
    nes_setvidbuf(video);
    memset(video, 0, NES_SCREEN_PITCH * NES_SCREEN_HEIGHT);
    frame_no = 0;
    return MEOW_NES_OK;
}
const meow_nes_info_t *meow_nes_info(void) { return opened ? &info : NULL; }
size_t meow_nes_sram_size(void) { return opened ? info.sram_bytes : 0; }
meow_nes_result_t meow_nes_sram_read(void *dst, size_t bytes) {
    if (!opened) return MEOW_NES_NOT_OPEN;
    if (!dst || !bytes || bytes != info.sram_bytes) return MEOW_NES_BAD_ARGUMENT;
    memcpy(dst, nes_getptr()->cart->prg_ram, bytes); return MEOW_NES_OK;
}
meow_nes_result_t meow_nes_sram_write(const void *src, size_t bytes) {
    if (!opened) return MEOW_NES_NOT_OPEN;
    if (!src || !bytes || bytes != info.sram_bytes) return MEOW_NES_BAD_ARGUMENT;
    memcpy(nes_getptr()->cart->prg_ram, src, bytes); return MEOW_NES_OK;
}
const char *meow_nes_error_string(meow_nes_result_t r) {
    switch(r) {
    case MEOW_NES_OK:return "OK"; case MEOW_NES_BAD_ARGUMENT:return "Invalid argument";
    case MEOW_NES_BAD_ROM:return "Invalid or truncated ROM";
    case MEOW_NES_UNSUPPORTED:return "Unsupported format or mapper";
    case MEOW_NES_NO_MEMORY:return "Insufficient memory";
    case MEOW_NES_NOT_OPEN:return "No ROM loaded";
    case MEOW_NES_BUSY:return "A ROM is already loaded"; default:return "Unknown error";
    }
}
