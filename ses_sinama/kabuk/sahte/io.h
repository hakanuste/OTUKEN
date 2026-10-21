/* TAKLIT io.h — ses koşumu için.
 * Gerçek include/io.h, kapı işlemlerini "static inline" asm olarak
 * tanımlar; ana bilgisayarda bunlar gerçek donanıma gider. Koşum
 * bunları kendi işlevleriyle değiştirir, bu yüzden içerme yolunda
 * gerçeğinden ÖNCE durur. */
#ifndef OTUKENOS_IO_H
#define OTUKENOS_IO_H
#include <stdint.h>
void     outb(uint16_t port, uint8_t  val);
uint8_t  inb(uint16_t port);
void     outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);
void     outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);
void     io_wait(void);
#endif
