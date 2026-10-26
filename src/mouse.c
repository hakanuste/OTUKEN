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
/* Sol düğmeye basıldı mandalı: basma ve bırakma ana döngünün iki yoklaması
 * arasına sığarsa (hızlı tık) GUI basmayı hiç görmüyordu. Mandal, basmayı
 * bir sonraki okumada bir kez bildirir; bırakma bir sonraki okumada gelir. */
static volatile int sol_mandal = 0;

static void mouse_handler(struct regs *r) {
    (void)r;
    uint8_t data = inb(PS2_DATA);

    switch (packet_idx) {
        case 0:
            /* Gecikmeli komut onayi (0xFA) bit 3'u tasir ve paket basi
             * sanilirdi: akis bir bayt kayar, ilk tik kaybolurdu. */
            if (data == 0xFA || data == 0xFE) return;
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
                if (mouse_buttons & 1) sol_mandal = 1;

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

/* USB HID fare/tablet (ana dongu; IF kapali: PS/2 kesmesiyle yarismaz).
 * goreli: dx,dy piksel (dy asagi pozitif). mutlak: 0..azami olcekte. */
static void usb_dugme(int b) {
    mouse_buttons = b & 0x07;
    if (mouse_buttons & 1) sol_mandal = 1;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= fb_width())  mouse_x = fb_width()  - 1;
    if (mouse_y >= fb_height()) mouse_y = fb_height() - 1;
    dirty = 1;
}
void mouse_usb_goreli(int dx, int dy, int dugmeler) {
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    mouse_x += dx; mouse_y += dy;
    usb_dugme(dugmeler);
    __asm__ volatile ("push %0; popfq" :: "r"(f) : "memory", "cc");
}
void mouse_usb_mutlak(uint32_t x, uint32_t y, uint32_t x_azami, uint32_t y_azami, int dugmeler) {
    if (x_azami == 0 || y_azami == 0) return;
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    mouse_x = (int)((uint64_t)x * (uint64_t)fb_width() / ((uint64_t)x_azami + 1));
    mouse_y = (int)((uint64_t)y * (uint64_t)fb_height() / ((uint64_t)y_azami + 1));
    usb_dugme(dugmeler);
    __asm__ volatile ("push %0; popfq" :: "r"(f) : "memory", "cc");
}

void mouse_get_state(int *x, int *y, int *buttons) {
    *x = mouse_x;
    *y = mouse_y;
    int b = mouse_buttons;
    if (sol_mandal) {
        sol_mandal = 0;
        if (!(b & 1)) dirty = 1;       /* bırakma da ayrı bir okumada görülsün */
        b |= 1;
    }
    *buttons = b;
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
    /* artakalan baytlari (gec gelen onaylar) bosalt: paket hizasi temiz baslar */
    for (int i = 0; i < 16 && (inb(PS2_STATUS) & 0x01); i++) (void)inb(PS2_DATA);
    packet_idx = 0;

    irq_install_handler(12, mouse_handler);
}
