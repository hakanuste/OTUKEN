/* =====================================================================
 * ÖTÜKEN "Sistem" — dizge izleyicisi (1.4.8)
 * Kendi adres alanında, yalnız EKRAN|KLAVYE|SISTEM yetkisiyle koşar.
 * Gösterir: çalışma süresi, bellek (toplam / kullanılan / boş çerçeve),
 * görev sayısı, hizmetler (durum + nabız). Saniyede bir tazelenir.
 * Süreç listesi GÖSTERİLMEZ: o çağrı (SYS_PROC_LISTE) TÖRE'de SÜREÇ
 * yetkisine bağlıdır ve bu uygulamaya verilmedi.
 * Esc / q / kapat düğmesi: çıkar.
 * ===================================================================== */
#include "uyg.h"

#define GEN 440
#define YUK 340
#define ARKA   0x14232Bu
#define PANO   0x1B313Au
#define YAZI   0xE8E2D0u
#define SONUK  0x8FA3A8u
#define VURGU  0x4FB3A9u
#define UYARI  0xE0A060u
#define KIRMIZI 0xD9534Fu

static int pen;

static void satir(int y, const char *etiket, const char *deger, uint32_u renk) {
    pencere_yazi(pen, 16, y, etiket, SONUK);
    pencere_yazi(pen, 180, y, deger, renk);
}

static const char *durum_adi(uint32_u d, uint32_u *renk) {
    switch (d) {
    case 0: *renk = SONUK;   return "durdu";
    case 1: *renk = UYARI;   return "başlıyor";
    case 2: *renk = VURGU;   return "çalışıyor";
    case 3: *renk = KIRMIZI; return "çöktü";
    default: *renk = SONUK;  return "devre dışı";
    }
}

static void ciz(uint64_u hz) {
    char s[64], t[24];
    pencere_doldur(pen, ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 34, PANO);
    pencere_yazi(pen, 16, 9, "ÖTÜKEN — Dizge Durumu", VURGU);

    uint64_u tik = sistem_bil(SYSBIL_TIK);
    uint64_u sn = hz ? tik / hz : 0;
    s[0] = 0;
    sayi_yaz(t, sn / 3600, 2); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, ":");
    sayi_yaz(t, (sn / 60) % 60, 2); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, ":");
    sayi_yaz(t, sn % 60, 2); dize_ekle(s, sizeof s, t);
    satir(48, "Çalışma süresi", s, YAZI);

    uint64_u toplam = sistem_bil(SYSBIL_TOPLAM_BELLEK);
    uint64_u bos = sistem_bil(SYSBIL_BOS_CERCEVE) * 4096ULL;
    uint64_u kul = toplam > bos ? toplam - bos : 0;
    s[0] = 0;
    sayi_yaz(t, kul >> 20, 1); dize_ekle(s, sizeof s, t);
    dize_ekle(s, sizeof s, " / ");
    sayi_yaz(t, toplam >> 20, 1); dize_ekle(s, sizeof s, t);
    dize_ekle(s, sizeof s, " MB kullanımda");
    satir(70, "Bellek", s, YAZI);
    int dolu = toplam ? (int)((kul * 400ULL) / toplam) : 0;
    pencere_dikdortgen(pen, 16, 92, 400, 10, PANO);
    pencere_dikdortgen(pen, 16, 92, dolu, 10, dolu > 340 ? UYARI : VURGU);

    s[0] = 0;
    sayi_yaz(t, sistem_bil(SYSBIL_BOS_CERCEVE), 1); dize_ekle(s, sizeof s, t);
    dize_ekle(s, sizeof s, " (4 KB)");
    satir(112, "Boş çerçeve", s, YAZI);
    sayi_yaz(t, sistem_bil(SYSBIL_GOREV_SAYISI), 1);
    satir(134, "Görev sayısı", t, YAZI);

    pencere_dikdortgen(pen, 0, 160, GEN, 1, PANO);
    pencere_yazi(pen, 16, 168, "Hizmetler", VURGU);
    pencere_yazi(pen, 180, 168, "durum", SONUK);
    pencere_yazi(pen, 320, 168, "nabız", SONUK);
    servis_bilgi_u sv[16];
    int n = servis_listesi(sv, 16);
    if (n < 0) {
        pencere_yazi(pen, 16, 190, "hizmet listesi okunamadı", KIRMIZI);
        n = 0;
    }
    int y = 190;
    for (int i = 0; i < n && y < YUK - 40; i++, y += 18) {
        uint32_u r;
        const char *d = durum_adi(sv[i].durum, &r);
        pencere_yazi(pen, 16, y, sv[i].name, YAZI);
        pencere_yazi(pen, 180, y, d, r);
        sayi_yaz(t, sv[i].nabiz, 1);
        pencere_yazi(pen, 320, y, t, SONUK);
    }
    if (n == 0 && y == 190) pencere_yazi(pen, 16, y, "(kayıtlı hizmet yok)", SONUK);
    pencere_yazi(pen, 16, YUK - 22, "Esc: kapat   saniyede bir tazelenir", SONUK);
    pencere_sun(pen);
}

int main(void) {
    pen = pencere_ac("Sistem — ÖTÜKEN", 560, 80, GEN, YUK);
    if (pen < 0) return 1;
    uint64_u hz = sistem_bil(SYSBIL_TIK_HZ);
    if (hz == 0) hz = 100;
    ciz(hz);
    uint64_u son = (uint64_u)ticks();
    for (;;) {
        olay_u o;
        while (pencere_olay(pen, &o) == 1) {
            if (o.tur == OLAY_KAPAT) goto cik;
            if (o.tur == OLAY_TUS && (o.tus == 27 || o.tus == 'q')) goto cik;
        }
        uint64_u simdi = (uint64_u)ticks();
        if (simdi - son >= hz) { son = simdi; ciz(hz); }
        sleep_ticks(2);
    }
cik:
    pencere_kapat(pen);
    return 0;
}
