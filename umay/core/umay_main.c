/* UMAY — giriş noktası ve ana döngü.
 * Tek Ring3 süreci. Bloklamayan olay döngüsü: kanalları yokla → görev ver →
 * OTAĞ'ı bir tur döndür → yanıtları kanallara ve ÜLGEN'e yaz. */
#include "umay.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_ozan.h"
#include "umay_bengi.h"
#include "umay_bilgi.h"
#include "umay_alp.h"
#include "umay_kanal.h"
#include "umay_us.h"
#include "umay_onay.h"
#include "umay_gui.h"
#include "umay_ses.h"

static otag_t        g_otag;
static umay_kanal_t *g_kanal[4];
static int           g_kanal_sayi;
static int           g_kosuyor = 1;
static kanal_tur_t   g_son_kanal = KANAL_KLAVYE;
static uint32_t      g_son_kaynak;

/* Yanıtı ÇIKTIĞI soruya yönlendir. UYUM düzeltmesi: görevler
 * eşzamansız biter; eskiden yanıt 'en son hangi kanaldan/kimden ileti
 * geldiyse' oraya gidiyordu. İki istemci aynı anda sorunca birinin
 * yanıtı ötekine düşebiliyordu. Klavye (konsol) her yanıtı görür. */
static void yanit_yaz_hedef(const char *metin, kanal_tur_t kanal, uint32_t kaynak) {
    gui_satir_ekle("UMAY", metin);
    for (int i = 0; i < g_kanal_sayi; i++) {
        umay_kanal_t *k = g_kanal[i];
        if (!k->acik || !k->yaz) continue;
        if (k->tur == KANAL_KLAVYE) { k->yaz(k, metin); continue; }
#ifdef UMAY_ESKI_YONLENDIRME
        /* KONTROL GRUBU (yalniz sinama): eski davranis - son kanal, son
         * gonderen. Uyum sinamasi bunu YAKALAMALI. */
        (void)kanal; (void)kaynak;
        if (k->tur == g_son_kanal) k->yaz(k, metin);
        continue;
#endif
        if (k->tur != kanal) continue;
        if (k->yaz_kime) k->yaz_kime(k, kaynak, metin);
        else             k->yaz(k, metin);
    }
    if (ses_var_mi()) ses_soyle(metin);
}

/* Eşzamanlı yanıt: o an işlenen iletinin kanalına/kaynağına. */
static void yanit_yaz(const char *metin) {
    yanit_yaz_hedef(metin, g_son_kanal, g_son_kaynak);
}

/* Onay buyrukları döngüde ele alınır — ajanlara girmez. */
static int onay_buyrugu(const char *soz) {
    const char *p = 0;
    int ver = 0;
    if ((p = um_icinde(soz, "onay ver "))) { p += um_uzun("onay ver "); ver = 1; }
    else if ((p = um_icinde(soz, "onay iptal "))) p += um_uzun("onay iptal ");
    else if (um_esit(soz, "onaylar")) {
        char c[512]; um_kopya(c, sizeof c, "Karar bekleyenler:\n");
        int n = onay_bekleyen_sayi();
        for (int i = 0; i < n; i++) {
            onay_t *k = onay_sirali(i);
            if (!k) continue;
            char s[192];
            um_bicim(s, sizeof s, "  %s — %s\n", k->kimlik, k->baslik);
            um_ekle(c, sizeof c, s);
        }
        if (n == 0) um_ekle(c, sizeof c, "  (yok)\n");
        yanit_yaz(c);
        return 1;
    } else return 0;

    char kimlik[16]; uint32_t o = 0;
    while (*p && *p != ' ' && o + 1 < sizeof kimlik) kimlik[o++] = *p++;
    kimlik[o] = 0;

    char c[256];
    if (ver) {
        if (onay_ver(kimlik)) {
            /* onaya bağlanmış görev varsa kuyruğa geri alınır */
            for (int i = 0; i < UMAY_GOREV_MAX; i++) {
                gorev_t *gg = &g_otag.havuz[i];
                if (gg->dolu && gg->durum == GD_ONAY_BEKLER && um_esit(gg->onay_kimlik, kimlik)) {
                    gg->onay_gerekli = 0;
                    gorev_t yeni = *gg;
                    gg->dolu = 0;
                    otag_gorev_ver(&g_otag, &yeni);
                }
            }
            um_bicim(c, sizeof c, "%s onaylandı. Elimi işe attım.\n", kimlik);
        }
        else um_bicim(c, sizeof c, "%s diye bir onay bulamadım; süresi geçmiş olabilir.\n", kimlik);
    } else {
        if (onay_reddet(kimlik)) um_bicim(c, sizeof c, "%s geri çevrildi. Dokunmam.\n", kimlik);
        else um_bicim(c, sizeof c, "%s bulunamadı.\n", kimlik);
    }
    yanit_yaz(c);
    return 1;
}

static void ileti_isle(const umay_ileti_t *i) {
    g_son_kanal  = i->tur;
    g_son_kaynak = i->kaynak;
    gui_satir_ekle("SEN", i->govde);

    if (onay_buyrugu(i->govde)) return;

    coz_t c = ozan_coz(i->govde);

    gorev_t g;
    gorev_hazirla(&g, (gorev_tur_t)c.tur, i->govde);
    g.niyet  = c.niyet;
    g.alan   = c.alan;
    g.kanal  = i->tur;
    g.kaynak = i->kaynak;
    um_kopya(g.konu, sizeof g.konu, c.konu);

    /* öncelik: onarım ve denetim beklemez */
    if (c.niyet == NIYET_ONARIM)  g.oncelik = ONC_YUKSEK;
    if (c.niyet == NIYET_DENETIM) g.oncelik = ONC_YUKSEK;

    /* kalıcı hafızayı göreve dayanak olarak ver */
    bengi_baglam(g.baglam, sizeof g.baglam);

    /* araştırma, çözümleme ve tasarım çıktıları bengi taşa geçer */
    g.bengiye_yaz = (g.tur == GT_ARASTIR || g.tur == GT_ANALIZ ||
                     g.tur == GT_TASARLA || g.tur == GT_KONTROL);

    int sonuc;
    if (c.niyet == NIYET_SELAM || c.niyet == NIYET_VEDA || c.niyet == NIYET_DURUM) {
        /* bunlar görev açmaz; OZAN doğrudan yanıtlar */
        char y[GOREV_SONUC_MAX];
        if (c.niyet == NIYET_DURUM) {
            char d[512];
            um_bicim(d, sizeof d, "Otağda %d iş bekliyor, %u iş bitti. Bellekte %d kayıt var.\n",
                     otag_bekleyen_sayi(&g_otag), g_otag.bitmis, bengi_sayi());
            ozan_soyle(c.niyet, d, y, sizeof y);
        } else ozan_soyle(c.niyet, 0, y, sizeof y);
        yanit_yaz(y);
        if (c.niyet == NIYET_VEDA) g_kosuyor = 0;
        if (gui_kapat_istendi()) g_kosuyor = 0;   /* pencere kapatıldı */
        return;
    }

    if (c.alan != ALAN_YOK && c.yardimci_sayi > 0) {
        /* çok uzmanlı soru: meclis kur */
        alan_t alanlar[UMAY_MECLIS_UYE + 1];
        int n = 0;
        alanlar[n++] = c.alan;
        for (int y = 0; y < c.yardimci_sayi && n <= UMAY_MECLIS_UYE; y++)
            alanlar[n++] = c.yardimci[y];
        sonuc = otag_meclis_kur(&g_otag, &g, alanlar, n);
        if (sonuc > 0) gui_durum("otağ toplandı — çok uzmanlı görev");
    } else {
        if (c.alan != ALAN_YOK && !otag_alan_var_mi(&g_otag, c.alan)) g.alan = ALAN_YOK;
        sonuc = otag_gorev_ver(&g_otag, &g);
        if (sonuc > 0) gui_durum(g.alan != ALAN_YOK ? alan_adi(g.alan) : gorev_tur_adi(g.tur));
    }
    if (sonuc < 0) yanit_yaz("Otağım dolu, biraz soluklan.\n");

}

/* Dis baglanti (static DEGIL): static yapilirsa derleyici main'e
 * gomebilir ve umay_tur'un buyuk cercevesi main'in cercevesine eklenir
 * (ring3 yigini sinirli). Bu yuzden yalniz bildirim verilir. */
int  umay_kur(void);
void umay_tur(void);
int  umay_kosuyor(void);
void umay_kapan(void);
void _start(void);

int umay_kur(void) {
    ayar_kur("/kut/umay/ayar.txt");
    gun_yolu_ayarla("/kut/umay/gunluk.txt");
    /* ÖTÜKEN sözleşmesi: platform bu iki izi bekler — açılış satırı ve
     * 'umay' adlı IPC portu. Sınama takımı bunlarla doğruluyor. */
    up_gunluk("[UMAY] kullanici alaninda baslatildi (surum 1.0)");
    gun_yaz("UMAY %s uyanıyor.", UMAY_SURUM);
    /* Dede Korkut'un sesi: açılış sözü kopuz ezgisiyle söylenir.
     * Ses yoksa (ana bilgisayar kipi) sessizce günlüğe düşer. */
    up_ozan_soyle("Ötüken'in yurdunda ocak tütüyor.", 2);

    ozan_kur(ayar_metin("kullanici", "Hakan"));
    bengi_kur(ayar_metin("bengi", "/kut/umay/bengi.txt"));
    onay_kur((uint32_t)ayar_sayi("onay_omur_ms", 600000));

    /* ÖTÜKEN'in FAT16 birimi düz köktür: alt dizin YOK, adlar 8.3.
     * Bu yüzden kütüphane kökü varsayılan olarak "/" olur; /kut/umay
     * yalnızca ana bilgisayar kipinde anlamlıdır. */
#ifdef UMAY_OTUKEN
    const char *kok = ayar_metin("kutuphane", "/");
#else
    const char *kok = ayar_metin("kutuphane", "/kut/umay");
#endif
    bilgi_dizin_kur(kok);                 /* 12 uzman alanı + ortak havuz */
    int d = bilgi_doktrin_yukle();
    int k = bilgi_tara(kok);
    gun_yaz("Bilgi: %d doktrin, %d bitig.", d, k);

    us_kur(ayar_metin("model", 0));       /* PORT'ta motor yoksa stub'a düşer */
    gun_yaz("US motoru: %s", us_motor_adi());

    /* Ses ayrı bir arka uçtur: Dede Korkut sürücüsü takılır. Açılamazsa
     * boş sürücüye düşülür ve bu açıkça günlüğe yazılır. */
    if (ses_surucu_tak(ses_ozan_surucu()) == UM_OLDU)
        gun_yaz("Ses: %s takıldı.", ses_surucu_adi());
    else {
        ses_surucu_tak(ses_bos_surucu());
        gun_yaz("Ses: sürücü açılamadı, sessiz koşuyorum.");
    }

    otag_kur(&g_otag);
    /* çekirdek görevliler */
    otag_alp_kat(&g_otag, alp_bilge());
    otag_alp_kat(&g_otag, alp_gozcu());
    otag_alp_kat(&g_otag, alp_onarici());
    otag_alp_kat(&g_otag, alp_yazman());
    /* uzman kadrosu — 12 ayrı ajan kaydı */
    int uzman = 0;
    for (int i = 0; i < alp_uzman_sayisi(); i++) {
        alp_t *a = alp_uzman_sirali(i);
        if (a && otag_alp_kat(&g_otag, a) == UM_OLDU) uzman++;
    }
    gun_yaz("Otağ kuruldu: 4 görevli, %d uzman.", uzman);

    g_kanal_sayi = 0;
    g_kanal[g_kanal_sayi++] = kanal_klavye();
    g_kanal[g_kanal_sayi++] = kanal_ipc("umay");   /* ÖTÜKEN port adı */
    g_kanal[g_kanal_sayi++] = kanal_ses();
    for (int i = 0; i < g_kanal_sayi; i++)
        if (g_kanal[i]->ac) g_kanal[i]->ac(g_kanal[i]);

    /* Yüzey tamponu çekirdek öbeğinden ayrılır: 900x600x4 = 2.1 MB
     * kaynak baskısı yaratıp öbek sınamasını düşürüyordu. 640x400
     * (1 MB) yeterli ve ölçüldü: baskı sınaması temiz kalıyor. */
    gui_ac(560, 340);

    char acilis[512];
    ozan_acilis(acilis, sizeof acilis);
    yanit_yaz(acilis);
    return UM_OLDU;
}

/* YIĞIN: bu işlev eskiden yığında ~52 KB açıyordu (sonuc dizisi +
 * ileti). Ring3 yığını 64 KB; çağrı zinciriyle pay birkaç yüz bayta
 * iniyordu ve US1 yolu biraz derinleşince yığın TAŞTI (sayfa hatası,
 * adres yığın tabanının hemen altı). Döngü tek iş parçacıklı ve yeniden
 * girilmez: büyük diziler durağan alanda tutulur. */
static umay_ileti_t g_tur_ileti;
static sonuc_t      g_tur_sonuc[UMAY_ALP_MAX];

void umay_tur(void) {
    umay_ileti_t *const ip = &g_tur_ileti;
    um_bellek_doldur(ip, 0, sizeof *ip);

    if (gui_giris_yokla(ip)) ileti_isle(ip);

    for (int c = 0; c < g_kanal_sayi; c++) {
        umay_kanal_t *k = g_kanal[c];
        if (!k->acik || !k->yokla) continue;
        um_bellek_doldur(ip, 0, sizeof *ip);
        if (k->yokla(k, ip)) ileti_isle(ip);
    }

    sonuc_t *const sonuc = g_tur_sonuc;
    int n = otag_dondur(&g_otag, sonuc, UMAY_ALP_MAX);
    for (int s = 0; s < n; s++) {
        /* kanalı bilinmeyen (iç) sonuç: eski davranış */
        if (sonuc[s].kanal == KANAL_YOK) yanit_yaz(sonuc[s].oz);
        else yanit_yaz_hedef(sonuc[s].oz, sonuc[s].kanal, sonuc[s].kaynak);
    }
    if (n > 0 && otag_catisma_sayi(&g_otag) > 0) gui_durum("kaynak çatışması var");
    if (n > 0) gui_durum(otag_mesgul(&g_otag) ? "otağ çalışıyor" : "hazır");

    gui_ciz(&g_otag);
    onay_temizle();
}

int umay_kosuyor(void) { return g_kosuyor; }

void umay_kapan(void) {
    gun_yaz("UMAY kapanıyor.");
    bengi_sakla();
    for (int i = 0; i < g_kanal_sayi; i++)
        if (g_kanal[i]->kapat) g_kanal[i]->kapat(g_kanal[i]);
    { umay_us_t *m = umay_us_al(); if (m && m->kapat) m->kapat(m); }
    gui_kapat();
}

#ifdef UMAY_OTUKEN
/* ÖTÜKEN giriş noktası. Çekirdek ELF'in e_entry'sine atlar; burada
 * yığın hazırdır (ring3.c dört sayfa haritalar). ulib'in crt0'ına
 * bağımlı olmamak için kendi _start'ımızı veriyoruz - ulib'deki
 * exit() sembolüne ihtiyaç kalmaz. */
extern int main(void);
__attribute__((naked, used)) void _start(void) {
    __asm__ __volatile__(
        "call main\n\t"
        "mov $5, %rax\n\t"      /* SYS_EXIT */
        "xor %rdi, %rdi\n\t"
        "syscall\n\t"
        "1: jmp 1b");
}
#endif

#ifndef UMAY_SINAMA
int main(void) {
    umay_kur();
    while (umay_kosuyor()) {
        umay_tur();
        up_uyu(16);            /* ~60 tur/sn; boşta işlemci yakmaz */
    }
    umay_kapan();
    return 0;
}
#endif
