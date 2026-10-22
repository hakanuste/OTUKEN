#include "keyboard.h"
#include "serial.h"
#include "spinlock.h"
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
static int altgr_down = 0;          /* 1.4.9: sag Alt (E0 38) */
static int ctrl_down = 0;
static int e0_onek = 0;             /* genisletilmis tus oneki (E0) */

/* 1.4.9: Turkce-Q AltGr katmani (scancode set 1). Adres yazarken gereken
 * @ # $ { } [ ] \ | ~ < > vb. Eskiden hic yoktu. */
static unsigned char altgr_tr(uint8_t sc) {
    switch (sc) {
    case 0x02: return '>';  case 0x03: return 0xA3; /* £ */
    case 0x04: return '#';  case 0x05: return '$';
    case 0x08: return '{';  case 0x09: return '[';
    case 0x0A: return ']';  case 0x0B: return '}';
    case 0x0C: return '\\'; case 0x0D: return '|';
    case 0x10: return '@';  case 0x1B: return '~';
    case 0x2B: return '`';  case 0x56: return '|';
    default:   return 0;
    }
}

/* Genisletilmis tuslar -> ozel baytlar (0x80-0x8F, Latin-5'te kullanilmaz;
 * klavye_kod_noktasi bunlari U+E000 + n ozel alanina cevirir). */
static unsigned char genis_tus(uint8_t sc) {
    switch (sc) {
    case 0x48: return KLAVYE_YUKARI;  case 0x50: return KLAVYE_ASAGI;
    case 0x4B: return KLAVYE_SOL;     case 0x4D: return KLAVYE_SAG;
    case 0x49: return KLAVYE_SAYFA_YUKARI; case 0x51: return KLAVYE_SAYFA_ASAGI;
    case 0x47: return KLAVYE_BAS;     case 0x4F: return KLAVYE_SON;
    case 0x53: return KLAVYE_SIL;     case 0x1C: return 13;       /* tus takimi Enter */
    case 0x35: return '/';                                         /* tus takimi / */
    default:   return 0;
    }
}

/* SMP adim 6: kuyrugun IKI ureticisi (IRQ1, ana dongudeki USB klavye)
 * ve IKI tuketicisi (kabuk, UMAY) var; tek-uretici/tek-tuketici varsayimi
 * artik gecersiz. Kisa islem, kesmeden de alinir -> donen kilit. */
static spinlock_t kbuf_kilit;

static volatile int odak = KLAVYE_ODAK_KABUK;
static volatile int odak_bildir = 0;

int  klavye_odak(void) { return odak; }
void klavye_odak_ata(int o) { odak = o; odak_bildir = 1; }

static void kbuf_push(char c) {
    if (c == '\t') {                  /* odak degistir; kuyruga girmez */
        odak = (odak == KLAVYE_ODAK_KABUK) ? KLAVYE_ODAK_PENCERE : KLAVYE_ODAK_KABUK;
        odak_bildir = 1;              /* IRQ'da yazmayiz; gorev baglaminda bildirilir */
        return;
    }
    uint64_t f = spin_lock_irqsave(&kbuf_kilit);
    int next = (kbuf_head + 1) % KBUF_SIZE;
    if (next != kbuf_tail) {
        kbuf[kbuf_head] = c;
        kbuf_head = next;
    }
    spin_unlock_irqrestore(&kbuf_kilit, f);
}

char keyboard_pop_char(void) {
    uint64_t f = spin_lock_irqsave(&kbuf_kilit);
    char c = 0;
    if (kbuf_tail != kbuf_head) {
        c = kbuf[kbuf_tail];
        kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    }
    spin_unlock_irqrestore(&kbuf_kilit, f);
    return c;
}

/* Klavye kuyrugundaki bayt ISO-8859-9'dur; Unicode kod noktasina cevirir. */
uint32_t klavye_kod_noktasi(unsigned char c) {
    switch (c) {
    case 0xD0: return 0x011E;   /* Ğ */
    case 0xDD: return 0x0130;   /* İ */
    case 0xDE: return 0x015E;   /* Ş */
    case 0xF0: return 0x011F;   /* ğ */
    case 0xFD: return 0x0131;   /* ı */
    case 0xFE: return 0x015F;   /* ş */
    default:
        if (c >= 0x80 && c <= 0x8F) return 0xE000u + (c - 0x80u);   /* 1.4.9 ozel tuslar */
        return c;
    }
}

char keyboard_pop_for(int tuketici) {
    if (__atomic_exchange_n(&odak_bildir, 0, __ATOMIC_ACQ_REL))
        serial_write(odak == KLAVYE_ODAK_KABUK ? "KLAVYE: odak -> kabuk\n"
                                               : "KLAVYE: odak -> pencere (UMAY)\n");
    if (tuketici != odak) return 0;
    char c = keyboard_pop_char();
    /* 1.4.9: kabuk ozel tuslari (oklar vb.) tanimaz: ona verilmez */
    while (tuketici == KLAVYE_ODAK_KABUK && (unsigned char)c >= 0x80 && (unsigned char)c <= 0x8F)
        c = keyboard_pop_char();
    return c;
}

static void keyboard_handler(struct regs *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == 0xE0) { e0_onek = 1; return; }
    int genis = e0_onek;
    e0_onek = 0;

    if (sc == 0x2A || sc == 0x36) { if (!genis) shift_down = 1; return; }  /* E0 2A: sahte shift */
    if (sc == 0xAA || sc == 0xB6) { if (!genis) shift_down = 0; return; }
    if (sc == 0x38) { if (genis) altgr_down = 1; return; }                  /* sol Alt: yok sayilir */
    if (sc == 0xB8) { if (genis) altgr_down = 0; return; }
    if (sc == 0x1D) { ctrl_down = 1; return; }                             /* sol ya da sag Ctrl */
    if (sc == 0x9D) { ctrl_down = 0; return; }

    if (sc & 0x80) {
        /* tus birakma (break code) - simdilik yok sayiliyor */
        return;
    }

    if (genis) {
        unsigned char g = genis_tus(sc);
        if (g) kbuf_push((char)g);
        return;
    }
    if (sc == 0x3F) { kbuf_push((char)KLAVYE_F5); return; }
    if (sc == 0x40) { kbuf_push((char)KLAVYE_F6); return; }
    if (ctrl_down && sc == 0x26) { kbuf_push((char)KLAVYE_F6); return; }     /* Ctrl+L: adres */
    if (sc == 0x56 && !altgr_down) { kbuf_push(shift_down ? '>' : '<'); return; }

    if (sc < 128) {
        unsigned char c;
        if (altgr_down) c = altgr_tr(sc);
        else c = shift_down ? scancode_ascii_shift[sc] : scancode_ascii[sc];
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
    kbuf_push(c);                     /* kuyruk doluysa yeni tus atilir */
}
