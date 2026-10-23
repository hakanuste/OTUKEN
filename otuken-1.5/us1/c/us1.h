/* =====================================================================
 * US1 — UMAY çıkarım motoru.
 *
 * ÖTÜKEN'de kayan nokta birimi ve SIMD KAPALIDIR. Bu motor bütün hesabı
 * 32/64 bit tamsayıyla yapar: ağırlıklar satır başına simetrik Q8
 * nicelenmiştir, etkinleşmeler 16.16 sabit noktadadır.
 *
 * KAPSAM: yalın dönüştürücü — RMSNorm, çok başlı nedensel dikkat,
 * SwiGLU ileri besleme, ayrık çıkış katmanı. Konumsal kodlama YOKTUR
 * (eğitici de kullanmıyor); RoPE eklenirse iki yerde değişir.
 *
 * DOĞRULAMA: arac/us1.py referans geçişiyle aynı sayıları üretmelidir.
 * Birebir eşitlik BEKLENMEZ — Q8 niceleme ve sabit nokta payı vardır.
 * ===================================================================== */
#ifndef US1_H
#define US1_H

#include <stdint.h>

#define US1_OLDU    0
#define US1_BOZUK  -1
#define US1_SURUM  -2

/* Sınırlar Ring3'te makul bellek için seçildi; model bunları aşarsa
 * yükleme REDDEDİLİR (sessizce kırpılmaz). */
#define US1_AZAMI_D        96
#define US1_AZAMI_KATMAN    4
#define US1_AZAMI_FFN     256
#define US1_AZAMI_SOZLUK  128
#define US1_AZAMI_BAGLAM   96

typedef struct {
    uint32_t satir, sutun;
    const uint8_t *veri;    /* satır başına: f32 ölçek + int8[sutun] */
} us1_tensor_t;

typedef struct {
    us1_tensor_t n1, wq, wk, wv, wo, n2, w1, w2, w3;
} us1_katman_t;

typedef struct {
    uint32_t d, katman, bas, ffn, sozluk, baglam;
    us1_tensor_t gomme, son_norm, cikis;
    us1_katman_t kat[US1_AZAMI_KATMAN];
} us1_model_t;

/* Başlığı çözer ve tensör yerlerini işaretler. Veriyi KOPYALAMAZ;
 * çağıranın tamponu motor kullanıldığı sürece yaşamalıdır. */
int      us1_yukle(us1_model_t *m, const uint8_t *veri, uint32_t boy);

/* İleri geçiş. mantik: T * sozluk değer, 16.16 sabit nokta. */
int      us1_ileri(us1_model_t *m, const uint32_t *imler, uint32_t T,
                   int32_t *mantik);

/* --- Artımlı üretim (KV önbelleği) ---
 * us1_ileri her çağrıda BÜTÜN diziyi baştan hesaplar: T imlik üretim
 * O(T^2 * parametre) iş demektir. us1_adim yalnız YENİ imi işler, önceki
 * konumların anahtar/değerlerini önbellekten okur. Sonuç us1_ileri'nin
 * aynı satırıyla BİT BİT aynıdır (aynı tamsayı işlemleri, aynı sıra);
 * us1_sina bunu sınar. Önbellek motor içinde tektir (tek iş parçacığı). */
#define US1_DOLU   -3
int      us1_sifirla(us1_model_t *m);                 /* konum = 0 */
uint32_t us1_konum(void);                              /* işlenen im sayısı */
/* im'i sıradaki konuma koyar, o konumun mantığını (sozluk değer) yazar.
 * Konum m->baglam'a ulaştıysa US1_DOLU: çağıran sıfırlayıp son imlerle
 * yeniden doldurur (konum kodlaması olmadığından kaydırma = yeniden hesap). */
int      us1_adim(us1_model_t *m, uint32_t im, int32_t *mantik);

/* Bir satır mantıktan im seçer. tohum NULL ise en yüksek değerli im
 * (belirlenimci); değilse olasılığa göre örneklenir. */
uint32_t us1_sec(const int32_t *mantik, uint32_t n, uint32_t *tohum);

/* Motorun gerektirdiği mantık tamponu büyüklüğü (değer sayısı). */
#define US1_MANTIK_BOY (US1_AZAMI_BAGLAM * US1_AZAMI_SOZLUK)

/* Bayt sözlüğüyle kodlama (UMAY ve uyum sınaması AYNI işlevi kullanır):
 * metnin her baytı sözlükteki sırasına eşlenir; sözlükte olmayan bayt
 * ATLANIR. Yazılan im sayısını döndürür (en çok 'azami'). */
uint32_t us1_kodla(const char *sozluk, uint32_t sozluk_boy, uint32_t model_sozluk,
                   const char *metin, uint32_t *imler, uint32_t azami);

#ifdef US1_IZ
/* --- YALNIZ SINAMA DERLEMESİ (-DUS1_IZ): işlem düzeyi iz ---
 * Üretim derlemesinde bu blok YOKTUR, motor hızı etkilenmez.
 * Her işlemden sonra çağrılır: islem adı, katman, baş (-1: yok), konum,
 * değerler (16.16) ve sayısı. tests/us1_conformance bunu kullanır. */
typedef void (*us1_iz_fn)(const char *islem, int katman, int bas, uint32_t konum,
                          const int32_t *deger, uint32_t n);
void    us1_iz_kur(us1_iz_fn f);
/* sabit nokta yardımcılarının dışa açılmış kopyaları (birim sınaması) */
int32_t us1_iz_carp(int32_t a, int32_t b);
int32_t us1_iz_bol(int32_t a, int32_t b);
int32_t us1_iz_kokk(int32_t x);
int32_t us1_iz_ustel(int32_t x);
int32_t us1_iz_silu(int32_t x);
void    us1_iz_softmax(int32_t *x, uint32_t n);
/* motorun bir satır ölçeği için İÇ gösterimi, 2^-32 biriminde */
int64_t us1_iz_olcek32(const us1_tensor_t *t, uint32_t satir);
#endif
#endif
