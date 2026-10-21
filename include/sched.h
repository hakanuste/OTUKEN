#ifndef OTUKENOS_SCHED_H
#define OTUKENOS_SCHED_H

#include <stdint.h>

typedef enum { TASK_READY = 0, TASK_TERMINATED = 1, TASK_SLEEPING = 2,
               TASK_BLOCKED = 3 } task_state_t;

typedef struct task {
    uint64_t rsp;
    uint8_t *stack_base;
    uint32_t id;
    char name[16];
    volatile task_state_t state;
    uint64_t wake_at_tick; /* state==TASK_SLEEPING iken anlamli */
    /* Asama 25: her gorevin KENDI cekirdek yigini. Ring3'ten syscall
     * geldiginde CPU bu yigina gecer - kullanici yiginini cekirdek
     * kodu icin KULLANMAK guvensizdir ve tasmaya yol acar. */
    uint8_t *kernel_stack;
    uint64_t kernel_stack_top;
    /* --- Denetim duzeltmesi (CRITICAL): surec adres alani ---
     * Onceden TUM gorevler tek adres alanini paylasiyordu;
     * vmm_create_address_space() yazilmis ama HIC KULLANILMIYORDU.
     * Artik her surec kendi PML4'une sahip olabilir. 0 = cekirdek
     * adres alanini kullan (cekirdek is parcaciklari icin). */
    uint64_t cr3;
    /* --- Denetim duzeltmesi (HIGH): surec basina kullanici heap'i ---
     * Onceden sbrk TEK bir global imlec kullaniyordu; kabuk ve UMAY
     * gibi IKI ayri kullanici programi AYNI heap bolgesini paylasip
     * birbirinin bellegini bozabiliyordu. Artik her gorevin kendi
     * heap sinirlari var. */
    uint64_t heap_baslangic;
    uint64_t heap_son;
    int      heap_yuva;   /* tahsis edilen heap penceresi (-1 = yok) */
} task_t;

/* Zamanlayiciyi baslatir. Bu fonksiyonu CAGIRAN mevcut yurutme baglami
 * (yani kernel_main'in kendisi) otomatik olarak "gorev 0" olarak
 * kaydedilir. Ayrica hicbir gorev kalmadiginda dusulecek, ASLA
 * sonlanmayan bir "idle" gorevi de burada olusturulur. */
void scheduler_init(const char *main_task_name);

/* Yeni bir cekirdek gorevi olusturur ve hazir kuyruguna ekler.
 * entry fonksiyonu ya SONSUZ DONGU olmali, ya da bitirmek istedigi
 * yerde task_exit() cagirmalidir (asla normal 'return' ile donmemeli). */
task_t *task_create(const char *name, void (*entry)(void));

/* Su an CALISAN gorevi sonlandirir - SADECE bir gorevin KENDI kendine
 * cagirmasi icindir. Asla geri donmez. */
void task_exit(void);

/* Zamanlayici tick'i - timer.c'nin IRQ0 handler'indan cagirilir. */
void scheduler_tick(void);

task_t *scheduler_current_task(void);
uint32_t scheduler_task_count(void);

/* TASK_TERMINATED durumundaki (ama henuz bellegi geri alinmamis)
 * gorevleri listeden cikarip yigin/struct bellegini kfree eder.
 * SU AN CALISAN gorevi ASLA reap etmez (kendi kendini yemez). */
void scheduler_reap_terminated(void);

/* BASKA (su an calisan olmayan) bir gorevi zorla sonlandirir ve hemen
 * reap eder. task, scheduler_current_task() ile ayniysa basarisiz olur
 * (-1) - kendini boyle sonlandiramaz, task_exit() kullanmali. */
int scheduler_terminate_task(task_t *task);

/* Su an CALISAN gorevi 'ticks' zamanlayici tik'i kadar uyutur (CPU'yu
 * baska gorevlere birakir - meşgul-bekleme (busy-wait) DEGILDIR).
 * Sure dolunca normal sekilde GERI DONER (task_exit'in aksine). */
void task_sleep(uint64_t ticks);

/* Uyuyan bir gorevi erken uyandirir (TASK_READY yapar). Gorev zaten
 * uyumuyorsa (READY/TERMINATED) hicbir sey yapmaz. */
void task_wake(task_t *task);

/* Su an CALISAN gorevi TASK_BLOCKED yapip baska bir goreve gecer -
 * mutex gibi "birisi beni acikca uyandirana kadar bekle" durumlari
 * icindir (task_sleep'ten farki: zaman asimi yok). GERI DONER (task_unblock
 * ile uyandirilinca). SADECE mutex.c gibi senkronizasyon ilkelleri
 * tarafindan kullanilmalidir. */
void task_block_and_switch(void);

/* TASK_BLOCKED durumundaki bir gorevi TASK_READY yapar. */
void task_unblock(task_t *task);

/* syscall_entry.asm bu degiskeni okur: su an calisan gorevin cekirdek
 * yigininin tepesi. Zamanlayici her gecişte gunceller. */
extern uint64_t current_kernel_rsp;

/* Gorevi belirtilen adres alanina baglar (0 = cekirdek alani). */
void task_adres_alani_ata(task_t *t, uint64_t pml4_fiz);

#endif
