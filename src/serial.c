#include "serial.h"
#include "cpu.h"
#include "spinlock.h"
#include "smp.h"
#include "sched.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static void serial_putc(char c) {
    while (!transmit_empty());
    outb(COM1, (uint8_t)c);
}

/* SMP adim 5: cagri basina kilit. Cok cekirdekte kilitsiz yazim
 * satirlari BAYT duzeyinde karistirir. Kilit sahibi cekirdek kaydedilir:
 * ayni cekirdekte yazarken istisna/NMI gelip o da yazarsa (yeniden
 * giris) beklemeden yazar - aksi halde panik ciktisi kilitlenirdi. */
static volatile int seri_kilit = 0;
static volatile int seri_sahip = -1;

/* --- Onek tamponu (gorev basina) --- seri kilit altinda. */
#define ONEK_YUVA 6
static struct { const void *sahip; int n; char b[240]; } onek[ONEK_YUVA];

static void ham_yaz(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static int seri_al(void) {           /* 1: yeniden giris (kilit zaten bizde) */
    int ben = (int)cpu_bu()->indeks;
    if (__atomic_load_n(&seri_sahip, __ATOMIC_ACQUIRE) == ben) return 1;
    while (__atomic_exchange_n(&seri_kilit, 1, __ATOMIC_ACQUIRE)) {
        smp_bekleyen_isle();
        __asm__ volatile ("pause");
    }
    __atomic_store_n(&seri_sahip, ben, __ATOMIC_RELEASE);
    return 0;
}
static void seri_birak(int yeniden) {
    if (yeniden) return;
    __atomic_store_n(&seri_sahip, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&seri_kilit, 0, __ATOMIC_RELEASE);
}

static int onek_yuvasi(const void *ben, int yarat) {
    for (int i = 0; i < ONEK_YUVA; i++)
        if (onek[i].n > 0 && onek[i].sahip == ben) return i;
    if (!yarat) return -1;
    for (int i = 0; i < ONEK_YUVA; i++)
        if (onek[i].n == 0) { onek[i].sahip = ben; return i; }
    return -1;
}

static void onek_bosalt(int y) {     /* kilit altinda */
    onek[y].b[onek[y].n] = 0;
    ham_yaz(onek[y].b);
    onek[y].n = 0;
}

void serial_write(const char *s) {
    uint64_t f = irq_kaydet_kapat();
    int yeniden = seri_al();
    if (!yeniden) {
        int y = onek_yuvasi(scheduler_current_task(), 0);
        if (y >= 0) onek_bosalt(y);       /* bekleyen onek: once o */
    }
    ham_yaz(s);
    seri_birak(yeniden);
    irq_geri_yukle(f);
}

void seri_onek_ekle(const char *s) {
    uint64_t f = irq_kaydet_kapat();
    int yeniden = seri_al();
    int y = yeniden ? -1 : onek_yuvasi(scheduler_current_task(), 1);
    if (y < 0) {                          /* yuva yok / yeniden giris: dogrudan */
        ham_yaz(s);
    } else {
        for (; *s; s++) {
            onek[y].b[onek[y].n++] = *s;
            if (*s == '\n' || onek[y].n >= (int)sizeof(onek[y].b) - 1) {
                const void *sahip = onek[y].sahip;
                onek_bosalt(y);
                if (s[1]) onek[y].sahip = sahip;
            }
        }
    }
    seri_birak(yeniden);
    irq_geri_yukle(f);
}

void serial_write_hex(unsigned int val) {
    const char *hex = "0123456789ABCDEF";
    char t[11];               /* tek serial_write: kilit altinda butun */
    t[0] = '0'; t[1] = 'x';
    for (int i = 0; i < 8; i++) t[2 + i] = hex[(val >> (28 - 4 * i)) & 0xF];
    t[10] = 0;
    serial_write(t);
}
