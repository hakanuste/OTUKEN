/* =====================================================================
 * ERGENEKON — yeniden kurulum (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | KURTARMA. İşi çekirdek yapar
 * (src/ergenekon.c): dizge imajı (GRUB modülü, imzalı) doğrulanmadan hiçbir
 * şey yazılmaz.
 *   Dizgeyi yenile : eksik/bozuk dizge dosyaları imajdan (kullanıcı
 *                    dosyaları ve paketler kalır). İki kez basılınca.
 *   Sıfırdan kur   : birim biçimlenir, BÜTÜN dosyalar gider; "EVET" yazılıp
 *                    Enter ile onaylanır.
 *   Yeniden başlat : iki kez basılınca.
 * ===================================================================== */
#include "uyg.h"

#define GEN 660
#define YUK 420
static int pen, hz;
static ergenekon_u d;
static char durum[160];
static uint32_u durum_renk = R_SONUK;
static long onay_zaman; static int onay_ne;    /* 1 yenile, 3 yeniden başlat */
static int evet_bekle; static char evet[8]; static int evet_n;

static void durum_yaz(const char *m, uint32_u r) { durum[0] = 0; dize_ekle(durum, sizeof durum, m); durum_renk = r; }

static void dugme(int x, int y, int w, const char *m, uint32_u renk) {
    pencere_dikdortgen(pen, x, y, w, 26, renk);
    pencere_yazi(pen, x + 10, y + 5, m, R_YAZI);
}

static void ciz(void) {
    char s[160], t[16];
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "ERGENEKON — Yeniden Kurulum", R_VURGU);
    int y = 44;
    s[0] = 0;
    if (!d.var) dize_ekle(s, sizeof s, "Dizge imajı yok (ISO'daki /boot/ergenekon.krv yüklenmedi).");
    else {
        dize_ekle(s, sizeof s, "Dizge imajı: "); dize_ekle(s, sizeof s, d.gecerli ? d.ad : "?");
        dize_ekle(s, sizeof s, " "); dize_ekle(s, sizeof s, d.gecerli ? d.surum : "");
        dize_ekle(s, sizeof s, " · "); sayi_yaz(t, d.dosya, 1); dize_ekle(s, sizeof s, t);
        dize_ekle(s, sizeof s, " dosya · "); sayi_yaz(t, d.boy / 1024, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " KB");
    }
    pencere_yazi(pen, 16, y, s, d.var ? R_YAZI : R_KIRMIZI); y += 22;
    s[0] = 0;
    if (d.var) {
        dize_ekle(s, sizeof s, "İmza: ");
        if (d.gecerli) { dize_ekle(s, sizeof s, d.anahtar_no == 0 ? "üretim anahtarı" : "sınama anahtarı"); dize_ekle(s, sizeof s, " · doğrulandı"); }
        else { dize_ekle(s, sizeof s, "GEÇERSİZ — "); dize_ekle(s, sizeof s, d.neden); }
        pencere_yazi(pen, 16, y, s, d.gecerli ? R_VURGU : R_KIRMIZI);
    }
    y += 40;
    int etkin = d.gecerli;
    dugme(16, y, 170, "Dizgeyi yenile", etkin ? 0x2A4652u : 0x1B2A30u);
    pencere_yazi(pen, 200, y + 5, "eksik/bozuk dizge dosyaları; senin dosyaların kalır", R_SONUK); y += 40;
    dugme(16, y, 170, "Sıfırdan kur", etkin ? 0x4A2A2Au : 0x1B2A30u);
    pencere_yazi(pen, 200, y + 5, "disk biçimlenir: BÜTÜN dosyalar ve paketler gider", R_UYARI); y += 40;
    dugme(16, y, 170, "Yeniden başlat", 0x2A4652u);
    pencere_yazi(pen, 200, y + 5, "kurulumdan sonra önerilir", R_SONUK); y += 50;
    if (evet_bekle) {
        pencere_yazi(pen, 16, y, "Onay için EVET yaz ve Enter'a bas (Esc vazgeçer):", R_UYARI); y += 22;
        pencere_dikdortgen(pen, 16, y, 120, 24, 0xFFFFFFu);
        pencere_yazi(pen, 22, y + 4, evet, 0x10181Cu);
    }
    pencere_yazi(pen, 16, YUK - 26, durum, durum_renk);
    pencere_sun(pen);
}

static void sonuc_yaz(long r, const char *ne) {
    char m[160], t[16]; m[0] = 0;
    dize_ekle(m, sizeof m, ne);
    dize_ekle(m, sizeof m, r == 0 ? ": tamam, " : ": HATA, ");
    sayi_yaz(t, d.yazilan, 1); dize_ekle(m, sizeof m, t); dize_ekle(m, sizeof m, " dosya yazıldı");
    if (d.ayni) { dize_ekle(m, sizeof m, ", "); sayi_yaz(t, d.ayni, 1); dize_ekle(m, sizeof m, t); dize_ekle(m, sizeof m, " zaten sağlam"); }
    if (d.hata) { dize_ekle(m, sizeof m, ", "); sayi_yaz(t, d.hata, 1); dize_ekle(m, sizeof m, t); dize_ekle(m, sizeof m, " yazılamadı"); }
    dize_ekle(m, sizeof m, ".");
    durum_yaz(m, r == 0 ? R_VURGU : R_KIRMIZI);
}

static int onaylandi(int ne, const char *soru) {
    long simdi = ticks();
    if (onay_ne == ne && simdi - onay_zaman <= 5 * hz) { onay_ne = 0; return 1; }
    onay_ne = ne; onay_zaman = simdi;
    durum_yaz(soru, R_UYARI);
    return 0;
}

int main(void) {
    pen = pencere_ac("ERGENEKON — Yeniden Kurulum", 190, 90, GEN, YUK);
    if (pen < 0) return 1;
    hz = (int)sistem_bil(SYSBIL_TIK_HZ); if (hz <= 0) hz = 100;
    syscall(SYS_ERGENEKON, 0, (long)&d, 0, 0);
    durum_yaz(d.gecerli ? "İmaj doğrulandı; bir işlem seç." : "İmaj olmadan kurulum yapılamaz.", R_SONUK);
    ciz();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TUS && evet_bekle) {
                if (e.tus == 27) { evet_bekle = 0; durum_yaz("Vazgeçildi.", R_SONUK); }
                else if (e.tus == 8 && evet_n > 0) evet[--evet_n] = 0;
                else if (e.tus == 13) {
                    evet_bekle = 0;
                    if (evet[0] == 'E' && evet[1] == 'V' && evet[2] == 'E' && evet[3] == 'T' && !evet[4]) {
                        durum_yaz("Biçimleniyor ve kuruluyor…", R_UYARI); ciz();
                        long r = syscall(SYS_ERGENEKON, 2, (long)&d, SYS_ONAY, 0);
                        sonuc_yaz(r, "Sıfırdan kurulum");
                    } else durum_yaz("Onay yazılmadı; hiçbir şey yapılmadı.", R_SONUK);
                } else if (e.tus >= 'a' && e.tus <= 'z' && evet_n < 6) { evet[evet_n++] = (char)(e.tus - 32); evet[evet_n] = 0; }
                else if (e.tus >= 'A' && e.tus <= 'Z' && evet_n < 6) { evet[evet_n++] = (char)e.tus; evet[evet_n] = 0; }
            } else if (e.tur == OLAY_TUS && e.tus == 27) { pencere_kapat(pen); return 0; }
            else if (e.tur == OLAY_TIK && e.x >= 16 && e.x < 186) {
                if (e.y >= 106 && e.y < 132 && d.gecerli) {
                    if (onaylandi(1, "Dizge dosyaları imajdan yazılacak. Onaylamak için yeniden bas.")) {
                        durum_yaz("Yenileniyor…", R_UYARI); ciz();
                        long r = syscall(SYS_ERGENEKON, 1, (long)&d, SYS_ONAY, 0);
                        sonuc_yaz(r, "Yenileme");
                    }
                } else if (e.y >= 146 && e.y < 172 && d.gecerli) {
                    evet_bekle = 1; evet_n = 0; evet[0] = 0; onay_ne = 0;
                    durum_yaz("DİKKAT: disk biçimlenecek.", R_KIRMIZI);
                } else if (e.y >= 186 && e.y < 212) {
                    if (onaylandi(3, "Yeniden başlatmak için yeniden bas.")) {
                        durum_yaz("Yeniden başlatılıyor…", R_UYARI); ciz();
                        syscall(SYS_ERGENEKON, 3, (long)&d, SYS_ONAY, 0);
                    }
                }
            }
            ciz();
        }
        if (!oldu) sleep_ticks(2);
    }
}
