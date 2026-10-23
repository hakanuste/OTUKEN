/* =====================================================================
 * TULPAR — ÖTÜKEN'in metin tabanlı ağ gezgini (1.4.9)
 *
 * Kendi adres alanında, yetkisi: EKRAN | KLAVYE | SISTEM | NET_SEND |
 * NET_RECV (disk YOK). Sayfayı indirir (http.c), HTML'i okunur metne
 * çevirir (html.c), pencerede gösterir. JavaScript, CSS yerleşimi ve
 * resim YOKTUR; bağlantılar, başlıklar, listeler, tablolar ve GET
 * formları (arama kutuları) çalışır.
 *
 * Tuşlar: F6 / Ctrl+L adres · Enter git · ↑↓ satır · PgUp/PgDn sayfa ·
 * Home/End · ←→ bağlantı seç, Enter aç · Backspace geri · F5 yenile.
 * ===================================================================== */
#include "http.h"
#include "html.h"

#define GEN 960
#define YUK 640
#define UST 34                  /* araç çubuğu */
#define ALT 22                  /* durum çubuğu */
#define SOL 10
#define SATIR_YUK 18
#define KAYDIRMA_EN 10
#define SUTUN ((GEN - SOL - KAYDIRMA_EN - 14) / 8)
#define GORUNUR_SATIR ((YUK - UST - ALT - 6) / SATIR_YUK)

/* renkler: kâğıt (uyg.h ortak renklerinden R_YAZI/R_SONUK burada kâğıda göre) */
#undef R_YAZI
#undef R_SONUK
#define R_KAGIT   0xF3EEE2u
#define R_YAZI    0x1E262Bu
#define R_BAG     0x1A5FB4u
#define R_BASLIK  0x8A3413u
#define R_ONBICIM 0x2F4F4Fu
#define R_ONB_ARKA 0xE6E0D0u
#define R_SONUK   0x6B6B6Bu
#define R_KALIN   0x000000u
#define R_ALAN    0x205A2Au
#define R_ALAN_ARKA 0xFFFFFFu
#define R_RESIM   0x6B5B95u
#define R_SECILI  0xFFE08Au
/* araç çubuğu */
#define R_CUBUK   0x1B313Au
#define R_CUBUK_YAZI 0xE8E2D0u
#define R_ADRES_ARKA 0x0F1E25u
#define R_VURGU   0x4FB3A9u
#define R_HATA    0xD9534Fu

#define ARAMA "https://lite.duckduckgo.com/lite/?q="

static int pen;
static uint8_u govde[2 * 1024 * 1024];
static sayfa_t sayfa;
static http_yanit_t yanit;

static char adres[1024];          /* gösterilen/düzenlenen */
static int  adres_imlec;
static int  adres_modu;           /* 1: adres yazılıyor */
static char adres_yedek[1024];
static int  adres_secili;         /* adres kutusuna yeni girildi: ilk tuş hepsini değiştirir */
static url_t simdiki;             /* gösterilen sayfanın adresi (taban) */
static int  simdiki_yerel;        /* about: sayfası */
static int  ust_satir;            /* kaydırma */
static int  secili = -1;          /* klavyeyle seçili öğe (oge no, 0 tabanlı) */
static char durum[200];
static uint32_u durum_renk = R_CUBUK_YAZI;

/* form alanı düzenleme */
static int  alan_modu = -1;       /* düzenlenen öğe */
static char alan_metin[512];
static int  alan_imlec;
/* sayfa başına alan değerleri (kullanıcının yazdıkları) */
static char alan_deger[64][512];
static int  alan_oge[64];
static int  alan_adet;

/* geçmiş */
#define GECMIS 32
static char gecmis[GECMIS][1024];
static int  gecmis_kaydir[GECMIS];
static int  gecmis_n, gecmis_i = -1;

/* ------------------------------------------------------------------ */
static int uzun(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void kopya(char *d, int boy, const char *s) {
    int i = 0; while (i < boy - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static void durum_yaz(const char *a, const char *b, uint32_u renk) {
    kopya(durum, sizeof durum, a);
    if (b) dize_ekle(durum, sizeof durum, b);
    durum_renk = renk;
}

/* UTF-8 dizeyi en çok 40 kod noktalık parçalarla çizer (çekirdek 127 bayt kabul eder) */
static void yazi(int x, int y, const char *s, uint32_u renk) {
    char p[168];
    while (*s) {
        int b = 0, k = 0;
        while (*s && k < 40) {
            unsigned char c = (unsigned char)*s;
            int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
            for (int j = 0; j < n && *s; j++) p[b++] = *s++;
            k++;
        }
        p[b] = 0;
        pencere_yazi(pen, x, y, p, renk);
        x += k * 8;
    }
}

static uint32_u stil_renk(int st) {
    switch (st) {
    case ST_BASLIK:  return R_BASLIK;
    case ST_BAG:     return R_BAG;
    case ST_ONBICIM: return R_ONBICIM;
    case ST_SONUK:   return R_SONUK;
    case ST_KALIN:   return R_KALIN;
    case ST_ALAN:    return R_ALAN;
    case ST_RESIM:   return R_RESIM;
    default:         return R_YAZI;
    }
}

static int esit_dize(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* o. öğenin görünen [x]/[ ] ya da (x)/( ) hücresini güncelle */
static void isaret_ciz(int o) {
    int sec = sayfa.ogeler[o].secili;
    for (uint32_u j = 0; j < sayfa.kar_adet; j++)
        if (sayfa.oge[j] == o + 1 && (H_KOD(sayfa.kar[j]) == 'x' || H_KOD(sayfa.kar[j]) == ' '))
            sayfa.kar[j] = (sayfa.kar[j] & ~0x1FFFFFu) | (sec ? 'x' : ' ');
}

/* düzenlenen alanın metnini sakla (Enter ya da başka yere tıklama) */
static void alan_sakla(int o) {
    int i;
    for (i = 0; i < alan_adet; i++) if (alan_oge[i] == o + 1) break;
    if (i == alan_adet && alan_adet < 64) alan_adet++;
    if (i < 64) { alan_oge[i] = o + 1; kopya(alan_deger[i], 512, alan_metin); }
}

/* alan düzenlenmişse onun değeri */
static const char *alan_degeri(int oge) {
    for (int i = 0; i < alan_adet; i++) if (alan_oge[i] == oge) return alan_deger[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/* çizim                                                               */
/* ------------------------------------------------------------------ */
static void cubuk_ciz(void) {
    pencere_dikdortgen(pen, 0, 0, GEN, UST, R_CUBUK);
    pencere_dikdortgen(pen, 6, 5, 58, 24, gecmis_i > 0 ? 0x2A4652u : 0x22363Fu);
    yazi(12, 9, "‹ Geri", gecmis_i > 0 ? R_CUBUK_YAZI : 0x6F7F84u);
    pencere_dikdortgen(pen, 70, 5, 66, 24, 0x2A4652u);
    yazi(76, 9, "Yenile", R_CUBUK_YAZI);
    int ax = 144, aw = GEN - ax - 8;
    pencere_dikdortgen(pen, ax, 5, aw, 24, adres_modu ? 0xFFFFFFu : R_ADRES_ARKA);
    if (adres_modu) pencere_dikdortgen(pen, ax, 27, aw, 2, R_VURGU);
    /* adres: sığmayanın sonu görünür */
    int en = (aw - 12) / 8;
    int n = 0; const char *p = adres;
    int kod = 0; for (const char *q = adres; *q; q++) if ((*q & 0xC0) != 0x80) kod++;
    int atla = kod > en ? kod - en : 0;
    if (adres_modu && adres_imlec < atla) atla = adres_imlec;
    while (*p && n < atla) { p++; while ((*p & 0xC0) == 0x80) p++; n++; }
    if (adres_modu && adres_secili && *p) {
        int kc = 0; for (const char *q = p; *q; q++) if ((*q & 0xC0) != 0x80) kc++;
        if (kc > en) kc = en;
        pencere_dikdortgen(pen, ax + 5, 7, kc * 8 + 2, 20, 0xBFD9F2u);
    }
    yazi(ax + 6, 9, p, adres_modu ? 0x10181Cu : R_CUBUK_YAZI);
    if (adres_modu) {
        int ix = ax + 6 + (adres_imlec - atla) * 8;
        pencere_dikdortgen(pen, ix, 8, 2, 18, R_BAG);
    }
}

static void durum_ciz(void) {
    int y = YUK - ALT;
    pencere_dikdortgen(pen, 0, y, GEN, ALT, R_CUBUK);
    if (alan_modu >= 0) {
        yazi(8, y + 3, "Yaz: ", R_VURGU);
        pencere_dikdortgen(pen, 52, y + 2, GEN - 60, 18, 0xFFFFFFu);
        yazi(56, y + 3, alan_metin, 0x10181Cu);
        pencere_dikdortgen(pen, 56 + alan_imlec * 8, y + 3, 2, 16, R_BAG);
        return;
    }
    yazi(8, y + 3, durum, durum_renk);
    if (sayfa.satir_adet > GORUNUR_SATIR) {
        char t[32], u[12];
        int yuzde = (ust_satir + GORUNUR_SATIR) * 100 / sayfa.satir_adet;
        if (yuzde > 100) yuzde = 100;
        sayi_yaz(u, (uint64_u)yuzde, 1);
        t[0] = 0; dize_ekle(t, sizeof t, u); dize_ekle(t, sizeof t, "%");
        yazi(GEN - 8 - uzun(t) * 8, y + 3, t, 0x9FB2B7u);
    }
}

static void sayfa_ciz(void) {
    pencere_dikdortgen(pen, 0, UST, GEN, YUK - UST - ALT, R_KAGIT);
    int y0 = UST + 4;
    for (int r = 0; r < GORUNUR_SATIR; r++) {
        int sat = ust_satir + r;
        if (sat >= sayfa.satir_adet) break;
        uint32_u bas = sayfa.satir[sat], son = sayfa.satir[sat + 1];
        int y = y0 + r * SATIR_YUK;
        /* ardışık aynı biçemli hücreleri tek parça çiz */
        uint32_u j = bas;
        while (j < son) {
            int st = H_STIL(sayfa.kar[j]);
            int og = sayfa.oge[j];
            uint32_u k = j;
            static char parca[1024];
            int b = 0;
            while (k < son && H_STIL(sayfa.kar[k]) == st && sayfa.oge[k] == og && b < 1000) {
                b += utf8_yaz(H_KOD(sayfa.kar[k]), parca + b);
                k++;
            }
            parca[b] = 0;
            int x = SOL + (int)(j - bas) * 8;
            int w = (int)(k - j) * 8;
            if (st == ST_ONBICIM) pencere_dikdortgen(pen, x, y - 1, w, SATIR_YUK, R_ONB_ARKA);
            if (og && secili == og - 1) pencere_dikdortgen(pen, x, y - 1, w, SATIR_YUK, R_SECILI);
            if (st == ST_ALAN && og && sayfa.ogeler[og - 1].tur == OGE_METIN_ALANI) {
                pencere_dikdortgen(pen, x, y - 1, w, SATIR_YUK, R_ALAN_ARKA);
                const char *dg = alan_degeri(og);
                if (dg) {           /* kullanıcının yazdığı değer */
                    static char g2[1024];
                    int n2 = 0;
                    g2[n2++] = '[';
                    int kc = 0;
                    for (const char *q = dg; *q && kc < (int)(k - j) - 2; ) {
                        unsigned char c = (unsigned char)*q;
                        int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
                        for (int z = 0; z < n && *q; z++) g2[n2++] = *q++;
                        kc++;
                    }
                    while (kc++ < (int)(k - j) - 2) g2[n2++] = '_';
                    g2[n2++] = ']'; g2[n2] = 0;
                    yazi(x, y, g2, R_ALAN);
                    j = k;
                    continue;
                }
            }
            yazi(x, y, parca, stil_renk(st));
            if (st == ST_BAG) {
                /* baştaki/sondaki boşluğun altı çizilmez */
                int bb = 0, ss = 0;
                while (j + (uint32_u)bb < k && H_KOD(sayfa.kar[j + bb]) == ' ') bb++;
                while (k - (uint32_u)ss > j + (uint32_u)bb && H_KOD(sayfa.kar[k - 1 - ss]) == ' ') ss++;
                int uw = w - (bb + ss) * 8;
                if (uw > 0) pencere_dikdortgen(pen, x + bb * 8, y + 15, uw, 1, R_BAG);
            }
            j = k;
        }
    }
    /* kaydırma çubuğu */
    int kx = GEN - KAYDIRMA_EN - 4, ky = UST + 2, kh = YUK - UST - ALT - 4;
    pencere_dikdortgen(pen, kx, ky, KAYDIRMA_EN, kh, 0xE0D9C8u);
    if (sayfa.satir_adet > GORUNUR_SATIR) {
        int th = kh * GORUNUR_SATIR / sayfa.satir_adet; if (th < 20) th = 20;
        int ty = ky + (kh - th) * ust_satir / (sayfa.satir_adet - GORUNUR_SATIR);
        pencere_dikdortgen(pen, kx, ty, KAYDIRMA_EN, th, 0x8C8577u);
    }
}

static void ciz(void) {
    cubuk_ciz();
    sayfa_ciz();
    durum_ciz();
    pencere_sun(pen);
}

static void sadece_cubuklar(void) {
    cubuk_ciz();
    durum_ciz();
    pencere_sun(pen);
}

/* ------------------------------------------------------------------ */
/* yerel sayfalar                                                      */
/* ------------------------------------------------------------------ */
static const char HAKKINDA[] =
    "<html><head><title>TULPAR</title></head><body>"
    "<h1>TULPAR — ÖTÜKEN Ağ Gezgini</h1>"
    "<p>Metin tabanlı gezgin. Sayfanın yazısını, başlıklarını, listelerini, tablolarını ve "
    "bağlantılarını gösterir. <b>JavaScript, resim ve sayfa biçimi (CSS) yoktur</b>; bunlara dayanan "
    "siteler (görüntülü, uygulama gibi siteler) çalışmaz. Haber, ansiklopedi, belge ve arama sayfaları "
    "okunabilir.</p>"
    "<h2>Tuşlar</h2><ul>"
    "<li><b>F6</b> ya da <b>Ctrl+L</b>: adres yaz. Nokta içermeyen ya da boşluklu yazı arama olur.</li>"
    "<li><b>↑ ↓</b> satır, <b>PgUp PgDn</b> sayfa, <b>Home End</b> baş/son</li>"
    "<li><b>← →</b> bağlantı seç, <b>Enter</b> aç. Fareyle tıklamak da olur.</li>"
    "<li><b>Backspace</b> geri, <b>F5</b> yenile, yüklerken <b>Esc</b> durdurur.</li>"
    "<li>Arama kutusuna tıkla, yaz, Enter: form gönderilir.</li></ul>"
    "<h2>Güvenlik</h2><ul><li>HTTPS: TLS 1.2 (BearSSL). Sertifika Mozilla kök listesiyle, ad ve "
    "tarih denetlenerek doğrulanır; doğrulanamayan site AÇILMAZ (geçersiz sertifikayı kabul etme "
    "seçeneği yoktur). Saat yanlışsa sertifikalar reddedilir.</li>"
    "<li>TULPAR diske erişemez: indirilen hiçbir şey diske yazılmaz.</li></ul>"
    "<h2>Sınırlar</h2><ul><li>TLS 1.3 yok (sitelerin büyük çoğunluğu 1.2 kabul eder).</li>"
    "<li>Formlar GET ile gönderilir (POST yok). Çerez (oturum) yok: giriş gerektiren siteler çalışmaz.</li>"
    "<li>Yazı tipinde Latin, Türkçe, Orta Asya Türk dilleri, Kiril ve Yunan harfleri var; "
    "Çince, Arapça, emoji gibi harfler soru işareti görünür.</li></ul>"
    "<p>Dene: <a href=\"https://tr.wikipedia.org/wiki/%C3%96t%C3%BCken\">Vikipedi: Ötüken</a> · "
    "<a href=\"https://lite.duckduckgo.com/lite/\">DuckDuckGo (arama)</a> · "
    "<a href=\"http://example.com/\">example.com</a></p>"
    "</body></html>";

static void yerel_goster(const char *html, const char *url) {
    html_isle((const uint8_u *)html, (uint32_u)uzun(html), KK_UTF8, 0, SUTUN, &sayfa);
    url_coz("http://tulpar/", &simdiki);
    simdiki_yerel = 1;
    kopya(adres, sizeof adres, url);
    ust_satir = 0; secili = -1; alan_adet = 0;
}

static char hata_html[3000];
static void hata_sayfasi(const char *url, const char *neden) {
    hata_html[0] = 0;
    dize_ekle(hata_html, sizeof hata_html, "<title>Açılamadı</title><h1>Sayfa açılamadı</h1><p>");
    dize_ekle(hata_html, sizeof hata_html, neden);
    dize_ekle(hata_html, sizeof hata_html, "</p><p>Adres: <code>");
    /* adresi kaçışlı yaz */
    char t[8];
    for (const char *p = url; *p; p++) {
        if (*p == '<') dize_ekle(hata_html, sizeof hata_html, "&lt;");
        else if (*p == '&') dize_ekle(hata_html, sizeof hata_html, "&amp;");
        else { t[0] = *p; t[1] = 0; dize_ekle(hata_html, sizeof hata_html, t); }
    }
    dize_ekle(hata_html, sizeof hata_html, "</code></p><p><a href=\"about:tulpar\">TULPAR yardım</a></p>");
    html_isle((const uint8_u *)hata_html, (uint32_u)uzun(hata_html), KK_UTF8, 0, SUTUN, &sayfa);
    kopya(adres, sizeof adres, url);
    ust_satir = 0; secili = -1; alan_adet = 0;
}

/* ------------------------------------------------------------------ */
/* yükleme                                                             */
/* ------------------------------------------------------------------ */
static int iptal;
static int ilerleme(uint32_u alinan, void *v) {
    (void)v;
    static uint32_u son;
    if (alinan < son) son = 0;
    if (alinan - son >= 32 * 1024 || alinan < 4096) {
        son = alinan;
        char t[24];
        sayi_yaz(t, alinan / 1024, 1);
        durum_yaz("Yükleniyor… ", t, R_VURGU);
        dize_ekle(durum, sizeof durum, " KB   (Esc: durdur)");
        sadece_cubuklar();
    }
    olay_u o;
    while (pencere_olay(pen, &o) == 1) {
        if (o.tur == OLAY_KAPAT) { iptal = 2; return 0; }
        if (o.tur == OLAY_TUS && o.tus == 27) { iptal = 1; return 0; }
    }
    return 1;
}

static int metin_turu(const char *tur) {       /* 0 html, 1 düz metin, 2 gösterilemez */
    if (!tur[0]) return 0;
    const char *p = tur;
    int html = 0, text = 0;
    for (; *p && *p != ';'; p++) {
        if ((p[0] == 'h' || p[0] == 'H') && (p[1] == 't' || p[1] == 'T') && (p[2] == 'm' || p[2] == 'M')) html = 1;
        if ((p[0] == 'x' || p[0] == 'X') && (p[1] == 'm' || p[1] == 'M') && (p[2] == 'l' || p[2] == 'L')) html = 1;
    }
    if ((tur[0] == 't' || tur[0] == 'T') && (tur[1] == 'e' || tur[1] == 'E') && (tur[2] == 'x' || tur[2] == 'X')) text = 1;
    if (html) return 0;
    if (text) return 1;
    return 2;
}

static void gecmise_ekle(const char *url) {
    if (gecmis_i >= 0 && gecmis_i < GECMIS) gecmis_kaydir[gecmis_i] = ust_satir;
    if (gecmis_i >= 0 && !strcmp(gecmis[gecmis_i], url)) return;
    if (gecmis_i + 1 >= GECMIS) {
        for (int i = 1; i < GECMIS; i++) { kopya(gecmis[i - 1], 1024, gecmis[i]); gecmis_kaydir[i - 1] = gecmis_kaydir[i]; }
        gecmis_i--;
    }
    gecmis_i++;
    kopya(gecmis[gecmis_i], 1024, url);
    gecmis_kaydir[gecmis_i] = 0;
    gecmis_n = gecmis_i + 1;
}

static int git(const char *url, int gecmise, int yenileme_derinlik);

static int yukle(const char *url, int gecmise, int derin) {
    if (url[0] == 'a' && url[1] == 'b' && url[2] == 'o' && url[3] == 'u' && url[4] == 't' && url[5] == ':') {
        yerel_goster(HAKKINDA, "about:tulpar");
        if (gecmise) gecmise_ekle("about:tulpar");
        durum_yaz("TULPAR yardım", 0, R_CUBUK_YAZI);
        return 0;
    }
    kopya(adres, sizeof adres, url);
    durum_yaz("Bağlanıyor…", 0, R_VURGU);
    adres_modu = 0;
    sadece_cubuklar();
    iptal = 0;
    int r = http_al(url, &yanit, govde, sizeof govde, ilerleme, 0);
    if (iptal == 2) { pencere_kapat(pen); exit(); }
    if (r != 0) {
        hata_sayfasi(yanit.son_url[0] ? yanit.son_url : url, iptal ? "Yükleme durduruldu." : yanit.hata);
        durum_yaz(iptal ? "Durduruldu" : yanit.hata, 0, iptal ? R_CUBUK_YAZI : R_HATA);
        if (gecmise) gecmise_ekle(yanit.son_url[0] ? yanit.son_url : url);
        return -1;
    }
    url_coz(yanit.son_url, &simdiki);
    simdiki_yerel = 0;
    kopya(adres, sizeof adres, yanit.son_url);
    if (gecmise) gecmise_ekle(yanit.son_url);
    int tur = metin_turu(yanit.tur);
    if (tur == 2) {
        static char bilgi[400];
        char t[24]; sayi_yaz(t, yanit.govde_boy / 1024, 1);
        bilgi[0] = 0;
        dize_ekle(bilgi, sizeof bilgi, "<title>Gösterilemez</title><h1>Bu içerik gösterilemez</h1><p>Tür: <code>");
        dize_ekle(bilgi, sizeof bilgi, yanit.tur);
        dize_ekle(bilgi, sizeof bilgi, "</code>, boy: ");
        dize_ekle(bilgi, sizeof bilgi, t);
        dize_ekle(bilgi, sizeof bilgi, " KB. TULPAR yalnız HTML ve düz metin gösterir.</p>");
        html_isle((const uint8_u *)bilgi, (uint32_u)uzun(bilgi), KK_UTF8, 0, SUTUN, &sayfa);
    } else {
        int kk = html_kodlama_ad(yanit.tur);
        if (kk < 0 && tur == 0) kk = html_kodlama_bul(govde, yanit.govde_boy);
        html_isle(govde, yanit.govde_boy, kk < 0 ? KK_UTF8 : kk, tur == 1, SUTUN, &sayfa);
    }
    ust_satir = 0; secili = -1; alan_adet = 0;
    /* durum: başlık, boy, süre */
    {
        char t[24];
        durum[0] = 0;
        if (yanit.durum != 200) {
            dize_ekle(durum, sizeof durum, "HTTP ");
            sayi_yaz(t, (uint64_u)yanit.durum, 1); dize_ekle(durum, sizeof durum, t);
            dize_ekle(durum, sizeof durum, " · ");
        }
        if (simdiki.guvenli) dize_ekle(durum, sizeof durum, "Güvenli (TLS) · ");
        dize_ekle(durum, sizeof durum, sayfa.baslik[0] ? sayfa.baslik : "(başlıksız)");
        dize_ekle(durum, sizeof durum, " · ");
        sayi_yaz(t, (yanit.govde_boy + 1023) / 1024, 1); dize_ekle(durum, sizeof durum, t);
        dize_ekle(durum, sizeof durum, " KB");
        if (yanit.kesik || sayfa.kesik) dize_ekle(durum, sizeof durum, " (kısaltıldı)");
        durum_renk = yanit.durum >= 400 ? R_HATA : R_CUBUK_YAZI;
    }
    /* <meta refresh> yönlendirmesi */
    if (sayfa.yenile[0] && derin < 3) {
        static char hedef[1200];
        if (url_birlestir(&simdiki, sayfa.yenile, hedef, sizeof hedef) == 0) return git(hedef, 0, derin + 1);
    }
    return 0;
}

static int git(const char *url, int gecmise, int derin) {
    int r = yukle(url, gecmise, derin);
    ciz();
    return r;
}

/* adres çubuğundan: arama mı, adres mi */
static void adres_git(void) {
    static char hedef[1200];
    const char *a = adres;
    while (*a == ' ') a++;
    int nokta = 0, bosluk = 0, sema = 0;
    for (const char *p = a; *p; p++) {
        if (*p == '.') nokta = 1;
        if (*p == ' ') bosluk = 1;
        if (*p == ':' && p[1] == '/' && p[2] == '/') sema = 1;
    }
    if (!*a) return;
    int about = a[0] == 'a' && a[1] == 'b' && a[2] == 'o' && a[3] == 'u' && a[4] == 't' && a[5] == ':';
    if (!sema && !about && (bosluk || !nokta)) {
        hedef[0] = 0;
        dize_ekle(hedef, sizeof hedef, ARAMA);
        char t[4];
        for (const unsigned char *p = (const unsigned char *)a; *p; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                *p == '-' || *p == '_' || *p == '.') { t[0] = (char)*p; t[1] = 0; }
            else if (*p == ' ') { t[0] = '+'; t[1] = 0; }
            else { t[0] = '%'; t[1] = "0123456789ABCDEF"[*p >> 4]; t[2] = "0123456789ABCDEF"[*p & 15]; t[3] = 0; }
            dize_ekle(hedef, sizeof hedef, t);
        }
    } else if (!sema && !about) {
        hedef[0] = 0;
        dize_ekle(hedef, sizeof hedef, "http://");
        dize_ekle(hedef, sizeof hedef, a);
    } else kopya(hedef, sizeof hedef, a);
    git(hedef, 1, 0);
}

/* bağlantıyı (ya da form düğmesini) aç */
static void form_gonder(int form, int dugme);
static void oge_ac(int o) {
    if (o < 0 || o >= sayfa.oge_adet) return;
    h_oge_t *g = &sayfa.ogeler[o];
    if (g->tur == OGE_BAG) {
        static char hedef[2400];
        url_t taban = simdiki;
        if (sayfa.taban[0]) url_coz(sayfa.taban, &taban);
        if (url_birlestir(&taban, h_metin(&sayfa, g->ad), hedef, sizeof hedef) != 0) {
            durum_yaz("Bu bağlantı türü açılamaz: ", h_metin(&sayfa, g->ad), R_HATA);
            ciz();
            return;
        }
        git(hedef, 1, 0);
    } else if (g->tur == OGE_METIN_ALANI) {
        alan_modu = o;
        const char *dg = alan_degeri(o + 1);
        kopya(alan_metin, sizeof alan_metin, dg ? dg : h_metin(&sayfa, g->deger));
        alan_imlec = 0; while (alan_metin[alan_imlec]) alan_imlec++;
        ciz();
    } else if (g->tur == OGE_GONDER) {
        form_gonder(g->form, o);
    } else if (g->tur == OGE_ISARET) {
        g->secili = !g->secili;
        isaret_ciz(o);
        ciz();
    } else if (g->tur == OGE_SECENEK) {
        /* radyo: bu seçilir, aynı form ve addaki ötekiler bırakılır */
        const char *ad = h_metin(&sayfa, g->ad);
        for (int k = 0; k < sayfa.oge_adet; k++) {
            h_oge_t *q = &sayfa.ogeler[k];
            if (k != o && q->tur == OGE_SECENEK && q->form == g->form && q->secili && esit_dize(h_metin(&sayfa, q->ad), ad)) {
                q->secili = 0; isaret_ciz(k);
            }
        }
        g->secili = 1;
        isaret_ciz(o);
        ciz();
    }
}

static void kodla_ekle(char *h, int boy, const char *s) {
    char t[4];
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') { t[0] = (char)*p; t[1] = 0; }
        else if (*p == ' ') { t[0] = '+'; t[1] = 0; }
        else { t[0] = '%'; t[1] = "0123456789ABCDEF"[*p >> 4]; t[2] = "0123456789ABCDEF"[*p & 15]; t[3] = 0; }
        dize_ekle(h, boy, t);
    }
}

static void form_gonder(int form, int dugme) {
    if (form < 0 || form >= sayfa.form_adet) return;
    static char sorgu[2048];
    sorgu[0] = 0;
    int ilk = 1;
#define EKLE(ad, deger) do { if (!ilk) dize_ekle(sorgu, sizeof sorgu, "&"); ilk = 0; \
        kodla_ekle(sorgu, sizeof sorgu, ad); dize_ekle(sorgu, sizeof sorgu, "="); \
        kodla_ekle(sorgu, sizeof sorgu, deger); } while (0)
    for (int i = 0; i < sayfa.gizli_adet; i++)
        if ((int)sayfa.gizli[i][0] == form && h_metin(&sayfa, sayfa.gizli[i][1])[0])
            EKLE(h_metin(&sayfa, sayfa.gizli[i][1]), h_metin(&sayfa, sayfa.gizli[i][2]));
    for (int o = 0; o < sayfa.oge_adet; o++) {
        h_oge_t *g = &sayfa.ogeler[o];
        if (g->form != form) continue;
        const char *ad = h_metin(&sayfa, g->ad);
        if (!ad[0]) continue;
        if (g->tur == OGE_METIN_ALANI) {
            const char *dg = alan_degeri(o + 1);
            EKLE(ad, dg ? dg : h_metin(&sayfa, g->deger));
        } else if ((g->tur == OGE_ISARET || g->tur == OGE_SECENEK) && g->secili) EKLE(ad, h_metin(&sayfa, g->deger));
        else if (g->tur == OGE_GONDER && o == dugme) EKLE(ad, h_metin(&sayfa, g->deger));
    }
#undef EKLE
    static char hedef[2400], eylem[2400];
    url_t taban = simdiki;
    if (sayfa.taban[0]) url_coz(sayfa.taban, &taban);
    const char *act = h_metin(&sayfa, sayfa.formlar[form].eylem);
    if (!act[0]) url_yaz(&taban, eylem, sizeof eylem);
    else if (url_birlestir(&taban, act, eylem, sizeof eylem) != 0) return;
    /* eylemin kendi sorgusu korunur, alanlar '&' ile eklenir; # atılır */
    int soru = 0;
    for (int i = 0; eylem[i]; i++) {
        if (eylem[i] == '#') { eylem[i] = 0; break; }
        if (eylem[i] == '?') soru = i + 1;
    }
    kopya(hedef, sizeof hedef, eylem);
    if (!soru) dize_ekle(hedef, sizeof hedef, "?");
    else if (eylem[soru] && sorgu[0]) dize_ekle(hedef, sizeof hedef, "&");
    dize_ekle(hedef, sizeof hedef, sorgu);
    git(hedef, 1, 0);
    if (sayfa.formlar[form].post)
        dize_ekle(durum, sizeof durum, "  [form POST istiyordu; GET ile gönderildi]");
    sadece_cubuklar();
}

/* ------------------------------------------------------------------ */
/* olaylar                                                             */
/* ------------------------------------------------------------------ */
static void kaydir(int d) {
    int azami = sayfa.satir_adet - GORUNUR_SATIR;
    if (azami < 0) azami = 0;
    int eski = ust_satir;
    ust_satir += d;
    if (ust_satir > azami) ust_satir = azami;
    if (ust_satir < 0) ust_satir = 0;
    if (ust_satir != eski) ciz();
}

/* bir öğenin ilk görüldüğü satır */
static int oge_satiri(int o) {
    for (int r = 0; r < sayfa.satir_adet; r++)
        for (uint32_u j = sayfa.satir[r]; j < sayfa.satir[r + 1]; j++)
            if (sayfa.oge[j] == o + 1) return r;
    return -1;
}

static void sec(int yon) {
    if (sayfa.oge_adet == 0) return;
    /* görünen alandaki ilk/son öğeden başla */
    int o = secili;
    if (o < 0 || oge_satiri(o) < ust_satir || oge_satiri(o) >= ust_satir + GORUNUR_SATIR) {
        o = -1;
        for (int r = ust_satir; r < ust_satir + GORUNUR_SATIR && r < sayfa.satir_adet && o < 0; r++)
            for (uint32_u j = sayfa.satir[r]; j < sayfa.satir[r + 1]; j++)
                if (sayfa.oge[j]) { o = sayfa.oge[j] - 1; break; }
        if (o < 0) return;
        if (secili < 0 || yon > 0) { secili = o; goto goster; }
    }
    o += yon;
    if (o < 0 || o >= sayfa.oge_adet) return;
    if (oge_satiri(o) < 0) { secili = o; sec(yon); return; }
    secili = o;
goster:;
    int r = oge_satiri(secili);
    if (r >= 0 && r < ust_satir) ust_satir = r;
    if (r >= ust_satir + GORUNUR_SATIR) ust_satir = r - GORUNUR_SATIR + 1;
    const h_oge_t *g = &sayfa.ogeler[secili];
    if (g->tur == OGE_BAG) durum_yaz("→ ", h_metin(&sayfa, g->ad), R_CUBUK_YAZI);
    else durum_yaz("Form alanı: Enter ile düzenle", 0, R_CUBUK_YAZI);
    ciz();
}

static void geri(void) {
    if (gecmis_i <= 0) return;
    gecmis_i--;
    int k = gecmis_kaydir[gecmis_i];
    yukle(gecmis[gecmis_i], 0, 0);
    ust_satir = k;
    kaydir(0);
    ciz();
}

/* satır içi düzenleme (adres ya da alan): 1 = Enter, -1 = Esc, 0 sürüyor */
static int duzenle(char *m, int boy, int *imlec, uint32_u tus) {
    int n = uzun(m);
    /* imleç kod noktası cinsinden; bayt konumunu bul */
    int bayt = 0, k = 0;
    while (m[bayt] && k < *imlec) { bayt++; while ((m[bayt] & 0xC0) == 0x80) bayt++; k++; }
    if (tus == 13) return 1;
    if (tus == 27) return -1;
    if (tus == 8) {
        if (*imlec == 0) return 0;
        int o = bayt - 1; while (o > 0 && (m[o] & 0xC0) == 0x80) o--;
        for (int i = o; i <= n - (bayt - o); i++) m[i] = m[i + (bayt - o)];
        (*imlec)--;
        return 0;
    }
    if (tus == 0xE000 + 8) {                       /* Delete */
        if (!m[bayt]) return 0;
        int s = bayt + 1; while ((m[s] & 0xC0) == 0x80) s++;
        for (int i = bayt; i <= n - (s - bayt); i++) m[i] = m[i + (s - bayt)];
        return 0;
    }
    if (tus == 0xE000 + 2) { if (*imlec > 0) (*imlec)--; return 0; }
    if (tus == 0xE000 + 3) { if (m[bayt]) (*imlec)++; return 0; }
    if (tus == 0xE000 + 6) { *imlec = 0; return 0; }
    if (tus == 0xE000 + 7) { int c = 0; for (const char *p = m; *p; p++) if ((*p & 0xC0) != 0x80) c++; *imlec = c; return 0; }
    if (tus < 0x20 || (tus >= 0xE000 && tus < 0xF000)) return 0;
    char u[4]; int ub = utf8_yaz(tus, u);
    if (n + ub >= boy - 1) return 0;
    for (int i = n; i >= bayt; i--) m[i + ub] = m[i];
    for (int i = 0; i < ub; i++) m[bayt + i] = u[i];
    (*imlec)++;
    return 0;
}

static void adres_duzenle_basla(void) {
    kopya(adres_yedek, sizeof adres_yedek, adres);
    adres_modu = 1;
    adres_secili = 1;
    adres_imlec = 0; for (const char *p = adres; *p; p++) if ((*p & 0xC0) != 0x80) adres_imlec++;
    sadece_cubuklar();
}

static void tus_isle(uint32_u t) {
    if (alan_modu >= 0) {
        int r = duzenle(alan_metin, sizeof alan_metin, &alan_imlec, t);
        if (r != 0) {
            int o = alan_modu;
            alan_modu = -1;
            if (r == 1) {
                alan_sakla(o);
                form_gonder(sayfa.ogeler[o].form, -1);
                return;
            }
        }
        ciz();
        return;
    }
    if (adres_modu) {
        if (adres_secili) {
            adres_secili = 0;
            if (t == 8 || t == 0xE000 + 8 || (t >= 0x20 && !(t >= 0xE000 && t < 0xF000))) {
                adres[0] = 0; adres_imlec = 0;           /* seçili adres silinir / değiştirilir */
                if (t == 8 || t == 0xE000 + 8) { sadece_cubuklar(); return; }
            }
        }
        int r = duzenle(adres, sizeof adres, &adres_imlec, t);
        if (r == 1) { adres_modu = 0; adres_git(); return; }
        if (r == -1) { adres_modu = 0; kopya(adres, sizeof adres, adres_yedek); }
        sadece_cubuklar();
        return;
    }
    switch (t) {
    case 0xE000 + 0x0A: case 0xE000 + 0x0C: adres_duzenle_basla(); break;   /* F6, Ctrl+L */
    case 0xE000 + 0x09: git(adres, 0, 0); break;                            /* F5 */
    case 0xE000 + 0: kaydir(-1); break;
    case 0xE000 + 1: kaydir(1); break;
    case 0xE000 + 4: kaydir(-(GORUNUR_SATIR - 2)); break;
    case 0xE000 + 5: case ' ': kaydir(GORUNUR_SATIR - 2); break;
    case 0xE000 + 6: kaydir(-1000000); break;
    case 0xE000 + 7: kaydir(1000000); break;
    case 0xE000 + 2: sec(-1); break;
    case 0xE000 + 3: sec(1); break;
    case 13: if (secili >= 0) oge_ac(secili); break;
    case 8: geri(); break;
    default: break;
    }
}

static void tik_isle(int x, int y) {
    if (alan_modu >= 0 && y < YUK - ALT) { alan_sakla(alan_modu); alan_modu = -1; ciz(); }   /* başka yere tıklamak yazılanı korur */
    if (y < UST) {
        if (x >= 6 && x < 64) { if (adres_modu) adres_modu = 0; geri(); return; }
        if (x >= 70 && x < 136) { adres_modu = 0; git(adres, 0, 0); return; }
        if (x >= 144) { if (!adres_modu) adres_duzenle_basla(); return; }
        return;
    }
    if (adres_modu) { adres_modu = 0; kopya(adres, sizeof adres, adres_yedek); sadece_cubuklar(); }
    if (y >= YUK - ALT) return;
    if (x >= GEN - KAYDIRMA_EN - 6) {                /* kaydırma çubuğu */
        int kh = YUK - UST - ALT - 4;
        int orta = UST + 2 + kh / 2;
        kaydir(y < orta ? -(GORUNUR_SATIR - 2) : (GORUNUR_SATIR - 2));
        return;
    }
    int r = ust_satir + (y - UST - 4 + 1) / SATIR_YUK;
    int c = (x - SOL) / 8;
    if (r < 0 || r >= sayfa.satir_adet || c < 0) return;
    uint32_u j = sayfa.satir[r] + (uint32_u)c;
    if (j >= sayfa.satir[r + 1]) return;
    int o = sayfa.oge[j];
    if (o) { secili = o - 1; oge_ac(o - 1); }
}

int main(void) {
    pen = pencere_ac("TULPAR — Ağ Gezgini", 32, 64, GEN, YUK);
    if (pen < 0) return 1;
    yerel_goster(HAKKINDA, "about:tulpar");
    gecmise_ekle("about:tulpar");
    durum_yaz("Adres yazmak için adres çubuğuna tıkla ya da F6", 0, R_CUBUK_YAZI);
    ciz();
    for (;;) {
        olay_u o;
        int oldu = 0;
        while (pencere_olay(pen, &o) == 1) {
            oldu = 1;
            if (o.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (o.tur == OLAY_TUS) tus_isle(o.tus);
            else if (o.tur == OLAY_TIK) tik_isle(o.x, o.y);
        }
        if (!oldu) sleep_ticks(2);
    }
}
