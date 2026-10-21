#include "msgqueue.h"

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

void msgqueue_init(msgqueue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->waiting_receiver_count = 0;
}

int msgqueue_send(msgqueue_t *q, const void *data, uint32_t size) {
    if (size > MSG_MAX_SIZE) return -1;

    cli();
    if (q->count >= MSG_QUEUE_CAPACITY) {
        sti();
        return -1; /* kuyruk dolu */
    }

    message_t *m = &q->messages[q->tail];
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < size; i++) m->data[i] = src[i];
    m->size = size;
    q->tail = (q->tail + 1) % MSG_QUEUE_CAPACITY;
    q->count++;

    task_t *to_wake = 0;
    if (q->waiting_receiver_count > 0) {
        to_wake = q->waiting_receivers[0];
        for (int i = 1; i < q->waiting_receiver_count; i++)
            q->waiting_receivers[i - 1] = q->waiting_receivers[i];
        q->waiting_receiver_count--;
    }
    sti();

    if (to_wake) task_unblock(to_wake); /* kendi cli/sti ciftini kullanir */
    return 0;
}

void msgqueue_receive(msgqueue_t *q, void *out_data, uint32_t *out_size) {
    for (;;) {
        cli();
        if (q->count > 0) {
            message_t *m = &q->messages[q->head];
            uint8_t *dst = (uint8_t *)out_data;
            for (uint32_t i = 0; i < m->size; i++) dst[i] = m->data[i];
            *out_size = m->size;
            q->head = (q->head + 1) % MSG_QUEUE_CAPACITY;
            q->count--;
            sti();
            return;
        }

        /* bos: kendimizi bekleyenler listesine ekleyip blokla */
        task_t *self = scheduler_current_task();
        if (q->waiting_receiver_count < MSG_MAX_WAITERS) {
            q->waiting_receivers[q->waiting_receiver_count++] = self;
        }
        sti();

        task_block_and_switch();
        /* Uyandirildik - dogrudan "mesaj var" varsaymadan basa donup
         * TEKRAR kontrol ediyoruz (saglam/dogru yaklasim). */
    }
}

int msgqueue_has_data(msgqueue_t *q) {
    return (q->count > 0) ? 1 : 0;
}
