#include "uyg.h"

int pencere_sun(int id) { return (int)syscall(SYS_PEN_SUN, id, 0, 0, 0); }
int pencere_olay(int id, olay_u *o) { return (int)syscall(SYS_PEN_OLAY, id, (long)o, 0, 0); }
int tema_bil(int sira, char ad[32]) { return (int)syscall(SYS_TEMA_BIL, sira, (long)ad, 0, 0); }
int tema_degistir(const char *ad) { return (int)syscall(SYS_TEMA, (long)ad, 0, 0, 0); }

int sayi_yaz(char *s, uint64_u v, int hane) {
    char t[24];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 20);
    while (n < hane && n < 20) t[n++] = '0';
    for (int i = 0; i < n; i++) s[i] = t[n - 1 - i];
    s[n] = 0;
    return n;
}

void dize_ekle(char *d, int boy, const char *k) {
    int i = 0;
    while (i < boy - 1 && d[i]) i++;
    while (i < boy - 1 && *k) d[i++] = *k++;
    d[i] = 0;
}

int ag_coz(const char *ad, uint8_u ip[4]) { return (int)syscall(SYS_AG_COZ, (long)ad, (long)ip, 0, 0); }
int ag_baglan(const uint8_u ip[4], int port) {
    long ip32 = ((long)ip[0] << 24) | ((long)ip[1] << 16) | ((long)ip[2] << 8) | (long)ip[3];
    return (int)syscall(SYS_AG_BAGLAN, ip32, port, 0, 0);
}
int ag_gonder(int t, const void *v, int n) { return (int)syscall(SYS_AG_GONDER, t, (long)v, n, 0); }
int ag_gonder_tum(int t, const void *v, int n, long zaman_tik) {
    const uint8_u *p = (const uint8_u *)v;
    long bas = ticks();
    while (n > 0) {
        int parca = n > 4096 ? 4096 : n;
        int k = ag_gonder(t, p, parca);
        if (k < 0) return -1;
        if (k == 0) {
            if (ticks() - bas > zaman_tik) return -1;
            sleep_ticks(1);
            continue;
        }
        p += k; n -= k;
    }
    return 0;
}
int ag_al(int t, void *tampon, int azami) { return (int)syscall(SYS_AG_AL, t, (long)tampon, azami, 0); }
int ag_kapat(int t) { return (int)syscall(SYS_AG_KAPAT, t, 0, 0, 0); }
int ag_bilgi(ag_bilgi_u *b) { return (int)syscall(SYS_AG_BILGI, (long)b, 0, 0, 0); }

/* 1.4.9: BearSSL'in istediği iki libc işlevi (memcpy/memset/strlen ulib'de) */
void *memmove(void *d, const void *k, size_t_u n) {
    uint8_u *a = (uint8_u *)d; const uint8_u *b = (const uint8_u *)k;
    if (a == b || n == 0) return d;
    if (a < b) for (size_t_u i = 0; i < n; i++) a[i] = b[i];
    else for (size_t_u i = n; i > 0; i--) a[i - 1] = b[i - 1];
    return d;
}
int memcmp(const void *x, const void *y, size_t_u n) {
    const uint8_u *a = (const uint8_u *)x, *b = (const uint8_u *)y;
    for (size_t_u i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
