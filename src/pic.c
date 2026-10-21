#include "pic.h"
#include "io.h"

#define PIC1        0x20
#define PIC1_COMMAND PIC1
#define PIC1_DATA    0x21
#define PIC2        0xA0
#define PIC2_COMMAND PIC2
#define PIC2_DATA    0xA1

#define ICW1_INIT   0x10
#define ICW1_ICW4   0x01
#define ICW4_8086   0x01

/* PIC'i, IRQ0-15'i CPU'nun kendi exception vektorleriyle (0-31)
 * cakismayacak sekilde INT 0x20-0x2F araligina tasir. */
void pic_remap(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);   /* master: IRQ0-7 -> INT 0x20-0x27 */
    io_wait();
    outb(PIC2_DATA, 0x28);   /* slave:  IRQ8-15 -> INT 0x28-0x2F */
    io_wait();
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}

/* Miras kalinan (BIOS/GRUB'dan gelen) maske durumuna guvenmek yerine,
 * ihtiyac duyulan bir IRQ hattini acikca aciyoruz - ozellikle timer
 * (IRQ0) icin, scheduler'in duzgun calismasi buna bagli. */
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
