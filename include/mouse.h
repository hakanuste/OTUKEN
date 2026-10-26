#ifndef OTUKENOS_MOUSE_H
#define OTUKENOS_MOUSE_H
#include <stdint.h>

void mouse_install(void);
void mouse_get_state(int *x, int *y, int *buttons);
/* 1.4.9: USB HID fare (goreli) ve tablet (mutlak) olaylari */
void mouse_usb_goreli(int dx, int dy, int dugmeler);
void mouse_usb_mutlak(uint32_t x, uint32_t y, uint32_t x_azami, uint32_t y_azami, int dugmeler);
/* Son okumadan beri fare hareket etti/tiklandi mi? Okunca bayragi sifirlar. */
int mouse_poll_dirty(void);

#endif
