#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "timur.h"
#include "log.h"
#include "serial.h"
#include "mutex.h"

int elf_validate(const uint8_t *data, uint64_t size) {
    if (size < sizeof(elf64_header_t)) {
        log_write(LOG_HATA, "ELF", "dosya cok kucuk");
        return -1;
    }
    const elf64_header_t *h = (const elf64_header_t *)data;

    if (h->e_ident[0] != ELF_MAGIC0 || h->e_ident[1] != ELF_MAGIC1 ||
        h->e_ident[2] != ELF_MAGIC2 || h->e_ident[3] != ELF_MAGIC3) {
        log_write(LOG_HATA, "ELF", "gecersiz sihirli imza (magic)");
        return -1;
    }
    if (h->e_ident[4] != ELFCLASS64) {
        log_write(LOG_HATA, "ELF", "64-bit degil");
        return -1;
    }
    if (h->e_ident[5] != ELFDATA2LSB) {
        log_write(LOG_HATA, "ELF", "little-endian degil");
        return -1;
    }
    if (h->e_machine != EM_X86_64) {
        log_write(LOG_HATA, "ELF", "x86-64 mimarisi degil");
        return -1;
    }
    if (h->e_type != ET_EXEC) {
        log_write(LOG_HATA, "ELF", "calistirilabilir (ET_EXEC) degil");
        return -1;
    }
    return 0;
}


/* --- Denetim duzeltmesi: guvenli sinir denetimleri ---
 * Onceki surumde p_offset + p_filesz toplami TASABILIR (her ikisi de
 * 64-bit ve saldirgan kontrolunde), p_vaddr HIC denetlenmiyordu.
 * Ozel hazirlanmis bir ELF, p_vaddr'i cekirdek adresine ayarlayarak
 * cekirdek kodunu KULLANICI-YAZILABILIR olarak yeniden haritalayabilirdi. */

static int toplama_tasar(uint64_t a, uint64_t b) {
    return (a > 0xFFFFFFFFFFFFFFFFULL - b);
}

/* Segmentin sanal adres araligi KULLANICI bolgesinde mi? */
static int kullanici_araliginda(uint64_t vaddr, uint64_t memsz) {
    if (memsz == 0) return 0;
    if (toplama_tasar(vaddr, memsz)) return 0;
    uint64_t son = vaddr + memsz;
    if (vaddr < ELF_USER_MIN) return 0;        /* cekirdek kimlik haritasi */
    if (son   > ELF_USER_MAX) return 0;        /* kanonik olmayan / ust yari */
    return 1;
}

/* Yuklenen sayfalarin kaydi - basarisizlikta GERI ALMAK icin. */
typedef struct {
    uint64_t virt;
    uint64_t phys;
} elf_sayfa_kaydi_t;

static void elf_geri_al(elf_sayfa_kaydi_t *kayit, int adet) {
    for (int i = 0; i < adet; i++) {
        vmm_unmap_page(kayit[i].virt);
        pmm_free_frame(kayit[i].phys);
    }
    if (adet > 0) {
        serial_write("ELF: yukleme basarisiz - ");
        serial_write_hex((uint32_t)adet);
        serial_write(" sayfa geri alindi (rollback)\n");
    }
}

/* Denetim (1.4.6): geri alma kaydi (kayit[]) DURAGAN ve tektir; iki
 * cekirdekte ayni anda yukleme (kabuk yeniden baslatma + UMAY) birinin
 * basarisizliginda digerinin sayfalarini serbest birakabiliyordu. Yuklemeler
 * gorev kilidiyle SIRAYA konur (uyuyan kilit; zamanlayici yokken islem yok). */
static int elf_load__ic(const uint8_t *data, uint64_t size, elf_image_t *out);
static ozkilit_t elf_kilit;

int elf_load(const uint8_t *data, uint64_t size, elf_image_t *out) {
    ozkilit_al(&elf_kilit);
    int r = elf_load__ic(data, size, out);
    ozkilit_birak(&elf_kilit);
    return r;
}

static int elf_load__ic(const uint8_t *data, uint64_t size, elf_image_t *out) {
    if (elf_validate(data, size) != 0) return -1;

    const elf64_header_t *h = (const elf64_header_t *)data;

    /* Giris noktasi da kullanici alaninda OLMALIDIR. */
    if (h->e_entry < ELF_USER_MIN || h->e_entry >= ELF_USER_MAX) {
        log_write(LOG_HATA, "ELF", "giris noktasi kullanici alani disinda");
        return -1;
    }
    out->entry_point = h->e_entry;
    out->segments_loaded = 0;
    out->lowest_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    out->highest_vaddr = 0;

    log_write_num(LOG_BILGI, "ELF", "giris noktasi", h->e_entry);
    log_write_num(LOG_BILGI, "ELF", "program basligi sayisi", h->e_phnum);

    /* Tasmaya karsi guvenli denetim (carpim ve toplam ayri ayri) */
    if (h->e_phentsize < sizeof(elf64_phdr_t)) {
        log_write(LOG_HATA, "ELF", "program basligi boyutu gecersiz");
        return -1;
    }
    if (h->e_phnum > ELF_MAX_SEG * 4) {
        log_write(LOG_HATA, "ELF", "asiri program basligi sayisi");
        return -1;
    }
    uint64_t ph_boyut = (uint64_t)h->e_phnum * (uint64_t)h->e_phentsize;
    if (toplama_tasar(h->e_phoff, ph_boyut) ||
        h->e_phoff + ph_boyut > size) {
        log_write(LOG_HATA, "ELF", "program basligi tablosu dosya disinda");
        return -1;
    }

    /* Geri alma kaydi + cakisma denetimi icin segment listesi */
    static elf_sayfa_kaydi_t kayit[ELF_MAX_SEG * ELF_MAX_PAGES];
    int kayit_adet = 0;
    uint64_t seg_bas[ELF_MAX_SEG], seg_son[ELF_MAX_SEG];
    int seg_adet = 0;

    for (uint16_t i = 0; i < h->e_phnum; i++) {
        const elf64_phdr_t *ph =
            (const elf64_phdr_t *)(data + h->e_phoff + (uint64_t)i * h->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        /* --- Sertlestirilmis denetimler --- */
        if (toplama_tasar(ph->p_offset, ph->p_filesz) ||
            ph->p_offset + ph->p_filesz > size) {
            log_write(LOG_HATA, "ELF", "segment verisi dosya disinda");
            elf_geri_al(kayit, kayit_adet);
            return -1;
        }
        if (ph->p_filesz > ph->p_memsz) {
            log_write(LOG_HATA, "ELF", "p_filesz > p_memsz (bozuk segment)");
            elf_geri_al(kayit, kayit_adet);
            return -1;
        }
        if (!kullanici_araliginda(ph->p_vaddr, ph->p_memsz)) {
            log_write(LOG_HATA, "ELF",
                      "segment KULLANICI alani disinda - reddedildi");
            serial_write("ELF: GUVENLIK - reddedilen vaddr=");
            serial_write_hex((uint32_t)(ph->p_vaddr >> 32));
            serial_write_hex((uint32_t)ph->p_vaddr);
            serial_write("\n");
            elf_geri_al(kayit, kayit_adet);
            return -1;
        }
        if (seg_adet >= ELF_MAX_SEG) {
            log_write(LOG_HATA, "ELF", "azami segment sayisi asildi");
            elf_geri_al(kayit, kayit_adet);
            return -1;
        }

        uint64_t seg_start = ph->p_vaddr & ~0xFFFULL;
        uint64_t seg_end   = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;
        uint64_t pages     = (seg_end - seg_start) / PMM_FRAME_SIZE;

        if (pages == 0 || pages > ELF_MAX_PAGES) {
            log_write(LOG_HATA, "ELF", "segment boyutu sinir disi");
            elf_geri_al(kayit, kayit_adet);
            return -1;
        }

        /* Segmentler BIRBIRIYLE cakismamali - cakisma, ayni sayfanin
         * iki kez ayrilmasina ve sayfa sizintisina yol acar. */
        for (int s = 0; s < seg_adet; s++) {
            if (seg_start < seg_son[s] && seg_bas[s] < seg_end) {
                log_write(LOG_HATA, "ELF", "segmentler cakisiyor");
                elf_geri_al(kayit, kayit_adet);
                return -1;
            }
        }
        seg_bas[seg_adet] = seg_start;
        seg_son[seg_adet] = seg_end;
        seg_adet++;

        if (ph->p_vaddr < out->lowest_vaddr)  out->lowest_vaddr = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > out->highest_vaddr)
            out->highest_vaddr = ph->p_vaddr + ph->p_memsz;

        /* Sayfalari once YAZILABILIR olarak haritala (icerigi kopyalayabilmek
         * icin), kopyalama bittikten SONRA gercek izinlere daraltilacak. */
        for (uint64_t p = 0; p < pages; p++) {
            uint64_t phys = pmm_alloc_frame();
            if (phys == 0) {
                log_write(LOG_HATA, "ELF", "PMM'de bellek kalmadi");
                elf_geri_al(kayit, kayit_adet);
                return -1;
            }
            uint64_t hedef_va = seg_start + p * PMM_FRAME_SIZE;
            if (vmm_map_page(hedef_va, phys,
                              VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
                pmm_free_frame(phys);
                log_write(LOG_HATA, "ELF", "segment haritalanamadi");
                elf_geri_al(kayit, kayit_adet);
                return -1;
            }
            /* Geri alma icin kaydet */
            if (kayit_adet < ELF_MAX_SEG * ELF_MAX_PAGES) {
                kayit[kayit_adet].virt = hedef_va;
                kayit[kayit_adet].phys = phys;
                kayit_adet++;
            }
            /* .bss icin sifirla (p_memsz > p_filesz oldugunda gerekli) */
            uint8_t *pg = (uint8_t *)(uintptr_t)(seg_start + p * PMM_FRAME_SIZE);
            for (int k = 0; k < PMM_FRAME_SIZE; k++) pg[k] = 0;
        }

        /* Dosyadaki icerigi hedef sanal adrese kopyala */
        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_vaddr;
        const uint8_t *src = data + ph->p_offset;
        for (uint64_t k = 0; k < ph->p_filesz; k++) dst[k] = src[k];

        /* Simdi GERCEK izinleri uygula - W^X politikasi:
         * calistirilabilir segment YAZILAMAZ, veri segmenti CALISTIRILAMAZ */
        for (uint64_t p = 0; p < pages; p++) {
            uint64_t va = seg_start + p * PMM_FRAME_SIZE;
            if (ph->p_flags & PF_X) {
                timur_protect_code(va);   /* R+X, yazilamaz */
            } else {
                timur_protect_data(va);   /* R+W, calistirilamaz (NX) */
            }
        }

        out->segments_loaded++;
        serial_write("ELF: segment yuklendi vaddr=");
        serial_write_hex((uint32_t)(ph->p_vaddr >> 32));
        serial_write_hex((uint32_t)ph->p_vaddr);
        serial_write(" boyut=");
        serial_write_hex((uint32_t)ph->p_memsz);
        serial_write(" izin=");
        serial_write((ph->p_flags & PF_R) ? "R" : "-");
        serial_write((ph->p_flags & PF_W) ? "W" : "-");
        serial_write((ph->p_flags & PF_X) ? "X" : "-");
        serial_write("\n");
    }

    if (out->segments_loaded == 0) {
        log_write(LOG_HATA, "ELF", "yuklenebilir segment (PT_LOAD) yok");
        elf_geri_al(kayit, kayit_adet);
        return -1;
    }
    return 0;
}
