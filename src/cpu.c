#include "cpu.h"

static cpu_yerel_t cpular[CPU_AZAMI];

#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

static inline void wrmsr_(uint32_t m, uint64_t v) {
    __asm__ volatile ("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

uint32_t cpu_apic_id(void) {
    uint32_t a = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (b >> 24) & 0xFF;
}

/* GS_BASE'i bloga, KERNEL_GS_BASE'i 0'a (kullanicinin GS'i) kurar. */
static void gs_kur(cpu_yerel_t *c) {
    c->kendi = (struct cpu_yerel *)c;
    wrmsr_(MSR_GS_BASE, (uint64_t)(uintptr_t)c);
    wrmsr_(MSR_KERNEL_GS_BASE, 0);
}

void cpu_bsp_hazirla(void) {
    cpu_yerel_t *c = &cpular[0];
    c->indeks = 0;
    c->apic_id = cpu_apic_id();
    gs_kur(c);
    __atomic_store_n(&c->kullanimda, 1, __ATOMIC_RELEASE);
}

void cpu_ap_hazirla(uint32_t indeks, uint32_t apic_id) {
    if (indeks >= CPU_AZAMI) return;
    cpu_yerel_t *c = &cpular[indeks];
    c->indeks = indeks;
    c->apic_id = apic_id;
    gs_kur(c);
    __atomic_store_n(&c->kullanimda, 1, __ATOMIC_RELEASE);
}

cpu_yerel_t *cpu_yerel(void) {
    cpu_yerel_t *c = cpu_bu();
    c->syscall_sayisi++;
    return c;
}

cpu_yerel_t *cpu_indeksle(uint32_t indeks) {
    return indeks < CPU_AZAMI ? &cpular[indeks] : 0;
}

int cpu_sayisi(void) {
    int n = 0;
    for (int i = 0; i < CPU_AZAMI; i++)
        if (__atomic_load_n(&cpular[i].kullanimda, __ATOMIC_ACQUIRE)) n++;
    return n;
}

/* Asm ofsetleri C yerlesimiyle ayni olmali. */
_Static_assert(__builtin_offsetof(cpu_yerel_t, kendi)           == CPU_OFS_KENDI,        "ofs");
_Static_assert(__builtin_offsetof(cpu_yerel_t, cekirdek_rsp)    == CPU_OFS_CEKIRDEK_RSP, "ofs");
_Static_assert(__builtin_offsetof(cpu_yerel_t, ara_rsp)         == CPU_OFS_ARA_RSP,      "ofs");
_Static_assert(__builtin_offsetof(cpu_yerel_t, son_syscall_rip) == CPU_OFS_SON_RIP,      "ofs");
