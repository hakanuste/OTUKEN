#ifndef OTUKENOS_ELF_H
#define OTUKENOS_ELF_H

#include <stdint.h>

/* ELF64 - Executable and Linkable Format.
 * Stage 10'da elle yazilmis ham makine kodu calistirmistik; burada
 * GERCEK bir ELF ikili dosyasini ayristirip yukluyoruz. */

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_X86_64   62
#define PT_LOAD     1

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_header_t;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

/* Kullanici alani siniri: PML4[1..255] araligi (surec-ozel bolge).
 * Cekirdek PML4[0] (kimlik haritasi) ve PML4[511] (higher-half)
 * kullanir; bir ELF'in bu bolgelere yuklenmesine ASLA izin verilmez. */
#define ELF_USER_MIN  0x0000008000000000ULL   /* 512GB = PML4[1] */
#define ELF_USER_MAX  0x0000800000000000ULL   /* kanonik alt yari siniri */
#define ELF_MAX_SEG   8                        /* islenecek azami PT_LOAD */
/* Segment basina azami sayfa. Bu bir GUVENLIK siniri degil, akil
 * saglik siniridir: asil koruma kullanici araligi denetimi ve PMM'dir.
 * UMAY'in .bss'i (bilgi deposu + US1 motoru) 256 sayfayi asti; 512
 * sayfa = 2 MB. Daha buyuk segment yine reddedilir. */
#define ELF_MAX_PAGES 512

typedef struct {
    uint64_t entry_point;
    int      segments_loaded;
    uint64_t lowest_vaddr;
    uint64_t highest_vaddr;
} elf_image_t;

/* Bellekteki bir ELF64 ikilisini dogrular. 0=gecerli, -1=gecersiz. */
int elf_validate(const uint8_t *data, uint64_t size);

/* ELF'i ayristirip PT_LOAD segmentlerini kullanici-erisilebilir
 * sayfalara yukler; sayfa izinleri segment bayraklarindan (R/W/X)
 * tureti lir - W^X politikasi burada GERCEKTEN uygulanir.
 * Basari:0, hata:-1 */
int elf_load(const uint8_t *data, uint64_t size, elf_image_t *out);

#endif
