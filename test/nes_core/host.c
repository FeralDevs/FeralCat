#include "meow_nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
static void log_message(int level,const char *text,void *unused) { (void)unused; if(level<4) fputs(text,stderr); }
static void put32(FILE *f,uint32_t x) {for(int i=0;i<4;i++) fputc((x>>(i*8))&255,f);}
static void put16(FILE *f,uint16_t x) {fputc(x&255,f);fputc(x>>8,f);}
static uint8_t input_for_frame(int i,bool scripted) {
    uint8_t pad=0;
    if(scripted) {
        if(i==180||i==181||i==420||i==421||i==720||i==721)pad=MEOW_NES_START;
        if(i>850){pad|=MEOW_NES_RIGHT|MEOW_NES_B;if((i%90)<20)pad|=MEOW_NES_A;}
    }
    return pad;
}
static uint32_t video_crc(const meow_nes_frame_t *f) {
    uint32_t crc=0;for(int y=0;y<240;y++)crc=meow_nes_crc32(crc,f->pixels+y*f->pitch,256);
    return crc;
}
static int snapshot(const char *dir,const meow_nes_frame_t *f) {
    char name[1024]; snprintf(name,sizeof(name),"%s/frame-%06" PRIu64 ".bmp",dir,f->frame_number);
    FILE *out=fopen(name,"wb"); if(!out)return -1;
    fputs("BM",out); put32(out,54+256*240*3);put32(out,0);put32(out,54);put32(out,40);
    put32(out,256);put32(out,240);put16(out,1);put16(out,24);put32(out,0);put32(out,256*240*3);
    put32(out,0);put32(out,0);put32(out,0);put32(out,0);
    for(int y=239;y>=0;y--) for(int x=0;x<256;x++) {
        uint16_t c=f->palette_rgb565[f->pixels[y*f->pitch+x]];
        fputc((c&31)*255/31,out);fputc(((c>>5)&63)*255/63,out);fputc((c>>11)*255/31,out);
    }
    return fclose(out);
}
int main(int argc,char **argv) {
    if(argc<3){fprintf(stderr,"Usage: nes_core_host ROM OUTPUT_DIRECTORY [frames=1800] [script=play|idle] [sessions=1]\n");return 2;}
    int frames=argc>3?atoi(argv[3]):1800; if(frames<1||frames>36000)return 2;
    bool scripted=argc<5||strcmp(argv[4],"idle");
    int sessions=argc>5?atoi(argv[5]):1; if(sessions<1||sessions>20)return 2;
    FILE *in=fopen(argv[1],"rb");if(!in)return 2;
    fseek(in,0,SEEK_END);long n=ftell(in);rewind(in);
    if(n<16||n>MEOW_NES_MAX_ROM_BYTES){fclose(in);return 2;}
    uint8_t *rom=malloc(n);if(!rom||fread(rom,1,n,in)!=(size_t)n){fclose(in);free(rom);return 2;}fclose(in);
    uint32_t original=meow_nes_crc32(0,rom,n);
    meow_nes_options_t opts={0};opts.log=log_message;
    meow_nes_result_t result=meow_nes_open(rom,n,&opts);
    if(result){fprintf(stderr,"Open: %s\n",meow_nes_error_string(result));free(rom);return 1;}
    meow_nes_info_t info=*meow_nes_info();
    char path[1024];snprintf(path,sizeof(path),"%s/audio.wav",argv[2]);FILE *wav=fopen(path,"wb");
    if(!wav){meow_nes_close();free(rom);return 2;}
    /* Reserve PCM WAV header and finalize after stepping. */
    for(int i=0;i<44;i++)fputc(0,wav);
    uint64_t samples=0, nonzero=0, changes=0;uint32_t prev=0,video_digest=0,audio_digest=0;int peak=0;
    clock_t begin=clock();
    for(int i=0;i<frames;i++) {
        uint8_t pad=input_for_frame(i,scripted);
        meow_nes_frame_t f;
        if(meow_nes_step(pad,&f)){fclose(wav);meow_nes_close();free(rom);return 1;}
        uint32_t crc=video_crc(&f);
        video_digest=meow_nes_crc32(video_digest,&crc,sizeof(crc));
        audio_digest=meow_nes_crc32(audio_digest,f.pcm,f.pcm_samples*sizeof(int16_t));
        if(i&&crc!=prev)changes++;prev=crc;
        for(size_t k=0;k<f.pcm_samples;k++){int v=f.pcm[k];if(v)nonzero++;if(v<0)v=-v;if(v>peak)peak=v;}
        fwrite(f.pcm,sizeof(int16_t),f.pcm_samples,wav);samples+=f.pcm_samples;
        if((i+1)%120==0||i==frames-1)if(snapshot(argv[2],&f)){fclose(wav);meow_nes_close();free(rom);return 2;}
    }
    double seconds=(double)(clock()-begin)/CLOCKS_PER_SEC;
    rewind(wav);fputs("RIFF",wav);put32(wav,(uint32_t)(36+samples*2));fputs("WAVEfmt ",wav);
    put32(wav,16);put16(wav,1);put16(wav,1);put32(wav,info.sample_rate);put32(wav,info.sample_rate*2);
    put16(wav,2);put16(wav,16);fputs("data",wav);put32(wav,(uint32_t)(samples*2));fclose(wav);
    meow_nes_close();
    bool repeat_matches=true;
    for(int session=1;session<sessions;session++) {
        if(meow_nes_open(rom,n,&opts)){free(rom);return 1;}
        uint32_t video_repeat=0,audio_repeat=0;
        for(int i=0;i<frames;i++) {
            meow_nes_frame_t f;
            if(meow_nes_step(input_for_frame(i,scripted),&f)){meow_nes_close();free(rom);return 1;}
            uint32_t crc=video_crc(&f);
            video_repeat=meow_nes_crc32(video_repeat,&crc,sizeof(crc));
            audio_repeat=meow_nes_crc32(audio_repeat,f.pcm,f.pcm_samples*sizeof(int16_t));
        }
        if(video_repeat!=video_digest||audio_repeat!=audio_digest) {
            fprintf(stderr,"Session %d diverged: video %08x/%08x audio %08x/%08x\n",session+1,video_repeat,video_digest,audio_repeat,audio_digest);
            repeat_matches=false;
        }
        meow_nes_close();
    }
    bool unchanged=original==meow_nes_crc32(0,rom,n);free(rom);
    printf("{\"mapper\":%u,\"frames\":%d,\"fps_target\":%u,\"host_seconds_with_io\":%.6f,\"changing_frames\":%" PRIu64 ",\"last_video_crc\":\"%08x\",\"pcm_samples\":%" PRIu64 ",\"pcm_nonzero\":%" PRIu64 ",\"pcm_peak\":%d,\"sessions\":%d,\"repeat_matches\":%s,\"rom_unchanged\":%s}\n",info.mapper,frames,info.frames_per_second,seconds,changes,prev,samples,nonzero,peak,sessions,repeat_matches?"true":"false",unchanged?"true":"false");
    return unchanged&&repeat_matches?0:1;
}
