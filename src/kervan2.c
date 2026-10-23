/* =====================================================================
 * 1.4.9 — KERVAN v2: imzalı paket kurma / kaldırma (bkz. include/kervan2.h)
 * ===================================================================== */
#include "kervan2.h"
#include "fat16.h"
#include "fat16_ic.h"
#include "vfs.h"
#include "heap.h"
#include "log.h"
#include "serial.h"
#include "mutex.h"
#include "spinlock.h"
#include "otukenconf.h"
#include "bearssl.h"

typedef struct __attribute__((packed)) {
    char     sihir[8];            /* "KERVAN2\0" */
    uint32_t surum;               /* 2 */
    uint32_t girdi;
    char     ad[24];
    char     paket_surum[16];
    char     aciklama[96];
    uint32_t veri_boyu;
    uint32_t anahtar_no;
} k2_baslik_t;

typedef struct __attribute__((packed)) {
    char     ad[13];
    uint8_t  tur;                 /* 0 veri, 1 uygulama ELF */
    uint16_t bos;
    uint32_t ofset;
    uint32_t boy;
    uint8_t  sha256[32];
} k2_girdi_t;

_Static_assert(sizeof(k2_baslik_t) == 160, "KERVAN2 baslik");
_Static_assert(sizeof(k2_girdi_t) == 56, "KERVAN2 girdi");

/* kurulu paket kaydı (bellekte; /KERVAN.LST'ten) */
#define K2_KURULU_AZAMI 32
typedef struct {
    int  var;
    char ad[KERVAN2_AD];
    char surum[KERVAN2_SURUM];
    char dosyalar[KERVAN2_AZAMI_DOSYA][13];
    int  dosya_adet;
} k2_kurulu_t;
static k2_kurulu_t kurulu[K2_KURULU_AZAMI];
static ozkilit_t k2_kilit;

/* dizge dosyaları: hiçbir paket bunların üzerine yazamaz */
static const char *const KORUNAN[] = {
    "UMAY.ELF", "UMAY.US1", "SOZLUK.TXT", "SISTEM.ELF", "GORUNUM.ELF", "TULPAR.ELF",
    "KERVAN.ELF", "KERVAN.LST", "AGSINA.ELF", "AGSINA.CFG",
    "ULAK.ELF", "DEDE.ELF", "TORE.ELF", "TIMUR.ELF", "KUT.ELF",
    "ASENA.ELF", "ERGENEKO.ELF", "TAMGA.ELF", 0
};

/* ------------------------------------------------------------------ */
static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void kopya(char *d, int boy, const char *s) { int i = 0; while (i < boy - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static void neden_yaz(char *n, const char *m) { if (n) kopya(n, 64, m); }
static int sonu(const char *s, const char *son) {
    int a = 0, b = 0; while (s[a]) a++; while (son[b]) b++;
    return a >= b && esit(s + a - b, son);
}

/* 8.3, büyük harf, [A-Z0-9_-] */
static int ad83_gecerli(const char *a, int boy_sinir) {
    int i = 0, ana = 0, uz = 0, nokta = 0;
    for (; i < boy_sinir && a[i]; i++) {
        char c = a[i];
        if (c == '.') { if (nokta || ana == 0) return 0; nokta = 1; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
        if (nokta) { if (++uz > 3) return 0; } else if (++ana > 8) return 0;
    }
    if (i == boy_sinir) return 0;          /* NUL yok */
    return ana > 0 && (!nokta || uz > 0);
}
static int paket_adi_gecerli(const char *a, int boy_sinir) {
    int i = 0;
    for (; i < boy_sinir && a[i]; i++) {
        char c = a[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return i > 0 && i < boy_sinir;
}
static int surum_gecerli(const char *a, int boy_sinir) {
    int i = 0;
    for (; i < boy_sinir && a[i]; i++) if (!((a[i] >= '0' && a[i] <= '9') || a[i] == '.')) return 0;
    return i > 0 && i < boy_sinir;
}
static int korunan_mu(const char *ad) {
    for (int i = 0; KORUNAN[i]; i++) if (esit(ad, KORUNAN[i])) return 1;
    return sonu(ad, ".KRV");
}

/* 1.4.9: kurulu paketlerdeki uygulamalar (.ELF) — Uygulamalar menüsü bunu
 * görsel kilit altında okur: uyuyan k2_kilit yerine dönen kilitli kopya. */
static struct { char dosya[13]; char paket[KERVAN2_AD]; } uyg_ob[KERVAN2_UYG_AZAMI];
static int uyg_ob_n;
static ozspin_t uyg_ob_kilit = OZSPIN_BASLANGIC;
static void uyg_onbellek_yenile(void) {          /* k2_kilit altında çağrılır */
    ozspin_al(&uyg_ob_kilit);
    uyg_ob_n = 0;
    for (int i = 0; i < K2_KURULU_AZAMI; i++) {
        if (!kurulu[i].var) continue;
        for (int d = 0; d < kurulu[i].dosya_adet && uyg_ob_n < KERVAN2_UYG_AZAMI; d++)
            if (sonu(kurulu[i].dosyalar[d], ".ELF")) {
                kopya(uyg_ob[uyg_ob_n].dosya, 13, kurulu[i].dosyalar[d]);
                kopya(uyg_ob[uyg_ob_n].paket, KERVAN2_AD, kurulu[i].ad);
                uyg_ob_n++;
            }
    }
    ozspin_birak(&uyg_ob_kilit);
}
int kervan2_uygulama_sayisi(void) { return uyg_ob_n; }
int kervan2_uygulama_al(int i, char dosya[13], char paket[KERVAN2_AD]) {
    int r = -1;
    ozspin_al(&uyg_ob_kilit);
    if (i >= 0 && i < uyg_ob_n) {
        if (dosya) kopya(dosya, 13, uyg_ob[i].dosya);
        if (paket) kopya(paket, KERVAN2_AD, uyg_ob[i].paket);
        r = 0;
    }
    ozspin_birak(&uyg_ob_kilit);
    return r;
}

/* ------------------------------------------------------------------ */
/* doğrulama                                                           */
/* ------------------------------------------------------------------ */
static int dogrula_sinirli(const uint8_t *v, uint32_t boy, char *neden, uint32_t azami_dosya) {
    if (boy < sizeof(k2_baslik_t) + 64) { neden_yaz(neden, "paket cok kisa"); return -1; }
    const k2_baslik_t *b = (const k2_baslik_t *)v;
    static const char SIHIR[8] = { 'K', 'E', 'R', 'V', 'A', 'N', '2', 0 };
    for (int i = 0; i < 8; i++) if (b->sihir[i] != SIHIR[i]) { neden_yaz(neden, "KERVAN2 paketi degil"); return -1; }
    if (b->surum != 2) { neden_yaz(neden, "desteklenmeyen paket surumu"); return -1; }
    if (b->girdi == 0 || b->girdi > azami_dosya) { neden_yaz(neden, "gecersiz dosya sayisi"); return -1; }
    if (!paket_adi_gecerli(b->ad, sizeof b->ad)) { neden_yaz(neden, "gecersiz paket adi"); return -1; }
    if (!surum_gecerli(b->paket_surum, sizeof b->paket_surum)) { neden_yaz(neden, "gecersiz surum"); return -1; }
    if (b->aciklama[sizeof b->aciklama - 1] != 0) { neden_yaz(neden, "aciklama sonlanmamis"); return -1; }
    uint64_t veri_bas = sizeof(k2_baslik_t) + (uint64_t)b->girdi * sizeof(k2_girdi_t);
    if (veri_bas + (uint64_t)b->veri_boyu + 64 != boy) { neden_yaz(neden, "boy tutarsiz"); return -1; }

    /* 1) imza: önce, çünkü girdilere güvenmeden önce kaynağı belli olmalı */
    const uint8_t *anahtar;
    if (b->anahtar_no == 0) anahtar = KERVAN_ANAHTAR_0;
    else if (b->anahtar_no == 1 && OTUKEN_TANI_MODU) anahtar = KERVAN_ANAHTAR_1;
    else { neden_yaz(neden, "imza anahtari taninmiyor"); return -1; }
    uint8_t ozet[32];
    br_sha256_context h;
    br_sha256_init(&h);
    br_sha256_update(&h, v, boy - 64);
    br_sha256_out(&h, ozet);
    br_ec_public_key pk = { BR_EC_secp256r1, (unsigned char *)anahtar, 65 };
    if (br_ecdsa_i31_vrfy_raw(&br_ec_p256_m31, ozet, 32, &pk, v + boy - 64, 64) != 1) {
        neden_yaz(neden, "IMZA GECERSIZ");
        return -1;
    }

    /* 2) girdiler: sınırlar, adlar, yinelenme, SHA-256 */
    const k2_girdi_t *g = (const k2_girdi_t *)(v + sizeof(k2_baslik_t));
    for (uint32_t i = 0; i < b->girdi; i++) {
        if (!ad83_gecerli(g[i].ad, sizeof g[i].ad)) { neden_yaz(neden, "gecersiz dosya adi"); return -1; }
        if (g[i].tur > 1) { neden_yaz(neden, "gecersiz dosya turu"); return -1; }
        if (g[i].ofset < veri_bas || (uint64_t)g[i].ofset + g[i].boy > veri_bas + b->veri_boyu) {
            neden_yaz(neden, "dosya paket sinirinin disinda"); return -1;
        }
        for (uint32_t j = 0; j < i; j++)
            if (esit(g[i].ad, g[j].ad)) { neden_yaz(neden, "ayni dosya iki kez"); return -1; }
        br_sha256_init(&h);
        br_sha256_update(&h, v + g[i].ofset, g[i].boy);
        br_sha256_out(&h, ozet);
        for (int k = 0; k < 32; k++)
            if (ozet[k] != g[i].sha256[k]) { neden_yaz(neden, "dosya ozeti tutmuyor (bozuk)"); return -1; }
    }
    return 0;
}

int kervan2_dogrula(const uint8_t *v, uint32_t boy, char *neden) {
    return dogrula_sinirli(v, boy, neden, KERVAN2_AZAMI_DOSYA);
}

/* 1.4.9: ERGENEKON dizge imajı aynı biçimdedir (aynı imza anahtarları),
 * yalnız daha çok dosya taşır. */
int kervan2_imaj_dogrula(const uint8_t *v, uint32_t boy, char *neden) {
    return dogrula_sinirli(v, boy, neden, KERVAN2_IMAJ_AZAMI_DOSYA);
}
int kervan2_baslik(const uint8_t *v, char ad[KERVAN2_AD], char surum[KERVAN2_SURUM],
                   uint32_t *girdi, uint32_t *anahtar_no) {
    const k2_baslik_t *b = (const k2_baslik_t *)v;
    if (ad) kopya(ad, KERVAN2_AD, b->ad);
    if (surum) kopya(surum, KERVAN2_SURUM, b->paket_surum);
    if (girdi) *girdi = b->girdi;
    if (anahtar_no) *anahtar_no = b->anahtar_no;
    return 0;
}
/* DOĞRULANMIŞ paketin i. girdisi */
int kervan2_girdi(const uint8_t *v, uint32_t i, char ad[13], const uint8_t **veri,
                  uint32_t *boy, const uint8_t **sha256) {
    const k2_baslik_t *b = (const k2_baslik_t *)v;
    if (i >= b->girdi) return -1;
    const k2_girdi_t *g = (const k2_girdi_t *)(v + sizeof(k2_baslik_t)) + i;
    if (ad) kopya(ad, 13, g->ad);
    if (veri) *veri = v + g->ofset;
    if (boy) *boy = g->boy;
    if (sha256) *sha256 = g->sha256;
    return 0;
}
/* ERGENEKON biçimlemesi sonrası: disk boşaldı, kurulu paket kaydı da */
void kervan2_kilit_al(void)    { ozkilit_al(&k2_kilit); }
void kervan2_kilit_birak(void) { ozkilit_birak(&k2_kilit); }

void kervan2_kaydi_unut(void) {
    ozkilit_al(&k2_kilit);
    for (int i = 0; i < K2_KURULU_AZAMI; i++) kurulu[i].var = 0;
    uyg_onbellek_yenile();
    ozkilit_birak(&k2_kilit);
}

/* ------------------------------------------------------------------ */
/* kayıt (/KERVAN.LST)                                                 */
/* ------------------------------------------------------------------ */
static int kayit_yaz(void) {
    static char m[K2_KURULU_AZAMI * (KERVAN2_AD + KERVAN2_SURUM + KERVAN2_AZAMI_DOSYA * 14 + 4)];
    int n = 0;
    for (int i = 0; i < K2_KURULU_AZAMI; i++) {
        if (!kurulu[i].var) continue;
        for (const char *p = kurulu[i].ad; *p; p++) m[n++] = *p;
        m[n++] = ' ';
        for (const char *p = kurulu[i].surum; *p; p++) m[n++] = *p;
        for (int d = 0; d < kurulu[i].dosya_adet; d++) {
            m[n++] = ' ';
            for (const char *p = kurulu[i].dosyalar[d]; *p; p++) m[n++] = *p;
        }
        m[n++] = '\n';
    }
    if (n == 0) { m[n++] = '\n'; }
    return fat16_write_file("KERVAN.LST", (const uint8_t *)m, (uint32_t)n) < 0 ? -1 : 0;
}

static void kayit_oku(void) {
    static char m[8192];
    for (int i = 0; i < K2_KURULU_AZAMI; i++) kurulu[i].var = 0;
    int fd = vfs_open("/KERVAN.LST");
    if (fd < 0) return;
    int n = vfs_read(fd, m, sizeof m - 1);
    vfs_close(fd);
    if (n <= 0) return;
    m[n] = 0;
    int k = 0;
    char *p = m;
    while (*p && k < K2_KURULU_AZAMI) {
        char *satir = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        /* alanlar */
        char *alan[2 + KERVAN2_AZAMI_DOSYA];
        int a = 0;
        char *q = satir;
        while (*q && a < 2 + KERVAN2_AZAMI_DOSYA) {
            while (*q == ' ') q++;
            if (!*q) break;
            alan[a++] = q;
            while (*q && *q != ' ') q++;
            if (*q) *q++ = 0;
        }
        if (a < 2 || !paket_adi_gecerli(alan[0], KERVAN2_AD) || !surum_gecerli(alan[1], KERVAN2_SURUM)) continue;
        k2_kurulu_t *u = &kurulu[k];
        u->var = 1;
        kopya(u->ad, sizeof u->ad, alan[0]);
        kopya(u->surum, sizeof u->surum, alan[1]);
        u->dosya_adet = 0;
        for (int d = 2; d < a; d++)
            if (ad83_gecerli(alan[d], 13)) kopya(u->dosyalar[u->dosya_adet++], 13, alan[d]);
        k++;
    }
}

void kervan2_init(void) {
    ozkilit_al(&k2_kilit);
    kayit_oku();
    uyg_onbellek_yenile();
    int n = 0;
    for (int i = 0; i < K2_KURULU_AZAMI; i++) n += kurulu[i].var;
    ozkilit_birak(&k2_kilit);
    serial_write("KERVAN2: kayit okundu, kurulu paket=");
    serial_write_hex((uint32_t)n);
    serial_write("\n");
}

static k2_kurulu_t *kurulu_bul(const char *ad) {
    for (int i = 0; i < K2_KURULU_AZAMI; i++) if (kurulu[i].var && esit(kurulu[i].ad, ad)) return &kurulu[i];
    return 0;
}
static k2_kurulu_t *dosya_sahibi(const char *dosya) {
    for (int i = 0; i < K2_KURULU_AZAMI; i++)
        if (kurulu[i].var)
            for (int d = 0; d < kurulu[i].dosya_adet; d++)
                if (esit(kurulu[i].dosyalar[d], dosya)) return &kurulu[i];
    return 0;
}
static int dosya_var(const char *ad) {
    char yol[16]; yol[0] = '/'; kopya(yol + 1, sizeof yol - 1, ad);
    int fd = vfs_open(yol);
    if (fd < 0) return 0;
    vfs_close(fd);
    return 1;
}

/* dosyayı belleğe oku (kmalloc; çağıran serbest bırakır) */
static uint8_t *dosya_oku(const char *ad, uint32_t *boy, char *neden) {
    char yol[16]; yol[0] = '/'; kopya(yol + 1, sizeof yol - 1, ad);
    int fd = vfs_open(yol);
    if (fd < 0) { neden_yaz(neden, "paket dosyasi yok"); return 0; }
    uint8_t *t = (uint8_t *)kmalloc(KERVAN2_AZAMI_BOY + 1);
    if (!t) { vfs_close(fd); neden_yaz(neden, "bellek yok"); return 0; }
    uint32_t n = 0;
    for (;;) {
        if (n >= KERVAN2_AZAMI_BOY + 1) break;
        uint32_t iste = KERVAN2_AZAMI_BOY + 1 - n; if (iste > 4096) iste = 4096;
        int r = vfs_read(fd, t + n, iste);
        if (r <= 0) break;
        n += (uint32_t)r;
    }
    vfs_close(fd);
    if (n > KERVAN2_AZAMI_BOY) { kfree(t); neden_yaz(neden, "paket 2 MB sinirini asiyor"); return 0; }
    *boy = n;
    return t;
}

/* ------------------------------------------------------------------ */
static int kur_bellek__ic(const uint8_t *v, uint32_t boy, char *neden);
int kervan2_kur(const char *krv, char *neden) {
    if (!ad83_gecerli(krv, 13) || !sonu(krv, ".KRV")) { neden_yaz(neden, "gecersiz paket dosyasi adi"); return -1; }
    ozkilit_al(&k2_kilit);
    uint32_t boy = 0;
    int r = -1;
    uint8_t *v = dosya_oku(krv, &boy, neden);
    if (v) r = kur_bellek__ic(v, boy, neden);
    if (v) kfree(v);
    ozkilit_birak(&k2_kilit);
    return r;
}

/* 1.4.9: bellekteki paketi kurar (KERVAN ağdan indirdi). Dosya yolu ile
 * AYNI doğrulama ve kurallar. */
int kervan2_kur_bellek(const uint8_t *v, uint32_t boy, char *neden) {
    ozkilit_al(&k2_kilit);
    int r = kur_bellek__ic(v, boy, neden);
    ozkilit_birak(&k2_kilit);
    return r;
}

static int kur_bellek__ic(const uint8_t *v, uint32_t boy, char *neden) {
    int r = -1;
    if (boy > KERVAN2_AZAMI_BOY) { neden_yaz(neden, "paket 2 MB sinirini asiyor"); goto son; }
    if (kervan2_dogrula(v, boy, neden) != 0) goto son;
    const k2_baslik_t *b = (const k2_baslik_t *)v;
    const k2_girdi_t *g = (const k2_girdi_t *)(v + sizeof(k2_baslik_t));
    k2_kurulu_t *eski = kurulu_bul(b->ad);
    /* çakışma denetimi */
    for (uint32_t i = 0; i < b->girdi; i++) {
        if (korunan_mu(g[i].ad)) { neden_yaz(neden, "dizge dosyasinin uzerine yazamaz"); goto son; }
        k2_kurulu_t *sahip = dosya_sahibi(g[i].ad);
        if (sahip && sahip != eski) { neden_yaz(neden, "dosya baska pakete ait"); goto son; }
        if (!sahip && dosya_var(g[i].ad)) { neden_yaz(neden, "ayni adli dosya diskte var"); goto son; }
    }
    k2_kurulu_t *yer = eski;
    if (!yer) for (int i = 0; i < K2_KURULU_AZAMI && !yer; i++) if (!kurulu[i].var) yer = &kurulu[i];
    if (!yer) { neden_yaz(neden, "kurulu paket tablosu dolu"); goto son; }
    serial_write("KERVAN2: '"); serial_write(b->ad); serial_write("' "); serial_write(b->paket_surum);
    serial_write(eski ? " (yukseltme) kuruluyor\n" : " kuruluyor\n");
    /* 1.4.9: yer önceden denetlenir. fat16_write_file eski zinciri yeni
     * veri yazıldıktan sonra bıraktığı için yükseltmede eski ve yeni
     * dosyalar bir an birlikte yer kaplar; bütün paket sığmıyorsa HİÇBİR
     * dosyaya dokunulmaz (eski sürüm sağlam kalır). */
    {
        uint32_t kb = fat16_sectors_per_cluster() * 512u, gereken = 0;
        for (uint32_t i = 0; i < b->girdi; i++) {
            uint32_t n = kb ? (g[i].boy + kb - 1) / kb : 1;
            gereken += n ? n : 1;
        }
        if (!kb || gereken > fat16_bos_kume_sayisi()) { neden_yaz(neden, "diskte yer yok (paket sigmiyor)"); goto son; }
    }
    /* yükseltme: üzerine yazılacak eski dosyaların içeriği önce belleğe
     * alınır; yazım yarıda kalırsa geri yazılır (karışık sürüm kalmaz).
     * Eski paket de en çok 2 MB olduğu için tek tampona sığar. */
    static uint32_t yedek_ofs[KERVAN2_AZAMI_DOSYA + 1], yedek_boy[KERVAN2_AZAMI_DOSYA + 1];
    static uint8_t  yedek_var[KERVAN2_AZAMI_DOSYA + 1];
    uint8_t *yedek = 0;
    if (eski) {
        yedek = (uint8_t *)kmalloc(KERVAN2_AZAMI_BOY);
        if (!yedek) { neden_yaz(neden, "bellek yok (yukseltme yedegi)"); goto son; }
        uint32_t dolu = 0;
        for (uint32_t i = 0; i < b->girdi; i++) {
            yedek_var[i] = 0;
            if (dosya_sahibi(g[i].ad) != eski) continue;
            char yol[16]; yol[0] = '/'; kopya(yol + 1, sizeof yol - 1, g[i].ad);
            int fd = vfs_open(yol);
            if (fd < 0) continue;
            uint32_t n = 0; int r2;
            while (dolu + n < KERVAN2_AZAMI_BOY &&
                   (r2 = vfs_read(fd, yedek + dolu + n, KERVAN2_AZAMI_BOY - dolu - n > 4096 ? 4096 : KERVAN2_AZAMI_BOY - dolu - n)) > 0)
                n += (uint32_t)r2;
            int tam = dolu + n < KERVAN2_AZAMI_BOY;      /* tampon dolduysa kesik olabilir: yükseltme yok */
            vfs_close(fd);
            if (!tam) { kfree(yedek); neden_yaz(neden, "eski surum yedeklenemedi"); goto son; }
            yedek_var[i] = 1; yedek_ofs[i] = dolu; yedek_boy[i] = n; dolu += n;
        }
    }
    /* yaz; hata olursa: yeni dosyalar silinir, eski sürümün dosyaları geri yazılır */
    uint32_t yazilan = 0;
    for (; yazilan < b->girdi; yazilan++) {
        if (fat16_write_file(g[yazilan].ad, v + g[yazilan].ofset, g[yazilan].boy) < 0) break;
    }
    if (yazilan != b->girdi) {
        /* yazılamayan dosya (yazilan. sıradaki) yerinde durur: fat16_write_file
         * eski zinciri ancak başarıdan sonra bırakır */
        for (uint32_t i = 0; i < yazilan; i++) {
            if (yedek && yedek_var[i]) fat16_write_file(g[i].ad, yedek + yedek_ofs[i], yedek_boy[i]);
            else fat16_sil(g[i].ad);
        }
        if (yedek) kfree(yedek);
        neden_yaz(neden, eski ? "diske yazilamadi; eski surum geri yazildi" : "diske yazilamadi (yer yok?)");
        goto son;
    }
    if (yedek) kfree(yedek);
    /* yükseltme: yeni sürümde olmayan eski dosyalar silinir */
    if (eski) {
        for (int d = 0; d < eski->dosya_adet; d++) {
            int kaldi = 0;
            for (uint32_t i = 0; i < b->girdi; i++) if (esit(eski->dosyalar[d], g[i].ad)) kaldi = 1;
            if (!kaldi) fat16_sil(eski->dosyalar[d]);
        }
    }
    yer->var = 1;
    kopya(yer->ad, sizeof yer->ad, b->ad);
    kopya(yer->surum, sizeof yer->surum, b->paket_surum);
    yer->dosya_adet = 0;
    for (uint32_t i = 0; i < b->girdi; i++) kopya(yer->dosyalar[yer->dosya_adet++], 13, g[i].ad);
    if (kayit_yaz() != 0) { neden_yaz(neden, "kayit yazilamadi"); goto son; }
    serial_write("KERVAN2: kurulum TAMAM\n");
    log_write(LOG_BILGI, "KERVAN", "imzali paket kuruldu");
    neden_yaz(neden, "kuruldu");
    uyg_onbellek_yenile();
    r = 0;
son:
    return r;
}

int kervan2_kaldir(const char *paket, char *neden) {
    ozkilit_al(&k2_kilit);
    int r = -1;
    k2_kurulu_t *u = paket_adi_gecerli(paket, KERVAN2_AD) ? kurulu_bul(paket) : 0;
    if (!u) { neden_yaz(neden, "paket kurulu degil"); goto son; }
    for (int d = 0; d < u->dosya_adet; d++) fat16_sil(u->dosyalar[d]);     /* yoksa sorun değil */
    u->var = 0;
    if (kayit_yaz() != 0) { neden_yaz(neden, "kayit yazilamadi"); goto son; }
    serial_write("KERVAN2: '"); serial_write(paket); serial_write("' kaldirildi\n");
    uyg_onbellek_yenile();
    neden_yaz(neden, "kaldirildi");
    r = 0;
son:
    ozkilit_birak(&k2_kilit);
    return r;
}

int kervan2_kurulu_mu(const char *paket, char *surum) {
    ozkilit_al(&k2_kilit);
    k2_kurulu_t *u = kurulu_bul(paket);
    if (u && surum) kopya(surum, KERVAN2_SURUM, u->surum);
    ozkilit_birak(&k2_kilit);
    return u != 0;
}

int kervan2_liste(kervan2_oge_t *out, int azami) {
    static fat16_dirent_t d[128];
    int n = 0;
    ozkilit_al(&k2_kilit);
    int dn = fat16_list_root(d, 128);
    for (int i = 0; i < dn && n < azami; i++) {
        if (d[i].is_dir || !sonu(d[i].name, ".KRV")) continue;
        kervan2_oge_t *o = &out[n];
        uint8_t *p = (uint8_t *)o;
        for (uint32_t k = 0; k < sizeof *o; k++) p[k] = 0;
        kopya(o->dosya, sizeof o->dosya, d[i].name);
        o->boyut = d[i].size;
        uint32_t boy = 0;
        uint8_t *v = dosya_oku(d[i].name, &boy, o->neden);
        if (v && kervan2_dogrula(v, boy, o->neden) == 0) {
            const k2_baslik_t *b = (const k2_baslik_t *)v;
            kopya(o->ad, sizeof o->ad, b->ad);
            kopya(o->surum, sizeof o->surum, b->paket_surum);
            kopya(o->aciklama, sizeof o->aciklama, b->aciklama);
            o->girdi = (uint16_t)b->girdi;
            o->durum = KERVAN2_GECERLI;
            k2_kurulu_t *u = kurulu_bul(b->ad);
            if (u) kopya(o->kurulu_surum, sizeof o->kurulu_surum, u->surum);
        } else {
            o->durum = KERVAN2_GECERSIZ;
            kopya(o->ad, sizeof o->ad, d[i].name);
        }
        if (v) kfree(v);
        n++;
    }
    /* paket dosyası diskte olmayan kurulu paketler */
    for (int i = 0; i < K2_KURULU_AZAMI && n < azami; i++) {
        if (!kurulu[i].var) continue;
        int var = 0;
        for (int j = 0; j < n; j++) if (out[j].durum == KERVAN2_GECERLI && esit(out[j].ad, kurulu[i].ad)) var = 1;
        if (var) continue;
        kervan2_oge_t *o = &out[n++];
        uint8_t *p = (uint8_t *)o;
        for (uint32_t k = 0; k < sizeof *o; k++) p[k] = 0;
        kopya(o->ad, sizeof o->ad, kurulu[i].ad);
        kopya(o->kurulu_surum, sizeof o->kurulu_surum, kurulu[i].surum);
        o->girdi = (uint16_t)kurulu[i].dosya_adet;
        o->durum = KERVAN2_YALNIZ_KURULU;
    }
    ozkilit_birak(&k2_kilit);
    return n;
}

int kervan2_kurulu_sayisi(void) {
    int n = 0;
    ozkilit_al(&k2_kilit);
    for (int i = 0; i < K2_KURULU_AZAMI; i++) n += kurulu[i].var;
    ozkilit_birak(&k2_kilit);
    return n;
}
