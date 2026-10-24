/* =====================================================================
 * DEDE KORKUT SESİ — UMAY'ın kendi konuşma sentezi (OSES).
 *
 * UMAY'ın yanıtını Dede Korkut söyler. Zincir tümüyle UMAY'ın içindedir:
 *   metin -> Türkçe sesletim (ses/oses/turkce.c) -> fonem kimlikleri
 *         -> VITS çizgesi (ses/oses/oses.c, model diskte /DEDE.SES)
 *         -> 22050 Hz ses -> 48000 Hz ikili kanal -> SYS_SES_PCM (AC'97)
 *
 * Bu dosya ve ses/oses/ SSE ile derlenir (çekirdek görev başına FXSAVE
 * saklar). UMAY'ın geri kalanı -mno-sse kalır; arayüzde kayan nokta
 * değeri geçmez.
 *
 * Bloklamaz: ses_soyle metni kuyruğa atar. Her UMAY turunda pompa en
 * çok BİR parça sentezler ve AC'97 halkasına sığdığı kadarını verir.
 * Model yoksa sürücü açılmaz; UMAY eski Dede sürücüsüne (çekirdekteki
 * formant sentezi) düşer ve bunu günlüğe yazar.
 * ===================================================================== */
#include "umay_ses.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "oses/oses.h"
#include "oses/oses_yigin.h"
#include "oses/turkce.h"
#include "oses/oses_ornekle.h"

#define DS_MODEL_YOL   "/DEDE.SES"     /* 8.3 ad: FAT16 suruculeri uzun ad okumaz */
#define DS_ALAN        (96ULL * 1024 * 1024)   /* motor çalışma alanı; tepe ~68 MB (80 bayt parça) */
#define DS_METIN       4096
#define DS_KIMLIK      4096
#define DS_ARA_KARE    (OSES_CIKIS_HZ / 5)        /* parçalar arası 0,2 sn sessizlik */
#define DS_GONDER      1024                    /* SYS_SES_PCM çağrı başına kare */

static struct {
    int       hazir;
    uint8_t  *model;
    void     *alan;
    ob_yigin_t yigin;
    oses_t    m;
    char      metin[DS_METIN];
    uint32_t  metin_n, imlec;                  /* kuyruk: [imlec, metin_n) söylenmedi */
    int64_t   kimlik[DS_KIMLIK];
    int16_t  *pcm;                             /* arenadan; gönderilince bırakılır */
    uint32_t  pcm_kare, pcm_giden;
    uint32_t  parca, soz_kare;
    int       soz_suruyor;
} D;

static void *ds_ayir(size_t n) { return ob_ayir(&D.yigin, n); }
static void  ds_birak(void *p) { ob_birak(&D.yigin, p); }

static int ds_ac(ses_surucu_t *s, uint32_t hz, uint32_t kanal) {
    (void)s; (void)hz; (void)kanal;
    if (D.hazir) return UM_OLDU;
    int64_t boy = up_boy(DS_MODEL_YOL);
    if (boy <= 0) { gun_yaz("DEDE-SES: %s yok, OSES acilmadi.", DS_MODEL_YOL); return UM_DESTEKSIZ; }
    uint64_t t0 = up_ms();
    D.model = (uint8_t *)up_ayir((uint64_t)boy);
    D.alan  = up_ayir(DS_ALAN);
    if (!D.model || !D.alan) { gun_yaz("DEDE-SES: bellek yetmedi (model %l bayt).", boy); return UM_YANLIS; }
    up_tut_t t = up_ac(DS_MODEL_YOL, UP_OKU);
    if (t == UP_GECERSIZ) return UM_YANLIS;
    int64_t okunan = 0;
    while (okunan < boy) {
        int64_t n = up_oku(t, D.model + okunan, (uint64_t)(boy - okunan));
        if (n <= 0) break;
        okunan += n;
    }
    up_kapat(t);
    if (okunan != boy) { gun_yaz("DEDE-SES: model eksik okundu (%l / %l).", okunan, boy); return UM_YANLIS; }
    if (ob_kur(&D.yigin, D.alan, DS_ALAN) != 0) return UM_YANLIS;
    oses_bellek_t bel = { ds_ayir, ds_birak };
    if (oses_yukle(&D.m, D.model, (size_t)boy, bel) != 0) {
        gun_yaz("DEDE-SES: model reddedildi: %s", D.m.hata);
        return UM_YANLIS;
    }
    D.hazir = 1;
    gun_yaz("DEDE-SES: model yuklendi boy=%l dugum=%u tensor=%u sure=%l ms",
            boy, D.m.dsay, D.m.tsay, (int64_t)(up_ms() - t0));
    return UM_OLDU;
}

static int ds_soyle(ses_surucu_t *s, const char *metin) {
    (void)s;
    if (!D.hazir || !metin || !*metin) return UM_YANLIS;
    /* söylenmiş kısmı at, yenisini sona ekle */
    if (D.imlec > 0) {
        uint32_t kalan = D.metin_n - D.imlec;
        for (uint32_t i = 0; i < kalan; i++) D.metin[i] = D.metin[D.imlec + i];
        D.metin_n = kalan; D.imlec = 0;
    }
    uint32_t n = um_uzun(metin);
    if (D.metin_n + n + 2 > DS_METIN) { gun_yaz("DEDE-SES: kuyruk dolu, soz atlandi."); return UM_YANLIS; }
    /* kanıt: söylenecek söz günlüğe (satır sonları '|'); sınama aynı sözü
     * ana makinede aynı motorla sentezleyip kartın çaldığıyla karşılaştırır */
    {
        char g[420]; uint32_t j = 0;
        for (uint32_t i = 0; i < n && j + 1 < sizeof g; i++) g[j++] = metin[i] == '\n' ? '|' : metin[i];
        g[j] = 0;
        gun_yaz("DEDE-SES: soz=%s~SON~", g);   /* son imi: seri satira baska cikti eklenebilir */
    }
    for (uint32_t i = 0; i < n; i++) D.metin[D.metin_n++] = metin[i];
    D.metin[D.metin_n++] = '\n';                /* cümle sınırı */
    D.metin[D.metin_n] = 0;
    if (!D.soz_suruyor) { D.soz_suruyor = 1; D.parca = 0; D.soz_kare = 0; }
    return UM_OLDU;
}

static int ds_pompa(ses_surucu_t *s) {
    (void)s;
    if (!D.hazir) return UM_DESTEKSIZ;
    /* 1) bekleyen sesi halkaya sığdığı kadar ver */
    while (D.pcm && D.pcm_giden < D.pcm_kare) {
        uint32_t n = D.pcm_kare - D.pcm_giden;
        if (n > DS_GONDER) n = DS_GONDER;
        int r = up_ses_pcm(D.pcm + 2 * (uint64_t)D.pcm_giden, n);
        if (r < 0) { gun_yaz("DEDE-SES: ses aygiti yok, parca atlandi."); D.pcm_giden = D.pcm_kare; break; }
        if (r == 0) return UM_OLDU;             /* halka dolu: sonraki turda */
        D.pcm_giden += (uint32_t)r;
    }
    if (D.pcm) { ds_birak(D.pcm); D.pcm = 0; }
    /* 2) sıradaki parçayı sentezle (turda en çok bir) */
    if (D.imlec >= D.metin_n) {
        if (D.soz_suruyor) {
            D.soz_suruyor = 0;
            gun_yaz("DEDE-SES: soz bitti parca=%u kare=%u", D.parca, D.soz_kare);
        }
        return UM_OLDU;
    }
    const char *p = D.metin + D.imlec;
    int ilk = 0;
    int k = tr_cumle(&p, D.kimlik, DS_KIMLIK, &ilk);
    D.imlec = (uint32_t)(p - D.metin);
    if (k <= 0) {
        if (k < 0) gun_yaz("DEDE-SES: cumle cok uzun, atlandi.");
        return UM_OLDU;
    }
    uint64_t t0 = up_ms();
    (void)ilk;                                 /* cümle başı ünlü: sesletim ː ekler (turkce.c) */
    D.m.bas_en_az = 0;
    float *ses = 0;
    long ns = oses_calistir(&D.m, D.kimlik, k, 0.4f, 1.2f, 0.3f, &ses);
    if (ns < 0) { gun_yaz("DEDE-SES: sentez hatasi: %s", D.m.hata); return UM_YANLIS; }
    uint32_t cn = oses_ornekle_boy(ns);
    D.pcm = (int16_t *)ds_ayir(((uint64_t)cn + DS_ARA_KARE) * 4);
    if (!D.pcm) { ds_birak(ses); gun_yaz("DEDE-SES: PCM icin bellek yok."); return UM_YANLIS; }
    cn = oses_ornekle(ses, ns, D.pcm);
    ds_birak(ses);
    for (uint32_t i = 0; i < DS_ARA_KARE * 2; i++) D.pcm[2 * cn + i] = 0;
    D.pcm_kare = cn + DS_ARA_KARE; D.pcm_giden = 0;
    D.parca++; D.soz_kare += D.pcm_kare;
    gun_yaz("DEDE-SES: parca=%u fonem=%d ornek=%l kare=%u sentez=%l ms tepe=%l KB",
            D.parca, k, (int64_t)ns, D.pcm_kare, (int64_t)(up_ms() - t0), (int64_t)(D.yigin.tepe / 1024));
    return UM_OLDU;
}

static ses_surucu_t g_dede_oses = {
    .ad = "dede-korkut-oses",
    .ac = ds_ac,
    .soyle = ds_soyle,
    .bosalt = ds_pompa
};

ses_surucu_t *ses_dede_surucu(void) { return &g_dede_oses; }
