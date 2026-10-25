#ifndef OTUKENOS_SERIAL_H
#define OTUKENOS_SERIAL_H

void serial_init(void);
void serial_write(const char *s);
void serial_write_hex(unsigned int val);
/* Onek tamponu: metni CAGIRAN GOREV icin bekletir; '\n' ile biterse
 * hemen, bitmezse ayni gorevin sonraki serial_write'i ile BIRLIKTE
 * (tek kilit tutusunda) basilir. Parcali sinama satirlari icin. */
void seri_onek_ekle(const char *s);

#endif
