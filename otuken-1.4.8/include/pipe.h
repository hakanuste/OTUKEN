#ifndef OTUKENOS_PIPE_H
#define OTUKENOS_PIPE_H

#include <stdint.h>
#include "spinlock.h"
#include "sched.h"

#define PIPE_BUFFER_SIZE 1024
#define PIPE_MAX_WAITERS    8

typedef struct {
    uint8_t buffer[PIPE_BUFFER_SIZE];
    uint32_t read_pos, write_pos, count;
    task_t *waiting_readers[PIPE_MAX_WAITERS];
    int waiting_reader_count;
    task_t *waiting_writers[PIPE_MAX_WAITERS];
    int waiting_writer_count;
    /* SMP: her nesne KENDI kilidini tasir. Kuresel tek kilit yerine
     * nesne basina kilit, farkli kuyruklarin birbirini bloke
     * etmemesini saglar. */
    spinlock_t kilit;
} pipe_t;

void pipe_init(pipe_t *p);

/* Tampon DOLUYSA CAGIRANI BLOKE EDER (yer acilana kadar), sonra
 * yazmaya devam eder. Yazilan bayt sayisini dondurur (her zaman
 * istenen 'size' kadar - blokladigi icin kismi yazim olmaz). */
uint32_t pipe_write(pipe_t *p, const void *data, uint32_t size);

/* Tampon BOSSA CAGIRANI BLOKE EDER, en az 1 bayt gelince okuyup
 * doner (bir akis/stream gibi - mesaj sinirlarini korumaz). */
uint32_t pipe_read(pipe_t *p, void *out, uint32_t max_size);

#endif
