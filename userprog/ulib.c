#include "ulib.h"

/* --- ham syscall: kullanici tarafi ABI'si ---
 * RAX=numara, RDI/RSI/RDX/R10 = arg1..4 (RCX DEGIL, cunku SYSCALL
 * komutu RCX'i donus adresi icin kullanir). */
long syscall(long num, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

/* --- surec --- */
void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0, 0);
    for (;;) { }   /* asla ulasilmaz */
}
long getpid(void)            { return syscall(SYS_GETPID, 0, 0, 0, 0); }
void yield(void)             { syscall(SYS_YIELD, 0, 0, 0, 0); }
void sleep_ticks(long t)     { syscall(SYS_SLEEP, t, 0, 0, 0); }
long ticks(void)             { return syscall(SYS_TICKS, 0, 0, 0, 0); }

/* --- cikti --- */
void puts(const char *s)     { syscall(SYS_WRITE, (long)s, 0, 0, 0); }

void putchar(char c) {
    char buf[2];
    buf[0] = c; buf[1] = 0;
    puts(buf);
}

void print_dec(uint64_u v) {
    char buf[24];
    int i = 0;
    if (v == 0) { puts("0"); return; }
    while (v > 0 && i < 23) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    char out[25];
    int j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = 0;
    puts(out);
}

void print_hex(uint64_u v) {
    static const char hx[] = "0123456789ABCDEF";
    char out[19];
    out[0] = '0'; out[1] = 'X';
    int p = 2, started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_u nib = (uint8_u)((v >> shift) & 0xF);
        if (nib || started || shift == 0) { out[p++] = hx[nib]; started = 1; }
    }
    out[p] = 0;
    puts(out);
}

/* --- giris --- */
char readkey(void) { return (char)syscall(SYS_READKEY, 0, 0, 0, 0); }

char readkey_blocking(void) {
    for (;;) {
        char c = readkey();
        if (c) return c;
        yield();   /* CPU'yu bosuna tuketme - diger gorevlere birak */
    }
}

int readline(char *buf, int max) {
    int n = 0;
    for (;;) {
        char c = readkey_blocking();
        if (c == 13) {            /* Enter */
            putchar('\n');
            buf[n] = 0;
            return n;
        } else if (c == 8) {      /* Backspace */
            if (n > 0) { n--; putchar((char)8); }
        } else if (n < max - 1) {
            buf[n++] = c;
            putchar(c);           /* echo */
        }
    }
}

/* --- dosya --- */
int open(const char *path)                  { return (int)syscall(SYS_OPEN, (long)path, 0, 0, 0); }
ssize_u read(int fd, void *buf, size_t_u n) { return syscall(SYS_READ, fd, (long)buf, (long)n, 0); }
int close(int fd)                           { return (int)syscall(SYS_CLOSE, fd, 0, 0, 0); }
int listdir(const char *path, dirent_u *out, int max) {
    return (int)syscall(SYS_LISTDIR, (long)path, (long)out, max, 0);
}

/* --- bellek: basit serbest-listesiz artan ayirici + tekrar kullanim ---
 * Gercek bir birlestirme (coalescing) yapmaz; kucuk userland
 * programlari icin yeterlidir ve free edilen bloklar ayni boyutta
 * yeniden kullanilabilir. */
typedef struct blk {
    size_t_u size;
    struct blk *next;
    int free_flag;
} blk_t;

static blk_t *heap_head = 0;

static void *sbrk(long inc) {
    long r = syscall(SYS_SBRK, inc, 0, 0, 0);
    if (r == -1) return 0;
    return (void *)r;
}

void *malloc(size_t_u size) {
    if (size == 0) return 0;
    size = (size + 15) & ~(size_t_u)15;   /* 16 bayt hizala */

    for (blk_t *b = heap_head; b; b = b->next) {
        if (b->free_flag && b->size >= size) { b->free_flag = 0; return (void *)(b + 1); }
    }

    blk_t *nb = (blk_t *)sbrk((long)(size + sizeof(blk_t)));
    if (!nb) return 0;
    nb->size = size;
    nb->free_flag = 0;
    nb->next = heap_head;
    heap_head = nb;
    return (void *)(nb + 1);
}

void free(void *p) {
    if (!p) return;
    blk_t *b = ((blk_t *)p) - 1;
    b->free_flag = 1;
}

void *memset(void *dst, int val, size_t_u n) {
    uint8_u *d = (uint8_u *)dst;
    for (size_t_u i = 0; i < n; i++) d[i] = (uint8_u)val;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t_u n) {
    uint8_u *d = (uint8_u *)dst;
    const uint8_u *s = (const uint8_u *)src;
    for (size_t_u i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* --- dize --- */
size_t_u strlen(const char *s) { size_t_u n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    size_t_u i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return (int)((uint8_u)a[i]) - (int)((uint8_u)b[i]);
}

int strncmp(const char *a, const char *b, size_t_u n) {
    for (size_t_u i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)((uint8_u)a[i]) - (int)((uint8_u)b[i]);
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    size_t_u i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

/* --- grafik --- */
void putpixel(int x, int y, uint32_u rgb) { syscall(SYS_PUTPIXEL, x, y, (long)rgb, 0); }


/* --- Asama 34: Uygulama API'si --- */
int pencere_ac(const char *baslik, int x, int y, int w, int h) {
    long xy = ((long)(x & 0xFFFF) << 16) | (long)(y & 0xFFFF);
    long wh = ((long)(w & 0xFFFF) << 16) | (long)(h & 0xFFFF);
    return (int)syscall(SYS_PENCERE_AC, (long)baslik, xy, wh, 0);
}
void pencere_doldur(int id, uint32_u renk) {
    syscall(SYS_PEN_DOLDUR, id, (long)renk, 0, 0);
}
void pencere_yazi(int id, int x, int y, const char *s, uint32_u renk) {
    long xy = ((long)(x & 0xFFFF) << 16) | (long)(y & 0xFFFF);
    syscall(SYS_PEN_YAZI, id, xy, (long)s, (long)renk);
}
void pencere_dikdortgen(int id, int x, int y, int w, int h, uint32_u renk) {
    long xy = ((long)(x & 0xFFFF) << 16) | (long)(y & 0xFFFF);
    long wh = ((long)(w & 0xFFFF) << 16) | (long)(h & 0xFFFF);
    syscall(SYS_PEN_DIK, id, xy, wh, (long)renk);
}
void pencere_kapat(int id) { syscall(SYS_PEN_KAPAT, id, 0, 0, 0); }
int servis_listesi(servis_bilgi_u *out, int max) {
    return (int)syscall(SYS_SERVIS_LISTE, (long)out, max, 0, 0);
}


/* --- IPC sarmalayicilari --- */
int ipc_ac(const char *isim)  { return (int)syscall(SYS_IPC_ACS, (long)isim, 0, 0, 0); }
int ipc_bul(const char *isim) { return (int)syscall(SYS_IPC_BUL, (long)isim, 0, 0, 0); }
int ipc_gonder(int hedef, uint32_u opkod, const void *veri, uint32_u uzunluk) {
    return (int)syscall(SYS_IPC_GONDER, hedef, (long)opkod, (long)veri, (long)uzunluk);
}
int ipc_al(int port, ipc_mesaj_u *cikti) {
    return (int)syscall(SYS_IPC_AL, port, (long)cikti, 0, 0);
}

/* --- Sistem bilgisi --- */
uint64_u sistem_bil(int alan) {
    return (uint64_u)syscall(SYS_SISTEM_BIL, alan, 0, 0, 0);
}
