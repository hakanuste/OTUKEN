#include "keyboard.h"
#include "idt.h"
#include "io.h"

#define KBD_DATA_PORT 0x60

/* Turkce Q klavye duzeni (Microsoft KBDTUQ.DLL referans alinarak,
 * scancode set 1). Harfler ISO-8859-9 kod noktalarinda:
 * Ç=0xC7 ç=0xE7 Ğ=0xD0 ğ=0xF0 İ=0xDD ı=0xFD Ö=0xD6 ö=0xF6
 * Ş=0xDE ş=0xFE Ü=0xDC ü=0xFC
 *
 * NOT: standart 'i' harfi noktali kucuk i'dir (Turkce'de ayri harf),
 * 'I' ise noktasiz BUYUK harftir - fontumuzda bu ayrim gozetildi. */

static unsigned char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0', '*','-', 8,
    '\t','q','w','e','r','t','y','u',0xFD,'o','p',0xF0,0xFC, 13,
    0,
    'a','s','d','f','g','h','j','k','l',0xFE,'i','"',
    0,
    ',','z','x','c','v','b','n','m',0xF6,0xE7,'.',
    0,
    '*',
    0,
    ' ',
    0,
};

static unsigned char scancode_ascii_shift[128] = {
    0,  27, '!','\'','^','+','%','&','/','(',')','=', '?','_', 8,
    '\t','Q','W','E','R','T','Y','U','I','O','P',0xD0,0xDC, 13,
    0,
    'A','S','D','F','G','H','J','K','L',0xDE,0xDD,'\'',
    0,
    ';','Z','X','C','V','B','N','M',0xD6,0xC7,':',
    0,
    '*',
    0,
    ' ',
    0,
};

#define KBUF_SIZE 256
static char kbuf[KBUF_SIZE];
static int  kbuf_head = 0, kbuf_tail = 0;

static int shift_down = 0;

static void kbuf_push(char c) {
    int next = (kbuf_head + 1) % KBUF_SIZE;
    if (next != kbuf_tail) {
        kbuf[kbuf_head] = c;
        kbuf_head = next;
    }
}

char keyboard_pop_char(void) {
    if (kbuf_tail == kbuf_head)
        return 0;
    char c = kbuf[kbuf_tail];
    kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    return c;
}

static void keyboard_handler(struct regs *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = 0; return; }

    if (sc & 0x80) {
        /* tus birakma (break code) - simdilik yok sayiliyor */
        return;
    }

    if (sc < 128) {
        unsigned char c = shift_down ? scancode_ascii_shift[sc] : scancode_ascii[sc];
        if (c) kbuf_push((char)c);
    }
}

void keyboard_install(void) {
    irq_install_handler(1, keyboard_handler);
}

/* Baska bir klavye kaynagi (ornegin USB) tarafindan uretilen karakteri
 * AYNI kuyruga koyar. Boylece ust katmanlar (kabuk, GUI) klavyenin
 * PS/2 mi USB mi oldugunu bilmek zorunda kalmaz. */
void keyboard_push_char(char c) {
    int next = (kbuf_head + 1) % KBUF_SIZE;
    if (next == kbuf_tail) return;   /* kuyruk dolu - yeni tusu at */
    kbuf[kbuf_head] = c;
    kbuf_head = next;
}
