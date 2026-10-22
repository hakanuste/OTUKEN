#include "sched.h"
#include "ipc2.h"
#include "cpu.h"
#include "heap.h"
#include "serial.h"
#include "timer.h"
#include "tss.h"
#include "spinlock.h"
#include "smp.h"

#include "syscall.h"
#include "ag.h"   /* heap_yuva_birak */
#include "vmm.h"
#include "process.h"
#include "vfs.h"
#include "tore.h"
#include "ulgen.h"

#define MAX_TASKS       96   /* SMP: her cekirdegin kendi bos gorevi de burada */
#define TASK_STACK_SIZE (16 * 1024)

extern void context_switch(uint64_t *old_rsp_ptr, uint64_t new_rsp);
extern void task_trampoline(void);

static task_t *tasks[MAX_TASKS];
static uint32_t task_count = 0;

/* --- SMP adim 5: IS DAGITIMI ---
 * Gorev tablosu TEK ve global; "hangi gorev bu cekirdekte" ise
 * cekirdek-basina (cpu_bu()->simdiki_gorev). Iki kural dogrulugu saglar:
 *
 * 1) calisiyor bayragi: bir cekirdekte calisan gorev baska cekirdek
 *    tarafindan secilemez.
 * 2) Kilit GECIS BOYUNCA tutulur: context_switch eski gorevin RSP'sini
 *    kaydetmeden once o gorev secilebilir hale gelirse, iki cekirdek ayni
 *    yigini kullanir. Bu yuzden kilidi YENI gorev, kendi baglaminda
 *    (sched_gecis_sonrasi) birakir ve eski gorevin calisiyor bayragini
 *    ancak o zaman temizler (Linux: finish_task_switch).
 *    Bu, task_exit'teki eski "kilit anahtarlamadan once birakiliyor"
 *    yarisini da kapatir.
 *
 * Kilit YALNIZ kesmeler kapaliyken alinir (aksi halde ayni cekirdekteki
 * tick kesmesi kilitlenir). Beklerken bekleyen IPI'ler islenir. */
static volatile int rq_kilit = 0;

static inline void rq_al(void) {
    while (__atomic_exchange_n(&rq_kilit, 1, __ATOMIC_ACQUIRE)) {
        smp_bekleyen_isle();
        __asm__ volatile ("pause");
    }
}
static inline void rq_birak(void) {
    __atomic_store_n(&rq_kilit, 0, __ATOMIC_RELEASE);
}
/* Geriye uyum: tablo islemleri (gecis yapmayanlar) icin. */
static inline uint64_t sched_kilitle(void) {
    uint64_t f = irq_kaydet_kapat();
    rq_al();
    return f;
}
static inline void sched_kilit_ac(uint64_t f) {
    rq_birak();
    irq_geri_yukle(f);
}
/* Denetim (1.4.6): eskiden iki adimdi (gs:0 -> cpu isaretcisi, sonra
 * ->simdiki_gorev). Kesmeler acikken arada gecis olup gorev baska cekirdege
 * tasinirsa ESKI cekirdegin simdiki gorevi (baska bir gorev) okunuyordu:
 * sc_tampon, sbrk, TORE kimligi ve ozkilit sahibi yanlis goreve gidebilirdi.
 * Artik TEK komut: gs-goreli okuma o an hangi cekirdekteysek onundur ve o
 * cekirdekte calisan gorev biziz. */
static inline task_t *simdiki(void) {
    task_t *t;
    __asm__ volatile ("mov %%gs:%c1, %0" : "=r"(t)
                      : "i"(__builtin_offsetof(cpu_yerel_t, simdiki_gorev)));
    return t;
}

static uint32_t next_task_id = 0;
static int scheduler_started = 0;

/* syscall_entry.asm tarafindan okunur (bkz. sched.h) */
/* SMP adim 4: gecerli gorevin cekirdek yigini artik cekirdek-basina
 * (cpu_bu()->cekirdek_rsp); syscall_entry onu gs:8 adresinden okur. */

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

static task_t *make_task_as(const char *name, void (*entry)(void),
                            uint64_t cr3, int adres_sahibi) {
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
    /* --- 1.4.7: adres alani gorev HAZIR KUYRUGUNA GIRMEDEN ONCE atanir ---
     * Eskiden cr3 hep 0 (cekirdek) ile baslar, task_adres_alani_ata SONRA
     * cagrilirdi. Gorev tabloya READY olarak eklendikten sonra o cagriya
     * kadar gecen pencerede baska cekirdek gorevi secip YANLIS (cekirdek)
     * adres alaninda calistirabiliyordu (latent yaris). Artik cr3 gorev
     * gorunur olmadan once burada set edilir. */
    t->cr3 = cr3;
    t->heap_yuva = -1;
    t->adres_sahibi = adres_sahibi;
    t->tutulan_kilit = 0;
    t->kullanici = 0;
    t->heap_baslangic = 0;   /* ilk sbrk cagrisinda atanir */
    t->heap_son = 0;
    t->id = next_task_id++;
    t->state = TASK_READY;
    t->calisiyor = 0;
    t->cpu_bagli = -1;
    t->bos = 0;
    t->uyandir_bekliyor = 0;
    t->sc_tampon = 0;

    int i = 0;
    while (name[i] && i < 15) { t->name[i] = name[i]; i++; }
    t->name[i] = 0;
    return t;
}

/* Cekirdek adres alaninda (cr3=0) calisan gorev - cekirdek is parcaciklari. */
static task_t *make_task(const char *name, void (*entry)(void)) {
    return make_task_as(name, entry, 0, 0);
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
    t->adres_sahibi = 0;
    t->tutulan_kilit = 0;
    t->kullanici = 0;
    t->heap_baslangic = 0;
    t->heap_son = 0;
    {
        uint8_t *ks = (uint8_t *)kmalloc(SYSCALL_KSTACK_SIZE);
        t->kernel_stack = ks;
        t->kernel_stack_top = ks ? (uint64_t)(uintptr_t)(ks + SYSCALL_KSTACK_SIZE) : 0;
        cpu_bu()->cekirdek_rsp = t->kernel_stack_top;
        tss_set_rsp0(t->kernel_stack_top);
    }
    t->id = next_task_id++;
    t->state = TASK_READY;
    t->calisiyor = 1;          /* su an BSP'de calisiyor */
    t->cpu_bagli = 0;          /* ana gorev BSP'ye bagli (PIC, surucu donguleri) */
    t->bos = 0;
    t->uyandir_bekliyor = 0;
    t->sc_tampon = 0;
    int i = 0;
    while (main_task_name[i] && i < 15) { t->name[i] = main_task_name[i]; i++; }
    t->name[i] = 0;
    tasks[task_count++] = t;
    cpu_bu()->simdiki_gorev = t;

    task_t *idle = make_task("idle", idle_entry);
    if (!idle) {
        serial_write("sched: KRITIK - idle gorevi olusturulamadi\n");
        return;
    }
    idle->cpu_bagli = 0;       /* BSP'nin bos gorevi */
    idle->bos = 1;
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

/* task_create'in adres-alani surumu: gorev, kendi adres alanina (cr3)
 * BAGLI olarak ve HAZIR KUYRUGUNA girmeden ONCE baglanmis halde olusur.
 * Boylece gorev hicbir an cekirdek adres alaninda gorunmez (yalitim
 * sizintisi penceresi yok). cr3=0 verilirse cekirdek alaninda calisir
 * (task_create ile ayni). adres_sahibi!=0 ise reap'te cr3 yok edilir. */
task_t *task_create_adresli(const char *name, void (*entry)(void),
                            uint64_t cr3, int adres_sahibi) {
    uint64_t ekle_bayrak = sched_kilitle();
    if (task_count >= MAX_TASKS) {
        sched_kilit_ac(ekle_bayrak);
        serial_write("sched: HATA - MAX_TASKS asildi\n");
        return 0;
    }
    task_t *t = make_task_as(name, entry, cr3, adres_sahibi);
    if (!t) {
        sched_kilit_ac(ekle_bayrak);
        serial_write("sched: task_create_adresli - bellek yetersiz\n");
        return 0;
    }
    tasks[task_count++] = t;
    sched_kilit_ac(ekle_bayrak);
    serial_write("sched: adresli gorev olusturuldu: ");
    serial_write(t->name);
    if (cr3 != 0) {
        serial_write("' adres alanina baglandi PML4=");
        serial_write_hex((uint32_t)cr3);
    }
    serial_write("\n");
    return t;
}

/* (eski not) Round-robin: bir sonraki calistirilabilir
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

/* Siradaki gorevi secer (rq kilidi TUTULU, IF=0). Bos gorevler yalniz
 * baska aday yoksa secilir; baska cekirdekte calisan ve baska cekirdege
 * bagli gorevler atlanir. Cekirdegin kendi bos gorevi hic bloke olmadigi
 * icin her zaman bir sonuc vardir. */
static task_t *siradaki_sec(task_t *self) {
    cpu_yerel_t *c = cpu_bu();
    uint64_t now = timer_get_ticks();
    uint32_t n = task_count;
    task_t *bos = 0;
    for (uint32_t k = 1; k <= n; k++) {
        uint32_t idx = (c->rr_indeks + k) % n;
        task_t *t = tasks[idx];
        if (t->state == TASK_SLEEPING && now >= t->wake_at_tick)
            t->state = TASK_READY;                 /* uyanma zamani geldi */
        if (t->state != TASK_READY) continue;
        if (t->cpu_bagli >= 0 && (uint32_t)t->cpu_bagli != c->indeks) continue;
        if (t->calisiyor && t != self) continue;
        if (t->bos) { if (!bos) bos = t; continue; }
        c->rr_indeks = idx;
        return t;
    }
    return bos ? bos : self;
}

/* Gecisin ikinci yarisi: YENI gorevin baglaminda calisir (yeni gorev
 * ilk kez calisiyorsa task_trampoline'dan). Eski gorevi serbest birakir
 * ve kilidi acar. IF'ye dokunmaz: devam eden gorev kendi bayragini
 * geri yukler, trampoline ise ardindan sti yapar. */
void sched_gecis_sonrasi(void) {
    cpu_yerel_t *c = cpu_bu();
    task_t *onceki = (task_t *)c->onceki_gorev;
    if (onceki) {
        __atomic_store_n(&onceki->calisiyor, 0, __ATOMIC_RELEASE);
        c->onceki_gorev = 0;
    }
    rq_birak();
}

/* rq kilidi TUTULU ve IF=0 iken cagrilir; kilidi (dolayli olarak) BIRAKIR. */
static void gecis_yap(task_t *self, task_t *yeni) {
    cpu_yerel_t *c = cpu_bu();
    if (yeni == self) { rq_birak(); return; }
    yeni->calisiyor = 1;
    c->onceki_gorev = self;
    c->simdiki_gorev = yeni;
    c->gecis_sayisi++;
    if (!yeni->bos) c->is_gecis++;
    c->cekirdek_rsp = yeni->kernel_stack_top;
    tss_set_rsp0(yeni->kernel_stack_top);
    adres_alanina_gec(yeni);
    context_switch(&self->rsp, yeni->rsp);
    /* Buradayiz: bu gorev (self) yeniden secildi - belki BASKA cekirdekte. */
    sched_gecis_sonrasi();
}

void scheduler_tick(void) {
    if (!scheduler_started) return;
    /* Kesme baglaminda IF zaten 0; ama sys_yield bunu IF=1 iken cagirir.
     * Kilit yalniz IF=0 ile alinabilir (ayni cekirdekteki tick kesmesi
     * aksi halde kilidi sonsuza dek bekler). */
    uint64_t bayrak = irq_kaydet_kapat();
    task_t *self = simdiki();
    if (!self) { irq_geri_yukle(bayrak); return; }
    rq_al();
    gecis_yap(self, siradaki_sec(self));
    irq_geri_yukle(bayrak);
}

task_t *scheduler_current_task(void) {
    if (!scheduler_started) return 0;
    return simdiki();
}

uint32_t scheduler_task_count(void) {
    return task_count;
}

void task_exit(void) {
    /* GERI DONMEZ. Kilit gecis boyunca tutulur ve yeni gorev tarafindan
     * birakilir; bu gorevin calisiyor bayragi ancak yigini artik
     * kullanilmiyorken temizlenir, reap de ancak o zaman serbest birakir.
     * (Eski surumde kilit gecisten ONCE birakiliyordu: SMP'de baska
     * cekirdek ayni gorevi secebiliyordu.) */
    serial_write("sched: gorev sonlandi: ");
    serial_write(simdiki()->name);
    serial_write("\n");
    irq_kaydet_kapat();
    rq_al();
    task_t *self = simdiki();
    self->state = TASK_TERMINATED;
    gecis_yap(self, siradaki_sec(self));   /* self READY degil, secilmez */
    /* BURAYA ASLA ULASILMAZ */
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void scheduler_reap_terminated(void) {
    /* Tablodan cikarma kilit altinda; SERBEST BIRAKMA kilit DISINDA:
     * kfree heap kilidini ve (sayfa iadesinde) TLB vurusu IPI'sini
     * gerektirebilir; rq kilidini tutarken bunu yapmak diger
     * cekirdekleri gereksiz bekletir. Calisan (calisiyor=1) gorev -
     * yigini hala kullanimda - reap EDILMEZ. */
    task_t *kurbanlar[MAX_TASKS];
    uint32_t kn = 0;
    uint64_t s_bayrak = sched_kilitle();
    task_t *self = simdiki();
    uint32_t i = 0;
    while (i < task_count) {
        task_t *t = tasks[i];
        if (t->state == TASK_TERMINATED && t != self &&
            !__atomic_load_n(&t->calisiyor, __ATOMIC_ACQUIRE)) {
            kurbanlar[kn++] = t;
            for (uint32_t j = i; j < task_count - 1; j++) tasks[j] = tasks[j + 1];
            task_count--;
        } else {
            i++;
        }
    }
    sched_kilit_ac(s_bayrak);

    for (uint32_t k = 0; k < kn; k++) {
        task_t *t = kurbanlar[k];
        serial_write("sched: reap ediliyor: ");
        serial_write(t->name);
        serial_write("\n");
        ipc_gorev_portlarini_kapat(t->id);
        /* 1.4.7: olen gorevin acik biraktigi dosyalar ve TORE yetki kaydi.
         * Ikisi de yalniz DONEN kilit alir (uyumaz). */
        vfs_gorev_bitti(t->id);
        ag_gorev_bitti(t->id);      /* 1.4.9: acik ag baglantilari */
        tore_gorev_bitti(t->id);
        /* olen programin pencereleri (tamponlari heap'e doner). gorsel_kilit
         * UYUYAN kilittir: reap gorev baglaminda cagrilir (ana dongu,
         * process_destroy kilidi biraktiktan sonra, sinama gorevleri). */
        ulgen_gorev_bitti(t->id);
        if (t->heap_yuva >= 0) { heap_yuva_birak(t->heap_yuva); t->heap_yuva = -1; }
        /* Denetim (1.4.6): surecin adres alani artik BURADA yok edilir -
         * gorev hicbir cekirdekte calismiyor (calisiyor=0) ve ondan
         * gecilirken CR3 baskasina gecti. Eskiden process_destroy onu
         * gorev baska cekirdekte calisirken yok edebiliyordu; kendi kendine
         * sonlanan surecinki ise hic geri alinmiyordu. */
        if (t->adres_sahibi && t->cr3) { vmm_destroy_address_space(t->cr3); t->cr3 = 0; }
        process_gorev_bitti(t);     /* surec tablosunda sarkan isaretci kalmasin */
        if (t->stack_base) kfree(t->stack_base);
        if (t->kernel_stack) kfree(t->kernel_stack);
        if (t->sc_tampon) kfree(t->sc_tampon);
        kfree(t);
    }
}

int scheduler_terminate_task(task_t *task) {
    uint64_t s_bayrak = sched_kilitle();
    if (!scheduler_started || !task || task == simdiki()) {
        sched_kilit_ac(s_bayrak);
        return -1; /* kendini boyle sonlandiramaz */
    }
    /* Denetim (1.4.6): eskiden hedef baska cekirdekte CALISIRKEN de,
     * bir bekleyen listesinde BLOKEYKEN de, mutex TUTARKEN de
     * sonlandiriliyordu: adres alani kullanimdayken yok ediliyor, bekleyen
     * listesinde sarkan isaretci kaliyor ya da kilit sonsuza dek kilitli
     * kaliyordu. Artik bunlar reddedilir; cagiran yeniden dener. */
    if (__atomic_load_n(&task->calisiyor, __ATOMIC_ACQUIRE) ||
        task->state == TASK_BLOCKED || task->state == TASK_TERMINATED ||
        __atomic_load_n(&task->tutulan_kilit, __ATOMIC_ACQUIRE) > 0) {
        sched_kilit_ac(s_bayrak);
        return -1;
    }
    task->state = TASK_TERMINATED;
    sched_kilit_ac(s_bayrak);

    serial_write("sched: DISARIDAN sonlandirildi: ");
    serial_write(task->name);
    serial_write("\n");

    /* --- 1.4.7 YASAM DONGUSU: sonlandirma ISTEGI != kaynak GERI ALIMI ---
     * Eskiden burada scheduler_reap_terminated() SATIR ICI cagriliyordu. Tek
     * cagiran process_destroy, surec_kilit'i (kesmeleri KAPATAN donen kilit)
     * tutarken buraya giriyordu: reap bu kilit altinda adres alanini yok
     * ediyor (TLB vurusu IPI'si, kfree, ...) ve eklenecek her temizlik
     * (FD, TORE) KESMELER KAPALIYKEN, baska bir kilidin icinde kosuyordu.
     * Artik bu islev YALNIZ istegi kaydeder (TERMINATED: bir daha secilmez);
     * geri alim, cagiran kilitlerini BIRAKTIKTAN sonra reap ile yapilir
     * (bkz. process_destroy; ya da ana dongunun periyodik reap'i). */
    return 0;
}

void task_sleep(uint64_t ticks) {
    /* Bayrak KAYDEDILIR ve donuste geri yuklenir (bkz. ders 6). */
    uint64_t bayrak = irq_kaydet_kapat();
    rq_al();
    task_t *self = simdiki();
    self->wake_at_tick = timer_get_ticks() + ticks;
    self->state = TASK_SLEEPING;
    gecis_yap(self, siradaki_sec(self));
    irq_geri_yukle(bayrak);
}

void task_wake(task_t *task) {
    uint64_t bayrak = sched_kilitle();
    if (task->state == TASK_SLEEPING) task->state = TASK_READY;
    sched_kilit_ac(bayrak);
}

/* --- KAYIP UYANDIRMA (SMP) ---
 * mutex/msgqueue/pipe: nesne kilidini BIRAKIR, sonra bloke olur. Arada
 * baska cekirdek task_unblock yaparsa gorev henuz BLOCKED degildir;
 * eskiden uyandirma yok sayiliyor ve gorev sonsuza dek uyuyordu. Artik
 * task_unblock bir JETON birakir; task_block_and_switch jeton varsa
 * bloke olmadan doner. Cagiranlar zaten kosulu yeniden denetler
 * (mutex: kilit dogrudan devredilmistir). */
void task_block_and_switch(void) {
    uint64_t bayrak = irq_kaydet_kapat();
    rq_al();
    task_t *self = simdiki();
    if (self->uyandir_bekliyor) {
        self->uyandir_bekliyor = 0;
        rq_birak();
        irq_geri_yukle(bayrak);
        return;
    }
    self->state = TASK_BLOCKED;
    gecis_yap(self, siradaki_sec(self));
    irq_geri_yukle(bayrak);
}

void task_unblock(task_t *task) {
    uint64_t bayrak = sched_kilitle();
    if (task->state == TASK_BLOCKED) task->state = TASK_READY;
    else if (task->state == TASK_READY) task->uyandir_bekliyor = 1;
    sched_kilit_ac(bayrak);
}

/* --- AP'nin zamanlayiciya katilmasi ---
 * AP'nin baslangic baglami, o cekirdege bagli BOS gorev olur. */
int sched_ap_katil(uint32_t indeks) {
    if (!scheduler_started) return -1;
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    uint8_t *ks = (uint8_t *)kmalloc(SYSCALL_KSTACK_SIZE);
    if (!t || !ks) { if (t) kfree(t); if (ks) kfree(ks); return -1; }
    t->rsp = 0;
    t->stack_base = 0;
    t->kernel_stack = ks;
    t->kernel_stack_top = (uint64_t)(uintptr_t)(ks + SYSCALL_KSTACK_SIZE);
    t->cr3 = 0;
    t->heap_yuva = -1;
    t->adres_sahibi = 0;
    t->tutulan_kilit = 0;
    t->kullanici = 0;
    t->heap_baslangic = 0;
    t->heap_son = 0;
    t->state = TASK_READY;
    t->calisiyor = 1;
    t->cpu_bagli = (int)indeks;
    t->bos = 1;
    t->uyandir_bekliyor = 0;
    t->sc_tampon = 0;
    const char *on = "bos-";
    int i = 0;
    while (on[i]) { t->name[i] = on[i]; i++; }
    if (indeks >= 10) t->name[i++] = (char)('0' + (indeks / 10) % 10);
    t->name[i++] = (char)('0' + indeks % 10);
    t->name[i] = 0;

    uint64_t f = sched_kilitle();
    if (task_count >= MAX_TASKS) { sched_kilit_ac(f); kfree(t); kfree(ks); return -1; }
    t->id = next_task_id++;
    tasks[task_count++] = t;
    cpu_yerel_t *c = cpu_bu();
    c->simdiki_gorev = t;
    c->cekirdek_rsp = t->kernel_stack_top;
    tss_set_rsp0(t->kernel_stack_top);
    sched_kilit_ac(f);
    return 0;
}

int scheduler_gorev_bilgi(uint32_t i, uint32_t *id, char ad[16],
                          uint32_t *durum, uint32_t *bellek_kb) {
    uint64_t f = sched_kilitle();
    if (i >= task_count) { sched_kilit_ac(f); return -1; }
    task_t *t = tasks[i];
    *id = t->id;
    for (int k = 0; k < 16; k++) ad[k] = t->name[k];
    ad[15] = 0;
    *durum = (uint32_t)t->state;
    uint64_t bayt = SYSCALL_KSTACK_SIZE + (t->stack_base ? TASK_STACK_SIZE : 0);
    if (t->heap_son > t->heap_baslangic) bayt += t->heap_son - t->heap_baslangic;
    *bellek_kb = (uint32_t)(bayt / 1024);
    sched_kilit_ac(f);
    return 0;
}

/* 1.4.9: i. gorev Ring3'e gecti mi (TORE goruntuleyici); yoksa -1 */
int scheduler_gorev_kullanici(uint32_t i) {
    uint64_t f = sched_kilitle();
    int r = (i < task_count) ? tasks[i]->kullanici : -1;
    sched_kilit_ac(f);
    return r;
}

void task_adres_alani_ata(task_t *t, uint64_t pml4_fiz) {
    if (!t) return;
    t->cr3 = pml4_fiz;
    t->adres_sahibi = 1;        /* reap'te yok edilir (bkz. scheduler_reap_terminated) */
    serial_write("sched: gorev '");
    serial_write(t->name);
    serial_write("' adres alanina baglandi PML4=");
    serial_write_hex((uint32_t)pml4_fiz);
    serial_write("\n");
}
