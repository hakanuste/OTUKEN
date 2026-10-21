#include "pipe.h"

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

void pipe_init(pipe_t *p) {
    p->read_pos = 0;
    p->write_pos = 0;
    p->count = 0;
    p->waiting_reader_count = 0;
    p->waiting_writer_count = 0;
}

uint32_t pipe_write(pipe_t *p, const void *data, uint32_t size) {
    const uint8_t *src = (const uint8_t *)data;
    uint32_t written = 0;

    while (written < size) {
        cli();
        while (written < size && p->count < PIPE_BUFFER_SIZE) {
            p->buffer[p->write_pos] = src[written];
            p->write_pos = (p->write_pos + 1) % PIPE_BUFFER_SIZE;
            p->count++;
            written++;
        }

        task_t *reader_to_wake = 0;
        if (p->waiting_reader_count > 0) {
            reader_to_wake = p->waiting_readers[0];
            for (int i = 1; i < p->waiting_reader_count; i++)
                p->waiting_readers[i - 1] = p->waiting_readers[i];
            p->waiting_reader_count--;
        }

        int need_block = (written < size); /* tampon doldu, yazilacak hala var */
        if (need_block) {
            task_t *self = scheduler_current_task();
            if (p->waiting_writer_count < PIPE_MAX_WAITERS)
                p->waiting_writers[p->waiting_writer_count++] = self;
        }
        sti();

        if (reader_to_wake) task_unblock(reader_to_wake);
        if (need_block) task_block_and_switch(); /* yer acilinca devam eder */
    }
    return written;
}

uint32_t pipe_read(pipe_t *p, void *out, uint32_t max_size) {
    uint8_t *dst = (uint8_t *)out;

    for (;;) {
        cli();
        if (p->count > 0) {
            uint32_t to_read = (p->count < max_size) ? p->count : max_size;
            for (uint32_t i = 0; i < to_read; i++) {
                dst[i] = p->buffer[p->read_pos];
                p->read_pos = (p->read_pos + 1) % PIPE_BUFFER_SIZE;
                p->count--;
            }

            task_t *writer_to_wake = 0;
            if (p->waiting_writer_count > 0) {
                writer_to_wake = p->waiting_writers[0];
                for (int i = 1; i < p->waiting_writer_count; i++)
                    p->waiting_writers[i - 1] = p->waiting_writers[i];
                p->waiting_writer_count--;
            }
            sti();

            if (writer_to_wake) task_unblock(writer_to_wake);
            return to_read;
        }

        /* bos: bekleyenler listesine ekleyip blokla */
        task_t *self = scheduler_current_task();
        if (p->waiting_reader_count < PIPE_MAX_WAITERS)
            p->waiting_readers[p->waiting_reader_count++] = self;
        sti();

        task_block_and_switch();
        /* Uyandirildik - basa donup TEKRAR kontrol ediyoruz. */
    }
}
