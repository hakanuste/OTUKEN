/* TULPAR — HTML'den okunur metne (bkz. html.h) */
#include "html.h"

#ifdef HTML_ANA_MAKINE
#include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* küçük yardımcılar                                                   */
/* ------------------------------------------------------------------ */
static int kucuk(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int bosluk_mu(uint32_u c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int esit(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int onek_kucuk(const char *s, const char *on) {
    while (*on) { if (kucuk((unsigned char)*s) != *on) return 0; s++; on++; }
    return 1;
}
static int icerir_kucuk(const char *s, const char *parca) {
    for (; *s; s++) if (onek_kucuk(s, parca)) return 1;
    return 0;
}

int utf8_yaz(uint32_u k, char *o) {
    if (k < 0x80) { o[0] = (char)k; return 1; }
    if (k < 0x800) { o[0] = (char)(0xC0 | (k >> 6)); o[1] = (char)(0x80 | (k & 63)); return 2; }
    if (k < 0x10000) {
        o[0] = (char)(0xE0 | (k >> 12)); o[1] = (char)(0x80 | ((k >> 6) & 63));
        o[2] = (char)(0x80 | (k & 63)); return 3;
    }
    o[0] = (char)(0xF0 | (k >> 18)); o[1] = (char)(0x80 | ((k >> 12) & 63));
    o[2] = (char)(0x80 | ((k >> 6) & 63)); o[3] = (char)(0x80 | (k & 63)); return 4;
}

/* windows-125x 0x80-0x9F (ikisinde ortak olanlar) */
static const uint16_u CP125X[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0, 0x0178
};

/* g[*i]'den bir kod noktası; *i ilerler */
static uint32_u kod_al(const uint8_u *g, uint32_u n, uint32_u *i, int kk) {
    uint8_u b = g[(*i)++];
    if (b < 0x80) return b;
    if (kk != KK_UTF8) {
        if (b < 0xA0) { uint32_u k = CP125X[b - 0x80]; return k ? k : '?'; }
        if (kk == KK_LATIN5) {
            switch (b) {
            case 0xD0: return 0x011E; case 0xDD: return 0x0130; case 0xDE: return 0x015E;
            case 0xF0: return 0x011F; case 0xFD: return 0x0131; case 0xFE: return 0x015F;
            }
        }
        return b;
    }
    int ek; uint32_u k;
    if ((b & 0xE0) == 0xC0) { ek = 1; k = b & 0x1F; }
    else if ((b & 0xF0) == 0xE0) { ek = 2; k = b & 0x0F; }
    else if ((b & 0xF8) == 0xF0) { ek = 3; k = b & 0x07; }
    else return 0xFFFD;
    for (int j = 0; j < ek; j++) {
        if (*i >= n || (g[*i] & 0xC0) != 0x80) return 0xFFFD;
        k = (k << 6) | (g[(*i)++] & 63);
    }
    return k;
}

int html_kodlama_ad(const char *ad) {
    if (!ad) return -1;
    const char *p = ad;
    for (; *p; p++) if (onek_kucuk(p, "charset=")) { p += 8; break; }
    if (!*p) p = ad;
    while (*p == ' ' || *p == '"' || *p == '\'') p++;
    if (onek_kucuk(p, "utf-8") || onek_kucuk(p, "utf8")) return KK_UTF8;
    if (onek_kucuk(p, "iso-8859-9") || onek_kucuk(p, "windows-1254") || onek_kucuk(p, "latin5") ||
        onek_kucuk(p, "cp1254")) return KK_LATIN5;
    if (onek_kucuk(p, "iso-8859-1") || onek_kucuk(p, "windows-1252") || onek_kucuk(p, "us-ascii") ||
        onek_kucuk(p, "latin1")) return KK_LATIN1;
    return -1;
}

int html_kodlama_bul(const uint8_u *g, uint32_u n) {
    if (n > 4096) n = 4096;
    char t[4097];
    for (uint32_u i = 0; i < n; i++) t[i] = g[i] ? (char)g[i] : ' ';
    t[n] = 0;
    for (uint32_u i = 0; i < n; i++) {
        if (onek_kucuk(t + i, "charset=")) {
            char ad[40]; int k = 0;
            const char *p = t + i + 8;
            while (*p == '"' || *p == '\'' || *p == ' ') p++;
            while (*p && *p != '"' && *p != '\'' && *p != ' ' && *p != ';' && *p != '>' && k < 39) ad[k++] = *p++;
            ad[k] = 0;
            int r = html_kodlama_ad(ad);
            if (r >= 0) return r;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* varlıklar (&amp; ...)                                               */
/* ------------------------------------------------------------------ */
static const struct { const char *ad; uint16_u kod; } VARLIK[] = {
    {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"shy",0xAD},
    {"copy",0xA9},{"reg",0xAE},{"trade",0x2122},{"mdash",0x2014},{"ndash",0x2013},{"hellip",0x2026},
    {"laquo",0xAB},{"raquo",0xBB},{"lsquo",0x2018},{"rsquo",0x2019},{"sbquo",0x201A},{"ldquo",0x201C},
    {"rdquo",0x201D},{"bdquo",0x201E},{"bull",0x2022},{"middot",0xB7},{"times",0xD7},{"divide",0xF7},
    {"deg",0xB0},{"euro",0x20AC},{"pound",0xA3},{"cent",0xA2},{"yen",0xA5},{"sect",0xA7},{"para",0xB6},
    {"plusmn",0xB1},{"frac12",0xBD},{"frac14",0xBC},{"frac34",0xBE},{"sup2",0xB2},{"sup3",0xB3},
    {"larr",0x2190},{"rarr",0x2192},{"uarr",0x2191},{"darr",0x2193},{"iexcl",0xA1},{"iquest",0xBF},
    {"Ccedil",0xC7},{"ccedil",0xE7},{"Ouml",0xD6},{"ouml",0xF6},{"Uuml",0xDC},{"uuml",0xFC},
    {"Acirc",0xC2},{"acirc",0xE2},{"Icirc",0xCE},{"icirc",0xEE},{"Ucirc",0xDB},{"ucirc",0xFB},
    {"Eacute",0xC9},{"eacute",0xE9},{"Egrave",0xC8},{"egrave",0xE8},{"Ecirc",0xCA},{"ecirc",0xEA},
    {"Agrave",0xC0},{"agrave",0xE0},{"Aacute",0xC1},{"aacute",0xE1},{"Iacute",0xCD},{"iacute",0xED},
    {"Oacute",0xD3},{"oacute",0xF3},{"Uacute",0xDA},{"uacute",0xFA},{"Ntilde",0xD1},{"ntilde",0xF1},
    {"Auml",0xC4},{"auml",0xE4},{"szlig",0xDF},{"Oslash",0xD8},{"oslash",0xF8},{"Ocirc",0xD4},{"ocirc",0xF4},
    {"ensp",' '},{"emsp",' '},{"thinsp",' '},{"zwnj",0},{"zwj",0},{"lrm",0},{"rlm",0},
    {0,0}
};

/* p '&'dan sonrasını gösterir; *tuk tüketilen bayt. Tanınmazsa 0 */
static uint32_u varlik_coz(const uint8_u *p, uint32_u kalan, uint32_u *tuk, int *gecerli) {
    *gecerli = 0;
    if (kalan >= 2 && p[0] == '#') {
        uint32_u v = 0, j = 1;
        int hex = 0;
        if (j < kalan && (p[j] == 'x' || p[j] == 'X')) { hex = 1; j++; }
        uint32_u bas = j;
        for (; j < kalan && j < 10; j++) {
            uint8_u c = p[j];
            if (c >= '0' && c <= '9') v = v * (hex ? 16 : 10) + (c - '0');
            else if (hex && kucuk(c) >= 'a' && kucuk(c) <= 'f') v = v * 16 + (uint32_u)(kucuk(c) - 'a' + 10);
            else break;
        }
        if (j == bas) return 0;
        if (j < kalan && p[j] == ';') j++;
        *tuk = j;
        *gecerli = 1;
        if (v == 0 || v > 0x10FFFF) return 0xFFFD;
        if (v >= 0x80 && v < 0xA0) { uint32_u k = CP125X[v - 0x80]; return k ? k : '?'; }
        return v;
    }
    char ad[12]; uint32_u j = 0;
    while (j < kalan && j < 10 && ((p[j] >= 'a' && p[j] <= 'z') || (p[j] >= 'A' && p[j] <= 'Z') ||
                                   (p[j] >= '0' && p[j] <= '9'))) { ad[j] = (char)p[j]; j++; }
    ad[j] = 0;
    if (!j) return 0;
    for (int k = 0; VARLIK[k].ad; k++) {
        if (esit(VARLIK[k].ad, ad)) {
            if (j < kalan && p[j] == ';') j++;
            *tuk = j;
            *gecerli = 1;
            return VARLIK[k].kod;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* dizgi (yerleşim) durumu                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    sayfa_t *s;
    int  sutun;              /* bu satırdaki hücre sayısı */
    int  girinti;
    int  bekleyen_bosluk;
    int  bos_satir;          /* art arda boş satır sayısı (paragraf aralığı) */
    int  stil;
    int  oge;                /* etkin bağlantı öğesi + 1 */
    int  onbicim;            /* <pre> derinliği */
    /* sözcük tamponu */
    uint32_u sz[512];
    int  sz_adet;
    int  sz_stil[512];
    int  sz_oge[512];
    int  satir_yazi;         /* bu satıra yazı yazıldı mı (girinti dışında) */
    int  madde;              /* madde işareti yazıldı, metni henüz gelmedi */
    int  pre_bas;            /* <pre>'nin hemen ardı: ilk satır sonu yok sayılır */
} dizgi_t;

static void satir_bitir(dizgi_t *d) {
    sayfa_t *s = d->s;
    if (s->satir_adet >= H_MAX_SATIR) { s->kesik = 1; return; }
    s->satir_adet++;
    s->satir[s->satir_adet] = s->kar_adet;
    s->satir[s->satir_adet + 1] = s->kar_adet;
    d->sutun = 0;
    d->satir_yazi = 0;
}

static void hucre(dizgi_t *d, uint32_u k, int stil, int oge) {
    sayfa_t *s = d->s;
    if (s->kar_adet >= H_MAX_KAR - 1 || s->satir_adet >= H_MAX_SATIR) { s->kesik = 1; return; }
    s->kar[s->kar_adet] = (k & 0x1FFFFF) | ((uint32_u)stil << 24);
    s->oge[s->kar_adet] = (uint16_u)oge;
    s->kar_adet++;
    s->satir[s->satir_adet + 1] = s->kar_adet;
    d->sutun++;
}

static void girinti_yaz(dizgi_t *d) {
    int g = d->girinti;
    if (g > d->s->genislik / 2) g = d->s->genislik / 2;
    while (d->sutun < g) hucre(d, ' ', ST_NORMAL, 0);
}

static void yeni_satir(dizgi_t *d) {
    satir_bitir(d);
}

/* sözcük tamponunu satıra dök (sığmazsa yeni satır) */
static void sozcuk_bosalt(dizgi_t *d) {
    if (d->sz_adet == 0) return;
    int w = d->s->genislik;
    int bosluk = (d->bekleyen_bosluk && d->satir_yazi) ? 1 : 0;
    if (d->satir_yazi && d->sutun + bosluk + d->sz_adet > w) { yeni_satir(d); bosluk = 0; }
    if (d->sutun == 0) girinti_yaz(d);
    if (bosluk) {
        /* aradaki boşluk, ancak önceki hücre ile aynı bağlantıdaysa bağlantıya aittir */
        sayfa_t *sp = d->s;
        int onceki = sp->kar_adet > 0 ? sp->oge[sp->kar_adet - 1] : 0;
        int ayni = d->sz_oge[0] && onceki == d->sz_oge[0];
        hucre(d, ' ', ayni ? d->sz_stil[0] : ST_NORMAL, ayni ? d->sz_oge[0] : 0);
    }
    for (int i = 0; i < d->sz_adet; i++) {
        if (d->sutun >= w) { yeni_satir(d); girinti_yaz(d); }
        hucre(d, d->sz[i], d->sz_stil[i], d->sz_oge[i]);
    }
    d->satir_yazi = 1;
    d->bos_satir = 0;
    d->sz_adet = 0;
    d->bekleyen_bosluk = 0;
    d->madde = 0;
}

/* blok sınırı: satırı kapat; bos=1 ise araya (en çok) bir boş satır */
static void blok(dizgi_t *d, int bos) {
    sozcuk_bosalt(d);
    d->bekleyen_bosluk = 0;
    if (d->madde) return;      /* <li><p>metin: madde işareti ile metin aynı satırda */
    if (d->satir_yazi || d->sutun > 0) { yeni_satir(d); }
    if (bos && d->bos_satir == 0 && d->s->satir_adet > 0) { yeni_satir(d); d->bos_satir = 1; }
}

/* yazının görünür karşılığı olmayan kod noktalarını ayıkla */
static uint32_u gorunur(uint32_u k) {
    if (k == 0xA0) return ' ';
    if (k == 0xAD || k == 0x200B || k == 0x200C || k == 0x200D || k == 0xFEFF ||
        k == 0x200E || k == 0x200F) return 0;
    if (k < 0x20) return 0;
    if (k == 0x2032) return '\'';
    if (k == 0x2033) return '"';
    if (k == 0x201A || k == 0x201E) return k == 0x201A ? 0x2018 : 0x201C;
    if (k == 0x2039) return '<';
    if (k == 0x203A) return '>';
    if (k == 0x2212) return '-';
    if (k == 0x2010 || k == 0x2011) return '-';
    if (k == 0x2002 || k == 0x2003 || k == 0x2009 || k == 0x202F) return ' ';
    return k;
}

static void metin_kod(dizgi_t *d, uint32_u k) {
    if (d->onbicim) {
        if (k == '\r') return;
        if (d->pre_bas) { d->pre_bas = 0; if (k == '\n') return; }
        if (k == '\n') { sozcuk_bosalt(d); yeni_satir(d); return; }
        if (k == '\t') { sozcuk_bosalt(d); do hucre(d, ' ', d->stil, d->oge); while (d->sutun % 8); return; }
        k = gorunur(k);
        if (!k) return;
        if (d->sutun >= d->s->genislik) yeni_satir(d);
        hucre(d, k, d->stil, d->oge);
        d->satir_yazi = 1; d->bos_satir = 0;
        return;
    }
    if (bosluk_mu(k)) {
        sozcuk_bosalt(d);
        d->bekleyen_bosluk = 1;
        return;
    }
    if (k == 0xA0) {                     /* bölünmez boşluk: sözcüğün parçası, daralmaz */
        if (d->sz_adet >= (int)(sizeof d->sz / sizeof d->sz[0])) sozcuk_bosalt(d);
        if (d->sz_adet == 0 && d->bekleyen_bosluk && d->satir_yazi) {
            d->sz[d->sz_adet] = ' '; d->sz_stil[d->sz_adet] = ST_NORMAL; d->sz_oge[d->sz_adet] = 0; d->sz_adet++;
            d->bekleyen_bosluk = 0;
        }
        d->sz[d->sz_adet] = ' '; d->sz_stil[d->sz_adet] = d->stil; d->sz_oge[d->sz_adet] = d->oge; d->sz_adet++;
        return;
    }
    k = gorunur(k);
    if (!k) return;
    if (k == ' ') { sozcuk_bosalt(d); d->bekleyen_bosluk = 1; return; }
    if (d->sz_adet >= (int)(sizeof d->sz / sizeof d->sz[0])) sozcuk_bosalt(d);
    d->sz[d->sz_adet] = k;
    d->sz_stil[d->sz_adet] = d->stil;
    d->sz_oge[d->sz_adet] = d->oge;
    d->sz_adet++;
}

static void metin_ascii(dizgi_t *d, const char *t) {
    while (*t) {
        uint32_u i = 0;
        uint32_u k = kod_al((const uint8_u *)t, 8, &i, KK_UTF8);
        t += i;
        metin_kod(d, k);
    }
}

/* metin[] deposuna dize ekle; başlangıç indisi (sığmazsa 0 = boş dize) */
static uint32_u depo(sayfa_t *s, const char *t, int n) {
    if (n < 0) { n = 0; while (t[n]) n++; }
    if (s->metin_boy == 0) { s->metin[0] = 0; s->metin_boy = 1; }
    if (s->metin_boy + (uint32_u)n + 1 >= H_MAX_METIN) { s->kesik = 1; return 0; }
    uint32_u b = s->metin_boy;
    for (int i = 0; i < n; i++) s->metin[b + (uint32_u)i] = t[i];
    s->metin[b + (uint32_u)n] = 0;
    s->metin_boy += (uint32_u)n + 1;
    return b;
}

static int oge_ekle(sayfa_t *s, int tur, int form, uint32_u ad, uint32_u deger) {
    if (s->oge_adet >= H_MAX_OGE) { s->kesik = 1; return 0; }
    h_oge_t *o = &s->ogeler[s->oge_adet];
    o->tur = (uint8_u)tur; o->form = (uint8_u)form; o->secili = 0; o->ad = ad; o->deger = deger;
    s->oge_adet++;
    return s->oge_adet;               /* +1'li */
}

/* ------------------------------------------------------------------ */
/* etiket ayrıştırma                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    char ad[16];
    int  bitis;                /* </ad> */
    int  kendi_kapali;         /* <br/> */
    int  nit_adet;
    char nit_ad[24][20];
    char nit_deger[24][768];   /* UTF-8; varlıklar çözülmüş */
} etiket_t;

static const char *nitelik(const etiket_t *e, const char *ad) {
    for (int i = 0; i < e->nit_adet; i++) if (esit(e->nit_ad[i], ad)) return e->nit_deger[i];
    return 0;
}

/* g[*i] = '<'. Etiketi ayrıştırır; *i '>' sonrasına ilerler. Etiket değilse 0 */
static int etiket_oku(const uint8_u *g, uint32_u n, uint32_u *i, int kk, etiket_t *e) {
    uint32_u p = *i + 1;
    e->bitis = 0; e->kendi_kapali = 0; e->nit_adet = 0; e->ad[0] = 0;
    if (p < n && g[p] == '/') { e->bitis = 1; p++; }
    if (p >= n || !((g[p] >= 'a' && g[p] <= 'z') || (g[p] >= 'A' && g[p] <= 'Z'))) return 0;
    int k = 0;
    while (p < n && ((g[p] >= 'a' && g[p] <= 'z') || (g[p] >= 'A' && g[p] <= 'Z') ||
                     (g[p] >= '0' && g[p] <= '9') || g[p] == '-' || g[p] == ':')) {
        if (k < 15) e->ad[k++] = (char)kucuk(g[p]);
        p++;
    }
    e->ad[k] = 0;
    for (;;) {
        while (p < n && bosluk_mu(g[p])) p++;
        if (p >= n) { *i = n; return 1; }
        if (g[p] == '>') { p++; break; }
        if (g[p] == '/') { e->kendi_kapali = 1; p++; continue; }
        char nad[20]; int a = 0;
        while (p < n && !bosluk_mu(g[p]) && g[p] != '=' && g[p] != '>' && g[p] != '/') {
            if (a < 19) nad[a++] = (char)kucuk(g[p]);
            p++;
        }
        nad[a] = 0;
        if (a == 0) { p++; continue; }
        while (p < n && bosluk_mu(g[p])) p++;
        char *hedef = 0;
        int boy = 0;
        if (e->nit_adet < 24) {
            for (int z = 0; z <= a; z++) e->nit_ad[e->nit_adet][z] = nad[z];
            hedef = e->nit_deger[e->nit_adet];
            hedef[0] = 0;
            e->nit_adet++;
        }
        if (p < n && g[p] == '=') {
            p++;
            while (p < n && bosluk_mu(g[p])) p++;
            uint8_u tirnak = 0;
            if (p < n && (g[p] == '"' || g[p] == '\'')) tirnak = g[p++];
            while (p < n) {
                uint8_u c = g[p];
                if (tirnak ? c == tirnak : (bosluk_mu(c) || c == '>')) break;
                uint32_u kod;
                if (c == '&') {
                    uint32_u tuk; int gec;
                    kod = varlik_coz(g + p + 1, n - p - 1, &tuk, &gec);
                    if (gec) p += 1 + tuk; else { kod = '&'; p++; }
                } else kod = kod_al(g, n, &p, kk);
                if (hedef && boy < 760 && kod) boy += utf8_yaz(kod, hedef + boy);
            }
            if (tirnak && p < n) p++;
        }
        if (hedef) hedef[boy] = 0;
    }
    *i = p;
    return 1;
}

/* ham metin öğesinin (script/style/textarea/title) sonunu bul */
static uint32_u ham_son(const uint8_u *g, uint32_u n, uint32_u p, const char *ad) {
    int al = 0; while (ad[al]) al++;
    for (; p + 2 + (uint32_u)al <= n; p++) {
        if (g[p] == '<' && g[p + 1] == '/') {
            int ok = 1;
            for (int k = 0; k < al; k++) if (kucuk(g[p + 2 + k]) != ad[k]) { ok = 0; break; }
            if (ok) return p;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* ana işleyici                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    int  liste_tur[16];        /* 0 ul, 1 ol */
    int  liste_no[16];
    int  liste_derin;
    int  form;                 /* etkin form (-1 yok) */
    int  dugme_oge;            /* <button> içinde */
    int  tablo_hucre;          /* satırdaki hücre sırası */
    int  stil_yigin[32];
    int  stil_derin;
    char atla_ad[16];          /* gizli öğe (hidden / display:none) */
    int  atla_derin;
    int  select_oge;           /* <select> içinde (değer seçimi) */
    int  select_yazildi;
} baglam_t;

static int ic_icerik_yok(const char *a) {    /* boş (void) öğeler */
    static const char *V[] = {"br","img","input","hr","meta","link","area","base","col","embed",
                              "source","track","wbr","param",0};
    for (int i = 0; V[i]; i++) if (esit(a, V[i])) return 1;
    return 0;
}
static int blok_mu(const char *a) {
    static const char *B[] = {"p","div","section","article","main","header","footer","nav","aside",
        "figure","figcaption","address","center","dl","dt","dd","form","fieldset","legend","details",
        "summary","table","caption","tbody","thead","tfoot","ul","ol","menu","blockquote",0};
    for (int i = 0; B[i]; i++) if (esit(a, B[i])) return 1;
    return 0;
}
static int bosluklu_blok(const char *a) {    /* ardından boş satır */
    return esit(a, "p") || esit(a, "table") || esit(a, "blockquote") || esit(a, "figure") ||
           esit(a, "ul") || esit(a, "ol") || esit(a, "dl") || esit(a, "form") || esit(a, "section") ||
           esit(a, "article") || esit(a, "header") || esit(a, "footer") || esit(a, "nav");
}

static void stil_it(dizgi_t *d, baglam_t *b, int st) {
    if (b->stil_derin < 32) b->stil_yigin[b->stil_derin++] = d->stil;
    d->stil = st;
}
static void stil_al(dizgi_t *d, baglam_t *b) {
    if (b->stil_derin > 0) d->stil = b->stil_yigin[--b->stil_derin];
}

static void alan_ciz(dizgi_t *d, const char *deger, int oge, int en) {
    sozcuk_bosalt(d);
    if (d->satir_yazi && d->sutun + en + 3 > d->s->genislik) yeni_satir(d);
    if (d->sutun == 0) girinti_yaz(d);
    if (d->bekleyen_bosluk && d->satir_yazi) hucre(d, ' ', ST_NORMAL, 0);
    hucre(d, '[', ST_ALAN, oge);
    int k = 0;
    const char *p = deger ? deger : "";
    while (*p && k < en) {
        uint32_u i = 0;
        uint32_u c = kod_al((const uint8_u *)p, 8, &i, KK_UTF8);
        p += i;
        hucre(d, gorunur(c) ? gorunur(c) : ' ', ST_ALAN, oge);
        k++;
    }
    while (k++ < en) hucre(d, '_', ST_ALAN, oge);
    hucre(d, ']', ST_ALAN, oge);
    d->satir_yazi = 1;
    d->bekleyen_bosluk = 1;
    d->bos_satir = 0;
}

static void kopya(char *h, int boy, const char *k) {
    int i = 0;
    while (k && k[i] && i < boy - 1) { h[i] = k[i]; i++; }
    h[i] = 0;
}

static void etiket_isle(dizgi_t *d, baglam_t *b, const etiket_t *e, sayfa_t *s) {
    const char *a = e->ad;

    if (!e->bitis) {
        /* gizli öğe: içeriği atla */
        const char *st = nitelik(e, "style");
        const char *ah = nitelik(e, "aria-hidden");
        if (!ic_icerik_yok(a) && (nitelik(e, "hidden") ||
            (st && (icerir_kucuk(st, "display:none") || icerir_kucuk(st, "display: none") ||
                    icerir_kucuk(st, "visibility:hidden"))) ||
            (ah && esit(ah, "true") && !esit(a, "a")))) {
            kopya(b->atla_ad, sizeof b->atla_ad, a);
            b->atla_derin = 1;
            return;
        }
    }

    if (esit(a, "br")) { if (!e->bitis) { sozcuk_bosalt(d); yeni_satir(d); d->bekleyen_bosluk = 0; } return; }
    if (esit(a, "hr")) {
        blok(d, 0);
        int w = s->genislik - d->girinti; if (w > 60) w = 60;
        girinti_yaz(d);
        for (int i = 0; i < w; i++) hucre(d, 0x2014, ST_SONUK, 0);
        d->satir_yazi = 1;
        blok(d, 0);
        return;
    }
    if (a[0] == 'h' && a[1] >= '1' && a[1] <= '6' && a[2] == 0) {
        blok(d, 1);
        if (!e->bitis) stil_it(d, b, ST_BASLIK); else { stil_al(d, b); blok(d, 1); }
        return;
    }
    if (esit(a, "li")) {
        if (e->bitis) { d->madde = 0; blok(d, 0); return; }
        d->madde = 0;
        blok(d, 0);
        girinti_yaz(d);
        int ol = b->liste_derin > 0 && b->liste_tur[b->liste_derin - 1] == 1;
        if (ol) {
            char t[12]; int n = ++b->liste_no[b->liste_derin - 1], k = 0;
            char r[12]; int m = 0;
            do { r[m++] = (char)('0' + n % 10); n /= 10; } while (n);
            while (m) t[k++] = r[--m];
            t[k++] = '.'; t[k] = 0;
            for (int i = 0; t[i]; i++) hucre(d, (uint32_u)t[i], ST_SONUK, 0);
        } else hucre(d, 0x2022, ST_SONUK, 0);
        hucre(d, ' ', ST_NORMAL, 0);
        d->satir_yazi = 0;          /* madde işaretinden sonra ilk sözcük boşluksuz */
        d->bekleyen_bosluk = 0;
        d->madde = 1;
        /* madde metni işaretin hizasından sürsün */
        return;
    }
    if (esit(a, "ul") || esit(a, "ol") || esit(a, "menu") || esit(a, "dir")) {
        blok(d, b->liste_derin == 0);
        if (!e->bitis) {
            if (b->liste_derin < 16) {
                b->liste_tur[b->liste_derin] = esit(a, "ol");
                const char *bas = nitelik(e, "start");
                int no = 0;
                if (bas) { for (; *bas >= '0' && *bas <= '9'; bas++) no = no * 10 + (*bas - '0'); no--; }
                b->liste_no[b->liste_derin] = no;
                b->liste_derin++;
            }
            d->girinti += 2;
        } else {
            if (b->liste_derin > 0) b->liste_derin--;
            d->girinti -= 2; if (d->girinti < 0) d->girinti = 0;
        }
        return;
    }
    if (esit(a, "blockquote") || esit(a, "dd")) {
        blok(d, esit(a, "blockquote"));
        if (!e->bitis) { d->girinti += 4; if (esit(a, "blockquote")) stil_it(d, b, ST_SONUK); }
        else { d->girinti -= 4; if (d->girinti < 0) d->girinti = 0; if (esit(a, "blockquote")) stil_al(d, b); }
        return;
    }
    if (esit(a, "pre") || esit(a, "listing") || esit(a, "xmp")) {
        blok(d, 1);
        if (!e->bitis) { d->onbicim++; d->pre_bas = 1; stil_it(d, b, ST_ONBICIM); }
        else { if (d->onbicim) d->onbicim--; stil_al(d, b); blok(d, 1); }
        return;
    }
    if (esit(a, "tr")) { blok(d, 0); b->tablo_hucre = 0; return; }
    if (esit(a, "td") || esit(a, "th")) {
        if (!e->bitis) {
            sozcuk_bosalt(d);
            if (b->tablo_hucre++ > 0) {
                if (d->sutun == 0) girinti_yaz(d);
                hucre(d, ' ', ST_NORMAL, 0); hucre(d, '|', ST_SONUK, 0);
                d->satir_yazi = 1; d->bekleyen_bosluk = 1;
            }
            if (esit(a, "th")) stil_it(d, b, ST_KALIN);
        } else if (esit(a, "th")) stil_al(d, b);
        return;
    }
    if (esit(a, "a")) {
        if (!e->bitis) {
            const char *h = nitelik(e, "href");
            if (h && *h) {
                sozcuk_bosalt(d);
                int o = oge_ekle(s, OGE_BAG, 0, depo(s, h, -1), 0);
                d->oge = o;
                stil_it(d, b, ST_BAG);
            }
        } else if (d->oge && s->ogeler[d->oge - 1].tur == OGE_BAG) {
            sozcuk_bosalt(d);
            /* görünür metni olmayan bağlantı (yalnız resim vb.) */
            int bos = 1;
            for (uint32_u i = s->satir[s->satir_adet]; i < s->kar_adet; i++) if (s->oge[i] == d->oge) bos = 0;
            if (bos) {
                for (int i = s->satir_adet - 1; i >= 0 && bos && i >= s->satir_adet - 3; i--)
                    for (uint32_u j = s->satir[i]; j < s->satir[i + 1]; j++) if (s->oge[j] == d->oge) bos = 0;
            }
            if (bos) metin_ascii(d, "[bağlantı]");
            sozcuk_bosalt(d);
            d->oge = 0;
            stil_al(d, b);
        }
        return;
    }
    if (esit(a, "b") || esit(a, "strong")) { if (!e->bitis) stil_it(d, b, d->stil == ST_BAG ? ST_BAG : ST_KALIN); else stil_al(d, b); return; }
    if (esit(a, "code") || esit(a, "kbd") || esit(a, "samp") || esit(a, "tt")) {
        if (!e->bitis) stil_it(d, b, d->stil == ST_BAG ? ST_BAG : ST_ONBICIM); else stil_al(d, b); return;
    }
    if (esit(a, "img")) {
        const char *alt = nitelik(e, "alt");
        if (alt && *alt) {
            sozcuk_bosalt(d);
            int st = d->stil;
            if (d->stil != ST_BAG) d->stil = ST_RESIM;
            metin_kod(d, '[');
            metin_ascii(d, alt);
            metin_kod(d, ']');
            d->stil = st;
        }
        return;
    }
    if (esit(a, "form")) {
        blok(d, 1);
        if (!e->bitis && s->form_adet < H_MAX_FORM) {
            const char *act = nitelik(e, "action");
            const char *m = nitelik(e, "method");
            b->form = s->form_adet;
            s->formlar[s->form_adet].eylem = depo(s, act ? act : "", -1);
            s->formlar[s->form_adet].post = (m && onek_kucuk(m, "post")) ? 1 : 0;
            s->form_adet++;
        } else if (e->bitis) b->form = -1;
        return;
    }
    if (esit(a, "input")) {
        if (e->bitis) return;
        const char *tur = nitelik(e, "type");
        const char *ad = nitelik(e, "name");
        const char *deger = nitelik(e, "value");
        int f = b->form >= 0 ? b->form : 0;
        if (!tur || esit(tur, "text") || esit(tur, "search") || esit(tur, "email") || esit(tur, "url") ||
            esit(tur, "number") || esit(tur, "tel")) {
            int o = oge_ekle(s, OGE_METIN_ALANI, f, depo(s, ad ? ad : "", -1), depo(s, deger ? deger : "", -1));
            const char *ph = nitelik(e, "placeholder");
            alan_ciz(d, (deger && *deger) ? deger : (ph ? ph : ""), o, 22);
        } else if (esit(tur, "hidden")) {
            if (ad && s->gizli_adet < 512) {
                s->gizli[s->gizli_adet][0] = (uint32_u)f;
                s->gizli[s->gizli_adet][1] = depo(s, ad, -1);
                s->gizli[s->gizli_adet][2] = depo(s, deger ? deger : "", -1);
                s->gizli_adet++;
            }
        } else if (esit(tur, "submit") || esit(tur, "button") || esit(tur, "image")) {
            int o = oge_ekle(s, OGE_GONDER, f, depo(s, ad ? ad : "", -1), depo(s, deger ? deger : "", -1));
            sozcuk_bosalt(d);
            int st = d->stil, og = d->oge;
            d->stil = ST_ALAN; d->oge = o;
            metin_kod(d, '[');
            metin_ascii(d, (deger && *deger) ? deger : "Gönder");
            metin_kod(d, ']');
            sozcuk_bosalt(d);
            d->stil = st; d->oge = og;
        } else if (esit(tur, "checkbox") || esit(tur, "radio")) {
            int radyo = esit(tur, "radio");
            int o = oge_ekle(s, radyo ? OGE_SECENEK : OGE_ISARET, f, depo(s, ad ? ad : "", -1), depo(s, deger ? deger : "on", -1));
            if (o && nitelik(e, "checked")) {
                s->ogeler[o - 1].secili = 1;
                /* radyo grubunda tek seçili: önceki seçili (aynı form + ad)
                 * bırakılır ve işareti silinir (tarayıcılarda son "checked" kazanır) */
                if (radyo) for (int k = 0; k < o - 1; k++) {
                    h_oge_t *q = &s->ogeler[k];
                    if (q->tur == OGE_SECENEK && q->form == s->ogeler[o - 1].form && q->secili &&
                        esit(h_metin(s, q->ad), h_metin(s, s->ogeler[o - 1].ad))) {
                        q->secili = 0;
                        for (uint32_u j = 0; j < s->kar_adet; j++)
                            if (s->oge[j] == k + 1 && H_KOD(s->kar[j]) == 'x') s->kar[j] = (s->kar[j] & ~0x1FFFFFu) | ' ';
                    }
                }
            }
            sozcuk_bosalt(d);
            int st = d->stil, og = d->oge;
            d->stil = ST_ALAN; d->oge = o;
            metin_kod(d, radyo ? '(' : '[');
            metin_kod(d, (o && s->ogeler[o - 1].secili) ? 'x' : ' ');
            metin_kod(d, radyo ? ')' : ']');
            sozcuk_bosalt(d);
            d->stil = st; d->oge = og;
        }
        return;
    }
    if (esit(a, "button")) {
        if (!e->bitis) {
            const char *tur = nitelik(e, "type");
            if (tur && !esit(tur, "submit")) return;
            const char *ad = nitelik(e, "name");
            const char *deger = nitelik(e, "value");
            int f = b->form >= 0 ? b->form : 0;
            int o = oge_ekle(s, OGE_GONDER, f, depo(s, ad ? ad : "", -1), depo(s, deger ? deger : "", -1));
            sozcuk_bosalt(d);
            metin_kod(d, '[');
            sozcuk_bosalt(d);
            b->dugme_oge = o;
            d->oge = o;
            stil_it(d, b, ST_ALAN);
        } else if (b->dugme_oge) {
            metin_kod(d, ']');
            sozcuk_bosalt(d);
            d->oge = 0; b->dugme_oge = 0;
            stil_al(d, b);
        }
        return;
    }
    if (esit(a, "select")) {
        if (!e->bitis) {
            const char *ad = nitelik(e, "name");
            int f = b->form >= 0 ? b->form : 0;
            if (s->gizli_adet < 512 && ad) {
                s->gizli[s->gizli_adet][0] = (uint32_u)f;
                s->gizli[s->gizli_adet][1] = depo(s, ad, -1);
                s->gizli[s->gizli_adet][2] = 0;
                b->select_oge = s->gizli_adet + 1;
                s->gizli_adet++;
            } else b->select_oge = -1;
            b->select_yazildi = 0;
        } else b->select_oge = 0;
        return;
    }
    if (esit(a, "option")) {
        if (!e->bitis && b->select_oge > 0) {
            const char *v = nitelik(e, "value");
            uint32_u *g = s->gizli[b->select_oge - 1];
            if (g[2] == 0 || nitelik(e, "selected")) g[2] = depo(s, v ? v : "", -1);
        }
        return;
    }
    if (esit(a, "textarea")) return;       /* ham metin olarak ayrıca işlenir */
    if (esit(a, "meta") && !e->bitis) {
        const char *he = nitelik(e, "http-equiv");
        const char *ic = nitelik(e, "content");
        if (he && ic && onek_kucuk(he, "refresh")) {
            const char *u = ic;
            for (; *u; u++) if (onek_kucuk(u, "url=")) { u += 4; break; }
            if (*u) {
                while (*u == '\'' || *u == '"' || *u == ' ') u++;
                kopya(s->yenile, sizeof s->yenile, u);
                int n = 0; while (s->yenile[n]) n++;
                while (n > 0 && (s->yenile[n - 1] == '\'' || s->yenile[n - 1] == '"')) s->yenile[--n] = 0;
            }
        }
        return;
    }
    if (esit(a, "base") && !e->bitis) {
        const char *h = nitelik(e, "href");
        if (h) kopya(s->taban, sizeof s->taban, h);
        return;
    }
    if (esit(a, "dt")) { blok(d, 0); if (!e->bitis) stil_it(d, b, ST_KALIN); else stil_al(d, b); return; }
    if (blok_mu(a)) { blok(d, bosluklu_blok(a)); return; }
}

int html_isle(const uint8_u *g, uint32_u n, int kk, int duz_metin, int genislik, sayfa_t *s) {
    static dizgi_t d;
    static baglam_t b;
    static etiket_t e;
    if (genislik < 20) genislik = 20;
    s->genislik = genislik;
    s->kar_adet = 0; s->satir_adet = 0; s->satir[0] = 0; s->satir[1] = 0;
    s->oge_adet = 0; s->form_adet = 0; s->gizli_adet = 0; s->metin_boy = 0;
    s->baslik[0] = 0; s->yenile[0] = 0; s->taban[0] = 0; s->kesik = 0;
    d.s = s; d.sutun = 0; d.girinti = 0; d.bekleyen_bosluk = 0; d.bos_satir = 1;
    d.stil = ST_NORMAL; d.oge = 0; d.onbicim = duz_metin ? 1 : 0; d.sz_adet = 0; d.satir_yazi = 0;
    d.madde = 0; d.pre_bas = 0;
    b.liste_derin = 0; b.form = -1; b.dugme_oge = 0; b.tablo_hucre = 0; b.stil_derin = 0;
    b.atla_derin = 0; b.atla_ad[0] = 0; b.select_oge = 0; b.select_yazildi = 0;
    if (kk < 0) kk = KK_UTF8;
    uint32_u i = 0;
    if (n >= 3 && g[0] == 0xEF && g[1] == 0xBB && g[2] == 0xBF) i = 3;   /* BOM */

    if (duz_metin) {
        while (i < n) metin_kod(&d, kod_al(g, n, &i, kk));
        blok(&d, 0);
        return 0;
    }

    int bas_icinde = 0;                          /* <head> */
    while (i < n && !s->kesik) {
        uint8_u c = g[i];
        if (c == '<') {
            if (i + 3 < n && g[i+1] == '!' && g[i+2] == '-' && g[i+3] == '-') {      /* yorum */
                uint32_u p = i + 4;
                while (p + 2 < n && !(g[p] == '-' && g[p+1] == '-' && g[p+2] == '>')) p++;
                i = p + 3 > n ? n : p + 3;
                continue;
            }
            if (i + 1 < n && (g[i+1] == '!' || g[i+1] == '?')) {                       /* doctype vb. */
                while (i < n && g[i] != '>') i++;
                i++;
                continue;
            }
            uint32_u eski = i;
            if (!etiket_oku(g, n, &i, kk, &e)) {                                       /* düz '<' */
                i = eski + 1;
                if (!b.atla_derin && !bas_icinde) metin_kod(&d, '<');
                continue;
            }
            const char *a = e.ad;
            /* gizli öğe içindeyiz: yalnız aynı adlı etiketleri say */
            if (b.atla_derin) {
                if (esit(a, b.atla_ad) && !e.kendi_kapali) b.atla_derin += e.bitis ? -1 : 1;
                continue;
            }
            if (esit(a, "head")) { bas_icinde = !e.bitis; continue; }
            if (esit(a, "body")) { bas_icinde = 0; continue; }
            if (!e.bitis && (esit(a, "script") || esit(a, "style") || esit(a, "template") ||
                             esit(a, "svg") || esit(a, "math") || esit(a, "iframe") || esit(a, "object") ||
                             esit(a, "canvas") || esit(a, "audio") || esit(a, "video"))) {
                if (e.kendi_kapali) continue;
                i = ham_son(g, n, i, a);
                while (i < n && g[i] != '>') i++;
                i++;
                continue;
            }
            if (!e.bitis && (esit(a, "title") || esit(a, "textarea"))) {
                uint32_u son = ham_son(g, n, i, a);
                if (esit(a, "title")) {
                    if (!s->baslik[0]) {
                        int k = 0; int onceki_bosluk = 1;
                        uint32_u p = i;
                        while (p < son && k < (int)sizeof(s->baslik) - 5) {
                            uint32_u kod;
                            if (g[p] == '&') {
                                uint32_u tuk; int gec;
                                kod = varlik_coz(g + p + 1, son - p - 1, &tuk, &gec);
                                if (gec) p += 1 + tuk; else { kod = '&'; p++; }
                            } else kod = kod_al(g, son, &p, kk);
                            if (bosluk_mu(kod) || kod == 0xA0) { if (onceki_bosluk) continue; kod = ' '; onceki_bosluk = 1; }
                            else onceki_bosluk = 0;
                            k += utf8_yaz(kod, s->baslik + k);
                        }
                        while (k > 0 && s->baslik[k - 1] == ' ') k--;
                        s->baslik[k] = 0;
                    }
                } else if (!bas_icinde) {                                              /* textarea */
                    const char *ad = nitelik(&e, "name");
                    char deger[512]; int k = 0;
                    uint32_u p = i;
                    while (p < son && k < 500) {
                        uint32_u kod;
                        if (g[p] == '&') {                 /* &amp; vb. çözülür (başlıktaki gibi) */
                            uint32_u tuk; int gec;
                            kod = varlik_coz(g + p + 1, son - p - 1, &tuk, &gec);
                            if (gec) p += 1 + tuk; else { kod = '&'; p++; }
                        } else kod = kod_al(g, son, &p, kk);
                        k += utf8_yaz(kod, deger + k);
                    }
                    deger[k] = 0;
                    int o = oge_ekle(s, OGE_METIN_ALANI, b.form >= 0 ? b.form : 0, depo(s, ad ? ad : "", -1), depo(s, deger, -1));
                    blok(&d, 0);
                    alan_ciz(&d, deger, o, 40);
                    blok(&d, 0);
                }
                i = son;
                while (i < n && g[i] != '>') i++;
                i++;
                continue;
            }
            if (bas_icinde && !esit(a, "meta") && !esit(a, "base")) continue;
            etiket_isle(&d, &b, &e, s);
            continue;
        }
        if (b.atla_derin || bas_icinde) { i++; continue; }
        if (c == '&') {
            uint32_u tuk; int gec;
            uint32_u k = varlik_coz(g + i + 1, n - i - 1, &tuk, &gec);
            if (gec) { i += 1 + tuk; if (k) metin_kod(&d, k); continue; }
            metin_kod(&d, '&'); i++;
            continue;
        }
        metin_kod(&d, kod_al(g, n, &i, kk));
    }
    blok(&d, 0);
    /* sondaki boş satırları at */
    while (s->satir_adet > 0 && s->satir[s->satir_adet - 1] == s->satir[s->satir_adet]) s->satir_adet--;
    return 0;
}
