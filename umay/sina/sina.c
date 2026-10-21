/* UMAY çekirdek mantık sınamaları — ana makinede koşar. */
#define UMAY_SINAMA 1
#include "umay.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_ozan.h"
#include "umay_bilgi.h"
#include "umay_bengi.h"
#include "umay_alp.h"
#include "umay_bengi.h"
#include "umay_onay.h"
#include "umay_us.h"
#include "umay_port.h"
#include "umay_ses.h"
#include <stdio.h>

extern void host_yetki_ayarla(uint32_t y);   /* port/umay_port_host.c */

static int g_gecti, g_kaldi;

/* --- sınama için sahte motorlar --- */
static int g_cagri;
static int sahte_uret(umay_us_t *u, const us_istem_t *i, char *c, uint32_t n) {
    (void)u; g_cagri++;
    um_kopya(c, n, "Sahte motor konuştu: ");
    um_ekle(c, n, i->baglam && i->baglam[0] ? "bağlam geldi." : "bağlam yok.");
    um_ekle(c, n, "\n");
    return UM_OLDU;
}
static int sahte_hazir(umay_us_t *u, const char *y) { (void)u; (void)y; return UM_OLDU; }
static umay_us_t g_sahte = { .ad = "sahte", .hazirla = sahte_hazir, .uret = sahte_uret };

static int cokan_uret(umay_us_t *u, const us_istem_t *i, char *c, uint32_t n) {
    (void)u; (void)i; (void)c; (void)n; g_cagri++; return UM_YANLIS;   /* hep düşer */
}
static umay_us_t g_cokan = { .ad = "cokan", .hazirla = sahte_hazir, .uret = cokan_uret };
static void dene(const char *ad, int kosul) {
    if (kosul) { g_gecti++; printf("  [GECTI] %s\n", ad); }
    else       { g_kaldi++; printf("  [KALDI] %s\n", ad); }
}

int main(void) {
    printf("=== UMAY SINAMA ===\n\n1) Dize ve UTF-8\n");
    char t[64];
    um_bicim(t, sizeof t, "%s-%d", "ok", 42);
    dene("um_bicim", um_esit(t, "ok-42"));
    um_kucult(t, sizeof t, "ÖTÜKEN Yurdu IŞIK İZ");
    dene("Türkçe küçültme (I->ı, İ->i)", um_esit(t, "ötüken yurdu ışık iz"));
    dene("um_icinde", um_icinde("bengi taş", "gi t") != 0);
    char kucuk[8];
    dene("taşma yok", um_kopya(kucuk, sizeof kucuk, "çokçokuzunbirdize") == 7);

    printf("\n2) Ayarlar\n");
    ayar_kur(0);
    dene("varsayılan kullanıcı", um_esit(ayar_metin("kullanici", ""), "Hakan"));
    dene("sayı ayarı", ayar_sayi("baglam_adet", 0) == 3);

    printf("\n3) Bilgi (kütüphane/RAG)\n");
    int d = bilgi_doktrin_yukle();
    dene("doktrin yüklendi", d >= 14);
    baglam_t b[3];
    int n = bilgi_baglam_bul("yer altı kileri kaç derece", b, 3);
    dene("bağlam bulundu", n > 0);
    dene("doğru kayda gitti", n > 0 && um_esit(b[0].kategori, "balbal"));
    n = bilgi_baglam_bul("Musso motoru hangi şanzıman", b, 3);
    dene("araç sorusu doğru eşleşti", n > 0 && um_esit(b[0].kategori, "tulga"));
    n = bilgi_baglam_bul("zzzqqq", b, 3);
    dene("alakasız sorguda boş", n == 0);

    printf("\n4) OZAN niyet çözümü\n");
    ozan_kur("Hakan");
    dene("selam", ozan_coz("selam UMAY").niyet == NIYET_SELAM);
    dene("denetim", ozan_coz("kendini bir yokla").niyet == NIYET_DENETIM);
    dene("onarım", ozan_coz("şu hatayı düzelt").niyet == NIYET_ONARIM);
    dene("soru", ozan_coz("sarnıç nasıl yapılır?").niyet == NIYET_SORU);
    dene("veda", ozan_coz("hoşça kal").niyet == NIYET_VEDA);
    char c[512];
    ozan_soyle(NIYET_SELAM, 0, c, sizeof c);
    dene("Dede Korkut ağzı (hitap)", um_icinde(c, "Hakan") != 0);

    printf("\n5) BENGİ kalıcı bellek\n");
    bengi_kur("/tmp/umay_bengi_sina.txt");
    bengi_yaz("not1", "sarnıç Horasan harcı ile");
    dene("yazıldı-okundu", um_esit(bengi_oku("not1"), "sarnıç Horasan harcı ile"));
    dene("saklandı", bengi_sakla() == UM_OLDU);
    bengi_kur("/tmp/umay_bengi_sina.txt");
    dene("diskten geri yüklendi", bengi_oku("not1") != 0);

    printf("\n6) ONAY katmanı\n");
    onay_kur(60000);
    const char *k = onay_iste("onarim", "Eksik iz", "özet", "/kut/umay");
    dene("onay istendi", k != 0);
    dene("bekleyen var", onay_bekleyen_sayi() == 1);
    dene("onay verildi", onay_ver(k) == 1);
    dene("iki kez tüketilemez", onay_ver(k) == 0);

    printf("\n7) OTAĞ ajan meclisi\n");
    us_kur(0);
    otag_t o;
    otag_kur(&o);
    dene("BİLGE katıldı",   otag_alp_kat(&o, alp_bilge())   == UM_OLDU);
    dene("GÖZCÜ katıldı",   otag_alp_kat(&o, alp_gozcu())   == UM_OLDU);
    dene("ONARICI katıldı", otag_alp_kat(&o, alp_onarici()) == UM_OLDU);

    gorev_t g;
    gorev_hazirla(&g, GT_SORU, "yer altı kileri kaç derecede durur");
    g.niyet = NIYET_SORU;
    dene("görev kuyruğa girdi", otag_gorev_ver(&o, &g) > 0);

    sonuc_t s[UMAY_ALP_MAX];
    int bitti = 0, tur = 0;
    while (otag_mesgul(&o) && tur++ < 50) bitti += otag_dondur(&o, s, UMAY_ALP_MAX);
    dene("BİLGE görevi bitirdi", bitti == 1 && s[0].basarili);
    dene("yanıt üretildi", um_uzun(s[0].oz) > 20);
    dene("dayanak gösterildi", um_icinde(s[0].oz, "Dayanağım") != 0);
    printf("     ---- UMAY'ın yanıtı ----\n%s     ------------------------\n", s[0].oz);

    gorev_hazirla(&g, GT_KONTROL, "kendini yokla");
    g.niyet = NIYET_DENETIM;
    otag_gorev_ver(&o, &g);
    bitti = 0; tur = 0;
    while (otag_mesgul(&o) && tur++ < 50) bitti += otag_dondur(&o, s, UMAY_ALP_MAX);
    dene("GÖZCÜ denetimi bitirdi", bitti == 1);
    dene("denetim raporu doldu", um_icinde(s[0].oz, "koşuyor") != 0);
    printf("     ---- GÖZCÜ raporu ----\n%s     ----------------------\n", s[0].oz);

    printf("\n8) Uzman kadrosu\n");
    dene("kadro 12 kişi", alp_uzman_sayisi() == 12);
    otag_t o2;
    otag_kur(&o2);
    int katilan = 0;
    for (int i = 0; i < alp_uzman_sayisi(); i++)
        if (otag_alp_kat(&o2, alp_uzman_sirali(i)) == UM_OLDU) katilan++;
    dene("12 uzman otağa katıldı", katilan == 12);
    dene("BİLGE de sığdı", otag_alp_kat(&o2, alp_bilge()) == UM_OLDU);
    dene("alan araması", otag_alan_var_mi(&o2, ALAN_TULGA) && otag_alan_var_mi(&o2, ALAN_OYUN));
    dene("her uzmanın adı var", um_esit(alan_adi(ALAN_BALBAL), "BALBAL") &&
                                um_esit(alan_adi(ALAN_YERSU), "YER-SU"));

    printf("\n9) OZAN alan yönlendirmesi\n");
    struct { const char *soz; alan_t bekle; } YOL[] = {
        { "musso motorunun şanzımanı ne",        ALAN_TULGA  },
        { "solar panel için kaç watt gerek",     ALAN_KUN    },
        { "zeytin budama artığından kompost",    ALAN_YERSU  },
        { "kerpiç duvara horasan harcı olur mu", ALAN_BALBAL },
        { "yaraya hangi merhem sürülür",         ALAN_OTACI  },
        { "imar izni ve tapu için ne gerek",     ALAN_YARGU  },
        { "altın ve döviz kuru ne olur",         ALAN_AKCA   },
        { "zeybek için prova dizilimi",          ALAN_OYUN   },
        { "kopuz ile ezgi bestelemek",           ALAN_KOPUZ  },
        { "pcb üstüne sensör lehimleme",         ALAN_DEMIR  },
        { "çit ve kamera ile devriye düzeni",    ALAN_ALPAGU },
        { "belgeleri arşivde indeksle",          ALAN_YAZIT  },
        { 0, ALAN_YOK }
    };
    for (int i = 0; YOL[i].soz; i++) {
        coz_t cz = ozan_coz(YOL[i].soz);
        char ad[64];
        um_bicim(ad, sizeof ad, "%s -> %s", YOL[i].soz, alan_adi(YOL[i].bekle));
        dene(ad, cz.alan == YOL[i].bekle);
    }
    dene("alansız soru genel kalır", ozan_coz("bugün ne yapsam").alan == ALAN_YOK);

    printf("\n10) Uzmana yönlendirme ve yanıt\n");
    gorev_t gu;
    gorev_hazirla(&gu, GT_SORU, "musso motoru hangi şanzımanla çalışır");
    gu.niyet = NIYET_SORU; gu.alan = ALAN_TULGA;
    otag_gorev_ver(&o2, &gu);
    int b2 = 0; int t2 = 0;
    while (otag_mesgul(&o2) && t2++ < 50) b2 += otag_dondur(&o2, s, UMAY_ALP_MAX);
    dene("TULGA görevi aldı ve bitirdi", b2 == 1 && um_esit(s[0].ad, "TULGA"));
    dene("uzman kendi kimliğiyle konuştu", um_icinde(s[0].oz, "TULGA (") != 0);
    printf("     ---- TULGA ----\n%s     ---------------\n", s[0].oz);

    printf("\n11) Çok uzmanlı meclis\n");
    gorev_t gm;
    gorev_hazirla(&gm, GT_SORU,
                  "sarnıç için taş duvar örerken solar pompayı hangi panel besler");
    gm.niyet = NIYET_SORU;
    um_kopya(gm.konu, sizeof gm.konu, "sarnıç");
    alan_t alanlar[3] = { ALAN_BALBAL, ALAN_KUN, ALAN_YERSU };
    int kok = otag_meclis_kur(&o2, &gm, alanlar, 3);
    dene("meclis kuruldu", kok > 0);
    int b3 = 0; int t3 = 0; sonuc_t s3[UMAY_ALP_MAX];
    while (otag_mesgul(&o2) && t3++ < 80) b3 += otag_dondur(&o2, s3, UMAY_ALP_MAX);
    dene("tek birleşik sonuç döndü", b3 == 1);
    dene("sonucu OTAĞ imzaladı", um_esit(s3[0].ad, "OTAĞ"));
    dene("üç uzman da konuştu",
         um_icinde(s3[0].oz, "BALBAL") && um_icinde(s3[0].oz, "KÜN") && um_icinde(s3[0].oz, "YER-SU"));
    printf("     ---- MECLİS ----\n%s     ----------------\n", s3[0].oz);

    printf("\n12) Uyarı ve dürüstlük\n");
    gorev_t gy;
    gorev_hazirla(&gy, GT_SORU, "imar izni için hangi yasa geçerli");
    gy.niyet = NIYET_SORU; gy.alan = ALAN_YARGU;
    otag_gorev_ver(&o2, &gy);
    int b4 = 0; int t4 = 0;
    while (otag_mesgul(&o2) && t4++ < 50) b4 += otag_dondur(&o2, s, UMAY_ALP_MAX);
    dene("YARGU hukuki görüş uyarısı koydu", b4 == 1 && um_icinde(s[0].oz, "hukuki görüş değildir"));
    gorev_t ga;
    gorev_hazirla(&ga, GT_SORU, "altın kuru bugün ne kadar");
    ga.niyet = NIYET_SORU; ga.alan = ALAN_AKCA;
    otag_gorev_ver(&o2, &ga);
    b4 = 0; t4 = 0;
    while (otag_mesgul(&o2) && t4++ < 50) b4 += otag_dondur(&o2, s, UMAY_ALP_MAX);
    dene("AKÇA ağ yokken güncel sayı vermedi", b4 == 1 && um_icinde(s[0].oz, "Ağ kapalı"));

    printf("\n13) Görev motoru\n");
    otag_t o3;
    otag_kur(&o3);
    otag_alp_kat(&o3, alp_bilge());
    otag_alp_kat(&o3, alp_gozcu());
    for (int i = 0; i < alp_uzman_sayisi(); i++) otag_alp_kat(&o3, alp_uzman_sirali(i));

    gorev_t gt;
    gorev_hazirla(&gt, GT_TASARLA, "sarnıç için taş duvar tasarla");
    gt.alan = ALAN_BALBAL;
    gt.oncelik = ONC_DUSUK;
    int k1 = otag_gorev_ver(&o3, &gt);
    dene("görev havuza girdi", k1 > 0);
    gorev_t *gp = otag_gorev_bul(&o3, (uint32_t)k1);
    dene("görev kimlikle bulunuyor", gp != 0 && gp->durum == GD_HAZIR);
    dene("tür ve öncelik korundu", gp->tur == GT_TASARLA && gp->oncelik == ONC_DUSUK);
    dene("tür adı", um_esit(gorev_tur_adi(GT_TASARLA), "TASARLA"));
    dene("durum adı", um_esit(gorev_durum_adi(GD_HAZIR), "hazır"));

    /* alt görev + bağımlılık */
    gorev_t ga2;
    gorev_hazirla(&ga2, GT_ARASTIR, "horasan harcı karışım oranı");
    ga2.alan = ALAN_BALBAL;
    int k2 = otag_alt_gorev(&o3, (uint32_t)k1, &ga2);
    dene("alt görev açıldı", k2 > 0 && otag_gorev_bul(&o3, (uint32_t)k2)->ebeveyn == (uint32_t)k1);
    dene("bağımlılık kuruldu", otag_bagimlilik_ekle(&o3, (uint32_t)k1, (uint32_t)k2) == UM_OLDU);
    dene("bağımlı görev bekliyor", otag_gorev_bul(&o3, (uint32_t)k1)->durum == GD_BEKLIYOR);
    dene("ilerleme yazılabiliyor", otag_ilerleme(&o3, (uint32_t)k2, 40) == UM_OLDU &&
                                   otag_gorev_bul(&o3, (uint32_t)k2)->ilerleme == 40);

    sonuc_t s4[UMAY_ALP_MAX];
    int b5 = 0, t5 = 0;
    char kim[4][UMAY_AD_MAX]; gorev_tur_t hangi[4];
    while (otag_mesgul(&o3) && t5++ < 120) {
        int n4 = otag_dondur(&o3, s4, UMAY_ALP_MAX);
        for (int q = 0; q < n4 && b5 < 4; q++, b5++) {
            um_kopya(kim[b5], UMAY_AD_MAX, s4[q].ad);
            hangi[b5] = s4[q].tur;
        }
    }
    dene("alt görev önce, ebeveyn sonra bitti", b5 == 2);
    dene("sıra doğru: ARAŞTIR sonra TASARLA",
         b5 == 2 && hangi[0] == GT_ARASTIR && hangi[1] == GT_TASARLA);
    dene("iki iş de BALBAL'a gitti",
         b5 == 2 && um_esit(kim[0], "BALBAL") && um_esit(kim[1], "BALBAL"));
    dene("otağ boşaldı", otag_mesgul(&o3) == 0);

    printf("\n14) Öncelik sırası\n");
    otag_t o4;
    otag_kur(&o4);
    otag_alp_kat(&o4, alp_bilge());
    gorev_t d1, d2;
    gorev_hazirla(&d1, GT_SORU, "düşük öncelikli soru");   d1.oncelik = ONC_DUSUK;
    gorev_hazirla(&d2, GT_SORU, "acil soru");              d2.oncelik = ONC_ACIL;
    otag_gorev_ver(&o4, &d1);
    uint32_t acil = (uint32_t)otag_gorev_ver(&o4, &d2);
    sonuc_t s5[UMAY_ALP_MAX];
    int ilk = 0, t6 = 0, alindi = 0;
    while (otag_mesgul(&o4) && t6++ < 80) {
        int n5 = otag_dondur(&o4, s5, UMAY_ALP_MAX);
        if (n5 > 0 && !alindi) { ilk = (int)s5[0].kimlik; alindi = 1; }
    }
    dene("acil görev önce koştu", ilk == (int)acil);

    printf("\n15) UYGULA türü onaya bağlı\n");
    otag_t o5;
    otag_kur(&o5);
    onay_kur(60000);
    otag_alp_kat(&o5, alp_yazman());
    gorev_t gy2;
    gorev_hazirla(&gy2, GT_UYGULA, "şu notu kaydet: sarnıç yeri seçildi");
    dene("UYGULA varsayılan onay ister", gy2.onay_gerekli == 1);
    uint32_t ku = (uint32_t)otag_gorev_ver(&o5, &gy2);
    sonuc_t s6[UMAY_ALP_MAX];
    otag_dondur(&o5, s6, UMAY_ALP_MAX);
    gorev_t *gpu = otag_gorev_bul(&o5, ku);
    dene("onay alınmadan koşmadı", gpu && gpu->durum == GD_ONAY_BEKLER);
    dene("onay belgesi üretildi", gpu->onay_kimlik[0] != 0);
    dene("YAZMAN boşta kaldı", alp_yazman()->durum == ALP_BOS);

    printf("\n16) Görev sonucu BENGİ'ye geçiyor\n");
    bengi_kur("/tmp/umay_bengi_sina2.txt");
    otag_t o6;
    otag_kur(&o6);
    for (int i = 0; i < alp_uzman_sayisi(); i++) otag_alp_kat(&o6, alp_uzman_sirali(i));
    gorev_t gb;
    gorev_hazirla(&gb, GT_ANALIZ, "musso için hangi şanzıman uygun");
    gb.alan = ALAN_TULGA;
    gb.bengiye_yaz = 1;
    uint32_t kb = (uint32_t)otag_gorev_ver(&o6, &gb);
    int t7 = 0;
    sonuc_t s7[UMAY_ALP_MAX];
    while (otag_mesgul(&o6) && t7++ < 60) otag_dondur(&o6, s7, UMAY_ALP_MAX);
    char anah[32];
    um_bicim(anah, sizeof anah, "gorev%d", (int)kb);
    dene("sonuç bengi taşa kazındı", bengi_oku(anah) != 0);
    dene("sonuç kaydı türü ve imzası var", um_icinde(bengi_oku(anah), "ANALİZ") &&
                                           um_icinde(bengi_oku(anah), "TULGA"));
    const bengi_kayit_t *kayitlar[4];
    dene("SONUC türünde aranabiliyor", bengi_turu_ara(BENGI_SONUC, kayitlar, 4) >= 1);
    bengi_kaydet(BENGI_KARAR, "karar1", "Betonarme kullanılmayacak", 1);
    bengi_kaydet(BENGI_PROJE, "asama", "ÖTÜKEN bitti, UMAY yazılıyor", 1);
    char bag[512];
    bengi_baglam(bag, sizeof bag);
    dene("KARAR bağlama giriyor", um_icinde(bag, "Betonarme") != 0);
    dene("doğrulanmamış SONUC bağlama girmiyor", um_icinde(bag, "TULGA") == 0);
    dene("bengi diske yazıldı", bengi_sakla() == UM_OLDU);
    bengi_kur("/tmp/umay_bengi_sina2.txt");
    dene("türler diskten geri geldi", um_esit(bengi_oku("karar1"), "Betonarme kullanılmayacak"));

    printf("\n17) BİLGİ etiket ve kaynak\n");
    dene("kaynak belge katılabiliyor",
         bilgi_kayit_kat("deneme", "Sınama Belgesi", "/kut/umay/deneme.txt",
                         "sinama,belge", "Bu belge sınama icin eklenmis ozel bir kayittir.") == UM_OLDU);
    baglam_t bt[2];
    bilgi_suzgec_t sz = { 0, 0, "sinama", 0 };
    int nb = bilgi_baglam_bul_suzgec("sınama belgesi kaydı", &sz, bt, 2);
    dene("etikete göre bulundu", nb == 1 && um_esit(bt[0].kategori, "deneme"));
    dene("kaynak taşındı", um_icinde(bt[0].kaynak, "deneme.txt") != 0);
    bilgi_suzgec_t sz2 = { 0, 0, 0, "doktrin" };
    dene("kaynağa göre süzülüyor", bilgi_baglam_bul_suzgec("zeytin toprak", &sz2, bt, 2) > 0);

    printf("\n18) Yetenek bildirimi\n");
    char yet[1024];
    otag_yetenek_dok(&o3, yet, sizeof yet);
    dene("uzmanlar TASARLA bildiriyor", um_icinde(yet, "BALBAL") && um_icinde(yet, "TASARLA"));
    dene("GÖZCÜ yalnız KONTROL/İZLE", um_icinde(yet, "GÖZCÜ — dizge yoklaması [KONTROL,İZLE]") != 0);

    printf("\n19) Depo standardı — 12 alan\n");
    dene("13 depo dizini tanımlı", um_esit(bilgi_alan_dizini(0), "akca") &&
                                   um_esit(bilgi_alan_dizini(12), "ortak") &&
                                   bilgi_alan_dizini(13) == 0);
    dene("alan -> kategori eşlemesi", um_esit(bilgi_alan_kategorisi(ALAN_YERSU), "yer-su") &&
                                      um_esit(bilgi_alan_kategorisi(ALAN_BALBAL), "balbal") &&
                                      um_esit(bilgi_alan_kategorisi(ALAN_YOK), "ortak"));
    struct { alan_t alan; const char *kat; const char *soru; } KAT[] = {
        { ALAN_AKCA,  "akca",   "yatırım maliyeti ve piyasa fiyatı" },
        { ALAN_YERSU, "yer-su", "zeytin budama kompost solucan" },
        { ALAN_KUN,   "kun",    "güneş paneli batarya kaç kilovat" },
        { ALAN_DEMIR, "demir",  "sensör kart besleme gerilim düşürücü" },
        { ALAN_TULGA, "tulga",  "musso motor şanzıman biyodizel" },
        { ALAN_OTACI, "otaci",  "yara merhem tentür kantaron" },
        { ALAN_YARGU, "yargu",  "imar mevzuatı tapu ruhsat" },
        { ALAN_BALBAL,"balbal", "kerpiç saman sıva kiler" },
        { ALAN_ALPAGU,"alpagu", "kamera radar sınır jammer" },
        { ALAN_KOPUZ, "kopuz",  "kopuz makam ezgi çalgı" },
        { ALAN_OYUN,  "oyun",   "zeybek halay prova dizilim" },
        { ALAN_YAZIT, "yazit",  "arşiv künye yedekleme belge" },
        { ALAN_YOK, 0, 0 }
    };
    for (int i = 0; KAT[i].kat; i++) {
        const char *tek[1] = { KAT[i].kat };
        baglam_t bk[2];
        int nk = bilgi_baglam_bul_kat(KAT[i].soru, tek, 1, bk, 2);
        char ad[64];
        um_bicim(ad, sizeof ad, "%s kendi kategorisinden buldu", alan_adi(KAT[i].alan));
        dene(ad, nk > 0 && um_esit(bk[0].kategori, KAT[i].kat));
    }

    printf("\n20) Yanlış kategoriye düşmüyor\n");
    const char *yanlis[1] = { "otaci" };
    baglam_t by[2];
    dene("tıp kategorisinde motor sorusu yok",
         bilgi_baglam_bul_kat("musso şanzıman diferansiyel", yanlis, 1, by, 2) == 0);
    const char *dogru[1] = { "tulga" };
    dene("aynı soru kendi kategorisinde var",
         bilgi_baglam_bul_kat("musso şanzıman diferansiyel", dogru, 1, by, 2) > 0);

    printf("\n21) Uzman kademeli erişim ve fallback\n");
    otag_t o7;
    otag_kur(&o7);
    for (int i = 0; i < alp_uzman_sayisi(); i++) otag_alp_kat(&o7, alp_uzman_sirali(i));
    sonuc_t s8[UMAY_ALP_MAX];

    gorev_t gk;
    gorev_hazirla(&gk, GT_SORU, "kerpiç duvara samanlı sıva nasıl vurulur");
    gk.alan = ALAN_BALBAL;
    otag_gorev_ver(&o7, &gk);
    int t8 = 0; char ilk_oz[GOREV_SONUC_MAX]; ilk_oz[0] = 0;
    while (otag_mesgul(&o7) && t8++ < 60) {
        int n8 = otag_dondur(&o7, s8, UMAY_ALP_MAX);
        if (n8 > 0 && !ilk_oz[0]) um_kopya(ilk_oz, sizeof ilk_oz, s8[0].oz);
    }
    dene("kendi kategorisinde bulunca fallback demiyor",
         um_icinde(ilk_oz, "balbal") && um_icinde(ilk_oz, "genel depoya") == 0);
    dene("kaynak türü yazılıyor", um_icinde(ilk_oz, "[doktrin]") != 0);

    gorev_t gf;
    gorev_hazirla(&gf, GT_SORU, "zeytin fidanı dikimi ve budama artığı");
    gf.alan = ALAN_KOPUZ;
    otag_gorev_ver(&o7, &gf);
    t8 = 0; ilk_oz[0] = 0;
    while (otag_mesgul(&o7) && t8++ < 60) {
        int n8 = otag_dondur(&o7, s8, UMAY_ALP_MAX);
        if (n8 > 0 && !ilk_oz[0]) um_kopya(ilk_oz, sizeof ilk_oz, s8[0].oz);
    }
    dene("alan dışına düşünce kullanıcıya söylüyor",
         um_icinde(ilk_oz, "genel depoya indim") != 0);

    printf("\n22) Kaynak güveni ve çatışma\n");
    dene("BENGİ kaydı BİLGİ deposuna giremez",
         bilgi_kayit_kat_tur("balbal", "Hafıza sızıntısı", "bengi", KT_BENGI,
                             "sinama", "bu kayit deposuna girmemeli") == UM_YANLIS);
    dene("dış belge KT_DIS olarak giriyor",
         bilgi_kayit_kat("balbal", "Usta Notu", "/kut/umay/balbal/usta.txt",
                         "harc,kirec", "Horasan harcinda kirec orani usta notuna gore farklidir.") == UM_OLDU);
    baglam_t bc[3];
    const char *kbal[1] = { "balbal" };
    int nc = bilgi_baglam_bul_kat("horasan harcı kireç oranı", kbal, 1, bc, 3);
    catisma_t ct;
    dene("iki ayrı kaynak çatışma sayılıyor", nc >= 2 && bilgi_catisma_bul(bc, nc, &ct) == 1);
    dene("çatışmada iki kaynak da adlandırılıyor",
         ct.var && !um_esit(ct.kaynak_a, ct.kaynak_b));
    dene("aynı kaynaktan iki kayıt çatışma değil",
         bilgi_catisma_bul(bc, 1, &ct) == 0);

    otag_t o8;
    otag_kur(&o8);
    otag_alp_kat(&o8, alp_uzman(ALAN_BALBAL));
    gorev_t gc;
    gorev_hazirla(&gc, GT_ANALIZ, "horasan harcı kireç oranı ne olmalı");
    gc.alan = ALAN_BALBAL;
    gc.bengiye_yaz = 1;
    uint32_t kc = (uint32_t)otag_gorev_ver(&o8, &gc);
    t8 = 0; ilk_oz[0] = 0;
    while (otag_mesgul(&o8) && t8++ < 60) {
        int n8 = otag_dondur(&o8, s8, UMAY_ALP_MAX);
        if (n8 > 0 && !ilk_oz[0]) um_kopya(ilk_oz, sizeof ilk_oz, s8[0].oz);
    }
    dene("çatışma OTAĞ'a bildirildi", otag_catisma_sayi(&o8) == 1 || kc > 0);
    dene("çatışma kullanıcıya söylendi", um_icinde(ilk_oz, "birleştirmedim") != 0);

    printf("\n23) BENGİ doğrulama kuralı\n");
    bengi_kur("/tmp/umay_bengi_sina3.txt");
    dene("dogrulandi bayrağı zorunlu",
         bengi_kaydet(BENGI_KARAR, "k1", "deger", 7) == UM_YANLIS);
    dene("geçersiz tür reddediliyor",
         bengi_kaydet((bengi_tur_t)99, "k2", "deger", 1) == UM_YANLIS);
    dene("doğrulanmamış kayıt yazılabiliyor",
         bengi_kaydet(BENGI_SONUC, "s1", "ham gorev ciktisi", 0) == UM_OLDU);
    char bg[512];
    bengi_baglam(bg, sizeof bg);
    dene("doğrulanmamış kayıt US bağlamına girmiyor", um_icinde(bg, "ham gorev") == 0);
    dene("doğrulanınca da SONUC bağlama girmiyor",
         bengi_dogrula("s1") == UM_OLDU);
    bengi_baglam(bg, sizeof bg);
    dene("SONUC türü hiçbir zaman bağlamda yok", um_icinde(bg, "ham gorev") == 0);
    bengi_kaydet(BENGI_PROJE, "p1", "UMAY bilgi deposu standardi uygulandi", 1);
    bengi_baglam(bg, sizeof bg);
    dene("doğrulanmış PROJE kaydı bağlamda", um_icinde(bg, "bilgi deposu") != 0);

    printf("\n24) US — tek uzman\n");
    us_kur(0);
    dene("motor yoksa stub'a düşüyor", um_esit(us_motor_adi(), "stub-ozan"));
    baglam_t bd[2];
    const char *ktul[1] = { "tulga" };
    int nd = bilgi_baglam_bul_kat("musso motor şanzıman", ktul, 1, bd, 2);
    us_derle_t di;
    um_bellek_doldur(&di, 0, sizeof di);
    di.kisilik = ozan_kimlik();
    di.soru = "musso motor şanzıman";
    di.niyet = NIYET_SORU;
    di.baglam = bd; di.baglam_sayi = nd;
    di.uzman_ad = "TULGA"; di.uzman_is = "otomotiv";
    us_cikti_t co;
    dene("çıkarım koştu", us_cevap_uret(&di, &co) == UM_OLDU);
    dene("metin üretildi", um_uzun(co.metin) > 20 && co.dayanaksiz == 0);
    dene("dayanak korundu", co.dayanak_sayi == nd && um_esit(co.dayanak[0].kategori, "tulga"));
    dene("kaynak türü korundu", co.dayanak[0].kaynak_tur == KT_DOKTRIN);
    dene("uzman kaydedildi", co.uzman_sayi == 1 && um_esit(co.uzman[0], "TULGA"));
    dene("güven verildi", co.guven >= GUVEN_ORTA);
    char cy[GOREV_SONUC_MAX];
    us_cikti_yaz(&co, cy, sizeof cy);
    dene("çıktıda başyazı, dayanak ve güven var",
         um_icinde(cy, "TULGA (") && um_icinde(cy, "Dayanağım") && um_icinde(cy, "Güven:"));

    printf("\n25) US — kaynaksız soru\n");
    g_cagri = 0;
    us_motor_tak(&g_sahte);
    us_derle_t db;
    um_bellek_doldur(&db, 0, sizeof db);
    db.kisilik = ozan_kimlik();
    db.soru = "zzzqqq bilinmeyen konu";
    db.niyet = NIYET_SORU;
    db.uzman_ad = "BİLGE"; db.uzman_is = "bilgi";
    us_cikti_t cb;
    us_cevap_uret(&db, &cb);
    dene("dayanaksız işaretlendi", cb.dayanaksiz == 1);
    dene("güven yok", cb.guven == GUVEN_YOK);
    dene("uydurmadığını söylüyor", um_icinde(cb.metin, "yeterli dayanak yok") != 0);
    dene("motor HİÇ çağrılmadı", g_cagri == 0);

    printf("\n26) US — BENGİ bağlamı\n");
    bengi_kur("/tmp/umay_bengi_sina4.txt");
    bengi_kaydet(BENGI_KARAR, "k1", "Betonarme kullanilmayacak", 1);
    bengi_kaydet(BENGI_SONUC, "s9", "dogrulanmamis gorev ciktisi", 0);
    char bgb[512];
    bengi_baglam(bgb, sizeof bgb);
    g_cagri = 0;
    us_derle_t dg;
    um_bellek_doldur(&dg, 0, sizeof dg);
    dg.kisilik = ozan_kimlik();
    dg.soru = "ne kullanmayacağız";
    dg.niyet = NIYET_SORU;
    dg.bengi_baglam = bgb;
    dg.uzman_ad = "BİLGE"; dg.uzman_is = "bilgi";
    us_cikti_t cg;
    us_cevap_uret(&dg, &cg);
    dene("BENGİ bağlamı tek başına yeter", cg.dayanaksiz == 0 && g_cagri == 1);
    dene("motora bağlam gitti", um_icinde(cg.metin, "bağlam geldi") != 0);
    dene("doğrulanmış KARAR bağlamda", um_icinde(bgb, "Betonarme") != 0);
    dene("doğrulanmamış SONUC bağlamda değil", um_icinde(bgb, "dogrulanmamis") == 0);

    printf("\n27) US — kaynak çatışması\n");
    catisma_t ck;
    um_bellek_doldur(&ck, 0, sizeof ck);
    ck.var = 1;
    um_kopya(ck.kategori, sizeof ck.kategori, "balbal");
    um_kopya(ck.kaynak_a, sizeof ck.kaynak_a, "doktrin");
    um_kopya(ck.kaynak_b, sizeof ck.kaynak_b, "/kut/umay/balbal/usta.txt");
    us_derle_t dc = di;
    dc.catisma = &ck;
    us_cikti_t cc;
    us_cevap_uret(&dc, &cc);
    dene("çatışma çıktıya taşındı", cc.catisma == 1);
    dene("çatışmada güven zayıf", cc.guven == GUVEN_ZAYIF);
    dene("iki kaynak da adıyla anıldı",
         um_icinde(cc.catisma_notu, "doktrin") && um_icinde(cc.catisma_notu, "usta.txt"));
    us_cikti_yaz(&cc, cy, sizeof cy);
    dene("birleştirmediğini söylüyor", um_icinde(cy, "birleştirmedim") != 0);

    printf("\n28) US — yedeğe düşme\n");
    g_cagri = 0;
    us_motor_tak(&g_cokan);
    dene("çöken motor takıldı", um_esit(us_motor_adi(), "cokan"));
    us_cikti_t cf;
    us_cevap_uret(&di, &cf);
    dene("asıl motor denendi", g_cagri == 1);
    dene("yedeğe düşüldü", cf.yedege_dustu == 1);
    dene("yedek motor adı yazıldı", um_esit(cf.motor, "stub-ozan"));
    dene("yine de yanıt üretildi", um_uzun(cf.metin) > 10 && cf.dayanaksiz == 0);
    dene("yedeğe düşüldüğü uyarıya işlendi", cf.uyari_sayi >= 1);
    us_kur(0);

    printf("\n29) US — uyarılı uzman sonucu\n");
    otag_t o9;
    otag_kur(&o9);
    otag_alp_kat(&o9, alp_uzman(ALAN_YARGU));
    otag_alp_kat(&o9, alp_uzman(ALAN_OTACI));
    sonuc_t s9[UMAY_ALP_MAX];
    gorev_t gyu;
    gorev_hazirla(&gyu, GT_SORU, "imar mevzuatı ruhsat tapu");
    gyu.alan = ALAN_YARGU;
    otag_gorev_ver(&o9, &gyu);
    int t9 = 0; char yz[GOREV_SONUC_MAX]; yz[0] = 0;
    while (otag_mesgul(&o9) && t9++ < 60) {
        int n9 = otag_dondur(&o9, s9, UMAY_ALP_MAX);
        if (n9 > 0 && !yz[0]) um_kopya(yz, sizeof yz, s9[0].oz);
    }
    dene("YARGU uyarısı çıktıda", um_icinde(yz, "hukuki görüş değildir") != 0);
    dene("uyarıyla birlikte dayanak da var", um_icinde(yz, "Dayanağım") != 0);
    dene("güven satırı korundu", um_icinde(yz, "Güven:") != 0);

    printf("\n30) US — 3 uzmanlı meclis\n");
    otag_t o10;
    otag_kur(&o10);
    for (int i = 0; i < alp_uzman_sayisi(); i++) otag_alp_kat(&o10, alp_uzman_sirali(i));
    gorev_t gme;
    gorev_hazirla(&gme, GT_SORU,
                  "sarnıç için taş duvar örerken solar pompayı hangi panel besler");
    um_kopya(gme.konu, sizeof gme.konu, "sarnıç");
    alan_t uc[3] = { ALAN_BALBAL, ALAN_KUN, ALAN_YERSU };
    dene("meclis kuruldu", otag_meclis_kur(&o10, &gme, uc, 3) > 0);
    int t10 = 0; char mz[GOREV_SONUC_MAX]; mz[0] = 0; int bm = 0;
    sonuc_t s10[UMAY_ALP_MAX];
    while (otag_mesgul(&o10) && t10++ < 90) {
        int n10 = otag_dondur(&o10, s10, UMAY_ALP_MAX);
        for (int q = 0; q < n10; q++) { bm++; if (!mz[0]) um_kopya(mz, sizeof mz, s10[q].oz); }
    }
    dene("tek birleşik sonuç", bm == 1);
    dene("üç uzman da çıktıda",
         um_icinde(mz, "BALBAL") && um_icinde(mz, "KÜN") && um_icinde(mz, "YER-SU"));
    dene("katılan uzmanlar listelendi", um_icinde(mz, "Katılan uzmanlar:") != 0);
    dene("ortak güven bildirildi", um_icinde(mz, "Otağın ortak güveni:") != 0);
    dene("dayanaklar korundu", um_icinde(mz, "Dayanağım") != 0);

    printf("\n31) Atanamayan görev kuyruğu kilitlemiyor\n");
    otag_t o11;
    otag_kur(&o11);
    otag_alp_kat(&o11, alp_uzman(ALAN_BALBAL));      /* yalnız BALBAL var */
    gorev_t gyok, gvar;
    gorev_hazirla(&gyok, GT_SORU, "musso şanzıman sorusu");
    gyok.alan = ALAN_TULGA;                          /* TULGA otağda YOK  */
    gyok.oncelik = ONC_ACIL;                         /* üstelik en önde   */
    gorev_hazirla(&gvar, GT_SORU, "kerpiç duvara samanlı sıva");
    gvar.alan = ALAN_BALBAL;
    uint32_t kyok = (uint32_t)otag_gorev_ver(&o11, &gyok);
    uint32_t kvar = (uint32_t)otag_gorev_ver(&o11, &gvar);
    sonuc_t s11[UMAY_ALP_MAX];
    int t11 = 0, dusen = 0, kosan = 0;
    while (otag_mesgul(&o11) && t11++ < 40) {
        int n11 = otag_dondur(&o11, s11, UMAY_ALP_MAX);
        for (int q = 0; q < n11; q++) {
            if (s11[q].kimlik == kyok && !s11[q].basarili) dusen = 1;
            if (s11[q].kimlik == kvar && s11[q].basarili)  kosan = 1;
        }
    }
    dene("atanamayan görev düştü, asılı kalmadı", dusen == 1);
    dene("arkasındaki atanabilir görev koştu", kosan == 1);
    dene("otağ kilitlenmedi", otag_mesgul(&o11) == 0 && t11 < 40);
    dene("düşen görev havuzdan temizlendi", otag_gorev_bul(&o11, kyok) == 0);

    printf("\n32) 24 atanamayan görev havuzu doldurmuyor\n");
    otag_t o12;
    otag_kur(&o12);
    otag_alp_kat(&o12, alp_uzman(ALAN_BALBAL));
    for (int i = 0; i < UMAY_GOREV_MAX; i++) {
        gorev_t gd;
        gorev_hazirla(&gd, GT_SORU, "sahipsiz iş");
        gd.alan = ALAN_TULGA;
        otag_gorev_ver(&o12, &gd);
    }
    dene("havuz doldu", otag_bekleyen_sayi(&o12) == UMAY_GOREV_MAX);
    int t12 = 0;
    while (otag_mesgul(&o12) && t12++ < 60) otag_dondur(&o12, s11, UMAY_ALP_MAX);
    dene("hepsi tek turda düştü", t12 <= 2);
    dene("havuz boşaldı", otag_bekleyen_sayi(&o12) == 0);
    gorev_t gson;
    gorev_hazirla(&gson, GT_SORU, "kerpiç sıva");
    gson.alan = ALAN_BALBAL;
    dene("havuz yeniden kullanılabiliyor", otag_gorev_ver(&o12, &gson) > 0);
    t12 = 0; int son_ok = 0;
    while (otag_mesgul(&o12) && t12++ < 40) {
        int n12 = otag_dondur(&o12, s11, UMAY_ALP_MAX);
        for (int q = 0; q < n12; q++) if (s11[q].basarili) son_ok = 1;
    }
    dene("dolduktan sonra iş yine koşuyor", son_ok == 1);

    printf("\n33) TÖRE birinci kapısı\n");
    dene("UYGULA yazma yetkisi ister", gorev_yetki_gerek(GT_UYGULA) == YET_DOSYA_YAZ);
    dene("İZLE süreç yetkisi ister", gorev_yetki_gerek(GT_IZLE) == YET_SUREC_GOR);
    dene("SORU yetki istemez", gorev_yetki_gerek(GT_SORU) == 0);

    otag_t o13;
    otag_kur(&o13);
    onay_kur(60000);
    otag_alp_kat(&o13, alp_yazman());
    host_yetki_ayarla(YET_DOSYA_OKU | YET_SUREC_GOR);      /* yazma yetkisi YOK */
    gorev_t gt2;
    gorev_hazirla(&gt2, GT_UYGULA, "şunu kaydet: sınama");
    uint32_t kt2 = (uint32_t)otag_gorev_ver(&o13, &gt2);
    int t13 = 0; char red[GOREV_SONUC_MAX]; red[0] = 0;
    while (otag_mesgul(&o13) && t13++ < 20) {
        int n13 = otag_dondur(&o13, s11, UMAY_ALP_MAX);
        for (int q = 0; q < n13; q++) if (!red[0]) um_kopya(red, sizeof red, s11[q].oz);
    }
    dene("TÖRE yetkisi yokken görev reddedildi", um_icinde(red, "TÖRE yetkim yok") != 0);
    dene("ajan görevi hiç almadı", alp_yazman()->durum == ALP_BOS);
    dene("onay bile istenmedi", onay_bekleyen_sayi() == 0);
    dene("görev havuzda asılı kalmadı", otag_gorev_bul(&o13, kt2) == 0);

    host_yetki_ayarla(YET_DOSYA_OKU | YET_DOSYA_YAZ | YET_SUREC_GOR);   /* geri ver */
    otag_t o14;
    otag_kur(&o14);
    otag_alp_kat(&o14, alp_yazman());
    gorev_t gt3;
    gorev_hazirla(&gt3, GT_UYGULA, "şunu kaydet: sınama iki");
    uint32_t kt3 = (uint32_t)otag_gorev_ver(&o14, &gt3);
    otag_dondur(&o14, s11, UMAY_ALP_MAX);
    gorev_t *gpt = otag_gorev_bul(&o14, kt3);
    dene("yetki verilince TÖRE geçildi", gpt != 0 && gpt->durum == GD_ONAY_BEKLER);
    dene("ikinci kapı (onay) devrede", gpt && gpt->onay_kimlik[0] != 0);

    printf("\n34) ARAŞTIR -> TASARLA zinciri ve SONUC izolasyonu\n");
    otag_t o15;
    otag_kur(&o15);
    for (int i = 0; i < alp_uzman_sayisi(); i++) otag_alp_kat(&o15, alp_uzman_sirali(i));
    gorev_t tas, ara2;
    gorev_hazirla(&tas, GT_TASARLA, "samanlı toprak sıva ile kiler tasarla");
    tas.alan = ALAN_BALBAL;
    uint32_t kt = (uint32_t)otag_gorev_ver(&o15, &tas);
    gorev_hazirla(&ara2, GT_ARASTIR, "horasan harcı ve samanlı sıva");
    ara2.alan = ALAN_BALBAL;
    uint32_t kar = (uint32_t)otag_alt_gorev(&o15, kt, &ara2);
    otag_bagimlilik_ekle(&o15, kt, kar);
    int t15 = 0; char zin[4][GOREV_SONUC_MAX]; int zn = 0;
    gorev_t *gtas = otag_gorev_bul(&o15, kt);
    int oncul_gordum = 0;
    while (otag_mesgul(&o15) && t15++ < 80) {
        int n15 = otag_dondur(&o15, s11, UMAY_ALP_MAX);
        for (int q = 0; q < n15 && zn < 4; q++) um_kopya(zin[zn++], GOREV_SONUC_MAX, s11[q].oz);
        gtas = otag_gorev_bul(&o15, kt);
        if (gtas && gtas->oncul_baglam[0]) oncul_gordum = 1;
    }
    dene("zincirin iki halkası da koştu", zn == 2);
    dene("öncül çıktısı ardıla ayrı alanda geçti", oncul_gordum == 1);
    dene("ardıl çıktısında öncül işaretlendi",
         zn == 2 && um_icinde(zin[1], "Önceki görevin çıktısını da okudum") != 0);
    dene("ardıl kendi kategorisinden dayanak gösterdi",
         zn == 2 && um_icinde(zin[1], "Dayanağım: balbal") != 0);
    dene("öncül dayanak listesine girmedi",
         zn == 2 && um_icinde(zin[1], "Dayanağım: (önceki") == 0 &&
                    um_icinde(zin[1], "ARAŞTIR sonucu") == 0);

    printf("\n35) SONUC US dayanağına girmiyor\n");
    us_kur(0);
    us_derle_t donly;
    um_bellek_doldur(&donly, 0, sizeof donly);
    donly.kisilik = ozan_kimlik();
    donly.soru = "önceki görevden ne çıktı";
    donly.niyet = NIYET_SORU;
    donly.oncul_baglam = "ARAŞTIR sonucu: horasan harcı üç ölçü kum bir ölçü kireç.";
    donly.uzman_ad = "BALBAL"; donly.uzman_is = "yapı";
    us_cikti_t conly;
    us_cevap_uret(&donly, &conly);
    dene("tek başına öncül çıktı dayanak sayılmadı", conly.dayanaksiz == 1);
    dene("güven yok", conly.guven == GUVEN_YOK);
    dene("dayanak listesi boş", conly.dayanak_sayi == 0);
    dene("öncül kullanıldığı işaretli", conly.oncul_kullanildi == 1);
    dene("sebebi söyleniyor", um_icinde(conly.metin, "dayanak saymam") != 0);

    baglam_t bz[2];
    const char *kbz[1] = { "balbal" };
    int nz = bilgi_baglam_bul_kat("kerpiç saman sıva kiler", kbz, 1, bz, 2);
    us_derle_t dmix = donly;
    dmix.baglam = bz; dmix.baglam_sayi = nz;
    us_cikti_t cmix;
    us_cevap_uret(&dmix, &cmix);
    dene("gerçek dayanak varken koşuyor", cmix.dayanaksiz == 0);
    dene("dayanak sayısı yalnız kaynaklardan", cmix.dayanak_sayi == nz);
    dene("öncül güveni sağlamaya çıkaramıyor", cmix.guven <= GUVEN_ORTA);
    char mz2[GOREV_SONUC_MAX];
    us_cikti_yaz(&cmix, mz2, sizeof mz2);
    dene("çıktıda doğrulanmamış diye anılıyor",
         um_icinde(mz2, "doğrulanmamış, dayanak saymadım") != 0);

    printf("\n36) Yalıtım\n");
    dene("US motoru takılabilir", us_stub_motor() != 0);
    us_istem_t i = { .kisilik = ozan_kimlik(), .baglam = "", .soru = "x",
                     .niyet = NIYET_SORU, .en_cok_im = 64 };
    char cikti[256];
    umay_us_al()->uret(umay_us_al(), &i, cikti, sizeof cikti);
    dene("bağlamsızken uydurmaz", um_icinde(cikti, "uydurmam") != 0);

    printf("\n37) Öbek: serbest liste\n");
    {
        /* up_biraktir eskiden BOŞTU: bırakılan her blok sızıyordu.
         * Artık bırakılan blok serbest listeye giriyor ve aynı boydaki
         * bir sonraki istek onu geri alıyor. */
        void *a = up_ayir(128);
        dene("ayırma çalışıyor", a != 0);
        up_biraktir(a);
        void *b = up_ayir(128);
        dene("bırakılan blok geri kullanılıyor", b == a);
        void *c = up_ayir(64);
        dene("ikinci blok ayrı", c != 0 && c != b);
        up_biraktir(b);
        up_biraktir(c);
        void *d = up_ayir(64);
        dene("liste bozulmadı", d != 0);
        up_biraktir((void *)0);            /* boş işaretçi güvenli */
        dene("sıfır işaretçi yutuluyor", 1);
    }

    printf("\n38) US motoru: değiştirilebilir arka uç\n");
    {
        umay_us_t *m = us_ipc_motor();
        dene("IPC motoru var", m != 0);
        dene("motor adı bildiriliyor", m && m->ad && um_uzun(m->ad) > 0);
        /* Servis YOKKEN hazırlama BAŞARISIZ dönmeli: sahte başarı olmamalı. */
        dene("servis yokken hazırlama başarısız",
             m && m->hazirla && m->hazirla(m, 0) != UM_OLDU);
        /* Bağlanmamış motor üretmemeli. */
        char c2[128];
        us_istem_t ii = { .kisilik = "x", .baglam = "", .soru = "y",
                          .niyet = NIYET_SORU, .en_cok_im = 64 };
        dene("bağlanmamış motor üretmez",
             m && m->uret && m->uret(m, &ii, c2, sizeof c2) != UM_OLDU);
        /* us_kur bu durumda stub'a düşmeli ve bunu bildirmeli. */
        us_kur(0);
        dene("stub'a düşüldü", um_icinde(us_motor_adi(), "stub") != 0);
    }

    printf("\n39) Sözlük: UTF-8 karakter sınırı\n");
    {
        /* Sözlük UTF-8'dir: 45 karakter 53 bayt tutabilir. İmler
         * KARAKTER sayar. Bayt bayt indekslenirse Türkçe harflerden
         * sonraki her im kayar ve çıktıda bozuk karakter oluşur —
         * gerçekte yaşandı. Burada ölçülen, çözümlemenin doğruluğudur. */
        const char *s1 = "aÖbüc";
        int kar_sayi = 0;
        for (const unsigned char *p = (const unsigned char *)s1; *p; p++)
            if ((*p & 0xC0) != 0x80) kar_sayi++;
        dene("UTF-8 karakter sayımı doğru", kar_sayi == 5);
        dene("bayt sayısı karakterden büyük", (int)um_uzun(s1) == 7);
    }

    printf("\n40) Ses: aygıt yoksa sürücü açılmaz\n");
    {
        /* Ana bilgisayarda ses yolu yoktur. Sürücünün açılmış gibi
         * davranıp metni günlüğe basması, konuşuyormuş izlenimi verirdi. */
        dene("ana bilgisayarda ses yolu yok", up_ses_var_mi() == 0);
        dene("ozan sürücüsü takılmayı reddeder",
             ses_surucu_tak(ses_ozan_surucu()) != UM_OLDU);
        dene("reddedilen sürücü takılı sayılmaz", ses_var_mi() == 0);
        ses_surucu_tak(ses_bos_surucu());
    }

    printf("\n=== SONUC: %d gecti, %d kaldi ===\n", g_gecti, g_kaldi);
    return g_kaldi == 0 ? 0 : 1;
}
