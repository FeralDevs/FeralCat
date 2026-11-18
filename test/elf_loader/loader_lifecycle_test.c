/* Real esp_elf.c + host allocator/MMU doubles. No Xtensa code is executed. */
#include "esp_elf.h"
#include "private/elf_platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void* allocations[4];
static int attempts, fail_at, live, double_frees, mmu_fail;

void* esp_elf_malloc(uint32_t bytes, bool executable)
{
    void* ptr;
    (void)executable;
    if (++attempts == fail_at) return NULL;
    ptr = malloc(bytes); CHECK(ptr != NULL);
    CHECK(live < 4); allocations[live++] = ptr; return ptr;
}
void esp_elf_free(void* ptr)
{
    int i;
    if (!ptr) return;
    for (i=0; i<live; ++i) {
        if (allocations[i] == ptr) {
            free(ptr); allocations[i] = allocations[--live]; return;
        }
    }
    ++double_frees; /* Detect deterministically without invoking allocator UB. */
}
int esp_elf_arch_init_mmu(esp_elf_t* elf) { (void)elf; return mmu_fail; }
void esp_elf_arch_deinit_mmu(esp_elf_t* elf) { (void)elf; }
uintptr_t elf_find_sym_default(const char* name) { (void)name; return 0; }
int esp_elf_arch_relocate(esp_elf_t* elf, const elf32_rela_t* rela, const elf32_sym_t* sym, uint32_t addr)
{
    (void)elf; (void)rela; (void)sym; (void)addr; return 0;
}
#ifdef _WIN32
/* File loading is outside this test; provide its unused POSIX linker shim. */
int asprintf(char** out, const char* format, ...) { (void)format; *out=NULL; return -1; }
#endif

typedef struct {
    elf32_hdr_t header;
    elf32_shdr_t sections[4];
    char names[32];
    uint8_t text[4], data[4];
} Image;

static void image_init(Image* image)
{
    elf32_shdr_t* section;
    memset(image, 0, sizeof(*image));
    memcpy(image->header.ident, "\177ELF\1\1\1", 7);
    image->header.shoff = offsetof(Image, sections);
    image->header.shnum = 4; image->header.shstrndx = 3;
    image->header.entry = 0x1000;
    memcpy(image->names, "\0.text\0.data\0.shstrtab\0", 22);
    section = &image->sections[1];
    section->name=1; section->type=SHT_PROGBITS; section->flags=SHF_ALLOC|SHF_EXECINSTR;
    section->addr=0x1000; section->offset=offsetof(Image,text); section->size=4;
    section = &image->sections[2];
    section->name=7; section->type=SHT_PROGBITS; section->flags=SHF_ALLOC|SHF_WRITE;
    section->addr=0x2000; section->offset=offsetof(Image,data); section->size=4;
    section = &image->sections[3];
    section->name=13; section->type=SHT_STRTAB; section->offset=offsetof(Image,names); section->size=32;
    image->text[0]=0x12; image->data[0]=0x34;
}

static void exercise(Image* image, int fail, int mmu, int expected)
{
    esp_elf_t elf;
    attempts=live=double_frees=0; fail_at=fail; mmu_fail=mmu;
    CHECK(esp_elf_init(&elf)==0);
    CHECK(esp_elf_relocate(&elf,(const uint8_t*)image)==expected);
    if (expected) {
        CHECK(elf.ptext==NULL && elf.pdata==NULL);
        CHECK(live==0);
    } else {
        CHECK(live==2 && elf.ptext && elf.pdata);
        CHECK(elf.ptext[0]==0x12 && elf.pdata[0]==0x34);
    }
    /* This is the runner's failure contract, followed by an idempotence check. */
    esp_elf_deinit(&elf); esp_elf_deinit(&elf);
    CHECK(double_frees==0 && live==0);
    CHECK(elf.ptext==NULL && elf.pdata==NULL && elf.entry==NULL);
}

int main(void)
{
    Image image; image_init(&image);
    exercise(&image,1,0,-ENOMEM); // executable allocation fails
    exercise(&image,2,0,-ENOMEM); // data allocation fails after executable allocation
    exercise(&image,0,1,-EIO);    // MMU setup fails after both allocations
    exercise(&image,0,0,0);      // successful relocate and repeated deinit
    puts("ELF loader: text/data OOM, MMU failure, success and repeated cleanup passed");
    return 0;
}
