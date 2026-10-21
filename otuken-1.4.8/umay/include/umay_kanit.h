/* =====================================================================
 * KANIT — UMAY karar katmanı: "kanıt yeterli mi?"
 *
 *   Kullanıcı sorusu -> terimler -> BİLGİ/BENGİ/ARAŞTIRMA dayanağı
 *     -> kanıt ölçüsü -> YETERLİ ise cevap, DEĞİLSE güvenle "bilmiyorum"
 *
 * Genel bir ölçüdür; soru ya da konu listesi İÇERMEZ. Atılan sözcükler
 * yalnız DİLBİLGİSEL soru/buyruk sözcükleridir (nedir, nasıl, anlat...).
 *
 *   ağırlık  : terimin kütüphanedeki seyrekliği (IDF benzeri basamak):
 *              hiç geçmeyen terim en ağırdır -> alan dışı işaretidir.
 *   kapsama  : dayanakta geçen terimlerin ağırlıklı payı (0..100).
 *              Eşleme (kanit_esles): aynı sözcük; ya da ikisi de >= 5 harfse
 *              ilk 5 harf aynı; ya da kısa olan 3-4 harfse uzun olan onunla
 *              başlayıp ünlüyle başlayan <= 2 harflik ek alır. Son kök
 *              harfinde ünsüz yumuşaması serbest (harç~harcı, kireç~kireci).
 *   odak     : en ağır terim; dayanakta GEÇMELİ.
 *   yüklem   : Türkçe soru sonu yüklemdir (SOV): SON terim sorunun EN SON
 *              sözcüğüyse (ardından yalnız soru eki: mı/mi/mu/mü...),
 *              kütüphanede hiç geçmiyorsa ve çekimli eylem ekiyle bitiyorsa
 *              ("durur", "kalır", "sıvanır", "yağacak mı") konu değil
 *              yüklem adayıdır: odak
 *              OLAMAZ ve ağırlığı KANIT_YUKLEM_AGIRLIK'tır. Ek listesi
 *              dilbilgisinden gelir (geniş/şimdiki/gelecek/geçmiş zaman,
 *              gereklilik, yeterlik, olumsuz); konu sözcüğü içermez.
 *   eylemsi  : kütüphanede geçmeyen, ulaç ekli terim ("örerken", "kullanarak")
 *              nerede olursa olsun eylemdir: odak olamaz, ağırlığı yüklemle aynı.
 *   yeterli  : dayanak var && kapsama >= KANIT_KAPSAMA_ESIK && odak kapsandı.
 *
 * Model sözünün dayanağa bağlılığı (kanit_bagliligi) ayrı ölçülür: sözün
 * içerik sözcüklerinin çoğu dayanakta geçmiyorsa söz ATILIR ve kaynaktaki
 * en ilgili cümle aynen verilir.
 * ===================================================================== */
#ifndef UMAY_KANIT_H
#define UMAY_KANIT_H
#include "umay_bilgi.h"

#define KANIT_TERIM_MAX      8
/* Eşik: tests/umay_kanit geliştirme kümesinde seçildi (bkz. belge). */
#define KANIT_KAPSAMA_ESIK   85
#define KANIT_BAGLILIK_ESIK  60   /* bkz. tests/umay_kanit/baglilik_esik.py */
#define KANIT_YUKLEM_AGIRLIK 40   /* bkz. tests/umay_kanit (geliştirme kümesi) */
#define KANIT_KAYNAK_MAX     16   /* ayrı kaynak sayımı için üst sınır */
#define KANIT_GENEL_AGIRLIK  60   /* kayıtların 1/4'ünden çoğunda geçen terim */

typedef struct {
    char terim[KANIT_TERIM_MAX][24];
    int  terim_sayi;
    int  agirlik[KANIT_TERIM_MAX];
    int  df[KANIT_TERIM_MAX];         /* kütüphanede terimi taşıyan kayıt */
    int  kapsandi[KANIT_TERIM_MAX];
    int  eylem[KANIT_TERIM_MAX];      /* yüklem ya da eylemsi: odak olamaz */
    int  yuklem;             /* yüklem adayı son terimin sırası, yoksa -1 */
    int  dayanak_var;
    int  kapsama;            /* 0..100, ağırlıklı */
    int  odak;               /* en ağır terimin sırası, yoksa -1 */
    int  odak_kapsandi;
    int  kaynak_sayi;        /* en az bir terimi taşıyan ayrı dayanak */
    int  yeterli;
} kanit_t;

/* Sorudan içerik terimleri (durak + soru/buyruk sözcükleri atılır). */
int  kanit_terimleri(const char *soru, char cikan[][24], int en_cok);

/* İki sözcük aynı kökten mi (yukarıdaki eşleme). */
int  kanit_esles(const char *a, const char *b);

/* Kanıtı ölçer. ek_metin: doğrulanmış BENGİ bağlamı ya da NULL. */
void kanit_degerlendir(const char *soru, const baglam_t *dayanak, int n,
                       const char *ek_metin, kanit_t *k);

/* Metnin içerik sözcüklerinin dayanakta geçen payı (0..100); içerik
 * sözcüğü yoksa 100. */
int  kanit_bagliligi(const char *metin, const baglam_t *dayanak, int n,
                     const char *ek_metin);

/* Metinden, sorunun terimlerini en AĞIR taşıyan tam cümle (kırpıntı < 15
 * bayt seçilmez). atla[t] != 0 olan terimler puana katılmaz (ikinci cümle
 * seçiminde: ilk cümlenin karşıladıkları). tutan: cümlenin karşıladığı
 * terimler (NULL olabilir). Dönüş: puan (ağırlık toplamı); cümle yoksa -1. */
int  kanit_cumle_sec(const kanit_t *k, const int *atla, const char *metin,
                     char *c, uint32_t n, int *tutan);

/* Sözcük çekimli eylem ekiyle mi bitiyor (yalnız biçim; >= 4 harf). */
int  kanit_eylem_ekli(const char *s);
/* Sözcük eylemsi (ulaç: -rken, -ArAk) ekiyle mi bitiyor (>= 5 harf). */
int  kanit_eylemsi_ekli(const char *s);

/* MECLİS: bir uzmanın payı var mı — dayanağı, sorunun genel olmayan
 * (kayıtların 1/4'ünden azında geçen), eylem olmayan en az bir terimini
 * karşılıyor mu. Uzman yalnız payına düşeni söyler; bütünün yeterliliğine
 * birleşik kanıt (kanit_birlestir) karar verir. */
int  kanit_pay_var(const kanit_t *k);
/* Aynı sorunun kanıtlarını birleştirir: karşılanan terimlerin birleşimi,
 * kapsama/odak/yeterli yeniden hesaplanır. */
void kanit_birlestir(kanit_t *hedef, const kanit_t *k);

/* Kütüphanede terimi (kanit_esles ile) taşıyan kayıt sayısı. */
int  kanit_belge_sikligi(const char *terim);

/* Terimin ağırlığı (yukarıdaki basamak; kütüphane büyüklüğüne göre). */
int  kanit_agirligi(const char *terim);

/* Kanıt kipinde dayanak arama: yalnız sorunun TERİMLERİyle, kök eşlemeli
 * (kanit_esles), ağırlık toplamıyla sıralı. kategoriler NULL: bütün depo. */
int  kanit_dayanak_bul(const char *soru, const char *const *kategoriler, int kat_sayi,
                       baglam_t *cikan, int en_cok);
#endif
