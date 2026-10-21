/* PL011 UART surucusu - QEMU 'virt' makinesinde 0x09000000'da bulunur.
 * x86'daki 16550 seri portunun ARM karsiligi. */
#include <stdint.h>

#define UART0_BASE 0x09000000UL
#define UART_DR    0x00   /* veri kaydi */
#define UART_FR    0x18   /* bayrak kaydi */
#define UART_FR_TXFF (1 << 5)   /* gonderim FIFO dolu */

static volatile uint32_t *uart_reg(uint32_t ofs) {
    return (volatile uint32_t *)(UART0_BASE + ofs);
}

void uart_putc(char c) {
    while (*uart_reg(UART_FR) & UART_FR_TXFF) { }   /* yer acilmasini bekle */
    *uart_reg(UART_DR) = (uint32_t)c;
}

void uart_yaz(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_hex(uint64_t v) {
    static const char hx[] = "0123456789ABCDEF";
    uart_yaz("0x");
    int basladi = 0;
    for (int k = 60; k >= 0; k -= 4) {
        uint8_t n = (uint8_t)((v >> k) & 0xF);
        if (n || basladi || k == 0) { uart_putc(hx[n]); basladi = 1; }
    }
}
