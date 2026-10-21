#ifndef OTUKENOS_IDT_H
#define OTUKENOS_IDT_H

#include <stdint.h>

/* 64-bit push sirasina gore (PUSH_ALL, tersten okuma):
 * r15..rax (15 alan) + int_no. err_code/rip/cs/rflags kasitli olarak
 * struct'a dahil edilmedi: ERR/NOERR yollarinda farkli konumlandiklari
 * icin (bkz. interrupt.asm yorumlari) ve su an hicbir C kodu bunlara
 * erismiyor - yalniz int_no kullaniliyor. */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;
};

void idt_install(void);

typedef void (*irq_handler_t)(struct regs *r);
void irq_install_handler(int irq, irq_handler_t handler);

#endif
