#include "us1.h"

/* =====================================================================
 * SAYISAL TASARIM (1.4.6 — uyum sınamasının bulduğu kök nedenlere göre)
 *
 * Etkinleşmeler 16.16 (int32). Ara hesaplar GENİŞ tutulur ve tek sefer
 * YUVARLANIR (kesilmez):
 *   - Q8 satır ölçeği 2^-32 biriminde (int64, yuvarlanmış). Eskiden 16.16'ya
 *     KESİLİYORDU: ölçekler ~100-560 birim olduğundan her satır %0.2-1.5
 *     küçülüyor, hata katmanlarda birikip mantıkta ~0.3'e ulaşıyordu.
 *   - Nokta çarpımları tam biriktirilip bir kez kaydırılır (terim başına
 *     kesme -0.5 birim yanlılık getiriyordu).
 *   - RMSNorm ortalamayı 2^-32 biriminde alır; eps tam 1e-5 (Python ile aynı).
 *   - ustel: 8 terimli seri, taşmada DOYAR (eskiden işaretli kaydırma
 *     taşması = tanımsız davranış, x > ~10.4).
 *   - silu: taşmasız sigmoid (e^-|x| hep <= 1).
 * Doğrulama: tests/us1_conformance (işlem düzeyinde float64 referans).
 * ===================================================================== */

#ifdef US1_IZ
static us1_iz_fn g_iz = 0;
void us1_iz_kur(us1_iz_fn f) { g_iz = f; }
#define IZ(op, L, b, t, v, n) do { if (g_iz) g_iz((op), (int)(L), (int)(b), (t), (v), (n)); } while (0)
#else
#define IZ(op, L, b, t, v, n) do { } while (0)
#endif

__extension__ typedef __int128 i128;

#define KESIR  16
#define BIR    (1 << KESIR)

static int32_t doyur32(int64_t v)
{
    if (v > 0x7FFFFFFFLL) return 0x7FFFFFFF;
    if (v < -0x7FFFFFFFLL) return -0x7FFFFFFF;
    return (int32_t)v;
}

/* sağa kaydır, en yakına yuvarla (s >= 1) */
static int64_t yuv64(int64_t v, int s)   { return (v + ((int64_t)1 << (s - 1))) >> s; }
static i128    yuv128(i128 v, int s)     { return (v + ((i128)1 << (s - 1))) >> s; }

static int64_t doyur64_128(i128 v)
{
    const i128 ust = (i128)0x3FFFFFFFFFFFFFFFLL;
    if (v > ust) return (int64_t)ust;
    if (v < -ust) return -(int64_t)ust;
    return (int64_t)v;
}

/* en yakın tamsayı karekök (64 bit) */
static uint64_t kok64(uint64_t v)
{
    if (v == 0) return 0;
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    /* r = taban(kök), v = kalan = orijinal - r^2; kalan > r ise r+1 daha yakın */
    return (v > r) ? r + 1 : r;
}

#ifdef US1_IZ   /* carp, bol, kokk yalnız sabit nokta birim sınamasında kullanılır */
static int32_t carp(int32_t a, int32_t b)
{
    return doyur32(yuv64((int64_t)a * (int64_t)b, KESIR));
}

static int32_t bol(int32_t a, int32_t b)
{
    if (b == 0) return 0;
    int64_t pay = (int64_t)a * BIR, yarim = (b > 0 ? (int64_t)b : -(int64_t)b) / 2;
    /* payı |b|/2 kadar sıfırdan uzaklaştırıp kesmek = en yakına yuvarlamak */
    return doyur32((pay >= 0 ? pay + yarim : pay - yarim) / b);
}

/* Karekök, 16.16 giriş ve çıkış (en yakına yuvarlanmış). */
static int32_t kokk(int32_t x)
{
    if (x <= 0) return 0;
    return doyur32((int64_t)kok64((uint64_t)x << KESIR));
}
#endif

/* e^x, 16.16. x = n*ln2 + r (0 <= r < ln2); e^r 2^-30 biriminde 8 terimli
 * seriyle; sonuç 16.16'yı aşarsa 0x7FFFFFFF'e DOYAR. */
/* e^x = e^r * 2^n; e^r'yi 2^-30 biriminde (8 terim) ve n'yi döndürür. */
static int64_t ustel_parca(int32_t x, int64_t *n_cik)
{
    const int64_t LN2_30 = 744261118LL;          /* ln2 * 2^30 */
    int64_t x30 = (int64_t)x * 16384;            /* 2^-30 birimi (negatifi kaydırma: UB) */
    int64_t n = x30 / LN2_30;
    if (x30 - n * LN2_30 < 0) n--;               /* taban bölme */
    int64_t r = x30 - n * LN2_30;                /* [0, ln2) */
    int64_t terim = (int64_t)1 << 30, top = terim;
    for (int k = 1; k <= 8; k++) {
        terim = yuv64(terim * r, 30) / k;
        top += terim;
    }
    *n_cik = n;
    return top;                                  /* [2^30, 2^31) */
}

static int32_t ustel(int32_t x)
{
    int64_t n, top = ustel_parca(x, &n);
    if (n >= 15) return 0x7FFFFFFF;              /* e^x >= 2^15: 16.16'ya sığmaz */
    /* e^x * 2^16 = top * 2^(n-14) */
    int s = (int)n - 14;
    if (s >= 0) return doyur32(top << s);        /* s = 0 en çok (n <= 14) */
    if (-s >= 62) return 0;
    return doyur32(yuv64(top, -s));
}

/* e^x, x <= 0, 2^-30 biriminde (sigmoid için; 16.16'dan 2^14 kat ince) */
static int64_t ustel30_negatif(int32_t x)
{
    int64_t n, top = ustel_parca(x, &n);         /* n <= 0 */
    if (-n >= 62) return 0;
    return (n == 0) ? top : yuv64(top, (int)-n);
}

static uint32_t oku32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* f32 -> 2^-32 biriminde, yuvarlanmış, doygun. Kayan nokta BİRİMİ
 * kullanılmaz; bitler elle çözülür. NaN/Inf ve alt-normal sıfır sayılır
 * (yükleyici bunları zaten reddeder). */
static int64_t f32_q32(uint32_t bit)
{
    int isaret = (int)((bit >> 31) & 1u);
    int ust = (int)((bit >> 23) & 0xFFu);
    if (ust == 0 || ust == 0xFF) return 0;
    int64_t taban = (int64_t)((bit & 0x7FFFFFu) | 0x800000u);   /* < 2^24 */
    int kaydir = ust - 127 - 23 + 32;
    int64_t v;
    if (kaydir >= 0) v = (kaydir > 38) ? ((int64_t)1 << 62) : (taban << kaydir);
    else v = (-kaydir >= 62) ? 0 : yuv64(taban, -kaydir);
    return isaret ? -v : v;
}

/* Tensörü çözer ve DOĞRULAR: biçim (satir x sutun) modelin boyutlarıyla
 * birebir tutmalı (eskiden denetlenmiyordu: biçimi bozuk bir wq, matcarp'ın
 * durağan tamponların dışına yazmasına yol açıyordu — tests/us1_fuzz);
 * her satır ölçeği sonlu, >= 0 ve <= 2^16 olmalı (NaN/Inf/negatif red). */
static const uint8_t *tensor_coz(const uint8_t *p, const uint8_t *son,
                                 us1_tensor_t *t, uint32_t satir, uint32_t sutun,
                                 int *hata)
{
    if (*hata) return son;
    if ((uint64_t)(son - p) < 8) { *hata = 1; return son; }
    t->satir = oku32(p);
    t->sutun = oku32(p + 4);
    p += 8;
    if (t->satir != satir || t->sutun != sutun) { *hata = 1; return son; }
    uint64_t gerek = (uint64_t)t->satir * (4u + (uint64_t)t->sutun);
    if ((uint64_t)(son - p) < gerek) { *hata = 1; return son; }
    t->veri = p;
    for (uint32_t i = 0; i < t->satir; i++) {
        uint32_t bit = oku32(p + (uint64_t)i * (4u + t->sutun));
        uint32_t ust = (bit >> 23) & 0xFFu;
        if (ust == 0xFF) { *hata = 1; return son; }                  /* NaN / Inf */
        if ((bit >> 31) && (bit & 0x7FFFFFFFu)) { *hata = 1; return son; }   /* negatif */
        if (ust > 127 + 16) { *hata = 1; return son; }               /* > 2^16 (doygunluk) */
    }
    return p + gerek;
}

static int64_t satir_olcek32(const us1_tensor_t *t, uint32_t satir)
{
    return f32_q32(oku32(t->veri + (uint64_t)satir * (4u + t->sutun)));
}

static const int8_t *satir_veri(const us1_tensor_t *t, uint32_t satir)
{
    return (const int8_t *)(t->veri + (uint64_t)satir * (4u + t->sutun) + 4);
}

int us1_yukle(us1_model_t *m, const uint8_t *veri, uint32_t boy)
{
    if (!m || !veri || boy < 48) return US1_BOZUK;
    if (!(veri[0] == 'U' && veri[1] == 'S' && veri[2] == '1' && veri[3] == 0))
        return US1_BOZUK;
    uint32_t surum = oku32(veri + 4);
    if (surum != US1_SURUM_V1 && surum != US1_SURUM_V2) return US1_SURUM;
    m->surum = surum;
    m->bayrak = 0; m->rope_taban = 0; m->rope_sutun = 0; m->rope_tablo = 0;

    m->d      = oku32(veri + 8);
    m->katman = oku32(veri + 12);
    m->bas    = oku32(veri + 16);
    m->ffn    = oku32(veri + 20);
    m->sozluk = oku32(veri + 24);
    m->baglam = oku32(veri + 28);

    if (m->d == 0 || m->d > US1_AZAMI_D) return US1_BOZUK;
    if (m->katman == 0 || m->katman > US1_AZAMI_KATMAN) return US1_BOZUK;
    if (m->bas == 0 || m->bas > m->d || m->d % m->bas) return US1_BOZUK;
    if (m->ffn == 0 || m->ffn > US1_AZAMI_FFN) return US1_BOZUK;
    if (m->sozluk == 0 || m->sozluk > US1_AZAMI_SOZLUK) return US1_BOZUK;
    if (m->baglam == 0 || m->baglam > US1_AZAMI_BAGLAM) return US1_BOZUK;
    uint32_t yarim = 0;           /* RoPE: baş başına döndürülen çift sayısı */
    if (surum == US1_SURUM_V1) {
        /* v1'de ayrılmış alanlar sıfırdır; dolu olması başka bir sürüm/bozulma
         * (1.4.6 kuralı, DEĞİŞMEDİ: v1 dosyaları bit bit aynı yüklenir). */
        for (uint32_t o = 32; o < 48; o += 4) if (oku32(veri + o) != 0) return US1_BOZUK;
    } else {
        /* v2: 32 bayrak, 36 rope_taban, 40/44 ayrılmış (sıfır). */
        uint32_t bayrak = oku32(veri + 32), taban = oku32(veri + 36);
        if (bayrak & ~US1_BAYRAK_ROPE) return US1_BOZUK;          /* bilinmeyen bayrak */
        if (!(bayrak & US1_BAYRAK_ROPE) && taban != 0) return US1_BOZUK;
        if ((bayrak & US1_BAYRAK_ROPE) && taban == 0) return US1_BOZUK;
        if (oku32(veri + 40) != 0 || oku32(veri + 44) != 0) return US1_BOZUK;
        if (bayrak & US1_BAYRAK_ROPE) {
            uint32_t bs = m->d / m->bas;
            if (bs % 2) return US1_BOZUK;         /* tek baş boyu: çift kurulamaz */
            yarim = bs / 2;
        }
        m->bayrak = bayrak;
        m->rope_taban = taban;
    }

    /* toplam boy BİREBİR: eksik ya da fazla bayt kabul edilmez */
    uint64_t d = m->d, f = m->ffn, V = m->sozluk;
    uint64_t kat = 2 * (8 + 1 * (4 + d))                 /* n1, n2      */
                 + 4 * (8 + d * (4 + d))                 /* wq wk wv wo */
                 + 2 * (8 + f * (4 + d))                 /* w1, w3      */
                 + (8 + d * (4 + f));                    /* w2          */
    uint64_t gerek = 48 + (8 + V * (4 + d)) + (uint64_t)m->katman * kat
                   + (8 + 1 * (4 + d)) + (8 + V * (4 + d));
    /* RoPE çizelgesi: satir, sutun + cos[baglam][yarim] + sin[baglam][yarim] (int32) */
    uint64_t cizelge = yarim ? (8 + 2 * (uint64_t)m->baglam * yarim * 4) : 0;
    gerek += cizelge;
    if (gerek != boy) return US1_BOZUK;

    const uint8_t *p = veri + 48, *son = veri + boy;
    int hata = 0;
    p = tensor_coz(p, son, &m->gomme, m->sozluk, m->d, &hata);
    for (uint32_t i = 0; i < m->katman; i++) {
        us1_katman_t *k = &m->kat[i];
        p = tensor_coz(p, son, &k->n1, 1, m->d, &hata);
        p = tensor_coz(p, son, &k->wq, m->d, m->d, &hata);
        p = tensor_coz(p, son, &k->wk, m->d, m->d, &hata);
        p = tensor_coz(p, son, &k->wv, m->d, m->d, &hata);
        p = tensor_coz(p, son, &k->wo, m->d, m->d, &hata);
        p = tensor_coz(p, son, &k->n2, 1, m->d, &hata);
        p = tensor_coz(p, son, &k->w1, m->ffn, m->d, &hata);
        p = tensor_coz(p, son, &k->w2, m->d, m->ffn, &hata);
        p = tensor_coz(p, son, &k->w3, m->ffn, m->d, &hata);
    }
    p = tensor_coz(p, son, &m->son_norm, 1, m->d, &hata);
    p = tensor_coz(p, son, &m->cikis, m->sozluk, m->d, &hata);
    if (!hata && yarim) {
        /* RoPE çizelgesi: biçim başlıkla BİREBİR; her değer sınırda ve
         * cos^2+sin^2 ~ 2^60 (bozuk çizelge motoru taşırmasın, döndürme
         * normu korusun). Çizelge dosyadan okunur (Python float64'ten Q30'a
         * yuvarlanmış): C'de trigonometri YOK, iki taraf AYNI tamsayıları görür. */
        if ((uint64_t)(son - p) < 8) hata = 1;
        else {
            uint32_t satir = oku32(p), sutun = oku32(p + 4);
            p += 8;
            if (satir != m->baglam || sutun != yarim) hata = 1;
            else {
                uint64_t n = (uint64_t)m->baglam * yarim;
                if ((uint64_t)(son - p) < 2 * n * 4) hata = 1;
                else {
                    const uint8_t *cb = p, *sb = p + n * 4;
                    const int64_t Q30 = (int64_t)1 << 30, NORM = (int64_t)1 << 60, TOL = (int64_t)1 << 40;
                    for (uint64_t j = 0; j < n; j++) {
                        int64_t c = (int32_t)oku32(cb + j * 4), s = (int32_t)oku32(sb + j * 4);
                        if (c > Q30 || c < -Q30 || s > Q30 || s < -Q30) { hata = 1; break; }
                        int64_t fark = c * c + s * s - NORM;                 /* |.| <= 2^61 */
                        if (fark > TOL || fark < -TOL) { hata = 1; break; }
                    }
                }
                if (!hata) {
                    m->rope_tablo = p;
                    m->rope_sutun = yarim;
                    p += 2 * n * 4;
                }
            }
        }
    }
    if (!hata && p != son) hata = 1;
    if (hata) m->rope_tablo = 0;
    /* Kısa, uzun ya da bozuk dosya sessizce kabul EDİLMEZ. */
    return hata ? US1_BOZUK : US1_OLDU;
}

/* --- RoPE (v2): d boyutlu v'yi (q ya da k) konum t için YERİNDE döndürür ---
 * Her başta, çift i (0 <= i < yarim): (v[2i], v[2i+1]) açısı t*theta_i ile.
 *   v'[2i]   = yuv( v[2i]*c - v[2i+1]*s , 30 )
 *   v'[2i+1] = yuv( v[2i]*s + v[2i+1]*c , 30 )
 * v 16.16 (|v| < 2^31), c/s Q30 (|.| <= 2^30): çarpım < 2^61, iki terimin
 * toplamı < 2^62 -> int64'e taşmadan sığar; tek yuvarlama, sonra doyurma.
 * Aynı işlev tam geçişte (us1_ileri) ve artımlıda (us1_adim) çağrılır:
 * adım = tam geçiş BİT BİT aynı kalır. Çağıran t < baglam'ı garanti eder. */
static void rope_uygula(const us1_model_t *m, int32_t *v, uint32_t t)
{
    uint32_t bs = m->d / m->bas, yarim = m->rope_sutun;
    uint64_t n = (uint64_t)m->baglam * yarim;
    const uint8_t *cb = m->rope_tablo + (uint64_t)t * yarim * 4;
    const uint8_t *sb = m->rope_tablo + (n + (uint64_t)t * yarim) * 4;
    for (uint32_t b = 0; b < m->bas; b++) {
        uint32_t d0 = b * bs;
        for (uint32_t i = 0; i < yarim; i++) {
            int64_t c = (int32_t)oku32(cb + i * 4), s = (int32_t)oku32(sb + i * 4);
            int64_t x0 = v[d0 + 2 * i], x1 = v[d0 + 2 * i + 1];
            v[d0 + 2 * i]     = doyur32(yuv64(x0 * c - x1 * s, 30));
            v[d0 + 2 * i + 1] = doyur32(yuv64(x0 * s + x1 * c, 30));
        }
    }
}

/* y = W x : W (satir x sutun) Q8, x 16.16, y 16.16.
 * top = sum q*x (2^-16 birimi, tam), y = top * olcek (2^-32) -> >> 32. */
static void matcarp(const us1_tensor_t *w, const int32_t *x, int32_t *y)
{
    for (uint32_t i = 0; i < w->satir; i++) {
        const int8_t *s = satir_veri(w, i);
        int64_t top = 0;
        for (uint32_t j = 0; j < w->sutun; j++)
            top += (int64_t)s[j] * (int64_t)x[j];
        y[i] = doyur32(doyur64_128(yuv128((i128)top * satir_olcek32(w, i), 32)));
    }
}

/* gömme satırı: q * olcek -> 16.16 */
static void gomme_satiri(const us1_tensor_t *g, uint32_t im, int32_t *x, uint32_t d)
{
    const int8_t *s = satir_veri(g, im);
    int64_t olc = satir_olcek32(g, im);
    for (uint32_t i = 0; i < d; i++)
        x[i] = doyur32(doyur64_128(yuv128((i128)s[i] * olc, 16)));
}

/* y = x / sqrt(ort(x^2) + 1e-5) * g.  ort 2^-32 biriminde; kök 16.16. */
static void rmsnorm(const int32_t *x, const us1_tensor_t *g,
                    uint32_t n, int32_t *y)
{
    uint64_t kare = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t xx = (uint64_t)((int64_t)x[i] * (int64_t)x[i]);
        kare = (kare > UINT64_MAX - xx) ? UINT64_MAX : kare + xx;
    }
    uint64_t ort = kare / n + 42950u;            /* + 1e-5 * 2^32, 2^-32 birimi */
    /* 1/rms 2^-32 biriminde, BİR kez: ort < 2^47 ise kök 2^-24 biriminde
     * (ort<<16), değilse 2^-16 biriminde alınır. Öğe başına bölme yok. */
    uint64_t ters;
    if (ort < ((uint64_t)1 << 47)) {
        uint64_t kok24 = kok64(ort << 16);                       /* >= 53k */
        ters = ((((uint64_t)1 << 56) + kok24 / 2) / kok24);      /* 2^32 / (kok24 / 2^24) */
    } else {
        uint64_t kok16 = kok64(ort);
        ters = ((((uint64_t)1 << 48) + kok16 / 2) / kok16);
    }
    const int8_t *gs = satir_veri(g, 0);
    int64_t golc = satir_olcek32(g, 0);
    for (uint32_t i = 0; i < n; i++) {
        i128 kazanc = (i128)gs[i] * golc;                         /* 2^-32 */
        int64_t xg = doyur64_128(yuv128((i128)x[i] * kazanc, 32)); /* x*g, 2^-16 */
        y[i] = doyur32(doyur64_128(yuv128((i128)xg * (int64_t)ters, 32)));
    }
}

static void softmax(int32_t *x, uint32_t n)
{
    int32_t enb = x[0];
    for (uint32_t i = 1; i < n; i++) if (x[i] > enb) enb = x[i];
    int64_t top = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t fark = (int64_t)x[i] - enb;
        x[i] = (fark < -(int64_t)0x7FFFFFFF) ? 0 : ustel((int32_t)fark);
        top += x[i];
    }
    if (top == 0) top = 1;
    for (uint32_t i = 0; i < n; i++)
        x[i] = (int32_t)(((int64_t)x[i] * BIR + top / 2) / top);
}

/* Dikkat için softmax: olasılıklar 2^-30 biriminde (p30). skor16'yı
 * YERİNDE 16.16 olasılıkla değiştirir (iz ve API için); ağırlıklı toplam
 * p30'u kullanır. */
static int64_t g_p30[US1_AZAMI_BAGLAM];

static void softmax30(int32_t *x, uint32_t n)
{
    int32_t enb = x[0];
    for (uint32_t i = 1; i < n; i++) if (x[i] > enb) enb = x[i];
    int64_t top = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t fark = (int64_t)x[i] - enb;
        g_p30[i] = (fark < -(int64_t)0x7FFFFFFF) ? 0 : ustel30_negatif((int32_t)fark);
        top += g_p30[i];
    }
    if (top == 0) top = 1;
    for (uint32_t i = 0; i < n; i++) {
        g_p30[i] = ((g_p30[i] << 30) + top / 2) / top;          /* 2^-30 */
        x[i] = (int32_t)yuv64(g_p30[i], 14);                     /* 16.16 (iz) */
    }
}

/* silu(x) = x * sigmoid(x). e = e^-|x| <= 1 taşmaz. sigmoid 2^-30
 * biriminde tutulur: 16.16'da tutulunca 0.5 birimlik hata |x| ile
 * çarpılıyordu (uyum sınaması: 6 birim). */
static int64_t sigmoid30(int32_t x)
{
    int64_t mut = (x < 0) ? -(int64_t)x : x;
    int64_t e = ustel30_negatif((int32_t)(mut > 0x7FFFFFFF ? -0x7FFFFFFF : -mut));  /* (0, 2^30] */
    const int64_t BIR30 = (int64_t)1 << 30;
    int64_t payda = BIR30 + e;
    return (x >= 0) ? ((BIR30 << 30) + payda / 2) / payda       /* 1/(1+e)   */
                    : ((e << 30) + payda / 2) / payda;          /* e/(1+e)   */
}

#ifdef US1_IZ
static int32_t silu(int32_t x)
{
    return doyur32(yuv64((int64_t)x * sigmoid30(x), 30));
}
#endif

/* SwiGLU öğesi: silu(g1) * g3, TEK yuvarlama (2^-16 * 2^-30 * 2^-16 -> >> 46) */
static int32_t swiglu(int32_t g1, int32_t g3)
{
    i128 v = (i128)((int64_t)g1 * sigmoid30(g1)) * g3;
    return doyur32(doyur64_128(yuv128(v, 46)));
}

/* Tek baş dikkat (artımlı ve tam geçiş AYNI işlevi kullanır):
 * q: konum t'nin sorgusu, K/V: konum 0..t anahtar/değerleri (satır adımı d),
 * olcek32 = 2^32/sqrt(bs). skor ve p iz için 'skor' tamponunda kalır. */
static void dikkat_bas(const int32_t *q, const int32_t *K, const int32_t *V,
                       uint32_t t, uint32_t d, uint32_t d0, uint32_t bs,
                       int64_t olcek32, int32_t *skor, int32_t *cik,
                       int L, int b)
{
    for (uint32_t u = 0; u <= t; u++) {
        i128 top = 0;
        for (uint32_t i = 0; i < bs; i++)
            top += (i128)((int64_t)q[d0 + i] * (int64_t)K[u * d + d0 + i]);   /* 2^-32 */
        skor[u] = doyur32(doyur64_128(yuv128(top * olcek32, 48)));          /* 16.16 */
    }
    IZ("skor", L, b, t, skor, t + 1);
    softmax30(skor, t + 1);
    IZ("p", L, b, t, skor, t + 1);
    for (uint32_t i = 0; i < bs; i++) {
        i128 top = 0;
        for (uint32_t u = 0; u <= t; u++)
            top += (i128)g_p30[u] * V[u * d + d0 + i];                         /* 2^-46 */
        cik[d0 + i] = doyur32(doyur64_128(yuv128(top, 30)));
    }
    (void)L; (void)b;
}

/* 2^32 / sqrt(bs), yuvarlanmış */
static int64_t dikkat_olcegi(uint32_t bs)
{
    uint64_t s16 = kok64((uint64_t)bs << 32);                /* sqrt(bs) * 2^16 */
    return (int64_t)((((uint64_t)1 << 48) + s16 / 2) / s16);
}

/* FFN (SwiGLU) + artık; g_h girişte n2 çıktısı */
static int32_t g_ara[US1_AZAMI_FFN], g_ara3[US1_AZAMI_FFN];

static void ffn_katman(const us1_katman_t *k, uint32_t ffn, const int32_t *h,
                       int32_t *x, int32_t *gecici, uint32_t d, uint32_t t, int L)
{
    matcarp(&k->w1, h, g_ara);
    matcarp(&k->w3, h, g_ara3);
    IZ("w1", L, -1, t, g_ara, ffn);
    IZ("w3", L, -1, t, g_ara3, ffn);
    for (uint32_t i = 0; i < ffn; i++)
        g_ara[i] = swiglu(g_ara[i], g_ara3[i]);
    IZ("swiglu", L, -1, t, g_ara, ffn);
    matcarp(&k->w2, g_ara, gecici);
    IZ("w2", L, -1, t, gecici, d);
    for (uint32_t i = 0; i < d; i++) x[i] = doyur32((int64_t)x[i] + gecici[i]);
    IZ("x_son", L, -1, t, x, d);
    (void)t; (void)L;
}

/* Çalışma tamponları: Ring3'te yığın küçüktür, durağan alan kullanılır.
 * Motor tek iş parçacıklıdır; eşzamanlı çağrı desteklenmez. */
static int32_t g_x[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_q[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_k[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_v[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_h[US1_AZAMI_D], g_cik[US1_AZAMI_D];
static int32_t g_skor[US1_AZAMI_BAGLAM];

int us1_ileri(us1_model_t *m, const uint32_t *imler, uint32_t T, int32_t *mantik)
{
    if (!m || !imler || !mantik || T == 0 || T > m->baglam) return US1_BOZUK;
    uint32_t d = m->d, bs = d / m->bas;

    for (uint32_t t = 0; t < T; t++) {
        if (imler[t] >= m->sozluk) return US1_BOZUK;
        gomme_satiri(&m->gomme, imler[t], &g_x[t * d], d);
    }
    int64_t olcek32 = dikkat_olcegi(bs);

    for (uint32_t L = 0; L < m->katman; L++) {
        us1_katman_t *k = &m->kat[L];

        for (uint32_t t = 0; t < T; t++) {
            rmsnorm(&g_x[t * d], &k->n1, d, g_h);
            matcarp(&k->wq, g_h, &g_q[t * d]);
            matcarp(&k->wk, g_h, &g_k[t * d]);
            matcarp(&k->wv, g_h, &g_v[t * d]);
            /* v2 RoPE: q ve k (v DEĞİL) konum t ile döndürülür; t = dizideki
             * sıra (us1_adim'daki g_konum ile aynı anlam). v1'de koşmaz. */
            if (US1_ROPE(m)) {
                rope_uygula(m, &g_q[t * d], t);
                rope_uygula(m, &g_k[t * d], t);
            }
        }
        for (uint32_t t = 0; t < T; t++) {
            for (uint32_t i = 0; i < d; i++) g_cik[i] = 0;
            for (uint32_t b = 0; b < m->bas; b++)
                dikkat_bas(&g_q[t * d], g_k, g_v, t, d, b * bs, bs, olcek32, g_skor, g_cik, -2, -1);
            matcarp(&k->wo, g_cik, g_h);
            for (uint32_t i = 0; i < d; i++) g_x[t * d + i] = doyur32((int64_t)g_x[t * d + i] + g_h[i]);
        }
        for (uint32_t t = 0; t < T; t++) {
            rmsnorm(&g_x[t * d], &k->n2, d, g_h);
            ffn_katman(k, m->ffn, g_h, &g_x[t * d], g_cik, d, t, -2);
        }
    }

    for (uint32_t t = 0; t < T; t++) {
        rmsnorm(&g_x[t * d], &m->son_norm, d, g_h);
        matcarp(&m->cikis, g_h, &mantik[t * m->sozluk]);
    }
    return US1_OLDU;
}

/* =================== artımlı üretim (KV önbelleği) =================== */
/* 1.5: KV önbelleği. Sabit bölge 4 katmanlık (bss büyümesin); daha çok
 * katmanlı (büyümüş) model için çağıran us1_kv_kur ile tampon verir. */
#define US1_KV_SABIT_KATMAN 4
static int32_t  g_kv_sabit[2 * US1_KV_SABIT_KATMAN * US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t *g_kv = g_kv_sabit;
static uint32_t g_kv_katman = US1_KV_SABIT_KATMAN;
void us1_kv_kur(int32_t *tampon, uint32_t katman) {
    if (tampon && katman >= US1_KV_SABIT_KATMAN) { g_kv = tampon; g_kv_katman = katman; }
    else { g_kv = g_kv_sabit; g_kv_katman = US1_KV_SABIT_KATMAN; }
}
uint32_t us1_kv_katman(void) { return g_kv_katman; }
static int32_t  g_xt[US1_AZAMI_D], g_qt[US1_AZAMI_D];
static uint32_t g_konum = 0;

int us1_sifirla(us1_model_t *m) { (void)m; g_konum = 0; return US1_OLDU; }
uint32_t us1_konum(void) { return g_konum; }

int us1_adim(us1_model_t *m, uint32_t im, int32_t *mantik)
{
    if (m->katman > g_kv_katman) return US1_BOZUK;     /* 1.5: KV tamponu bu katman sayısına yetmiyor (us1_kv_kur) */
    if (!m || !mantik || im >= m->sozluk) return US1_BOZUK;
    if (g_konum >= m->baglam) return US1_DOLU;
    uint32_t d = m->d, bs = d / m->bas, t = g_konum;

    gomme_satiri(&m->gomme, im, g_xt, d);
    IZ("gomme", -1, -1, t, g_xt, d);
    int64_t olcek32 = dikkat_olcegi(bs);

    for (uint32_t L = 0; L < m->katman; L++) {
        us1_katman_t *k = &m->kat[L];
        int32_t *kk = g_kv + (uint32_t)L * US1_AZAMI_BAGLAM * US1_AZAMI_D;
        int32_t *vk = g_kv + (g_kv_katman + (uint32_t)L) * US1_AZAMI_BAGLAM * US1_AZAMI_D;

        rmsnorm(g_xt, &k->n1, d, g_h);
        IZ("n1", L, -1, t, g_h, d);
        matcarp(&k->wq, g_h, g_qt);
        matcarp(&k->wk, g_h, &kk[t * d]);
        matcarp(&k->wv, g_h, &vk[t * d]);
        IZ("q", L, -1, t, g_qt, d);
        IZ("k", L, -1, t, &kk[t * d], d);
        IZ("v", L, -1, t, &vk[t * d], d);
        /* v2 RoPE: sorgu ve anahtar konum t (= g_konum) ile döndürülür;
         * KV önbelleğine DÖNDÜRÜLMÜŞ k kalır (sonraki konumlar onu okur).
         * İz: "q"/"k" döndürmeden ÖNCE, "q_rope"/"k_rope" SONRA. */
        if (US1_ROPE(m)) {
            rope_uygula(m, g_qt, t);
            rope_uygula(m, &kk[t * d], t);
            IZ("q_rope", L, -1, t, g_qt, d);
            IZ("k_rope", L, -1, t, &kk[t * d], d);
        }

        for (uint32_t i = 0; i < d; i++) g_cik[i] = 0;
        for (uint32_t b = 0; b < m->bas; b++) {
            /* dikkat_bas q'yu d0'dan okur: sorgu satırı tek konumluk */
            dikkat_bas(g_qt, kk, vk, t, d, b * bs, bs, olcek32, g_skor, g_cik, (int)L, (int)b);
        }
        IZ("dikkat", L, -1, t, g_cik, d);
        matcarp(&k->wo, g_cik, g_h);
        IZ("wo", L, -1, t, g_h, d);
        for (uint32_t i = 0; i < d; i++) g_xt[i] = doyur32((int64_t)g_xt[i] + g_h[i]);
        IZ("x_orta", L, -1, t, g_xt, d);

        rmsnorm(g_xt, &k->n2, d, g_h);
        IZ("n2", L, -1, t, g_h, d);
        ffn_katman(k, m->ffn, g_h, g_xt, g_cik, d, t, (int)L);
    }
    rmsnorm(g_xt, &m->son_norm, d, g_h);
    IZ("son_norm", -1, -1, t, g_h, d);
    matcarp(&m->cikis, g_h, mantik);
    IZ("mantik", -1, -1, t, mantik, m->sozluk);
    g_konum++;
    return US1_OLDU;
}

static int32_t g_p[US1_AZAMI_SOZLUK];

uint32_t us1_sec(const int32_t *mantik, uint32_t n, uint32_t *tohum)
{
    if (n == 0) return 0;
    if (n > US1_AZAMI_SOZLUK) n = US1_AZAMI_SOZLUK;
    if (!tohum) {
        uint32_t en = 0;
        for (uint32_t i = 1; i < n; i++) if (mantik[i] > mantik[en]) en = i;
        return en;
    }
    for (uint32_t i = 0; i < n; i++) g_p[i] = mantik[i];
    softmax(g_p, n);
    *tohum = (*tohum) * 1103515245u + 12345u;
    int32_t r = (int32_t)((*tohum >> 16) & 0xFFFFu);
    int32_t top = 0;
    for (uint32_t i = 0; i < n; i++) {
        top += g_p[i];
        if (r < top) return i;
    }
    return n - 1;
}

uint32_t us1_kodla(const char *sozluk, uint32_t sozluk_boy, uint32_t model_sozluk,
                   const char *metin, uint32_t *imler, uint32_t azami)
{
    uint32_t n = 0, sinir = sozluk_boy < model_sozluk ? sozluk_boy : model_sozluk;
    if (!sozluk || !metin || !imler) return 0;
    for (const char *p = metin; *p && n < azami; p++)
        for (uint32_t i = 0; i < sinir; i++)
            if (sozluk[i] == *p) { imler[n++] = i; break; }
    return n;
}

#ifdef US1_IZ
int32_t us1_iz_carp(int32_t a, int32_t b) { return carp(a, b); }
int32_t us1_iz_bol(int32_t a, int32_t b)  { return bol(a, b); }
int32_t us1_iz_kokk(int32_t x)            { return kokk(x); }
int32_t us1_iz_ustel(int32_t x)           { return ustel(x); }
int32_t us1_iz_silu(int32_t x)            { return silu(x); }
void    us1_iz_softmax(int32_t *x, uint32_t n) { softmax(x, n); }
int64_t us1_iz_olcek32(const us1_tensor_t *t, uint32_t satir) { return satir_olcek32(t, satir); }
int us1_iz_rope(const us1_model_t *m, int32_t *v, uint32_t t)
{
    if (!m || !v || !US1_ROPE(m) || t >= m->baglam) return US1_BOZUK;
    rope_uygula(m, v, t);
    return US1_OLDU;
}
#endif
