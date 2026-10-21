#include "us1.h"

/* --- 16.16 sabit nokta --- */
#define KESIR  16
#define BIR    (1 << KESIR)

static int32_t carp(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> KESIR);
}

static int32_t bol(int32_t a, int32_t b)
{
    if (b == 0) return 0;
    return (int32_t)((((int64_t)a) << KESIR) / b);
}

/* Tamsayı karekök (Newton). Girdi ve çıktı 16.16. */
static int32_t kokk(int32_t x)
{
    if (x <= 0) return 0;
    int64_t v = (int64_t)x << KESIR;
    int64_t t = 1LL << (KESIR + 8);
    for (int i = 0; i < 24; i++) {
        if (t == 0) break;
        t = (t + v / t) >> 1;
    }
    return (int32_t)t;
}

/* e^x, 16.16. x = n*ln2 + r ayrıştırması, r için dört terimli seri. */
static int32_t ustel(int32_t x)
{
    const int32_t LN2 = 45426;              /* ln2 << 16 */
    if (x < -12 * BIR) return 0;
    if (x >  12 * BIR) x = 12 * BIR;
    int32_t n = x / LN2;
    int32_t r = x - n * LN2;
    int32_t t = BIR, top = BIR;
    t = carp(t, r);       top += t;
    t = carp(t, r) / 2;   top += t;
    t = carp(t, r) / 3;   top += t;
    t = carp(t, r) / 4;   top += t;
    if (n >= 0) {
        if (n > 30) return 0x7FFFFFFF;
        top <<= n;
    } else {
        int s = -n;
        if (s > 30) return 0;
        top >>= s;
    }
    return top;
}

static uint32_t oku32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* f32 -> 16.16. Kayan nokta BİRİMİ kullanılmaz; bitler elle çözülür. */
static int32_t f32_sabit(uint32_t bit)
{
    int isaret = (int)((bit >> 31) & 1u);
    int ust = (int)((bit >> 23) & 0xFFu);
    uint32_t kesir = bit & 0x7FFFFFu;
    if (ust == 0) return 0;
    int64_t taban = (int64_t)(kesir | 0x800000u);
    int kaydir = ust - 127 - 23 + KESIR;
    int64_t v = (kaydir >= 0) ? (taban << kaydir) : (taban >> (-kaydir));
    if (v > 0x7FFFFFFF) v = 0x7FFFFFFF;
    return isaret ? (int32_t)(-v) : (int32_t)v;
}

static const uint8_t *tensor_coz(const uint8_t *p, const uint8_t *son,
                                 us1_tensor_t *t, int *hata)
{
    if (p + 8 > son) { *hata = 1; return son; }
    t->satir = oku32(p);
    t->sutun = oku32(p + 4);
    p += 8;
    if (t->satir == 0 || t->sutun == 0) { *hata = 1; return son; }
    uint64_t gerek = (uint64_t)t->satir * (4u + t->sutun);
    if ((uint64_t)(son - p) < gerek) { *hata = 1; return son; }
    t->veri = p;
    return p + gerek;
}

static int32_t satir_olcek(const us1_tensor_t *t, uint32_t satir)
{
    return f32_sabit(oku32(t->veri + (uint64_t)satir * (4u + t->sutun)));
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
    if (oku32(veri + 4) != 1) return US1_SURUM;

    m->d      = oku32(veri + 8);
    m->katman = oku32(veri + 12);
    m->bas    = oku32(veri + 16);
    m->ffn    = oku32(veri + 20);
    m->sozluk = oku32(veri + 24);
    m->baglam = oku32(veri + 28);

    if (m->d == 0 || m->d > US1_AZAMI_D) return US1_BOZUK;
    if (m->katman == 0 || m->katman > US1_AZAMI_KATMAN) return US1_BOZUK;
    if (m->bas == 0 || m->d % m->bas) return US1_BOZUK;
    if (m->ffn == 0 || m->ffn > US1_AZAMI_FFN) return US1_BOZUK;
    if (m->sozluk == 0 || m->sozluk > US1_AZAMI_SOZLUK) return US1_BOZUK;
    if (m->baglam == 0 || m->baglam > US1_AZAMI_BAGLAM) return US1_BOZUK;

    const uint8_t *p = veri + 48, *son = veri + boy;
    int hata = 0;
    p = tensor_coz(p, son, &m->gomme, &hata);
    for (uint32_t i = 0; i < m->katman && !hata; i++) {
        p = tensor_coz(p, son, &m->kat[i].n1, &hata);
        p = tensor_coz(p, son, &m->kat[i].wq, &hata);
        p = tensor_coz(p, son, &m->kat[i].wk, &hata);
        p = tensor_coz(p, son, &m->kat[i].wv, &hata);
        p = tensor_coz(p, son, &m->kat[i].wo, &hata);
        p = tensor_coz(p, son, &m->kat[i].n2, &hata);
        p = tensor_coz(p, son, &m->kat[i].w1, &hata);
        p = tensor_coz(p, son, &m->kat[i].w2, &hata);
        p = tensor_coz(p, son, &m->kat[i].w3, &hata);
    }
    p = tensor_coz(p, son, &m->son_norm, &hata);
    p = tensor_coz(p, son, &m->cikis, &hata);
    /* Kısa ya da bozuk dosya sessizce kabul EDİLMEZ. */
    return hata ? US1_BOZUK : US1_OLDU;
}

/* y = W x : W (satir x sutun) Q8, x 16.16, y 16.16 */
static void matcarp(const us1_tensor_t *w, const int32_t *x, int32_t *y)
{
    for (uint32_t i = 0; i < w->satir; i++) {
        const int8_t *s = satir_veri(w, i);
        int64_t top = 0;
        for (uint32_t j = 0; j < w->sutun; j++)
            top += (int64_t)s[j] * (int64_t)x[j];
        y[i] = (int32_t)((top * satir_olcek(w, i)) >> KESIR);
    }
}

static void rmsnorm(const int32_t *x, const us1_tensor_t *g,
                    uint32_t n, int32_t *y)
{
    int64_t kare = 0;
    for (uint32_t i = 0; i < n; i++)
        kare += ((int64_t)x[i] * (int64_t)x[i]) >> KESIR;
    int32_t kok = kokk((int32_t)(kare / (int64_t)n) + 1);
    if (kok == 0) kok = 1;
    const int8_t *gs = satir_veri(g, 0);
    int32_t golc = satir_olcek(g, 0);
    for (uint32_t i = 0; i < n; i++)
        y[i] = carp(bol(x[i], kok), (int32_t)((int64_t)gs[i] * golc));
}

static void softmax(int32_t *x, uint32_t n)
{
    int32_t enb = x[0];
    for (uint32_t i = 1; i < n; i++) if (x[i] > enb) enb = x[i];
    int64_t top = 0;
    for (uint32_t i = 0; i < n; i++) {
        x[i] = ustel(x[i] - enb);
        top += x[i];
    }
    if (top == 0) top = 1;
    for (uint32_t i = 0; i < n; i++)
        x[i] = (int32_t)(((int64_t)x[i] << KESIR) / top);
}

static int32_t silu(int32_t x)
{
    return carp(x, bol(BIR, BIR + ustel(-x)));
}

/* Çalışma tamponları: Ring3'te yığın küçüktür, durağan alan kullanılır.
 * Motor tek iş parçacıklıdır; eşzamanlı çağrı desteklenmez. */
static int32_t g_x[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_q[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_k[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_v[US1_AZAMI_BAGLAM * US1_AZAMI_D];
static int32_t g_h[US1_AZAMI_D], g_cik[US1_AZAMI_D];
static int32_t g_ara[US1_AZAMI_FFN], g_ara3[US1_AZAMI_FFN];
static int32_t g_skor[US1_AZAMI_BAGLAM];

int us1_ileri(us1_model_t *m, const uint32_t *imler, uint32_t T, int32_t *mantik)
{
    if (!m || !imler || !mantik || T == 0 || T > m->baglam) return US1_BOZUK;
    uint32_t d = m->d, bs = d / m->bas;

    for (uint32_t t = 0; t < T; t++) {
        uint32_t im = imler[t];
        if (im >= m->sozluk) return US1_BOZUK;
        const int8_t *s = satir_veri(&m->gomme, im);
        int32_t olc = satir_olcek(&m->gomme, im);
        for (uint32_t i = 0; i < d; i++)
            g_x[t * d + i] = (int32_t)((int64_t)s[i] * olc);
    }

    int32_t olcek_dikkat = bol(BIR, kokk((int32_t)bs * BIR));

    for (uint32_t L = 0; L < m->katman; L++) {
        us1_katman_t *k = &m->kat[L];

        for (uint32_t t = 0; t < T; t++) {
            rmsnorm(&g_x[t * d], &k->n1, d, g_h);
            matcarp(&k->wq, g_h, &g_q[t * d]);
            matcarp(&k->wk, g_h, &g_k[t * d]);
            matcarp(&k->wv, g_h, &g_v[t * d]);
        }

        for (uint32_t t = 0; t < T; t++) {
            for (uint32_t i = 0; i < d; i++) g_cik[i] = 0;
            for (uint32_t b = 0; b < m->bas; b++) {
                uint32_t d0 = b * bs;
                for (uint32_t u = 0; u <= t; u++) {      /* nedensel maske */
                    int64_t top = 0;
                    for (uint32_t i = 0; i < bs; i++)
                        top += ((int64_t)g_q[t * d + d0 + i] *
                                (int64_t)g_k[u * d + d0 + i]) >> KESIR;
                    g_skor[u] = carp((int32_t)top, olcek_dikkat);
                }
                softmax(g_skor, t + 1);
                for (uint32_t i = 0; i < bs; i++) {
                    int64_t top = 0;
                    for (uint32_t u = 0; u <= t; u++)
                        top += ((int64_t)g_skor[u] *
                                (int64_t)g_v[u * d + d0 + i]) >> KESIR;
                    g_cik[d0 + i] = (int32_t)top;
                }
            }
            matcarp(&k->wo, g_cik, g_h);
            for (uint32_t i = 0; i < d; i++) g_x[t * d + i] += g_h[i];
        }

        for (uint32_t t = 0; t < T; t++) {
            rmsnorm(&g_x[t * d], &k->n2, d, g_h);
            matcarp(&k->w1, g_h, g_ara);
            matcarp(&k->w3, g_h, g_ara3);
            for (uint32_t i = 0; i < m->ffn; i++)
                g_ara[i] = carp(silu(g_ara[i]), g_ara3[i]);
            matcarp(&k->w2, g_ara, g_cik);
            for (uint32_t i = 0; i < d; i++) g_x[t * d + i] += g_cik[i];
        }
    }

    for (uint32_t t = 0; t < T; t++) {
        rmsnorm(&g_x[t * d], &m->son_norm, d, g_h);
        matcarp(&m->cikis, g_h, &mantik[t * m->sozluk]);
    }
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
