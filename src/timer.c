#include "timer.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint64_t ticks = 0;
static uint32_t tik_hz = 0;

static void timer_irq_handler(struct regs *r) {
    (void)r;
    ticks++;
    scheduler_tick();
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    tik_hz = frequency_hz;
    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_COMMAND, 0x36); /* kanal0, lo/hi bayt, mod3 (kare dalga), binary */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_install_handler(0, timer_irq_handler);
    pic_unmask_irq(0);
}

uint32_t timer_hz(void) { return tik_hz; }

uint64_t timer_get_ticks(void) {
    return ticks;
}
