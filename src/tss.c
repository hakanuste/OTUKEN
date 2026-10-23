#include "tss.h"
#include "serial.h"
#include "cpu.h"

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

#define TSS_INTERRUPT_STACK_SIZE (16 * 1024)
static uint8_t tss_interrupt_stack[TSS_INTERRUPT_STACK_SIZE];

/* SMP adim 4: cekirdek-basina TSS. Bir TSS tanimlayicisi LTR ile
 * yuklenince 'mesgul' olur ve ikinci bir cekirdekte yeniden yuklenemez;
 * RSP0 da cekirdegin O AN calistirdigi goreve aittir. Bu yuzden her
 * cekirdegin kendi TSS'i ve (TSS tanimlayicisini tasiyan) kendi GDT
 * kopyasi vardir. BSP, boot.asm'deki gdt64'u kullanmaya devam eder. */
static tss64_t tsslar[CPU_AZAMI];
static uint64_t gdt_kopya[CPU_AZAMI][8] __attribute__((aligned(16)));

extern uint8_t gdt64[]; /* boot.asm'de 'global gdt64' ile disa acildi */

#define GDT_TSS_OFFSET 0x30

static inline void ltr(uint16_t selector) {
    __asm__ volatile ("ltr %0" : : "r"(selector));
}

static void tanimlayici_yaz(uint8_t *desc, tss64_t *t) {
    uint64_t base = (uint64_t)(uintptr_t)t;
    uint32_t limit = (uint32_t)sizeof(tss64_t) - 1;
    desc[0]  = (uint8_t)(limit & 0xFF);
    desc[1]  = (uint8_t)((limit >> 8) & 0xFF);
    desc[2]  = (uint8_t)(base & 0xFF);
    desc[3]  = (uint8_t)((base >> 8) & 0xFF);
    desc[4]  = (uint8_t)((base >> 16) & 0xFF);
    desc[5]  = 0x89; /* P=1,DPL=0,S=0(sistem),type=9 (musait 64-bit TSS) */
    desc[6]  = (uint8_t)((limit >> 16) & 0x0F);
    desc[7]  = (uint8_t)((base >> 24) & 0xFF);
    desc[8]  = (uint8_t)((base >> 32) & 0xFF);
    desc[9]  = (uint8_t)((base >> 40) & 0xFF);
    desc[10] = (uint8_t)((base >> 48) & 0xFF);
    desc[11] = (uint8_t)((base >> 56) & 0xFF);
    desc[12] = 0; desc[13] = 0; desc[14] = 0; desc[15] = 0;
}

static void tss_hazirla(tss64_t *t, uint64_t rsp0) {
    uint8_t *raw = (uint8_t *)t;
    for (uint32_t i = 0; i < sizeof(tss64_t); i++) raw[i] = 0;
    t->rsp0 = rsp0;
    t->iomap_base = (uint16_t)sizeof(tss64_t);
}

void tss_init(void) {
    /* Ring3'ten kesme/istisna gelince CPU'nun otomatik gececegi
     * cekirdek yigini - bu ayarlanmadan ring3'e girmek GUVENSIZDIR
     * (ilk kesmede cokme olur). Zamanlayici bunu her gecis'te gorevin
     * kendi yiginina cevirir (tss_set_rsp0). */
    tss64_t *t = &tsslar[cpu_bu()->indeks];
    uint64_t onceki = t->rsp0;   /* tss_init'ten once gelen set_rsp0 korunur */
    tss_hazirla(t, onceki ? onceki
                          : (uint64_t)(uintptr_t)(tss_interrupt_stack + TSS_INTERRUPT_STACK_SIZE));
    tanimlayici_yaz(&gdt64[GDT_TSS_OFFSET], t);
    ltr(GDT_TSS_OFFSET);
    serial_write("tss: kuruldu ve LTR ile yuklendi (RSP0 ayarlandi)\n");
}

int tss_ap_kur(uint32_t indeks, uint64_t rsp0) {
    if (indeks == 0 || indeks >= CPU_AZAMI) return -1;
    uint8_t *g = (uint8_t *)gdt_kopya[indeks];
    for (int i = 0; i < 64; i++) g[i] = gdt64[i];
    tss_hazirla(&tsslar[indeks], rsp0);
    tanimlayici_yaz(&g[GDT_TSS_OFFSET], &tsslar[indeks]);
    struct __attribute__((packed)) { uint16_t sinir; uint64_t taban; } gp =
        { 63, (uint64_t)(uintptr_t)g };
    __asm__ volatile (
        "lgdt %0\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t mov %%ax, %%es\n\t mov %%ax, %%ss\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : "m"(gp) : "rax", "memory");
    /* FS/GS secicisine DOKUNULMAZ: GS yuklemek GS_BASE'i sifirlar. */
    ltr(GDT_TSS_OFFSET);
    return 0;
}

void tss_set_rsp0(uint64_t rsp0) {
    tsslar[cpu_bu()->indeks].rsp0 = rsp0;
}
