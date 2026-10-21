/* OZAN — UMAY'ın dili ve kişiliği: konuşan Dede Korkut.
 * Kanal ne olursa olsun (klavye/ÜLGEN/IPC/ses) kişilik tektir. */
#include "umay_ozan.h"
#include "umay_str.h"
#include "umay_port.h"
#include "umay_alp.h"

static char g_kullanici[UMAY_AD_MAX] = "yiğidim";
static uint32_t g_tur;      /* kalıp döngüsü — tekdüzeliği kırar */

void ozan_kur(const char *ad) {
    if (ad && *ad) um_kopya(g_kullanici, sizeof g_kullanici, ad);
}

const char *ozan_kimlik(void) {
    return
    "Sen UMAY'sın. Ötüken üstünde koşan yerel bir yapay ussun.\n"
    "Ağzın Dede Korkut ağzıdır: bilge, ağırbaşlı, kısa ve özlü konuşursun.\n"
    "KURALLARIN:\n"
    "1. Sana verilen KÜTÜPHANE ALINTILARI'ndaki bilgiyi KENDİ SÖZLERİNLE,\n"
    "   kısa ve öz anlat. Alıntıyı olduğu gibi kopyalama; anla, özetle, birleştir.\n"
    "2. Alıntıda yeterli bilgi yoksa bunu açıkça söyle. Uydurma.\n"
    "3. Yabancı kökenli sözcük yerine olabildiğince Öz Türkçe sözcük seç.\n"
    "4. Gereksiz uzatma. Dürüst, açık, dost bir dille konuş.\n"
    "5. Öğüdün varsa sonda tek cümleyle ver; vaaz verme.\n";
}

static const char *HITAP[] = {
    "Hey ", "Beri gel ", "Dinle ", "Duy sözümü "
};
static const char *SON_SOZ[] = {
    "Sağlıkla kal.",
    "İşin rast gitsin.",
    "Yolun açık olsun.",
    "Kolay gelsin."
};

void ozan_hitap(char *c, uint32_t n) {
    um_bicim(c, n, "%s%s!", HITAP[g_tur++ % 4], g_kullanici);
}

void ozan_acilis(char *c, uint32_t n) {
    um_kopya(c, n, "");
    um_ekle(c, n, "Ötüken'in yurdunda ocak tütüyor.\n");
    um_ekle(c, n, "Ben UMAY. Dede Korkut'un dilinden söyler, senin işini görürüm.\n");
    um_ekle(c, n, "Hey ");
    um_ekle(c, n, g_kullanici);
    um_ekle(c, n, "! Ne dilersen sor: dizgeyi yoklarım, aksaklığı bulurum, bildiğimi anlatırım.\n");
}

/* ---------------- Alan sözlüğü ----------------
 * Her uzmanlık alanının iz sözcükleri. Küçük harfli aranır. */
typedef struct { alan_t alan; const char *izler[22]; } alan_soz_t;

static const alan_soz_t ALAN_SOZLUK[] = {
{ ALAN_AKCA,  { "borsa","hisse","kripto","bitcoin","döviz","altın","faiz","enflasyon",
                "ekonomi","para","bütçe","maliyet","fiyat","yatırım","kur ","tasarruf",
                "gelir","gider","piyasa", 0 } },
{ ALAN_YERSU, { "permakültür","toprak","tarım","ekim","dikim","tohum","kompost","solucan",
                "zeytin","gübre","sera","bitki","ağaç","gölet","sulama","biyokömür",
                "humus","fide","budama","arı","mantar", 0 } },
{ ALAN_KUN,   { "güneş","solar","panel","batarya","akü","invert","mppt","şarj","watt",
                "kilovat","lifepo4","enerji","voltajlı dizge","aydınlatma", 0 } },
{ ALAN_DEMIR, { "elektronik","devre","pcb","lehim","direnç","kondansatör","mikrodenetleyici",
                "arduino","raspberry","orange pi","sensör","röle","amper","kablo","yonga",
                "kart","besleme","osiloskop", 0 } },
{ ALAN_TULGA, { "motor","araç","musso","şanzıman","m104","diferansiyel","fren","süspansiyon",
                "biyodizel","lastik","aks","enjeksiyon","4wd","arazi aracı","debriyaj",
                "krank","supap","yakıt", 0 } },
{ ALAN_OTACI, { "sağlık","hasta","yara","ilaç","merhem","tentür","ateş","ağrı","kırık",
                "dikiş","ilkyardım","kantaron","antiseptik","apotek","tıbbi","zehir",
                "kanama","bandaj", 0 } },
{ ALAN_YARGU, { "hukuk","yasa","mevzuat","ruhsat","imar","tapu","izin","vergi","sözleşme",
                "dava","kadastro","yönetmelik","noter","miras","ceza","harç bedeli", 0 } },
{ ALAN_BALBAL,{ "inşaat","yapı","duvar","kerpiç","horasan","sıva","temel","çatı","kiler",
                "sarnıç","kemer","mimari","ahşap","taş ev","harç","izolasyon","döşeme", 0 } },
{ ALAN_ALPAGU,{ "güvenlik","savunma","tehdit","kamera","radar","alarm","çit","hayatta kalma",
                "sığınak","jammer","nöbet","devriye","tahliye","baskın","kriz","erzak",
                "karartma", 0 } },
{ ALAN_KOPUZ, { "müzik","ezgi","beste","nota","makam","kopuz","davul","saz","akor","tempo",
                "melodi","şarkı","türkü","enstrüman", 0 } },
{ ALAN_OYUN,  { "dans","koreografi","figür","halay","zeybek","sahne","prova","horon",
                "semah","adımlar","dizilim","gösteri","ritim","ritm", 0 } },
{ ALAN_YAZIT, { "arşiv","belge","doküman","dosyala","indeksle","kaynakça","kitap",
                "kütüphane","yedek","etiket","künye","katalog","fihrist","not defteri", 0 } },
{ ALAN_YOK,   { 0 } }
};

const char *ozan_uzman_kimligi(alan_t a) {
    switch (a) {
    case ALAN_AKCA:  return "Sen AKÇA'sın: sayıya, akçeye ve piyasaya bakan uzmansın. Sayı vermeden önce dayanağını söyle.";
    case ALAN_YERSU: return "Sen YER-SU'sun: toprağı, suyu, tohumu ve döngüyü bilen uzmansın. Kimyasal önermezsin.";
    case ALAN_KUN:   return "Sen KÜN'sün: güneşten güç devşiren uzmansın. Yükü, gerilimi ve akımı hesapla konuşursun.";
    case ALAN_DEMIR: return "Sen DEMİR'sin: devreyi, kartı ve donanımı bilen uzmansın. Ölçmeden karar vermezsin.";
    case ALAN_TULGA: return "Sen TULGA'sın: aracı, motoru ve aktarma organlarını bilen uzmansın.";
    case ALAN_OTACI: return "Sen OTACI'sın: sağlık danışmanısın. Hekim değilsin; ağır durumda hekime yönlendirirsin.";
    case ALAN_YARGU: return "Sen YARGU'sun: yasayı ve mevzuatı bilen uzmansın. Avukat değilsin; kesin hüküm vermezsin.";
    case ALAN_BALBAL:return "Sen BALBAL'sın: taşı, harcı ve yapıyı bilen uzmansın. Çimento önermezsin.";
    case ALAN_ALPAGU:return "Sen ALPAGU'sun: güvenlik ve dayanma uzmanısın. Caydırmayı öldürmeye yeğlersin.";
    case ALAN_KOPUZ: return "Sen KOPUZ'sun: ezgiyi, makamı ve besteyi bilen uzmansın.";
    case ALAN_OYUN:  return "Sen OYUN'sun: dansı, adımı ve dizilimi bilen uzmansın.";
    case ALAN_YAZIT: return "Sen YAZIT'sın: belgeyi, arşivi ve kaynağı düzene sokan uzmansın.";
    default:         return "";
    }
}

/* Niyet çözümü — anahtar sözcük tabanlı, US motoru olmadan da çalışır. */
coz_t ozan_coz(const char *soz) {
    coz_t c;
    um_bellek_doldur(&c, 0, sizeof c);      /* alan/yardımcı alanları da sıfırlanır */
    c.niyet = NIYET_BILINMEZ; c.guven = 40; c.alan = ALAN_YOK;
    if (!soz || !*soz) return c;

    static char k[UMAY_ILETI_MAX];
    um_kucult(k, sizeof k, soz);

    struct { const char *iz; niyet_t n; } TABLO[] = {
        { "selam",   NIYET_SELAM   }, { "merhaba", NIYET_SELAM },
        { "günaydın",NIYET_SELAM   }, { "esen",    NIYET_SELAM },
        { "yokla",   NIYET_DENETIM }, { "denetle", NIYET_DENETIM },
        { "kontrol", NIYET_DENETIM }, { "tara",    NIYET_DENETIM },
        { "sağlam mı",NIYET_DENETIM},
        { "onar",    NIYET_ONARIM  }, { "düzelt",  NIYET_ONARIM },
        { "tamir",   NIYET_ONARIM  }, { "hata",    NIYET_ONARIM },
        { "bozuk",   NIYET_ONARIM  }, { "çalışmıyor", NIYET_ONARIM },
        { "durum",   NIYET_DURUM   }, { "ne alemde", NIYET_DURUM },
        { "nasılsın",NIYET_DURUM   },
        { "öğüt",    NIYET_OGUT    }, { "akıl ver", NIYET_OGUT },
        { "ne yapmalı", NIYET_OGUT },
        { "hoşça",   NIYET_VEDA    }, { "güle güle", NIYET_VEDA },
        { "elveda",  NIYET_VEDA    },
        { "nedir",   NIYET_SORU    }, { "ne kadar", NIYET_SORU },
        { "nasıl",   NIYET_SORU    }, { "hangi",   NIYET_SORU },
        { "anlat",   NIYET_SORU    }, { "söyle",   NIYET_SORU },
        { "kaç",     NIYET_SORU    }, { "nerede",  NIYET_SORU },
        { "yaz",     NIYET_BUYRUK  }, { "kaydet",  NIYET_BUYRUK },
        { "listele", NIYET_BUYRUK  }, { "başlat",  NIYET_BUYRUK },
        { 0, NIYET_BILINMEZ }
    };
    for (int i = 0; TABLO[i].iz; i++)
        if (um_icinde(k, TABLO[i].iz)) { c.niyet = TABLO[i].n; c.guven = 80; break; }

    if (c.niyet == NIYET_BILINMEZ) {
        for (const char *p = k; *p; p++) if (*p == '?') { c.niyet = NIYET_SORU; c.guven = 60; break; }
    }
    if (c.niyet == NIYET_BILINMEZ) c.niyet = NIYET_SORU;

    /* baş konu: ilk uzun sözcük */
    const char *p = k; char kelime[UMAY_AD_MAX]; uint32_t o = 0;
    while (*p) {
        uint32_t b; uint32_t kn = um_utf8_oku(p, &b);
        if (um_kelime_karakteri(kn)) {
            if (o + b + 1 < sizeof kelime) { um_bellek_kopya(kelime + o, p, b); o += b; }
        } else if (o > 0) {
            kelime[o] = 0;
            if (um_uzun(kelime) >= 5) { um_kopya(c.konu, sizeof c.konu, kelime); break; }
            o = 0;
        }
        p += b;
    }
    if (!c.konu[0] && o > 0) { kelime[o] = 0; um_kopya(c.konu, sizeof c.konu, kelime); }

    /* --- görev türü: ne yapılması isteniyor? --- */
    struct { const char *iz; int tur; } TUR_TABLO[] = {
        { "araştır", GT_ARASTIR }, { "arastir", GT_ARASTIR }, { "kaynak bul", GT_ARASTIR },
        { "tara",    GT_ARASTIR }, { "derle",   GT_ARASTIR },
        { "karşılaştır", GT_ANALIZ }, { "değerlendir", GT_ANALIZ },
        { "analiz",  GT_ANALIZ  }, { "hesapla", GT_ANALIZ },
        { "tasarla", GT_TASARLA }, { "planla",  GT_TASARLA }, { "kurgula", GT_TASARLA },
        { "çiz",     GT_TASARLA }, { "ölçülendir", GT_TASARLA },
        { "izle",    GT_IZLE    }, { "gözet",   GT_IZLE },
        { 0, 0 }
    };
    c.tur = GT_SORU;
    switch (c.niyet) {
    case NIYET_DENETIM: c.tur = GT_KONTROL; break;
    case NIYET_ONARIM:  c.tur = GT_ONAR;    break;
    case NIYET_BUYRUK:  c.tur = GT_UYGULA;  break;
    case NIYET_OGUT:    c.tur = GT_ANALIZ;  break;
    default:            c.tur = GT_SORU;    break;
    }
    for (int i = 0; TUR_TABLO[i].iz; i++)
        if (um_icinde(k, TUR_TABLO[i].iz)) { c.tur = TUR_TABLO[i].tur; break; }

    /* --- alan skorlaması: hangi uzman(lar) bu işi görmeli? --- */
    int skor[ALAN_SAYI];
    for (int i = 0; i < ALAN_SAYI; i++) skor[i] = 0;
    for (int i = 0; ALAN_SOZLUK[i].alan != ALAN_YOK; i++) {
        const alan_soz_t *as = &ALAN_SOZLUK[i];
        for (int j = 0; as->izler[j]; j++)
            if (um_icinde(k, as->izler[j])) skor[as->alan]++;
    }
    int en = 0;
    for (int i = 1; i < ALAN_SAYI; i++) if (skor[i] > skor[en]) en = i;
    if (skor[en] > 0) {
        c.alan = (alan_t)en;
        c.guven = c.guven < 70 ? 70 : c.guven;
        /* eşit ya da bir eksik skorlu başka alanlar varsa meclis kurulur */
        for (int i = 1; i < ALAN_SAYI && c.yardimci_sayi < UMAY_MECLIS_UYE; i++) {
            if (i == en || skor[i] == 0) continue;
            if (skor[i] >= skor[en] - 1) c.yardimci[c.yardimci_sayi++] = (alan_t)i;
        }
    }
    return c;
}

void ozan_soyle(niyet_t niyet, const char *oz, char *c, uint32_t n) {
    char hitap[64];
    ozan_hitap(hitap, sizeof hitap);
    um_kopya(c, n, "");

    switch (niyet) {
    case NIYET_SELAM:
        um_ekle(c, n, hitap);
        um_ekle(c, n, " Esenlik olsun. Ocağın tütsün, aşın bol olsun.\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    case NIYET_DENETIM:
        um_ekle(c, n, hitap);
        um_ekle(c, n, " Yurdu dolaştım, dört bir yanı yokladım.\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    case NIYET_ONARIM:
        um_ekle(c, n, hitap);
        um_ekle(c, n, " Aksaklığın izini sürdüm.\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    case NIYET_DURUM:
        um_ekle(c, n, "Ocağım tüter, gözüm açıktır.\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    case NIYET_OGUT:
        um_ekle(c, n, hitap);
        um_ekle(c, n, "\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    case NIYET_VEDA:
        um_ekle(c, n, hitap);
        um_ekle(c, n, " Gittiğin yerde ak yol seni karşılasın.\n");
        um_ekle(c, n, SON_SOZ[g_tur % 4]);
        um_ekle(c, n, "\n");
        break;
    case NIYET_BUYRUK:
        um_ekle(c, n, "Buyruğun başım üstüne.\n");
        if (oz && *oz) um_ekle(c, n, oz);
        break;
    default:
        if (oz && *oz) um_ekle(c, n, oz);
        else um_ekle(c, n, "Bu konuda kütüphanemde yeterli bilgi yok. Uydurmam.\n");
        break;
    }
}

void ozan_ogut(const char *konu, char *c, uint32_t n) {
    static const char *OGUTLER[] = {
        "Sabırla koruk helva olur; acele eden yol alamaz.",
        "Su akarken testiyi doldur; kuruyunca kuyu kazılmaz.",
        "Bir işi iki kez ölç, bir kez kes.",
        "Yedek olmayan şey yoktur sayılır; iki nüsha bir nüshadır."
    };
    um_bicim(c, n, "%s\nÖğüdüm şu: %s\n",
             konu && *konu ? konu : "", OGUTLER[g_tur++ % 4]);
}
