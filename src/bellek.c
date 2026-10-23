/* 1.4.9: çekirdek için C bellek/dize işlevleri. BearSSL'in çekirdek kopyası
 * (KERVAN paket imzası doğrulaması) bunları ister; derleyici de büyük yapı
 * kopyalarında memcpy/memset çağırabilir. */
#include <stddef.h>
#include <stdint.h>

/* dongu -> memcpy donusumu kendini cagirmasin */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
    for (size_t i = 0; i < n; i++) a[i] = b[i];
    return d;
}
void *memset(void *d, int c, size_t n) {
    uint8_t *a = (uint8_t *)d;
    for (size_t i = 0; i < n; i++) a[i] = (uint8_t)c;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
    if (a == b || n == 0) return d;
    if (a < b) for (size_t i = 0; i < n; i++) a[i] = b[i];
    else for (size_t i = n; i > 0; i--) a[i - 1] = b[i - 1];
    return d;
}
int memcmp(const void *x, const void *y, size_t n) {
    const uint8_t *a = (const uint8_t *)x, *b = (const uint8_t *)y;
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
