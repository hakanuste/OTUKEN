#ifndef OTUKENOS_SERIAL_H
#define OTUKENOS_SERIAL_H

void serial_init(void);
void serial_write(const char *s);
void serial_write_hex(unsigned int val);

#endif
