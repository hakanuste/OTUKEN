#include "sched.h"
#include "heap.h"
#include "serial.h"
#include "timer.h"
#include "tss.h"
#include "spinlock.h"

/* syscall.c: heap penceresi yuva yoneticisi */
extern void heap_yuva_birak(int yuva);
#include "vmm.h"

#define MAX_TASKS       24
#define TASK_STACK_SIZE (16 * 1024)

extern void context_switch(uint64_t *old_rsp_ptr, uint64_t new_rsp);
extern void task_trampoline(void);

static task_t *tasks[MAX_TASKS];
static uint32_t task_count = 0;
static uint32_t current_index = 0;

/* --- SMP: gorev tablosu paylasilan veridir ---
 * Iki cekirdek ayni anda gorev olusturursa AYNI yuvayi kapabilir;
 * ayni anda reap ederse ayni yapiyi iki kez serbest birakabilir.
 * Tablo uzerindeki tum degisiklikler kilit altinda.
 *
 * NOT: context_switch KILIT TUTULURKEN CAGRILMAZ - gorev degistikten
 * sonra kilidi acacak kod bir daha calismaz ve kilit sonsuza dek
 * tutulu kalirdi. Bu yuzden kilit yalnizca TABLO islemlerini sarar. */
static spinlock_t sched_kilit;
static int sched_kilit_hazir = 0;

static inline uint64_t sched_kilitle(void) {
    if (!sched_kilit_hazir) { spinlock_init(&sched_kilit); sched_kilit_hazir = 1; }
    return spin_lock_irqsave(&sched_kilit);
}
static inline void sched_kilit_ac(uint64_t f) {
    spin_unlock_irqrestore(&sched_kilit, f);
}
static uint32_t next_task_id = 0;
static int scheduler_started = 0;

/* syscall_entry.asm tarafindan okunur (bkz. sched.h) */
uint64_t current_kernel_rsp = 0;

/* DENETIM BULGUSU (CRITICAL): CR3 gecisi ASIMETRIKTI. Yalnizca
 * hedef gorevin KENDI adres alani varsa (cr3 != 0) gecis yapiliyor,
 * cekirdek gorevine (cr3 == 0) donulurken ONCEKI SURECIN adres
 * alaninda kalinıyordu. Cekirdek bolgeleri paylasildigi icin cokme
 * olmuyordu ama cekirdek gorevi, alakasiz bir surecin kullanici
 * haritalarini goruyordu - gercek bir YALITIM SIZINTISI.
 * Cozum: cekirdek adres alanini kaydedip cr3==0 oldugunda ONA don. */
static uint64_t cekirdek_cr3 = 0;

#define SYSCALL_KSTACK_SIZE (16 * 1024)

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

static void idle_entry(void) {
    for (;;) { __asm__ volatile ("hlt"); }
}

static task_t *make_task(const char *name, void (*entry)(void)) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    /* Stage 19 duzeltmesi: biri basarili digeri basarisiz olursa,
     * BASARILI olani serbest birakmadan donmek sizinti olurdu. */
    if (!t || !stack) {
        if (t) kfree(t);
        if (stack) kfree(stack);
        return 0;
    }

    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE);
    *(--sp) = (uint64_t)task_trampoline;
    *(--sp) = (uint64_t)entry; /* rbx -> trampoline call eder */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    /* Ring3'ten syscall geldiginde kullanilacak AYRI cekirdek yigini */
    uint8_t *kstack = (uint8_t *)kmalloc(SYSCALL_KSTACK_SIZE);
    if (!kstack) { kfree(t); kfree(stack); return 0; }
    t->kernel_stack = kstack;
    t->kernel_stack_top = (uint64_t)(uintptr_t)(kstack + SYSCALL_KSTACK_SIZE);

    t->rsp = (uint64_t)(uintptr_t)sp;
    t->stack_base = stack;
    t->cr3 = 0;   /* varsayilan: cekirdek adres alani */
    t->heap_yuva = -1;
    t->heap_baslangic = 0;   /* ilk sbrk cagrisinda atanir */
    t->heap_son = 0;
    t->id = next_task_id++;
    t->state = TASK_READY;

    int i = 0;
    while (name[i] && i < 15) { t->name[i] = name[i]; i++; }
    t->name[i] = 0;
    return t;
}

void scheduler_init(const char *main_task_name) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    /* Stage 19 duzeltmesi: kmalloc basarisiz olursa (bellek yetersiz)
     * asagidaki t->rsp yazimi NULL ISARETCI BASVURUSU olur ve cekirdek
     * coker. Kod taramasinda bulunan gercek bir hataydi. */
    if (!t) {
        serial_write("sched: KRITIK - ana gorev yapisi ayrilamadi, "
                     "zamanlayici baslatilamiyor\n");
        return;
    }
    t->rsp = 0;
    t->stack_base = 0;
    t->cr3 = 0;
    t->heap_yuva = -1;
    t->heap_baslangic = 0;
    t->heap_son = 0;
    {
        uint8_t *ks = (uint8_t *)kmalloc(SYSCALL_KSTACK_SIZE);
        t->kernel_stack = ks;
        t->kernel_stack_top = ks ? (uint64_t)(uintptr_t)(ks + SYSCALL_KSTACK_SIZE) : 0;
        current_kernel_rsp = t->kernel_stack_top;
        tss_set_rsp0(t->kernel_stack_top);
    }
    t->id = next_task_id++;
    t->state = TASK_READY;
    int i = 0;
    while (main_task_name[i] && i < 15) { t->name[i] = main_task_name[i]; i++; }
    t->name[i] = 0;
    tasks[task_count++] = t;

    task_t *idle = make_task("idle", idle_entry);
    if (!idle) {
        serial_write("sched: KRITIK - idle gorevi olusturulamadi\n");
        return;
    }
    tasks[task_count++] = idle;

    cekirdek_cr3 = vmm_current_address_space();
    scheduler_started = 1;

    serial_write("sched: baslatildi, ana gorev='");
    serial_write(t->name);
    serial_write("', idle gorevi eklendi\n");
}

task_t *task_create(const char *name, void (*entry)(void)) {
    /* Yuva tahsisi ATOMIK olmali: iki cekirdek ayni anda bakarsa
     * ikisi de "yer var" gorup AYNI yuvayi kapabilir. */
    uint64_t ekle_bayrak = sched_kilitle();
    if (task_count >= MAX_TASKS) {
        sched_kilit_ac(ekle_bayrak);
        serial_write("sched: HATA - MAX_TASKS asildi\n");
        return 0;
    }
    task_t *t = make_task(name, entry);
    if (!t) {
        sched_kilit_ac(ekle_bayrak);
        serial_write("sched: task_create - bellek yetersiz\n");
        return 0;
    }
    tasks[task_count++] = t;
    sched_kilit_ac(ekle_bayrak);

    serial_write("sched: gorev olusturuldu: ");
    serial_write(t->name);
    serial_write("\n");
    return t;
}

/* Round-robin: current_index'ten baslayarak bir sonraki calistirilabilir
 * (TASK_READY, ya da uyku suresi dolmus TASK_SLEEPING) gorevi bulur.
 * idle gorevi asla TERMINATED/SLEEPING olmadigi icin bu dongu her zaman
 * en az idle'da durur (sonsuz donguye girmez). */
/* Hedef gorevin adres alanina gecer. cr3 == 0 ise CEKIRDEK adres
 * alanina DONER - bu, yalitim sizintisini kapatan asil duzeltmedir. */
static void adres_alanina_gec(task_t *t) {
    uint64_t hedef = (t->cr3 != 0) ? t->cr3 : cekirdek_cr3;
    if (hedef != 0 && hedef != vmm_current_address_space())
        vmm_switch_address_space(hedef);
}

static uint32_t find_next_ready(uint32_t from_index) {
    uint64_t now = timer_get_ticks();
    uint32_t idx = from_index;
    for (uint32_t tries = 0; tries < task_count; tries++) {
        idx = (idx + 1) % task_count;
        if (tasks[idx]->state == TASK_SLEEPING && now >= tasks[idx]->wake_at_tick) {
            tasks[idx]->state = TASK_READY; /* uyanma zamani geldi */
        }
        if (tasks[idx]->state == TASK_READY) return idx;
    }
    return from_index; /* buraya dusmemeli */
}

void scheduler_tick(void) {
    if (!scheduler_started || task_count < 2) return;

    uint32_t old_index = current_index;
    uint32_t new_index = find_next_ready(current_index);

    if (old_index != new_index) {
        task_t *old_task = tasks[old_index];
        task_t *new_task = tasks[new_index];
        current_index = new_index;
        current_kernel_rsp = new_task->kernel_stack_top;
        tss_set_rsp0(new_task->kernel_stack_top);
        adres_alanina_gec(new_task);
        context_switch(&old_task->rsp, new_task->rsp);
    }
}

task_t *scheduler_current_task(void) {
    if (!scheduler_started) return 0;
    return tasks[current_index];
}

uint32_t scheduler_task_count(void) {
    return task_count;
}

void task_exit(void) {
    cli();
    task_t *self = tasks[current_index];
    self->state = TASK_TERMINATED;

    serial_write("sched: gorev sonlandi: ");
    serial_write(self->name);
    serial_write("\n");

    uint32_t new_index = find_next_ready(current_index);
    task_t *new_task = tasks[new_index];
    current_index = new_index;
    current_kernel_rsp = new_task->kernel_stack_top;
        tss_set_rsp0(new_task->kernel_stack_top);
        adres_alanina_gec(new_task);
    /* NOT: kendi rsp'mizi kaydetmenin bir anlami yok (bir daha asla
     * geri donmeyecegiz) ama context_switch imzasi yine de bir hedef
     * ister - self->rsp'ye yazmasi zararsiz (o hafiza zaten
     * scheduler_reap_terminated ile serbest birakilana kadar durur). */
    context_switch(&self->rsp, new_task->rsp);
    /* BURAYA ASLA ULASILMAZ */
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void scheduler_reap_terminated(void) {
    /* Tablo degisikligi: SMP'de iki cekirdek ayni yapiyi iki kez
     * serbest birakabilirdi. Kilit hem bunu hem de kesme yarisini kapatir. */
    uint64_t s_bayrak = sched_kilitle();
    task_t *self = tasks[current_index];
    uint32_t i = 0;
    while (i < task_count) {
        task_t *t = tasks[i];
        if (t->state == TASK_TERMINATED && t != self) {
            serial_write("sched: reap ediliyor: ");
            serial_write(t->name);
            serial_write("\n");
            if (t->heap_yuva >= 0) { heap_yuva_birak(t->heap_yuva); t->heap_yuva = -1; }
            if (t->stack_base) kfree(t->stack_base);
            if (t->kernel_stack) kfree(t->kernel_stack);
            kfree(t);
            /* diziyi sikistir */
            for (uint32_t j = i; j < task_count - 1; j++) tasks[j] = tasks[j + 1];
            task_count--;
            if (current_index > i) current_index--;
            /* bu index'i TEKRAR kontrol et (yerine baskasi kaydi) */
        } else {
            i++;
        }
    }
    sched_kilit_ac(s_bayrak);
}

int scheduler_terminate_task(task_t *task) {
    uint64_t s_bayrak = sched_kilitle();
    if (!scheduler_started || task == tasks[current_index]) {
        sched_kilit_ac(s_bayrak);
        return -1; /* kendini boyle sonlandiramaz */
    }
    task->state = TASK_TERMINATED;
    sched_kilit_ac(s_bayrak);

    serial_write("sched: DISARIDAN sonlandirildi: ");
    serial_write(task->name);
    serial_write("\n");

    scheduler_reap_terminated();
    return 0;
}

void task_sleep(uint64_t ticks) {
    /* ONEMLI: bayragi KAYDET. Eskiden yalnizca cli() yapiliyor ve
     * uyanildiktan sonra sti() CAGRILMIYORDU; gorev kesmeler KAPALI
     * devam ediyordu. Bu, baska yerlerdeki kosulsuz sti() cagrilariyla
     * kazara telafi ediliyordu. Kilitler bayragi dogru geri yukler
     * hale gelince hata ortaya cikti: zamanlayici duruyor ve hicbir
     * gorev calismiyordu. */
    uint64_t bayrak = irq_kaydet_kapat();
    task_t *self = tasks[current_index];
    self->wake_at_tick = timer_get_ticks() + ticks;
    self->state = TASK_SLEEPING;

    uint32_t new_index = find_next_ready(current_index);
    task_t *new_task = tasks[new_index];
    current_index = new_index;
    current_kernel_rsp = new_task->kernel_stack_top;
        tss_set_rsp0(new_task->kernel_stack_top);
        adres_alanina_gec(new_task);
    context_switch(&self->rsp, new_task->rsp);
    /* Uyandirildiginda BURADAN devam eder - bayragi GERI YUKLE. */
    irq_geri_yukle(bayrak);
}

void task_wake(task_t *task) {
    uint64_t bayrak = irq_kaydet_kapat();
    if (task->state == TASK_SLEEPING) {
        task->state = TASK_READY;
    }
    irq_geri_yukle(bayrak);
}

void task_block_and_switch(void) {
    /* task_sleep ile AYNI hata: geri dondugunde bayrak yuklenmiyordu. */
    uint64_t bayrak = irq_kaydet_kapat();
    task_t *self = tasks[current_index];
    self->state = TASK_BLOCKED;

    uint32_t new_index = find_next_ready(current_index);
    task_t *new_task = tasks[new_index];
    current_index = new_index;
    current_kernel_rsp = new_task->kernel_stack_top;
        tss_set_rsp0(new_task->kernel_stack_top);
        adres_alanina_gec(new_task);
    context_switch(&self->rsp, new_task->rsp);
    irq_geri_yukle(bayrak);
    /* task_unblock ile TASK_READY yapilip zamanlayici tekrar bize
     * geldiginde BURADAN normal sekilde devam eder. */
}

void task_unblock(task_t *task) {
    uint64_t bayrak = irq_kaydet_kapat();
    if (task->state == TASK_BLOCKED) {
        task->state = TASK_READY;
    }
    irq_geri_yukle(bayrak);
}

void task_adres_alani_ata(task_t *t, uint64_t pml4_fiz) {
    if (!t) return;
    t->cr3 = pml4_fiz;
    serial_write("sched: gorev '");
    serial_write(t->name);
    serial_write("' adres alanina baglandi PML4=");
    serial_write_hex((uint32_t)pml4_fiz);
    serial_write("\n");
}
