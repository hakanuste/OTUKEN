/* =====================================================================
 * BİLGİ — UMAY belge deposu (kaynak doküman + RAG).
 *
 * DEPO STANDARDI
 *   Kök: /kut/umay/
 *   12 uzman alanı + ortak havuz, her biri kendi dizininde:
 *     akca yer-su kun demir tulga otaci yargu balbal alpagu kopuz oyun yazit
 *     ortak            (birden çok alanı ilgilendiren belgeler)
 *
 *   KATEGORİ DOSYA YOLUNDAN ÇIKARILMAZ. Her belgenin başındaki üstveri
 *   esastır:
 *       #kategori: balbal
 *       #baslik:   Horasan harcı karışımı
 *       #etiket:   harc,sarnic
 *       #kaynak:   Usta notları 1998
 *       <boş satır>
 *       ...gövde...
 *   Üstveri yoksa belge "ortak" havuzuna düşer ve günlüğe yazılır.
 * ===================================================================== */
#ifndef UMAY_BILGI_H
#define UMAY_BILGI_H
#include "umay.h"

#define BILGI_KAYIT_MAX   96
#define BILGI_KELIME_MAX  96
#define BILGI_ICERIK_MAX  3072
#define BILGI_ALINTI_MAX  800
#define BILGI_ETIKET_MAX  4
#define BILGI_ALAN_SAYI   13          /* 12 uzman alanı + ortak */

/* Kaynak türü — BENGİ kaydı ile kaynak belge asla karışmasın diye. */
typedef enum {
    KT_DOKTRIN = 0,   /* ikiliye gömülü proje veri tabanı */
    KT_BELGE,         /* /kut/umay altındaki dosya        */
    KT_DIS,           /* çalışırken dışarıdan katılan     */
    KT_BENGI          /* BENGİ kaydı — BİLGİ'ye GİREMEZ   */
} kaynak_tur_t;

typedef struct {
    char kategori[32];
    char baslik[96];
    char kaynak[96];                     /* dosya yolu ya da kaynak adı */
    kaynak_tur_t kaynak_tur;
    char etiket[BILGI_ETIKET_MAX][24];
    int  etiket_sayi;
    char alinti[BILGI_ALINTI_MAX];
    int  skor;
} baglam_t;

typedef struct {
    const char *const *kategoriler;
    int         kategori_sayi;
    const char *etiket;
    const char *kaynak;
    int         onek;     /* 1: sorgu sözcüğü kayıt sözcüğünün BAŞI olsun
                           * (kök araması: "sarnı" -> sarnıç, sarnıcı) */
    /* Kanıt kipi (ikisi de isteğe bağlı; verilmezse eski davranış):
     *   esles  : sorgu sözcüğü ile kayıt sözcüğü eşleşmesi (onek'i ezer)
     *   agirlik: sorgu sözcüğünün ağırlığı; verilirse kayıtlar eşleşen
     *            sözcüklerin AĞIRLIK TOPLAMIyla sıralanır (baglam_t.skor
     *            yine eşleşen SÖZCÜK SAYISIdır) ve alıntı en ağır
     *            eşleşen sözcüğün çevresinden alınır. */
    int       (*esles)(const char *sorgu, const char *kayit);
    int       (*agirlik)(const char *sorgu);
} bilgi_suzgec_t;

/* Çatışma: aynı kategoride, ayrı kaynaklardan, yakın skorlu iki kayıt.
 * Sessizce birleştirilmez; OTAĞ'a bildirilir. */
typedef struct {
    int  var;
    char kategori[32];
    char kaynak_a[96];
    char kaynak_b[96];
    char baslik_a[96];
    char baslik_b[96];
} catisma_t;

const char *bilgi_alan_kategorisi(alan_t a);   /* ALAN_TULGA -> "tulga" */
const char *bilgi_alan_dizini(int i);          /* 0..12 depo dizin adları */

int  bilgi_dizin_kur(const char *kok);         /* 13 alt dizini açar */
int  bilgi_doktrin_yukle(void);
int  bilgi_tara(const char *kok);              /* üstveriye göre indeksler */
int  bilgi_sayi(void);
int  bilgi_kategori_sayisi(const char *kategori);

int  bilgi_kayit_kat(const char *kategori, const char *baslik, const char *kaynak,
                     const char *etiketler, const char *icerik);
int  bilgi_kayit_kat_tur(const char *kategori, const char *baslik, const char *kaynak,
                         kaynak_tur_t kt, const char *etiketler, const char *icerik);

int  bilgi_baglam_bul(const char *soru, baglam_t *cikan, int en_cok);
int  bilgi_baglam_bul_kat(const char *soru, const char *const *kategoriler,
                          int kat_sayi, baglam_t *cikan, int en_cok);
int  bilgi_baglam_bul_suzgec(const char *soru, const bilgi_suzgec_t *s,
                             baglam_t *cikan, int en_cok);
int  bilgi_catisma_bul(const baglam_t *dizi, int sayi, catisma_t *c);
/* Metindeki sıradaki sözcük (küçük harfli, <= 23 bayt, durak ayıklanmaz);
 * sözcük kalmadıysa 0. Dönüş: sözcükten sonraki konum. */
const char *bilgi_sonraki_kelime(const char *p, char kelime[24]);
/* Metinden küçük harfli, durak sözcüğü ayıklanmış benzersiz sözcükler. */
int  bilgi_kelimeler(const char *metin, char cikan[][24], int en_cok);
/* Eşleme işleviyle sözcüğü taşıyan kayıt sayısı (belge sıklığı). */
int  bilgi_siklik(const char *kelime, int (*esles)(const char *, const char *));
#endif
