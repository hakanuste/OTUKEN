#ifndef OTUKENOS_MSGQUEUE_H
#define OTUKENOS_MSGQUEUE_H

#include <stdint.h>
#include "spinlock.h"
#include "sched.h"

/* Asama 31: ipc_message_t (108 bayt) sigacak sekilde buyutuldu */
#define MSG_MAX_SIZE      128
#define MSG_QUEUE_CAPACITY 16
#define MSG_MAX_WAITERS     8

typedef struct {
    uint8_t  data[MSG_MAX_SIZE];
    uint32_t size;
} message_t;

typedef struct {
    message_t messages[MSG_QUEUE_CAPACITY];
    int head, tail, count;
    task_t *waiting_receivers[MSG_MAX_WAITERS];
    int waiting_receiver_count;
    /* SMP: her nesne KENDI kilidini tasir. Kuresel tek kilit yerine
     * nesne basina kilit, farkli kuyruklarin birbirini bloke
     * etmemesini saglar. */
    spinlock_t kilit;
} msgqueue_t;

void msgqueue_init(msgqueue_t *q);

/* Kuyruk DOLUYSA basarisiz olur (-1) - su an bloklayan gonderim yok
 * (basit tutuldu). Basari: 0. */
int msgqueue_send(msgqueue_t *q, const void *data, uint32_t size);

/* Kuyruk BOSSA CAGIRANI BLOKE EDER (meşgul-bekleme degil), mesaj
 * gelince GERI DONER. Her zaman basarili doner (bloke oldugu icin
 * hata durumu yok). */
void msgqueue_receive(msgqueue_t *q, void *out_data, uint32_t *out_size);

/* Bloklamadan kontrol: kuyrukta mesaj var mi? */
int msgqueue_has_data(msgqueue_t *q);

#endif
