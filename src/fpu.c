/* 1.4.9 — x87/SSE durumu (bkz. include/fpu.h) */
#include "fpu.h"
#include "heap.h"
#include "serial.h"

static uint8_t sablon[FPU_ALAN] __attribute__((aligned(16)));
static volatile int etkin;

static void cpuid(uint32_t yaprak, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(yaprak), "c"(0));
}

int fpu_ac(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1u << 24)) || !(d & (1u << 25)) || !(d & (1u << 26))) return -1;   /* FXSR, SSE, SSE2 */
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1ULL << 2) | (1ULL << 3));       /* EM=0 (öykünme yok), TS=0 */
    cr0 |= (1ULL << 1) | (1ULL << 5);          /* MP=1, NE=1 (yerel x87 hata bildirimi) */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);         /* OSFXSR, OSXMMEXCPT */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    uint32_t mxcsr = 0x1F80;                   /* bütün istisnalar maskeli, en yakına yuvarla */
    __asm__ volatile ("fninit; ldmxcsr %0" : : "m"(mxcsr));
    if (!etkin) {                              /* ilk çağrı (BSP): şablon */
        __asm__ volatile ("fxsave64 (%0)" : : "r"(sablon) : "memory");
        etkin = 1;
    }
    return 0;
}

int fpu_etkin(void) { return etkin; }

uint8_t *fpu_alan_ayir(void **ham) {
    *ham = 0;
    if (!etkin) return 0;
    uint8_t *p = (uint8_t *)kmalloc(FPU_ALAN + 16);
    if (!p) return 0;
    uint8_t *a = (uint8_t *)(((uintptr_t)p + 15) & ~(uintptr_t)15);
    for (int i = 0; i < FPU_ALAN; i++) a[i] = sablon[i];
    *ham = p;
    return a;
}
