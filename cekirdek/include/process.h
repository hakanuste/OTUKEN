#ifndef OTUKENOS_PROCESS_H
#define OTUKENOS_PROCESS_H

#include <stdint.h>
#include "sched.h"

#define MAX_PROCESSES 16

typedef struct process {
    uint32_t pid;
    char name[16];
    task_t *task;   /* Thread Manager asamasi oncesi: 1 process = 1 task */
    uint64_t address_space;  /* surece ozel PML4 (0 = cekirdek alani) */
    int active;
} process_t;

void process_manager_init(void);

/* Yeni bir surec olusturur (kendi gorevini de olusturur). Basarisizsa 0
 * doner (PMM/heap yetersiz ya da process tablosu dolu). */
process_t *process_create(const char *name, void (*entry)(void));

/* Su an CALISAN sureci sonlandirir - surecin KENDI kodu tarafindan
 * cagirilmalidir. Asla geri donmez. */
void process_exit_current(void);

/* pid'ye sahip, BASKA (calismakta olan degil) bir sureci disaridan
 * sonlandirir ve kaynaklarini hemen geri alir. Basari:0, hata:-1
 * (pid bulunamadi ya da bu, su an calisan surecin kendisi). */
int process_destroy(uint32_t pid);

process_t *process_find(uint32_t pid);
int process_count_active(void);

#endif
