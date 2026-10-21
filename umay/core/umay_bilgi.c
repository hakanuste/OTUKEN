/* BİLGİ — kütüphane indeksi + bağlam bulma.
 * V9.2 zeka.c'deki kelime çıkarma / kesişim skoru / alıntı pencereleme
 * algoritması korunmuştur; öbek yerine durağan dizi kullanılır. */
#include "umay_bilgi.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"

typedef struct {
    char kategori[32];
    char baslik[96];
    char kaynak[96];
    kaynak_tur_t kaynak_tur;
    char etiket[BILGI_ETIKET_MAX][24];
    int  etiket_sayi;
    char icerik[BILGI_ICERIK_MAX];
    char kelime[BILGI_KELIME_MAX][24];
    int  kelime_sayisi;
    int  dolu;
} kayit_t;

static kayit_t g_indeks[BILGI_KAYIT_MAX];
static int     g_sayi;

static const char *DURAK[] = {
    "ve","ile","için","bir","bu","da","de","mi","ne","ki","ya","çok","daha",
    "ama","gibi","olarak","göre","kadar","the","and","for","that","with", 0
};

static int durak_mi(const char *k) {
    if (um_uzun(k) < 3) return 1;
    for (int i = 0; DURAK[i]; i++) if (um_esit(k, DURAK[i])) return 1;
    return 0;
}

/* metinden en çok 'en_cok' benzersiz, küçük harfli kelime çıkarır */
static int kelime_ayikla(const char *metin, char cikan[][24], int en_cok) {
    int n = 0;
    const char *p = metin;
    while (*p && n < en_cok) {
        uint32_t b; uint32_t kn = um_utf8_oku(p, &b);
        if (!um_kelime_karakteri(kn)) { p += b; continue; }
        char kelime[24]; uint32_t o = 0;
        while (*p) {
            kn = um_utf8_oku(p, &b);
            if (!um_kelime_karakteri(kn)) break;
            uint32_t k2 = um_kucuk(kn);
            char yaz[5]; uint32_t yb = um_utf8_yaz(k2, yaz);
            if (o + yb + 1 < sizeof kelime) { um_bellek_kopya(kelime + o, yaz, yb); o += yb; }
            p += b;
        }
        kelime[o] = 0;
        if (durak_mi(kelime)) continue;
        int var = 0;
        for (int i = 0; i < n; i++) if (um_esit(cikan[i], kelime)) { var = 1; break; }
        if (!var) um_kopya(cikan[n++], 24, kelime);
    }
    return n;
}

/* "a,b,c" biçimli etiket dizesini ayırır */
static void etiket_ayikla(kayit_t *k, const char *etiketler) {
    k->etiket_sayi = 0;
    if (!etiketler) return;
    const char *p = etiketler;
    while (*p && k->etiket_sayi < BILGI_ETIKET_MAX) {
        char e[24]; uint32_t o = 0;
        while (*p == ' ' || *p == ',') p++;
        while (*p && *p != ',' && o + 1 < sizeof e) e[o++] = *p++;
        e[o] = 0;
        if (o) um_kopya(k->etiket[k->etiket_sayi++], 24, e);
    }
}

static int kayit_ekle_tam(const char *kategori, const char *baslik, const char *kaynak,
                          kaynak_tur_t kt, const char *etiketler, const char *icerik) {
    /* BENGİ kaydı belge deposuna giremez: hafıza ile kaynak karışmasın. */
    if (kt == KT_BENGI) return UM_YANLIS;
    if (g_sayi >= BILGI_KAYIT_MAX) return UM_DOLU;
    kayit_t *k = &g_indeks[g_sayi];
    um_kopya(k->kategori, sizeof k->kategori, kategori);
    um_kopya(k->baslik,   sizeof k->baslik,   baslik);
    um_kopya(k->kaynak,   sizeof k->kaynak,   kaynak ? kaynak : "doktrin");
    k->kaynak_tur = kt;
    etiket_ayikla(k, etiketler);
    um_kopya(k->icerik,   sizeof k->icerik,   icerik);
    k->kelime_sayisi = kelime_ayikla(k->icerik, k->kelime, BILGI_KELIME_MAX);
    k->dolu = 1;
    g_sayi++;
    return UM_OLDU;
}

int bilgi_kayit_kat_tur(const char *kategori, const char *baslik, const char *kaynak,
                        kaynak_tur_t kt, const char *etiketler, const char *icerik) {
    return kayit_ekle_tam(kategori, baslik, kaynak, kt, etiketler, icerik);
}

int bilgi_kayit_kat(const char *kategori, const char *baslik, const char *kaynak,
                    const char *etiketler, const char *icerik) {
    return kayit_ekle_tam(kategori, baslik, kaynak, KT_DIS, etiketler, icerik);
}

/* ---- depo standardı: 12 uzman alanı + ortak havuz ---- */
static const char *DEPO_DIZIN[BILGI_ALAN_SAYI] = {
    "akca", "yer-su", "kun", "demir", "tulga", "otaci",
    "yargu", "balbal", "alpagu", "kopuz", "oyun", "yazit", "ortak"
};

const char *bilgi_alan_dizini(int i) {
    return (i >= 0 && i < BILGI_ALAN_SAYI) ? DEPO_DIZIN[i] : 0;
}

const char *bilgi_alan_kategorisi(alan_t a) {
    switch (a) {
    case ALAN_AKCA:  return "akca";
    case ALAN_YERSU: return "yer-su";
    case ALAN_KUN:   return "kun";
    case ALAN_DEMIR: return "demir";
    case ALAN_TULGA: return "tulga";
    case ALAN_OTACI: return "otaci";
    case ALAN_YARGU: return "yargu";
    case ALAN_BALBAL:return "balbal";
    case ALAN_ALPAGU:return "alpagu";
    case ALAN_KOPUZ: return "kopuz";
    case ALAN_OYUN:  return "oyun";
    case ALAN_YAZIT: return "yazit";
    default:         return "ortak";
    }
}

int bilgi_kategori_sayisi(const char *kategori) {
    int n = 0;
    for (int i = 0; i < g_sayi; i++)
        if (g_indeks[i].dolu && um_esit(g_indeks[i].kategori, kategori)) n++;
    return n;
}

int bilgi_dizin_kur(const char *kok) {
    if (!kok || !up_yetkim_var_mi(YET_DOSYA_YAZ)) return UM_YETKISIZ;
    int n = 0;
    char yol[UMAY_YOL_MAX];
    up_dizin_yarat(kok);
    for (int i = 0; i < BILGI_ALAN_SAYI; i++) {
        um_bicim(yol, sizeof yol, "%s/%s", kok, DEPO_DIZIN[i]);
        if (up_var_mi(yol) || up_dizin_yarat(yol) == UM_OLDU) n++;
    }
    gun_yaz("Bilgi deposu: %d/%d alan dizini hazır.", n, BILGI_ALAN_SAYI);
    return n;
}

int bilgi_sayi(void) { return g_sayi; }

/* ---- Gömülü doktrin: V15 proje veri tabanı özü ----
 * Kütüphane diskte bulunmasa bile UMAY temel bilgisini taşır. */
static const struct { const char *kat; const char *bas; const char *etiket; const char *ic; } DOKTRIN[] = {
{"ortak","Kullanıcı ve görüş","temel,vizyon",
 "Kullanıcı Hakan Üste, dans eğitmeni. Yaşam görüşü: şebekeden bağımsız (off-grid), kendine yeten, kalıcı, sürdürülebilir, sıfır atıklı, ekolojik yaşam. Yaklaşık 60 yaşında modifiye arazi aracıyla Türkistan ve Orta Asya ata topraklarına yolculuk tasarısı var. Dilde Öz Türkçe sözcükler yeğlenir."},
{"yer-su","Arsa ve permakültür","permakultur,arsa",
 "Aydın Koçarlı Cincin Köyü, 1509 metrekare zeytinlik. Girdi çıktı döngüleri kapatılır. Bölgelendirme Zone 1-5, rüzgar kıranlar, güneş kapanları, çok katmanlı gıda ormanı. Zeytin budama atığından biyokömür biochar üretilir. Ata tohumları vakumlu silika jelli cam kavanozlarda yer altı kilerinde saklanır. Kimyasal girdi kullanılmaz."},
{"balbal","Doğal yapı ve yer altı kileri","yapi,kiler",
 "Dancer's Sanctuary yapımında betonarme ve çimento kesinlikle kullanılmaz. Yerel doğal taş, ahşap, killi toprak ve saman kullanılır. İç duvarlarda nem dengeleyici samanlı toprak sıva uygulanır. Yer altı kileri Horasan harcı bağlayıcılı olup yıl boyu 12-15 derece sabit sıcaklık ve pasif havalandırma bacalarıyla nem dengesi sağlar."},
{"alpagu","Güvenlik ve siber savunma","cevre,siber",
 "Arazi çevresinde off-grid çalışan düşük güç tüketimli gece görüşlü IP kameralar ve Orange Pi tabanlı sınır mikro radar sensörleri bulunur. Daewoo Musso üstünde 360 derece taktik kamera seti ve yakın mesafe engel radarı vardır. Sunucular ve IoT aygıtları internetten bağımsız şifreli yerel ağda çalışır. Kritik veri için air-gapped yedekleme ünitesi kurulur, Wi-Fi jammer saldırısına karşı donanımsal yalıtım uygulanır."},
{"otaci","Acil tıp ve doğal apotek","apotek,ilkyardim",
 "Hastane devre dışıyken ileri ilk yardım, travma müdahalesi, dikiş atma, yara ve kırık dengeleme bilgisi yerel sunucuda tutulur. Serada yetişen kantaron, nane, lavanta, biberiye ve kekikten esansiyel yağ, merhem ve tentür çıkarılır. Koloidal gümüş suyu üretimi ve doğal antiseptik yordamları uygulanır."},
{"ortak","Haritacılık ve pusula","harita,yol",
 "Türkiye için köy yolları, kurumuş dere yatakları, orman patikaları ve topoğrafya katmanlarını içeren en yüksek çözünürlüklü vektörel harita verisi obf ve mbtiles biçiminde yerel diskte tutulur. Gürcistan, Rusya, Kazakistan, Özbekistan, Kırgızistan, Türkmenistan ve Moğolistan için İpek Yolu dağ geçitleri, çöl patikaları ve sınır kapısı koordinatlarını içeren çevrim dışı topoğrafik harita seti bulunur."},
{"ortak","Su yönetimi ve yangın savunması","su,sarnic,yangin",
 "Yağmur suyu hasadı ve yeraltı suyu için Horasan harcı sıvalı toprak altı kadim sarnıç yapılır. Arazi sınırlarına yüksek basınçlı geniş açılı yangın söndürme yağmurlama sistemi kurulur, sarnıçtaki yedek sudan mekanik ya da solar yüksek debili pompayla beslenir."},
{"yer-su","Gölet ekolojisi ve biyolojik arıtma","golet,aritma",
 "Klor ve kimyasal kullanılmadan bitkiler ve yararlı bakterilerle temizlenen yüzme havuzu ve ördek kaz göleti kurulur. Devirdaim pompasıyla su mercimeği, sazlık ve volkanik çakıldan oluşan yenilenme alanından geçirilir. Gambusia balığıyla sivrisinek larvası denetlenir. Gri ve siyah su beş bölümlü hatta katı çökeltme, volkanik taş süzme, sazlık yapay sulak alan, şelaleli oksijenlendirme ve son depolama ile geri kazanılır."},
{"yer-su","Sera, kümes ve biyolojik duvar","kumes,bitki",
 "20 adet Australorp tavuğu akrep ve kene avcısı olarak, 2 veya 3 kaz doğal alarm ve sürüngen itici olarak beslenir. Sınırlarda kadife çiçeği, sarımsak, nane ve pelin otundan keskin kokulu doğal itici duvar oluşturulur."},
{"yer-su","Toprak yenilenmesi","kompost,solucan",
 "Kırmızı Kaliforniya solucanlarıyla mutfak ve kümes atıkları vermikompost olarak işlenir. Dal ve yapraklar sıcak ve soğuk kompostla humuslu toprağa dönüştürülür."},
{"kun","Enerji ve altyapı","solar,batarya",
 "Yaklaşık 6 kW şebekeden bağımsız güneş dizgesi kurulur. Paneller evin çatısına değil, ayrı kamelya ya da garaj yapısının çatısına konur. 3D yazıcının yüksek akım çeken ısıtıcı tablası LiFePO4 bataryaları yormamak için yalnız güneşin zirvede olduğu saatlerde çalıştırılır."},
{"tulga","Mekanik ve otomotiv","motor,sanziman",
 "Proje aracı 1998 model Daewoo Musso. Motor Mercedes-Benz M104 3.2 litre sıralı altı silindir HFM enjeksiyon. Şanzıman Mercedes lisanslı dört ileri otomatik 722.4 veya 722.5 varyantı. Arazi şanzımanı BorgWarner part-time 4WD. BTR şanzımanlar projeden tamamen çıkarılmıştır. Zeytinyağı dip tortusu ve atık yağdan biyodizel üretilir, yakıt hattında Viton hortum kullanılır."},
{"demir","Telsiz ve iletişim","telsiz,anten",
 "HF kısa dalga, VHF UHF, CB 27 MHz, deniz ve hava bandı dinleme altyapısı kurulur. Raspberry Pi üzerinden APRS ve SSTV sayısal veri aktarımı yapılır."},
{"yazit","Umay Kitaplığı akademik arşiv","arsiv,akademik",
 "Tarih ve kültür kaynakları: Ahmet Taşağıl Orta Asya Göktürkler ve Türkistan göç yolları, İlber Ortaylı Türk yönetim tarihi ve kültür coğrafyası, Celal Şengör Orta Asya tektoniği ve yer bilimleri. Sanat ve dil kaynakları: Metin And geleneksel Türk tiyatrosu şamanik törenler ve dans antropolojisi, Refik Ahmet Sevengil sahne sanatları tarihi, Kazım Mirşan proto Türk yazıtları ve runik tamgalar, Muazzez İlmiye Çığ Sümer ve Mezopotamya kültür sürekliliği."},
{"ortak","Çarklar mekaniği ve kinetik enerji","mekanik,disli",
 "Temel kural hızdan feragat et güce tahvil et, mekanik kazanç 10 üzeri. Beş altı dişliden oluşan kaydırmalı vites sliding gear şanzıman kutusu tasarlanır. Mod A kuvvet öğütme: yel değirmeni ya da koşu tamburundan gelen hareket kademeli redüksiyonla yüksek torka çevrilir, ağır bazalt taş değirmeni döndürerek bakliyatı un yapar. Mod B hız hızlı şarj: küçük el çarkı altı kademeli hız artırıcı overdrive dişli zinciri, bilyalı rulman ve ağır volan tekerleği üzerinden alternatörü yüksek devire ulaştırır, günde bir saatlik çevrimle aküler beslenir. Ana mil hattından tahrik alan bağımsız darbe üreteci çit hatlarına yüksek voltaj düşük amper vererek öldürmeyen caydırıcılık sağlar. Arazi kapıları sonsuz vida ve dişli çark worm gear ile çalışır, sistem geri dönüşsüz self locking olduğundan kapı dışarıdan itilerek açılamaz, gemi dümeni tipi el çarkı ve kontr ağırlıkla tonluk kapılar el gücüyle açılır."},
{"oyun","Dans, koreografi ve sahne","dans,sahne",
 "Kullanıcı dans eğitmenidir. Sığınakta prova alanı, düz ve kaymaz döşeme, yeterli tavan yüksekliği ve doğal aydınlatma gözetilir. Metin And'ın geleneksel Türk tiyatrosu, şamanik törenler ve dans antropolojisi çalışmaları ile Refik Ahmet Sevengil'in sahne sanatları tarihi kaynakları arşivde tutulur. Halay, zeybek, horon ve semah gibi yerel oyunların adım ve dizilim çizimleri belgelenir."},
{"kopuz","Ezgi, makam ve çalgı","muzik,calgi",
 "Kopuz, davul ve saz gibi çalgılar şebekeden bağımsız ortamda elektrik gerektirmeden çalınabilir. Makam ve usul bilgisi ile ezgi kayıtları sayısal olarak yerel arşivde tutulur. Dans provaları için ritim ve tempo kaynakları oyun alanıyla birlikte düzenlenir."},
{"yazit","Arşiv ve belge düzeni","arsiv,belge",
 "Umay Kitaplığı kaynakları kategori dizinlerinde düz metin olarak tutulur. Her belgeye künye, tarih ve kaynak bilgisi eklenir. Kritik veri için air-gapped yedekleme ünitesi kullanılır, iki ayrı ortamda kopya bulundurulur. Belge adları aranabilir sözcükler içerir."},
{"yargu","Yasa ve mevzuat çerçevesi","mevzuat,imar",
 "Zeytinlik vasıflı tarım arsasında yapılaşma imar mevzuatına ve zeytinciliğin ıslahı hakkındaki düzenlemelere bağlıdır. Yapı ruhsatı, tapu kaydı, kadastro sınırı ve su kuyusu izni gibi başlıklar ilgili kurumdan doğrulanmalıdır. Bu bilgi genel çerçevedir, hukuki görüş değildir."},
{"akca","Akçe ve maliyet düzeni","maliyet,piyasa",
 "Yatırım kalemleri güneş dizgesi, sarnıç, kiler, yapı malzemesi ve araç bakımı olarak ayrılır. Piyasa, döviz ve altın gibi anlık veriler ancak ULAK ağ bağlantısı açıkken alınabilir; ağ yokken güncel sayı verilmez. Maliyet karşılaştırmaları yerel ve ikinci el kaynak önceliğiyle yapılır."},
{"demir","Elektronik ve donanım","elektronik,kart",
 "Sınır radar sensörleri ve kameralar Orange Pi tabanlı kartlarla sürülür. Kartlar düşük güç tüketimi için uyku kipinde tutulur, besleme hattı LiFePO4 bataryadan gerilim düşürücü üzerinden alınır. Raspberry Pi telsiz tarafında APRS ve SSTV sayısal veri aktarımında kullanılır. Ölçüm yapılmadan devreye gerilim verilmez."},
{0,0,0,0}
};

int bilgi_doktrin_yukle(void) {
    int n = 0;
    for (int i = 0; DOKTRIN[i].kat; i++)
        if (kayit_ekle_tam(DOKTRIN[i].kat, DOKTRIN[i].bas, "doktrin", KT_DOKTRIN,
                           DOKTRIN[i].etiket, DOKTRIN[i].ic) == UM_OLDU) n++;
    return n;
}

/* ---- belge üstverisi ----
 * Kategori DOSYA YOLUNDAN çıkarılmaz; belgenin başındaki satırlar esastır:
 *   #kategori: balbal
 *   #baslik:   Horasan harcı
 *   #etiket:   harc,sarnic
 *   #kaynak:   Usta notları
 * Üstveri yoksa belge "ortak" havuzuna düşer ve günlüğe not edilir. */
static const char *ustveri_al(const char *bas, const char *anahtar, char *c, uint32_t n) {
    const char *p = bas;
    c[0] = 0;
    while (*p == '#') {
        const char *satir_sonu = p;
        while (*satir_sonu && *satir_sonu != '\n') satir_sonu++;
        if (um_bas_mi(p + 1, anahtar)) {
            const char *v = p + 1 + um_uzun(anahtar);
            while (*v == ':' || *v == ' ' || *v == '\t') v++;
            uint32_t o = 0;
            while (v < satir_sonu && o + 1 < n) c[o++] = *v++;
            while (o > 0 && (c[o-1] == ' ' || c[o-1] == '\r')) o--;
            c[o] = 0;
            return c;
        }
        if (!*satir_sonu) break;
        p = satir_sonu + 1;
    }
    return 0;
}

/* üstveri başlığından sonraki gövdenin başlangıcı */
static const char *govde_bul(const char *bas) {
    const char *p = bas;
    while (*p == '#') {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    while (*p == '\n' || *p == '\r') p++;
    return p;
}

static int gecerli_kategori(const char *k) {
    for (int i = 0; i < BILGI_ALAN_SAYI; i++) if (um_esit(k, DEPO_DIZIN[i])) return 1;
    return 0;
}

/* Bir belgeyi üstverisine göre indeksler. */
static int belge_yukle(const char *yol) {
    static char tampon[BILGI_ICERIK_MAX];
    up_tut_t t = up_ac(yol, UP_OKU);
    if (t < 0) return UM_YOK;
    int64_t okundu = up_oku(t, tampon, sizeof tampon - 1);
    up_kapat(t);
    if (okundu <= 0) return UM_YOK;
    tampon[okundu] = 0;

    char kategori[32], baslik[96], etiket[96], kaynak[96];
    const char *k = ustveri_al(tampon, "kategori", kategori, sizeof kategori);
    const char *b = ustveri_al(tampon, "baslik",   baslik,   sizeof baslik);
    const char *e = ustveri_al(tampon, "etiket",   etiket,   sizeof etiket);
    const char *s = ustveri_al(tampon, "kaynak",   kaynak,   sizeof kaynak);

    if (!k || !gecerli_kategori(kategori)) {
        gun_yaz("Belge üstverisiz ya da tanınmaz kategoride, ortak havuza koydum: %s", yol);
        um_kopya(kategori, sizeof kategori, "ortak");
    }
    if (!b || !baslik[0]) um_kopya(baslik, sizeof baslik, yol);
    if (!e) etiket[0] = 0;
    /* kaynak adı verilmişse yol ile birlikte saklanır */
    char kaynak_tam[96];
    if (s && kaynak[0]) um_bicim(kaynak_tam, sizeof kaynak_tam, "%s (%s)", yol, kaynak);
    else um_kopya(kaynak_tam, sizeof kaynak_tam, yol);

    return kayit_ekle_tam(kategori, baslik, kaynak_tam, KT_BELGE, etiket, govde_bul(tampon));
}

/* Depoyu tarar: 13 alan dizini + kök. Kategori yine üstveriden gelir;
 * dizin adı yalnız nereye bakılacağını söyler, kategoriyi belirlemez. */
int bilgi_tara(const char *kok) {
    if (!kok || !up_yetkim_var_mi(YET_DOSYA_OKU)) return 0;
    int n = 0;
    char yol[UMAY_YOL_MAX];
    up_girdi_t girdi[32];

    /* GERÇEK TARAMA: her alan dizini LİSTELENİR.
     *
     * Eskiden dizin hiç okunmuyordu; her alanda altı SABİT dosya adı
     * (belge1.txt, notlar.txt, kaynak.txt ...) yoklanıyordu. Yani
     * kütüphaneye konan bir belge, künyesi doğru olsa bile görünmezdi
     * — alan başına en çok altı, o da ancak adı tutarsa. Gerçek bir
     * arşivde dosya adları böyle olmaz.
     *
     * ÖTÜKEN'de SYS_LISTDIR baştan beri vardı; port onu dışa
     * vurmamıştı. Şimdi vuruyor. */
    for (int d = 0; d <= BILGI_ALAN_SAYI && g_sayi < BILGI_KAYIT_MAX; d++) {
        const char *dizin = (d < BILGI_ALAN_SAYI) ? DEPO_DIZIN[d] : 0;
        const char *ayrac = (kok[0] && kok[um_uzun(kok) - 1] == '/') ? "" : "/";

        char dyol[UMAY_YOL_MAX];
        if (dizin) um_bicim(dyol, sizeof dyol, "%s%s%s", kok, ayrac, dizin);
        else       um_bicim(dyol, sizeof dyol, "%s", kok);

        for (int sayfa = 0; sayfa < 4096 && g_sayi < BILGI_KAYIT_MAX;
             sayfa += 32) {
        int adet = up_dizin_listele(dyol, girdi, sayfa, 32);
        if (adet <= 0) break;

        for (int i = 0; i < adet && g_sayi < BILGI_KAYIT_MAX; i++) {
            if (girdi[i].dizin_mi) continue;
            /* yalnız düz metin: .txt ve .md */
            uint32_t u = um_uzun(girdi[i].ad);
            int metin = (u > 4 && um_esit(girdi[i].ad + u - 4, ".txt")) ||
                        (u > 3 && um_esit(girdi[i].ad + u - 3, ".md"));
            if (!metin) continue;
            /* model ve sözlük belge değildir */
            if (um_esit(girdi[i].ad, "umay.us1") ||
                um_esit(girdi[i].ad, "sozluk.txt") ||
                um_esit(girdi[i].ad, "bengi.txt") ||
                um_esit(girdi[i].ad, "ayar.txt")) continue;

            um_bicim(yol, sizeof yol, "%s/%s", dyol, girdi[i].ad);
            if (belge_yukle(yol) == UM_OLDU) n++;
        }
        if (adet < 32) break;          /* son sayfa */
        }
    }
    if (g_sayi >= BILGI_KAYIT_MAX)
        gun_yaz("Kütüphane: indeks DOLDU (%d kayıt) - kalan belgeler "
                "okunmadı.", BILGI_KAYIT_MAX);
    gun_yaz("Kütüphane: %d belge indekslendi, toplam %d kayıt.", n, g_sayi);
    return n;
}


/* V9.2 baglam_bul() ile aynı: kesişim skoru + ilk eşleşme çevresinde pencere.
 * kategoriler verilirse yalnız o kategorilerdeki kayıtlara bakılır. */
int bilgi_baglam_bul_suzgec(const char *soru, const bilgi_suzgec_t *sz,
                            baglam_t *cikan, int en_cok) {
    const char *const *kategoriler = sz ? sz->kategoriler : 0;
    int kat_sayi = sz ? sz->kategori_sayi : 0;
    char kw[24][24];
    int kws = kelime_ayikla(soru, kw, 24);
    if (kws == 0 || en_cok <= 0) return 0;

    int skor[BILGI_KAYIT_MAX], sira[BILGI_KAYIT_MAX], es = 0;
    for (int i = 0; i < g_sayi; i++) {
        if (kategoriler && kat_sayi > 0) {
            int uygun = 0;
            for (int q = 0; q < kat_sayi; q++)
                if (kategoriler[q] && um_esit(g_indeks[i].kategori, kategoriler[q])) { uygun = 1; break; }
            if (!uygun) continue;
        }
        if (sz && sz->etiket) {
            int uygun = 0;
            for (int q = 0; q < g_indeks[i].etiket_sayi; q++)
                if (um_esit(g_indeks[i].etiket[q], sz->etiket)) { uygun = 1; break; }
            if (!uygun) continue;
        }
        if (sz && sz->kaynak && !um_icinde(g_indeks[i].kaynak, sz->kaynak)) continue;
        int ortak = 0;
        for (int a = 0; a < kws; a++)
            for (int b = 0; b < g_indeks[i].kelime_sayisi; b++)
                if (um_esit(kw[a], g_indeks[i].kelime[b])) { ortak++; break; }
        if (ortak > 0) { skor[es] = ortak; sira[es] = i; es++; }
    }
    for (int i = 0; i < es; i++)
        for (int j = i + 1; j < es; j++)
            if (skor[j] > skor[i]) {
                int ts = skor[i]; skor[i] = skor[j]; skor[j] = ts;
                int ti = sira[i]; sira[i] = sira[j]; sira[j] = ti;
            }

    int sonuc = es < en_cok ? es : en_cok;
    static char kucuk[BILGI_ICERIK_MAX];
    for (int i = 0; i < sonuc; i++) {
        kayit_t *r = &g_indeks[sira[i]];
        um_kopya(cikan[i].kategori, sizeof cikan[i].kategori, r->kategori);
        um_kopya(cikan[i].baslik,   sizeof cikan[i].baslik,   r->baslik);
        um_kopya(cikan[i].kaynak,   sizeof cikan[i].kaynak,   r->kaynak);
        cikan[i].kaynak_tur = r->kaynak_tur;
        cikan[i].etiket_sayi = r->etiket_sayi;
        for (int e = 0; e < r->etiket_sayi; e++)
            um_kopya(cikan[i].etiket[e], 24, r->etiket[e]);
        cikan[i].skor = skor[i];

        um_kopya(kucuk, sizeof kucuk, r->icerik);
        um_kucult_esbicim(kucuk);

        int32_t idx = -1;
        for (int a = 0; a < kws; a++) {
            const char *bul = um_icinde(kucuk, kw[a]);
            if (bul) { int32_t k = (int32_t)(bul - kucuk); if (idx < 0 || k < idx) idx = k; }
        }
        int32_t uz = (int32_t)um_uzun(r->icerik), bas, bit;
        if (idx < 0) { bas = 0; bit = uz < 600 ? uz : 600; }
        else { bas = idx - 200; if (bas < 0) bas = 0;
               bit = idx + 600; if (bit > uz) bit = uz; }
        /* sözcük ortasından başlamasın: sonraki boşluğa kaydır */
        if (bas > 0) {
            int32_t k = bas;
            while (k < bit && r->icerik[k] != ' ' && k - bas < 40) k++;
            if (k < bit) bas = k + 1;
        }
        int32_t parca = bit - bas;
        if (parca > BILGI_ALINTI_MAX - 8) parca = BILGI_ALINTI_MAX - 8;
        um_bellek_kopya(cikan[i].alinti, r->icerik + bas, (uint64_t)parca);
        cikan[i].alinti[parca] = 0;
    }
    return sonuc;
}

int bilgi_baglam_bul_kat(const char *soru, const char *const *kategoriler,
                         int kat_sayi, baglam_t *cikan, int en_cok) {
    bilgi_suzgec_t sz = { kategoriler, kat_sayi, 0, 0 };
    return bilgi_baglam_bul_suzgec(soru, &sz, cikan, en_cok);
}

int bilgi_baglam_bul(const char *soru, baglam_t *cikan, int en_cok) {
    return bilgi_baglam_bul_suzgec(soru, 0, cikan, en_cok);
}

/* ---------------------------------------------------------------------
 * ÇATIŞMA — aynı kategoride, AYRI kaynaklardan gelen yakın skorlu iki
 * kayıt bulunduğunda bunlar sessizce birleştirilmez; çağırana bildirilir.
 * Bu bir anlam denetimi değildir: iki ayrı kaynağın aynı konuda söz
 * söylediğini saptar ve kararı kullanıcıya bırakır.
 * ------------------------------------------------------------------ */
int bilgi_catisma_bul(const baglam_t *dizi, int sayi, catisma_t *c) {
    if (c) um_bellek_doldur(c, 0, sizeof *c);
    if (!dizi || sayi < 2) return 0;
    for (int i = 0; i < sayi; i++)
        for (int j = i + 1; j < sayi; j++) {
            if (!um_esit(dizi[i].kategori, dizi[j].kategori)) continue;
            if (um_esit(dizi[i].kaynak, dizi[j].kaynak)) continue;
            if (dizi[i].skor - dizi[j].skor > 1) continue;   /* biri açıkça baskınsa çatışma sayılmaz */
            if (!c) return 1;
            c->var = 1;
            um_kopya(c->kategori, sizeof c->kategori, dizi[i].kategori);
            um_kopya(c->kaynak_a, sizeof c->kaynak_a, dizi[i].kaynak);
            um_kopya(c->kaynak_b, sizeof c->kaynak_b, dizi[j].kaynak);
            um_kopya(c->baslik_a, sizeof c->baslik_a, dizi[i].baslik);
            um_kopya(c->baslik_b, sizeof c->baslik_b, dizi[j].baslik);
            return 1;
        }
    return 0;
}
