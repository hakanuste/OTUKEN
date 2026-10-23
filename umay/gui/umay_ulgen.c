/* ÜLGEN arayüzü — üç bölge:
 *   sol/üst : konuşma akışı (kaydırmalı)
 *   alt     : giriş satırı (imleçli)
 *   sağ     : ajan meclisi + durum
 * Çizim yalnız up_* çağrılarıyla yapılır; ÜLGEN API'si port'ta bağlanır. */
#include "umay_gui.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"

#define RENK_ZEMIN   0x0E1512u
#define RENK_PANEL   0x16211Cu
#define RENK_CIZGI   0x2A3A32u
#define RENK_YAZI    0xD8E0DAu
#define RENK_UMAY    0xE0B24Au   /* UMAY'ın sözü — altın */
#define RENK_KISI    0x7FC8A9u   /* kullanıcının sözü    */
#define RENK_SOLGUN  0x6C7B73u
#define RENK_UYARI   0xD2603Cu

#define SATIR_YUK 16
#define KENAR     12
#define SAG_EN    220

static up_pencere_t *g_p;
static int  g_en, g_boy;
static char g_akis[GUI_SATIR_MAX][GUI_SUTUN_MAX];
static uint8_t g_kim[GUI_SATIR_MAX];       /* 0=umay 1=kişi 2=dizge */
static int  g_akis_sayi;
static char g_giris[GUI_SUTUN_MAX];
static uint32_t g_giris_o;
static char g_durum[GUI_SUTUN_MAX];

int gui_acik_mi(void) { return g_p != 0; }

int gui_ac(int en, int boy) {
    g_p = up_pencere_yarat("UMAY — Ötüken", en, boy);
    if (!g_p) { gun_yaz("ÜLGEN penceresi açılamadı; yalnız yazı kipinde koşarım."); return UM_DESTEKSIZ; }
    g_en = en; g_boy = boy;
    um_kopya(g_durum, sizeof g_durum, "hazır");
    gun_yaz("ÜLGEN penceresi açıldı: %dx%d", en, boy);
    return UM_OLDU;
}

static int g_kapat_istendi = 0;

int gui_kapat_istendi(void) { return g_kapat_istendi; }

void gui_kapat(void) { if (g_p) { up_pencere_yok(g_p); g_p = 0; } }

/* Akışa tek satır ekler (dolunca en eskisi kayar). */
static void satir_it(uint8_t kim, const char *satir) {
    if (g_akis_sayi >= GUI_SATIR_MAX) {
        for (int i = 1; i < GUI_SATIR_MAX; i++) {
            um_kopya(g_akis[i-1], GUI_SUTUN_MAX, g_akis[i]);
            g_kim[i-1] = g_kim[i];
        }
        g_akis_sayi = GUI_SATIR_MAX - 1;
    }
    um_kopya(g_akis[g_akis_sayi], GUI_SUTUN_MAX, satir);
    g_kim[g_akis_sayi] = kim;
    g_akis_sayi++;
}

/* Uzun metni konuşma sütununun GENİŞLİĞİNE göre sözcük sınırından böler.
 * Genişlik HARF (UTF-8 kod noktası) sayısıyla ölçülür: çekirdek yazısı
 * her harfi 8 piksellik tek hücreye çizer. Eskiden bölme yalnız bayt
 * sınırındaydı ve satırlar sağdaki ajan sütununun altına taşıyordu. */
static void akisa_koy(uint8_t kim, const char *metin) {
    int sutun = g_en > 0 ? (g_en - SAG_EN - KENAR * 2) / 8 : 40;
    if (sutun < 10) sutun = 10;
    const char *p = metin;
    for (;;) {
        char satir[GUI_SUTUN_MAX];
        uint32_t o = 0, son_bosluk_o = 0;
        const char *son_bosluk_p = 0;
        int harf = 0;
        while (*p && *p != '\n') {
            /* bir harfin bayt uzunluğu */
            uint8_t b = (uint8_t)*p;
            uint32_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
            if (harf >= sutun || o + n + 1 >= sizeof satir) break;
            if (*p == ' ') { son_bosluk_o = o; son_bosluk_p = p; }
            for (uint32_t k = 0; k < n && p[k]; k++) satir[o++] = p[k];
            p += n; harf++;
        }
        if (*p && *p != '\n' && son_bosluk_p) {   /* sözcük ortasında kesme */
            o = son_bosluk_o;
            p = son_bosluk_p + 1;
        }
        satir[o] = 0;
        satir_it(kim, satir);
        if (*p == '\n') { p++; if (!*p) break; continue; }
        if (!*p) break;
    }
}

void gui_satir_ekle(const char *kim, const char *metin) {
    uint8_t k = 2;
    if (um_esit(kim, "UMAY")) k = 0;
    else if (um_esit(kim, "SEN")) k = 1;
    akisa_koy(k, metin);
}

void gui_durum(const char *metin) { um_kopya(g_durum, sizeof g_durum, metin); }

static const char *durum_adi(alp_durum_t d) {
    switch (d) {
    case ALP_BOS:     return "boş";
    case ALP_KOSUYOR: return "koşuyor";
    case ALP_BEKLIYOR:return "bekliyor";
    case ALP_BITTI:   return "bitti";
    default:          return "yanlış";
    }
}

void gui_ciz(const otag_t *o) {
    if (!g_p) return;
    int govde_en = g_en - SAG_EN - KENAR * 2;

    up_dikdortgen(g_p, 0, 0, g_en, g_boy, RENK_ZEMIN);

    /* başlık */
    up_dikdortgen(g_p, 0, 0, g_en, 28, RENK_PANEL);
    up_yazi(g_p, KENAR, 6, "UMAY — Dede Korkut dili ile", RENK_UMAY);
    up_yazi(g_p, g_en - SAG_EN + 8, 6, UMAY_SURUM, RENK_SOLGUN);
    up_dikdortgen(g_p, 0, 28, g_en, 1, RENK_CIZGI);

    /* konuşma akışı */
    int y = 40;
    int gorunur = (g_boy - 40 - 44) / SATIR_YUK;
    int bas = g_akis_sayi > gorunur ? g_akis_sayi - gorunur : 0;
    for (int i = bas; i < g_akis_sayi; i++) {
        uint32_t renk = g_kim[i] == 0 ? RENK_UMAY : (g_kim[i] == 1 ? RENK_KISI : RENK_SOLGUN);
        up_yazi(g_p, KENAR, y, g_akis[i], renk);
        y += SATIR_YUK;
    }

    /* giriş satırı */
    int gy = g_boy - 36;
    up_dikdortgen(g_p, KENAR, gy, govde_en, 26, RENK_PANEL);
    up_yazi(g_p, KENAR + 6, gy + 5, ">", RENK_UMAY);
    up_yazi(g_p, KENAR + 22, gy + 5, g_giris, RENK_YAZI);
    up_dikdortgen(g_p, KENAR + 22 + up_metin_en(g_giris), gy + 6, 8, 15, RENK_SOLGUN);

    /* sağ yan: ajan meclisi */
    int sx = g_en - SAG_EN;
    up_dikdortgen(g_p, sx, 29, SAG_EN, g_boy - 29, RENK_PANEL);
    up_dikdortgen(g_p, sx, 29, 1, g_boy - 29, RENK_CIZGI);
    up_yazi(g_p, sx + 12, 44, "OTAĞ — ajan meclisi", RENK_SOLGUN);
    int ay = 66;
    if (o) {
        for (int i = 0; i < o->alp_sayisi; i++) {
            const alp_t *a = o->alplar[i];
            uint32_t renk = a->durum == ALP_KOSUYOR ? RENK_UMAY :
                            (a->durum == ALP_YANLIS ? RENK_UYARI : RENK_YAZI);
            if (ay > g_boy - 64) break;             /* durum çubuğuna taşma */
            up_dikdortgen(g_p, sx + 12, ay + 5, 6, 6, renk);
            up_yazi(g_p, sx + 26, ay, a->ad, renk);
            const char *da = durum_adi(a->durum);
            up_yazi(g_p, sx + SAG_EN - 12 - up_metin_en(da), ay, da, RENK_SOLGUN);
            ay += 18;
        }
        char s[80];
        um_bicim(s, sizeof s, "kuyruk: %d   biten: %u", otag_bekleyen_sayi(o), o->bitmis);
        if (ay < g_boy - 48) up_yazi(g_p, sx + 12, ay + 4, s, RENK_SOLGUN);
        ay += 26;

        /* görev görünümü: tür, durum, ilerleme çubuğu */
        if (ay < g_boy - 48) up_yazi(g_p, sx + 12, ay, "GÖREVLER", RENK_SOLGUN);
        ay += 18;
        for (int i = 0; i < UMAY_GOREV_MAX && ay < g_boy - 56; i++) {
            const gorev_t *gv = &o->havuz[i];
            if (!gv->dolu) continue;
            char b[80];
            um_bicim(b, sizeof b, "#%u %s", gv->kimlik, gorev_tur_adi(gv->tur));
            uint32_t renk = gv->durum == GD_KOSUYOR ? RENK_UMAY :
                            (gv->durum == GD_ONAY_BEKLER ? RENK_UYARI : RENK_YAZI);
            up_yazi(g_p, sx + 12, ay, b, renk);
            up_yazi(g_p, sx + 110, ay, gorev_durum_adi(gv->durum), RENK_SOLGUN);
            /* ilerleme çubuğu */
            int cen = SAG_EN - 32;
            up_dikdortgen(g_p, sx + 12, ay + 14, cen, 3, RENK_CIZGI);
            up_dikdortgen(g_p, sx + 12, ay + 14, cen * gv->ilerleme / 100, 3, renk);
            ay += 24;
        }
    }

    /* durum çubuğu */
    up_yazi(g_p, sx + 12, g_boy - 24, g_durum, RENK_SOLGUN);
    up_sun(g_p);
}

int gui_giris_yokla(umay_ileti_t *i) {
    if (!g_p) return 0;
    up_olay_t o;
    while (up_olay_al(g_p, &o)) {
        if (o.tur == UP_OLAY_KAPAT) {
            /* Pencere kapatma düğmesi: veda iletisi üretilir; OZAN bunu
             * NIYET_VEDA sayar ve ana döngü kapanır. Eskiden bu olay
             * hiç gelmiyordu, pencere kapatılamıyordu. */
            g_kapat_istendi = 1;
            um_kopya(i->govde, sizeof i->govde, "hoşça kal");
            i->tur = KANAL_ULGEN; i->damga = up_ms();
            i->uzunluk = um_uzun(i->govde);
            return 1;
        }
        if (o.tur != UP_OLAY_TUS) continue;
        if (o.tus == '\n' || o.tus == '\r') {
            if (g_giris_o == 0) continue;
            g_giris[g_giris_o] = 0;
            i->tur = KANAL_ULGEN; i->damga = up_ms(); i->kaynak = 1;
            i->uzunluk = um_kopya(i->govde, sizeof i->govde, g_giris);
            g_giris_o = 0; g_giris[0] = 0;
            return 1;
        }
        if (o.tus == 8 || o.tus == 127) {                 /* geri sil */
            while (g_giris_o > 0) {
                g_giris_o--;
                if (((uint8_t)g_giris[g_giris_o] & 0xC0) != 0x80) break;  /* UTF-8 başı */
            }
            g_giris[g_giris_o] = 0;
            continue;
        }
        char yaz[5];
        uint32_t yb = um_utf8_yaz(o.tus, yaz);
        if (g_giris_o + yb + 1 < sizeof g_giris) {
            um_bellek_kopya(g_giris + g_giris_o, yaz, yb);
            g_giris_o += yb;
            g_giris[g_giris_o] = 0;
        }
    }
    return 0;
}
