/* =====================================================================
 * ÖTÜKEN taşıma katmanı — UMAY'ın ÖTÜKEN'e bakan TEK yüzeyi.
 *
 * ÖTÜKEN'in GERÇEK ABI'sine göre yazılmıştır:
 *   include/syscall.h  — çağrı numaraları (0..35)
 *   userprog/ulib.c    — kayıt düzeni (RAX=no, RDI/RSI/RDX/R10=arg)
 *
 * DİKKAT: dördüncü argüman R10'a SABİTLENİR. SYSCALL komutu RCX'i
 * dönüş adresi için kullandığından çekirdek dördüncü argümanı R10'dan
 * okur; genel "r" kısıtı verilirse derleyici başka kayıt seçer ve
 * çekirdeğe çöp değer gider. (Bu gerçekten yaşanmış bir hataydı.)
 *
 * Derleme: -DUMAY_OTUKEN -mcmodel=large
 * ===================================================================== */
#include "umay_port.h"
#include "umay_str.h"
#include "umay_us.h"

#ifdef UMAY_OTUKEN

enum {
    SYS_WRITE = 0, SYS_GETPID = 1, SYS_YIELD = 2, SYS_SLEEP = 3,
    SYS_RING3_PROOF = 4, SYS_EXIT = 5, SYS_OPEN = 6, SYS_READ = 7,
    SYS_CLOSE = 8, SYS_SBRK = 9, SYS_READKEY = 10, SYS_LISTDIR = 11,
    SYS_PUTPIXEL = 12, SYS_TICKS = 13, SYS_PENCERE_AC = 14,
    SYS_PEN_DOLDUR = 15, SYS_PEN_YAZI = 16, SYS_PEN_DIK = 17,
    SYS_PEN_KAPAT = 18, SYS_SERVIS_LISTE = 19, SYS_IPC_ACS = 20,
    SYS_IPC_BUL = 21, SYS_IPC_GONDER = 22, SYS_IPC_AL = 23,
    SYS_SISTEM_BIL = 24, SYS_WRITE_FILE = 25, SYS_STAT = 26,
    SYS_MKDIR = 27, SYS_YETKI = 28, SYS_PROC_LISTE = 29,
    SYS_PEN_SUN = 30, SYS_PEN_OLAY = 31,
    SYS_SES_NOTA = 32, SYS_OZAN_SOYLE = 33, SYS_DEDE_KONUS = 35
};

/* ÖTÜKEN TÖRE yetenek bitleri (include/tore.h) */
enum {
    KUT_DISK_READ = 1u << 0, KUT_DISK_WRITE = 1u << 1,
    KUT_NET_SEND  = 1u << 2, KUT_NET_RECV   = 1u << 3,
    KUT_MEM_ALLOC = 1u << 4, KUT_PROC_CREATE= 1u << 5,
    KUT_PROC_KILL = 1u << 6, KUT_HW_DIRECT  = 1u << 7
};

static inline int64_t sys(int64_t no, int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t sonuc;
    register int64_t r10 __asm__("r10") = d;
    __asm__ __volatile__("syscall"
        : "=a"(sonuc)
        : "a"(no), "D"(a), "S"(b), "d"(c), "r"(r10)
        : "rcx", "r11", "memory");
    return sonuc;
}

/* Tik süresi ÇEKİRDEKTEN okunur (SYS_SISTEM_BIL / SYSBIL_TIK_HZ).
 * UYUM düzeltmesi: burada eskiden sabit 10 ms yazıyordu; ÖTÜKEN PIT'i
 * 50 Hz'de (20 ms) kurar. UMAY'ın saati yarı hızda işliyor, her
 * beklemesi iki kat uzun sürüyordu. Eski çekirdek alanı bilmez (0
 * döner) -> ÖTÜKEN varsayılanı 50 Hz. */
#define SYSBIL_TIK_HZ 4
static uint32_t g_tik_ms = 0;
static uint32_t tik_ms(void) {
    if (!g_tik_ms) {
        int64_t hz = sys(SYS_SISTEM_BIL, SYSBIL_TIK_HZ, 0, 0, 0);
        if (hz <= 0 || hz > 1000) hz = 50;
        g_tik_ms = (uint32_t)(1000 / hz);
        if (!g_tik_ms) g_tik_ms = 1;
    }
    return g_tik_ms;
}
#define TIK_MS (tik_ms())

uint64_t up_ms(void)         { return (uint64_t)sys(SYS_TICKS, 0, 0, 0, 0) * TIK_MS; }
void     up_uyu(uint32_t ms) { sys(SYS_SLEEP, (ms + TIK_MS - 1) / TIK_MS, 0, 0, 0); }
void     up_birak(void)      { sys(SYS_YIELD, 0, 0, 0, 0); }

void up_gunluk(const char *satir) {
    sys(SYS_WRITE, (int64_t)(uintptr_t)satir, 0, 0, 0);
    sys(SYS_WRITE, (int64_t)(uintptr_t)"\n", 0, 0, 0);
}

/* SBRK yalnız büyür; ÖTÜKEN'de serbest bırakma yok. UMAY durağan
 * diziler kullandığından öbek açılışta bir kez istenir. */
/* --- Öbek: SBRK üstünde serbest listeli ayırıcı ---
 * ÖTÜKEN'de bellek İADE çağrısı yoktur; SBRK yalnız büyür. Eskiden
 * up_biraktir boştu, yani bırakılan her blok sızıyordu. Artık bloklar
 * kendi başlıklarını taşır ve bırakılınca serbest listeye girer;
 * sonraki istek önce listeden karşılanır. Çekirdekten yeni sayfa
 * yalnız liste yetmediğinde istenir. */
typedef struct blok {
    uint64_t     boy;        /* başlık hariç kullanılabilir bayt */
    struct blok *sonraki;    /* serbest listede bir sonraki      */
    uint32_t     serbest;
    uint32_t     imza;
} blok_t;

#define BLOK_IMZA 0x554D4159u   /* 'UMAY' */

static blok_t *g_serbest = 0;

void *up_ayir(uint64_t n) {
    if (n == 0) return 0;
    n = (n + 15) & ~(uint64_t)15;          /* 16 bayta hizala */

    /* 1) Serbest listede yeterince büyük ilk bloğu ara. */
    blok_t *onceki = 0, *b = g_serbest;
    while (b) {
        if (b->boy >= n) {
            if (onceki) onceki->sonraki = b->sonraki;
            else        g_serbest = b->sonraki;
            b->serbest = 0;
            b->sonraki = 0;
            return (void *)(b + 1);
        }
        onceki = b;
        b = b->sonraki;
    }

    /* 2) Yoksa çekirdekten yeni yer iste. */
    int64_t p = sys(SYS_SBRK, (int64_t)(n + sizeof(blok_t)), 0, 0, 0);
    if (p <= 0) return 0;
    b = (blok_t *)(uintptr_t)p;
    b->boy = n;
    b->sonraki = 0;
    b->serbest = 0;
    b->imza = BLOK_IMZA;
    return (void *)(b + 1);
}

void up_biraktir(void *p) {
    if (!p) return;
    blok_t *b = ((blok_t *)p) - 1;
    /* İmza denetimi: yanlış işaretçi listeyi bozmasın. */
    if (b->imza != BLOK_IMZA || b->serbest) return;
    b->serbest = 1;
    b->sonraki = g_serbest;
    g_serbest = b;
}

up_tut_t up_ac(const char *yol, uint32_t kip) {
    if (kip & (UP_YAZ | UP_YARAT | UP_EKLE)) return UP_YAZ_TUTAMAK;
    return (up_tut_t)sys(SYS_OPEN, (int64_t)(uintptr_t)yol, 0, 0, 0);
}

int64_t up_oku(up_tut_t t, void *tampon, uint64_t n) {
    if (t == UP_YAZ_TUTAMAK) return -1;
    return sys(SYS_READ, t, (int64_t)(uintptr_t)tampon, (int64_t)n, 0);
}

int64_t up_yaz(up_tut_t t, const void *tampon, uint64_t n) {
    if (t == 1) {                                  /* 1 = konsol */
        char gec[256];
        uint64_t k = n < sizeof gec - 1 ? n : sizeof gec - 1;
        um_bellek_kopya(gec, tampon, k);
        gec[k] = 0;
        sys(SYS_WRITE, (int64_t)(uintptr_t)gec, 0, 0, 0);
        return (int64_t)k;
    }
    return UM_DESTEKSIZ;                           /* yol tabanlı yazım kullan */
}

int up_yaz_yola(const char *yol, const void *tampon, uint64_t n) {
    return (int)sys(SYS_WRITE_FILE, (int64_t)(uintptr_t)yol,
                    (int64_t)(uintptr_t)tampon, (int64_t)n, 0);
}

int up_kapat(up_tut_t t) {
    if (t == UP_YAZ_TUTAMAK) return UM_OLDU;
    return (int)sys(SYS_CLOSE, t, 0, 0, 0);
}

typedef struct { uint64_t boyut; uint32_t dizin_mi; uint32_t ayrilmis; } up_stat_t;

int up_var_mi(const char *yol) {
    up_stat_t s;
    return sys(SYS_STAT, (int64_t)(uintptr_t)yol, (int64_t)(uintptr_t)&s, 0, 0) == 0;
}

int64_t up_boy(const char *yol) {
    up_stat_t s;
    if (sys(SYS_STAT, (int64_t)(uintptr_t)yol, (int64_t)(uintptr_t)&s, 0, 0) != 0) return -1;
    return (int64_t)s.boyut;
}

int up_dizin_yarat(const char *yol) {
    int64_t r = sys(SYS_MKDIR, (int64_t)(uintptr_t)yol, 0, 0, 0);
    return (r == 0) ? UM_OLDU : (r == -2 ? UM_DESTEKSIZ : UM_YANLIS);
}

up_tut_t up_kapi_ac(const char *ad)    { return (up_tut_t)sys(SYS_IPC_ACS, (int64_t)(uintptr_t)ad, 0, 0, 0); }
up_tut_t up_kapi_bagla(const char *ad) { return (up_tut_t)sys(SYS_IPC_BUL, (int64_t)(uintptr_t)ad, 0, 0, 0); }

#define IPC_YUK 96                     /* ÖTÜKEN ipc_message_t yükü */

int up_gonder(up_tut_t t, const void *v, uint32_t n) {
    /* Uzun ileti parçalanır: opkod 1 = devamı var, 0 = son parça. */
    const uint8_t *p = (const uint8_t *)v;
    uint32_t kalan = n, yollanan = 0;
    while (kalan > 0) {
        uint32_t parca = kalan > IPC_YUK ? IPC_YUK : kalan;
        /* UYUM düzeltmesi: hedef kuyruğu (8 ileti) doluysa çekirdek
         * IPC_DOLU (-2) döner. Eskiden ilk hatada vazgeçiliyordu; uzun
         * yanıt (>8 parça) karşı taraf okurken bile YARIM kalıyordu.
         * Artık karşı taraf boşaltana dek en çok ~2 sn beklenir. */
        int64_t r;
        uint32_t bekledi = 0;
        for (;;) {
            r = sys(SYS_IPC_GONDER, t, (kalan > parca) ? 1 : 0,
                    (int64_t)(uintptr_t)(p + yollanan), parca);
            if (r != -2 || bekledi >= 2000) break;
            up_uyu(TIK_MS);
            bekledi += TIK_MS;
        }
        if (r < 0) return (int)r;
        yollanan += parca;
        kalan -= parca;
    }
    return (int)yollanan;
}

/* Tutamak basina son gonderen. UYUM düzeltmesi: eskiden 0 'bilinmiyor'
 * sayılıyordu ama 0 GEÇERLİ bir port kimliğidir. Çekirdek artık gerçek
 * göndereni yazar; portu olmayan gönderen IPC_GONDEREN_YOK
 * (0xFFFFFFFF) taşır. Port tablosu küçüktür (<256). */
#define GONDEREN_YUVA 32
#define GONDEREN_YOK  0xFFFFFFFFu
static uint32_t g_son_gonderen[GONDEREN_YUVA];
static int      g_gonderen_hazir = 0;

up_tut_t up_yanit_kapisi(up_tut_t kendi) {
    if (kendi >= 0 && kendi < GONDEREN_YUVA && g_gonderen_hazir &&
        g_son_gonderen[kendi] < 256 && (up_tut_t)g_son_gonderen[kendi] != kendi)
        return (up_tut_t)g_son_gonderen[kendi];
    return kendi;
}

int up_al(up_tut_t t, void *v, uint32_t n, uint32_t bekle_ms) {
    struct { uint32_t opkod, uzunluk, gonderen, sira; uint8_t yuk[IPC_YUK]; } m;
    uint32_t gecen = 0;
    for (;;) {
        if (sys(SYS_IPC_AL, t, (int64_t)(uintptr_t)&m, 0, 0) > 0) {
            if (!g_gonderen_hazir) {
                for (int i = 0; i < GONDEREN_YUVA; i++) g_son_gonderen[i] = GONDEREN_YOK;
                g_gonderen_hazir = 1;
            }
            if (t >= 0 && t < GONDEREN_YUVA) g_son_gonderen[t] = m.gonderen;
            uint32_t k = m.uzunluk < n ? m.uzunluk : n;
            um_bellek_kopya(v, m.yuk, k);
            return (int)k;
        }
        if (gecen >= bekle_ms) return 0;
        up_uyu(TIK_MS);
        gecen += TIK_MS;
    }
}

int up_surec_listele(up_surec_t *dizi, int en_cok) {
    struct { uint32_t pid; char ad[16]; uint32_t durum, bellek_kb; } gec[32];
    int adet = en_cok > 32 ? 32 : en_cok;
    int64_t n = sys(SYS_PROC_LISTE, (int64_t)(uintptr_t)gec, adet, 0, 0);
    if (n < 0) return 0;
    for (int i = 0; i < (int)n; i++) {
        dizi[i].pid = (int32_t)gec[i].pid;
        um_kopya(dizi[i].ad, sizeof dizi[i].ad, gec[i].ad);
        dizi[i].bellek_kb = (int32_t)gec[i].bellek_kb;
        dizi[i].durum = gec[i].durum;
    }
    return (int)n;
}

int up_yetkim_var_mi(up_yetki_t y) {
    uint32_t kut;
    switch (y) {
    case YET_DOSYA_OKU: kut = KUT_DISK_READ;   break;
    case YET_DOSYA_YAZ: kut = KUT_DISK_WRITE;  break;
    case YET_AG:        kut = KUT_NET_SEND;    break;
    case YET_SUREC_GOR: kut = KUT_PROC_CREATE; break;
    case YET_SUREC_DUR: kut = KUT_PROC_KILL;   break;
    default:            return 0;              /* ses: ÖTÜKEN'de yok */
    }
    return sys(SYS_YETKI, (int64_t)kut, 0, 0, 0) == 1;
}

/* Gerçek çıkarım motoru: ÖTÜKEN IPC arka ucu. Model UMAY'ın içinde
 * DEĞİL, ayrı bir serviste durur; burada yalnızca arka uç seçilir.
 * Servis yoksa motorun hazirla() çağrısı başarısız döner ve US
 * katmanı stub'a düşer — sahte başarı üretilmez. */
struct umay_us *up_us_motoru(void) { return (struct umay_us *)us_yerel_motor(); }

/* ---------------- ÜLGEN ---------------- */
struct up_pencere { int32_t id; int en, boy; };
static struct up_pencere g_pencere;

#define PAKET(a, b) ((int64_t)(((uint32_t)(a) << 16) | ((uint32_t)(b) & 0xFFFF)))

up_pencere_t *up_pencere_yarat(const char *baslik, int en, int boy) {
    int64_t id = sys(SYS_PENCERE_AC, (int64_t)(uintptr_t)baslik,
                     PAKET(400, 90), PAKET(en, boy), 0);   /* masaüstü sol sütunu kapatmasın */
    if (id < 0) return 0;
    g_pencere.id = (int32_t)id;
    g_pencere.en = en;
    g_pencere.boy = boy;
    return &g_pencere;
}

void up_pencere_yok(up_pencere_t *p) { if (p) sys(SYS_PEN_KAPAT, p->id, 0, 0, 0); }

void up_dikdortgen(up_pencere_t *p, int x, int y, int en, int boy, uint32_t renk) {
    if (p) sys(SYS_PEN_DIK, p->id, PAKET(x, y), PAKET(en, boy), (int64_t)renk);
}

void up_yazi(up_pencere_t *p, int x, int y, const char *s, uint32_t renk) {
    if (p) sys(SYS_PEN_YAZI, p->id, PAKET(x, y), (int64_t)(uintptr_t)s, (int64_t)renk);
}

void up_sun(up_pencere_t *p) { if (p) sys(SYS_PEN_SUN, p->id, 0, 0, 0); }

int up_olay_al(up_pencere_t *p, up_olay_t *o) {
    if (!p || !o) return 0;
    struct { uint32_t tur, tus; int32_t x, y; } e;
    if (sys(SYS_PEN_OLAY, p->id, (int64_t)(uintptr_t)&e, 0, 0) != 1) return 0;
    switch (e.tur) {
    case 1: o->tur = UP_OLAY_TUS; o->tus = e.tus; break;
    case 2: o->tur = UP_OLAY_TIK; o->x = e.x; o->y = e.y; break;
    case 3: o->tur = UP_OLAY_KAPAT; break;
    default: return 0;
    }
    return 1;
}

int up_metin_en(const char *s) { return (int)um_uzun(s) * 8; }

/* --- Ses --- */
void up_ozan_soyle(const char *metin, int hiz) {
    if (metin) sys(SYS_OZAN_SOYLE, (int64_t)(uintptr_t)metin, hiz, 0, 0);
}

/* Gerçek konuşma: Türkçe formant sentezi. Ses aygıtı yoksa -1 döner. */
int up_dede_konus(const char *metin) {
    if (!metin) return -1;
    return (int)sys(SYS_DEDE_KONUS, (int64_t)(uintptr_t)metin, 0, 0, 0);
}

void up_ses_nota(uint32_t hz, uint32_t ms) {
    sys(SYS_SES_NOTA, (int64_t)hz, (int64_t)ms, 0, 0);
}

#endif /* UMAY_OTUKEN */
