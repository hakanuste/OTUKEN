/* =====================================================================
 * KERVAN — paket yöneticisi (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | KERVAN. Diske doğrudan yazamaz; kurma
 * ve kaldırma çekirdeğin imza doğrulayan çağrılarıyla olur. Diskteki .KRV
 * paketlerini ve kurulu paketleri gösterir.
 * Tuşlar: ↑↓ seç · Enter kur/yükselt · Delete kaldır · F5 yenile · Esc kapat
 * 1.4.9: "Ağdan indir": adres yazılır (http/https), paket indirilir ve
 * çekirdeğe verilir; çekirdek imzayı doğrulamadan HİÇBİR ŞEY yazmaz (paket
 * imzalı olduğu için düz HTTP de güvenlidir: değiştirilen paket reddedilir).
 * ===================================================================== */
#include "uyg.h"
#include "http.h"
#define SYS_KERVAN_YUKLE  56

#define SYS_KERVAN_LISTE  45
#define SYS_KERVAN_KUR    46
#define SYS_KERVAN_KALDIR 47

typedef struct {
    char     ad[24];
    char     surum[16];
    char     kurulu_surum[16];
    char     aciklama[96];
    char     dosya[13];
    uint8_u  durum;               /* 0 geçerli, 1 geçersiz, 2 yalnız kurulu */
    unsigned short girdi;
    uint32_u boyut;
    char     neden[64];
} oge_u;

#define GEN 700
#define YUK 470
#define UST 38
#define SATIR 44
#define ALT 100
#define GORUNUR ((YUK - UST - ALT) / SATIR)

#define ARKA   0x14232Bu
#define PANO   0x1B313Au
#define YAZI   0xE8E2D0u
#define SONUK  0x8FA3A8u
#define VURGU  0x4FB3A9u
#define UYARI  0xE0A060u
#define KIRMIZI 0xD9534Fu
#define SECILI 0x24424Cu

static int pen;
static oge_u liste[24];
static int adet, secili, ust;
static char durum[160];
static uint32_u durum_renk = SONUK;
static char adres[256];
static int adres_n, adres_yaz;
static uint8_u indirme[2 * 1024 * 1024 + 1];      /* KERVAN2_AZAMI_BOY + 1 */

static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void yenile(void) {
    int n = (int)syscall(SYS_KERVAN_LISTE, (long)liste, 24, 0, 0);
    adet = n < 0 ? 0 : n;
    if (n < 0) { durum[0] = 0; dize_ekle(durum, sizeof durum, "Liste alınamadı (yetki?)"); durum_renk = KIRMIZI; }
    if (secili >= adet) secili = adet - 1;
    if (secili < 0) secili = 0;
}

static const char *durum_metni(const oge_u *o, uint32_u *renk) {
    if (o->durum == 1) { *renk = KIRMIZI; return "GEÇERSİZ"; }
    if (o->durum == 2) { *renk = SONUK; return "kurulu (paket dosyası yok)"; }
    if (!o->kurulu_surum[0]) { *renk = VURGU; return "kurulabilir"; }
    if (esit(o->kurulu_surum, o->surum)) { *renk = YAZI; return "kurulu"; }
    *renk = UYARI; return "güncellenebilir";
}

static void ciz(void) {
    pencere_doldur(pen, ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, PANO);
    pencere_yazi(pen, 14, 7, "KERVAN — Paketler", VURGU);
    pencere_yazi(pen, GEN - 330, 7, "yalnız imzalı paketler kurulur", SONUK);
    if (adet == 0) pencere_yazi(pen, 14, UST + 10, "Diskte paket (.KRV) yok, kurulu paket de yok.", SONUK);
    if (secili < ust) ust = secili;
    if (secili >= ust + GORUNUR) ust = secili - GORUNUR + 1;
    for (int r = 0; r < GORUNUR && ust + r < adet; r++) {
        const oge_u *o = &liste[ust + r];
        int y = UST + r * SATIR;
        if (ust + r == secili) pencere_dikdortgen(pen, 6, y - 2, GEN - 12, SATIR - 4, SECILI);
        uint32_u renk;
        const char *d = durum_metni(o, &renk);
        char s[120];
        s[0] = 0;
        dize_ekle(s, sizeof s, o->ad);
        if (o->surum[0]) { dize_ekle(s, sizeof s, "  "); dize_ekle(s, sizeof s, o->surum); }
        pencere_yazi(pen, 14, y + 2, s, YAZI);
        s[0] = 0;
        dize_ekle(s, sizeof s, d);
        if (o->kurulu_surum[0] && o->durum == 0 && !esit(o->kurulu_surum, o->surum)) {
            dize_ekle(s, sizeof s, " ("); dize_ekle(s, sizeof s, o->kurulu_surum); dize_ekle(s, sizeof s, " kurulu)");
        }
        pencere_yazi(pen, 330, y + 2, s, renk);
        s[0] = 0;
        if (o->durum == 1) { dize_ekle(s, sizeof s, o->dosya); dize_ekle(s, sizeof s, ": "); dize_ekle(s, sizeof s, o->neden); }
        else dize_ekle(s, sizeof s, o->aciklama[0] ? o->aciklama : "(açıklama yok)");
        pencere_yazi(pen, 30, y + 20, s, o->durum == 1 ? KIRMIZI : SONUK);
    }
    int by = YUK - ALT + 8;
    pencere_dikdortgen(pen, 0, by - 8, GEN, 1, PANO);
    pencere_dikdortgen(pen, 14, by, 124, 26, 0x2A4652u);  pencere_yazi(pen, 24, by + 5, "Kur / Yükselt", YAZI);
    pencere_dikdortgen(pen, 148, by, 80, 26, 0x4A2A2Au);  pencere_yazi(pen, 164, by + 5, "Kaldır", YAZI);
    pencere_dikdortgen(pen, 238, by, 80, 26, 0x2A4652u);  pencere_yazi(pen, 254, by + 5, "Yenile", YAZI);
    pencere_dikdortgen(pen, 328, by, 120, 26, 0x2A4652u); pencere_yazi(pen, 340, by + 5, "Ağdan indir", YAZI);
    pencere_yazi(pen, 14, by + 38, "Adres:", SONUK);
    pencere_dikdortgen(pen, 70, by + 34, GEN - 84, 24, adres_yaz ? 0xFFFFFFu : 0x0F1E25u);
    pencere_yazi(pen, 76, by + 38, adres[0] ? adres : (adres_yaz ? "" : "http://... /paket.krv"), adres_yaz ? 0x10181Cu : SONUK);
    pencere_yazi(pen, 14, by + 66, durum, durum_renk);
    pencere_sun(pen);
}

static void islem(int kur) {
    if (secili < 0 || secili >= adet) return;
    oge_u *o = &liste[secili];
    char neden[64];
    neden[0] = 0;
    long r;
    if (kur) {
        if (o->durum != 0) { durum[0] = 0; dize_ekle(durum, sizeof durum, "Bu paket kurulamaz: geçersiz ya da dosyası yok"); durum_renk = KIRMIZI; ciz(); return; }
        durum[0] = 0; dize_ekle(durum, sizeof durum, "Kuruluyor… (imza doğrulanıyor)"); durum_renk = VURGU; ciz();
        r = syscall(SYS_KERVAN_KUR, (long)o->dosya, (long)neden, 0, 0);
    } else {
        if (!o->kurulu_surum[0]) { durum[0] = 0; dize_ekle(durum, sizeof durum, "Paket kurulu değil"); durum_renk = UYARI; ciz(); return; }
        r = syscall(SYS_KERVAN_KALDIR, (long)o->ad, (long)neden, 0, 0);
    }
    char ad[24];
    int i = 0; while (o->ad[i] && i < 23) { ad[i] = o->ad[i]; i++; } ad[i] = 0;
    durum[0] = 0;
    dize_ekle(durum, sizeof durum, ad);
    dize_ekle(durum, sizeof durum, r == 0 ? (kur ? ": kuruldu" : ": kaldırıldı") : ": BAŞARISIZ — ");
    if (r != 0) dize_ekle(durum, sizeof durum, neden);
    durum_renk = r == 0 ? VURGU : KIRMIZI;
    yenile();
    ciz();
}

static void indir(void) {
    if (!adres[0]) { durum[0] = 0; dize_ekle(durum, sizeof durum, "Önce adresi yaz."); durum_renk = UYARI; return; }
    durum[0] = 0; dize_ekle(durum, sizeof durum, "İndiriliyor…"); durum_renk = VURGU; ciz();
    static http_yanit_t y;
    int r = http_al(adres, &y, indirme, sizeof indirme, 0, 0);
    durum[0] = 0;
    if (r != 0 || y.durum != 200) {
        dize_ekle(durum, sizeof durum, "İndirilemedi: ");
        if (y.durum && y.durum != 200) { char t[8]; sayi_yaz(t, (uint64_u)y.durum, 1); dize_ekle(durum, sizeof durum, "HTTP "); dize_ekle(durum, sizeof durum, t); }
        else dize_ekle(durum, sizeof durum, y.hata[0] ? y.hata : "bağlantı kurulamadı");
        durum_renk = KIRMIZI; return;
    }
    if (y.kesik) { dize_ekle(durum, sizeof durum, "Paket 2 MB sınırını aşıyor."); durum_renk = KIRMIZI; return; }
    char neden[64]; neden[0] = 0;
    long k = syscall(SYS_KERVAN_YUKLE, (long)indirme, (long)y.govde_boy, (long)neden, 0);
    if (k == 0) {
        dize_ekle(durum, sizeof durum, "İndirildi ve kuruldu (imza doğrulandı).");
        durum_renk = VURGU;
    } else {
        dize_ekle(durum, sizeof durum, "KURULMADI: ");
        dize_ekle(durum, sizeof durum, neden[0] ? neden : "çekirdek reddetti");
        durum_renk = KIRMIZI;
    }
    yenile();
}

int main(void) {
    pen = pencere_ac("KERVAN — Paket Yöneticisi", 150, 110, GEN, YUK);
    if (pen < 0) return 1;
    yenile();
    if (!durum[0]) { dize_ekle(durum, sizeof durum, "↑↓ seç · Enter kur · Delete kaldır · F5 yenile"); }
    ciz();
    for (;;) {
        olay_u e;
        int oldu = 0, kirli = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TUS && adres_yaz) {
                if (e.tus == 27) adres_yaz = 0;
                else if (e.tus == 13) { adres_yaz = 0; indir(); }
                else if (e.tus == 8 && adres_n > 0) adres[--adres_n] = 0;
                else if (e.tus > 32 && e.tus < 127 && adres_n < 250) { adres[adres_n++] = (char)e.tus; adres[adres_n] = 0; }
                kirli = 1;                 /* bekleyen tuslar bitince TEK cizim */
                continue;
            }
            if (e.tur == OLAY_TUS) {
                if (e.tus == 27) { pencere_kapat(pen); return 0; }
                if (e.tus == 0xE000 && secili > 0) { secili--; ciz(); }
                else if (e.tus == 0xE001 && secili + 1 < adet) { secili++; ciz(); }
                else if (e.tus == 13) islem(1);
                else if (e.tus == 0xE008) islem(0);
                else if (e.tus == 0xE009) { yenile(); ciz(); }
            } else if (e.tur == OLAY_TIK) {
                int by = YUK - ALT + 8;
                if (e.y >= by && e.y < by + 26) {
                    if (e.x >= 14 && e.x < 138) islem(1);
                    else if (e.x >= 148 && e.x < 228) islem(0);
                    else if (e.x >= 238 && e.x < 318) { yenile(); ciz(); }
                    else if (e.x >= 328 && e.x < 448) { if (adres[0]) indir(); else adres_yaz = 1; ciz(); }
                } else if (e.y >= by + 34 && e.y < by + 58 && e.x >= 70) { adres_yaz = 1; ciz(); } else if (e.y >= UST && e.y < UST + GORUNUR * SATIR) {
                    int r = ust + (e.y - UST) / SATIR;
                    if (r < adet) { secili = r; ciz(); }
                }
            }
        }
        if (kirli) { kirli = 0; ciz(); }
        if (!oldu) sleep_ticks(2);
    }
}
