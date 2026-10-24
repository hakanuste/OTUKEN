/* =====================================================================
 * SSESINA — x87/SSE durumunun görev değişiminde korunduğunu sınar (TANI).
 * Aynı anda birkaç kopya koşar. Her kopya XMM0-15'e ve MXCSR'ye kendine
 * özgü değerler yükler, beklerken (zamanlayıcı başka görevlere geçer)
 * dokunmaz, sonra geri okuyup karşılaştırır. Çekirdek durumu saklamazsa
 * bir kopyanın değerleri ötekinin üstüne yazılır ve hata sayılır.
 * Ayrıca SSE ile çift duyarlıklı bir toplam hesaplanıp beklenenle denetlenir.
 * ===================================================================== */
#include "ulib.h"

typedef struct { unsigned long long a, b; } x128;

static void yukle(const x128 *v) {
    __asm__ volatile (
        "movdqu   0(%0), %%xmm0\n\t movdqu  16(%0), %%xmm1\n\t movdqu  32(%0), %%xmm2\n\t movdqu  48(%0), %%xmm3\n\t"
        "movdqu  64(%0), %%xmm4\n\t movdqu  80(%0), %%xmm5\n\t movdqu  96(%0), %%xmm6\n\t movdqu 112(%0), %%xmm7\n\t"
        "movdqu 128(%0), %%xmm8\n\t movdqu 144(%0), %%xmm9\n\t movdqu 160(%0), %%xmm10\n\t movdqu 176(%0), %%xmm11\n\t"
        "movdqu 192(%0), %%xmm12\n\t movdqu 208(%0), %%xmm13\n\t movdqu 224(%0), %%xmm14\n\t movdqu 240(%0), %%xmm15\n\t"
        : : "r"(v) : "memory");   /* derleyici bu dosyada XMM kullanmaz (yalnız asm) */
}
static void oku(x128 *v) {
    __asm__ volatile (
        "movdqu %%xmm0,   0(%0)\n\t movdqu %%xmm1,  16(%0)\n\t movdqu %%xmm2,  32(%0)\n\t movdqu %%xmm3,  48(%0)\n\t"
        "movdqu %%xmm4,  64(%0)\n\t movdqu %%xmm5,  80(%0)\n\t movdqu %%xmm6,  96(%0)\n\t movdqu %%xmm7, 112(%0)\n\t"
        "movdqu %%xmm8, 128(%0)\n\t movdqu %%xmm9, 144(%0)\n\t movdqu %%xmm10, 160(%0)\n\t movdqu %%xmm11, 176(%0)\n\t"
        "movdqu %%xmm12, 192(%0)\n\t movdqu %%xmm13, 208(%0)\n\t movdqu %%xmm14, 224(%0)\n\t movdqu %%xmm15, 240(%0)\n\t"
        : : "r"(v) : "memory");
}
static void sayi_yaz_(char *o, unsigned long v) { char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); for (int i = 0; i < n; i++) o[i] = t[n - 1 - i]; o[n] = 0; }

int main(void) {
    unsigned long pid = (unsigned long)getpid();
    unsigned long hata = 0, tur;
    static x128 v[16], g[16];
    /* yuvarlama kipi kopyaya göre farklı: MXCSR bit 13-14 */
    unsigned int mx = 0x1F80u | ((unsigned)(pid % 4) << 13), mg;
    for (tur = 0; tur < 40; tur++) {
        for (int i = 0; i < 16; i++) { v[i].a = (pid << 40) ^ (tur << 20) ^ (unsigned long long)i * 0x9E3779B97F4A7C15ull; v[i].b = ~v[i].a + pid; }
        __asm__ volatile ("ldmxcsr %0" : : "m"(mx));
        yukle(v);
        /* bekle: SSE'ye dokunmadan; arada zamanlayıcı başka görevlere geçer */
        for (volatile unsigned long k = 0; k < 300000; k++) { }
        if (tur % 4 == 0) yield();
        if (tur % 8 == 0) sleep_ticks(1);
        oku(g);
        __asm__ volatile ("stmxcsr %0" : "=m"(mg));
        for (int i = 0; i < 16; i++) if (g[i].a != v[i].a || g[i].b != v[i].b) hata++;
        if (mg != mx) hata++;
    }
    /* SSE ile çift duyarlıklı hesap: sum_{k=1..n} 1/k^2 (en yakına yuvarlamada) */
    unsigned int yak = 0x1F80;
    __asm__ volatile ("ldmxcsr %0" : : "m"(yak));
    double top = 0, bir = 1.0;
    for (int k = 1; k <= 20000; k++) {
        double d = (double)k;
        __asm__ volatile ("mulsd %1, %0" : "+x"(d) : "x"(d));        /* d = k*k */
        double q = bir;
        __asm__ volatile ("divsd %1, %0" : "+x"(q) : "x"(d));        /* q = 1/k^2 */
        __asm__ volatile ("addsd %1, %0" : "+x"(top) : "x"(q));
        if (k % 5000 == 0) yield();
    }
    /* beklenen: aynı hesabın ana makinedeki sonucu (bit bit) */
    union { double d; unsigned long long u; } s = { top };
    if (s.u != 0x3FFA5171F4E31786ull) hata++;   /* ana makinede ayni sirayla: 1.6448840680982086 */
    char t[160], n[24]; int k = 0;
    const char *a = "SSE-SINA: pid=";
    while (*a) t[k++] = *a++;
    sayi_yaz_(n, pid); for (char *p = n; *p; ) t[k++] = *p++;
    a = " tur=40 xmm0-15+mxcsr+hesap hata="; while (*a) t[k++] = *a++;
    sayi_yaz_(n, hata); for (char *p = n; *p; ) t[k++] = *p++;
    a = hata ? " -> HATA\n" : " -> BASARILI\n"; while (*a) t[k++] = *a++;
    t[k] = 0;
    syscall(0, (long)t, 0, 0, 0);
    return 0;
}
