#include "tss.h"
#include "serial.h"

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
static tss64_t g_tss;

extern uint8_t gdt64[]; /* boot.asm'de 'global gdt64' ile disa acildi */

#define GDT_TSS_OFFSET 0x30

static inline void ltr(uint16_t selector) {
    __asm__ volatile ("ltr %0" : : "r"(selector));
}

void tss_init(void) {
    uint8_t *raw = (uint8_t *)&g_tss;
    for (uint32_t i = 0; i < sizeof(tss64_t); i++) raw[i] = 0;

    /* Ring3'ten kesme/istisna gelince CPU'nun otomatik gececegi
     * cekirdek yigini - bu ayarlanmadan ring3'e girmek GUVENSIZDIR
     * (ilk kesmede cokme olur). */
    g_tss.rsp0 = (uint64_t)(uintptr_t)(tss_interrupt_stack + TSS_INTERRUPT_STACK_SIZE);
    g_tss.iomap_base = (uint16_t)sizeof(tss64_t);

    uint64_t base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t limit = (uint32_t)sizeof(tss64_t) - 1;

    uint8_t *desc = &gdt64[GDT_TSS_OFFSET];
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

    ltr(GDT_TSS_OFFSET);

    serial_write("tss: kuruldu ve LTR ile yuklendi (RSP0 ayarlandi)\n");
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}
