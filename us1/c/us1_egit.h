/* =====================================================================
 * US1 EĞİTİCİ (1.5) — UMAY'ın modelini ÖTÜKEN İÇİNDE eğitir.
 *
 * us1/arac/egit.py'nin ileri/geri geçişinin ve Adam adımının C'deki
 * BİREBİR karşılığı (float32): RMSNorm, nedensel çok başlı dikkat,
 * SwiGLU, konum kodlaması yok (v1). Yığın, tek dizilik parçaların
 * gradyanı biriktirilerek kurulur (bellek: bir dizinin ara değerleri).
 *
 * Bağımsızdır: libc/libm yok (exp/sqrt kendi). SSE ile derlenir;
 * ÖTÜKEN çekirdeği görev başına FXSAVE saklar.
 *
 * Doğruluk: tests/us1_egitim — gradyanlar egit.py'nin 'geri'siyle,
 * Adam adımı egit.py'ninkiyle, Q8 dışa aktarımı model_yaz ile bayt bayt.
 * ===================================================================== */
#ifndef US1_EGIT_H
#define US1_EGIT_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int V, d, L, H, F, T;           /* sözlük, boyut, katman, baş, ffn, bağlam */
    size_t n;                        /* parametre sayısı */
    float *w, *g, *m1, *m2;          /* ağırlık, gradyan, Adam momentleri */
    /* ofsetler (w içinde) */
    size_t o_gomme, o_son, o_cikis;
    size_t o_kat[8][9];              /* n1 wq wk wv wo n2 w1 w2 w3 */
    int adim;                        /* yapılan Adam adımı */
    /* tek dizinin ara değerleri */
    float *ara; size_t ara_n;
} us1e_t;

typedef void *(*us1e_ayir_fn)(size_t);

/* US1 v1 dosyasından (Q8 -> float; egit.py model_oku ile aynı) kurar. 0 / -1
 * ayir = 0: önceki kurulumun tamponları kullanılır (boylar aynı olmalı);
 * ağırlıklar dosyadan tazelenir, momentler ve gradyan sıfırlanır. */
int    us1e_kur(us1e_t *e, const uint8_t *dosya, uint32_t boy, us1e_ayir_fn ayir);
/* Bir dizi: ileri + geri, gradyan BİRİKİR. x,y: T im; a: konum ağırlığı
 * (0 = hepsi 1). wt: YIĞININ toplam ağırlığı (ortalama kaybın paydası).
 * Dönen: bu dizinin ağırlıklı kayıp toplamı (sum lp*a). */
double us1e_ileri_geri(us1e_t *e, const int *x, const int *y, const float *a, int T, double wt);
/* Yalnız ileri: ağırlıklı kayıp toplamı (doğrulama). */
double us1e_kayip(us1e_t *e, const int *x, const int *y, const float *a, int T);
/* Adam adımı (egit.py: kırpma 1.0, b1 .9, b2 .99, matrislere 0.01 sönüm);
 * gradyanlar sıfırlanır. Dönen: kırpma öncesi gradyan normu. */
double us1e_adim(us1e_t *e, float lr);
/* ince ayar: momentumlu SGD (kırpma 1.0, sönüm yok). Dönen: gradyan normu. */
double us1e_adim_sgd(us1e_t *e, float lr);
/* egit.py öğrenme hızı çizelgesi: ısınma + kosinüs */
float  us1e_oran(int t, int isinma, int plan, float tepe);
/* Q8'e yazar (model_yaz v1 ile bayt bayt aynı). Yazılan bayt / -1 */
long   us1e_yaz(const us1e_t *e, uint8_t *c, size_t azami);
/* 1.5: İŞLEVİ KORUYAN büyütme. dst: yeni_L katman (en sona eklenir), yeni_F
 * ffn. Yeni katmanın çıkışları (wo, w2) ve yeni ffn birimlerinin w2
 * sütunları SIFIR: büyümüş model aynı girdide AYNI kaybı verir (eklenen
 * terimler tam sıfır). Diğer yeni ağırlıklar ~N(0, 1/girdi) (us1.py ile
 * aynı ölçek). Momentler sıfır. 0 / -1 */
int    us1e_buyut(us1e_t *dst, const us1e_t *src, int yeni_L, int yeni_F, us1e_ayir_fn ayir, uint32_t tohum);
/* yazılacak dosya boyu */
size_t us1e_dosya_boy(const us1e_t *e);
#endif
