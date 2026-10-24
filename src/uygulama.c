/* =====================================================================
 * 1.4.8 — Masaüstü uygulama yükleyicisi (bkz. include/uygulama.h)
 *
 * Zincir: rıhtım/üst çubuk tıklaması -> uygulama_ac -> process_create
 * (YENİ adres alanı) -> süreç kendi bağlamında: diskten ELF oku (VFS)
 * -> elf_load (kendi PML4'üne) -> yığın -> ring3_gir(AÇIK profil).
 * Uygulama çıkınca (SYS_EXIT) reap: pencere, TÖRE kaydı, adres alanı
 * geri alınır (sched.c). UMAY ve kabuğun yükleme yolu DEĞİŞMEDİ.
 * ===================================================================== */
#include "uygulama.h"
#include "process.h"
#include "sched.h"
#include "elf.h"
#include "vfs.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "timur.h"
#include "tore.h"
#include "ring3.h"
#include "ulgen.h"
#include "serial.h"
#include "log.h"
#include "spinlock.h"
#include "otukenconf.h"

/* Uygulamalar kendi adres alanlarında: hepsi aynı sanal yerleşimi
 * kullanabilir (userprog/user.ld 0x8000400000). Yığın, heap penceresinin
 * (0x8001000000) altında. */
#define UYG_YIGIN_VIRT   0x8000E00000ULL
#define UYG_YIGIN_SAYFA  16                /* 64 KB (1.4.9: TLS/sertifika dogrulamasi) */
#define UYG_AZAMI_BOYUT  (1024 * 1024)     /* dosya boyu; .bss bunu etkilemez */

typedef struct {
    const char *ad;        /* süreç adı (16'ya sığar) */
    const char *yol;       /* diskte 8.3 */
    uint32_t    profil;    /* TÖRE: yalnız gerekenler */
} uyg_tanim_t;

static const uyg_tanim_t UYG[UYG_SAYI] = {
    [UYG_SISTEM]  = { "Sistem",  "/SISTEM.ELF",  TORE_PROFILE_SISTEM_UYG  },
    [UYG_GORUNUM] = { "Gorunum", "/GORUNUM.ELF", TORE_PROFILE_GORUNUM_UYG },
    [UYG_AGSINA]  = { "AgSinama", "/AGSINA.ELF", TORE_PROFILE_AGSINA_UYG  },
    [UYG_TULPAR]  = { "Tulpar",   "/TULPAR.ELF", TORE_PROFILE_TULPAR_UYG  },
    [UYG_KERVAN]  = { "Kervan",   "/KERVAN.ELF", TORE_PROFILE_KERVAN_UYG  },
    [UYG_ULAK]    = { "Ulak",     "/ULAK.ELF",   TORE_PROFILE_ULAK_UYG    },
    [UYG_DEDE]    = { "Dede",     "/DEDE.ELF",   TORE_PROFILE_DEDE_UYG    },
    [UYG_TORE]    = { "Tore",     "/TORE.ELF",   TORE_PROFILE_TORE_UYG    },
    [UYG_TIMUR]   = { "Timur",    "/TIMUR.ELF",  TORE_PROFILE_TIMUR_UYG   },
    [UYG_KUT]     = { "Kut",      "/KUT.ELF",    TORE_PROFILE_KUT_UYG     },
    [UYG_ASENA]   = { "Asena",    "/ASENA.ELF",  TORE_PROFILE_ASENA_UYG   },
    [UYG_ERGENEKON] = { "Ergenekon", "/ERGENEKO.ELF", TORE_PROFILE_ERGENEKON_UYG },
    [UYG_TAMGA]   = { "Tamga",    "/TAMGA.ELF",  TORE_PROFILE_TAMGA_UYG   },
};

/* 1.4.9: KERVAN paketlerinin uygulamaları: UYG_SAYI'dan sonraki yuvalar.
 * Yol çalışırken atanır; profil HER ZAMAN en dar paket profilidir (paket
 * imzalı olsa da kendi yetkisini seçemez). */
#define UYG_TOPLAM (UYG_SAYI + UYG_PAKET_YUVA)
static char paket_yol[UYG_PAKET_YUVA][16];

static ozspin_t uyg_kilit = OZSPIN_BASLANGIC;
static uint32_t uyg_pid[UYG_TOPLAM];
static uint32_t uyg_gorev[UYG_TOPLAM];
static uint32_t uyg_baslatma[UYG_TOPLAM];   /* başarılı süreç oluşturma sayısı */

static void uyg_yaz(const char *a, const char *b) {
    serial_write("UYGULAMA: ");
    serial_write(a);
    serial_write(b);
    serial_write("\n");
}

/* Süreç kendi bağlamında (kendi CR3'ü etkin) çalışır; GERİ DÖNMEZ. */
static void uyg_calistir(int u) {
    if (u >= UYG_SAYI) uygulama_elf_calistir(paket_yol[u - UYG_SAYI], TORE_PROFILE_PAKET_UYG);
    uygulama_elf_calistir(UYG[u].yol, UYG[u].profil);
}

/* diskteki ELF'i bu süreçte (kendi adres alanında) Ring3'te başlatır; GERİ DÖNMEZ */
void uygulama_elf_calistir(const char *yol, uint32_t profil) {
    struct { const char *yol; uint32_t profil; } tt = { yol, profil }, *t = &tt;
    int fd = vfs_open(t->yol);
    if (fd < 0) { uyg_yaz("diskte yok: ", t->yol); process_exit_current(); }
    uint8_t *tampon = (uint8_t *)kmalloc(UYG_AZAMI_BOYUT);
    if (!tampon) { vfs_close(fd); uyg_yaz("bellek yok: ", t->yol); process_exit_current(); }
    uint32_t toplam = 0;
    int tasti = 0;
    for (;;) {
        if (toplam + 4096 > UYG_AZAMI_BOYUT) {
            uint8_t s1[1];
            if (vfs_read(fd, s1, 1) > 0) tasti = 1;
            break;
        }
        int n = vfs_read(fd, tampon + toplam, 4096);
        if (n <= 0) break;
        toplam += (uint32_t)n;
    }
    vfs_close(fd);
    elf_image_t img;
    int hata = tasti || elf_load(tampon, toplam, &img) != 0;
    kfree(tampon);
    if (hata) { uyg_yaz(tasti ? "ikili siniri asiyor: " : "ELF yuklenemedi: ", t->yol); process_exit_current(); }

    for (int i = 0; i < UYG_YIGIN_SAYFA; i++) {
        uint64_t v = UYG_YIGIN_VIRT + (uint64_t)i * PMM_FRAME_SIZE;
        uint64_t f = pmm_alloc_frame();
        if (!f) { uyg_yaz("yigin yok: ", t->yol); process_exit_current(); }
        if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            pmm_free_frame(f);
            uyg_yaz("yigin haritalanamadi: ", t->yol);
            process_exit_current();
        }
        timur_protect_data(v);
    }
    serial_write("UYGULAMA: ");
    serial_write(t->yol);
    serial_write(" Ring3'e geciyor (kendi adres alani PML4=");
    serial_write_hex((uint32_t)vmm_current_address_space());
    serial_write(")\n");
    ring3_gir(img.entry_point,
              UYG_YIGIN_VIRT + (uint64_t)UYG_YIGIN_SAYFA * PMM_FRAME_SIZE - 64, t->profil);
}

static void giris_sistem(void)  { uyg_calistir(UYG_SISTEM); }
static void giris_gorunum(void) { uyg_calistir(UYG_GORUNUM); }
static void giris_agsina(void)  { uyg_calistir(UYG_AGSINA); }
static void giris_tulpar(void)  { uyg_calistir(UYG_TULPAR); }
static void giris_kervan(void)  { uyg_calistir(UYG_KERVAN); }
static void giris_ulak(void)    { uyg_calistir(UYG_ULAK); }
static void giris_dede(void)    { uyg_calistir(UYG_DEDE); }
static void giris_tore(void)    { uyg_calistir(UYG_TORE); }
static void giris_timur(void)   { uyg_calistir(UYG_TIMUR); }
static void giris_kut(void)     { uyg_calistir(UYG_KUT); }
static void giris_asena(void)   { uyg_calistir(UYG_ASENA); }
static void giris_ergenekon(void) { uyg_calistir(UYG_ERGENEKON); }
static void giris_tamga(void)   { uyg_calistir(UYG_TAMGA); }
static void giris_paket0(void)  { uyg_calistir(UYG_SAYI + 0); }
static void giris_paket1(void)  { uyg_calistir(UYG_SAYI + 1); }
static void giris_paket2(void)  { uyg_calistir(UYG_SAYI + 2); }
static void giris_paket3(void)  { uyg_calistir(UYG_SAYI + 3); }
_Static_assert(UYG_PAKET_YUVA == 4, "giris_paketN sayisi");
static void (*const GIRIS[UYG_TOPLAM])(void) = { giris_sistem, giris_gorunum, giris_agsina, giris_tulpar, giris_kervan,
                                                giris_ulak, giris_dede, giris_tore, giris_timur, giris_kut,
                                                giris_asena, giris_ergenekon, giris_tamga,
                                                giris_paket0, giris_paket1, giris_paket2, giris_paket3 };

static int pencere_bul(uint32_t gorev) {
    for (int id = 0; id < ULGEN_MAX_YUZEY; id++) {
        uint32_t sahip;
        if (ulgen_yuzey_sahibi(id, &sahip) == 0 && sahip == gorev) return id;
    }
    return -1;
}

int uygulama_ac(int u) {
    if (u < 0 || u >= UYG_TOPLAM) return -1;
    if (u == UYG_AGSINA && !OTUKEN_TANI_MODU) return -1;   /* sınama programı üretimde açılmaz */
    ozspin_al(&uyg_kilit);
    if (uyg_pid[u] && process_find(uyg_pid[u])) {
        /* Açık: penceresi varsa öne; yoksa hâlâ açılıyor (ya da kapanıyor).
         * Pencere araması UYUYAN görsel kilidi alır: dönen kilit
         * bırakıldıktan SONRA yapılır. */
        uint32_t gorev = uyg_gorev[u];
        ozspin_birak(&uyg_kilit);
        int id = pencere_bul(gorev);
        if (id >= 0) { ulgen_gorunurluk(id, 1); ulgen_one_getir(id); }
        uyg_yaz("zaten acik, one getirildi: ", uygulama_adi(u));
        return 0;
    }
    process_t *p = process_create(u < UYG_SAYI ? UYG[u].ad : "Paket", GIRIS[u]);
    if (!p) {
        uyg_pid[u] = 0;
        ozspin_birak(&uyg_kilit);
        log_write(LOG_UYARI, "ULGEN", "uygulama sureci olusturulamadi");
        return -1;
    }
    uyg_pid[u] = p->pid;
    uyg_gorev[u] = p->task ? p->task->id : 0;
    uyg_baslatma[u]++;
    ozspin_birak(&uyg_kilit);
    uyg_yaz("baslatildi: ", uygulama_adi(u));
    return 1;
}

/* 1.4.9: kurulu paketin uygulaması ("MERHABA.ELF"). Aynı dosya açıksa öne
 * getirilir; değilse boş (çalışmayan) bir yuva alınır. Yuva / -1 */
int uygulama_paket_ac(const char *dosya) {
    char yol[16]; yol[0] = '/';
    int n = 0; while (dosya[n] && n < 13) { yol[n + 1] = dosya[n]; n++; } yol[n + 1] = 0;
    int bos = -1, ayni = -1;
    ozspin_al(&uyg_kilit);
    for (int k = 0; k < UYG_PAKET_YUVA; k++) {
        int u = UYG_SAYI + k;
        int calisiyor = uyg_pid[u] && process_find(uyg_pid[u]);
        int esit = 1; for (int i = 0; i < 16 && (paket_yol[k][i] || yol[i]); i++) if (paket_yol[k][i] != yol[i]) { esit = 0; break; }
        if (esit && paket_yol[k][0]) { ayni = k; break; }
        if (!calisiyor && bos < 0) bos = k;
    }
    int k = ayni >= 0 ? ayni : bos;
    if (k >= 0 && ayni < 0) for (int i = 0; i < 16; i++) paket_yol[k][i] = yol[i];
    ozspin_birak(&uyg_kilit);
    if (k < 0) { log_write(LOG_UYARI, "ULGEN", "paket uygulamasi icin bos yuva yok"); return -1; }
    return uygulama_ac(UYG_SAYI + k) < 0 ? -1 : UYG_SAYI + k;
}

uint32_t uygulama_pid(int u) {
    if (u < 0 || u >= UYG_TOPLAM) return 0;
    uint32_t p = uyg_pid[u];
    return (p && process_find(p)) ? p : 0;
}
uint32_t uygulama_gorev(int u) { return uygulama_pid(u) ? uyg_gorev[u] : 0; }
int uygulama_penceresi(int u) {
    uint32_t g = uygulama_gorev(u);
    return g ? pencere_bul(g) : -1;
}
uint32_t uygulama_baslatma_sayisi(int u) {
    return (u >= 0 && u < UYG_TOPLAM) ? __atomic_load_n(&uyg_baslatma[u], __ATOMIC_ACQUIRE) : 0;
}
const char *uygulama_adi(int u) {
    if (u >= 0 && u < UYG_SAYI) return UYG[u].ad;
    if (u >= UYG_SAYI && u < UYG_TOPLAM) return paket_yol[u - UYG_SAYI] + 1;
    return "?";
}
