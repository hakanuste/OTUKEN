#include "vmm.h"
#include "smp.h"
#include "pmm.h"
#include "serial.h"

/* SMP adim 6: VMM: kernel ve surec sayfa tablolari; heap buyurken de cagrilir. Kisa islemler -> ozyinelemeli donen kilit. */
#include "spinlock.h"
static ozspin_t vmm_kilit = OZSPIN_BASLANGIC;

#define ENTRIES 512
#define PAGE_2MB (2ULL * 1024 * 1024)
#define PS_BIT   (1ULL << 7)

static inline uint64_t *table_ptr(uint64_t phys) {
    /* Identity-map (0-4GB) sayesinde fiziksel adres = sanal adres,
     * tablo icerigine dogrudan bu pointer'la erisebiliyoruz. */
    return (uint64_t *)(uintptr_t)phys;
}

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void invlpg(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static void zero_table(uint64_t *t) {
    for (int i = 0; i < ENTRIES; i++) t[i] = 0;
}

/* table[index] bos ise PMM'den yeni bir cerceve alip alt tablo olarak
 * kurar (P|RW|US ile - kisitlama gercek yapraktaki PT girisinde
 * uygulanir). Var olan girisi (fiziksel adresini) dondurur. 0 = basarisiz
 * (PMM'de bellek kalmadi). */
/* --- Denetim duzeltmesi (CRITICAL) ---
 * Onceki surumde get_or_create, MEVCUT bir ust-seviye girise KOSULSUZ
 * olarak VMM_USER ekliyordu. Bu, cekirdek bolgelerini (PML4[0] kimlik
 * haritasi ve PML4[511] higher-half) tasiyan tablolarin kullanici
 * erisimine acilmasi anlamina gelir. Yaprak (PT) girisi hala koruma
 * saglasa da, bu savunma katmanini kaldirir ve 2MB cekirdek
 * sayfalarinda (PS=1, ara seviyede yaprak) GERCEK bir acik olusturur.
 *
 * Artik USER biti YALNIZCA kullanici haritalamasi yapilirken ve
 * YALNIZCA kullanici PML4 araligi (1..255) icin eklenir. */
static int kullanici_pml4_araligi(int pml4_index) {
    return (pml4_index >= 1 && pml4_index <= 255);
}

static uint64_t get_or_create_bayrakli(uint64_t *table, int index,
                                        int kullanici_ister, int pml4_idx,
                                        int seviye) {
    if (table[index] & VMM_PRESENT) {
        /* USER bitini SADECE gercekten kullanici haritalamasi yapilirken
         * ve girdigimiz PML4 dalı kullanici bolgesindeyse ekle. */
        if (kullanici_ister && !(table[index] & VMM_USER)) {
            if (seviye == 4) {
                if (kullanici_pml4_araligi(index)) table[index] |= VMM_USER;
            } else if (kullanici_pml4_araligi(pml4_idx)) {
                table[index] |= VMM_USER;
            }
        }
        return table[index] & 0x000FFFFFFFFFF000ULL;
    }
    uint64_t new_frame = pmm_alloc_frame();
    if (new_frame == 0) return 0;
    zero_table(table_ptr(new_frame));
    uint64_t bayrak = VMM_PRESENT | VMM_WRITABLE;
    int user_ok = kullanici_ister &&
                  ((seviye == 4) ? kullanici_pml4_araligi(index)
                                 : kullanici_pml4_araligi(pml4_idx));
    if (user_ok) bayrak |= VMM_USER;
    table[index] = new_frame | bayrak;
    return new_frame;
}


/* pd[index] su an bir 2MB (PS=1) sayfaysa, onu 512x4KB PT girisine
 * "boler": ayni fiziksel araligi ayni bayraklarla koruyarak yeni bir
 * PT olusturur ve PD girisini bu PT'yi gosterecek sekilde gunceller.
 * PS=0 ya da bos ise dokunmaz. */
/* --- 1GB SAYFA BOLME ---
 * boot.asm, 1GB sayfa destekleyen islemcilerde kimlik haritasini
 * PDPT seviyesinde 1GB sayfalarla kuruyor (PDPTE.PS=1). VMM ince
 * taneli (4KB) haritalama yaparken bu girisi bir PD ISARETCISI
 * saniyordu; oysa PS=1 iken alan bir SAYFA CERCEVESIDIR. Sonuc:
 * cop bir adres PD olarak kullaniliyor, haritalama bozuluyor ve
 * heap magic dogrulamasi paniklıyordu (gercekten yasandi).
 *
 * Cozum, 2MB bolmenin birebir esi: 1GB sayfayi 512 adet 2MB sayfaya
 * cevirip PDPT girisini gercek bir PD'ye yonlendiriyoruz. */
static int split_1gb_if_needed(uint64_t *pdpt, int pdpt_index) {
    uint64_t entry = pdpt[pdpt_index];
    if (!(entry & VMM_PRESENT)) return 0;
    if (!(entry & PS_BIT))      return 0;   /* zaten PD'ye isaret ediyor */

    uint64_t old_phys_base = entry & 0x000FFFFFC0000000ULL;  /* 1GB hizali */
    uint64_t old_flags     = entry & 0x7;                     /* P|RW|US */

    uint64_t new_pd_frame = pmm_alloc_frame();
    if (new_pd_frame == 0) return -1;

    uint64_t *new_pd = table_ptr(new_pd_frame);
    for (int i = 0; i < ENTRIES; i++) {
        /* Her giris 2MB sayfa olarak kalir (PS biti korunur) */
        new_pd[i] = (old_phys_base + (uint64_t)i * 0x200000ULL) | old_flags | PS_BIT;
    }

    pdpt[pdpt_index] = new_pd_frame | old_flags;   /* PS temizlendi -> PD */
    invlpg(old_phys_base);
    return 0;
}

static int split_2mb_if_needed(uint64_t *pd, int pd_index) {
    uint64_t entry = pd[pd_index];
    if (!(entry & VMM_PRESENT)) return 0;   /* bos, bolunecek bir sey yok */
    if (!(entry & PS_BIT))      return 0;   /* zaten PT'ye isaret ediyor */

    uint64_t old_phys_base = entry & 0x000FFFFFFFE00000ULL; /* 2MB hizali */
    uint64_t old_flags      = entry & 0x7; /* P|RW|US */

    uint64_t new_pt_frame = pmm_alloc_frame();
    if (new_pt_frame == 0) return -1;

    uint64_t *new_pt = table_ptr(new_pt_frame);
    for (int i = 0; i < ENTRIES; i++) {
        new_pt[i] = (old_phys_base + (uint64_t)i * PMM_FRAME_SIZE) | old_flags;
    }

    pd[pd_index] = new_pt_frame | old_flags; /* PS temizlendi -> artik PT */
    invlpg(old_phys_base); /* bu 2MB'lik bolgenin TLB girisini gecersiz kil */
    return 0;
}

void vmm_init(void) {
    serial_write("vmm: hazir (mevcut CR3 uzerinden calisiyor)\n");
}

static int vmm_map_page__ic(uint64_t virt, uint64_t phys, uint64_t flags) {
    virt &= ~0xFFFULL;
    phys &= ~0xFFFULL;

    int i4 = (int)((virt >> 39) & 0x1FF);
    int i3 = (int)((virt >> 30) & 0x1FF);
    int i2 = (int)((virt >> 21) & 0x1FF);
    int i1 = (int)((virt >> 12) & 0x1FF);

    uint64_t *pml4 = table_ptr(read_cr3() & 0x000FFFFFFFFFF000ULL);

    int kullanici = (flags & VMM_USER) ? 1 : 0;
    uint64_t pdpt_phys = get_or_create_bayrakli(pml4, i4, kullanici, i4, 4);
    if (pdpt_phys == 0) return -1;
    uint64_t *pdpt = table_ptr(pdpt_phys);

    /* PDPT girisi 1GB SAYFA ise once 2MB'lik bir PD'ye bol -
     * aksi halde sayfa cercevesini tablo adresi sanardik. */
    if (split_1gb_if_needed(pdpt, i3) != 0) return -1;
    uint64_t pd_phys = get_or_create_bayrakli(pdpt, i3, kullanici, i4, 3);
    if (pd_phys == 0) return -1;
    uint64_t *pd = table_ptr(pd_phys);

    if (split_2mb_if_needed(pd, i2) != 0) return -1;

    uint64_t pt_phys = get_or_create_bayrakli(pd, i2, kullanici, i4, 2);
    if (pt_phys == 0) return -1;
    uint64_t *pt = table_ptr(pt_phys);

    uint64_t eski = pt[i1];
    pt[i1] = phys | (flags & 0xFFFULL) | VMM_PRESENT;
    invlpg(virt);
    /* Onceden gecerli girdi degistiyse diger cekirdeklerin TLB'si bayat. */
    if (eski & VMM_PRESENT) tlb_uzak_vur(virt);
    return 0;
}

static void vmm_unmap_page__ic(uint64_t virt) {
    virt &= ~0xFFFULL;
    int i4 = (int)((virt >> 39) & 0x1FF);
    int i3 = (int)((virt >> 30) & 0x1FF);
    int i2 = (int)((virt >> 21) & 0x1FF);
    int i1 = (int)((virt >> 12) & 0x1FF);

    uint64_t *pml4 = table_ptr(read_cr3() & 0x000FFFFFFFFFF000ULL);
    if (!(pml4[i4] & VMM_PRESENT)) return;
    uint64_t *pdpt = table_ptr(pml4[i4] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[i3] & VMM_PRESENT)) return;
    /* 1GB sayfa: tek 4KB kaldirilamaz (ve PD sanilirsa rastgele fiziksel
     * bellege SIFIR yazilirdi). 2MB durumuyla ayni davranis: dokunma. */
    if (pdpt[i3] & PS_BIT) return;
    uint64_t *pd = table_ptr(pdpt[i3] & 0x000FFFFFFFFFF000ULL);
    if (!(pd[i2] & VMM_PRESENT)) return;
    if (pd[i2] & PS_BIT) {
        /* 2MB'lik sayfanin tamamini kaldirmiyoruz, tek 4KB istendi */
        return;
    }
    uint64_t *pt = table_ptr(pd[i2] & 0x000FFFFFFFFFF000ULL);
    pt[i1] = 0;
    invlpg(virt);
    tlb_uzak_vur(virt);
}

uint64_t vmm_get_physical(uint64_t virt) {
    int i4 = (int)((virt >> 39) & 0x1FF);
    int i3 = (int)((virt >> 30) & 0x1FF);
    int i2 = (int)((virt >> 21) & 0x1FF);
    int i1 = (int)((virt >> 12) & 0x1FF);
    uint64_t offset12 = virt & 0xFFFULL;
    uint64_t offset21 = virt & 0x1FFFFFULL;

    uint64_t *pml4 = table_ptr(read_cr3() & 0x000FFFFFFFFFF000ULL);
    if (!(pml4[i4] & VMM_PRESENT)) return 0;
    uint64_t *pdpt = table_ptr(pml4[i4] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[i3] & VMM_PRESENT)) return 0;
    /* 1GB sayfa (PDPT.PS): eskiden PD tablosu sanilip o fiziksel bellek
     * tablo diye okunuyordu -> 1GB kimlik bolgesinde COP adres. */
    if (pdpt[i3] & PS_BIT)
        return (pdpt[i3] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
    uint64_t *pd = table_ptr(pdpt[i3] & 0x000FFFFFFFFFF000ULL);
    if (!(pd[i2] & VMM_PRESENT)) return 0;
    if (pd[i2] & PS_BIT) {
        return (pd[i2] & 0x000FFFFFFFE00000ULL) | offset21;
    }
    uint64_t *pt = table_ptr(pd[i2] & 0x000FFFFFFFFFF000ULL);
    if (!(pt[i1] & VMM_PRESENT)) return 0;
    return (pt[i1] & 0x000FFFFFFFFFF000ULL) | offset12;
}

/* ===================== Asama 23: Adres Alanlari ===================== */

/* Cekirdegin paylasilan PML4 girisleri:
 *  - indeks 0   : 0-512GB kimlik haritasi (cekirdek fiziksel bellege
 *                 dogrudan erisiyor: PMM cerceveleri, sayfa tablolari,
 *                 DMA tamponlari - bu YASAMSAL)
 *  - indeks 511 : higher-half cekirdek kodu/verisi
 * Bunlar TUM adres alanlarinda AYNI kalir; kullanici bolgesi
 * (indeks 1..510) her surece ozeldir. */
#define KERNEL_PML4_IDENTITY 0
#define KERNEL_PML4_HIGHER   511

/* 1.4.7: CANLI adres alani sayaci (olusturulan - yok edilen). Gercek
 * muhasebe: surec yasam dongusu sinamasi, yok edilen surecin adres alaninin
 * GERCEKTEN geri alindigini gurultusuz (baska ayirmalardan etkilenmeden)
 * olcer. vmm_kilit altinda degisir. */
static int64_t canli_alan = 0;
int64_t vmm_canli_alan_sayisi(void) { return __atomic_load_n(&canli_alan, __ATOMIC_ACQUIRE); }

/* 1.4.8: canli adres alanlarinin kimligi (olusturma sirasi). PML4 cercevesi
 * yok edildikten sonra YENIDEN kullanilabilir; sira no bunu ayirt eder.
 * Yalniz sinama/tani icindir (vmm_kilit altinda guncellenir). */
#define ALAN_KAYIT 64
static struct { uint64_t pml4, sira; } alan_kayit[ALAN_KAYIT];
static uint64_t alan_sira = 0;
static void alan_kaydet(uint64_t pml4) {
    for (int i = 0; i < ALAN_KAYIT; i++)
        if (!alan_kayit[i].pml4) { alan_kayit[i].pml4 = pml4; alan_kayit[i].sira = ++alan_sira; return; }
}
static void alan_sil(uint64_t pml4) {
    for (int i = 0; i < ALAN_KAYIT; i++)
        if (alan_kayit[i].pml4 == pml4) { alan_kayit[i].pml4 = 0; alan_kayit[i].sira = 0; }
}
uint64_t vmm_alan_kimlik(uint64_t pml4) {
    uint64_t r = 0;
    ozspin_al(&vmm_kilit);
    for (int i = 0; i < ALAN_KAYIT && !r; i++)
        if (alan_kayit[i].pml4 == pml4) r = alan_kayit[i].sira;
    ozspin_birak(&vmm_kilit);
    return r;
}

uint64_t vmm_current_address_space(void) {
    return read_cr3() & 0x000FFFFFFFFFF000ULL;
}

void vmm_switch_address_space(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

static uint64_t vmm_create_address_space__ic(void) {
    uint64_t new_pml4 = pmm_alloc_frame();
    if (new_pml4 == 0) {
        serial_write("vmm: adres alani icin cerceve ayrilamadi\n");
        return 0;
    }

    uint64_t *dst = table_ptr(new_pml4);
    uint64_t *src = table_ptr(vmm_current_address_space());

    zero_table(dst);   /* kullanici bolgesi tamamen bos baslar */

    /* Cekirdek bolgelerini PAYLAS (kopyala degil - ayni alt tablolara
     * isaret ederler, boylece cekirdek guncellemeleri tum adres
     * alanlarinda gecerli olur) */
    dst[KERNEL_PML4_IDENTITY] = src[KERNEL_PML4_IDENTITY];
    dst[KERNEL_PML4_HIGHER]   = src[KERNEL_PML4_HIGHER];
    __atomic_add_fetch(&canli_alan, 1, __ATOMIC_ACQ_REL);
    alan_kaydet(new_pml4);

    serial_write("vmm: yeni adres alani olusturuldu PML4=");
    serial_write_hex((uint32_t)new_pml4);
    serial_write("\n");
    return new_pml4;
}

/* Belirtilen PML4 uzerinde haritalama - mevcut CR3 degismez.
 * (Ara tablolar identity map sayesinde dogrudan erisilebilir.) */
static int vmm_map_page_in__ic(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    virt &= ~0xFFFULL;
    phys &= ~0xFFFULL;

    int i4 = (int)((virt >> 39) & 0x1FF);
    int i3 = (int)((virt >> 30) & 0x1FF);
    int i2 = (int)((virt >> 21) & 0x1FF);
    int i1 = (int)((virt >> 12) & 0x1FF);

    uint64_t *pml4 = table_ptr(pml4_phys);

    int kullanici = (flags & VMM_USER) ? 1 : 0;
    uint64_t pdpt_phys = get_or_create_bayrakli(pml4, i4, kullanici, i4, 4);
    if (pdpt_phys == 0) return -1;
    uint64_t *pdpt = table_ptr(pdpt_phys);

    /* PDPT girisi 1GB SAYFA ise once 2MB'lik bir PD'ye bol -
     * aksi halde sayfa cercevesini tablo adresi sanardik. */
    if (split_1gb_if_needed(pdpt, i3) != 0) return -1;
    uint64_t pd_phys = get_or_create_bayrakli(pdpt, i3, kullanici, i4, 3);
    if (pd_phys == 0) return -1;
    uint64_t *pd = table_ptr(pd_phys);

    if (split_2mb_if_needed(pd, i2) != 0) return -1;

    uint64_t pt_phys = get_or_create_bayrakli(pd, i2, kullanici, i4, 2);
    if (pt_phys == 0) return -1;
    uint64_t *pt = table_ptr(pt_phys);

    pt[i1] = phys | (flags & 0xFFFULL) | VMM_PRESENT;

    /* Bu haritalama SU ANKI adres alanindaysa TLB'yi gecersiz kil */
    if (pml4_phys == vmm_current_address_space()) invlpg(virt);
    return 0;
}

static void vmm_destroy_address_space__ic(uint64_t pml4_phys) {
    if (pml4_phys == 0) return;
    if (pml4_phys == vmm_current_address_space()) {
        serial_write("vmm: UYARI - KULLANIMDA olan adres alani yok "
                     "edilemez, atlaniyor\n");
        return;
    }

    uint64_t *pml4 = table_ptr(pml4_phys);

    /* SADECE kullanici bolgesini (1..510) serbest birak - cekirdek
     * girisleri (0 ve 511) PAYLASILDIGI icin onlara DOKUNMA, yoksa
     * diger tum adres alanlari bozulur. */
    for (int i4 = 1; i4 <= 510; i4++) {
        if (!(pml4[i4] & VMM_PRESENT)) continue;
        uint64_t pdpt_phys = pml4[i4] & 0x000FFFFFFFFFF000ULL;
        uint64_t *pdpt = table_ptr(pdpt_phys);

        for (int i3 = 0; i3 < ENTRIES; i3++) {
            if (!(pdpt[i3] & VMM_PRESENT)) continue;
            uint64_t pd_phys = pdpt[i3] & 0x000FFFFFFFFFF000ULL;
            uint64_t *pd = table_ptr(pd_phys);

            for (int i2 = 0; i2 < ENTRIES; i2++) {
                if (!(pd[i2] & VMM_PRESENT)) continue;
                if (pd[i2] & PS_BIT) continue;   /* 2MB sayfa - PT yok */
                uint64_t pt_phys = pd[i2] & 0x000FFFFFFFFFF000ULL;
                uint64_t *pt = table_ptr(pt_phys);

                /* Yaprak sayfalarin kendisini de iade et */
                for (int i1 = 0; i1 < ENTRIES; i1++) {
                    if (pt[i1] & VMM_PRESENT)
                        pmm_free_frame(pt[i1] & 0x000FFFFFFFFFF000ULL);
                }
                pmm_free_frame(pt_phys);
            }
            pmm_free_frame(pd_phys);
        }
        pmm_free_frame(pdpt_phys);
    }

    alan_sil(pml4_phys);
    pmm_free_frame(pml4_phys);
    __atomic_sub_fetch(&canli_alan, 1, __ATOMIC_ACQ_REL);
    serial_write("vmm: adres alani yok edildi PML4=");
    serial_write_hex((uint32_t)pml4_phys);
    serial_write("\n");
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    ozspin_al(&vmm_kilit);
    int r = vmm_map_page__ic(virt, phys, flags);
    ozspin_birak(&vmm_kilit);
    return r;
}

void vmm_unmap_page(uint64_t virt) {
    ozspin_al(&vmm_kilit);
    vmm_unmap_page__ic(virt);
    ozspin_birak(&vmm_kilit);
}

int vmm_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    ozspin_al(&vmm_kilit);
    int r = vmm_map_page_in__ic(pml4_phys, virt, phys, flags);
    ozspin_birak(&vmm_kilit);
    return r;
}

uint64_t vmm_create_address_space(void) {
    ozspin_al(&vmm_kilit);
    uint64_t r = vmm_create_address_space__ic();
    ozspin_birak(&vmm_kilit);
    return r;
}

void vmm_destroy_address_space(uint64_t pml4_phys) {
    ozspin_al(&vmm_kilit);
    vmm_destroy_address_space__ic(pml4_phys);
    ozspin_birak(&vmm_kilit);
}
