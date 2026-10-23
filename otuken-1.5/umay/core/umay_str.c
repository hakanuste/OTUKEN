#include "umay_str.h"
#include <stdarg.h>

void um_bellek_kopya(void *h, const void *k, uint64_t n) {
    uint8_t *d = (uint8_t*)h; const uint8_t *s = (const uint8_t*)k;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}
void um_bellek_doldur(void *h, uint8_t d, uint64_t n) {
    uint8_t *p = (uint8_t*)h;
    for (uint64_t i = 0; i < n; i++) p[i] = d;
}
uint32_t um_uzun(const char *s) { uint32_t n = 0; if (!s) return 0; while (s[n]) n++; return n; }
int um_esit(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}
int um_esit_n(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}
int um_bas_mi(const char *s, const char *on) {
    while (*on) { if (*s != *on) return 0; s++; on++; }
    return 1;
}
const char *um_icinde(const char *saman, const char *igne) {
    if (!saman || !igne || !*igne) return saman;
    for (const char *p = saman; *p; p++) {
        const char *a = p, *b = igne;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return p;
    }
    return 0;
}
uint32_t um_kopya(char *h, uint32_t n, const char *k) {
    if (!h || n == 0) return 0;
    uint32_t i = 0;
    if (k) for (; k[i] && i + 1 < n; i++) h[i] = k[i];
    h[i] = 0;
    return i;
}
uint32_t um_ekle(char *h, uint32_t n, const char *k) {
    if (!h || n == 0) return 0;
    uint32_t u = um_uzun(h);
    if (u + 1 >= n) return u;
    return u + um_kopya(h + u, n - u, k);
}
uint32_t um_sayi_yaz(char *h, uint32_t n, int64_t d) {
    char gec[24]; int i = 0, eksi = 0;
    uint64_t m;
    if (d < 0) { eksi = 1; m = (uint64_t)(-(d + 1)) + 1ULL; } else m = (uint64_t)d;
    if (m == 0) gec[i++] = '0';
    while (m > 0 && i < 20) { gec[i++] = (char)('0' + (m % 10)); m /= 10; }
    uint32_t o = 0;
    if (eksi && o + 1 < n) h[o++] = '-';
    while (i > 0 && o + 1 < n) h[o++] = gec[--i];
    if (n) h[o] = 0;
    return o;
}
uint32_t um_bicim(char *h, uint32_t n, const char *kalip, ...) {
    va_list ap; va_start(ap, kalip);
    uint32_t o = 0;
    for (const char *p = kalip; *p && o + 1 < n; p++) {
        if (*p != '%') { h[o++] = *p; continue; }
        p++;
        if (*p == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(yok)";
            while (*s && o + 1 < n) h[o++] = *s++;
        } else if (*p == 'd') {
            char t[24]; um_sayi_yaz(t, sizeof t, (int64_t)va_arg(ap, int));
            for (char *q = t; *q && o + 1 < n; q++) h[o++] = *q;
        } else if (*p == 'l') {
            char t[24]; um_sayi_yaz(t, sizeof t, va_arg(ap, int64_t));
            for (char *q = t; *q && o + 1 < n; q++) h[o++] = *q;
        } else if (*p == 'u') {
            char t[24]; um_sayi_yaz(t, sizeof t, (int64_t)va_arg(ap, unsigned));
            for (char *q = t; *q && o + 1 < n; q++) h[o++] = *q;
        } else if (*p == 'c') {
            h[o++] = (char)va_arg(ap, int);
        } else if (*p == '%') {
            h[o++] = '%';
        } else if (*p == 0) break;
    }
    h[o < n ? o : n - 1] = 0;
    va_end(ap);
    return o;
}

/* ---------- UTF-8 ve Türkçe küçük harf ---------- */
uint32_t um_utf8_oku(const char *s, uint32_t *bayt) {
    const uint8_t *u = (const uint8_t*)s;
    if (u[0] < 0x80)            { *bayt = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0)  { *bayt = 2; return (uint32_t)((u[0]&0x1F)<<6 | (u[1]&0x3F)); }
    if ((u[0] & 0xF0) == 0xE0)  { *bayt = 3; return (uint32_t)((u[0]&0x0F)<<12 | (u[1]&0x3F)<<6 | (u[2]&0x3F)); }
    if ((u[0] & 0xF8) == 0xF0)  { *bayt = 4; return (uint32_t)((u[0]&0x07)<<18 | (u[1]&0x3F)<<12 | (u[2]&0x3F)<<6 | (u[3]&0x3F)); }
    *bayt = 1; return u[0];
}
uint32_t um_utf8_yaz(uint32_t kn, char *c) {
    if (kn < 0x80)   { c[0] = (char)kn; return 1; }
    if (kn < 0x800)  { c[0] = (char)(0xC0|(kn>>6)); c[1] = (char)(0x80|(kn&0x3F)); return 2; }
    if (kn < 0x10000){ c[0] = (char)(0xE0|(kn>>12)); c[1] = (char)(0x80|((kn>>6)&0x3F)); c[2] = (char)(0x80|(kn&0x3F)); return 3; }
    c[0] = (char)(0xF0|(kn>>18)); c[1] = (char)(0x80|((kn>>12)&0x3F));
    c[2] = (char)(0x80|((kn>>6)&0x3F)); c[3] = (char)(0x80|(kn&0x3F)); return 4;
}
uint32_t um_kucuk(uint32_t kn) {
    if (kn == 0x0049) return 0x0131;          /* I -> ı : ASCII kuralından ÖNCE */
    if (kn >= 'A' && kn <= 'Z') return kn + 32;
    switch (kn) {
        case 0x0130: return 0x0069; /* İ -> i */
        case 0x011E: return 0x011F; /* Ğ */
        case 0x00DC: return 0x00FC; /* Ü */
        case 0x015E: return 0x015F; /* Ş */
        case 0x00D6: return 0x00F6; /* Ö */
        case 0x00C7: return 0x00E7; /* Ç */
        default: return kn;
    }
}
int um_kelime_karakteri(uint32_t kn) {
    if (kn >= '0' && kn <= '9') return 1;
    if (kn >= 'a' && kn <= 'z') return 1;
    if (kn >= 'A' && kn <= 'Z') return 1;
    switch (kn) {
        case 0x00E7: case 0x00C7: case 0x011F: case 0x011E:
        case 0x0131: case 0x0130: case 0x00F6: case 0x00D6:
        case 0x015F: case 0x015E: case 0x00FC: case 0x00DC:
        case 0x0069: return 1;
        default: return 0;
    }
}
uint32_t um_kucult(char *hedef, uint32_t n, const char *kaynak) {
    uint32_t o = 0;
    const char *p = kaynak;
    while (*p && o + 5 < n) {
        uint32_t b; uint32_t kn = um_utf8_oku(p, &b);
        char yaz[5]; uint32_t yb = um_utf8_yaz(um_kucuk(kn), yaz);
        um_bellek_kopya(hedef + o, yaz, yb);
        o += yb;
        p += b;
    }
    if (n) hedef[o] = 0;
    return o;
}

void um_kucult_esbicim(char *s) {
    char *p = s;
    while (*p) {
        uint32_t b; uint32_t kn = um_utf8_oku(p, &b);
        char yaz[5]; uint32_t yb = um_utf8_yaz(um_kucuk(kn), yaz);
        if (yb == b) um_bellek_kopya(p, yaz, yb);
        else if (kn == 0x0049) *p = 'i';   /* I: konum korunmalı, tek bayta düşür */
        p += b;
    }
}

/* Geçersiz UTF-8 baytlarını yerinde atar (öncüsüz devam baytı, geçersiz
 * öncü, eksik ya da yarım dizi). Yeni uzunluk; sonuna 0 yazar. */
uint32_t um_utf8_temizle(char *s, uint32_t n) {
    uint32_t o = 0, j = 0;
    while (j < n) {
        uint8_t lb = (uint8_t)s[j];
        uint32_t gerek;
        if (lb < 0x80) gerek = 0;
        else if (lb >= 0xC2 && lb <= 0xDF) gerek = 1;
        else if ((lb & 0xF0) == 0xE0) gerek = 2;
        else if (lb >= 0xF0 && lb <= 0xF4) gerek = 3;
        else { j++; continue; }                  /* devam baytı ya da geçersiz öncü */
        if (j + gerek >= n + (gerek == 0 ? 1u : 0u)) { j++; continue; }   /* yarım dizi */
        uint32_t d = 1;
        while (d <= gerek && ((uint8_t)s[j + d] & 0xC0) == 0x80) d++;
        if (d <= gerek) { j++; continue; }       /* devam baytı eksik */
        for (uint32_t q = 0; q <= gerek; q++) s[o++] = s[j + q];
        j += gerek + 1;
    }
    s[o] = 0;
    return o;
}
