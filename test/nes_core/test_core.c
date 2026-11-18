#include "meow_nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Internal controller port contract: sampled hold state vs serialized latch. */
extern void input_write(uint32_t address, uint8_t value);
extern uint8_t input_read(uint32_t address);
extern void input_update(int port, int state);
extern uint32_t mem_getword(uint32_t address);
extern uint8_t mem_getbyte(uint32_t address);
extern void mem_putbyte(uint32_t address, uint8_t value);
extern uint8_t *ppu_getpage(uint32_t page_num);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static int calls, fail_at, live;
static void *allocate(size_t n, void *unused) {
    (void)unused;
    if (calls++ == fail_at) return NULL;
    void *p = malloc(n); if(p) live++; return p;
}
static void release(void *p, void *unused) { (void)unused; if(p) live--; free(p); }
static void synthetic(uint8_t *r) {
    memset(r,0,16+16384+8192); memcpy(r,"NES\x1a",4); r[4]=1; r[5]=1;
    /* SEI; LDX #$FF; TXS; JMP $8004. NMI/reset/IRQ all point to entry. */
    const uint8_t code[]={0x78,0xa2,0xff,0x9a,0x4c,0x04,0x80};
    memcpy(r+16,code,sizeof(code));
    for (int i=0x3ffa;i<0x4000;i+=2) {r[16+i]=0;r[16+i+1]=0x80;}
}
int main(void) {
    uint8_t rom[16+16384+8192]; synthetic(rom);
    CHECK(meow_nes_crc32(0,"123456789",9)==0xcbf43926u);
    CHECK(meow_nes_open(rom,15,NULL)==MEOW_NES_BAD_ROM);
    CHECK(meow_nes_open(rom,sizeof(rom)-1,NULL)==MEOW_NES_BAD_ROM);
    rom[7]=8; CHECK(meow_nes_open(rom,sizeof(rom),NULL)==MEOW_NES_UNSUPPORTED); rom[7]=0;
    rom[6]=0xf0; rom[7]=0xf0; CHECK(meow_nes_open(rom,sizeof(rom),NULL)==MEOW_NES_UNSUPPORTED);
    synthetic(rom);
    meow_nes_options_t opts={0}; opts.alloc=allocate; opts.free=release;
    for (int fail=0;fail<8;fail++) {
        calls=live=0; fail_at=fail;
        meow_nes_result_t e=meow_nes_open(rom,sizeof(rom),&opts);
        CHECK(e==MEOW_NES_OK || e==MEOW_NES_NO_MEMORY);
        meow_nes_close(); meow_nes_close(); CHECK(live==0);
    }
    fail_at=-1; calls=live=0;
    for (int cycle=0;cycle<20;cycle++) {
        CHECK(meow_nes_open(rom,sizeof(rom),&opts)==MEOW_NES_OK);
        CHECK(meow_nes_open(rom,sizeof(rom),NULL)==MEOW_NES_BUSY);
        input_update(0,MEOW_NES_A|MEOW_NES_B|MEOW_NES_RIGHT);
        input_write(0x4016,1);
        for(int i=0;i<12;i++) CHECK((input_read(0x4016)&1)==1);
        input_write(0x4016,0);
        input_update(0,0); /* Must not alter a previously latched report. */
        for(int i=0;i<8;i++) CHECK((input_read(0x4016)&1)==((0x83>>i)&1));
        for(int i=0;i<48;i++) CHECK((input_read(0x4016)&1)==1);
        meow_nes_frame_t frame;
        for(int i=0;i<5;i++) CHECK(meow_nes_step(MEOW_NES_A|MEOW_NES_B|MEOW_NES_RIGHT,&frame)==MEOW_NES_OK);
        CHECK(frame.width==256 && frame.height==240 && frame.pcm_samples==735);
        CHECK(meow_nes_reset(false)==MEOW_NES_OK);
        CHECK(meow_nes_step(0,&frame)==MEOW_NES_OK);
        meow_nes_close(); CHECK(live==0);
    }
    rom[6]=2; rom[8]=8; /* Battery backed 64 KiB header contract. */
    CHECK(meow_nes_open(rom,sizeof(rom),&opts)==MEOW_NES_OK);
    CHECK(meow_nes_sram_size()==65536);
    uint8_t *ram=malloc(65536); CHECK(ram); memset(ram,0xa5,65536);
    CHECK(meow_nes_sram_write(ram,65536)==MEOW_NES_OK);
    CHECK(meow_nes_reset(true)==MEOW_NES_OK);
    memset(ram,0,65536); CHECK(meow_nes_sram_read(ram,65536)==MEOW_NES_OK);
    for(int i=0;i<65536;i++) CHECK(ram[i]==0xa5);
    free(ram); meow_nes_close(); CHECK(live==0);
    /* Real CPU addressing tests: zero-page pointer $FF and operand $FFFF. */
    synthetic(rom);
    const uint8_t addressing[]={
        0x78,0xa0,0x00,0xa2,0x00, /* SEI; LDY #0; LDX #0 */
        0xb1,0xff,0x8d,0x00,0x03, /* LDA ($FF),Y; STA $0300 */
        0xa1,0xff,0x8d,0x01,0x03, /* LDA ($FF,X); STA $0301 */
        0x4c,0xfe,0xff            /* JMP $FFFE */
    };
    memcpy(rom+16,addressing,sizeof(addressing));
    rom[16+0x3ffe]=0xad; rom[16+0x3fff]=0; /* LDA $0200 across end of bus */
    CHECK(meow_nes_open(rom,sizeof(rom),&opts)==MEOW_NES_OK);
    mem_putbyte(0x00ff,0); mem_putbyte(0x0000,2); mem_putbyte(0x0100,4);
    mem_putbyte(0x0200,0xaa); mem_putbyte(0x0400,0xbb);
    const uint8_t tail[]={0x8d,0x02,0x03,0x4c,0x04,0x00}; /* STA $0302; JMP $0004 */
    for(size_t i=0;i<sizeof(tail);i++) mem_putbyte(1+(uint32_t)i,tail[i]);
    CHECK(mem_getword(0xffff)==0x0200);
    meow_nes_frame_t boundary_frame;
    CHECK(meow_nes_step(0,&boundary_frame)==MEOW_NES_OK);
    CHECK(mem_getbyte(0x0300)==0xaa);
    CHECK(mem_getbyte(0x0301)==0xaa);
    CHECK(mem_getbyte(0x0302)==0xaa);
    meow_nes_close(); CHECK(live==0);
    /* VRC2 half-registers must match initial CHR banks after reopen/reset. */
    const size_t vrc_size=16+16384+32768;
    uint8_t *vrc=malloc(vrc_size); CHECK(vrc); synthetic(vrc);
    vrc[5]=4; vrc[6]=0x70; vrc[7]=0x10;
    for(int bank=0;bank<32;bank++) memset(vrc+16+16384+bank*1024,bank,1024);
    for(int cycle=0;cycle<2;cycle++) {
        CHECK(meow_nes_open(vrc,vrc_size,&opts)==MEOW_NES_OK);
        mem_putbyte(0xb000,2); CHECK(ppu_getpage(0)[0]==2);
        mem_putbyte(0xb001,1); CHECK(ppu_getpage(0)[0]==18);
        CHECK(meow_nes_reset(false)==MEOW_NES_OK);
        mem_putbyte(0xb000,3); CHECK(ppu_getpage(0)[0]==3);
        mem_putbyte(0xb001,1); CHECK(ppu_getpage(0)[0]==19);
        meow_nes_close(); CHECK(live==0);
    }
    free(vrc);
    CHECK(meow_nes_step(0,NULL)==MEOW_NES_NOT_OPEN);
    synthetic(rom); memcpy(rom+7,"DiskDude!",9);
    CHECK(meow_nes_open(rom,sizeof(rom),NULL)==MEOW_NES_OK);
    CHECK(meow_nes_info()->mapper==0); meow_nes_close();
    puts("PASS parser/CRC/real CPU stepping/address wrap/OOM lifecycle/20 sessions/64KiB SRAM/reset/legacy header");
    return 0;
}
