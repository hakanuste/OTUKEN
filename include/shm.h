#ifndef OTUKENOS_SHM_H
#define OTUKENOS_SHM_H

#include <stdint.h>

#define SHM_MAX_SEGMENTS   8
#define SHM_MAX_FRAMES    16 /* segment basina en fazla 16*4KB=64KB */

/* NOT: su anki cekirdek tum gorevler icin TEK bir adres uzayi (identity
 * map) kullaniyor (ayri surec adres uzaylari/Ring3 sonraki asamalar).
 * Bu yuzden bu API su an icin "altyapi" niteliginde: gercek fiziksel
 * cerceveleri PMM'den ayirip VMM ile ISTENEN herhangi bir sanal adrese
 * haritalama mekanizmasini kurar ve test eder - ileride her surecin
 * kendi CR3'u oldugunda, farkli surecler ayni segment'i KENDI
 * adreslerine attach edebilecek, mekanizma zaten hazir olacak. */

uint32_t shm_create(uint64_t size);
void *shm_attach(uint32_t id, uint64_t desired_virt_addr);
int shm_detach(uint32_t id, void *ptr);
int shm_destroy(uint32_t id);

#endif
