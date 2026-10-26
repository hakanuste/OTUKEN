/* HID klavye ceviricisi ana makine sinamasi (gercek src/hid.c).
 * Iki USB klavye: birinin raporu digerinin basili tusunu birakmamali. */
#include <stdio.h>
#include <string.h>
#include "hid.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
static uint8_t kod[256]; static int kn;
void keyboard_tarama_kodu(uint8_t sc) { if (kn < 256) kod[kn++] = sc; }
void mouse_usb_goreli(int dx, int dy, int b) { (void)dx; (void)dy; (void)b; }
void mouse_usb_mutlak(uint32_t x, uint32_t y, uint32_t xa, uint32_t ya, int b) { (void)x; (void)y; (void)xa; (void)ya; (void)b; }
uint64_t timer_get_ticks(void) { return 0; }
uint32_t timer_hz(void) { return 100; }
static int kalan;
static void kosul(const char *ad, int k) { printf("HID-SINA %s: %s\n", ad, k ? "GECTI" : "KALDI"); if (!k) kalan++; }
static int var_mi(uint8_t sc) { for (int i = 0; i < kn; i++) if (kod[i] == sc) return 1; return 0; }
int main(void) {
    uint8_t a_bas[8] = { 0x02, 0, 0x04 }, bos[8] = { 0 }, b_x[8] = { 0, 0, 0x1B };
    kn = 0; hid_klavye_rapor(0, a_bas, 8);                 /* A: Shift + a */
    kosul("A: Shift ve 'a' basildi", var_mi(0x2A) && var_mi(0x1E));
    kn = 0; hid_klavye_rapor(1, bos, 8);                    /* B: bos rapor */
    kosul("B'nin bos raporu A'nin tuslarini BIRAKMADI", kn == 0);
    kn = 0; hid_klavye_rapor(1, b_x, 8);                    /* B: x */
    kosul("B: 'x' basildi, A'nin tuslari tekrar basilmadi", kn == 1 && kod[0] == 0x2D);
    kn = 0; hid_klavye_rapor(0, bos, 8);                    /* A birakir */
    kosul("A birakinca Shift ve 'a' birakildi, 'x' basili kaldi", var_mi(0xAA) && var_mi(0x9E) && !var_mi(0xAD));
    kn = 0; hid_klavye_rapor(1, bos, 8);
    kosul("B birakinca 'x' birakildi", kn == 1 && kod[0] == 0xAD);
    printf("=== HID-SINA: %s (kalan=%d) ===\n", kalan ? "KALDI" : "GECTI", kalan);
    return kalan ? 1 : 0;
}
