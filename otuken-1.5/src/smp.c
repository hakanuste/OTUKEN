#include "smp.h"
#include "spinlock.h"
#include "cpu.h"
#include "idt.h"
#include "io.h"
#include "heap.h"
#include "vmm.h"
#include "serial.h"
#include "multiboot2.h"
#include "gecikme.h"
#include "tss.h"
#include "pmm.h"
#include "sched.h"
#include "syscall.h"
#include "timer.h"

/* =================== yardimcilar =================== */

static inline uint64_t rdmsr_(uint32_t m) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr_(uint32_t m, uint64_t v) {
    __asm__ volatile ("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}
static inline void cpuid_(uint32_t yaprak, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(yaprak), "c"(0));
}

/* Tek serial_write cagrisi icin kucuk bicimlendirici (lesson 5). */
typedef struct { char b[160]; int n; } satir_t;
static void s_ekle(satir_t *s, const char *t) { while (*t && s->n < 158) s->b[s->n++] = *t++; s->b[s->n] = 0; }
static void s_ondalik(satir_t *s, uint64_t v) {
    char t[24]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 23);
    while (i-- > 0 && s->n < 158) s->b[s->n++] = t[i];
    s->b[s->n] = 0;
}
static void s_onalti(satir_t *s, uint64_t v) {
    const char *h = "0123456789ABCDEF"; char t[20]; int i = 0;
    do { t[i++] = h[v & 0xF]; v >>= 4; } while (v && i < 16);
    s_ekle(s, "0x");
    while (i-- > 0 && s->n < 158) s->b[s->n++] = t[i];
    s->b[s->n] = 0;
}

/* Kimlik haritasinin ust siniri (boot.asm ile ayni CPUID denetimi). */
static uint64_t kimlik_siniri(void) {
    uint32_t a, b, c, d;
    cpuid_(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000001) {
        cpuid_(0x80000001, &a, &b, &c, &d);
        if (d & (1u << 26)) return 512ULL << 30;
    }
    return 4ULL << 30;
}

/* =================== ACPI / MADT =================== */

static int toplam_sifir(const uint8_t *p, uint32_t n) {
    uint8_t t = 0;
    for (uint32_t i = 0; i < n; i++) t += p[i];
    return t == 0;
}

static const uint8_t *rsdp_tara(uint64_t bas, uint64_t uzunluk) {
    for (uint64_t a = bas; a + 20 <= bas + uzunluk; a += 16) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)a;
        if (p[0]=='R'&&p[1]=='S'&&p[2]=='D'&&p[3]==' '&&p[4]=='P'&&p[5]=='T'&&p[6]=='R'&&p[7]==' '
            && toplam_sifir(p, 20))
            return p;
    }
    return 0;
}

static const uint8_t *rsdp_bul(int *kaynak) {
    const uint8_t *r = multiboot2_rsdp();
    if (r && toplam_sifir(r, 20)) { *kaynak = 2; return r; }
    uintptr_t bda = 0x40E;                  /* BDA: EBDA segmenti */
    __asm__ ("" : "+r"(bda));               /* GCC sabit-adres uyarisini onler */
    uint64_t ebda = (uint64_t)(*(volatile uint16_t *)bda) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        r = rsdp_tara(ebda, 1024);
        if (r) { *kaynak = 1; return r; }
    }
    r = rsdp_tara(0xE0000, 0x20000);
    if (r) { *kaynak = 1; return r; }
    return 0;
}

static int tablo_gecerli(uint64_t fiz, uint64_t sinir) {
    if (fiz == 0 || fiz + 36 > sinir) return 0;
    uint32_t uz = *(const uint32_t *)(uintptr_t)(fiz + 4);
    if (uz < 36 || uz > 0x100000 || fiz + uz > sinir) return 0;
    return toplam_sifir((const uint8_t *)(uintptr_t)fiz, uz);
}

static const uint8_t *madt_bul(const uint8_t *rsdp) {
    uint64_t sinir = kimlik_siniri();
    uint64_t kok; int giris8;
    if (rsdp[15] >= 2 && *(const uint64_t *)(rsdp + 24) != 0) {
        kok = *(const uint64_t *)(rsdp + 24); giris8 = 1;      /* XSDT */
        if (!tablo_gecerli(kok, sinir)) { kok = *(const uint32_t *)(rsdp + 16); giris8 = 0; }
    } else {
        kok = *(const uint32_t *)(rsdp + 16); giris8 = 0;      /* RSDT */
    }
    if (!tablo_gecerli(kok, sinir)) return 0;
    const uint8_t *t = (const uint8_t *)(uintptr_t)kok;
    uint32_t uz = *(const uint32_t *)(t + 4);
    uint32_t adim = giris8 ? 8 : 4;
    for (uint32_t o = 36; o + adim <= uz; o += adim) {
        uint64_t a = giris8 ? *(const uint64_t *)(t + o) : *(const uint32_t *)(t + o);
        if (!tablo_gecerli(a, sinir)) continue;
        const uint8_t *h = (const uint8_t *)(uintptr_t)a;
        if (h[0]=='A'&&h[1]=='P'&&h[2]=='I'&&h[3]=='C') return h;
    }
    return 0;
}

/* =================== yerel APIC =================== */

#define MSR_APIC_BASE   0x1B
#define APIC_BASE_EN    (1ULL << 11)
#define APIC_BASE_EXTD  (1ULL << 10)
#define LAPIC_ID        0x020
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LO    0x300
#define LAPIC_ICR_HI    0x310
#define LAPIC_LINT0     0x350
#define LAPIC_LINT1     0x360

static volatile uint32_t *lapic_mmio = 0;
static int x2apic_kip = 0;

static uint32_t lapic_oku(uint32_t r) {
    if (x2apic_kip) return (uint32_t)rdmsr_(0x800 + (r >> 4));
    return lapic_mmio[r / 4];
}
static void lapic_yaz(uint32_t r, uint32_t v) {
    if (x2apic_kip) { wrmsr_(0x800 + (r >> 4), v); return; }
    lapic_mmio[r / 4] = v;
}

uint32_t lapic_kimlik(void) {
    if (x2apic_kip) return (uint32_t)rdmsr_(0x802);
    if (lapic_mmio) return lapic_mmio[LAPIC_ID / 4] >> 24;
    return cpu_apic_id();
}

/* Yazilimsal etkinlestirme. Firmware kapali biraktiysa LINT0/LINT1
 * sanal-tel kipine (ExtINT/NMI) kurulur ki 8259 PIC kesmeleri BSP'ye
 * ulasmaya devam etsin. Firmware acik biraktiysa LVT'lere dokunulmaz. */
static int lapic_mmio_veya_x2(void) { return x2apic_kip || lapic_mmio != 0; }

static void lapic_etkinlestir(int bsp) {
    uint32_t svr = lapic_oku(LAPIC_SVR);
    if (svr & 0x100) return;
    lapic_yaz(LAPIC_SVR, (svr & ~0xFFu) | 0x100 | 0xFF);
    if (bsp) {
        lapic_yaz(LAPIC_LINT0, 0x00000700);   /* ExtINT, maskesiz */
        lapic_yaz(LAPIC_LINT1, 0x00000400);   /* NMI */
    } else {
        lapic_yaz(LAPIC_LINT0, 0x00010700);   /* AP: maskeli */
        lapic_yaz(LAPIC_LINT1, 0x00000400);
    }
}

static int ipi_gonder(uint32_t hedef, uint32_t dusuk) {
    if (x2apic_kip) {
        wrmsr_(0x830, ((uint64_t)hedef << 32) | dusuk);
        return 0;
    }
    lapic_yaz(LAPIC_ICR_HI, hedef << 24);
    lapic_yaz(LAPIC_ICR_LO, dusuk);
    for (uint32_t i = 0; i < 1000000u; i++)
        if (!(lapic_oku(LAPIC_ICR_LO) & (1u << 12))) return 0;   /* teslim edildi */
    return -1;
}

/* =================== AP baslatma =================== */

#define TRAMP_FIZ   0x8000ULL
#define TRAMP_VERI  (TRAMP_FIZ + 0xF00)
#define AP_YIGIN    16384

extern const uint8_t ap_tramp_bas[];
extern const uint8_t ap_tramp_son[];
extern uint8_t gdt64[];

typedef struct {
    uint64_t cr3, efer, cr0, cr4, yigin, giris, arg;
} __attribute__((packed)) tramp_veri_t;

static uint32_t apic_kimlikleri[SMP_AZAMI];
static int bulunan = 0;
static volatile int uyanan = 1;          /* BSP */
static volatile int ap_isareti = -1;     /* son uyanan AP'nin indeksi */

static uint64_t ap_yigin_ust = 0;       /* BSP yazar, AP okur (sirali) */
static volatile int dagitim_acik = 0;
static volatile int katilan = 0;          /* zamanlayiciya katilan AP sayisi */
static uint32_t lapic_tik_sayim = 0;      /* PIT tik periyoduna denk sayim */
static void lapic_zamanlayici_kur(void);

static void ap_giris(uint64_t indeks) {
    /* Cekirdegin kendi GDT'si (paylasilan) ve IDT'si; ardindan kendi
     * GS blogu, kendi GDT kopyasi ve TSS'i (tss_ap_kur). */
    struct __attribute__((packed)) { uint16_t sinir; uint64_t taban; } g = { 63, (uint64_t)(uintptr_t)gdt64 };
    __asm__ volatile (
        "lgdt %0\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t mov %%ax, %%es\n\t mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t mov %%ax, %%gs\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : "m"(g) : "rax", "memory");
    idt_ap_yukle();

    if (x2apic_kip) {
        uint64_t t = rdmsr_(MSR_APIC_BASE);
        if (!(t & APIC_BASE_EXTD)) wrmsr_(MSR_APIC_BASE, t | APIC_BASE_EN | APIC_BASE_EXTD);
    }
    lapic_etkinlestir(0);
    uint32_t kimlik = lapic_kimlik();

    cpu_ap_hazirla((uint32_t)indeks, kimlik);              /* GS_BASE */
    int tss_tamam = tss_ap_kur((uint32_t)indeks, ap_yigin_ust) == 0;

    satir_t s = { {0}, 0 };
    s_ekle(&s, "SMP: cekirdek "); s_ondalik(&s, indeks);
    s_ekle(&s, " uyandi (APIC="); s_onalti(&s, kimlik);
    s_ekle(&s, tss_tamam ? ", GS+TSS kuruldu)\n" : ", TSS KURULAMADI)\n");
    serial_write(s.b);

    __atomic_add_fetch(&uyanan, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&ap_isareti, (int)indeks, __ATOMIC_RELEASE);

    /* Dagitim acilana kadar yalniz cagri IPI'lerini (TLB vurusu) isler.
     * PIC kesmeleri yalniz BSP'ye gelir (AP LINT0 maskeli). */
    while (!__atomic_load_n(&dagitim_acik, __ATOMIC_ACQUIRE))
        __asm__ volatile ("sti; hlt; cli" ::: "memory");

    /* Zamanlayiciya katil: bu baslangic baglami cekirdegin BOS gorevi
     * olur; kendi LAPIC zamanlayicisi ile tik alir. */
    syscall_msr_kur();     /* bu cekirdekte Ring3 gorevi calisabilir */
    if (sched_ap_katil((uint32_t)indeks) == 0) {
        lapic_zamanlayici_kur();
        __atomic_add_fetch(&katilan, 1, __ATOMIC_SEQ_CST);
    }
    for (;;) __asm__ volatile ("sti; hlt" ::: "memory");
}

/* =================== cekirdekler arasi cagri (IPI 0xF0) ===================
 * Tek bir istek yuvasi; bir kilit sirayla kullandirir. Kilidi bekleyen
 * cekirdek, beklerken KENDISINE gelmis istegi de isler: aksi halde
 * kilit sahibi onu, o da kilidi bekler (kesmeler kapaliysa kilitlenme). */

#define SMP_CAGRI_VEKTOR 0xF0

static volatile int      cagri_kilit = 0;
static void            (*cagri_fn)(uint64_t) = 0;
static volatile uint64_t cagri_arg = 0;
static volatile uint64_t cagri_maske = 0;     /* bit i: indeks i henuz islemedi */
static volatile uint32_t apic_indeks[SMP_AZAMI];  /* indeks -> APIC kimligi */
static volatile int      tlb_uzak_kapali = 0; /* yalniz sinama (kontrol grubu) */

static void lapic_eoi(void) { lapic_yaz(0x0B0, 0); }

static void cagri_bekleyeni_isle(void) {
    uint32_t i = cpu_bu()->indeks;
    uint64_t bit = 1ULL << i;
    if (!(__atomic_load_n(&cagri_maske, __ATOMIC_ACQUIRE) & bit)) return;
    void (*fn)(uint64_t) = cagri_fn;
    uint64_t arg = cagri_arg;
    if (fn) fn(arg);
    cpu_bu()->ipi_sayisi++;
    __atomic_and_fetch(&cagri_maske, ~bit, __ATOMIC_RELEASE);
}

/* Kesmeler kapaliyken spin eden donguler (spinlock, rq kilidi) bunu
 * cagirir: kilit sahibi TLB vurusu icin BIZI bekliyor olabilir. */
void smp_bekleyen_isle(void) {
    if (__atomic_load_n(&cagri_maske, __ATOMIC_RELAXED)) cagri_bekleyeni_isle();
}

/* interrupt.asm: lapic_cagri_stub -> burasi */
void smp_cagri_isle(void *cerceve) {
    (void)cerceve;
    cagri_bekleyeni_isle();
    lapic_eoi();
}

/* Diger TUM cevrimici cekirdeklerde fn(arg) calistirir ve bitmelerini
 * bekler. Cagiranda calistirmaz. 0: tamam, -1: zaman asimi. */
int smp_cagir_digerleri(void (*fn)(uint64_t), uint64_t arg) {
    int n = uyanan;
    if (n <= 1 || !lapic_mmio_veya_x2()) return 0;
    /* AGIR sinamada yakalanan kilitlenme: kesmeler ACIKKEN cagri_kilit'i
     * alan gorev zamanlayici tarafindan kesilip baska goreve gecilirse
     * kilit UYUYAN bir gorevde kalir; diger cekirdekler IF=0 ile donerken
     * o gorev bir daha calisamaz (cagri_kilit=1, cagri_maske=0, tum
     * cekirdekler spin). Cagri suresince kesmeler KAPALI; ayrica 'ben'
     * de ancak goc edemeyecegimiz noktada okunur. */
    uint64_t irq = irq_kaydet_kapat();
    uint32_t ben = cpu_bu()->indeks;

    while (__atomic_exchange_n(&cagri_kilit, 1, __ATOMIC_ACQUIRE)) {
        cagri_bekleyeni_isle();
        __asm__ volatile ("pause");
    }
    uint64_t maske = 0;
    for (int i = 0; i < n && i < SMP_AZAMI; i++)
        if ((uint32_t)i != ben) maske |= 1ULL << i;
    cagri_fn = fn;
    cagri_arg = arg;
    __atomic_store_n(&cagri_maske, maske, __ATOMIC_RELEASE);
    for (int i = 0; i < n && i < SMP_AZAMI; i++)
        if ((uint32_t)i != ben) ipi_gonder(apic_indeks[i], SMP_CAGRI_VEKTOR);

    int sonuc = 0;
    for (uint64_t t = 0; __atomic_load_n(&cagri_maske, __ATOMIC_ACQUIRE); t++) {
        __asm__ volatile ("pause");
        if (t > 200000000ULL) { sonuc = -1; break; }
    }
    __atomic_store_n(&cagri_maske, 0, __ATOMIC_RELEASE);
    cagri_fn = 0;
    __atomic_store_n(&cagri_kilit, 0, __ATOMIC_RELEASE);
    irq_geri_yukle(irq);
    if (sonuc) serial_write("SMP: [HATA] cagri IPI zaman asimi\n");
    return sonuc;
}

/* =================== TLB vurusu =================== */

static void tlb_isle(uint64_t sanal) {
    if (sanal == ~0ULL) {
        uint64_t c3; __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=r"(c3) : : "memory");
    } else {
        __asm__ volatile ("invlpg (%0)" : : "r"(sanal) : "memory");
    }
}

void tlb_uzak_vur(uint64_t sanal) {
    if (uyanan <= 1 || tlb_uzak_kapali) return;
    smp_cagir_digerleri(tlb_isle, sanal & ~0xFFFULL);
}

void tlb_vur(uint64_t sanal) {
    tlb_isle(sanal & ~0xFFFULL);
    tlb_uzak_vur(sanal);
}

void tlb_vur_hepsi(void) {
    tlb_isle(~0ULL);
    if (uyanan > 1 && !tlb_uzak_kapali) smp_cagir_digerleri(tlb_isle, ~0ULL);
}

/* ---- sinama: gercek bayat TLB girisi olusturup vurusla temizlenmesi ---- */
/* Kimlik haritasinin (0-512GB) DISINDA, kullanici yarisinin cekirdege
 * ozel bir PML4 yuvasi (254): unmap edilince kimlik haritasinda delik
 * birakmaz. Eskiden 0x7E00000000 idi - 1GB kimlik sayfasinin icindeydi. */
#define TLB_SINAMA_SANAL 0x00007F0000000000ULL
static volatile uint64_t okunan[SMP_AZAMI];

static void sinama_oku(uint64_t sanal) {
    okunan[cpu_bu()->indeks] = *(volatile uint64_t *)(uintptr_t)sanal;
}

static void tlb_sinamasi(void) {
    int n = uyanan;
    if (n <= 1) return;
    uint64_t a = pmm_alloc_frame(), b = pmm_alloc_frame();
    if (!a || !b) { serial_write("SMP: [UYARI] TLB sinamasi: cerceve yok\n"); return; }
    const uint64_t IA = 0x4141414141414141ULL, IB = 0x4242424242424242ULL;
    *(volatile uint64_t *)(uintptr_t)a = IA;   /* kimlik haritasi */
    *(volatile uint64_t *)(uintptr_t)b = IB;

    int hata = 0, bayat = 0;
    vmm_map_page(TLB_SINAMA_SANAL, a, VMM_PRESENT | VMM_WRITABLE);
    for (int i = 0; i < n; i++) okunan[i] = 0;
    smp_cagir_digerleri(sinama_oku, TLB_SINAMA_SANAL);      /* AP'ler A'yi onbellege alir */
    for (int i = 1; i < n; i++) if (okunan[i] != IA) hata++;

    /* Kontrol grubu: uzak vurus KAPALI iken yeniden haritala. */
    tlb_uzak_kapali = 1;
    vmm_map_page(TLB_SINAMA_SANAL, b, VMM_PRESENT | VMM_WRITABLE);
    tlb_uzak_kapali = 0;
    if (*(volatile uint64_t *)(uintptr_t)TLB_SINAMA_SANAL != IB) hata++;   /* BSP yerel invlpg */
    smp_cagir_digerleri(sinama_oku, TLB_SINAMA_SANAL);
    for (int i = 1; i < n; i++) if (okunan[i] == IA) bayat++;

    /* Vurus: artik her cekirdek B'yi gormeli. */
    tlb_uzak_vur(TLB_SINAMA_SANAL);
    smp_cagir_digerleri(sinama_oku, TLB_SINAMA_SANAL);
    for (int i = 1; i < n; i++) if (okunan[i] != IB) hata++;

    vmm_unmap_page(TLB_SINAMA_SANAL);   /* bu da uzak vurus yapar */
    pmm_free_frame(a); pmm_free_frame(b);

    satir_t s = { {0}, 0 };
    s_ekle(&s, hata ? "SMP: [HATA] TLB vurusu sinamasi KALDI" : "SMP: TLB vurusu sinamasi GECTI");
    s_ekle(&s, " (AP="); s_ondalik(&s, (uint64_t)(n - 1));
    s_ekle(&s, ", vurussuz bayat okuyan="); s_ondalik(&s, (uint64_t)bayat);
    s_ekle(&s, ", hata="); s_ondalik(&s, (uint64_t)hata);
    s_ekle(&s, ")\n");
    serial_write(s.b);
}

/* =================== LAPIC zamanlayicisi (AP tiki) ===================
 * Vektor 0xEF, periyodik. Sayim BSP'de PIT kanal 2 ile olculur ve PIT
 * (IRQ0) ile ayni periyoda ayarlanir. Yalniz AP'ler kullanir; BSP'nin
 * tiki PIT'ten gelmeye devam eder. */
#define LAPIC_LVT_ZAMAN   0x320
#define LAPIC_ZAMAN_ILK   0x380
#define LAPIC_ZAMAN_SIMDI 0x390
#define LAPIC_ZAMAN_BOL   0x3E0
#define SMP_ZAMAN_VEKTOR  0xEF
#define SMP_TIK_HZ        50      /* timer_init(50) ile ayni */

static void lapic_zamanlayici_olc(void) {
    lapic_yaz(LAPIC_ZAMAN_BOL, 0x3);                 /* /16 */
    lapic_yaz(LAPIC_LVT_ZAMAN, 0x00010000 | SMP_ZAMAN_VEKTOR);   /* maskeli, tek atim */
    lapic_yaz(LAPIC_ZAMAN_ILK, 0xFFFFFFFFu);
    pit2_bekle_us(20000);
    uint32_t gecen = 0xFFFFFFFFu - lapic_oku(LAPIC_ZAMAN_SIMDI);
    lapic_yaz(LAPIC_ZAMAN_ILK, 0);
    /* 20 ms -> bir tik periyodu (1000/HZ ms) */
    uint64_t sayim = (uint64_t)gecen * (1000 / SMP_TIK_HZ) / 20;
    if (sayim < 1000) sayim = 1000;
    if (sayim > 0xFFFFFFFFULL) sayim = 0xFFFFFFFFULL;
    lapic_tik_sayim = (uint32_t)sayim;
}

static void lapic_zamanlayici_kur(void) {
    lapic_yaz(LAPIC_ZAMAN_BOL, 0x3);
    lapic_yaz(LAPIC_LVT_ZAMAN, 0x00020000 | SMP_ZAMAN_VEKTOR);   /* periyodik */
    lapic_yaz(LAPIC_ZAMAN_ILK, lapic_tik_sayim);
}

/* interrupt.asm: lapic_zaman_stub -> burasi. EOI ONCE: scheduler_tick
 * baska goreve gecip buraya uzun sure donmeyebilir (bkz. irq_handler). */
void smp_zaman_isle(void *cerceve) {
    (void)cerceve;
    lapic_eoi();
    cpu_bu()->yerel_tik++;
    scheduler_tick();
}

/* kernel.c: zamanlayici ve gorevler hazir olunca cagrilir. */
void smp_dagitimi_ac(void) {
    if (uyanan <= 1) return;
    __atomic_store_n(&dagitim_acik, 1, __ATOMIC_RELEASE);
    for (int i = 1; i < uyanan && i < SMP_AZAMI; i++)
        ipi_gonder(apic_indeks[i], SMP_CAGRI_VEKTOR);    /* hlt'den uyandir */
    for (uint64_t t = 0; __atomic_load_n(&katilan, __ATOMIC_ACQUIRE) < uyanan - 1 && t < 400000000ULL; t++)
        __asm__ volatile ("pause");
    satir_t s = { {0}, 0 };
    s_ekle(&s, "SMP: is dagitimi acik - zamanlayiciya katilan AP=");
    s_ondalik(&s, (uint64_t)katilan);
    s_ekle(&s, "/"); s_ondalik(&s, (uint64_t)(uyanan - 1));
    s_ekle(&s, " (LAPIC tik sayimi="); s_ondalik(&s, lapic_tik_sayim);
    s_ekle(&s, ")\n");
    serial_write(s.b);
}

int smp_dagitim_acik(void) { return dagitim_acik; }

int smp_bulunan(void) { return bulunan; }
int smp_uyanan(void)  { return uyanan; }
int smp_x2apic(void)  { return x2apic_kip; }

void smp_baslat(void) {
    satir_t s = { {0}, 0 };
    uint32_t a, b, c, d;
    cpuid_(1, &a, &b, &c, &d);
    if (!(d & (1u << 9))) { serial_write("SMP: yerel APIC yok - tek cekirdek\n"); return; }

    uint64_t taban = rdmsr_(MSR_APIC_BASE);
#ifdef SMP_X2APIC_ZORLA
    /* Yalniz sinama: firmware'in x2APIC kipinde biraktigi makineyi
     * (orn. bazi AM5 kartlari) taklit eder. */
    if ((c & (1u << 21)) && (taban & APIC_BASE_EN) && !(taban & APIC_BASE_EXTD)) {
        wrmsr_(MSR_APIC_BASE, taban | APIC_BASE_EXTD);
        taban = rdmsr_(MSR_APIC_BASE);
    }
#endif
    x2apic_kip = (taban & APIC_BASE_EXTD) ? 1 : 0;
    if (!(taban & APIC_BASE_EN)) {
        serial_write("SMP: yerel APIC firmware tarafindan kapatilmis - tek cekirdek\n");
        return;
    }

    int kaynak = 0;
    const uint8_t *rsdp = rsdp_bul(&kaynak);
    const uint8_t *madt = rsdp ? madt_bul(rsdp) : 0;
    if (!madt) { serial_write("SMP: ACPI MADT bulunamadi - tek cekirdek\n"); return; }

    uint64_t lapic_fiz = *(const uint32_t *)(madt + 36);
    uint32_t uz = *(const uint32_t *)(madt + 4);
    int x2_atlanan = 0;
    for (uint32_t o = 44; o + 2 <= uz; ) {
        uint8_t tur = madt[o], boy = madt[o + 1];
        if (boy < 2 || o + boy > uz) break;
        if (tur == 0 && boy >= 8) {                       /* yerel APIC */
            uint32_t bay = *(const uint32_t *)(madt + o + 4);
            if ((bay & 1) && bulunan < SMP_AZAMI) apic_kimlikleri[bulunan++] = madt[o + 3];
        } else if (tur == 9 && boy >= 16) {               /* yerel x2APIC */
            uint32_t kim = *(const uint32_t *)(madt + o + 4);
            uint32_t bay = *(const uint32_t *)(madt + o + 8);
            if (bay & 1) {
                if (x2apic_kip && bulunan < SMP_AZAMI) apic_kimlikleri[bulunan++] = kim;
                else x2_atlanan++;
            }
        } else if (tur == 5 && boy >= 12) {               /* adres gecersiz kilma */
            lapic_fiz = *(const uint64_t *)(madt + o + 4);
        }
        o += boy;
    }

    if (!x2apic_kip) {
        uint64_t msr_fiz = taban & 0x000FFFFFFFFFF000ULL;
        if (msr_fiz) lapic_fiz = msr_fiz;
        /* MMIO: onbelleksiz (PCD|PWT) 4KB sayfa. Basarisizsa kimlik
         * haritasindaki mevcut sayfa kullanilir (firmware MTRR'i UC). */
        vmm_map_page(lapic_fiz, lapic_fiz, VMM_PRESENT | VMM_WRITABLE | 0x18ULL);
        lapic_mmio = (volatile uint32_t *)(uintptr_t)lapic_fiz;
    }
    lapic_etkinlestir(1);
    uint32_t bsp = lapic_kimlik();
    apic_indeks[0] = bsp;
    idt_lapic_kapilari_kur();
    lapic_zamanlayici_olc();

    s_ekle(&s, "SMP: MADT (");
    s_ekle(&s, kaynak == 2 ? "multiboot2" : "BIOS alani");
    s_ekle(&s, ") etkin cekirdek="); s_ondalik(&s, (uint64_t)bulunan);
    s_ekle(&s, " kip="); s_ekle(&s, x2apic_kip ? "x2APIC" : "xAPIC");
    s_ekle(&s, " LAPIC="); s_onalti(&s, lapic_fiz);
    s_ekle(&s, " BSP="); s_onalti(&s, bsp);
    s_ekle(&s, "\n");
    serial_write(s.b);
    if (x2_atlanan) serial_write("SMP: xAPIC kipinde x2APIC girisleri atlandi\n");
    if (bulunan <= 1) return;

    /* Trampoline: dusuk bellek PMM'de ayrilmis (ilk 1MB), kimlik haritali. */
    uint64_t tr_boy = (uint64_t)(ap_tramp_son - ap_tramp_bas);
    uint64_t cr0, cr3, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr3 &= 0x000FFFFFFFFFF000ULL;
    if (cr3 >= 0x100000000ULL || tr_boy > 0x1000) {
        serial_write("SMP: [HATA] PML4 4GB ustunde veya trampoline cok buyuk - AP baslatilmadi\n");
        return;
    }
    volatile uint8_t *hedef = (volatile uint8_t *)(uintptr_t)TRAMP_FIZ;
    for (uint64_t i = 0; i < tr_boy; i++) hedef[i] = ap_tramp_bas[i];
    volatile tramp_veri_t *v = (volatile tramp_veri_t *)(uintptr_t)TRAMP_VERI;
    v->cr3   = cr3;
    v->efer  = rdmsr_(0xC0000080) & ~(1ULL << 10);             /* LMA salt-okunur */
    v->cr0   = cr0 & 0xFFFFFFFFULL;
    v->cr4   = (cr4 | (1ULL << 5)) & ~((1ULL << 17) | (1ULL << 12));  /* +PAE -PCIDE -LA57 */
    v->giris = (uint64_t)(uintptr_t)ap_giris;

    int sira = 1;
    for (int i = 0; i < bulunan; i++) {
        uint32_t kim = apic_kimlikleri[i];
        if (kim == bsp) continue;

        uint8_t *yigin = (uint8_t *)kmalloc(AP_YIGIN);
        if (!yigin) { serial_write("SMP: [HATA] AP yigini ayrilamadi\n"); break; }
        v->yigin = ((uint64_t)(uintptr_t)(yigin + AP_YIGIN)) & ~0xFULL;
        ap_yigin_ust = v->yigin;
        apic_indeks[sira] = kim;
        v->arg   = (uint64_t)sira;
        __atomic_store_n(&ap_isareti, -1, __ATOMIC_SEQ_CST);

        /* INIT, 10ms, SIPI; gelmezse ikinci SIPI (Intel MP tavsiyesi). */
        int hata = ipi_gonder(kim, 0x00004500);
        pit2_bekle_us(10000);
        hata |= ipi_gonder(kim, 0x00004600 | (uint32_t)(TRAMP_FIZ >> 12));
        for (int t = 0; t < 20 && __atomic_load_n(&ap_isareti, __ATOMIC_ACQUIRE) != sira; t++)
            pit2_bekle_us(100);
        if (__atomic_load_n(&ap_isareti, __ATOMIC_ACQUIRE) != sira) {
            hata |= ipi_gonder(kim, 0x00004600 | (uint32_t)(TRAMP_FIZ >> 12));
            for (int t = 0; t < 100 && __atomic_load_n(&ap_isareti, __ATOMIC_ACQUIRE) != sira; t++)
                pit2_bekle_us(10000);                         /* <= 1 sn */
        }
        if (__atomic_load_n(&ap_isareti, __ATOMIC_ACQUIRE) != sira) {
            satir_t u = { {0}, 0 };
            s_ekle(&u, "SMP: [UYARI] APIC="); s_onalti(&u, kim);
            s_ekle(&u, hata ? " IPI teslim edilemedi\n" : " yanit vermedi\n");
            serial_write(u.b);
            /* Gec uyanan AP bu yigini kullanabilir veya bir sonraki
             * AP'nin verisini okuyabilir. INIT onu SIPI-bekleme
             * durumuna geri iter; ancak ondan sonra yigin iade edilir. */
            ipi_gonder(kim, 0x00004500);
            pit2_bekle_us(10000);
            kfree(yigin);
            continue;
        }
        sira++;
    }

    tlb_sinamasi();

    satir_t son = { {0}, 0 };
    s_ekle(&son, "SMP: "); s_ondalik(&son, (uint64_t)uyanan);
    s_ekle(&son, "/"); s_ondalik(&son, (uint64_t)bulunan);
    s_ekle(&son, " cekirdek calisiyor (AP'ler hazir)\n");
    serial_write(son.b);
}
