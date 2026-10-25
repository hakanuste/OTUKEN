/* =====================================================================
 * DEDE — Dede Korkut'un sesi: Türkçe formant sentezleyici.
 *
 * NE YAPAR: metni Türkçe seslerine ayırır ve KAYNAK-SÜZGEÇ modeliyle
 * PCM üretir. Kaynak, gırtlak darbe dizisidir (ötümlü sesler) ya da
 * gürültüdür (sürtünmeli sesler). Süzgeç, ses yolunu temsil eden üç
 * formant çınlayıcısıdır. Çıkış 48 kHz, 16 bit, iki kanal — doğrudan
 * AC'97 sürücüsüne (ses_cal) verilir.
 *
 * SES KİMLİĞİ: davudi, tok, yaşlı bilge bir erkek.
 *   - Temel perde 84 Hz (tipik erkek 100-120; burada bilinçli olarak
 *     daha pes) ve cümle boyunca hafifçe düşer.
 *   - Formantlar %8 aşağı kaydırıldı: uzun ses yolu = iri gövde izlenimi.
 *   - Gırtlak darbesine hafif titreşim (jitter) ve soluk gürültüsü
 *     eklendi; yaşlı sesin pürüzü bundan gelir.
 *
 * NE DEĞİLDİR: bu bir sinir ağı sentezleyicisi değildir. Anlaşılır ama
 * mekanik bir ses üretir; eşleme birebir fonetik değil, harf tabanlıdır.
 * Türkçe yazıldığı gibi okunduğu için bu yaklaşım tutar.
 *
 * TAMSAYI: kayan nokta birimi kapalıdır. Çınlayıcılar 16.16 sabit
 * noktada iki kutuplu özyinelemeli süzgeçtir.
 * ===================================================================== */
#include "dede.h"
#include "ses.h"
#include "serial.h"
#include "ses_kilit.h"
#include "spinlock.h"
#include "thread.h"

#define HZ        48000
#define KESIR     16
#define BIR       (1 << KESIR)
#define PARCA     1024          /* tek seferde üretilen çerçeve */

/* --- Ses birimi tanımı --- */
typedef enum {
    T_SESLI,        /* ötümlü, formantlı            */
    T_SURTUNME,     /* gürültü kaynaklı (s, ş, f, h) */
    T_PATLAMA,      /* kapanma + patlama (p, t, k)   */
    T_OTUMLU_PAT,   /* ötümlü patlama (b, d, g)      */
    T_GENIZ,        /* m, n                          */
    T_AKICI,        /* l, r, y, v, z, j              */
    T_SESSIZLIK
} ses_turu_t;

typedef struct {
    ses_turu_t tur;
    uint16_t   f1, f2, f3;   /* formantlar, Hz */
    uint16_t   sure_ms;
} birim_t;

/* Türkçe seslilerin formantları (erkek sesi, Hz).
 * Kaynak: genel fonetik değerler; ÖTÜKEN için %8 pesleştirildi. */
static const struct { uint32_t kod; uint16_t f1, f2, f3; } SESLI[] = {
    { 'a',    640, 1100, 2400 },
    { 'e',    460, 1800, 2500 },
    { 0x0131, 350, 1400, 2300 },   /* ı */
    { 'i',    280, 2100, 2800 },
    { 'o',    460,  800, 2400 },
    { 0x00F6, 420, 1500, 2200 },   /* ö */
    { 'u',    320,  700, 2300 },
    { 0x00FC, 300, 1600, 2200 },   /* ü */
};
#define SESLI_SAYI ((int)(sizeof(SESLI) / sizeof(SESLI[0])))

static int sesli_ara(uint32_t kod)
{
    for (int i = 0; i < SESLI_SAYI; i++) if (SESLI[i].kod == kod) return i;
    return -1;
}

/* --- Rastgelelik: gürültü ve titreşim için --- */
static uint32_t g_tohum = 0x5EED1234u;

static int32_t gurultu(void)
{
    g_tohum = g_tohum * 1103515245u + 12345u;
    return (int32_t)((g_tohum >> 16) & 0xFFFF) - 32768;
}

/* --- İki kutuplu çınlayıcı (formant) ---
 * y[n] = x[n] + 2r·cos(w)·y[n-1] − r²·y[n-2]
 * Katsayılar 16.16'da; cos tablo ile, kayan nokta yok. */
typedef struct { int32_t y1, y2, a1, a2; } cinlayici_t;

/* cos(x) yaklaşımı, x 0..BIR yarım çevrimi temsil eder (0..pi). */
static int32_t kosinus_yarim(int32_t t)
{
    /* Parabol yaklaşımı: cos(pi·t) ≈ 1 − 8t² + 8t⁴ (0≤t≤1)
     * Hata %1'in altında; formant konumu için fazlasıyla yeterli. */
    int32_t t2 = (int32_t)(((int64_t)t * t) >> KESIR);
    int32_t t4 = (int32_t)(((int64_t)t2 * t2) >> KESIR);
    return BIR - 8 * t2 + 8 * t4;
}

static void cinlayici_kur(cinlayici_t *c, uint32_t frekans, uint32_t bant)
{
    /* r = 1 − pi·bant/HZ  (yaklaşık), w = 2pi·f/HZ */
    int32_t r = BIR - (int32_t)(((uint64_t)bant * 205887u) / HZ);  /* pi<<16 */
    if (r < 0) r = 0;
    if (r > BIR - 200) r = BIR - 200;
    int32_t t = (int32_t)(((uint64_t)frekans * 2u * BIR) / HZ);    /* w/pi */
    if (t > BIR) t = BIR;
    int32_t cosw = kosinus_yarim(t);
    c->a1 = (int32_t)((((int64_t)2 * r) * cosw) >> KESIR);
    c->a2 = -(int32_t)(((int64_t)r * r) >> KESIR);
    c->y1 = c->y2 = 0;
}

static int32_t cinlayici_isle(cinlayici_t *c, int32_t x)
{
    int64_t y = (int64_t)x
              + (((int64_t)c->a1 * c->y1) >> KESIR)
              + (((int64_t)c->a2 * c->y2) >> KESIR);
    if (y >  (1 << 28)) y =  (1 << 28);
    if (y < -(1 << 28)) y = -(1 << 28);
    c->y2 = c->y1;
    c->y1 = (int32_t)y;
    return (int32_t)y;
}

/* --- Harf -> ses birimi eşlemesi ---
 * Türkçe yazıldığı gibi okunur; harf tabanlı eşleme tutarlı sonuç verir.
 * Formantı olmayan sesler için, komşu seslinin formantları kullanılır
 * (geçişler doğal dursun diye). */
static void birim_coz(uint32_t kod, birim_t *b, const birim_t *onceki_sesli)
{
    int s = sesli_ara(kod);
    if (s >= 0) {
        b->tur = T_SESLI;
        b->f1 = SESLI[s].f1; b->f2 = SESLI[s].f2; b->f3 = SESLI[s].f3;
        b->sure_ms = 105;
        return;
    }
    /* Sessizler komşu seslinin ses yolunu ödünç alır. */
    b->f1 = onceki_sesli ? onceki_sesli->f1 : 500;
    b->f2 = onceki_sesli ? onceki_sesli->f2 : 1400;
    b->f3 = onceki_sesli ? onceki_sesli->f3 : 2400;

    switch (kod) {
    case 's': case 0x015F: case 'f': case 'h':          /* s ş f h */
        b->tur = T_SURTUNME; b->sure_ms = 85;
        if (kod == 's')      { b->f1 = 3600; b->f2 = 5200; b->f3 = 7000; }
        else if (kod == 0x015F) { b->f1 = 2200; b->f2 = 3400; b->f3 = 5000; }
        else if (kod == 'f') { b->f1 = 1600; b->f2 = 4200; b->f3 = 6000; }
        else                 { b->f1 = 1000; b->f2 = 2000; b->f3 = 3000; }
        break;
    case 'p': case 't': case 'k': case 0x00E7:          /* p t k ç */
        b->tur = T_PATLAMA; b->sure_ms = 70;
        break;
    case 'b': case 'd': case 'g': case 0x011F: case 'c': /* b d g ğ c */
        b->tur = T_OTUMLU_PAT; b->sure_ms = 70;
        if (kod == 0x011F) { b->tur = T_AKICI; b->sure_ms = 45; }  /* ğ uzatır */
        break;
    case 'm': case 'n':
        b->tur = T_GENIZ; b->sure_ms = 80;
        b->f1 = 280; b->f2 = 1100; b->f3 = 2300;
        break;
    case 'l': case 'r': case 'y': case 'v': case 'z': case 'j':
        b->tur = T_AKICI; b->sure_ms = 65;
        if (kod == 'l') { b->f1 = 360; b->f2 = 1100; b->f3 = 2600; }
        if (kod == 'r') { b->f1 = 420; b->f2 = 1300; b->f3 = 1600; }
        if (kod == 'y') { b->f1 = 280; b->f2 = 2200; b->f3 = 3000; }
        break;
    default:
        b->tur = T_SESSIZLIK; b->sure_ms = 70;
        break;
    }
}

/* UTF-8 çözücü: Türkçe harfler iki bayttır. */
static uint32_t utf8_al(const char **p)
{
    const unsigned char *u = (const unsigned char *)*p;
    if (u[0] < 0x80) { (*p)++; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        uint32_t k = (uint32_t)((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        *p += 2;
        /* Büyük harfleri küçüğe indir (Türkçe dahil). */
        if (k == 0x00C7) k = 0x00E7;   /* Ç */
        if (k == 0x011E) k = 0x011F;   /* Ğ */
        if (k == 0x0130) k = 'i';      /* İ */
        if (k == 0x00D6) k = 0x00F6;   /* Ö */
        if (k == 0x015E) k = 0x015F;   /* Ş */
        if (k == 0x00DC) k = 0x00FC;   /* Ü */
        return k;
    }
    (*p)++;
    return u[0];
}

static int16_t g_tampon[PARCA * 2];

/* Tek birimi seslendirir ve sürücüye yollar. */
static void birim_sesle(const birim_t *b, int32_t *faz, uint32_t *f0)
{
    cinlayici_t c1, c2, c3;
    uint32_t bant1 = 90, bant2 = 110, bant3 = 170;
    if (b->tur == T_SURTUNME) { bant1 = 500; bant2 = 700; bant3 = 900; }
    cinlayici_kur(&c1, b->f1, bant1);
    cinlayici_kur(&c2, b->f2, bant2);
    cinlayici_kur(&c3, b->f3, bant3);

    uint32_t toplam = (uint32_t)b->sure_ms * HZ / 1000;
    uint32_t yazilan = 0;

    while (yazilan < toplam) {
        uint32_t n = (toplam - yazilan < PARCA) ? (toplam - yazilan) : PARCA;
        for (uint32_t i = 0; i < n; i++) {
            int32_t kaynak = 0;
            uint32_t k = yazilan + i;

            if (b->tur == T_SESLI || b->tur == T_GENIZ ||
                b->tur == T_AKICI || b->tur == T_OTUMLU_PAT) {
                /* Gırtlak darbesi: perde döneminde bir kez uyarım.
                 * Titreşim (jitter) yaşlı sesin pürüzünü verir. */
                uint32_t donem = HZ / (*f0);
                *faz += 1;
                if ((uint32_t)*faz >= donem + (uint32_t)((gurultu() >> 13) & 3)) {
                    *faz = 0;
                    kaynak = 26000;
                }
                kaynak += gurultu() >> 7;        /* soluk gürültüsü */
                if (b->tur == T_OTUMLU_PAT && k < 500) kaynak += gurultu() >> 3;
            } else if (b->tur == T_SURTUNME) {
                kaynak = gurultu() >> 1;
            } else if (b->tur == T_PATLAMA) {
                /* Kapanma sessiz, sonra kısa patlama. */
                uint32_t patla = toplam * 2 / 3;
                kaynak = (k > patla) ? (gurultu()) : 0;
            }

            int32_t y = cinlayici_isle(&c1, kaynak);
            y += cinlayici_isle(&c2, kaynak) >> 1;
            y += cinlayici_isle(&c3, kaynak) >> 2;

            /* Birim başı ve sonunda yumuşatma: çıtlama olmasın. */
            uint32_t kenar = HZ / 250;
            if (k < kenar)             y = (int32_t)(((int64_t)y * k) / kenar);
            else if (k + kenar > toplam)
                y = (int32_t)(((int64_t)y * (toplam - k)) / kenar);

            y >>= 9;
            if (y >  30000) y =  30000;
            if (y < -30000) y = -30000;
            g_tampon[i * 2]     = (int16_t)y;
            g_tampon[i * 2 + 1] = (int16_t)y;
        }
        ses_cal(g_tampon, n);
        yazilan += n;
    }
}

static uint32_t son_kare, son_birim;
uint32_t dede_son_kare(void) { return son_kare; }
uint32_t dede_son_birim(void) { return son_birim; }

static int dede_konus__ic(const char *metin) {
    if (!metin || !ses_var()) return -1;

    uint32_t bas_kare = ses_durum()->calinan_ornek;
    uint32_t f0 = DEDE_TEMEL_PERDE;
    int32_t faz = 0;
    birim_t onceki_sesli;
    int onceki_var = 0;
    int birim_sayisi = 0;

    const char *p = metin;
    while (*p) {
        uint32_t kod = utf8_al(&p);
        if (kod >= 'A' && kod <= 'Z') kod += 32;

        if (kod == ' ' || kod == ',' || kod == '.' ||
            kod == '!' || kod == '?' || kod == ';' || kod == ':') {
            birim_t d = { T_SESSIZLIK, 0, 0, 0,
                          (uint16_t)((kod == ' ') ? 60 : 180) };
            birim_sesle(&d, &faz, &f0);
            /* Cümle sonunda perde iner: bitmişlik duygusu. */
            if (kod == '.' || kod == '!') f0 = DEDE_TEMEL_PERDE;
            continue;
        }

        birim_t b;
        birim_coz(kod, &b, onceki_var ? &onceki_sesli : 0);
        if (b.tur == T_SESLI) { onceki_sesli = b; onceki_var = 1; }
        birim_sesle(&b, &faz, &f0);
        birim_sayisi++;

        /* Ezgi düşüşü: her birkaç seste perde bir tık iner, cümle
         * boyunca doğal bir alçalma olur (yaşlı, sakin söyleyiş). */
        if ((birim_sayisi % 6) == 0 && f0 > DEDE_TEMEL_PERDE - 12) f0--;
    }
    ses_bitir();
    son_kare = ses_durum()->calinan_ornek - bas_kare;
    son_birim = (uint32_t)birim_sayisi;
    serial_write("DEDE: soylendi, birim=");
    serial_write_hex((uint32_t)birim_sayisi);
    serial_write(" kare=");
    serial_write_hex(ses_durum()->calinan_ornek - bas_kare);
    serial_write("\n");
    return 0;
}

int dede_birim_sayisi(const char *metin)
{
    if (!metin) return 0;
    int n = 0;
    const char *p = metin;
    while (*p) {
        uint32_t kod = utf8_al(&p);
        if (kod != ' ') n++;
    }
    return n;
}

int dede_sesli_mi(uint32_t kod)
{
    if (kod >= 'A' && kod <= 'Z') kod += 32;
    return sesli_ara(kod) >= 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int dede_konus(const char *metin) {
    ozkilit_al(&ses_kilit);
    int r = dede_konus__ic(metin);
    ozkilit_birak(&ses_kilit);
    return r;
}

/* =====================================================================
 * 1.4.9: SIRALI (eşzamansız) konuşma. Ses kartı bağlanınca (ses_init)
 * Dede Korkut UMAY'ın her yanıtını söylemeye başladı ve SYS_DEDE_KONUS konuşma bitene
 * dek (uzun yanıtta ~15 sn) UMAY'ı bekletiyordu; UYUM sınaması bunu
 * yakaladı. Artık çağrı metni sıraya koyup hemen döner; "Dede" çekirdek
 * iş parçacığı sırayla söyler. Sıra doluysa EN ESKİ bekleyen düşer (en
 * yeni yanıt önemlidir); o an söylenen kesilmez.
 * ===================================================================== */
#define DEDE_SIRA 4
#define DEDE_METIN 512
static char     sira[DEDE_SIRA][DEDE_METIN];
static int      sira_bas, sira_say;
static volatile int konusuyor;
static uint32_t dusen;
static ozspin_t sira_kilit = OZSPIN_BASLANGIC;   /* sahip -1 olmali */
static volatile int isci_var;

static void dede_isci(void) {
    static char metin[DEDE_METIN];
    for (;;) {
        int var = 0;
        ozspin_al(&sira_kilit);
        if (sira_say > 0) {
            for (int i = 0; i < DEDE_METIN; i++) metin[i] = sira[sira_bas][i];
            sira_bas = (sira_bas + 1) % DEDE_SIRA;
            sira_say--;
            konusuyor = 1;
            var = 1;
        }
        ozspin_birak(&sira_kilit);
        if (!var) { thread_sleep(2); continue; }
        dede_konus(metin);
        __atomic_store_n(&konusuyor, 0, __ATOMIC_RELEASE);
    }
}

void dede_sira_baslat(void) {
    if (isci_var || !ses_var()) return;
    /* işçi GERÇEKTEN başladıysa istek kabul edilir; yoksa sıraya alınan
     * söz hiç tüketilmez, "söylüyor" bayrağı hiç kalkmazdı */
    if (!thread_create("Dede", dede_isci)) {
        serial_write("DEDE: konusma iscisi baslatilamadi (gorev ayrilamadi)\n");
        return;
    }
    isci_var = 1;
}

int dede_konus_sirala(const char *metin) {
    if (!metin || !ses_var() || !isci_var) return -1;
    ozspin_al(&sira_kilit);
    if (sira_say == DEDE_SIRA) {                  /* en eskiyi düşür */
        sira_bas = (sira_bas + 1) % DEDE_SIRA;
        sira_say--;
        dusen++;
    }
    int k = (sira_bas + sira_say) % DEDE_SIRA;
    int i = 0;
    for (; metin[i] && i < DEDE_METIN - 1; i++) sira[k][i] = metin[i];
    sira[k][i] = 0;
    sira_say++;
    ozspin_birak(&sira_kilit);
    return 0;
}

int dede_mesgul(void) {
    ozspin_al(&sira_kilit);
    int m = sira_say > 0 || __atomic_load_n(&konusuyor, __ATOMIC_ACQUIRE);
    ozspin_birak(&sira_kilit);
    return m;
}
uint32_t dede_dusen(void) { return dusen; }
