#include "multiboot2.h"
#include "serial.h"

int onyukleme_protokolu(uint32_t magic) {
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC)  return 1;
    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) return 2;
    return 0;
}

/* Multiboot2 mmap girisi */
typedef struct __attribute__((packed)) {
    uint64_t taban;
    uint64_t uzunluk;
    uint32_t tur;
    uint32_t yedek;
} mb2_mmap_giris_t;

/* Cevrilen bellek haritasi buraya yazilir (multiboot1 bicimi). */
static multiboot_mmap_entry_t cevrilen_mmap[64];

/* DENETIM: multiboot1'in mmap_addr alani 32 BITTIR (spesifikasyon
 * 32-bit cekirdekler icin tasarlandi). Bizim cevrilen tamponumuz
 * higher-half'ta (0xFFFFFFFF8...) oturdugu icin o alana sigmaz ve
 * adres KIRPILIR -> PMM hic bellek goremez (UEFI'de "toplam bellek=0"
 * olarak gercekten yasandi). Bu yuzden 64-bit adresi AYRI tutuyoruz. */
static uint64_t cevrilen_mmap_adres64 = 0;
static uint32_t cevrilen_mmap_uzunluk = 0;

uint64_t multiboot2_mmap_adresi(void) { return cevrilen_mmap_adres64; }
uint32_t multiboot2_mmap_uzunlugu(void) { return cevrilen_mmap_uzunluk; }

/* RSDP kopyasi: v2 (36 bayt) en genis bicim. mbi dusuk bellekte
 * olabilir ve ileride ezilebilir; bu yuzden icerik kopyalanir. */
static uint8_t rsdp_kopya[36];
static int     rsdp_var = 0;
const uint8_t *multiboot2_rsdp(void) { return rsdp_var ? rsdp_kopya : 0; }

int multiboot2_cevir(uint64_t mb2_adres, multiboot_info_t *cikti) {
    if (mb2_adres == 0 || cikti == 0) return -1;

    const uint8_t *p = (const uint8_t *)(uintptr_t)mb2_adres;
    /* Ilk 8 bayt: toplam boyut + yedek */
    uint32_t toplam = *(const uint32_t *)p;
    uint32_t ofs = 8;

    cikti->flags = 0;
    uint32_t mmap_sayisi = 0;

    while (ofs < toplam) {
        const mb2_etiket_t *e = (const mb2_etiket_t *)(p + ofs);
        if (e->tur == MB2_TAG_SON) break;

        if (e->tur == MB2_TAG_FRAMEBUFFER) {
            /* framebuffer_addr(8) width(4) height(4) pitch(4) bpp(1) tur(1) */
            const uint8_t *f = (const uint8_t *)e + 8;
            cikti->framebuffer_addr   = *(const uint64_t *)(f + 0);
            cikti->framebuffer_pitch  = *(const uint32_t *)(f + 8);
            cikti->framebuffer_width  = *(const uint32_t *)(f + 12);
            cikti->framebuffer_height = *(const uint32_t *)(f + 16);
            cikti->framebuffer_bpp    = *(const uint8_t  *)(f + 20);
            cikti->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;

        } else if (e->tur == MB2_TAG_ACPI_ESKI || e->tur == MB2_TAG_ACPI_YENI) {
            /* Yeni (v2) etiket eskisine ustun gelir. */
            if (e->tur == MB2_TAG_ACPI_YENI || !rsdp_var) {
                uint32_t n = e->boyut - 8;
                if (n > sizeof(rsdp_kopya)) n = sizeof(rsdp_kopya);
                const uint8_t *k = (const uint8_t *)e + 8;
                for (uint32_t i = 0; i < sizeof(rsdp_kopya); i++)
                    rsdp_kopya[i] = (i < n) ? k[i] : 0;
                rsdp_var = 1;
            }

        } else if (e->tur == MB2_TAG_MMAP) {
            /* etiket: tur(4) boyut(4) giris_boyutu(4) surum(4) sonra girisler */
            uint32_t giris_boyutu = *(const uint32_t *)((const uint8_t *)e + 8);
            uint32_t girisler_ofs = 16;
            uint32_t kalan = e->boyut - girisler_ofs;

            while (kalan >= giris_boyutu && mmap_sayisi < 64) {
                const mb2_mmap_giris_t *g =
                    (const mb2_mmap_giris_t *)((const uint8_t *)e + girisler_ofs);

                /* multiboot1 bicimine cevir: her girisin basinda 'size'
                 * alani vardir ve KENDISINI saymaz. */
                cevrilen_mmap[mmap_sayisi].size   = sizeof(multiboot_mmap_entry_t) - 4;
                cevrilen_mmap[mmap_sayisi].addr   = g->taban;
                cevrilen_mmap[mmap_sayisi].len    = g->uzunluk;
                cevrilen_mmap[mmap_sayisi].type   = g->tur;
                mmap_sayisi++;

                girisler_ofs += giris_boyutu;
                kalan -= giris_boyutu;
            }

            cevrilen_mmap_adres64 = (uint64_t)(uintptr_t)cevrilen_mmap;
            cevrilen_mmap_uzunluk = mmap_sayisi * (uint32_t)sizeof(multiboot_mmap_entry_t);
            cikti->mmap_addr   = (uint32_t)(uintptr_t)cevrilen_mmap;  /* kirpilmis olabilir */
            cikti->mmap_length = cevrilen_mmap_uzunluk;
            cikti->flags |= MULTIBOOT_INFO_MEM_MAP;
        }

        /* Etiketler 8 bayta hizalidir */
        ofs += (e->boyut + 7) & ~7u;
    }

    serial_write("multiboot2: cevrildi - mmap girisi=");
    serial_write_hex(mmap_sayisi);
    serial_write(" framebuffer=");
    serial_write((cikti->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) ? "VAR" : "YOK");
    serial_write("\n");

    return (mmap_sayisi > 0) ? 0 : -1;
}
