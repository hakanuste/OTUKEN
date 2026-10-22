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
    /* SMP adim 5 */
    volatile int calisiyor;        /* bir cekirdekte su an calisiyor */
    int      cpu_bagli;            /* -1: her cekirdek; >=0: yalniz o cekirdek */
    int      bos;                  /* cekirdegin bos (idle) gorevi */
    volatile int uyandir_bekliyor; /* bloke olmadan once gelen uyandirma */
    void    *sc_tampon;            /* SMP adim 6: gorev-basina syscall tamponu (SC_TAMPON_BOYUT) */
    /* Denetim (1.4.6) */
    int      adres_sahibi;         /* cr3 bu gorevin: reap'te (artik hicbir
                                    * cekirdekte calismiyorken) yok edilir */
    volatile int tutulan_kilit;    /* tuttugu mutex sayisi: >0 iken disaridan
                                    * sonlandirilamaz (kilit sonsuza dek kalirdi) */
    /* 1.4.7: gorev Ring3'e GECTI mi. Bir kez 1 olur, hic sifirlanmaz.
     * syscall politika kapisi bunu okur: Ring3 cagirani TORE yetkisiyle
     * denetlenir; hic Ring3'e gecmemis cekirdek gorevi (kendi syscall
     * katmanini dogrudan cagiran, orn. iskence sinamalari) zaten Ring0'dadir. */
    int      kullanici;
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

/* task_create gibi, ancak gorev HAZIR KUYRUGUNA GIRMEDEN ONCE verilen
 * adres alanina (cr3) baglanir; boylece gorev hicbir an cekirdek adres
 * alaninda gorunmez. cr3=0: cekirdek alani. adres_sahibi!=0 ise reap'te
 * bu adres alani yok edilir. process_create bunu kullanir. */
task_t *task_create_adresli(const char *name, void (*entry)(void),
                            uint64_t cr3, int adres_sahibi);

/* Disaridan sonlandirma. Guvenli olmayan durumda REDDEDER (-1):
 *   - hedef su an bir cekirdekte calisiyor (yigini ve CR3'u kullanimda)
 *   - hedef BLOKE (bir mutex/boru/kuyruk bekleyen listesinde; serbest
 *     birakilsa oradaki isaretci sarkardi)
 *   - hedef bir mutex tutuyor (kilit sonsuza dek kilitli kalirdi)
 * Cagiran daha sonra yeniden dener. 0: sonlandirma ISTEGI kaydedildi.
 *
 * 1.4.7: Bu islev YALNIZ istegi kaydeder (gorev TERMINATED olur ve bir daha
 * secilmez); kaynaklari (adres alani, yigin, FD, TORE, IPC) GERI ALMAZ.
 * Geri alim, cagiran tuttugu kilitleri BIRAKTIKTAN sonra
 * scheduler_reap_terminated() ile yapilir: reap bir donen kilidin icinde,
 * kesmeler kapaliyken kosmamalidir (TLB vurusu IPI'si, kfree...). */
int scheduler_terminate_task(task_t *task);

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
/* current_kernel_rsp kaldirildi: bkz. cpu_yerel_t.cekirdek_rsp */

/* Gorevi belirtilen adres alanina baglar (0 = cekirdek alani). */
void task_adres_alani_ata(task_t *t, uint64_t pml4_fiz);
/* SMP adim 5 */
void sched_gecis_sonrasi(void);      /* task_trampoline cagirir */
int  sched_ap_katil(uint32_t indeks);
/* SYS_PROC_LISTE: i. gorevin anlik kopyasi (kilit altinda). 0: tamam, -1: yok. */
int  scheduler_gorev_bilgi(uint32_t i, uint32_t *id, char ad[16],
                           uint32_t *durum, uint32_t *bellek_kb);
int  scheduler_gorev_kullanici(uint32_t i);   /* 1.4.9: Ring3 mu (-1 yok) */

#endif
