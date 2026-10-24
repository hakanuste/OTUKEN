/* =====================================================================
 * EĞİTİM (1.5) — UMAY kendi US1 modelini ÖTÜKEN İÇİNDE eğitir.
 *
 * Ne zaman: UMAY BOŞTAYKEN (son iletiden 'egitim_bos_sn' saniye sonra) ve
 * kütüphanenin ağ rafı son eğitimden bu yana DEĞİŞMİŞSE.
 *
 * Neyle: kütüphanedeki BÜTÜN kayıtlar (doktrin, belgeler, ağ rafı). Her
 * cümleden, cümlenin kendi anahtar sözcükleriyle sorular türetilir
 * ("S: <soru>\nU: <cümle>\n"; us1/arac/derlem.py ile aynı kalıplar).
 * Doktrin cümleleri de eğitime girer: yeni bilgi eski bilgiyi unutturmasın.
 *
 * Nasıl: us1/c/us1_egit.c (egit.py'nin float32 karşılığı; gradyanlar ve
 * Adam adımı tests/us1_egitim ile egit.py'ye karşı sınanır). Her UMAY
 * turunda TEK dizinin ileri+geri geçişi; 'egitim_yigin' dizide bir Adam
 * adımı. UMAY yanıt vermeyi sürdürür.
 *
 * Kapı (dürüst seçim): eğitimden ÖNCE ve SONRA iki ayrı doğrulama
 * kümesinde yanıt kaybı ölçülür (sorular eğitimde HİÇ görülmemiş
 * kalıplarla). Yeni model YALNIZ
 *   ağ doğrulaması  en az %2 iyileşmişse  VE
 *   doktrin doğrulaması en çok %5 kötüleşmişse
 * /KUT/UMAY/UMAY.US1 olarak yazılır ve US motoru onu yükler. Yoksa
 * atılır; eski model kalır. Karar ve sayılar günlüğe yazılır.
 * ===================================================================== */
#include "umay_egitim.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_bilgi.h"
#include "umay_us.h"
#include "us1_egit.h"

#define KISISEL_MODEL "/KUT/UMAY/UMAY.US1"
#define ANA_MODEL     "/UMAY.US1"
#define SOZLUK_YOL    "/SOZLUK.TXT"
#define DURUM_YOL     "/KUT/UMAY/EGITIM.TXT"
/* Modelin ÖZGÜN eğitim derlemi (us1/arac/derlem.py, dağıtım koşusu):
 * "S: soru\nU: yanıt\n" çiftleri. Tekrar (replay) ve koruma ölçümü
 * bununla yapılır: model TAM BU soru biçimleriyle eğitildi. */
#define DERLEM_YOL    "/UMAY.DRL"
#define CIFT_AZAMI    4000
#define CIFT_BAYT     (400 * 1024)

typedef enum { E_BEKLE = 0, E_OLC0, E_EGIT, E_OLC1, E_BITTI } evre_t;

typedef struct { uint32_t bas, boy, cevap; uint8_t ag, dog, ozgun; } cift_t;   /* imler[] içinde */

static char g_ag_soz[16][32]; static int g_ag_soz_n;   /* yeni bilginin anahtar sözcükleri */
static struct {
    evre_t   evre;
    int      kapali;
    uint64_t son_etkinlik, son_bakis;
    uint32_t im_son;                 /* son eğitimdeki veri imi */
    us1e_t   e;
    int      e_hazir;
    uint8_t  soz_im[256];            /* bayt -> im (255: sözlükte yok) */
    uint8_t  soz_bayt[256];          /* im -> bayt */
    uint8_t *imler; uint32_t im_n;   /* bütün çiftler uç uca */
    /* büyük diziler ÖBEKTEN, ilk eğitimde (bss'i şişirmesin: çekirdeğin
     * sızıntı izleyicisi açılıştaki kalıcı bellek artışını ölçer) */
    cift_t  *cift; int cift_n;
    int     *egit_idx, egit_n;
    int     *egit_ag_idx, egit_ag_n;    /* yeni (ağ) bilgi */
    int     *egit_konu_idx, egit_konu_n; /* yeni bilgiyle aynı konudaki eski çiftler */
    int     *dog_idx, dog_n;    /* görülmemiş kalıplı doğrulama (ag, doktrin) */
    /* ölçüm listesi: 0 ag (görülmemiş kalıp), 1 doktrin (görülmemiş kalıp),
     * 2 KORUMA: eski bilginin eğitimde görülen biçimli çiftleri — model
     * bunları BİLİYORDU; unutursa kapı reddeder */
    int     *olc_idx; uint8_t *olc_tur; int olc_n;
    float   *olc_on;     /* pencerenin eğitim öncesi kaybı (nat/bayt) */
    double   bil_once, bil_sonra, bil_w; int bil_n;   /* modelin BİLDİĞİ koruma çiftleri */
    /* ayarlar */
    int T, B, adim_top, dog_azami, bos_sn; float tepe;
    /* ilerleme */
    int adim, mikro, sira;
    double olc[2][3], ow[3], kayip_top;
    uint32_t tohum;
    uint32_t veri_imi;
    int      alan, buyudu, buyut_iste;
    uint8_t *dosya; uint32_t dosya_boy;
} G;

/* Eğitici belleği: iki sabit alan (her biri EN BÜYÜK model için: 8 katman,
 * ffn 512). UMAY'da bellek iadesi yok; büyütmede eski ve yeni model aynı
 * anda gerekir: kaynak bir alanda, büyümüş model öbüründe. Alanlar ilk
 * eğitimde bir kez ayrılır. */
#define ALAN_BOY (40ULL * 1024 * 1024)
static uint8_t *g_alan[2]; static size_t g_alan_dolu[2]; static int g_alan_sec;
static void *alan_ayir(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!g_alan[g_alan_sec] || g_alan_dolu[g_alan_sec] + n > ALAN_BOY) return 0;
    void *p = g_alan[g_alan_sec] + g_alan_dolu[g_alan_sec];
    g_alan_dolu[g_alan_sec] += n;
    return p;
}
static int alan_hazirla(int sec) {
    if (!g_alan[sec]) g_alan[sec] = (uint8_t *)up_ayir(ALAN_BOY);
    if (!g_alan[sec]) return -1;
    g_alan_dolu[sec] = 0; g_alan_sec = sec;
    return 0;
}
static uint32_t rnd(void) { G.tohum ^= G.tohum << 13; G.tohum ^= G.tohum >> 17; G.tohum ^= G.tohum << 5; return G.tohum; }

void egitim_etkinlik(void) { G.son_etkinlik = up_ms(); }

/* ---------------- derlem ---------------- */
static const char *SORU_KALIP[] = { "%s nedir", "%s nasıl", "%s ne olur", "%s nasıl yapılır", "%s nerede",
    "%s neyle yapılır", "%s hakkında bilgi ver", "%s anlat", "%s ne işe yarar", "%s kaç", "%s nasıl olmalı",
    "%s ne durumda" };
#define KALIP_SAYI 12
/* doğrulama soruları eğitimde HİÇ görülmeyen kalıplarla (sızıntı yok) */
static const char *DOG_KALIP[] = { "%s konusunda ne biliyorsun", "%s nedir acaba" };
static const char *DURAK[] = { "ve","ile","ya","da","veya","için","gibi","olarak","olan","bir","bu","şu","o","de",
    "ki","mi","mı","mu","mü","çok","daha","en","her","hiç","ise","yani","ancak","ama","fakat","değil","kadar",
    "sonra","önce","üzeri","üstü","yerine","sayede","edilir","olur","olacak","yapılır","şekilde","tüm","tamamen",
    "yalnızca","alınır","konur","kurulur", 0 };

static void kucult(const char *k, char *h, uint32_t n) { um_kucult(h, n, k); }
static int durak(const char *w) { for (int i = 0; DURAK[i]; i++) if (um_esit(w, DURAK[i])) return 1; return 0; }

/* cümleden anahtar sözcükler (en çok 8) */
static int anahtarlar(const char *c, char a[8][32]) {
    int n = 0; const char *p = c;
    while (*p && n < 8) {
        while (*p == ' ') p++;
        char w[64]; uint32_t o = 0;
        while (*p && *p != ' ' && o < 63) w[o++] = *p++;
        w[o] = 0;
        /* noktalamayı kırp */
        uint32_t b = 0; while (w[b] && um_icinde(".,;:()'\"!?", (char[]){ w[b], 0 })) b++;
        uint32_t s = um_uzun(w); while (s > b && um_icinde(".,;:()'\"!?", (char[]){ w[s - 1], 0 })) s--;
        w[s] = 0;
        char k[64]; kucult(w + b, k, sizeof k);
        uint32_t harf = 0, bb; for (const char *q = k; *q; q += bb) { um_utf8_oku(q, &bb); harf++; }
        int rakam = k[0] >= '0' && k[0] <= '9';
        if (harf >= 4 && !rakam && !durak(k)) um_kopya(a[n++], 32, k);
    }
    return n;
}

static int im_ekle(const char *s, int *ekli) {
    int n = 0;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
        if (G.soz_im[*p] == 255) { (*ekli)++; continue; }      /* sözlükte yok: atlanır */
        if (G.im_n >= CIFT_BAYT) return -1;
        G.imler[G.im_n++] = G.soz_im[*p]; n++;
    }
    return n;
}

static void cift_kat(const char *soru, const char *yanit, int ag, int dog) {
    if (G.cift_n >= CIFT_AZAMI) return;
    int atlanan = 0;
    cift_t *c = &G.cift[G.cift_n];
    c->bas = G.im_n; c->ag = (uint8_t)ag; c->dog = (uint8_t)dog; c->ozgun = 0;
    char on[200]; um_bicim(on, sizeof on, "S: %s\nU: ", soru);
    int a = im_ekle(on, &atlanan); if (a < 0) return;
    c->cevap = G.im_n;
    int b = im_ekle(yanit, &atlanan); if (b < 0) return;
    if (im_ekle("\n", &atlanan) < 0) return;
    c->boy = G.im_n - c->bas;
    G.cift_n++;
}

/* bir kaydın içeriğini cümlelere böl, her cümleden soru-yanıt */
static void kayit_isle(const char *icerik, int ag, int *dog_say) {
    static char cumle[400];
    const char *p = icerik;
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        uint32_t o = 0;
        while (*p && *p != '\n' && o < sizeof cumle - 1) {
            cumle[o++] = *p;
            if ((*p == '.' || *p == ';') && (p[1] == ' ' || p[1] == '\n' || !p[1])) { p++; break; }
            p++;
        }
        cumle[o] = 0;
        if (o < 12) continue;
        if (o > 150) {                                       /* kisalt(): sözcük sınırından */
            int k = 150; while (k > 0 && cumle[k] != ' ') k--;
            cumle[k > 0 ? k : 150] = 0; um_ekle(cumle, sizeof cumle, ".");
        }
        char a[8][32]; int an = anahtarlar(cumle, a);
        if (an == 0) continue;
        if (ag) for (int z = 0; z < an && g_ag_soz_n < 16; z++) {
            int var = 0; for (int q = 0; q < g_ag_soz_n; q++) if (um_esit(g_ag_soz[q], a[z])) var = 1;
            if (!var) um_kopya(g_ag_soz[g_ag_soz_n++], 32, a[z]);
        }
        /* eğitim: en çok 4 kalıplı soru */
        for (int s = 0; s < 4; s++) {
            int i = (int)(rnd() % (uint32_t)an);
            char konu[70];
            if ((rnd() % 100) < 55 || i + 1 >= an) um_kopya(konu, sizeof konu, a[i]);
            else um_bicim(konu, sizeof konu, "%s %s", a[i], a[i + 1]);
            char soru[140]; um_bicim(soru, sizeof soru, SORU_KALIP[rnd() % KALIP_SAYI], konu);
            cift_kat(soru, cumle, ag, 0);
        }
        /* doğrulama: görülmemiş kalıpla bir soru */
        if (*dog_say < G.dog_azami) {
            char soru[140]; um_bicim(soru, sizeof soru, DOG_KALIP[rnd() % 2], a[0]);
            cift_kat(soru, cumle, ag, 1);
            (*dog_say)++;
        }
    }
}

static uint32_t veri_imi_hesapla(void) {
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < bilgi_sayi(); i++) {
        const char *k, *b, *s, *ic;
        if (bilgi_kayit(i, &k, &b, &s, &ic) != UM_OLDU || !um_esit(k, "ag")) continue;
        for (const char *p = s; *p; p++) { h ^= (uint8_t)*p; h *= 0x01000193u; }
    }
    return h;
}

/* özgün derlem: dosyadaki çiftler aynen */
static int ozgun_yukle(void) {
    int64_t boy = up_boy(DERLEM_YOL);
    if (boy <= 0 || boy > 512 * 1024) return 0;
    char *t = (char *)up_ayir((uint64_t)boy + 1);       /* bir kez (aynı boyda yeniden kullanılır) */
    static char *onceki = 0; static int64_t onceki_boy = 0;
    if (onceki && onceki_boy >= boy) t = onceki; else { onceki = t; onceki_boy = boy; }
    if (!t) return 0;
    up_tut_t h = up_ac(DERLEM_YOL, UP_OKU);
    int64_t top = 0;
    while (top < boy) { int64_t n = up_oku(h, t + top, (uint64_t)(boy - top)); if (n <= 0) break; top += n; }
    up_kapat(h);
    t[top] = 0;
    int say = 0;
    for (char *p = t; *p && G.cift_n < CIFT_AZAMI; ) {
        if (!(p[0] == 'S' && p[1] == ':' && p[2] == ' ')) { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        char *soru = p + 3; char *e = soru; while (*e && *e != '\n') e++;
        if (!*e) break;
        *e = 0; char *u = e + 1;
        if (!(u[0] == 'U' && u[1] == ':' && u[2] == ' ')) { p = u; continue; }
        char *yanit = u + 3; char *f = yanit; while (*f && *f != '\n') f++;
        char son = *f; *f = 0;
        int once = G.cift_n;
        cift_kat(soru, yanit, 0, 0);
        if (G.cift_n > once) { G.cift[once].ozgun = 1; say++; }
        p = son ? f + 1 : f;
    }
    return say;
}

static int derlem_kur(void) {
    G.cift_n = 0; G.im_n = 0; G.tohum = 0x1453u; g_ag_soz_n = 0;
    int dog_ag = 0, dog_dok = 0;
    int ozgun = ozgun_yukle();
    for (int i = 0; i < bilgi_sayi(); i++) {
        const char *k, *b, *s, *ic;
        if (bilgi_kayit(i, &k, &b, &s, &ic) != UM_OLDU) continue;
        int ag = um_esit(k, "ag");
        /* özgün derlem varsa doktrinden çift türetilmez: model onları
         * özgün soru biçimleriyle zaten öğrendi (tekrar oradan yapılır) */
        if (!ag && ozgun > 0 && um_esit(s, "doktrin")) continue;
        kayit_isle(ic, ag, ag ? &dog_ag : &dog_dok);
    }
    if (ozgun == 0) gun_yaz("EGITIM: ozgun derlem (%s) yok: tekrar turetilmis ciftlerle", DERLEM_YOL);
    G.egit_n = G.dog_n = G.egit_ag_n = 0;
    for (int i = 0; i < G.cift_n; i++) {
        if (G.cift[i].dog) G.dog_idx[G.dog_n++] = i;
        else if (G.cift[i].ag) G.egit_ag_idx[G.egit_ag_n++] = i;
        else G.egit_idx[G.egit_n++] = i;
    }
    G.olc_n = 0;
    for (int i = 0; i < G.dog_n; i++) { G.olc_idx[G.olc_n] = G.dog_idx[i]; G.olc_tur[G.olc_n++] = G.cift[G.dog_idx[i]].ag ? 0 : 1; }
    /* KORUMA: önce yeni bilgiyle AYNI KONUDAKİ eski çiftler (unutma en çok
     * orada olur: aynı sözcükler, farklı yanıt), sonra eşit aralıklı örnek */
    int kor = 0, kor_azami = 4 * G.dog_azami;
    static uint8_t *secildi; if (!secildi) secildi = (uint8_t *)up_ayir(CIFT_AZAMI); if (!secildi) return -1;
    for (int i = 0; i < G.egit_n; i++) secildi[i] = 0;
    int ozgun_var = 0;
    for (int i = 0; i < G.egit_n; i++) if (G.cift[G.egit_idx[i]].ozgun) { ozgun_var = 1; break; }
    for (int i = 0; i < G.egit_n && kor < kor_azami / 2; i++) {
        const cift_t *c = &G.cift[G.egit_idx[i]];
        if (ozgun_var && !c->ozgun) continue;
        /* yanıt baytlarını metne geri çevirmek yerine: çiftin kaynağı olan
         * cümle soruyla aynı sözcükleri taşır; soru kısmında ara */
        static char soru[200]; uint32_t o = 0;
        for (uint32_t k = c->bas; k < c->cevap && o < sizeof soru - 1; k++) soru[o++] = (char)G.soz_bayt[G.imler[k]];
        soru[o] = 0;
        for (int q = 0; q < g_ag_soz_n; q++) if (um_icinde(soru, g_ag_soz[q])) { secildi[i] = 1; break; }
        if (secildi[i]) { G.olc_idx[G.olc_n] = G.egit_idx[i]; G.olc_tur[G.olc_n++] = 2; kor++; }
    }
    G.egit_konu_n = 0;
    for (int i = 0; i < G.egit_n; i++) {
        const cift_t *c = &G.cift[G.egit_idx[i]];
        static char soru[200]; uint32_t o = 0;
        for (uint32_t k = c->bas; k < c->cevap && o < sizeof soru - 1; k++) soru[o++] = (char)G.soz_bayt[G.imler[k]];
        soru[o] = 0;
        for (int q = 0; q < g_ag_soz_n; q++) if (um_icinde(soru, g_ag_soz[q])) { G.egit_konu_idx[G.egit_konu_n++] = G.egit_idx[i]; break; }
    }
    for (int i = 0; i < G.egit_n && kor < kor_azami; i += (G.egit_n / kor_azami > 0 ? G.egit_n / kor_azami : 1)) {
        if (secildi[i] || (ozgun_var && !G.cift[G.egit_idx[i]].ozgun)) continue;
        G.olc_idx[G.olc_n] = G.egit_idx[i]; G.olc_tur[G.olc_n++] = 2; kor++;
    }
    gun_yaz("EGITIM: derlem: %d cift (egitim: ag %d + eski %d, ayni konu %d, ozgun %d; dogrulama: ag %d, doktrin %d, koruma %d), %u im",
            G.cift_n, G.egit_ag_n, G.egit_n, G.egit_konu_n, ozgun, dog_ag, dog_dok, kor, G.im_n);
    return (G.egit_ag_n > 0 && dog_ag > 0) ? 0 : -1;
}

/* doğrulama penceresi: çiftin başından T+1 im (gerekirse sonraki çiftlerle
 * doldurulur); ağırlık YALNIZ bu çiftin yanıt baytlarında */
static void dog_penceresi(int ci, int *x, int *y, float *w, int T) {
    const cift_t *c = &G.cift[ci];
    for (int t = 0; t < T; t++) {
        uint32_t k = c->bas + (uint32_t)t;
        x[t] = G.imler[k % G.im_n];
        y[t] = G.imler[(k + 1) % G.im_n];
        uint32_t hedef = k + 1;
        w[t] = (hedef >= c->cevap && hedef < c->bas + c->boy) ? 1.0f : 0.0f;
    }
}

/* eğitim penceresi: rastgele çiftler uç uca (egit.py _cift_akisi). Yeni
 * (ağ) çiftleri yarı yarıya, eski bilgi (doktrin, belge) öbür yarı:
 * yeni bilgi öğrenilsin, eskisi unutulmasın (tekrar). */
static void egit_penceresi(int *x, int *y, int T) {
    int t = 0;
    int ilk = 1; uint32_t k = 0, son = 0;
    static int dizi[260];
    while (t < T + 1) {
        if (ilk || k >= son) {
            /* çeyrek yeni; çeyrek aynı konudaki eski (çakışan sözcükler); yarı
             * rastgele eski (özgün derlemden tekrar). Ölçüldü (tests/us1_egitim):
             * yarı yeni ile model bildiğini unutuyordu (koruma +%14..x6). */
            uint32_t z = rnd() & 3;
            const cift_t *c;
            if (G.egit_n == 0 || z == 0) c = &G.cift[G.egit_ag_idx[rnd() % (uint32_t)G.egit_ag_n]];
            else if (z == 1 && G.egit_konu_n > 0) c = &G.cift[G.egit_konu_idx[rnd() % (uint32_t)G.egit_konu_n]];
            else c = &G.cift[G.egit_idx[rnd() % (uint32_t)G.egit_n]];
            k = c->bas; son = c->bas + c->boy; ilk = 0;
        }
        dizi[t++] = G.imler[k++];
    }
    for (int i = 0; i < T; i++) { x[i] = dizi[i]; y[i] = dizi[i + 1]; }
}

/* ---------------- hazırlık ---------------- */
static int hazirla(void) {
    G.T = (int)ayar_sayi("egitim_T", 96);
    G.B = (int)ayar_sayi("egitim_yigin", 8);
    G.adim_top = (int)ayar_sayi("egitim_adim", 100);
    G.dog_azami = (int)ayar_sayi("egitim_dog", 24);
    /* öğrenme hızı: 5e-5. ÖLÇÜLDÜ (tests/us1_egitim/ayar_dene): eğitimi
     * bitmiş bu modelde 2e-4 ve üstü, bildiği çiftleri %25..%2400 bozuyor */
    G.tepe = (float)ayar_sayi("egitim_oran_e6", 50) * 1e-6f;
    if (G.T < 16) G.T = 16;
    if (G.B < 1) G.B = 1;
    if (G.adim_top < 1) G.adim_top = 1;
    /* sözlük */
    static char soz[256];
    up_tut_t t = up_ac(SOZLUK_YOL, UP_OKU);
    if (t == UP_GECERSIZ || (int64_t)t < 0) { gun_yaz("EGITIM: sozluk yok"); return -1; }
    int64_t sn = up_oku(t, soz, sizeof soz); up_kapat(t);
    if (sn <= 0) return -1;
    for (int i = 0; i < 256; i++) G.soz_im[i] = 255;
    for (int i = 0; i < sn; i++) { G.soz_im[(uint8_t)soz[i]] = (uint8_t)i; G.soz_bayt[i] = (uint8_t)soz[i]; }
    /* model: kişisel varsa o, yoksa ana */
    const char *yol = up_var_mi(KISISEL_MODEL) ? KISISEL_MODEL : ANA_MODEL;
    int64_t boy = up_boy(yol);
    if (boy <= 0 || boy > 2 * 1024 * 1024) { gun_yaz("EGITIM: model okunamadi (%s)", yol); return -1; }
    if (!G.cift) {
        G.cift = (cift_t *)up_ayir(sizeof(cift_t) * CIFT_AZAMI);
        G.egit_idx = (int *)up_ayir(sizeof(int) * CIFT_AZAMI); G.egit_ag_idx = (int *)up_ayir(sizeof(int) * CIFT_AZAMI);
        G.egit_konu_idx = (int *)up_ayir(sizeof(int) * CIFT_AZAMI); G.dog_idx = (int *)up_ayir(sizeof(int) * CIFT_AZAMI);
        G.olc_idx = (int *)up_ayir(sizeof(int) * CIFT_AZAMI); G.olc_tur = (uint8_t *)up_ayir(CIFT_AZAMI);
        G.olc_on = (float *)up_ayir(sizeof(float) * CIFT_AZAMI);
        if (!G.cift || !G.egit_idx || !G.egit_ag_idx || !G.egit_konu_idx || !G.dog_idx || !G.olc_idx || !G.olc_tur || !G.olc_on) {
            G.cift = 0; gun_yaz("EGITIM: bellek yok"); return -1;
        }
    }
    if (!G.dosya) G.dosya = (uint8_t *)up_ayir(2 * 1024 * 1024);
    if (!G.imler) G.imler = (uint8_t *)up_ayir(CIFT_BAYT);
    if (!G.dosya || !G.imler) { gun_yaz("EGITIM: bellek yok"); return -1; }
    t = up_ac(yol, UP_OKU);
    uint32_t top = 0;
    while (top < (uint32_t)boy) { int64_t n = up_oku(t, G.dosya + top, (uint64_t)boy - top); if (n <= 0) break; top += (uint32_t)n; }
    up_kapat(t);
    if (top != (uint32_t)boy) return -1;
    if (alan_hazirla(G.alan) != 0) { gun_yaz("EGITIM: egitici icin bellek yok"); return -1; }
    if (us1e_kur(&G.e, G.dosya, top, alan_ayir) != 0) { gun_yaz("EGITIM: model egitilebilir degil (yalniz v1)"); return -1; }
    G.e_hazir = 1;
    /* ---- 1.5: BÜYÜME ----
     * Veri modele göre büyüdüyse (derlem baytı / parametre > eşik) ya da
     * önceki eğitim yeni bilgiyi ÖĞRENEMEDİYSE model büyütülür: önce ffn
     * (+128, en çok 512), sonra katman (+1, en çok 8). Büyütme İŞLEVİ KORUR
     * (us1e_buyut): büyümüş model eğitimden önce eskisiyle aynı kaybı verir;
     * bu, bir doğrulama penceresinde ölçülüp günlüğe yazılır. Büyümüş model
     * de AYNI kapıdan geçer; reddedilirse eski model kalır. */
    G.buyudu = 0;
    if (G.T > G.e.T) G.T = G.e.T;
    if (derlem_kur() != 0) { gun_yaz("EGITIM: derlem yetersiz (ag rafi bos ya da sozluk disi)"); return -1; }
    {
        int esik_e3 = (int)ayar_sayi("egitim_buyut_oran_e3", 600);
        uint64_t oran_e3 = (uint64_t)G.im_n * 1000 / (G.e.n ? G.e.n : 1);
        int iste = G.buyut_iste || oran_e3 > (uint64_t)esik_e3;
        int yL = G.e.L, yF = G.e.F;
        if (G.e.F < 512) yF = G.e.F + 128 > 512 ? 512 : G.e.F + 128;
        else if (G.e.L < 8) yL = G.e.L + 1;
        if (iste && (yL != G.e.L || yF != G.e.F)) {
            static int x[260], y[260]; static float w[260];
            double k0 = 0, k1 = 0;
            if (G.dog_n > 0) { dog_penceresi(G.dog_idx[0], x, y, w, G.T); k0 = us1e_kayip(&G.e, x, y, w, G.T); }
            us1e_t yeni;
            int hedef = 1 - G.alan;
            if (alan_hazirla(hedef) == 0 && us1e_buyut(&yeni, &G.e, yL, yF, alan_ayir, G.veri_imi | 1u) == 0) {
                size_t n0 = G.e.n;
                int L0 = G.e.L, F0 = G.e.F;
                G.e = yeni; G.alan = hedef; G.buyudu = 1;
                if (G.dog_n > 0) k1 = us1e_kayip(&G.e, x, y, w, G.T);
                gun_yaz("EGITIM: model buyutuldu (veri/parametre %d e-3%s): katman %d ffn %d -> katman %d ffn %d, parametre %l -> %l, islev %s",
                        (int)oran_e3, G.buyut_iste ? ", onceki egitim ogrenemedi" : "", L0, F0, G.e.L, G.e.F,
                        (int64_t)n0, (int64_t)G.e.n, k0 == k1 ? "korundu (kayip birebir)" : "KORUNMADI");
            } else {
                alan_hazirla(G.alan);                        /* (kaynak alan değişmedi) */
                gun_yaz("EGITIM: buyutme icin bellek yok, mevcut boyla egitiliyor");
            }
        }
    }
    G.adim = 0; G.mikro = 0; G.sira = 0; G.kayip_top = 0;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 3; b++) G.olc[a][b] = 0;
    for (int b = 0; b < 3; b++) G.ow[b] = 0;
    G.bil_once = G.bil_sonra = G.bil_w = 0; G.bil_n = 0;
    gun_yaz("EGITIM: basladi model=%s T=%d yigin=%d adim=%d oran=%d e-6", yol, G.T, G.B, G.adim_top, (int)(G.tepe * 1e6f + 0.5f));
    return 0;
}

/* bir ölçüm penceresi; bitti mi. ev: 0 önce, 1 sonra */
static int olc_adim(int ev) {
    if (G.sira >= G.olc_n) return 1;
    static int x[260], y[260]; static float w[260];
    int j = G.sira++;
    int ci = G.olc_idx[j];
    dog_penceresi(ci, x, y, w, G.T);
    double ws = 0; for (int i = 0; i < G.T; i++) ws += w[i];
    if (ws > 0) {
        double k = us1e_kayip(&G.e, x, y, w, G.T);
        G.olc[ev][G.olc_tur[j]] += k;
        G.ow[G.olc_tur[j]] += ws;
        if (ev == 0) G.olc_on[j] = (float)(k / ws);
        /* bilinen: öncesi < 0,5 nat/bayt olan koruma çifti */
        if (ev == 1 && G.olc_tur[j] == 2 && G.olc_on[j] < 0.5f) {
            G.bil_once += G.olc_on[j] * ws; G.bil_sonra += k; G.bil_w += ws; G.bil_n++;
        }
    }
    if (G.sira >= G.olc_n) {
        for (int b = 0; b < 3; b++) { G.olc[ev][b] /= (G.ow[b] > 0 ? G.ow[b] : 1); G.ow[b] = 0; }
        return 1;
    }
    return 0;
}

/* aday model her durumda /KUT/UMAY/ADAY.US1'e: RET'in nedeni dışarıdan
 * incelenebilsin (US motoru onu YÜKLEMEZ) */
static int dosyaya_yaz(const char *yol, long w) {
    up_dizin_yarat("/KUT/UMAY");
    up_tut_t t = up_ac(yol, UP_YAZ | UP_YARAT);
    if (t == UP_GECERSIZ) return -1;
    int64_t y = up_yaz(t, G.dosya, (uint64_t)w);
    int r = up_kapat(t);
    return (y == w && r == UM_OLDU) ? 0 : -1;
}
static void aday_yaz(void) {
    size_t n = us1e_dosya_boy(&G.e);
    if (n > 2 * 1024 * 1024) return;
    long w = us1e_yaz(&G.e, G.dosya, n);
    if (w > 0 && dosyaya_yaz("/KUT/UMAY/ADAY.US1", w) == 0) gun_yaz("EGITIM: aday model /KUT/UMAY/ADAY.US1");
}

static void kaydet_ve_yukle(void) {
    size_t n = us1e_dosya_boy(&G.e);
    if (n > 2 * 1024 * 1024) return;
    long w = us1e_yaz(&G.e, G.dosya, n);
    if (w <= 0) return;
    up_dizin_yarat("/KUT/UMAY");
    up_tut_t t = up_ac(KISISEL_MODEL, UP_YAZ | UP_YARAT);
    if (t == UP_GECERSIZ) { gun_yaz("EGITIM: model YAZILAMADI"); return; }
    int64_t y = up_yaz(t, G.dosya, (uint64_t)w);
    int r = up_kapat(t);
    if (y != w || r != UM_OLDU) { gun_yaz("EGITIM: model YAZILAMADI (%l / %l)", y, (int64_t)w); return; }
    gun_yaz("EGITIM: yeni model yazildi %s (%l bayt)", KISISEL_MODEL, (int64_t)w);
    us_kur(KISISEL_MODEL);
    gun_yaz("EGITIM: US motoru: %s", us_motor_adi());
}

static void durum_yaz(const char *karar) {
    char s[200];
    uint32_t n = um_bicim(s, sizeof s, "im=%u\nkarar=%s\nbuyut=%d\nkatman=%d\nffn=%d\n", G.veri_imi, karar, G.buyut_iste, G.e.L, G.e.F);
    up_yaz_yola(DURUM_YOL, s, n);
}
static uint32_t durum_oku(void) {
    char s[200];
    up_tut_t t = up_ac(DURUM_YOL, UP_OKU);
    if (t == UP_GECERSIZ || (int64_t)t < 0) return 0;
    int64_t n = up_oku(t, s, sizeof s - 1); up_kapat(t);
    if (n <= 0) return 0;
    s[n] = 0;
    uint32_t v = 0;
    if (s[0] == 'i' && s[1] == 'm' && s[2] == '=') for (const char *p = s + 3; *p >= '0' && *p <= '9'; p++) v = v * 10 + (uint32_t)(*p - '0');
    const char *b = um_icinde(s, "buyut=");
    G.buyut_iste = b && b[6] == '1';
    return v;
}

void egitim_kur(void) {
    G.kapali = ayar_sayi("egitim", 1) == 0;
    G.bos_sn = (int)ayar_sayi("egitim_bos_sn", 120);
    G.im_son = durum_oku();
    G.son_etkinlik = up_ms();
    if (G.kapali) gun_yaz("EGITIM: kapali (ayar egitim=0)");
}

void egitim_pompa(void) {
    if (G.kapali) return;
    uint64_t simdi = up_ms();
    switch (G.evre) {
    case E_BEKLE:
        if (simdi - G.son_bakis < 5000) return;
        G.son_bakis = simdi;
        if (simdi - G.son_etkinlik < (uint64_t)G.bos_sn * 1000) return;
        G.veri_imi = veri_imi_hesapla();
        if (G.veri_imi == G.im_son || G.veri_imi == 0x811C9DC5u) return;   /* değişiklik yok / raf boş */
        if (hazirla() != 0) { G.im_son = G.veri_imi; return; }
        G.evre = E_OLC0;
        return;
    case E_OLC0:
        if (olc_adim(0)) {
            gun_yaz("EGITIM: once: ag %d, doktrin %d, koruma %d e-4 nat/bayt",
                    (int)(G.olc[0][0] * 1e4), (int)(G.olc[0][1] * 1e4), (int)(G.olc[0][2] * 1e4));
            G.evre = E_EGIT;
        }
        return;
    case E_EGIT: {
        static int x[260], y[260];
        egit_penceresi(x, y, G.T);
        G.kayip_top += us1e_ileri_geri(&G.e, x, y, 0, G.T, (double)G.B * G.T);
        if (++G.mikro < G.B) return;
        G.mikro = 0;
        G.adim++;
        int isinma = G.adim_top / 10 > 0 ? G.adim_top / 10 : 1;
        float lr = us1e_oran(G.adim, isinma, G.adim_top, G.tepe);
        double norm = us1e_adim(&G.e, lr);
        int kayip_e4 = (int)(G.kayip_top / ((double)G.B * G.T) * 1e4);
        G.kayip_top = 0;
        if (G.adim <= 3 || G.adim % 10 == 0 || G.adim == G.adim_top)
            gun_yaz("EGITIM: adim %d/%d kayip %d e-4 norm %d e-3", G.adim, G.adim_top, kayip_e4, (int)(norm * 1e3));
        if (G.adim >= G.adim_top) { G.evre = E_OLC1; G.sira = 0; }
        return;
    }
    case E_OLC1:
        if (olc_adim(1)) {
            /* KAPI: yeni bilgi >= %2 iyileşmeli; görülmemiş kalıplı doktrin
             * <= %5, eğitimde görülen biçimli eski bilgi (koruma) <= %10 kötüleşebilir */
            int ag_iyi = G.olc[1][0] <= G.olc[0][0] * 0.98;
            double bo = G.bil_w > 0 ? G.bil_once / G.bil_w : 0, bs = G.bil_w > 0 ? G.bil_sonra / G.bil_w : 0;
            /* + modelin BİLDİĞİ çiftler (öncesi < 0,5) en çok %15 kötüleşebilir */
            int dok_tamam = (G.olc[0][1] == 0 || G.olc[1][1] <= G.olc[0][1] * 1.05) && G.olc[1][2] <= G.olc[0][2] * 1.10 &&
                            (G.bil_n == 0 || bs <= bo * 1.15);
            gun_yaz("EGITIM: bilinen koruma ciftleri %d: %d -> %d e-4", G.bil_n, (int)(bo * 1e4), (int)(bs * 1e4));
            const char *karar = (ag_iyi && dok_tamam) ? "KABUL" : "RET";
            gun_yaz("EGITIM: sonra: ag %d -> %d e-4, doktrin %d -> %d e-4, koruma %d -> %d e-4 -> %s%s",
                    (int)(G.olc[0][0] * 1e4), (int)(G.olc[1][0] * 1e4), (int)(G.olc[0][1] * 1e4), (int)(G.olc[1][1] * 1e4),
                    (int)(G.olc[0][2] * 1e4), (int)(G.olc[1][2] * 1e4), karar,
                    ag_iyi ? (dok_tamam ? "" : " (eski bilgi unutuluyor)") : " (yeni bilgi ogrenilmedi)");
            aday_yaz();
            if (ag_iyi && dok_tamam) kaydet_ve_yukle();
            G.im_son = G.veri_imi;
            /* yeni bilgi öğrenilemediyse sonraki eğitim büyümüş modelle denenir */
            G.buyut_iste = !ag_iyi;
            durum_yaz(karar);
            G.evre = E_BEKLE;
            G.son_etkinlik = simdi;
        }
        return;
    default: G.evre = E_BEKLE; return;
    }
}

int egitim_suruyor(void) { return G.evre != E_BEKLE; }
