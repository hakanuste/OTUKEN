#include "http.h"

/* ---------------- küçük dize yardımcıları ---------------- */
static int buyut(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int bas_esit(const char *s, const char *on) {      /* büyük/küçük duyarsız önek */
    while (*on) { if (buyut((unsigned char)*s) != buyut((unsigned char)*on)) return 0; s++; on++; }
    return 1;
}
static void kopya(char *d, int boy, const char *s) {
    int i = 0;
    while (i < boy - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void kopya_n(char *d, int boy, const char *s, int n) {
    int i = 0;
    while (i < boy - 1 && i < n && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int uzun(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* ---------------- URL ---------------- */
int url_coz(const char *s, url_t *u) {
    while (*s == ' ') s++;
    u->guvenli = 0; u->port = 80; u->host[0] = 0; u->yol[0] = '/'; u->yol[1] = 0;
    if (bas_esit(s, "https://")) { u->guvenli = 1; u->port = 443; s += 8; }
    else if (bas_esit(s, "http://")) s += 7;
    else if (bas_esit(s, "//")) s += 2;
    /* şemasız yazılmışsa (tulpar örnek.com) http kabul edilir */
    int i = 0;
    while (*s && *s != '/' && *s != ':' && *s != '?' && *s != '#') {
        if (i >= 255) return -1;
        u->host[i++] = (char)buyut((unsigned char)*s++);
    }
    u->host[i] = 0;
    if (i == 0) return -1;
    if (*s == ':') {
        s++;
        int p = 0, b = 0;
        while (*s >= '0' && *s <= '9') { p = p * 10 + (*s++ - '0'); if (++b > 5) return -1; }
        if (p <= 0 || p > 65535) return -1;
        u->port = p;
    }
    if (*s == '/' || *s == '?') {
        int j = 0;
        if (*s == '?') u->yol[j++] = '/';
        while (*s && *s != '#' && j < (int)sizeof(u->yol) - 1) {
            unsigned char c = (unsigned char)*s++;
            if (c == ' ') {                                  /* boşluk kodlanır */
                if (j + 3 >= (int)sizeof(u->yol)) break;
                u->yol[j++] = '%'; u->yol[j++] = '2'; u->yol[j++] = '0';
            } else u->yol[j++] = (char)c;
        }
        u->yol[j] = 0;
    }
    return 0;
}

void url_yaz(const url_t *u, char *c, int boy) {
    c[0] = 0;
    dize_ekle(c, boy, u->guvenli ? "https://" : "http://");
    dize_ekle(c, boy, u->host);
    if (u->port != (u->guvenli ? 443 : 80)) {
        char t[8]; sayi_yaz(t, (uint64_u)u->port, 1);
        dize_ekle(c, boy, ":"); dize_ekle(c, boy, t);
    }
    dize_ekle(c, boy, u->yol);
}

int url_birlestir(const url_t *taban, const char *bag, char *c, int boy) {
    while (*bag == ' ' || *bag == '\t' || *bag == '\n' || *bag == '\r') bag++;
    if (bas_esit(bag, "http://") || bas_esit(bag, "https://")) { kopya(c, boy, bag); return 0; }
    if (bas_esit(bag, "javascript:") || bas_esit(bag, "mailto:") || bas_esit(bag, "data:") ||
        bas_esit(bag, "tel:")) return -1;
    if (bag[0] == '/' && bag[1] == '/') {
        c[0] = 0;
        dize_ekle(c, boy, taban->guvenli ? "https:" : "http:");
        dize_ekle(c, boy, bag);
        return 0;
    }
    url_t u = *taban;
    if (bag[0] == '#') { url_yaz(&u, c, boy); return 0; }
    char yol[2048];
    if (bag[0] == '/') kopya(yol, sizeof yol, bag);
    else if (bag[0] == '?') {
        int i = 0;
        while (u.yol[i] && u.yol[i] != '?') i++;
        kopya_n(yol, sizeof yol, u.yol, i);
        dize_ekle(yol, sizeof yol, bag);
    } else {
        int son = 0;                       /* tabanın dizini (son '/'a kadar, sorgu hariç) */
        for (int i = 0; u.yol[i] && u.yol[i] != '?'; i++) if (u.yol[i] == '/') son = i;
        kopya_n(yol, sizeof yol, u.yol, son + 1);
        dize_ekle(yol, sizeof yol, bag);
    }
    /* # at, ./ ve ../ çöz */
    for (int i = 0; yol[i]; i++) if (yol[i] == '#') { yol[i] = 0; break; }
    char norm[2048];
    int n = 0;
    int i = 0;
    int sorgu = 0;
    while (yol[i]) {
        if (!sorgu && yol[i] == '?') sorgu = 1;
        if (!sorgu && yol[i] == '/' && yol[i+1] == '.' && (yol[i+2] == '/' || yol[i+2] == 0 || yol[i+2] == '?')) {
            i += 2; if (!yol[i]) { norm[n++] = '/'; } continue;
        }
        if (!sorgu && yol[i] == '/' && yol[i+1] == '.' && yol[i+2] == '.' &&
            (yol[i+3] == '/' || yol[i+3] == 0 || yol[i+3] == '?')) {
            while (n > 0 && norm[n-1] != '/') n--;
            if (n > 0) n--;
            while (n > 0 && norm[n-1] != '/') n--;
            if (n > 0) n--;
            i += 3; if (!yol[i]) norm[n++] = '/';
            continue;
        }
        if (n < (int)sizeof(norm) - 1) norm[n++] = yol[i];
        i++;
    }
    norm[n] = 0;
    if (norm[0] != '/') { kopya(yol, sizeof yol, "/"); dize_ekle(yol, sizeof yol, norm); }
    else kopya(yol, sizeof yol, norm);
    kopya(u.yol, sizeof u.yol, yol);
    url_yaz(&u, c, boy);
    return 0;
}

/* ---------------- akış ---------------- */
#define BOSTA_ZAMAN_SN 20

static long hz(void) { long h = (long)sistem_bil(SYSBIL_TIK_HZ); return h > 0 ? h : 100; }

static int akis_doldur(akis_t *a) {
    if (a->bas < a->son) return 1;
    if (a->bitti) return 0;
    long bas = ticks();
    for (;;) {
        int n = a->tls ? guvenli_oku(a, a->tam, sizeof a->tam) : ag_al(a->t, a->tam, sizeof a->tam);
        if (n > 0) { a->bas = 0; a->son = n; return 1; }
        if (n == AG_SON) { a->bitti = 1; return 0; }
        if (n < 0) return -1;
        if (ticks() - bas > BOSTA_ZAMAN_SN * hz()) return -2;
        sleep_ticks(1);
    }
}

static int akis_bayt(akis_t *a) {
    int r = akis_doldur(a);
    if (r <= 0) return r == 0 ? -1 : -2;
    return a->tam[a->bas++];
}

static int akis_yaz(akis_t *a, const void *v, int n) {
    return a->tls ? guvenli_yaz(a, v, n) : ag_gonder_tum(a->t, v, n, 10 * hz());
}

/* ---------------- yerel ad eşlemesi ---------------- */
static struct { char ad[64]; uint8_u ip[4]; } esle[8];
static int esle_adet;
int http_ad_esle(const char *ad, const uint8_u ip[4]) {
    if (esle_adet >= 8) return -1;
    kopya(esle[esle_adet].ad, sizeof esle[0].ad, ad);
    for (int i = 0; i < 4; i++) esle[esle_adet].ip[i] = ip[i];
    esle_adet++;
    return 0;
}
static int coz(const char *ad, uint8_u ip[4]) {
    for (int k = 0; k < esle_adet; k++) {
        const char *a = esle[k].ad, *b = ad;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) { for (int i = 0; i < 4; i++) ip[i] = esle[k].ip[i]; return 0; }
    }
    return ag_coz(ad, ip);
}

/* ---------------- HTTP ---------------- */
static int satir_oku(akis_t *a, char *s, int boy) {       /* CRLF'siz satır; uzunluk / -1 */
    int n = 0;
    for (;;) {
        int c = akis_bayt(a);
        if (c < 0) return n ? n : -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < boy - 1) s[n++] = (char)c;
    }
    s[n] = 0;
    return n;
}

static long onaltili(const char *s) {
    long v = 0; int b = 0;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * 16 + d;
        if (++b > 8) return -1;
    }
    return b ? v : -1;
}

typedef struct {
    http_yanit_t *y;
    uint8_u *govde; uint32_u azami;
    int (*ilerleme)(uint32_u, void *); void *v;
} alici_t;

static int govdeye(alici_t *al, akis_t *a, long n) {       /* n bayt (n<0: sona dek) */
    http_yanit_t *y = al->y;
    while (n != 0) {
        int r = akis_doldur(a);
        if (r == 0) return n < 0 ? 0 : -1;
        if (r < 0) return r;
        int var = a->son - a->bas;
        int k = (n > 0 && n < var) ? (int)n : var;
        uint32_u yer = al->azami - y->govde_boy;
        uint32_u kopy = (uint32_u)k < yer ? (uint32_u)k : yer;
        if (kopy) memcpy(al->govde + y->govde_boy, a->tam + a->bas, kopy);
        if (kopy < (uint32_u)k) y->kesik = 1;
        y->govde_boy += kopy;
        a->bas += k;
        if (n > 0) n -= k;
        if (al->ilerleme && !al->ilerleme(y->govde_boy, al->v)) return -3;
        if (y->kesik) return 0;                     /* tampon doldu: yeter */
    }
    return 0;
}

static void hata_yaz(http_yanit_t *y, const char *a, const char *b) {
    kopya(y->hata, sizeof y->hata, a);
    if (b) dize_ekle(y->hata, sizeof y->hata, b);
}

static int tek_istek(const url_t *u, alici_t *al) {
    http_yanit_t *y = al->y;
    static akis_t a;                                  /* 4 KB tampon: yığında değil */
    uint8_u ip[4];
    y->durum = 0; y->tur[0] = 0; y->konum[0] = 0; y->govde_boy = 0; y->kesik = 0;
    if (coz(u->host, ip) != 0) { hata_yaz(y, "Ad çözülemedi: ", u->host); return -1; }
    a.t = ag_baglan(ip, u->port); a.tls = 0; a.bas = a.son = 0; a.bitti = 0;
    if (a.t < 0) { hata_yaz(y, "Bağlanılamadı: ", u->host); return -1; }
    if (u->guvenli && guvenli_ac(&a, u->host, y->hata, sizeof y->hata) != 0) {
        ag_kapat(a.t);
        return -1;
    }
    char istek[2600];
    istek[0] = 0;
    dize_ekle(istek, sizeof istek, "GET ");
    dize_ekle(istek, sizeof istek, u->yol);
    dize_ekle(istek, sizeof istek, " HTTP/1.1\r\nHost: ");
    dize_ekle(istek, sizeof istek, u->host);
    if (u->port != (u->guvenli ? 443 : 80)) {
        char t[8]; sayi_yaz(t, (uint64_u)u->port, 1);
        dize_ekle(istek, sizeof istek, ":"); dize_ekle(istek, sizeof istek, t);
    }
    dize_ekle(istek, sizeof istek, "\r\nUser-Agent: TULPAR/0.1 (OTUKEN; metin)\r\n"
              "Accept: text/html,text/plain;q=0.9,*/*;q=0.5\r\n"
              "Accept-Language: tr,en;q=0.7\r\nAccept-Encoding: identity\r\n"
              "Connection: close\r\n\r\n");
    int r = -1;
    if (akis_yaz(&a, istek, uzun(istek)) != 0) { hata_yaz(y, "İstek gönderilemedi", 0); goto son; }

    char satir[2200];
    if (satir_oku(&a, satir, sizeof satir) < 0 || !bas_esit(satir, "HTTP/")) {
        hata_yaz(y, "Sunucu HTTP yanıtı vermedi", 0); goto son;
    }
    {
        const char *p = satir;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        int d = 0;
        for (int k = 0; k < 3 && *p >= '0' && *p <= '9'; k++) d = d * 10 + (*p++ - '0');
        y->durum = d;
    }
    long uzunluk = -1;
    int parcali = 0;
    for (int say = 0; ; say++) {
        int n = satir_oku(&a, satir, sizeof satir);
        if (n < 0) { hata_yaz(y, "Yanıt başlığı yarım kaldı", 0); goto son; }
        if (n == 0) break;
        if (say > 200) { hata_yaz(y, "Yanıt başlığı çok uzun", 0); goto son; }
        const char *deger = satir;
        while (*deger && *deger != ':') deger++;
        if (!*deger) continue;
        deger++;
        while (*deger == ' ' || *deger == '\t') deger++;
        if (bas_esit(satir, "content-length:")) {
            long v = 0;
            for (const char *q = deger; *q >= '0' && *q <= '9'; q++) v = v * 10 + (*q - '0');
            uzunluk = v;
        } else if (bas_esit(satir, "transfer-encoding:")) {
            for (const char *q = deger; *q; q++) if (bas_esit(q, "chunked")) parcali = 1;
        } else if (bas_esit(satir, "location:")) kopya(y->konum, sizeof y->konum, deger);
        else if (bas_esit(satir, "content-type:")) kopya(y->tur, sizeof y->tur, deger);
    }
    if (y->durum == 204 || y->durum == 304 || (y->durum >= 100 && y->durum < 200)) { r = 0; goto son; }
    if (parcali) {
        for (;;) {
            if (satir_oku(&a, satir, sizeof satir) < 0) { hata_yaz(y, "Parçalı aktarım yarım kaldı", 0); goto son; }
            long n = onaltili(satir);
            if (n < 0) { hata_yaz(y, "Parçalı aktarım bozuk", 0); goto son; }
            if (n == 0) break;
            int g = govdeye(al, &a, n);
            if (g == -3) { hata_yaz(y, "İptal edildi", 0); goto son; }
            if (g < 0) { hata_yaz(y, g == -2 ? "Zaman aşımı" : "Bağlantı koptu", 0); goto son; }
            if (y->kesik) break;
            satir_oku(&a, satir, sizeof satir);           /* parça sonu CRLF */
        }
    } else {
        int g = govdeye(al, &a, uzunluk);
        if (g == -3) { hata_yaz(y, "İptal edildi", 0); goto son; }
        if (g < 0) { hata_yaz(y, g == -2 ? "Zaman aşımı" : "Bağlantı koptu (yanıt eksik)", 0); goto son; }
    }
    r = 0;
son:
    if (a.tls) guvenli_kapat(&a);
    ag_kapat(a.t);
    return r;
}

int http_al(const char *url, http_yanit_t *y, uint8_u *govde, uint32_u azami,
            int (*ilerleme)(uint32_u, void *), void *v) {
    static url_t u;
    alici_t al = { y, govde, azami, ilerleme, v };
    long bas = ticks();
    y->hata[0] = 0;
    y->son_url[0] = 0;
    if (url_coz(url, &u) != 0) { hata_yaz(y, "Adres anlaşılamadı", 0); return -1; }
    for (int yon = 0; yon <= 5; yon++) {
        url_yaz(&u, y->son_url, sizeof y->son_url);
        if (tek_istek(&u, &al) != 0) { y->sure_tik = (uint32_u)(ticks() - bas); return -1; }
        int d = y->durum;
        if ((d == 301 || d == 302 || d == 303 || d == 307 || d == 308) && y->konum[0]) {
            static char yeni[2400];
            if (url_birlestir(&u, y->konum, yeni, sizeof yeni) != 0 || url_coz(yeni, &u) != 0) {
                hata_yaz(y, "Yönlendirme adresi anlaşılamadı", 0);
                return -1;
            }
            continue;
        }
        y->sure_tik = (uint32_u)(ticks() - bas);
        return 0;
    }
    hata_yaz(y, "Çok fazla yönlendirme", 0);
    return -1;
}
