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

/* Bir satır mantıktan im seçer. tohum NULL ise en yüksek değerli im
 * (belirlenimci); değilse olasılığa göre örneklenir. */
uint32_t us1_sec(const int32_t *mantik, uint32_t n, uint32_t *tohum);

/* Motorun gerektirdiği mantık tamponu büyüklüğü (değer sayısı). */
#define US1_MANTIK_BOY (US1_AZAMI_BAGLAM * US1_AZAMI_SOZLUK)
#endif
