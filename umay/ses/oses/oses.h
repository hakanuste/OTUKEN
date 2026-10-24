/* =====================================================================
 * OSES — ÖTÜKEN ses motoru: Piper/VITS çizgesini çalıştıran yalın yorumlayıcı.
 *
 * Model, ana makinede arac/ses_cevir.py ile ONNX'ten OSES biçimine
 * çevrilir. Motor yalnız bu çizgenin kullandığı 50 işlemi bilir; her işlem
 * onnxruntime'ın sonucuyla düğüm düğüm karşılaştırılarak sınanır
 * (arac/karsilastir.py).
 *
 * Bağımsızdır: libc/libm kullanmaz (matematik oses_mat.c'de). Bellek,
 * çağıranın verdiği ayır/birak işlevleriyle alınır; ana makinede malloc,
 * UMAY'da UMAY'ın kendi yığını.
 * ===================================================================== */
#ifndef OSES_H
#define OSES_H
#include <stdint.h>
#include <stddef.h>

enum { OD_F32 = 1, OD_I64 = 2, OD_BOOL = 3 };
#define OSES_AZAMI_BOYUT 6

typedef struct {
    uint32_t sayac;          /* başvuru sayısı (Reshape/Squeeze veriyi paylaşır) */
    uint8_t  sabit;          /* model verisine bakar: bırakılmaz */
    void    *v;
} oses_tampon_t;

typedef struct {
    uint8_t  dt, nd;
    int64_t  d[OSES_AZAMI_BOYUT];
    int64_t  n;              /* öğe sayısı */
    oses_tampon_t *b;        /* 0: tensör henüz yok */
} oses_tensor_t;

typedef struct {
    uint16_t op;
    uint8_t  nin, nout;
    const int32_t *in, *out;
    int64_t  ia[16];
    float    fa[4];
} oses_dugum_t;

typedef struct {
    void *(*ayir)(size_t);
    void  (*birak)(void *);
} oses_bellek_t;

typedef struct oses_model {
    oses_bellek_t bel;
    uint32_t tsay, dsay, gsay, csay;
    const int32_t *giris, *cikis;
    oses_tensor_t *t;            /* çalışma anındaki tensörler */
    oses_tensor_t *sabit;        /* modeldeki sabitler (t'ye kopyalanır) */
    oses_dugum_t  *dugum;
    int32_t       *son_kullanim; /* her tensörün son okunduğu düğüm (-1: hiç) */
    uint64_t       tohum;        /* RandomNormalLike üreteci */
    int            bas_en_az;    /* ilk sesin en az kaç çerçeve (256 örnek) süreceği; 0: dokunma.
                                    Model cümle başındaki ünlüyü yutuyor ("Ötüken" -> "tüken") */
    /* sınama kancaları (0 olabilir) */
    void (*izle)(struct oses_model *, int dugum, void *baglam);
    int  (*gurultu)(struct oses_model *, int tensor, oses_tensor_t *cikti, void *baglam);
    void  *baglam;
    char   hata[96];
} oses_t;

/* model ikilisini çözer (veri belleği oses_t yaşadıkça durmalı). 0 / -1 */
int  oses_yukle(oses_t *m, const uint8_t *veri, size_t boy, oses_bellek_t bel);
/* ses üret: ids (fonem kimlikleri), ölçekler (gürültü, uzunluk, gürültü_w).
 * *ses çağıranın bellek işleviyle ayrılır; örnek sayısı döner, hata -1 */
long oses_calistir(oses_t *m, const int64_t *ids, int n, float olcek_gurultu,
                   float olcek_uzunluk, float olcek_gurultu_w, float **ses);
void oses_birak(oses_t *m);
#endif
