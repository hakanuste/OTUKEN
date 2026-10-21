/* GÖZCÜ — dizge yoklaması. Süreçleri, kütüphaneyi, ayarları denetler.
 * Salt okunur; hiçbir şeyi değiştirmez. */
#include "umay_alp.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_bilgi.h"
#include "umay_ortak.h"

typedef struct {
    int  adim_no;
    int  surec_sayisi;
    int  cakilan;
    int  eksik_dosya;
    char rapor[UMAY_ILETI_MAX];
} gozcu_ic_t;

static gozcu_ic_t g_ic;

static const char *GEREKLI[] = { "/kut/umay", "/kut/umay/ayar.txt", 0 };

static int uygun(const gorev_t *g) { (void)g; return 1; }  /* tür maskesi yeter */

static int basla(alp_t *a, const gorev_t *g) {
    (void)g;
    um_bellek_doldur(&g_ic, 0, sizeof g_ic);
    a->ic = &g_ic;
    return UM_OLDU;
}

static alp_durum_t adim(alp_t *a) {
    gozcu_ic_t *ic = (gozcu_ic_t*)a->ic;
    if (a->gorev) a->gorev->ilerleme = ic->adim_no * 33;
    switch (ic->adim_no++) {
    case 0: {                                   /* süreçler */
        static up_surec_t dizi[32];
        int n = up_surec_listele(dizi, 32);
        ic->surec_sayisi = n < 0 ? 0 : n;
        for (int i = 0; i < ic->surec_sayisi; i++)
            if (dizi[i].durum == 3) ic->cakilan++;
        return ALP_KOSUYOR;
    }
    case 1:                                     /* dosyalar */
        for (int i = 0; GEREKLI[i]; i++)
            if (!up_var_mi(GEREKLI[i])) ic->eksik_dosya++;
        return ALP_KOSUYOR;
    case 2: {                                   /* rapor */
        char s[128];
        um_kopya(ic->rapor, sizeof ic->rapor, "");
        um_bicim(s, sizeof s, "Yurtta %d iş koşuyor, %d tanesi devrilmiş.\n",
                 ic->surec_sayisi, ic->cakilan);
        um_ekle(ic->rapor, sizeof ic->rapor, s);
        um_bicim(s, sizeof s, "Kütüphanede %d bitig var; %d gerekli iz eksik.\n",
                 bilgi_sayi(), ic->eksik_dosya);
        um_ekle(ic->rapor, sizeof ic->rapor, s);
        if (ic->cakilan == 0 && ic->eksik_dosya == 0)
            um_ekle(ic->rapor, sizeof ic->rapor, "Dizge sağlamdır, bir eksik görmedim.\n");
        else
            um_ekle(ic->rapor, sizeof ic->rapor, "Aksaklık var; ONARICI'yı çağırmak gerek.\n");
        return ALP_BITTI;
    }
    default: return ALP_BITTI;
    }
}

static void topla(alp_t *a, sonuc_t *s) {
    gozcu_ic_t *ic = (gozcu_ic_t*)a->ic;
    um_kopya(s->oz, sizeof s->oz, ic->rapor);
}

static alp_t g_gozcu = {
    .ad = "GÖZCÜ", .is = "dizge yoklaması",
    .alan = ALAN_YOK,
    .tur_maskesi = GT_BIT(GT_KONTROL) | GT_BIT(GT_IZLE),
    .kategori = { 0 },
    .yetki_gerek = YET_SUREC_GOR,
    .uygun_mu = uygun, .basla = basla, .adim = adim, .topla = topla,
    .durum = ALP_BOS
};
alp_t *alp_gozcu(void) { return &g_gozcu; }
