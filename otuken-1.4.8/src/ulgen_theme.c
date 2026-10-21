#include "ulgen_theme.h"
#include "tema.h"
#include "tamga.h"
#include "fb.h"
#include "duvar.h"
#include "simge.h"
#include "tamga_gorsel.h"
#include "mouse.h"
#include "orbitron.h"
#include "timer.h"

/* gui.c ile aynı olmalı: görev çubuğu yüksekliği */
#define TASKBAR_YUKSEK 86

#define TAMGA_N 15
#define RIHTIM_KARO 42

/* Rıhtım kimliği -> tamga kimliği (sıra tamga_id_t ile aynıdır). */
/* Rol yazıları hücre genişliğine (yaklaşık 75 piksel) göre kısaltıldı;
 * tam karşılıkları üzerine gelince günlükte gösterilir. */
/* Roller BÜYÜK HARF: Orbitron bir başlık yazı tipidir; 10 pikselde
 * küçük harfleri okunmuyordu ("Yapay Us" ekranda "Yaoau Us" çıkıyordu). */
static const char *ROLLER[TAMGA_N] = {
    "SİSTEM", "YAPAY US", "BİLGELİK", "GRAFİK", "TARAYICI",
    "GÜVENLİK", "YETKİ", "KURTARMA", "KURULUM",
    "DOSYA", "AĞ", "PAKET", "POLİTİKA",
    "KİMLİK", "ARAÇLAR"
};

static int __attribute__((unused)) uzunluk(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* --- yardımcı çizimler (tamsayı; FPU kapalı) --- */
static void __attribute__((unused)) cizgi(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 - x0, sx = dx >= 0 ? 1 : -1; if (dx < 0) dx = -dx;
    int dy = y1 - y0, sy = dy >= 0 ? 1 : -1; if (dy < 0) dy = -dy;
    int e = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        fb_put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = e;
        if (e2 > -dx) { e -= dy; x0 += sx; }
        if (e2 <  dy) { e += dx; y0 += sy; }
    }
}

static void __attribute__((unused)) dag(int taban, int zirve, int x0, int x1, uint32_t c) {
    int xm = (x0 + x1) / 2;
    cizgi(x0, taban, xm, zirve, c);
    cizgi(xm, zirve, x1, taban, c);
}

/* Dolu daire: ay için. Yarıçap karesi karşılaştırmasıyla. */
static void __attribute__((unused)) daire_dolu(int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) fb_put_pixel(cx + x, cy + y, c);
}

/* =====================================================================
 * MASAÜSTÜ ÖNBELLEĞİ
 * Duvar kâğıdı + masaüstü simgeleri + üst bar her yeniden çizimde
 * baştan besteleniyordu: 786 bin piksel ölçekleme, üstüne alfa
 * harmanlı simgeler. Yeniden çizim sık olduğu için ekranda YIRTILMA
 * (yarım çizilmiş kare) görülüyordu. Durağan katman bir kez besteleyip
 * saklanır; her karede yalnız kopyalanır ve üstüne dinamik rıhtım
 * çizilir. .bss'te 3 MB tutar, dosya boyutunu artırmaz. */
#define MASA_EN  1280
#define MASA_BOY 1024
/* Tam ekran önbelleği KALDIRILDI: 1280x1024x4 = 5 MB .bss, çekirdeğin
 * haritalı bölgesini aşıp sayfa hatasına yol açıyordu. Hızlı satır
 * kopyası (fb_satir_yaz) geldikten sonra zaten gereksiz. */

static void masa_beste(int sw, int sh);

void ulgen_theme_init(void) { }

void ulgen_theme_tazele(void) { }

/* Uygulamalar ızgarası: rıhtımda yer bulamayan tamgalar buradadır.
 * Düğmeye basılınca açılır, tekrar basılınca ya da bir tamgaya
 * tıklanınca kapanır. */
static int g_izgara_acik = 0;

void ulgen_theme_izgara_degis(void) { g_izgara_acik = !g_izgara_acik; }
int  ulgen_theme_izgara_acik(void)  { return g_izgara_acik; }

/* Durağan masaüstü katmanını çerçeve arabelleğine çizip önbelleğe alır. */
static void masa_beste(int sw, int sh) {
    const tema_t *tm = tema_simdiki(); (void)tm;
    duvar_ciz(0, 0, sw, sh);

    /* --- üst bar sağı: çalışma süresi göstergesi ---
     * Gerçek zaman saati (RTC) sürücüsü yok; zamanlayıcı tiklerinden
     * hesaplanan ÇALIŞMA SÜRESİ gösterilir. Bunu "saat" diye sunmak
     * yanıltıcı olurdu. */
    {
        uint64_t tik = timer_get_ticks();
        uint32_t sn = (uint32_t)(tik / 100);
        char s[32];
        int i = 0;
        s[i++] = 'A'; s[i++] = 'C'; s[i++] = 'I'; s[i++] = 'K'; s[i++] = ' ';
        uint32_t dk = sn / 60, ss = sn % 60;
        if (dk >= 10) s[i++] = (char)('0' + (dk / 10) % 10);
        s[i++] = (char)('0' + dk % 10);
        s[i++] = ':';
        s[i++] = (char)('0' + ss / 10);
        s[i++] = (char)('0' + ss % 10);
        s[i] = 0;
        orb_yaz(sw - orb_en(s, 1) - 18, 12, s, tm->gorev_yazi, 0, 1);
    }

    /* --- masaüstü simgeleri (sol sütun) --- */
    {
        static const struct { simge_id_t id; const char *ad; } MASA[] = {
            { SIMGE_DISK,   "ÖTÜKENOS" },
            { SIMGE_KLASOR, "Belgeler" },
            { SIMGE_KLASOR, "Projeler" },
            { SIMGE_KLASOR, "Arşiv"    },
            { SIMGE_COP,    "Çöp Kutusu" }
        };
        int n = (int)(sizeof(MASA) / sizeof(MASA[0]));
        int sx = 28, sy = ULGEN_UST_BAR + 26;
        for (int i = 0; i < n; i++) {
            int y = sy + i * 96;
            simge_ciz(sx, y, MASA[i].id, 64);
            orb_yaz_ortali(sx + 32, y + 68, MASA[i].ad, tm->beyaz_yerine_krem, 1, 1);
        }
    }

    /* --- üst bar --- */
    fb_fill_rect(0, 0, sw, ULGEN_UST_BAR, tm->gorev_cubugu);
    /* Sol üstte ÖTÜKEN tamgası — "ÖTÜKENOS" yazısının yerine.
     * Bu yüzden rıhtımda 14 tamga kalır (UMAY..ORKUN). */
    tamga_gorsel_ciz(14, 3, 0, 48, 0);
    orb_yaz(74, 11, "Dosya    Düzen    Görünüm    Uygulamalar    Sistem    Yardım",
            tm->gorev_yazi, 0, 0);


}

void ulgen_theme_draw(void) {
    const tema_t *tm = tema_simdiki(); (void)tm;
    int sw = fb_width(), sh = fb_height();

    masa_beste(sw, sh);

    /* --- Uygulamalar ızgarası (açıksa) --- */
    if (g_izgara_acik) {
        int gen = 5 * 150 + 60, yuk = 3 * 170 + 70;
        int gx = (sw - gen) / 2, gy = (sh - yuk) / 2;
        for (int j = 0; j < yuk; j++) {
            static uint32_t sat[2048];
            int en = gen < 2048 ? gen : 2048;
            fb_satir_oku(gx, gy + j, sat, en);
            for (int i = 0; i < en; i++) {
                uint32_t z = sat[i];
                int zr=(int)((z>>16)&0xFF), zg=(int)((z>>8)&0xFF), zb=(int)(z&0xFF);
                sat[i] = ((uint32_t)(zr*22/100 + 10) << 16)
                       | ((uint32_t)(zg*22/100 + 16) << 8)
                       |  (uint32_t)(zb*22/100 + 24);
            }
            fb_satir_yaz(gx, gy + j, sat, en);
        }
        orb_yaz_ortali(sw / 2, gy + 16, "UYGULAMALAR", 0xFFFFFFu, 1, 0);
        for (int i = 0; i < TAMGA_N; i++) {
            int r = i / 5, c2 = i % 5;
            int x = gx + 30 + c2 * 150, y = gy + 46 + r * 170;
            tamga_gorsel_ciz(x, y, i, 128, 0);
        }
    }

    /* --- rıhtım: İKİ SIRA, 180 piksel simge ---
     * Üst sıra 8, alt sıra 7 tamga. Simge boyu ekran genişliğinden
     * türetilir; 180 tavandır, sığmazsa küçülür. */
    const int ara = 10, ic_pay = 16;
    /* RIHTIM SEÇİMİ: günlük kullanımda en sık gerekenler tek sırada.
     * ÖTÜKEN üst barda; kurtarma (ASENA), yeniden kurulum (ERGENEKON),
     * yetki (KUT), politika (TÖRE), güvenlik (TİMUR), ağ (ULAK) ve
     * DEDE KORKUT nadir kullanılır, rıhtımda yer tutmaz. */
    static const int RIHTIM[] = {
        1,   /* UMAY   — yapay us          */
        9,   /* BİTİG  — dosya sistemi     */
        14,  /* ORKUN  — geliştirici       */
        3,   /* ÜLGEN  — grafik ortamı     */
        4,   /* TULPAR — web tarayıcı      */
        11,  /* KERVAN — paket yönetimi    */
        13   /* TAMGA  — dijital kimlik    */
    };
    const int rihtim_adet = (int)(sizeof(RIHTIM) / sizeof(RIHTIM[0]));
    const int ust_adet = rihtim_adet, alt_adet = 0;
    int azami = sw - 24;
    int simge = (azami - 2 * ic_pay - (ust_adet - 1) * ara) / ust_adet;
    if (simge > 180) simge = 180;   /* 200x185 hücreye sığan logo */
    if (simge < 48)  simge = 48;
    int cubuk_en = ust_adet * simge + (ust_adet - 1) * ara + 2 * ic_pay;
    int rx0 = (sw - cubuk_en) / 2, rx1 = rx0 + cubuk_en;
    int cubuk_boy = simge + 2 * 12;
    int dy = sh - 86 - cubuk_boy;   /* görev çubuğu 86 piksel */

    for (int j = 0; j < cubuk_boy; j++) {
        static uint32_t satir[2048];
        int en = rx1 - rx0;
        if (en > 2048) en = 2048;
        /* Satırı toplu oku, yerinde karıştır, toplu yaz: piksel başına
         * iki fonksiyon çağrısı çizimi yetişemez hale getiriyordu. */
        fb_satir_oku(rx0, dy + j, satir, en);
        int kat = (j < 2) ? 60 : 16;
        for (int i = 0; i < en; i++) {
            uint32_t z = satir[i];
            int zr=(int)((z>>16)&0xFF), zg=(int)((z>>8)&0xFF), zb=(int)(z&0xFF);
            satir[i] = ((uint32_t)(zr*30/100 + kat) << 16)
                     | ((uint32_t)(zg*30/100 + kat + 4) << 8)
                     |  (uint32_t)(zb*30/100 + kat + 10);
        }
        int r = 18;
        int dyk = (j < r) ? (r - j) : (j >= cubuk_boy - r ? j - (cubuk_boy - r - 1) : 0);
        int bas_i = 0, son_i = en;
        if (dyk) {
            int k = 0;
            while (k < r && (r - k) * (r - k) + dyk * dyk > r * r) k++;
            bas_i = k; son_i = en - k;
        }
        if (son_i > bas_i) fb_satir_yaz(rx0 + bas_i, dy + j, &satir[bas_i], son_i - bas_i);
    }

        int fx, fy, fb_dugme;
    mouse_get_state(&fx, &fy, &fb_dugme);
    (void)fb_dugme;

    int ustunde = -1, u_x = 0, u_y = 0;
    for (int k = 0; k < rihtim_adet; k++) {
        int i = RIHTIM[k];
        int sira = 0;
        int sut  = k;
        int adet = ust_adet; (void)alt_adet;
        int sira_en = adet * simge + (adet - 1) * ara;
        int x = rx0 + (cubuk_en - sira_en) / 2 + sut * (simge + ara);
        int y = dy + 12 + sira * (simge + 12);
        int uzerinde = (fx >= x && fx < x + simge && fy >= y && fy < y + simge);
        if (uzerinde) { ustunde = i; u_x = x; u_y = y; }
        tamga_gorsel_ciz(x, y, i, simge, uzerinde ? 50 : 0);
    }

    if (ustunde >= 0) {
        const char *ad = tamga_al((tamga_id_t)ustunde)->ad;
        int en = orb_en(ad, 0) + 24;
        int x = u_x + simge / 2 - en / 2;
        if (x < 8) x = 8;
        if (x + en > sw - 8) x = sw - 8 - en;
        int y = u_y - 32;
        if (y < 4) y = u_y + simge + 6;
        tamga_yuvarlak_kutu(x, y, en, 28, 8, 0x0A1B24u);
        orb_yaz_ortali(x + en / 2, y + 6, ad, 0xFFFFFFu, 0, 0);
    }
}

int ulgen_theme_izgara_hit(int x, int y) {
    if (!g_izgara_acik) return -1;
    int sw = fb_width(), sh = fb_height();
    int gen = 5 * 150 + 60, yuk = 3 * 170 + 70;
    int gx = (sw - gen) / 2, gy = (sh - yuk) / 2;
    for (int i = 0; i < TAMGA_N; i++) {
        int r = i / 5, c2 = i % 5;
        int ix = gx + 30 + c2 * 150, iy = gy + 46 + r * 170;
        if (x >= ix && x < ix + 128 && y >= iy && y < iy + 128) return i;
    }
    return -2;   /* ızgara içinde ama tamga değil */
}

int ulgen_theme_dock_hit(int x, int y) {
    int sw = fb_width(), sh = fb_height();
    const int ara = 10, ic_pay = 16;
    /* RIHTIM SEÇİMİ: günlük kullanımda en sık gerekenler tek sırada.
     * ÖTÜKEN üst barda; kurtarma (ASENA), yeniden kurulum (ERGENEKON),
     * yetki (KUT), politika (TÖRE), güvenlik (TİMUR), ağ (ULAK) ve
     * DEDE KORKUT nadir kullanılır, rıhtımda yer tutmaz. */
    static const int RIHTIM[] = {
        1,   /* UMAY   — yapay us          */
        9,   /* BİTİG  — dosya sistemi     */
        14,  /* ORKUN  — geliştirici       */
        3,   /* ÜLGEN  — grafik ortamı     */
        4,   /* TULPAR — web tarayıcı      */
        11,  /* KERVAN — paket yönetimi    */
        13   /* TAMGA  — dijital kimlik    */
    };
    const int rihtim_adet = (int)(sizeof(RIHTIM) / sizeof(RIHTIM[0]));
    const int ust_adet = rihtim_adet, alt_adet = 0;
    int azami = sw - 24;
    int simge = (azami - 2 * ic_pay - (ust_adet - 1) * ara) / ust_adet;
    if (simge > 180) simge = 180;   /* 200x185 hücreye sığan logo */
    if (simge < 48)  simge = 48;
    int cubuk_en = ust_adet * simge + (ust_adet - 1) * ara + 2 * ic_pay;
    int rx0 = (sw - cubuk_en) / 2;
    int cubuk_boy = simge + 2 * 12;
    int dy = sh - 86 - cubuk_boy;   /* görev çubuğu 86 piksel */

    for (int i = 0; i < TAMGA_N; i++) {
        int sira = (i < ust_adet) ? 0 : 1;
        int sut  = (i < ust_adet) ? i : i - ust_adet;
        int adet = ust_adet; (void)alt_adet;
        int sira_en = adet * simge + (adet - 1) * ara;
        int gx = rx0 + (cubuk_en - sira_en) / 2 + sut * (simge + ara);
        int gy = dy + 10 + sira * (simge + 10);
        if (x >= gx && x < gx + simge && y >= gy && y < gy + simge) return i;
    }
    return -1;
}

const char *ulgen_theme_name(int id) {
    const tamga_t *t = tamga_al((tamga_id_t)id);
    return t ? t->ad : "";
}

const char *ulgen_theme_role(int id) {
    return (id >= 0 && id < TAMGA_N) ? ROLLER[id] : "";
}
