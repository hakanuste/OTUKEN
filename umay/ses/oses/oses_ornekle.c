#include "oses_ornekle.h"

static float ornek(const float *s, long n, long i) { return s[i < 0 ? 0 : (i >= n ? n - 1 : i)]; }

uint32_t oses_ornekle_boy(long n) { return (uint32_t)(((uint64_t)n * OSES_CIKIS_HZ) / OSES_GIRIS_HZ); }

uint32_t oses_ornekle(const float *s, long n, int16_t *c) {
    uint32_t cn = oses_ornekle_boy(n);
    const double adim = (double)OSES_GIRIS_HZ / OSES_CIKIS_HZ;
    for (uint32_t k = 0; k < cn; k++) {
        double x = k * adim;
        long i = (long)x;
        float t = (float)(x - (double)i);
        float p0 = ornek(s, n, i - 1), p1 = ornek(s, n, i), p2 = ornek(s, n, i + 1), p3 = ornek(s, n, i + 2);
        float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
        float o = v * 32767.0f;
        if (o > 32767.0f) o = 32767.0f;
        if (o < -32768.0f) o = -32768.0f;
        int16_t q = (int16_t)o;
        c[2 * k] = q; c[2 * k + 1] = q;
    }
    return cn;
}
