/* BENGİ — türlenmiş kalıcı hafıza. VFS üstünde düz satır deposu. */
#include "umay_bengi.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"

static bengi_kayit_t g_bengi[UMAY_BENGI_MAX];
static int  g_sayi;
static char g_yol[UMAY_YOL_MAX];
static int  g_kirli;

const char *bengi_tur_adi(bengi_tur_t t) {
    switch (t) {
    case BENGI_KULLANICI: return "kullanici";
    case BENGI_PROJE:     return "proje";
    case BENGI_KARAR:     return "karar";
    default:              return "sonuc";
    }
}
static bengi_tur_t tur_coz(const char *s) {
    if (um_esit(s, "proje")) return BENGI_PROJE;
    if (um_esit(s, "karar")) return BENGI_KARAR;
    if (um_esit(s, "sonuc")) return BENGI_SONUC;
    return BENGI_KULLANICI;
}

int bengi_sayi(void) { return g_sayi; }

static int bul(const char *a) {
    for (int i = 0; i < g_sayi; i++)
        if (g_bengi[i].dolu && um_esit(g_bengi[i].anahtar, a)) return i;
    return -1;
}

/* Havuz dolduğunda en eski, doğrulanmamış SONUC kaydı düşer;
 * KULLANICI ve KARAR kayıtları hiçbir zaman kendiliğinden silinmez. */
static int yer_ac(void) {
    int aday = -1;
    uint64_t en_eski = 0;
    for (int i = 0; i < g_sayi; i++) {
        if (!g_bengi[i].dolu) return i;
        if (g_bengi[i].tur != BENGI_SONUC || g_bengi[i].dogrulandi) continue;
        if (aday < 0 || g_bengi[i].damga < en_eski) { aday = i; en_eski = g_bengi[i].damga; }
    }
    return aday;
}

/* Disk biçimi satır ve '|' ile ayrılır: denetim karakteri (satır sonu
 * dahil) hiçbir alana, '|' anahtara giremez. Eskiden çok satırlı bir görev
 * çıktısı kaydı bölüyor, geri yüklemede sonraki satırlar AYRI KAYIT gibi
 * ayrıştırılabiliyordu ("karar|1|..." satırı doğrulanmış karar olurdu). */
static int alan_temiz(const char *s, int bar_yasak) {
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c < 0x20 || c == 0x7F) return 0;
        if (bar_yasak && c == '|') return 0;
    }
    return 1;
}

int bengi_kaydet(bengi_tur_t tur, const char *anahtar, const char *deger, int dogrulandi) {
    if (!anahtar || !*anahtar) return UM_YANLIS;
    if (!alan_temiz(anahtar, 1) || (deger && !alan_temiz(deger, 0))) return UM_YANLIS;
    if (tur >= BENGI_TUR_SAYI) return UM_YANLIS;
    /* dogrulandi zorunlu bir bayraktır: 0 ya da 1 dışında değer kabul edilmez */
    if (dogrulandi != 0 && dogrulandi != 1) return UM_YANLIS;
    int i = bul(anahtar);
    if (i < 0) {
        if (g_sayi < UMAY_BENGI_MAX) i = g_sayi++;
        else { i = yer_ac(); if (i < 0) return UM_DOLU; }
        um_bellek_doldur(&g_bengi[i], 0, sizeof g_bengi[i]);
        um_kopya(g_bengi[i].anahtar, sizeof g_bengi[i].anahtar, anahtar);
        g_bengi[i].dolu = 1;
    }
    g_bengi[i].tur = tur;
    g_bengi[i].dogrulandi = dogrulandi ? 1 : 0;
    g_bengi[i].damga = up_ms();
    um_kopya(g_bengi[i].deger, sizeof g_bengi[i].deger, deger ? deger : "");
    g_kirli = 1;
    return UM_OLDU;
}

void bengi_duzle(const char *ham, char *c, uint32_t n) {
    uint32_t o = 0;
    int bosluk = 0;
    if (!c || n == 0) return;
    for (const char *p = ham ? ham : ""; *p && o + 1 < n; p++) {
        uint8_t ch = (uint8_t)*p;
        if (ch < 0x20 || ch == 0x7F) { bosluk = 1; continue; }
        if (bosluk && o > 0) { c[o++] = ' '; if (o + 1 >= n) break; }
        bosluk = 0;
        c[o++] = (char)ch;
    }
    c[o] = 0;
    um_utf8_temizle(c, o);          /* n'de kesilmiş yarım UTF-8 dizisi kalmasın */
}

/* Konuşmadan hafıza ADAYI: buyruk sözcükleri atılır, biçim denetlenir. */
static const char *BENGI_BUYRUK[] = {
    "şu notu kaydet", "bu notu kaydet", "notu kaydet", "bunu kaydet", "şunu kaydet",
    "kaydet", "not al", "bengiye yaz", "belleğe yaz", "hafızaya yaz", "hatırla", "unutma", 0
};

static int bosluk_mu(char c) { return c == ' ' || c == ':' || c == ',' || c == '.' || c == ';' || c == '-'; }

int bengi_denetle(const char *ham, char *temiz, uint32_t n, char *neden, uint32_t nn) {
    if (neden && nn) neden[0] = 0;
    if (temiz && n) temiz[0] = 0;
    if (!ham || !temiz || n < 8) { if (neden) um_kopya(neden, nn, "boş"); return UM_YANLIS; }
    if (!alan_temiz(ham, 0)) {
        if (neden) um_kopya(neden, nn, "denetim karakteri (satır sonu vb.) içeriyor");
        return UM_YANLIS;
    }
    static char b[512], k[512];
    um_kopya(b, sizeof b, ham);
    /* baştaki ve sondaki buyruk sözcüklerini (büyük/küçük harf duyarsız) at */
    for (int tekrar = 1; tekrar; ) {
        tekrar = 0;
        uint32_t L = um_uzun(b), bas = 0;
        while (bas < L && bosluk_mu(b[bas])) bas++;
        while (L > bas && bosluk_mu(b[L - 1])) L--;
        b[L] = 0;
        if (bas) { um_bellek_kopya(b, b + bas, L - bas + 1); L -= bas; }
        um_kopya(k, sizeof k, b);
        um_kucult_esbicim(k);
        for (int i = 0; BENGI_BUYRUK[i]; i++) {
            uint32_t e = um_uzun(BENGI_BUYRUK[i]);
            if (L >= e && um_esit_n(k, BENGI_BUYRUK[i], e) && (k[e] == 0 || bosluk_mu(k[e]))) {
                um_bellek_kopya(b, b + e, L - e + 1); tekrar = 1; break;
            }
            if (L >= e && um_esit(k + (L - e), BENGI_BUYRUK[i]) && (L == e || bosluk_mu(k[L - e - 1]))) {
                b[L - e] = 0; tekrar = 1; break;
            }
        }
    }
    uint32_t L = um_uzun(b), harf = 0, bb;
    for (const char *p = b; *p; p += bb) { um_utf8_oku(p, &bb); harf++; }
    if (harf < 3) { if (neden) um_kopya(neden, nn, "buyruk sözcüklerinden sonra kaydedilecek söz kalmadı"); return UM_YANLIS; }
    if (L >= n || L > 191) { if (neden) um_kopya(neden, nn, "çok uzun (en çok 191 bayt)"); return UM_YANLIS; }
    if (b[L - 1] == '?') { if (neden) um_kopya(neden, nn, "soru hafızaya yazılmaz"); return UM_YANLIS; }
    um_kopya(k, sizeof k, b);
    um_kucult_esbicim(k);
    const char *son = k + L;
    while (son > k && son[-1] != ' ') son--;
    static const char *EK[] = { "mı", "mi", "mu", "mü", "mıdır", "midir", "mudur", "müdür", 0 };
    for (int i = 0; EK[i]; i++)
        if (um_esit(son, EK[i])) { if (neden) um_kopya(neden, nn, "soru hafızaya yazılmaz"); return UM_YANLIS; }
    for (int i = 0; i < g_sayi; i++)
        if (g_bengi[i].dolu && um_esit(g_bengi[i].deger, b)) {
            if (neden) um_bicim(neden, nn, "zaten kayıtlı (%s)", g_bengi[i].anahtar);
            return UM_YANLIS;
        }
    um_kopya(temiz, n, b);
    return UM_OLDU;
}

int bengi_dogrulandi_mi(const char *anahtar) {
    int i = bul(anahtar);
    return i >= 0 && g_bengi[i].dogrulandi;
}

int bengi_yaz(const char *anahtar, const char *deger) {
    return bengi_kaydet(BENGI_KULLANICI, anahtar, deger, 1);
}

const char *bengi_oku(const char *anahtar) {
    int i = bul(anahtar);
    return i < 0 ? 0 : g_bengi[i].deger;
}

int bengi_dogrula(const char *anahtar) {
    int i = bul(anahtar);
    if (i < 0) return UM_YOK;
    g_bengi[i].dogrulandi = 1;
    g_kirli = 1;
    return UM_OLDU;
}

int bengi_sil(const char *anahtar) {
    int i = bul(anahtar);
    if (i < 0) return UM_YOK;
    g_bengi[i].dolu = 0;
    g_kirli = 1;
    return UM_OLDU;
}

int bengi_ara(const char *kelime, const char **cikan, int en_cok) {
    int n = 0;
    for (int i = 0; i < g_sayi && n < en_cok; i++)
        if (g_bengi[i].dolu &&
            (um_icinde(g_bengi[i].deger, kelime) || um_icinde(g_bengi[i].anahtar, kelime)))
            cikan[n++] = g_bengi[i].deger;
    return n;
}

int bengi_turu_ara(bengi_tur_t tur, const bengi_kayit_t **cikan, int en_cok) {
    int n = 0;
    for (int i = 0; i < g_sayi && n < en_cok; i++)
        if (g_bengi[i].dolu && g_bengi[i].tur == tur) cikan[n++] = &g_bengi[i];
    return n;
}

void bengi_ozet(char *c, uint32_t n) {
    um_kopya(c, n, "");
    for (int i = 0; i < g_sayi && um_uzun(c) + 96 < n; i++) {
        if (!g_bengi[i].dolu) continue;
        char s[256];
        um_bicim(s, sizeof s, "[%s%s] %s: %s\n",
                 bengi_tur_adi(g_bengi[i].tur),
                 g_bengi[i].dogrulandi ? "" : "?",
                 g_bengi[i].anahtar, g_bengi[i].deger);
        um_ekle(c, n, s);
    }
}

/* US'e verilecek özet: yalnız doğrulanmış KULLANICI/PROJE/KARAR kayıtları.
 * Doğrulanmamış görev çıktıları modele dayanak olarak sunulmaz. */
void bengi_baglam(char *c, uint32_t n) {
    um_kopya(c, n, "");
    for (int i = 0; i < g_sayi && um_uzun(c) + 96 < n; i++) {
        const bengi_kayit_t *k = &g_bengi[i];
        if (!k->dolu || !k->dogrulandi) continue;
        if (k->tur == BENGI_SONUC) continue;
        um_ekle(c, n, k->deger);
        um_ekle(c, n, "\n");
    }
}

/* Model dosyaları (US1 ağırlıkları ve sözlüğü) BENGİ yolu olamaz: ayar
 * dosyası "bengi=/UMAY.US1" dese bile hafıza modelin üstüne yazılmaz. */
static int model_dosyasi_mi(const char *yol) {
    static char k[UMAY_YOL_MAX];
    um_kopya(k, sizeof k, yol);
    um_kucult_esbicim(k);
    uint32_t L = um_uzun(k);
    const char *ad = k + L;
    while (ad > k && ad[-1] != '/') ad--;
    return (L >= 4 && um_esit(k + L - 4, ".us1")) || um_esit(ad, "sozluk.txt");
}

int bengi_kur(const char *yol) {
    g_sayi = 0; g_kirli = 0;
    g_yol[0] = 0;
    um_bellek_doldur(g_bengi, 0, sizeof g_bengi);
    if (!yol) return UM_OLDU;
    if (model_dosyasi_mi(yol)) {
        gun_yaz("BENGİ: %s bir model dosyası; hafıza oraya yazılmaz (yalnız bellekte).", yol);
        return UM_YETKISIZ;
    }
    um_kopya(g_yol, sizeof g_yol, yol);
    if (!up_var_mi(yol)) return UM_OLDU;

    up_tut_t t = up_ac(yol, UP_OKU);
    if (t < 0) return UM_YANLIS;
    static char tampon[6144];
    int64_t n = up_oku(t, tampon, sizeof tampon - 1);
    up_kapat(t);
    if (n <= 0) return UM_OLDU;
    tampon[n] = 0;

    char satir[288]; uint32_t s = 0;
    for (int64_t i = 0; i <= n; i++) {
        char ch = tampon[i];
        if (ch == '\n' || ch == 0) {
            satir[s] = 0;
            /* tur|dogrulandi|anahtar|deger  (eski biçim: anahtar|deger) */
            char *p[4] = { satir, 0, 0, 0 };
            int alan = 0;
            for (uint32_t j = 0; j < s && alan < 3; j++)
                if (satir[j] == '|') { satir[j] = 0; p[++alan] = satir + j + 1; }
            if (alan == 3 && p[3])
                bengi_kaydet(tur_coz(p[0]), p[2], p[3], p[1][0] == '1');
            else if (alan == 1 && p[1] && satir[0])
                bengi_kaydet(BENGI_KULLANICI, p[0], p[1], 1);
            s = 0;
        } else if (s + 1 < sizeof satir) satir[s++] = ch;
    }
    g_kirli = 0;
    return UM_OLDU;
}

int bengi_sakla(void) {
    if (!g_yol[0]) return UM_YOK;
    if (!g_kirli) return UM_OLDU;
    if (!up_yetkim_var_mi(YET_DOSYA_YAZ)) return UM_YETKISIZ;
    up_tut_t t = up_ac(g_yol, UP_YAZ | UP_YARAT);
    if (t < 0) return UM_YANLIS;
    char satir[288];
    for (int i = 0; i < g_sayi; i++) {
        if (!g_bengi[i].dolu) continue;
        uint32_t n = um_bicim(satir, sizeof satir, "%s|%d|%s|%s\n",
                              bengi_tur_adi(g_bengi[i].tur), g_bengi[i].dogrulandi,
                              g_bengi[i].anahtar, g_bengi[i].deger);
        up_yaz(t, satir, n);
    }
    up_kapat(t);
    g_kirli = 0;
    return UM_OLDU;
}
