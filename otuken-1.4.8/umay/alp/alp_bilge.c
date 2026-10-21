/* BİLGE — kütüphaneden bağlam bulur, US motoruna verir, OZAN diliyle söyler. */
#include "umay_alp.h"
#include "umay_bilgi.h"
#include "umay_ozan.h"
#include "umay_us.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_arastirma.h"
#include "umay_kanit.h"


typedef struct {
    int      adim_no;
    gorev_t  gorev;
    baglam_t baglam[4];
    int        baglam_sayi;
    us_cikti_t cikti;
    char       rapor[GOREV_SONUC_MAX];
    arastirma_t ars;               /* ARAŞTIR: alanı olmayan iş -> genel depo */
} bilge_ic_t;

static bilge_ic_t g_ic;

static int uygun(const gorev_t *g) { (void)g; return 1; }

static int basla(alp_t *a, const gorev_t *g) {
    um_bellek_doldur(&g_ic, 0, sizeof g_ic);
    um_bellek_kopya(&g_ic.gorev, g, sizeof *g);
    a->ic = &g_ic;
    if (g->tur == GT_ARASTIR) ars_basla(&g_ic.ars, g->istem, 0, 0, 0);
    return UM_OLDU;
}

static alp_durum_t adim(alp_t *a) {
    bilge_ic_t *ic = (bilge_ic_t*)a->ic;

    int ars_kip = (ic->gorev.tur == GT_ARASTIR);
    if (ars_kip) {
        if (!ic->ars.bitti) {
            ars_adim(&ic->ars);
            if (a->gorev) a->gorev->ilerleme = ars_ilerleme(&ic->ars);
            return ALP_KOSUYOR;
        }
        ic->baglam_sayi = ars_dayanak(&ic->ars, ic->baglam, 3);
    } else if (ic->adim_no++ == 0) {
        int adet = (int)ayar_sayi("baglam_adet", 3);
        if (adet > 4) adet = 4;
        ic->baglam_sayi = kanit_dayanak_bul(ic->gorev.istem, 0, 0, ic->baglam, adet);
        return ALP_KOSUYOR;
    }

    /* US çıkarım katmanına devret — BİLGE de aynı çıktı sözleşmesini kullanır */
    us_derle_t istem;
    um_bellek_doldur(&istem, 0, sizeof istem);
    istem.kisilik      = ozan_kimlik();
    istem.soru         = ic->gorev.istem;
    istem.niyet        = ic->gorev.niyet;
    istem.tur          = (int)ic->gorev.tur;
    istem.bengi_baglam = ic->gorev.baglam[0] ? ic->gorev.baglam : 0;
    istem.oncul_baglam = ic->gorev.oncul_baglam[0] ? ic->gorev.oncul_baglam : 0;
    istem.baglam       = ic->baglam;
    istem.baglam_sayi  = ic->baglam_sayi;
    istem.nereden      = US_KENDI_ALANI;
    istem.uzman_ad     = "BİLGE";
    istem.uzman_is     = "bilgi bulma ve öğüt";
    istem.en_cok_im    = 256;

    us_cevap_uret(&istem, &ic->cikti);

    if (ic->gorev.niyet == NIYET_OGUT) {
        char ham[US_METIN_MAX];
        um_kopya(ham, sizeof ham, ic->cikti.metin);
        ozan_ogut(ham, ic->cikti.metin, sizeof ic->cikti.metin);
    }
    if (ars_kip) {
        static char ozet[GOREV_SONUC_MAX];
        us_cikti_yaz(&ic->cikti, ozet, sizeof ozet);
        ars_rapor(&ic->ars, "BİLGE", ic->rapor, sizeof ic->rapor);
        um_ekle(ic->rapor, sizeof ic->rapor, "Özet — ");
        um_ekle(ic->rapor, sizeof ic->rapor, ozet);
    } else {
        us_cikti_yaz(&ic->cikti, ic->rapor, sizeof ic->rapor);
    }
    return ALP_BITTI;
}

static const us_cikti_t *bilge_cikti(alp_t *a) {
    bilge_ic_t *ic = (bilge_ic_t *)a->ic;
    return &ic->cikti;
}

static void topla(alp_t *a, sonuc_t *s) {
    bilge_ic_t *ic = (bilge_ic_t*)a->ic;
    um_kopya(s->oz, sizeof s->oz, ic->rapor);
}

static alp_t g_bilge = {
    .ad = "BİLGE", .is = "bilgi bulma ve öğüt",
    .alan = ALAN_YOK,
    .tur_maskesi = GT_BIT(GT_SORU) | GT_BIT(GT_ANALIZ) | GT_BIT(GT_ARASTIR) | GT_BIT(GT_TASARLA),
    .kategori = { 0 },
    .yetki_gerek = 0,
    .uygun_mu = uygun, .basla = basla, .adim = adim, .topla = topla,
    .cikti_al = bilge_cikti,
    .durum = ALP_BOS
};
alp_t *alp_bilge(void) { return &g_bilge; }
