#include "mouse.h"
#include "idt.h"
#include "io.h"
#include "fb.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout--) {
        if ((inb(PS2_STATUS) & 0x02) == 0) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01) return;
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, val);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(PS2_DATA);
}

static int8_t  packet[3];
static int     packet_idx = 0;
static int     mouse_x, mouse_y;
static int     mouse_buttons = 0;
static volatile int dirty = 0;

static void mouse_handler(struct regs *r) {
    (void)r;
    uint8_t data = inb(PS2_DATA);

    switch (packet_idx) {
        case 0:
            if (!(data & 0x08)) return; /* gecersiz paket basi, atla */
            packet[0] = (int8_t)data;
            packet_idx = 1;
            break;
        case 1:
            packet[1] = (int8_t)data;
            packet_idx = 2;
            break;
        case 2:
            packet[2] = (int8_t)data;
            packet_idx = 0;

            {
                int dx = (int8_t)packet[1];
                int dy = (int8_t)packet[2];
                mouse_buttons = packet[0] & 0x07;

                mouse_x += dx;
                mouse_y -= dy; /* PS/2'de Y ekseni ters */

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x >= fb_width())  mouse_x = fb_width()  - 1;
                if (mouse_y >= fb_height()) mouse_y = fb_height() - 1;

                dirty = 1;
            }
            break;
    }
}

void mouse_get_state(int *x, int *y, int *buttons) {
    *x = mouse_x;
    *y = mouse_y;
    *buttons = mouse_buttons;
}

int mouse_poll_dirty(void) {
    if (dirty) { dirty = 0; return 1; }
    return 0;
}

void mouse_install(void) {
    mouse_x = fb_width()  / 2;
    mouse_y = fb_height() / 2;

    /* aux (fare) portunu etkinlestir */
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);

    /* controller yapilandirma baytini oku, IRQ12'yi etkinlestir */
    mouse_wait_write();
    outb(PS2_CMD, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;   /* IRQ12 (aux interrupt) etkin */
    status &= ~0x20;  /* aux clock etkin (disable bitini temizle) */
    mouse_wait_write();
    outb(PS2_CMD, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, status);

    /* fareyi varsayilanlara ayarla, sonra paket akisini ac */
    mouse_write(0xF6);
    mouse_read(); /* ACK */
    mouse_write(0xF4);
    mouse_read(); /* ACK */

    irq_install_handler(12, mouse_handler);
}
