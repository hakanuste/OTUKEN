/* =====================================================================
 * UZMAN AJAN KADROSU — 12 ayrı, gerçek alp_t kaydı.
 *
 * Her uzman OTAĞ'a bağımsız bir ajan olarak katılır; kendi durumu, kendi
 * görev kimliği, kendi bağlam tamponu vardır ve koşut çalışabilirler.
 * Adım mantığı ortaktır (bağlam bul → US'e sor → OZAN diliyle söyle),
 * ama her uzmanın kendi kimliği, kendi iz sözcükleri, kendi kütüphane
 * kategorileri ve kendi uyarısı vardır.
 *
 * GÖZCÜ / ONARICI / BİLGE / YAZMAN yerlerinde durur; bunlar onların
 * yerine değil, yanına katılır.
 * ===================================================================== */
#include "umay_alp.h"
#include "umay_bilgi.h"
#include "umay_ozan.h"
#include "umay_us.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_port.h"
#include "umay_arastirma.h"
#include "umay_kanit.h"


#define UZMAN_SAYI 12

typedef struct {
    alan_t      alan;
    const char *ad;
    const char *is;
    const char *kategori[3];    /* kütüphanede baktığı kategoriler */
    uint32_t    yetki_uyari;    /* bu yetki yoksa uyarı ekler      */
    const char *uyari;
} uzman_tanim_t;

static const uzman_tanim_t TANIM[UZMAN_SAYI] = {
{ ALAN_AKCA,  "AKÇA",   "akçe, borsa, kripto, makroekonomi",
  { "akca", "ortak", 0 }, YET_AG,
  "Ağ kapalı olduğundan güncel kur ve fiyat veremem; söylediklerim yerel dayanağa göredir.\n" },
{ ALAN_YERSU, "YER-SU", "permakültür, tarım, toprak, ekosistem",
  { "yer-su", "ortak", 0 }, 0, 0 },
{ ALAN_KUN,   "KÜN",    "güneş enerjisi ve solar dizgeler",
  { "kun", "ortak", 0 }, 0, 0 },
{ ALAN_DEMIR, "DEMİR",  "elektronik, kart, donanım",
  { "demir", "ortak", 0 }, 0, 0 },
{ ALAN_TULGA, "TULGA",  "otomotiv ve araç mühendisliği",
  { "tulga", "ortak", 0 }, 0, 0 },
{ ALAN_OTACI, "OTACI",  "sağlık ve tıp danışmanlığı",
  { "otaci", "ortak", 0 }, 0,
  "Ben hekim değilim. Ağır ya da süren bir durumda hekime görün.\n" },
{ ALAN_YARGU, "YARGU",  "hukuk ve mevzuat",
  { "yargu", "ortak", 0 }, 0,
  "Bu bilgi genel çerçevedir, hukuki görüş değildir. Kesin karar için ilgili kuruma ya da avukata danış.\n" },
{ ALAN_BALBAL,"BALBAL", "inşaat, taş ev, yapı ve mimari",
  { "balbal", "ortak", 0 }, 0, 0 },
{ ALAN_ALPAGU,"ALPAGU", "savunma, güvenlik, dayanma",
  { "alpagu", "ortak", 0 }, 0, 0 },
{ ALAN_KOPUZ, "KOPUZ",  "müzik ve beste",
  { "kopuz", "yazit", 0 }, 0, 0 },
{ ALAN_OYUN,  "OYUN",   "dans ve koreografi",
  { "oyun", "yazit", 0 }, 0, 0 },
{ ALAN_YAZIT, "YAZIT",  "bilgi arşivi ve belge yönetimi",
  { "yazit", "ortak", 0 }, 0, 0 }
};

typedef struct {
    alp_t      alp;
    const uzman_tanim_t *tanim;
    int        adim_no;
    gorev_t    gorev;
    baglam_t   baglam[3];
    int        baglam_sayi;
    int        nereden;          /* 0 = kendi kategorisi, 1 = ortak, 2 = genel depo */
    catisma_t  catisma;
    us_cikti_t cikti;            /* US çıkarım katmanının ortak sözleşmesi */
    char       rapor[GOREV_SONUC_MAX];
    int        ars_kip;          /* ARAŞTIR görevi: çok adımlı araştırma */
    arastirma_t ars;
} uzman_t;

static uzman_t g_uzman[UZMAN_SAYI];
static int     g_kuruldu;

const char *alan_adi(alan_t a) {
    for (int i = 0; i < UZMAN_SAYI; i++) if (TANIM[i].alan == a) return TANIM[i].ad;
    return "UMAY";
}
const char *alan_isi(alan_t a) {
    for (int i = 0; i < UZMAN_SAYI; i++) if (TANIM[i].alan == a) return TANIM[i].is;
    return "genel";
}

/* Kademeli dayanak araması (uzman ajan ve tests/umay_kanit AYNI işlevi
 * kullanır): (a) kendi kategorisi, (b) bildirdiği ortak kategoriler,
 * (c) genel depo (en çok 2). nereden: 0 kendi, 1 ortak, 2 genel. */
/* Dayanak arama (kanıt kipi: yalnız sorunun terimleri, kök eşlemeli,
 * ağırlıklı; bkz. umay_kanit.h). Kademe: kendi kategorisi -> ortak
 * kategorileri -> genel depo. Alan yönlendirmesi yanlışsa doğru kayıt
 * alan süzgecinin dışında kalır: alan dayanağı KANITI KARŞILAMIYORSA
 * genel depo da denenir ve kanıtı daha iyi karşılayan seçilir (genel
 * depodan gelen dayanak kullanıcıya söylenir, güveni ZAYIF'ı geçmez). */
int uzman_dayanak_bul(alan_t alan, const char *soru, baglam_t *cikan, int en_cok, int *nereden) {
    const uzman_tanim_t *t = 0;
    for (int i = 0; i < UZMAN_SAYI; i++) if (TANIM[i].alan == alan) t = &TANIM[i];
    int n = 0;
    *nereden = 0;
    if (t) {
        const char *birincil[1] = { t->kategori[0] };
        n = kanit_dayanak_bul(soru, birincil, 1, cikan, en_cok);
        if (n == 0) {
            const char *ortak[2]; int on = 0;
            for (int q = 1; q < 3 && t->kategori[q] && on < 2; q++) ortak[on++] = t->kategori[q];
            if (on > 0) {
                n = kanit_dayanak_bul(soru, ortak, on, cikan, en_cok);
                if (n > 0) *nereden = 1;
            }
        }
    }
    int genel_en_cok = en_cok < 2 ? en_cok : 2;
    if (n == 0) {
        n = kanit_dayanak_bul(soru, 0, 0, cikan, genel_en_cok);
        if (n > 0) *nereden = 2;
        return n;
    }
    kanit_t k_alan, k_genel;
    kanit_degerlendir(soru, cikan, n, 0, &k_alan);
    if (k_alan.yeterli) return n;
    static baglam_t genel[2];
    int g = kanit_dayanak_bul(soru, 0, 0, genel, genel_en_cok);
    if (g == 0) return n;
    kanit_degerlendir(soru, genel, g, 0, &k_genel);
    if ((k_genel.yeterli && !k_alan.yeterli) || k_genel.kapsama > k_alan.kapsama) {
        for (int i = 0; i < g; i++) um_bellek_kopya(&cikan[i], &genel[i], sizeof *cikan);
        n = g;
        *nereden = 2;
    }
    return n;
}

/* --- ortak ajan davranışı --- */

static int uzman_uygun(const gorev_t *g, alan_t alan) { return g->alan == alan; }

static int uzman_basla(alp_t *a, const gorev_t *g) {
    uzman_t *u = (uzman_t*)a->ic;
    u->adim_no = 0;
    u->baglam_sayi = 0;
    u->rapor[0] = 0;
    um_bellek_kopya(&u->gorev, g, sizeof *g);
    /* ARAŞTIR: uzman önce KENDİ kategorisinde, sonra bildirdiği ortak
     * kategorilerde, en son genel depoda tarar (arastirma.c). */
    u->ars_kip = (g->tur == GT_ARASTIR);
    if (u->ars_kip) {
        int ks = 0;
        while (ks < 3 && u->tanim->kategori[ks]) ks++;
        ars_basla(&u->ars, g->istem, u->tanim->kategori, ks, 1);
    }
    return UM_OLDU;
}

static alp_durum_t uzman_adim(alp_t *a) {
    uzman_t *u = (uzman_t*)a->ic;
    const uzman_tanim_t *t = u->tanim;

    /* 1. adım: kademeli arama.
     *   (a) kendi uzmanlık kategorisi
     *   (b) açıkça bildirdiği ortak / kütüphane kategorileri
     *   (c) genel depo — bu durumda kullanıcıya söylenir */
    if (u->ars_kip) {
        /* araştırma: her OTAĞ turunda TEK sorgu; ajan koşut kalır */
        if (!u->ars.bitti) {
            ars_adim(&u->ars);
            if (a->gorev) a->gorev->ilerleme = ars_ilerleme(&u->ars);
            return ALP_KOSUYOR;
        }
        /* tarama bitti: en iyi bulgular özetin dayanağı olur */
        u->baglam_sayi = ars_dayanak(&u->ars, u->baglam, 3);
        u->nereden = u->baglam_sayi ? (int)u->ars.havuz_kademe[0] : 0;
        if (u->nereden > 2) u->nereden = 2;            /* ağ belgesi: genel depo gibi, güven ZAYIF'ı geçmez */
        um_bellek_kopya(&u->catisma, &u->ars.catisma, sizeof u->catisma);
        if (u->catisma.var && a->gorev) {
            a->gorev->catisma = 1;
            um_bicim(a->gorev->catisma_notu, sizeof a->gorev->catisma_notu,
                     "%s alanında iki ayrı kaynak: %s ile %s",
                     u->catisma.kategori, u->catisma.kaynak_a, u->catisma.kaynak_b);
        }
    } else if (a->gorev) a->gorev->ilerleme = u->adim_no * 50;
    if (!u->ars_kip && u->adim_no++ == 0) {
        u->nereden = 0;
        um_bellek_doldur(&u->catisma, 0, sizeof u->catisma);

        u->baglam_sayi = uzman_dayanak_bul(t->alan, u->gorev.istem, u->baglam, 3, &u->nereden);
        /* çatışma: ayrı kaynaklar aynı konuda söz söylüyorsa sessizce
         * birleştirilmez, göreve iliştirilir ve OTAĞ'a bildirilir */
        if (bilgi_catisma_bul(u->baglam, u->baglam_sayi, &u->catisma) && a->gorev) {
            a->gorev->catisma = 1;
            um_bicim(a->gorev->catisma_notu, sizeof a->gorev->catisma_notu,
                     "%s alanında iki ayrı kaynak: %s ile %s",
                     u->catisma.kategori, u->catisma.kaynak_a, u->catisma.kaynak_b);
        }
        return ALP_KOSUYOR;
    }

    /* 2. adım: US çıkarım katmanına devret.
     * Dayanak, kaynak, uyarı, çatışma ve güven orada tek sözleşmeye girer. */
    char kisilik[768];
    um_bicim(kisilik, sizeof kisilik, "%s\n%s", ozan_kimlik(), ozan_uzman_kimligi(t->alan));

    char bengi[GOREV_BAGLAM_MAX];
    bengi[0] = 0;
    if (u->gorev.baglam[0]) um_kopya(bengi, sizeof bengi, u->gorev.baglam);

    us_derle_t istem;
    um_bellek_doldur(&istem, 0, sizeof istem);
    istem.kisilik      = kisilik;
    istem.soru         = u->gorev.istem;
    istem.niyet        = u->gorev.niyet;
    istem.tur          = (int)u->gorev.tur;
    istem.bengi_baglam = bengi[0] ? bengi : 0;
    istem.oncul_baglam = u->gorev.oncul_baglam[0] ? u->gorev.oncul_baglam : 0;
    istem.baglam       = u->baglam;
    istem.baglam_sayi  = u->baglam_sayi;
    istem.nereden      = (us_nereden_t)u->nereden;
    istem.catisma      = &u->catisma;
    istem.uzman_ad     = t->ad;
    istem.uzman_is     = t->is;
    istem.en_cok_im    = 256;
    istem.meclis       = u->gorev.kok_kimlik != 0;

    /* yetkiye bağlı uyarı (AKÇA'nın ağ uyarısı gibi) yalnız koşulu tutunca */
    if (t->uyari) {
        if (!t->yetki_uyari || !up_yetkim_var_mi((up_yetki_t)t->yetki_uyari))
            istem.uyari = t->uyari;
    }

    us_cevap_uret(&istem, &u->cikti);
    if (u->ars_kip) {
        /* rapor önce (bulgular + kaynaklar), özet altta */
        static char ozet[GOREV_SONUC_MAX];
        us_cikti_yaz(&u->cikti, ozet, sizeof ozet);
        ars_rapor(&u->ars, t->ad, u->rapor, sizeof u->rapor);
        um_ekle(u->rapor, sizeof u->rapor, "Özet — ");
        um_ekle(u->rapor, sizeof u->rapor, ozet);
    } else {
        us_cikti_yaz(&u->cikti, u->rapor, sizeof u->rapor);
    }
    return ALP_BITTI;
}

static const us_cikti_t *uzman_cikti(alp_t *a) {
    uzman_t *u = (uzman_t *)a->ic;
    return &u->cikti;
}

static void uzman_topla(alp_t *a, sonuc_t *s) {
    uzman_t *u = (uzman_t*)a->ic;
    um_kopya(s->oz, sizeof s->oz, u->rapor);
    um_kopya(s->ad, sizeof s->ad, a->ad);
    s->alan = a->alan;
}

/* uygun_mu imzası alan taşımadığından her uzmana ince bir kapı yazılır */
#define UZMAN_KAPI(no, alan_sabiti) \
    static int uzman_uygun_##no(const gorev_t *g) { return uzman_uygun(g, alan_sabiti); }
UZMAN_KAPI(0,  ALAN_AKCA)
UZMAN_KAPI(1,  ALAN_YERSU)
UZMAN_KAPI(2,  ALAN_KUN)
UZMAN_KAPI(3,  ALAN_DEMIR)
UZMAN_KAPI(4,  ALAN_TULGA)
UZMAN_KAPI(5,  ALAN_OTACI)
UZMAN_KAPI(6,  ALAN_YARGU)
UZMAN_KAPI(7,  ALAN_BALBAL)
UZMAN_KAPI(8,  ALAN_ALPAGU)
UZMAN_KAPI(9,  ALAN_KOPUZ)
UZMAN_KAPI(10, ALAN_OYUN)
UZMAN_KAPI(11, ALAN_YAZIT)

static int (*const KAPI[UZMAN_SAYI])(const gorev_t *) = {
    uzman_uygun_0, uzman_uygun_1, uzman_uygun_2,  uzman_uygun_3,
    uzman_uygun_4, uzman_uygun_5, uzman_uygun_6,  uzman_uygun_7,
    uzman_uygun_8, uzman_uygun_9, uzman_uygun_10, uzman_uygun_11
};

static void kadroyu_kur(void) {
    if (g_kuruldu) return;
    for (int i = 0; i < UZMAN_SAYI; i++) {
        uzman_t *u = &g_uzman[i];
        um_bellek_doldur(u, 0, sizeof *u);
        u->tanim = &TANIM[i];
        um_kopya(u->alp.ad, sizeof u->alp.ad, TANIM[i].ad);
        um_kopya(u->alp.is, sizeof u->alp.is, TANIM[i].is);
        u->alp.alan        = TANIM[i].alan;
        /* yetenek bildirimi: uzmanlar bilgi ve tasarım işlerini görür,
         * yıkıcı UYGULA işini görmez — o YAZMAN'a ve onaya bağlıdır. */
        u->alp.tur_maskesi = GT_BIT(GT_SORU) | GT_BIT(GT_ARASTIR) |
                             GT_BIT(GT_ANALIZ) | GT_BIT(GT_TASARLA);
        for (int q = 0; q < 3; q++) u->alp.kategori[q] = TANIM[i].kategori[q];
        u->alp.kategori[3] = 0;
        u->alp.yetki_gerek = 0;          /* katılmayı engellemez; uyarı içeride */
        u->alp.uygun_mu    = KAPI[i];
        u->alp.basla       = uzman_basla;
        u->alp.adim        = uzman_adim;
        u->alp.topla       = uzman_topla;
        u->alp.cikti_al    = uzman_cikti;
        u->alp.durum       = ALP_BOS;
        u->alp.ic          = u;
    }
    g_kuruldu = 1;
}

int alp_uzman_sayisi(void) { return UZMAN_SAYI; }

alp_t *alp_uzman_sirali(int i) {
    kadroyu_kur();
    if (i < 0 || i >= UZMAN_SAYI) return 0;
    return &g_uzman[i].alp;
}

alp_t *alp_uzman(alan_t alan) {
    kadroyu_kur();
    for (int i = 0; i < UZMAN_SAYI; i++)
        if (TANIM[i].alan == alan) return &g_uzman[i].alp;
    return 0;
}
