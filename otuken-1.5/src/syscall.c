#include "syscall.h"
#include "heap.h"
#include "serial.h"
#include "log.h"
#include "cpu.h"
#include "sched.h"
#include "thread.h"
#include "vfs.h"
#include "pmm.h"
#include "vmm.h"
#include "keyboard.h"
#include "fb.h"
#include "timer.h"
#include "gui.h"
#include "ipc2.h"
#include "sched.h"
#include "ulgen.h"
#include "servis.h"
#include "tore.h"
#include "tema.h"
#include "dede.h"
#include "ozan_ses.h"
#include "ses.h"
#include "fat16.h"

#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

extern void syscall_entry(void);

typedef uint64_t (*syscall_fn_t)(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);

static syscall_fn_t syscall_table[SYS_MAX];

/* =====================================================================
 * GOREV-BASINA SYSCALL TAMPONU (SMP adim 6)
 * syscall_entry kesmeleri yeniden acar (sti): bir syscall ortasinda
 * gorev kesilip AYNI cekirdekte baska gorev calisabilir. Eskiden
 * tamponlar CEKIRDEK-BASINA (cpu_yerel) ya da STATIK idi; kesilen
 * gorevin tamponunu digeri eziyordu -> A sureci B'nin dosya icerigini
 * kendi bellegine kopyalayabilirdi (tek cekirdekte bile). Bir gorev
 * ayni anda tek syscall yurutur: gorev-basina tek tampon yeterli.
 * Ilk kullanimda ayrilir, reap'te serbest birakilir.
 * ===================================================================== */
#define SC_TAMPON_BOYUT 4096
static uint8_t sc_onyukleme_tamponu[SC_TAMPON_BOYUT];   /* zamanlayici oncesi (tek akis) */

static void *gorev_tamponu(void) {
    task_t *t = scheduler_current_task();
    if (!t) return sc_onyukleme_tamponu;
    if (!t->sc_tampon) t->sc_tampon = kmalloc(SC_TAMPON_BOYUT);
    return t->sc_tampon;       /* bellek yoksa 0: cagiran -1 doner */
}

static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* ===================== Kullanici isaretci dogrulamasi =====================
 * DENETIM BULGUSU (CRITICAL): syscall isleyicileri kullanicidan gelen
 * isaretcileri HIC denetlemeden dereference ediyordu. Ring3'teki bir
 * program cekirdek adresi (ornegin higher-half kod ya da kimlik
 * haritasindaki yapilar) gecirerek cekirdegin o adresi OKUMASINI ya da
 * YAZMASINI saglayabilirdi - tam bir ayricalik yukseltme acigi.
 *
 * Artik her isaretci: (a) kullanici adres araliginda mi, (b) sayfalari
 * GERCEKTEN haritali ve KULLANICI erisimine acik mi diye denetlenir. */

#define KULLANICI_MIN 0x0000008000000000ULL   /* PML4[1] */
#define KULLANICI_MAX 0x0000800000000000ULL   /* kanonik alt yari siniri */

static int toplama_tasar_s(uint64_t a, uint64_t b) {
    return (a > 0xFFFFFFFFFFFFFFFFULL - b);
}

/* Tek bir sayfanin kullanici tarafindan erisilebilir oldugunu dogrular. */
/* DENETIM BULGUSU (HIGH): bu denetleyici yalnizca KULLANICI erisimini
 * siniyordu, YAZILABILIRLIGI degil. Cekirdegin kullanici tamponuna
 * YAZDIGI cagrilarda (read, listdir, ipc_al) kullanici kendi SALT-OKUNUR
 * kod sayfasini verebilir ve cekirdek, W^X korumasini delerek oraya
 * yazardi. Artik yazma gerektiginde RW biti de zorunlu. */
static int sayfa_kullanici_erisimli_ex(uint64_t va, int yazma) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & 0x000FFFFFFFFFF000ULL);

    int i4 = (int)((va >> 39) & 0x1FF);
    int i3 = (int)((va >> 30) & 0x1FF);
    int i2 = (int)((va >> 21) & 0x1FF);
    int i1 = (int)((va >> 12) & 0x1FF);

    if (!(pml4[i4] & VMM_PRESENT) || !(pml4[i4] & VMM_USER)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[i3] & VMM_PRESENT) || !(pdpt[i3] & VMM_USER)) return 0;
    if (pdpt[i3] & (1ULL << 7)) {              /* 1GB sayfa, US dogrulandi */
        if (yazma && !(pdpt[i3] & VMM_WRITABLE)) return 0;
        return 1;
    }
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i3] & 0x000FFFFFFFFFF000ULL);
    if (!(pd[i2] & VMM_PRESENT) || !(pd[i2] & VMM_USER)) return 0;
    if (pd[i2] & (1ULL << 7)) {                /* 2MB sayfa, US dogrulandi */
        if (yazma && !(pd[i2] & VMM_WRITABLE)) return 0;
        return 1;
    }
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[i2] & 0x000FFFFFFFFFF000ULL);
    if (!(pt[i1] & VMM_PRESENT) || !(pt[i1] & VMM_USER)) return 0;
    if (yazma && !(pt[i1] & VMM_WRITABLE)) return 0;
    return 1;
}

/* Geriye donuk uyum: yalnizca okuma denetimi */
static int sayfa_kullanici_erisimli(uint64_t va) {
    return sayfa_kullanici_erisimli_ex(va, 0);
}

/* [ptr, ptr+uzunluk) araliginin TAMAMI kullanici erisimine acik mi? */
static int kullanici_tampon_gecerli_ex(uint64_t ptr, uint64_t uzunluk, int yazma) {
    if (uzunluk == 0) return (ptr >= KULLANICI_MIN && ptr < KULLANICI_MAX);
    if (toplama_tasar_s(ptr, uzunluk)) return 0;
    if (ptr < KULLANICI_MIN || ptr + uzunluk > KULLANICI_MAX) return 0;

    uint64_t ilk = ptr & ~0xFFFULL;
    uint64_t son = (ptr + uzunluk - 1) & ~0xFFFULL;
    for (uint64_t p = ilk; p <= son; p += 4096) {
        if (!sayfa_kullanici_erisimli_ex(p, yazma)) return 0;
    }
    return 1;
}

static int kullanici_tampon_gecerli(uint64_t ptr, uint64_t uzunluk) {
    return kullanici_tampon_gecerli_ex(ptr, uzunluk, 0);
}

/* Cekirdegin YAZACAGI tamponlar icin: RW biti de zorunlu. */
/* --- TOCTOU savunmasi ---
 * syscall_entry.asm giriste `sti` yapar, yani syscall SIRASINDA
 * on-alim aciktir. Dolayisiyla "dogrula ... sonra yaz" arasinda baska
 * bir gorev calisabilir. Bugun userland'e acik bir unmap/mprotect
 * cagrisi olmadigi icin bu pencere SOMURULEBILIR degildir, ama
 * gelecekte olursa aciga donusur.
 *
 * Bu yardimci, cekirdek tamponundan kullanici tamponuna yazarken
 * DOGRULAMA ile KOPYALAMA arasini kesmelere kapatarak atomik yapar.
 * Basari: 0, hata: -1 */
static int kullaniciya_atomik_yaz(uint64_t hedef, const void *kaynak, uint64_t uzunluk);

static int kullanici_tampon_yazilabilir(uint64_t ptr, uint64_t uzunluk) {
    return kullanici_tampon_gecerli_ex(ptr, uzunluk, 1);
}

/* Kullanici dizesini GUVENLI kopyalar (azami uzunlukla sinirli).
 * Basari: kopyalanan uzunluk, hata: -1 */
/* --- Ham dize kopyalama: kesme korumasi YOK, yalnizca sarmalayici
 * tarafindan cagrilir. Her sayfa sinirinda yeniden dogrular. --- */
static int dize_kopyala_ham(uint64_t ptr, char *hedef, int azami) {
    if (ptr < KULLANICI_MIN || ptr >= KULLANICI_MAX) return -1;
    uint64_t son_sayfa = (uint64_t)-1;
    for (int i = 0; i < azami - 1; i++) {
        uint64_t va = ptr + (uint64_t)i;
        uint64_t sayfa = va & ~0xFFFULL;
        if (sayfa != son_sayfa) {
            if (!sayfa_kullanici_erisimli(sayfa)) return -1;
            son_sayfa = sayfa;
        }
        char ch = *(volatile char *)(uintptr_t)va;
        hedef[i] = ch;
        if (ch == 0) return i;
    }
    hedef[azami - 1] = 0;
    return azami - 1;
}

/* --- TOCTOU KAPATILDI ---
 * Onceden dogrulama ile kopyalama arasinda on-alim mumkundu
 * (syscall_entry.asm giriste `sti` yapar). Bu pencerede baska bir
 * gorev calisip CR3'u degistirebilir ya da haritalari bozabilirdi;
 * dogrulanan sayfa ile OKUNAN sayfa ayni olmayabilirdi.
 * Artik dogrulama ve kopyalama TEK bir kesmesiz bolgede yapilir.
 * Kritik bolge `azami` ile sinirlidir (en fazla birkac yuz bayt),
 * bu yuzden gecikme etkisi ihmal edilebilir. */
static int kullanici_dize_kopyala(uint64_t ptr, char *hedef, int azami) {
    if (azami <= 0) return -1;
    uint64_t bayrak;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(bayrak) : : "memory");
    int r = dize_kopyala_ham(ptr, hedef, azami);
    __asm__ volatile ("push %0; popfq" : : "r"(bayrak) : "memory", "cc");
    if (r < 0) hedef[0] = 0;
    return r;
}

/* --- Kullanicidan cekirdege ATOMIK tampon okuma ---
 * sys_ipc_gonder gibi cagrilar icin: dogrulama sonrasi kullanici
 * isaretcisini DOGRUDAN okumak yerine, dogrulama+kopyalama tek bir
 * kesmesiz bolgede yapilir ve veri CEKIRDEK tamponuna alinir. */
static int kullanicidan_atomik_oku(void *hedef, uint64_t kaynak, uint64_t uzunluk) {
    if (uzunluk == 0) return 0;
    uint64_t bayrak;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(bayrak) : : "memory");

    int ok = kullanici_tampon_gecerli(kaynak, uzunluk);
    if (ok) {
        uint8_t *h = (uint8_t *)hedef;
        const volatile uint8_t *k = (const volatile uint8_t *)(uintptr_t)kaynak;
        for (uint64_t i = 0; i < uzunluk; i++) h[i] = k[i];
    }

    __asm__ volatile ("push %0; popfq" : : "r"(bayrak) : "memory", "cc");
    return ok ? 0 : -1;
}


/* --- Gercek syscall implementasyonlari --- */

static uint64_t sys_write_impl(uint64_t str_ptr, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char *guvenli = (char *)gorev_tamponu();
    if (!guvenli) return (uint64_t)-1;
    if (kullanici_dize_kopyala(str_ptr, guvenli, 512) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "write: gecersiz kullanici isaretcisi");
        return (uint64_t)-1;
    }
    const char *s = guvenli;
    serial_write(s);
    /* Asama 24: kullanici ciktisi ekranda da gorunsun. TOPLU yazma
     * kullaniyoruz - karakter basina tam ekran yeniden cizim, kabugu
     * kullanilamaz hale getirecek kadar yavasti. */
    gui_write_string(s);
    return 0;
}

static uint64_t sys_getpid_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    task_t *t = scheduler_current_task();
    return t ? t->id : 0xFFFFFFFFULL;
}

static uint64_t sys_yield_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    scheduler_tick();
    return 0;
}

static uint64_t sys_sleep_impl(uint64_t ticks, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    thread_sleep(ticks);
    return 0;
}

/* kernel.c bu bayragi okuyup ring3 kodunun GERCEKTEN calistigini
 * dogrular. */
volatile int g_ring3_proof_received = 0;

static uint64_t sys_ring3_proof_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    g_ring3_proof_received = 1;
    serial_write("syscall: SYS_RING3_PROOF alindi - ring3 kodu GERCEKTEN "
                 "calisiyor ve syscall ile geri geldi!\n");
    return 0;
}

static uint64_t sys_exit_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    serial_write("syscall: SYS_EXIT alindi - gorev sonlandiriliyor\n");
    task_exit(); /* ASLA GERI DONMEZ */
    return 0;    /* buraya asla ulasilmaz, derleyiciyi mutlu eder */
}

/* --- Asama 24: userland runtime destegi --- */

/* Userland heap'i: her surece verilecek basit, artan bir bolge.
 * Su an tek kullanici baglami oldugu icin tek global imlec yeterli;
 * surec basina heap, surec-basina-adres-alani tam devreye girdiginde
 * (Asama 30) ayrilacak. */
/* --- Denetim duzeltmesi (HIGH): surec basina kullanici heap'i ---
 * Global user_heap_end, tum kullanici programlarinin AYNI bolgeyi
 * paylasmasina yol aciyordu (kabuk + UMAY birbirini bozabilirdi).
 * Artik her gorev kendi heap araligina sahiptir; aralik gorev
 * kimligine gore ayrilir. */
#define USER_HEAP_TABAN   0x8001000000ULL
#define USER_HEAP_ADIM    (16ULL * 1024 * 1024)   /* gorev basina pencere */
#define USER_HEAP_AZAMI   (4ULL * 1024 * 1024)    /* gorev basina kullanilabilir */

/* Heap penceresi yuva haritasi: her bit bir pencereyi temsil eder. */
#define HEAP_YUVA_SAYISI 32
static uint32_t heap_yuva_haritasi = 0;

/* Denetim (1.4.6): denetle-sonra-yaz kilitsizdi; iki cekirdekte ayni
 * anda ilk sbrk ayni yuvayi alabiliyordu. Artik atomik. */
static int heap_yuva_al(void) {
    for (int i = 0; i < HEAP_YUVA_SAYISI; i++) {
        uint32_t bit = 1u << i;
        if (__atomic_load_n(&heap_yuva_haritasi, __ATOMIC_ACQUIRE) & bit) continue;
        if (!(__atomic_fetch_or(&heap_yuva_haritasi, bit, __ATOMIC_ACQ_REL) & bit)) return i;
    }
    return -1;
}

void heap_yuva_birak(int yuva) {
    if (yuva >= 0 && yuva < HEAP_YUVA_SAYISI)
        __atomic_fetch_and(&heap_yuva_haritasi, ~(1u << yuva), __ATOMIC_ACQ_REL);
}

static int kullaniciya_atomik_yaz(uint64_t hedef, const void *kaynak, uint64_t uzunluk) {
    if (uzunluk == 0) return 0;
    uint64_t bayrak;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(bayrak) : : "memory");

    int ok = kullanici_tampon_yazilabilir(hedef, uzunluk);
    if (ok) {
        volatile uint8_t *h = (volatile uint8_t *)(uintptr_t)hedef;
        const uint8_t *k = (const uint8_t *)kaynak;
        for (uint64_t i = 0; i < uzunluk; i++) h[i] = k[i];
    }

    __asm__ volatile ("push %0; popfq" : : "r"(bayrak) : "memory", "cc");
    return ok ? 0 : -1;
}

static uint64_t sys_sbrk_impl(uint64_t increment, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;

    task_t *gorev = scheduler_current_task();
    if (!gorev) return (uint64_t)-1;

    /* Ilk cagride bu goreve OZEL bir heap penceresi ayir.
     *
     * DENETIM BULGUSU (HIGH): pencere onceden `id % 32` ile
     * seciliyordu. Gorev kimlikleri tekdüze artar; 32'den fazla gorev
     * olusturuldugunda kimlikler sarmalanir ve IKI CANLI gorev AYNI
     * heap bolgesini paylasir - sessiz bellek bozulmasi. Artik
     * pencereler bir YUVA HARITASINDAN tahsis edilir ve gorev
     * sonlandiginda IADE edilir; cakisma imkansizdir. */
    if (gorev->heap_baslangic == 0) {
        int yuva = heap_yuva_al();
        if (yuva < 0) {
            log_write(LOG_UYARI, "SYSCALL", "sbrk: bos heap penceresi kalmadi");
            return (uint64_t)-1;
        }
        gorev->heap_yuva = yuva;
        gorev->heap_baslangic = USER_HEAP_TABAN +
                                 ((uint64_t)yuva * USER_HEAP_ADIM);
        gorev->heap_son = gorev->heap_baslangic;
        serial_write("sbrk: gorev '");
        serial_write(gorev->name);
        serial_write("' icin heap penceresi=");
        serial_write_hex((uint32_t)(gorev->heap_baslangic >> 32));
        serial_write_hex((uint32_t)gorev->heap_baslangic);
        serial_write("\n");
    }

    uint64_t eski_son = gorev->heap_son;
    if (increment == 0) return eski_son;

    /* Tasma ve sinir denetimi */
    if (increment > USER_HEAP_AZAMI) return (uint64_t)-1;
    uint64_t yeni_son = eski_son + increment;
    if (yeni_son < eski_son) return (uint64_t)-1;                /* tasma */
    if (yeni_son > gorev->heap_baslangic + USER_HEAP_AZAMI) {
        log_write(LOG_UYARI, "SYSCALL", "sbrk: gorev heap siniri asildi");
        return (uint64_t)-1;
    }

    uint64_t ilk = eski_son & ~0xFFFULL;
    uint64_t son = (yeni_son + 0xFFF) & ~0xFFFULL;
    for (uint64_t va = ilk; va < son; va += 4096) {
        if (va < eski_son && eski_son != gorev->heap_baslangic) continue;
        if (vmm_get_physical(va) != 0) {
            /* Denetim (1.4.6): yuva iade edilirken sayfalari haritada
             * kaliyor; yuvanin yeni sahibi eski programin verisini
             * goruyordu. Bu gorevin sinirinin OTESINDEKI haritali sayfa
             * oncekinden kalmadir: sifirlanir. */
            uint8_t *eski = (uint8_t *)(uintptr_t)va;
            for (int z = 0; z < 4096; z++) eski[z] = 0;
            continue;
        }
        uint64_t fiz = pmm_alloc_frame();
        if (fiz == 0) return (uint64_t)-1;
        if (vmm_map_page(va, fiz, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            pmm_free_frame(fiz);
            return (uint64_t)-1;
        }
        uint8_t *pg = (uint8_t *)(uintptr_t)va;
        for (int i = 0; i < 4096; i++) pg[i] = 0;
    }
    gorev->heap_son = yeni_son;
    return eski_son;
}

static uint32_t cagiran_kimlik(void) {
    task_t *t = scheduler_current_task();
    return t ? t->id : 0xFFFFFFFFu;
}

static uint64_t sys_open_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(path_ptr, yol, sizeof(yol)) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "open: gecersiz kullanici isaretcisi");
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)vfs_open(yol);
}

static uint64_t sys_read_impl(uint64_t fd, uint64_t buf_ptr, uint64_t size, uint64_t a4) {
    (void)a4;
    if (size > 4096) size = 4096;                 /* makul ust sinir */
    /* Denetim: yalniz KENDI actigi fd'yi okuyabilir (fd tablosu global) */
    if (!vfs_fd_gorevin_mi((int)fd, cagiran_kimlik())) return (uint64_t)-1;
    if (!kullanici_tampon_yazilabilir(buf_ptr, size)) {
        log_write(LOG_UYARI, "SYSCALL", "read: gecersiz kullanici tamponu");
        return (uint64_t)-1;
    }
    /* TOCTOU: dogrudan kullanici tamponuna okumak yerine once
     * CEKIRDEK ara tamponuna oku, sonra atomik olarak kopyala. */
    uint8_t *ara_tampon = (uint8_t *)gorev_tamponu();   /* gorev-basina */
    if (!ara_tampon) return (uint64_t)-1;
    int n = vfs_read((int)fd, ara_tampon, size);
    if (n <= 0) return (uint64_t)(int64_t)n;
    if (kullaniciya_atomik_yaz(buf_ptr, ara_tampon, (uint64_t)n) != 0) {
        log_write(LOG_UYARI, "SYSCALL", "read: kullaniciya yazma reddedildi");
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)n;
}

static uint64_t sys_close_impl(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    if (!vfs_fd_gorevin_mi((int)fd, cagiran_kimlik())) return (uint64_t)-1;
    return (uint64_t)(int64_t)vfs_close((int)fd);
}

static uint64_t sys_readkey_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return (uint64_t)(uint8_t)keyboard_pop_for(KLAVYE_ODAK_KABUK);
}

static uint64_t sys_listdir_impl(uint64_t path_ptr, uint64_t out_ptr, uint64_t max, uint64_t a4) {
    (void)a4;
    char yol2[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(path_ptr, yol2, sizeof(yol2)) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "listdir: gecersiz yol isaretcisi");
        return (uint64_t)-1;
    }
    if (max > 16) max = 16;
    if (!kullanici_tampon_yazilabilir(out_ptr, max * sizeof(sys_dirent_t))) {
        log_write(LOG_UYARI, "SYSCALL", "listdir: gecersiz cikti tamponu");
        return (uint64_t)-1;
    }
    const char *path = yol2;

    vfs_dirent_t tmp[16];
    int cap = (max < 16) ? (int)max : 16;
    int n = vfs_list_dir(path, tmp, cap);
    if (n < 0) return (uint64_t)-1;

    /* TOCTOU: once CEKIRDEK tamponunda kur, sonra atomik kopyala. */
    sys_dirent_t *ara_dizin = (sys_dirent_t *)gorev_tamponu();
    if (!ara_dizin) return (uint64_t)-1;
    sys_dirent_t *out = ara_dizin;

    for (int i = 0; i < n; i++) {
        int j = 0;
        while (tmp[i].name[j] && j < 31) { out[i].name[j] = tmp[i].name[j]; j++; }
        out[i].name[j] = 0;
        out[i].size = tmp[i].size;
        out[i].is_dir = (uint32_t)tmp[i].is_dir;
        out[i].reserved = 0;
    }

    if (n > 0 && kullaniciya_atomik_yaz(out_ptr, ara_dizin,
                                         (uint64_t)n * sizeof(sys_dirent_t)) != 0) {
        log_write(LOG_UYARI, "SYSCALL", "listdir: kullaniciya yazma reddedildi");
        return (uint64_t)-1;
    }
    return (uint64_t)n;
}

static uint64_t sys_putpixel_impl(uint64_t x, uint64_t y, uint64_t rgb, uint64_t a4) {
    (void)a4;
    /* Ekran disi koordinat framebuffer disina yazmaya yol acabilir */
    if ((int64_t)x < 0 || (int64_t)y < 0 ||
        x >= (uint64_t)fb_width() || y >= (uint64_t)fb_height()) {
        return (uint64_t)-1;   /* ekran disi - sessizce reddet */
    }
    fb_put_pixel((int)x, (int)y, (uint32_t)rgb);
    return 0;
}

static uint64_t sys_ticks_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return timer_get_ticks();
}

/* --- Asama 34: Uygulama API'si - ULGEN pencere sistemi --- */
static uint64_t sys_pencere_ac_impl(uint64_t baslik, uint64_t xy, uint64_t wh, uint64_t a4) {
    (void)a4;
    int x = (int)((xy >> 16) & 0xFFFF), y = (int)(xy & 0xFFFF);
    int w = (int)((wh >> 16) & 0xFFFF), h = (int)(wh & 0xFFFF);
    /* DENETIM BULGUSU (HIGH): baslik, kullanici isaretcisiydi ve
     * DOGRULANMADAN dereference ediliyordu. Ring3 bir cekirdek
     * adresi vererek cekirdek bellegini pencere basligi olarak
     * EKRANA yazdirabilirdi (bilgi sizintisi). */
    char guv_baslik[64];
    if (kullanici_dize_kopyala(baslik, guv_baslik, sizeof(guv_baslik)) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "pencere_ac: gecersiz baslik isaretcisi");
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)ulgen_yuzey_olustur(guv_baslik, x, y, w, h);
}
static uint64_t sys_pen_doldur_impl(uint64_t id, uint64_t renk, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    ulgen_doldur((int)id, (uint32_t)renk);
    return 0;
}
static uint64_t sys_pen_yazi_impl(uint64_t id, uint64_t xy, uint64_t txt, uint64_t renk) {
    int x = (int)((xy >> 16) & 0xFFFF), y = (int)(xy & 0xFFFF);
    /* DENETIM BULGUSU (HIGH): metin de dogrulanmadan dereference
     * ediliyordu - ayni bilgi sizintisi riski. */
    char guv_metin[128];
    if (kullanici_dize_kopyala(txt, guv_metin, sizeof(guv_metin)) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "pen_yazi: gecersiz metin isaretcisi");
        return (uint64_t)-1;
    }
    ulgen_yazi((int)id, x, y, guv_metin, (uint32_t)renk, 0xE8E4DA);
    return 0;
}
static uint64_t sys_pen_dik_impl(uint64_t id, uint64_t xy, uint64_t wh, uint64_t renk) {
    int x = (int)((xy >> 16) & 0xFFFF), y = (int)(xy & 0xFFFF);
    int w = (int)((wh >> 16) & 0xFFFF), h = (int)(wh & 0xFFFF);
    ulgen_dikdortgen((int)id, x, y, w, h, (uint32_t)renk);
    return 0;
}
static uint64_t sys_pen_kapat_impl(uint64_t id, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    return (uint64_t)(int64_t)ulgen_yuzey_yoket((int)id);
}
static uint64_t sys_servis_liste_impl(uint64_t out_ptr, uint64_t max, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    /* Ust sinir: cekirdek ara tamponunun boyutu */
    if (max > 16) max = 16;
    if (!kullanici_tampon_yazilabilir(out_ptr, (uint64_t)max * sizeof(sys_servis_t))) {
        log_write(LOG_UYARI, "SYSCALL", "servis: gecersiz cikti tamponu");
        return (uint64_t)-1;
    }

    /* TOCTOU: kullanici bellegine DOGRUDAN yazmak yerine once
     * CEKIRDEK tamponunda kur, sonra atomik olarak kopyala. */
    sys_servis_t *ara_servis = (sys_servis_t *)gorev_tamponu();
    if (!ara_servis) return (uint64_t)-1;
    sys_servis_t *out = ara_servis;

    int n = servis_sayisi();
    if ((uint64_t)n > max) n = (int)max;
    for (int i = 0; i < n; i++) {
        const servis_t *s = servis_al(i);
        int j = 0;
        while (s->name[j] && j < 19) { out[i].name[j] = s->name[j]; j++; }
        out[i].name[j] = 0;
        out[i].durum = (uint32_t)s->durum;
        out[i].nabiz = s->heartbeat;
    }

    if (n > 0 && kullaniciya_atomik_yaz(out_ptr, ara_servis,
                                         (uint64_t)n * sizeof(sys_servis_t)) != 0) {
        log_write(LOG_UYARI, "SYSCALL", "servis: kullaniciya yazma reddedildi");
        return (uint64_t)-1;
    }
    return (uint64_t)n;
}

/* --- Kullanici alani IPC kopruleri (UMAY ayri program oldugu icin
 * gerekli). Cekirdek ici IPC ile AYNI port kaydini kullanir, boylece
 * userland UMAY ile cekirdek servisleri (hesapd vb.) ayni ad alaninda
 * bulusur. --- */
static uint64_t sys_ipc_acs_impl(uint64_t isim, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char ad[32];
    if (kullanici_dize_kopyala(isim, ad, sizeof(ad)) < 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)ipc_port_create(ad);
}

static uint64_t sys_ipc_bul_impl(uint64_t isim, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char ad2[32];
    if (kullanici_dize_kopyala(isim, ad2, sizeof(ad2)) < 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)ipc_port_find(ad2);
}

static uint64_t sys_ipc_gonder_impl(uint64_t hedef, uint64_t opkod,
                                     uint64_t veri, uint64_t uzunluk) {
    if (uzunluk > IPC_MAX_PAYLOAD) return (uint64_t)-1;
    ipc_message_t m;
    m.opcode = (uint32_t)opkod;
    m.length = (uint32_t)uzunluk;
    /* Gonderenin portunu bul: cagiran gorev bir port acmissa yanit
     * oraya donebilsin. Basit cozum: 'kabuk' portu varsa onu kullan.
     * (Tam cozum surec->port eslesmesi gerektirir; su anki tek
     * istemci kabuk oldugu icin bu yeterli ve dogru davraniyor.) */
    /* UYUM duzeltmesi: gonderen eskiden SABIT 'kabuk' portuydu. Kabuk
     * disinda bir istemci UMAY'a soru sorunca yanit KABUGA gidiyordu.
     * Artik gonderen = cagiran gorevin KENDI portu; portu yoksa
     * IPC_GONDEREN_YOK (0 gecerli bir port kimligidir, 'yok' degil). */
    task_t *cg = scheduler_current_task();
    int gonderen = cg ? ipc_gorevin_portu(cg->id) : -1;
    m.sender_port = (gonderen >= 0) ? (uint32_t)gonderen : IPC_GONDEREN_YOK;
    m.sequence = 0;
    /* TOCTOU: dogrulama sonrasi kullanici isaretcisini DOGRUDAN
     * okumak yerine, dogrulama+kopyalama tek kesmesiz bolgede yapilir
     * ve veri dogrudan CEKIRDEK yapisina (m.payload) alinir. */
    for (uint64_t i = 0; i < IPC_MAX_PAYLOAD; i++) m.payload[i] = 0;
    if (kullanicidan_atomik_oku(m.payload, veri, uzunluk) != 0) {
        log_write(LOG_UYARI, "SYSCALL", "ipc_gonder: gecersiz veri tamponu");
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)ipc_send((int)hedef, &m);
}

static uint64_t sys_ipc_al_impl(uint64_t port, uint64_t cikti,
                                 uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    /* UYUM duzeltmesi: her gorev HERHANGI bir portu bosaltabiliyordu;
     * iki istemci ayni anda UMAY'la konusurken biri otekinin yanitini
     * alabilirdi. Kullanici alani yalniz KENDI portundan okur. */
    {
        uint32_t sahip = 0;
        task_t *cg = scheduler_current_task();
        if (ipc_port_sahibi((int)port, &sahip) != 0) return 0;      /* kapali: ileti yok */
        if (!cg || sahip != cg->id) return (uint64_t)-1;
    }
    if (!kullanici_tampon_yazilabilir(cikti, sizeof(ipc_message_t))) {
        log_write(LOG_UYARI, "SYSCALL", "ipc_al: gecersiz cikti tamponu");
        return (uint64_t)-1;
    }
    /* TOCTOU: mesaji CEKIRDEK tamponuna al, sonra atomik kopyala. */
    ipc_message_t *ara_mesaj_p = (ipc_message_t *)gorev_tamponu();
    if (!ara_mesaj_p) return (uint64_t)-1;
    int r = ipc_try_receive((int)port, ara_mesaj_p);
    if (r <= 0) return (uint64_t)(int64_t)r;
    if (kullaniciya_atomik_yaz(cikti, ara_mesaj_p, sizeof(ipc_message_t)) != 0) {
        log_write(LOG_UYARI, "SYSCALL", "ipc_al: kullaniciya yazma reddedildi");
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)r;
}

static uint64_t sys_sistem_bil_impl(uint64_t alan, uint64_t a2,
                                     uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    switch (alan) {
        case SYSBIL_BOS_CERCEVE:   return pmm_free_frames();
        case SYSBIL_TOPLAM_BELLEK: return pmm_total_bytes();
        case SYSBIL_GOREV_SAYISI:  return (uint64_t)scheduler_task_count();
        case SYSBIL_TIK:           return timer_get_ticks();
        case SYSBIL_TIK_HZ:        return timer_hz();
        default:                   return 0;
    }
}

/* =====================================================================
 * UMAY paketi (Asama 42) cagrilari.
 *
 * GUVENLIK: paketteki surum kullanici isaretcilerini yalniz ADRES
 * ARALIGIYLA denetleyip DOGRUDAN okuyup yaziyordu. Aralik icindeki
 * haritalanmamis bir adres cekirdekte sayfa hatasi -> panik demekti
 * (herhangi bir Ring3 programi sistemi dusurebilirdi); ust sinir da
 * kullanici heap penceresini kesiyordu. Burada HER isaretci mevcut
 * yardimcilarla (sayfa varligi + RW denetimi, kesmesiz tek bolgede)
 * cekirdek tamponuna kopyalanir; cekirdek kullanici bellegine
 * dogrudan dokunmaz.
 * ===================================================================== */
static uint32_t cagiran_pid(void) {
    task_t *t = scheduler_current_task();
    return t ? (uint32_t)t->id : 0;
}

static uint64_t sys_stat_impl(uint64_t yol_p, uint64_t cikti, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    uint64_t boyut = 0;
    int dizin = 0;
    if (vfs_stat(yol, &boyut, &dizin) != 0) return (uint64_t)-1;
    sys_stat_t o;
    o.boyut = boyut;
    o.dizin_mi = (uint32_t)dizin;
    o.ayrilmis = 0;
    if (kullaniciya_atomik_yaz(cikti, &o, sizeof(o)) != 0) return (uint64_t)-1;
    return 0;
}

static uint64_t sys_mkdir_impl(uint64_t yol_p, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    if (!tore_check(cagiran_pid(), KUT_DISK_WRITE, "dizin olustur")) return (uint64_t)-1;
    return (uint64_t)(int64_t)vfs_mkdir(yol);
}

/* Cagiranin yetenegi var mi. TORE denetiminden GECMEZ: yetkinin
 * kendisini sorar, reddedilen erisim kaydi olusturmamalidir. */
static uint64_t sys_yetki_impl(uint64_t yetenek, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    uint32_t sahip = tore_get_capabilities(cagiran_pid());
    return (sahip & (uint32_t)yetenek) ? 1u : 0u;
}

/* Yazim yolu VFS'e gider: FAT16 sistem birimi yalniz KOK dizine ("/AD.UZN",
 * 8.3) yazar, FAT32 birimleri ("/fat32/...") alt dizinlerle yazilabilir. */
#define YAZ_AZAMI 4096
static uint64_t sys_write_file_impl(uint64_t yol_p, uint64_t tampon, uint64_t n, uint64_t a4) {
    (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    if (!tore_check(cagiran_pid(), KUT_DISK_WRITE, "dosya yaz")) return (uint64_t)-1;
    if (yol[0] != '/' || yol[1] == 0) return (uint64_t)-1;
    if (n > YAZ_AZAMI) return (uint64_t)-1;
    uint8_t *veri = (uint8_t *)gorev_tamponu();
    if (!veri) return (uint64_t)-1;
    if (kullanicidan_atomik_oku(veri, tampon, n) != 0) return (uint64_t)-1;
    /* vfs_write_file BASARIDA YAZILAN BAYT SAYISINI dondurur (0 degil).
     * Ilk surumde "!= 0 -> hata" yazmistim: her basarili yazim -1
     * donuyordu. SMP-YARIS sinamasi yakaladi. */
    int yazilan = vfs_write_file(yol, veri, n);
    if (yazilan < 0) {
        log_write(LOG_UYARI, "SYSCALL", "write_file: yazim reddedildi (yol/surucu desteklemiyor)");
        return (uint64_t)-1;
    }
    return (uint64_t)yazilan;
}

static uint64_t sys_proc_liste_impl(uint64_t cikti, uint64_t azami, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (!tore_check(cagiran_pid(), KUT_PROC_CREATE, "surec listele")) return (uint64_t)-1;
    if (azami > 32) azami = 32;
    uint64_t adet = 0;
    for (uint32_t i = 0; adet < azami; i++) {
        sys_surec_t s;
        if (scheduler_gorev_bilgi(i, &s.pid, s.ad, &s.durum, &s.bellek_kb) != 0) break;
        if (kullaniciya_atomik_yaz(cikti + adet * sizeof(sys_surec_t), &s, sizeof(s)) != 0)
            return (uint64_t)-1;
        adet++;
    }
    return adet;
}

static uint64_t sys_pen_sun_impl(uint64_t id, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)id; (void)a2; (void)a3; (void)a4;
    return (uint64_t)(int64_t)ulgen_birlestir();
}

static uint64_t sys_pen_olay_impl(uint64_t id, uint64_t cikti, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    sys_olay_t o = { 0, 0, 0, 0 };
    ulgen_olay_t e;
    uint64_t sonuc;
    if (ulgen_olay_cek((int)id, &e)) {
        o.tur = e.tur; o.tus = e.tus; o.x = e.x; o.y = e.y;
        sonuc = 1;
    } else {
        /* yalniz klavye odagi penceredeyse (Tab) tus al */
        char c = keyboard_pop_for(KLAVYE_ODAK_PENCERE);
        if (c == 0) { sonuc = 0; }
        else { o.tur = 1; o.tus = (uint32_t)(unsigned char)c; sonuc = 1; }
    }
    if (kullaniciya_atomik_yaz(cikti, &o, sizeof(o)) != 0) return (uint64_t)-1;
    return sonuc;
}

static uint64_t sys_ses_nota_impl(uint64_t hz, uint64_t ms, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (ms > 3000) ms = 3000;
    return (uint64_t)(int64_t)ses_ton((uint32_t)hz, (uint32_t)ms);
}

static uint64_t sys_ozan_soyle_impl(uint64_t metin_p, uint64_t hiz, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    char *metin = (char *)gorev_tamponu();
    if (!metin) return (uint64_t)-1;
    if (kullanici_dize_kopyala(metin_p, metin, 512) < 0) return (uint64_t)-1;
    ozan_soyle(metin, (int)hiz);
    return 0;
}

static uint64_t sys_dede_konus_impl(uint64_t metin_p, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char *metin = (char *)gorev_tamponu();
    if (!metin) return (uint64_t)-1;
    if (kullanici_dize_kopyala(metin_p, metin, 512) < 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)dede_konus(metin);
}

static uint64_t sys_tema_impl(uint64_t ad_p, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char ad[32];
    if (kullanici_dize_kopyala(ad_p, ad, sizeof(ad)) < 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)tema_uygula(ad);
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    if (num >= SYS_MAX || syscall_table[num] == 0) {
        log_write_num(LOG_UYARI, "SYSCALL", "gecersiz cagri no", num);
        return (uint64_t)-1;
    }
    return syscall_table[num](a1, a2, a3, a4);
}

/* SMP adim 5: SYSCALL MSR'leri CEKIRDEK-BASINADIR. Eskiden yalniz
 * BSP'de, AP'ler uyandiktan SONRA kuruluyordu; AP'ye dusen bir Ring3
 * gorevinin ilk 'syscall' komutu #UD veriyordu (EFER.SCE=0). Her
 * cekirdek zamanlayiciya katilmadan once bunu cagirir. */
void syscall_msr_kur(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1ULL; /* SCE (System Call Extensions) biti */
    wrmsr(MSR_EFER, efer);

    /* STAR[47:32]=0x08 -> SYSCALL: CS=0x08(kernel kod), SS=0x08+8=0x10(kernel veri)
     * STAR[63:48]=0x18 -> SYSRET:  SS=0x18+8=0x20(kullanici veri),
     *                              CS=0x18+16=0x28(kullanici 64-bit kod)
     * (bkz. boot.asm gdt64 - bu offsetler icin ozel olarak duzenlendi) */
    uint64_t star = (0x18ULL << 48) | (0x08ULL << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    /* Denetim duzeltmesi (HIGH): yalniz IF temizleniyordu. Ring3 kodu
     * DF=1 (string islemleri ters yone gider -> cekirdek bellek bozulmasi),
     * TF=1 (cekirdekte tek-adim #DB), AC=1, NT=1 ya da IOPL bitleriyle
     * SYSCALL yapabilirdi. Hepsi giriste temizlenir; kullanicinin kendi
     * RFLAGS'i R11'de saklanir ve SYSRET ile aynen geri yuklenir. */
    wrmsr(MSR_FMASK, 0x200 | 0x400 | 0x100 | 0x4000 | 0x3000 | 0x40000);
}

void syscall_init(void) {
    for (int i = 0; i < SYS_MAX; i++) syscall_table[i] = 0;
    syscall_table[SYS_WRITE]       = sys_write_impl;
    syscall_table[SYS_GETPID]      = sys_getpid_impl;
    syscall_table[SYS_YIELD]       = sys_yield_impl;
    syscall_table[SYS_SLEEP]       = sys_sleep_impl;
    syscall_table[SYS_RING3_PROOF] = sys_ring3_proof_impl;
    syscall_table[SYS_EXIT]        = sys_exit_impl;
    syscall_table[SYS_OPEN]        = sys_open_impl;
    syscall_table[SYS_READ]        = sys_read_impl;
    syscall_table[SYS_CLOSE]       = sys_close_impl;
    syscall_table[SYS_SBRK]        = sys_sbrk_impl;
    syscall_table[SYS_READKEY]     = sys_readkey_impl;
    syscall_table[SYS_LISTDIR]     = sys_listdir_impl;
    syscall_table[SYS_PUTPIXEL]    = sys_putpixel_impl;
    syscall_table[SYS_TICKS]       = sys_ticks_impl;
    syscall_table[SYS_IPC_ACS]     = sys_ipc_acs_impl;
    syscall_table[SYS_IPC_BUL]     = sys_ipc_bul_impl;
    syscall_table[SYS_IPC_GONDER]  = sys_ipc_gonder_impl;
    syscall_table[SYS_IPC_AL]      = sys_ipc_al_impl;
    syscall_table[SYS_SISTEM_BIL]  = sys_sistem_bil_impl;
    syscall_table[SYS_WRITE_FILE]  = sys_write_file_impl;
    syscall_table[SYS_STAT]        = sys_stat_impl;
    syscall_table[SYS_MKDIR]       = sys_mkdir_impl;
    syscall_table[SYS_YETKI]       = sys_yetki_impl;
    syscall_table[SYS_PROC_LISTE]  = sys_proc_liste_impl;
    syscall_table[SYS_PEN_SUN]     = sys_pen_sun_impl;
    syscall_table[SYS_PEN_OLAY]    = sys_pen_olay_impl;
    syscall_table[SYS_SES_NOTA]    = sys_ses_nota_impl;
    syscall_table[SYS_OZAN_SOYLE]  = sys_ozan_soyle_impl;
    syscall_table[SYS_TEMA]        = sys_tema_impl;
    syscall_table[SYS_DEDE_KONUS]  = sys_dede_konus_impl;
    syscall_table[SYS_PENCERE_AC]  = sys_pencere_ac_impl;
    syscall_table[SYS_PEN_DOLDUR]  = sys_pen_doldur_impl;
    syscall_table[SYS_PEN_YAZI]    = sys_pen_yazi_impl;
    syscall_table[SYS_PEN_DIK]     = sys_pen_dik_impl;
    syscall_table[SYS_PEN_KAPAT]   = sys_pen_kapat_impl;
    syscall_table[SYS_SERVIS_LISTE]= sys_servis_liste_impl;

    syscall_msr_kur();


    serial_write("syscall: SYSCALL/SYSRET mekanizmasi kuruldu "
                 "(STAR/LSTAR/FMASK/EFER.SCE), 19 syscall kayitli\n");
}

_Static_assert(SC_TAMPON_BOYUT >= 4096, "read/write_file tamponu");
_Static_assert(SC_TAMPON_BOYUT >= YAZ_AZAMI, "write_file");
_Static_assert(SC_TAMPON_BOYUT >= 16 * sizeof(sys_dirent_t), "listdir");
_Static_assert(SC_TAMPON_BOYUT >= 16 * sizeof(sys_servis_t), "servis");
_Static_assert(SC_TAMPON_BOYUT >= sizeof(ipc_message_t), "ipc_al");
