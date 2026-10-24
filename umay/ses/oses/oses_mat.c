/* OSES matematik: libm'siz, çift duyarlıkla hesaplayıp tek duyarlığa
 * yuvarlar (sonuçlar çoğunlukla doğru yuvarlanmış float olur). */
#include "oses_mat.h"

static const double LN2_HI = 6.93147180369123816490e-01;
static const double LN2_LO = 1.90821492927058770002e-10;

double osm_exp(double x) {
    if (x > 709.0) return 1.0 / 0.0;
    if (x < -745.0) return 0.0;
    double kd = x * 1.4426950408889634;
    long k = (long)(kd < 0 ? kd - 0.5 : kd + 0.5);
    double r = (x - (double)k * LN2_HI) - (double)k * LN2_LO;   /* |r| <= ln2/2 */
    /* Taylor (r küçük): 13 terim, hata < 1e-17 */
    double t = 1.0 + r * (1.0 + r * (1.0/2 + r * (1.0/6 + r * (1.0/24 + r * (1.0/120 + r * (1.0/720 +
               r * (1.0/5040 + r * (1.0/40320 + r * (1.0/362880 + r * (1.0/3628800 + r * (1.0/39916800 +
               r * (1.0/479001600))))))))))));
    /* 2^k */
    union { double d; uint64_t u; } s;
    if (k < -1022) { s.u = (uint64_t)(k + 1022 + 1023) << 52; return t * s.d * 2.2250738585072014e-308; }
    s.u = (uint64_t)(k + 1023) << 52;
    return t * s.d;
}

double osm_log(double x) {
    if (!(x > 0)) return x == 0 ? -1.0 / 0.0 : (x - x) / (x - x);
    union { double d; uint64_t u; } v = { x };
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    if (e == -1023) { v.d = x * 4503599627370496.0; e = (int)((v.u >> 52) & 0x7FF) - 1023 - 52; }
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;   /* m in [1,2) */
    double m = v.d;
    if (m > 1.4142135623730951) { m *= 0.5; e++; }
    /* log(m) = 2 atanh(s), s = (m-1)/(m+1), |s| < 0.172 */
    double s = (m - 1.0) / (m + 1.0), s2 = s * s, sum = 0, p = s;
    for (int i = 1; i < 40; i += 2) { sum += p / i; p *= s2; if (p < 1e-20 && p > -1e-20) break; }
    return 2.0 * sum + e * LN2_HI + e * LN2_LO;
}

double osm_sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }

double osm_tanh(double x) {
    double a = x < 0 ? -x : x;
    if (a > 20) return x < 0 ? -1.0 : 1.0;
    if (a < 1e-5) return x;
    double t = osm_exp(-2.0 * a);
    double r = (1.0 - t) / (1.0 + t);
    return x < 0 ? -r : r;
}

double osm_erf(double x) {
    double a = x < 0 ? -x : x, r;
    if (a < 3.0) {
        /* erf(x) = 2/sqrt(pi) * sum (-1)^n x^(2n+1) / (n! (2n+1)) */
        double x2 = a * a, terim = a, toplam = a;
        for (int n = 1; n < 80; n++) {
            terim *= -x2 / n;
            double k = terim / (2 * n + 1);
            toplam += k;
            if (k < 1e-18 && k > -1e-18) break;
        }
        r = 1.1283791670955126 * toplam;
    } else if (a < 6.0) {
        /* erfc sürekli kesir (Lentz) */
        double x2 = a * a;
        double f = a, C = a, D = 0, tiny = 1e-300;
        for (int i = 1; i < 200; i++) {
            double an = i * 0.5;
            D = a + an * D; if (D == 0) D = tiny; D = 1 / D;
            C = (i & 1 ? a : a) + an / C;   /* x + (n/2)/(x + ...) */
            double del = C * D; f *= del;
            if (del - 1 < 1e-16 && del - 1 > -1e-16) break;
        }
        double erfc = osm_exp(-x2) / (f * 1.7724538509055159);
        r = 1.0 - erfc;
    } else r = 1.0;
    return x < 0 ? -r : r;
}

double osm_pow(double x, double y) {
    if (y == 2.0) return x * x;
    if (y == 1.0) return x;
    if (y == 0.5) return osm_sqrt(x);
    if (y == 0.0) return 1.0;
    long yi = (long)y;
    if ((double)yi == y && yi > 0 && yi < 64) { double r = 1; for (long i = 0; i < yi; i++) r *= x; return r; }
    if (x < 0) {
        if ((double)yi == y) { double r = osm_exp(y * osm_log(-x)); return (yi & 1) ? -r : r; }
        return (x - x) / (x - x);
    }
    if (x == 0) return y > 0 ? 0.0 : 1.0 / 0.0;
    return osm_exp(y * osm_log(x));
}

double osm_ceil(double x) {
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    long i = (long)x;
    double d = (double)i;
    return (d < x) ? d + 1.0 : d;
}

double osm_softplus(double x) {
    if (x > 30) return x;
    return osm_log(1.0 + osm_exp(x));
}
