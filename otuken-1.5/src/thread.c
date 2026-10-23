#include "thread.h"
#include "serial.h"

static uint32_t thread_registry_count = 0;

void thread_manager_init(void) {
    thread_registry_count = 0;
    serial_write("thread: yonetici baslatildi\n");
}

task_t *thread_create(const char *name, void (*entry)(void)) {
    task_t *t = task_create(name, entry);
    if (t) thread_registry_count++;
    return t;
}

void thread_sleep(uint64_t ticks) {
    task_sleep(ticks);
}

void thread_wake(task_t *thread) {
    task_wake(thread);
}

void thread_exit(void) {
    task_exit(); /* ASLA GERI DONMEZ */
}
