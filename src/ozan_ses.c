/* =====================================================================
 * OZAN — Dede Korkut'un sesi.
 *
 * NE DEĞİLDİR: bu bir konuşma sentezleyicisi değildir. ÖTÜKEN'de PCM
 * çıkışı yok; elde yalnız PC hoparlörü var ve o tek kanal kare dalga
 * üretir. Kelimeleri "söyleyemez".
 *
 * NE YAPAR: metni Türkçe hece kurallarına göre böler, her heceyi kopuz
 * düzeninde bir notaya oturtur ve ezgiyi çalar. Sonuç, ozanın kopuz
 * eşliğinde söz söylemesinin sesli karşılığıdır: hece sayısı, vurgu
 * ve cümle sonu inişi duyulur.
 *
 * DÜZEN: Türk müziğinde yaygın beşli (pentatonik) dizi kullanılır —
 * kapalı hecelerde perde düşer, açık hecelerde yükselir; cümle sonunda
 * karar sesine iner. Soru cümlesi sonda yükselerek biter.
 * ===================================================================== */
#include "ozan_ses.h"
#include "ses.h"
#include "serial.h"
#include "ses_kilit.h"

/* Kopuz düzeni: La pentatonik (Hz). Karar sesi ilk derecedir. */
static const uint32_t PERDE[] = {
    220,  /* karar (La)  */
    247,  /* Si          */
    262,  /* Do          */
    294,  /* Re          */
    330,  /* Mi          */
    392,  /* Sol         */
    440   /* üst La      */
};
#define PERDE_SAYI ((int)(sizeof(PERDE) / sizeof(PERDE[0])))

/* Türkçe sesli harfler; UTF-8 çok baytlıları da tanınır. */
static int sesli_mi(const char *s, int *bayt)
{
    unsigned char c = (unsigned char)s[0];
    *bayt = 1;
    if (c < 0x80) {
        switch (c) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
        case 'A': case 'E': case 'I': case 'O': case 'U':
            return 1;
        default:
            return 0;
        }
    }
    /* İki baytlı: ı(0xC4 0xB1) İ(0xC4 0xB0) ö(0xC3 0xB6) Ö(0xC3 0x96)
     *             ü(0xC3 0xBC) Ü(0xC3 0x9C) â(0xC3 0xA2)              */
    if ((c & 0xE0) == 0xC0) {
        unsigned char d = (unsigned char)s[1];
        /* Denetim (1.4.6): yarim dizi (s[1] devam bayti degil, ornegin
         * NUL) iki bayt atlatip dizenin sonunu asiyordu. */
        if ((d & 0xC0) != 0x80) return 0;             /* *bayt = 1 */
        *bayt = 2;
        if (c == 0xC4 && (d == 0xB1 || d == 0xB0)) return 1;   /* ı İ */
        if (c == 0xC3 && (d == 0xB6 || d == 0x96)) return 1;   /* ö Ö */
        if (c == 0xC3 && (d == 0xBC || d == 0x9C)) return 1;   /* ü Ü */
        if (c == 0xC3 && (d == 0xA2 || d == 0x82)) return 1;   /* â Â */
        return 0;
    }
    if ((c & 0xF0) == 0xE0) {
        if (((unsigned char)s[1] & 0xC0) == 0x80 && ((unsigned char)s[2] & 0xC0) == 0x80) *bayt = 3;
        return 0;
    }
    return 0;
}

/* Basit ve yeterli hece sayacı: Türkçede hece sayısı = sesli sayısıdır.
 * Kapalı hece (sesliden sonra sessiz geliyorsa) daha uzun sürer. */
int ozan_hece_say(const char *metin)
{
    int n = 0, b;
    const char *p = metin;
    while (*p) {
        if (sesli_mi(p, &b)) n++;
        p += b;
    }
    return n;
}

/* Sözcük sonu / cümle sonu ayracı mı? */
static int ayrac_mi(char c)
{
    return c == ' ' || c == ',' || c == '.' || c == '!' || c == '?' ||
           c == ';' || c == ':' || c == '\n' || c == '\t';
}

static void ozan_soyle__ic(const char *metin, int hiz) {
    if (!metin || !ses_var()) return;
    if (hiz < 1) hiz = 1;

    int toplam = ozan_hece_say(metin);
    if (toplam == 0) return;

    int soru = 0;
    for (const char *q = metin; *q; q++) if (*q == '?') soru = 1;

    int derece = 2;          /* orta perdeden başla */
    int sayac  = 0;          /* kaçıncı hece        */
    const char *p = metin;
    int b;

    while (*p) {
        if (!sesli_mi(p, &b)) { p += b; continue; }

        /* Bu hece kapalı mı: sesliden sonra sessiz gelip söz bitiyorsa. */
        const char *q = p + b;
        int kapali = 0;
        if (*q && !ayrac_mi(*q)) {
            int b2;
            if (!sesli_mi(q, &b2)) kapali = 1;
        }
        int son_hece = (sayac == toplam - 1);

        /* Ezgi hareketi: açık hece yükseltir, kapalı hece düşürür.
         * Cümle sonunda karar sesine inilir; soru ise yükselir. */
        if (son_hece) derece = soru ? PERDE_SAYI - 2 : 0;
        else if (kapali) derece--;
        else derece++;

        if (derece < 0) derece = 0;
        if (derece >= PERDE_SAYI) derece = PERDE_SAYI - 1;

        uint32_t sure = (kapali ? 220u : 160u) / (uint32_t)hiz;
        if (son_hece) sure = 420u / (uint32_t)hiz;   /* karar uzar */
        ses_ton(PERDE[derece], sure);

        /* Sözcük arasında kısa soluk. */
        if (*q && ayrac_mi(*q)) ses_ton(0, 60 / (uint32_t)hiz);

        sayac++;
        p += b;
    }
    ses_bitir();
}

void ozan_selam(void)
{
    /* Dede Korkut'un giriş kopuzu: karardan çıkıp karara dönen kısa
     * bir açılış. Sözden önce çalınır. */
    static const uint32_t EZGI[][2] = {
        { 220, 180 }, { 262, 140 }, { 330, 180 }, { 294, 120 },
        { 262, 160 }, {   0,  80 }, { 220, 380 }
    };
    if (!ses_var()) return;
    for (int i = 0; i < (int)(sizeof(EZGI) / sizeof(EZGI[0])); i++)
        ses_ton(EZGI[i][0], EZGI[i][1]);
    serial_write("ozan: selam ezgisi calindi\n");
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
void ozan_soyle(const char *metin, int hiz) {
    ozkilit_al(&ses_kilit);
    ozan_soyle__ic(metin, hiz);
    ozkilit_birak(&ses_kilit);
}
