/* YAZMAN — dosya işleri ve kalıcı belleğe yazım. Yazma yetkisi ister. */
#include "umay_alp.h"
#include "umay_bengi.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_onay.h"

typedef struct { int adim_no; gorev_t gorev; char rapor[UMAY_ILETI_MAX]; } yazman_ic_t;
static yazman_ic_t g_ic;

static int uygun(const gorev_t *g) { (void)g; return 1; }

static int basla(alp_t *a, const gorev_t *g) {
    um_bellek_doldur(&g_ic, 0, sizeof g_ic);
    um_bellek_kopya(&g_ic.gorev, g, sizeof *g);
    a->ic = &g_ic;
    return UM_OLDU;
}

static alp_durum_t adim(alp_t *a) {
    yazman_ic_t *ic = (yazman_ic_t*)a->ic;
    const char *soz = ic->gorev.istem;

    if (um_icinde(soz, "kaydet") || um_icinde(soz, "not al")) {
        char anahtar[32];
        um_bicim(anahtar, sizeof anahtar, "not%d", bengi_sayi() + 1);
        bengi_yaz(anahtar, soz);
        if (up_yetkim_var_mi(YET_DOSYA_YAZ)) {
            bengi_sakla();
            um_bicim(ic->rapor, sizeof ic->rapor,
                     "Sözünü bengi taşa kazıdım: %s\n", anahtar);
        } else {
            um_bicim(ic->rapor, sizeof ic->rapor,
                     "Sözünü belleğime aldım (%s) ama taşa kazımaya yetkim yok; "
                     "yazma yetkisi verilmeli.\n", anahtar);
        }
        return ALP_BITTI;
    }

    if (um_icinde(soz, "listele")) {
        um_kopya(ic->rapor, sizeof ic->rapor, "Belleğimde duranlar:\n");
        char oz[1024];
        bengi_ozet(oz, sizeof oz);
        um_ekle(ic->rapor, sizeof ic->rapor, oz[0] ? oz : "(bomboş)\n");
        return ALP_BITTI;
    }

    um_kopya(ic->rapor, sizeof ic->rapor,
             "Bu buyruğu tanımadım. \"kaydet\" ya da \"listele\" diyebilirsin.\n");
    return ALP_BITTI;
}

static void topla(alp_t *a, sonuc_t *s) {
    yazman_ic_t *ic = (yazman_ic_t*)a->ic;
    um_kopya(s->oz, sizeof s->oz, ic->rapor);
}

static alp_t g_yazman = {
    .ad = "YAZMAN", .is = "bitig ve bellek işleri",
    .alan = ALAN_YOK,
    .tur_maskesi = GT_BIT(GT_UYGULA),
    .kategori = { 0 },
    .yetki_gerek = 0,
    .uygun_mu = uygun, .basla = basla, .adim = adim, .topla = topla,
    .durum = ALP_BOS
};
alp_t *alp_yazman(void) { return &g_yazman; }
