#include "pmm.h"
#include "modul.h"
#include "spinlock.h"
#include "serial.h"
#include "multiboot2.h"

/* --- Bellek kapasitesi ---
 * Onceden 4GB'a sabitti (statik bitmap + 0-4GB kimlik haritasi).
 * Modern makinelerde (ornegin 256GB RAM) bunun otesi GORULMUYORDU.
 *
 * Artik bitmap CALISMA ANINDA, makinede gercekten bulunan bellege
 * gore boyutlandiriliyor. Statik bir dizi yerine, multiboot bellek
 * haritasi okunduktan sonra fiziksel bellegin KENDISINDEN ayrilan bir
 * bolge kullaniliyor (kendini barindiran bitmap).
 *
 * Ust sinir: 256GB. Bunun bitmap maliyeti 256GB/4096/8 = 8MB'dir -
 * 256GB'lik bir sistemde ihmal edilebilir. Kimlik haritasinin da
 * ayni oranda genisletilmesi gerekir (bkz. vmm_kimlik_genislet). */
#define PMM_AZAMI_BELLEK  (256ULL * 1024 * 1024 * 1024)
#define BITMAP_MAX_FRAMES (PMM_AZAMI_BELLEK / PMM_FRAME_SIZE)

/* Cekirdek imajinda yer kaplamamasi icin bitmap DINAMIK yerlestirilir.
 * Ilk 4GB'i kapsayan kucuk bir onyukleme bitmap'i statik kalir; boylece
 * bellek haritasi okunmadan once de calisabiliriz. */
#define ONYUKLEME_CERCEVE (4ULL * 1024 * 1024 * 1024 / PMM_FRAME_SIZE)
static uint8_t  onyukleme_bitmap[ONYUKLEME_CERCEVE / 8];

/* --- SMP: PMM bitmap'i paylasilan veridir ---
 * Iki cekirdek ayni anda cerceve ayirirsa AYNI cerceveyi iki kez
 * dagitabilir (bitmap'te oku-degistir-yaz yarisi). Tum ayirma/iade
 * yollari kilit altinda. */
static spinlock_t pmm_kilit;
static int pmm_kilit_hazir = 0;

static inline uint64_t pmm_kilitle(void) {
    if (!pmm_kilit_hazir) { spinlock_init(&pmm_kilit); pmm_kilit_hazir = 1; }
    return spin_lock_irqsave(&pmm_kilit);
}
static inline void pmm_kilit_ac(uint64_t f) {
    spin_unlock_irqrestore(&pmm_kilit, f);
}

/* --- PERFORMANS (kilitleme sonrasi gerekti) ---
 * pmm_used_frames() TUM bitmap'i tariyordu. Kilit eklenince bu tarama
 * KESMELER KAPALIYKEN yapilir oldu; 512 MB'da 131 bin yineleme demek
 * ve sistem o sure boyunca hicbir is yapamiyor (acilis belirgin
 * sekilde yavasladi). Cozum: sayaci ayirma/iade sirasinda CANLI
 * tutmak - sorgu O(1) olur.
 *
 * Ayrica ayirma her seferinde 0'dan taramak yerine son bulunan
 * yerden devam eder (arama ipucu); bos cerceveler genelde arka
 * arkaya oldugu icin bu cok buyuk fark yaratir. */
static uint64_t g_used_frames = 0;
static uint64_t arama_ipucu = 0;

static uint8_t *bitmap = onyukleme_bitmap;
static uint64_t bitmap_cerceve_kapasitesi = ONYUKLEME_CERCEVE;
static uint64_t g_total_frames = 0;

/* Higher-half duzeltmesi: kernel_start/kernel_end artik SANAL
 * adreslerdir (0xFFFFFFFF80...). PMM bitmap'i FIZIKSEL cerceveleri
 * takip ettigi icin, cekirdegin kapladigi alani isaretlerken FIZIKSEL
 * adresleri kullanmak ZORUNDAYIZ - aksi halde sanal adres bitmap
 * sinirinin disinda kalir, cekirdegin kendi bellegi "bos" sanilir ve
 * PMM onu baskasina dagitip cekirdegi ezer (gercekten yasanan hata). */
/* --- KIMLIK HARITASI SINIRI ---
 * PMM yalnizca KIMLIK HARITALI bellegi dagitabilir; cunku cekirdek
 * cerceveleri fiziksel adreslerinden dogrudan kullanir. boot.asm,
 * CPU 1GB sayfa (PDPE1GB) destekliyorsa 512GB, desteklemiyorsa
 * yalnizca 4GB haritalar. Bu sinirin OTESINDE cerceve dagitmak,
 * haritasiz bellege erisim demektir -> sayfa hatasi / cokme.
 * Bu yuzden PMM, ayni CPUID sinamasini yapip ust sinirini AYARLAR. */
static uint64_t kimlik_harita_siniri(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000000;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    if (eax >= 0x80000001) {
        uint32_t e2 = 0x80000001;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(e2));
        if (edx & (1u << 26))                 /* PDPE1GB */
            return 512ULL * 1024 * 1024 * 1024;
    }
    return 4ULL * 1024 * 1024 * 1024;         /* 2MB yolu: yalnizca 4GB */
}

extern char kernel_phys_start[];
extern char kernel_phys_end[];

static inline void bit_set(uint64_t frame) {
    if (frame >= bitmap_cerceve_kapasitesi) return;
    if (!((bitmap[frame >> 3] >> (frame & 7)) & 1)) g_used_frames++;
    bitmap[frame >> 3] |= (uint8_t)(1u << (frame & 7));
}
static inline void bit_clear(uint64_t frame) {
    if (frame >= bitmap_cerceve_kapasitesi) return;
    if ((bitmap[frame >> 3] >> (frame & 7)) & 1) {
        if (g_used_frames > 0) g_used_frames--;
        if (frame < arama_ipucu) arama_ipucu = frame;   /* burada yer acildi */
    }
    bitmap[frame >> 3] &= (uint8_t)~(1u << (frame & 7));
}
static inline int bit_test(uint64_t frame) {
    if (frame >= bitmap_cerceve_kapasitesi) return 1;
    return (bitmap[frame >> 3] >> (frame & 7)) & 1;
}

static void mark_range(uint64_t phys_start, uint64_t length, int used) {
    if (length == 0) return;
    uint64_t first = phys_start / PMM_FRAME_SIZE;
    uint64_t last  = (phys_start + length - 1) / PMM_FRAME_SIZE;
    for (uint64_t f = first; f <= last && f < BITMAP_MAX_FRAMES; f++) {
        if (used) bit_set(f); else bit_clear(f);
    }
}

static void serial_hex64(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = 60 - i * 4;
        buf[2 + i] = hex[(v >> shift) & 0xF];
    }
    buf[18] = 0;
    serial_write(buf);
}

void pmm_init(multiboot_info_t *mbi) {
    /* 1) Guvenli varsayilan: her sey "kullanimda/bilinmiyor" diye baslar. */
    for (uint64_t i = 0; i < sizeof(onyukleme_bitmap); i++) bitmap[i] = 0xFF;

    uint64_t highest_available_end = 0;

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        serial_write("pmm: multiboot bellek haritasi kullaniliyor\n");
        /* multiboot2 yolunda harita CEKIRDEK bellegindedir ve adresi
         * 64 bittir; mbi->mmap_addr (32-bit) onu tasiyamaz. Varsa
         * tam adresi kullan. */
        uint64_t mmap_taban = multiboot2_mmap_adresi();
        uint32_t mmap_uzunluk = multiboot2_mmap_uzunlugu();
        if (mmap_taban == 0) {
            mmap_taban = mbi->mmap_addr;
            mmap_uzunluk = mbi->mmap_length;
        }
        uint64_t p = mmap_taban;
        uint64_t end = mmap_taban + mmap_uzunluk;
        while (p < end) {
            multiboot_mmap_entry_t *e = (multiboot_mmap_entry_t *)(uintptr_t)p;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                mark_range(e->addr, e->len, 0); /* free say */
                uint64_t region_end = e->addr + e->len;
                if (region_end > highest_available_end)
                    highest_available_end = region_end;
            }
            p += e->size + 4;
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        /* Geri dusme yolu: sadece mem_lower/mem_upper var. */
        serial_write("pmm: UYARI - tam bellek haritasi yok, mem_upper kullaniliyor\n");
        mark_range(0x100000, (uint64_t)mbi->mem_upper * 1024, 0);
        highest_available_end = 0x100000 + (uint64_t)mbi->mem_upper * 1024;
    } else {
        serial_write("pmm: HATA - hicbir bellek bilgisi yok, sadece ilk 8MB varsayiliyor\n");
        mark_range(0x100000, 7 * 1024 * 1024, 0);
        highest_available_end = 8 * 1024 * 1024;
    }

    /* Hem bitmap kapasitesi hem de KIMLIK HARITASI sinirina uy */
    uint64_t harita_siniri = kimlik_harita_siniri();
    if (highest_available_end > harita_siniri) {
        serial_write("pmm: bellek kimlik haritasi sinirina KIRPILDI (");
        serial_write_hex((uint32_t)(harita_siniri >> 30));
        serial_write(" GB) - CPU 1GB sayfa desteklemiyor\n");
        highest_available_end = harita_siniri;
    }
    if (highest_available_end > BITMAP_MAX_FRAMES * PMM_FRAME_SIZE)
        highest_available_end = BITMAP_MAX_FRAMES * PMM_FRAME_SIZE;
    g_total_frames = highest_available_end / PMM_FRAME_SIZE;

    /* --- 4GB ustu bellek: bitmap'i BELLEGIN KENDISINE tasi ---
     * Onyukleme bitmap'i yalnizca ilk 4GB'i kapsar. Makinede daha
     * fazlasi varsa, gereken buyuklukte bir bitmap'i fiziksel bellekte
     * (ilk 4GB icinde, cunku su an yalnizca orasi haritali) ayirip
     * onyukleme bitmap'ini oraya kopyalariz. Boylece 4GB'in otesindeki
     * cerceveler de TAKIP EDILEBILIR hale gelir.
     *
     * NOT: takip etmek ile ERISMEK ayri seylerdir. 4GB ustu cerceveleri
     * dagitabilmek icin kimlik haritasinin da genisletilmesi gerekir
     * (vmm tarafinda); bu yapilmadan pmm_alloc_frame o cerceveleri
     * DONDURMEZ - bkz. asagidaki tavan. */
    /* 1.4.9: onyukleme modulleri (ERGENEKON imaji) cekirdegin hemen
     * ustunde durur; buyuk bitmap tasinmadan ve ayirma baslamadan AYRILIR. */
    for (int mi = 0; mi < modul_sayisi(); mi++) {
        const onyukleme_modul_t *m = modul_al(mi);
        mark_range(m->bas & ~0xFFFULL, ((m->son + 0xFFF) & ~0xFFFULL) - (m->bas & ~0xFFFULL), 1);
    }
    if (g_total_frames > bitmap_cerceve_kapasitesi) {
        uint64_t gereken_bayt = (g_total_frames + 7) / 8;
        uint64_t gereken_cerceve = (gereken_bayt + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;

        /* Ilk 4GB icinde, cekirdegin ustunde ardisik bos yer ara */
        uint64_t aday = ((uint64_t)(uintptr_t)kernel_phys_end + PMM_FRAME_SIZE - 1)
                        / PMM_FRAME_SIZE;
        uint64_t bulundu = 0;
        for (uint64_t f = aday; f + gereken_cerceve < ONYUKLEME_CERCEVE; f++) {
            uint64_t k = 0;
            while (k < gereken_cerceve && !bit_test(f + k)) k++;
            if (k == gereken_cerceve) { bulundu = f; break; }
            f += k;
        }

        if (bulundu) {
            uint8_t *yeni_bitmap = (uint8_t *)(uintptr_t)(bulundu * PMM_FRAME_SIZE);
            for (uint64_t i = 0; i < gereken_bayt; i++) yeni_bitmap[i] = 0;
            for (uint64_t i = 0; i < sizeof(onyukleme_bitmap); i++)
                yeni_bitmap[i] = onyukleme_bitmap[i];

            bitmap = yeni_bitmap;
            bitmap_cerceve_kapasitesi = g_total_frames;
            mark_range(bulundu * PMM_FRAME_SIZE,
                       gereken_cerceve * PMM_FRAME_SIZE, 1);   /* bitmap'in kendisi */

            serial_write("pmm: bitmap bellege tasindi, kapasite=");
            serial_hex64(bitmap_cerceve_kapasitesi);
            serial_write(" cerceve (");
            serial_hex64(gereken_bayt >> 10);
            serial_write(" KB)\n");
        } else {
            serial_write("pmm: UYARI - buyuk bitmap icin yer yok, 4GB ile sinirli\n");
            g_total_frames = bitmap_cerceve_kapasitesi;
        }
    }

    /* 2) Bilinen ayrilmis/erisilmemesi gereken bolgeleri geri "kullanimda" isaretle. */
    mark_range(0, 0x100000, 1);                              /* ilk 1MB: BIOS/IVT/video */
    mark_range((uint64_t)(uintptr_t)kernel_phys_start,
               (uint64_t)(uintptr_t)kernel_phys_end -
               (uint64_t)(uintptr_t)kernel_phys_start, 1);      /* kendi imajimiz (FIZIKSEL) */
    mark_range((uint64_t)(uintptr_t)mbi, sizeof(multiboot_info_t), 1);
    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP)
        /* multiboot1'de harita bootloader bellegindedir ve korunmalidir;
         * multiboot2'de zaten cekirdek imajinin icindedir (kernel araligi
         * olarak isaretli). */
        if (multiboot2_mmap_adresi() == 0)
            mark_range(mbi->mmap_addr, mbi->mmap_length, 1);
    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        uint64_t fb_size = (uint64_t)mbi->framebuffer_pitch * mbi->framebuffer_height;
        mark_range(mbi->framebuffer_addr, fb_size, 1);
    }

    uint64_t used = 0;
    for (uint64_t f = 0; f < g_total_frames; f++) if (bit_test(f)) used++;
    /* 1.4.7 HATA DUZELTMESI: canli sayac bitmap'ten ESITLENIR. bit_set/
     * bit_clear sayaci yalniz 0<->1 GECISLERINDE degistirir; bitmap ise
     * bastan tumu-1 baslar. Bellek haritasindaki BOSLUKLAR (ornegin 640 KB -
     * 1 MB) hic "bos" isaretlenmedigi icin 1 kalir ve HIC sayilmazdi:
     * g_used_frames eksik sayiyor, pmm_free_frames() hic ayrilamayacak
     * cerceveleri (-m 128'de 96 tane) "bos" gosteriyordu. Dogru deger
     * burada zaten hesaplanip yaziliyordu; yalniz sayaca atanmiyordu.
     * (ASYOK-TEST bunu yakaladi: ayirici "bitti" derken sayac 96 bos
     * diyordu.) Ayirma dogrulugu etkilenmiyordu; yalniz bildirilen bos
     * bellek sabit bir payla fazlaydi. */
    g_used_frames = used;

    serial_write("pmm: toplam cerceve="); serial_hex64(g_total_frames);
    serial_write(" kullanilan="); serial_hex64(used);
    serial_write(" bos="); serial_hex64(g_total_frames - used);
    serial_write("\n");
    serial_write("pmm: toplam bellek(bayt)="); serial_hex64(g_total_frames * PMM_FRAME_SIZE);
    serial_write("\n");
}

uint64_t pmm_alloc_frame(void) {
    /* Tara-ve-isaretle ATOMIK olmali: iki cekirdek ayni bos cerceveyi
     * gorup ikisi de sahiplenirse AYNI bellek iki yere dagitilir. */
    uint64_t f_bayrak = pmm_kilitle();
    uint64_t sonuc = 0;

    /* Once ipucundan ileri tara, bulamazsan basa sarip ipucuna kadar */
    for (uint64_t f = arama_ipucu; f < g_total_frames; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            arama_ipucu = f + 1;
            sonuc = f * PMM_FRAME_SIZE;
            break;
        }
    }
    if (sonuc == 0) {
        for (uint64_t f = 0; f < arama_ipucu && f < g_total_frames; f++) {
            if (!bit_test(f)) {
                bit_set(f);
                arama_ipucu = f + 1;
                sonuc = f * PMM_FRAME_SIZE;
                break;
            }
        }
    }
    pmm_kilit_ac(f_bayrak);
    return sonuc;   /* 0 = bellek tukendi */
}

void pmm_free_frame(uint64_t phys_addr) {
    uint64_t f = phys_addr / PMM_FRAME_SIZE;
    uint64_t f_bayrak = pmm_kilitle();
    if (f < g_total_frames) bit_clear(f);
    pmm_kilit_ac(f_bayrak);
}

uint64_t pmm_total_frames(void) { return g_total_frames; }

uint64_t pmm_used_frames(void) {
    /* O(1): sayac ayirma/iade sirasinda canli tutuluyor. Tek bir
     * 64-bit okuma atomiktir, kilide gerek yok. */
    return g_used_frames;
}

uint64_t pmm_free_frames(void) {
    return g_total_frames - pmm_used_frames();
}

uint64_t pmm_total_bytes(void) {
    return g_total_frames * PMM_FRAME_SIZE;
}
