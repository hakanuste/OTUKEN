/* =====================================================================
 * ASENA — kurtarma (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | KURTARMA. Diske kendisi yazamaz:
 * denetim, onarım ve dosya kurtarma çekirdekte (src/asena.c,
 * src/ergenekon.c) yapılır. Açılışta disk salt okunur denetlenir.
 *   Denetle : FAT16 birimi (FAT kopyaları, zincirler, çapraz bağ, kayıp küme)
 *   Onar    : bulunanları düzeltir (iki kez basılınca; diske yazar)
 *   Dizge   : dizge dosyalarını ERGENEKON imajıyla karşılaştırır; seçileni
 *             imajdan geri yükler
 * ===================================================================== */
#include "uyg.h"

#define GEN 700
#define YUK 480
#define UST 76
#define SAT 20
static int pen, hz;
static int gorunum;               /* 0 dosya sistemi, 1 dizge dosyaları */
static asena_rapor_u rap;
static int rap_var;
static asena_bulgu_u bul[16];
static int bul_n;
static dizge_dosya_u dz[40];
static int dz_n, secili;
static char durum[160];
static uint32_u durum_renk = R_SONUK;
static long onay_zaman;           /* Onar'a ilk basış */

static const char *const TUR[ASENA_TUR_SAYISI] = {
    "FAT kopyaları farklı", "kayıp küme", "çapraz bağlı küme", "kırık zincir",
    "döngülü zincir", "boy zincirden büyük", "zincir boydan uzun", "geçersiz başlangıç kümesi"
};

static void durum_yaz(const char *m, uint32_u r) { durum[0] = 0; dize_ekle(durum, sizeof durum, m); durum_renk = r; }

static void denetle(void) {
    rap_var = syscall(SYS_ASENA_DENETLE, 0, 0, (long)&rap, 0) == 0;
    bul_n = 0;
    /* bulgular BU taramanın kuşağıyla istenir: arada başka tarama olduysa okunmaz */
    while (rap_var && bul_n < 16 && syscall(SYS_ASENA_DENETLE, 1, bul_n, (long)&bul[bul_n], (long)rap.kusak) == 0) bul_n++;
}

static void dizge_oku(void) {
    dz_n = 0;
    while (dz_n < 40 && syscall(SYS_ASENA_DENETLE, 2, dz_n, (long)&dz[dz_n], 0) == 0) dz_n++;
    if (secili >= dz_n) secili = dz_n - 1;
    if (secili < 0) secili = 0;
}

static void dugme(int x, int y, int w, const char *m, int etkin) {
    pencere_dikdortgen(pen, x, y, w, 24, etkin ? 0x2A4652u : 0x1B2A30u);
    pencere_yazi(pen, x + 10, y + 4, m, etkin ? R_YAZI : R_SONUK);
}

static void ciz(void) {
    char s[160], t[16];
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "ASENA — Kurtarma", R_VURGU);
    dugme(14, 38, 90, "Denetle", gorunum == 0);
    dugme(112, 38, 70, "Onar", gorunum == 0);
    dugme(190, 38, 150, "Dizge dosyaları", gorunum == 1);
    if (gorunum == 1) dugme(348, 38, 150, "İmajdan kurtar", 1);
    int y = UST;
    if (gorunum == 0) {
        if (!rap_var) { pencere_yazi(pen, 16, y, "Denetim yapılamadı (disk bağlı değil?)", R_KIRMIZI); }
        else {
            s[0] = 0;
            dize_ekle(s, sizeof s, "Taranan: "); sayi_yaz(t, rap.dosya, 1); dize_ekle(s, sizeof s, t);
            dize_ekle(s, sizeof s, " dosya, "); sayi_yaz(t, rap.dizin, 1); dize_ekle(s, sizeof s, t);
            dize_ekle(s, sizeof s, " dizin · boş küme "); sayi_yaz(t, rap.bos_kume, 1); dize_ekle(s, sizeof s, t);
            dize_ekle(s, sizeof s, " / "); sayi_yaz(t, rap.kume_sayisi, 1); dize_ekle(s, sizeof s, t);
            pencere_yazi(pen, 16, y, s, R_SONUK); y += SAT;
            s[0] = 0;
            if (rap.bulgu == 0) dize_ekle(s, sizeof s, "Dosya sistemi sağlam: sorun yok.");
            else { sayi_yaz(t, rap.bulgu, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " sorun bulundu:"); }
            pencere_yazi(pen, 16, y, s, rap.bulgu ? R_UYARI : R_VURGU); y += SAT + 4;
            for (int i = 0; i < bul_n && y < YUK - 60; i++, y += SAT) {
                s[0] = 0;
                dize_ekle(s, sizeof s, bul[i].tur < ASENA_TUR_SAYISI ? TUR[bul[i].tur] : "?");
                if (bul[i].yol[0]) { dize_ekle(s, sizeof s, ": "); dize_ekle(s, sizeof s, bul[i].yol); }
                dize_ekle(s, sizeof s, " ("); sayi_yaz(t, bul[i].deger, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, ")");
                pencere_yazi(pen, 30, y, s, R_YAZI);
            }
        }
    } else {
        pencere_yazi(pen, 16, y, "dosya          imaj      disk       durum", R_SONUK); y += SAT;
        if (dz_n == 0) pencere_yazi(pen, 16, y, "Dizge imajı yok ya da doğrulanamadı (bkz. ERGENEKON).", R_UYARI);
        int bozuk = 0;
        for (int i = 0; i < dz_n; i++) if (dz[i].durum) bozuk++;
        for (int i = 0; i < dz_n && y < YUK - 60; i++, y += SAT - 2) {
            if (i == secili) pencere_dikdortgen(pen, 8, y - 2, GEN - 16, SAT - 2, 0x24424Cu);
            pencere_yazi(pen, 16, y, dz[i].ad, R_YAZI);
            sayi_yaz(t, dz[i].boy, 1); pencere_yazi(pen, 136, y, t, R_SONUK);
            if (dz[i].durum != 2) { sayi_yaz(t, dz[i].disk_boy, 1); pencere_yazi(pen, 216, y, t, R_SONUK); }
            pencere_yazi(pen, 304, y, dz[i].durum == 0 ? "aynı" : dz[i].durum == 1 ? "FARKLI" : "YOK",
                         dz[i].durum ? R_KIRMIZI : R_VURGU);
        }
        if (dz_n) {
            s[0] = 0;
            if (bozuk == 0) dize_ekle(s, sizeof s, "Bütün dizge dosyaları imajla aynı.");
            else { sayi_yaz(t, (uint64_u)bozuk, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " dizge dosyası imajdan farklı ya da eksik."); }
            pencere_yazi(pen, 16, YUK - 50, s, bozuk ? R_UYARI : R_VURGU);
        }
    }
    pencere_yazi(pen, 16, YUK - 26, durum, durum_renk);
    pencere_sun(pen);
}

static void onar(void) {
    long simdi = ticks();
    if (onay_zaman == 0 || simdi - onay_zaman > 5 * hz) {
        onay_zaman = simdi;
        durum_yaz("Onarım diske yazar ve kesilen veri geri gelmez. Onaylamak için yeniden 'Onar'.", R_UYARI);
        return;
    }
    onay_zaman = 0;
    asena_rapor_u r;
    r.hata = 0;
    long s = syscall(SYS_ASENA_ONAR, 0, 0, (long)&r, SYS_ONAY);
    char m[120], t[16]; m[0] = 0;
    if (s != 0 && r.hata == 4) { durum_yaz("Dosya sayısı ASENA'nın sınırını (2047) aşıyor; güvenli onarım yapılamaz, hiçbir şey yazılmadı.", R_KIRMIZI); return; }
    if (s != 0) { durum_yaz("Onarım başarısız (disk yazılamadı ya da yetki yok).", R_KIRMIZI); return; }
    sayi_yaz(t, r.bulgu, 1); dize_ekle(m, sizeof m, t);
    dize_ekle(m, sizeof m, " sorun onarıldı, "); sayi_yaz(t, r.onarilan, 1); dize_ekle(m, sizeof m, t);
    dize_ekle(m, sizeof m, " düzeltme yazıldı.");
    denetle();
    durum_yaz(m, R_VURGU);
}

static void kurtar(void) {
    if (secili < 0 || secili >= dz_n) return;
    char neden[64];
    long r = syscall(SYS_ASENA_ONAR, 1, (long)dz[secili].ad, (long)neden, SYS_ONAY);
    char m[120]; m[0] = 0;
    dize_ekle(m, sizeof m, dz[secili].ad); dize_ekle(m, sizeof m, ": ");
    dize_ekle(m, sizeof m, r == 0 ? "imajdan geri yüklendi." : "kurtarılamadı.");
    dizge_oku();
    durum_yaz(m, r == 0 ? R_VURGU : R_KIRMIZI);
}

int main(void) {
    pen = pencere_ac("ASENA — Kurtarma", 170, 70, GEN, YUK);
    if (pen < 0) return 1;
    hz = (int)sistem_bil(SYSBIL_TIK_HZ); if (hz <= 0) hz = 100;
    denetle();
    durum_yaz("Disk salt okunur denetlendi. Onar iki kez basılınca yazar.", R_SONUK);
    ciz();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT || (e.tur == OLAY_TUS && e.tus == 27)) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TIK && e.y >= 38 && e.y < 62) {
                if (e.x >= 14 && e.x < 104) { gorunum = 0; denetle(); durum_yaz("Denetlendi (salt okunur).", R_SONUK); }
                else if (e.x >= 112 && e.x < 182) { gorunum = 0; onar(); }
                else if (e.x >= 190 && e.x < 340) { gorunum = 1; dizge_oku(); durum_yaz("Dizge dosyaları imajla karşılaştırıldı.", R_SONUK); }
                else if (gorunum == 1 && e.x >= 348 && e.x < 498) kurtar();
            } else if (e.tur == OLAY_TIK && gorunum == 1 && e.y >= UST + SAT) {
                int i = (e.y - UST - SAT + 2) / (SAT - 2);
                if (i >= 0 && i < dz_n) secili = i;
            } else if (e.tur == OLAY_TUS && gorunum == 1) {
                if (e.tus == 0xE000 && secili > 0) secili--;
                else if (e.tus == 0xE001 && secili + 1 < dz_n) secili++;
                else if (e.tus == 13) kurtar();
            }
            ciz();
        }
        if (!oldu) sleep_ticks(2);
    }
}
