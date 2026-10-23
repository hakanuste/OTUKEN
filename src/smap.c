/* 1.4.9: SMEP/SMAP acma (bkz. include/smap.h) */
#include "smap.h"
#include <stdint.h>

volatile int g_smap_etkin = 0;

int smep_smap_ac(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 7) return 0;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    int smep = (b >> 7) & 1, smap = (b >> 20) & 1;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (smep) cr4 |= 1ULL << 20;
    if (smap) cr4 |= 1ULL << 21;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    if (smap) { __asm__ volatile ("clac" ::: "memory", "cc"); g_smap_etkin = 1; }
    return smep | (smap << 1);
}
