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
#include "ulgen_theme.h"
#include "gorsel_kilit.h"
#include "ekran.h"
#include "servis.h"
#include "tore.h"
#include "tema.h"
#include "ag.h"
#include "io.h"
#include "rtc.h"
#include "kervan2.h"
#include "tcp.h"
#include "kut.h"
#include "timur.h"
#include "ses.h"
#include "dede.h"
#include "asena.h"
#include "smap.h"
#include "fat16_ic.h"
#include "ergenekon.h"
#include "bearssl.h"
#include "otukenconf.h"

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
        kullanici_ac();
        char ch = *(volatile char *)(uintptr_t)va;
        kullanici_kapat();
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
        kullanici_ac();
        for (uint64_t i = 0; i < uzunluk; i++) h[i] = k[i];
        kullanici_kapat();
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
/* 1.5: pencere 16 MB -> 256 MB. 32 pencere x 256 MB = 8 GB sanal alan
 * (1.5: pencere 512 MB -> 0x8001000000 .. 0x8401000000; UMAY'in ikilisi
 * bunun OTESINDE, 0x9000000000'da); fiziksel sayfa yalniz sbrk'te alinir.
 * Varsayilan sinir 4 MB'ta kaldi; yalniz cekirdegin acikca buyuttugu
 * gorev (UMAY: ses modeli + calisma alani) penceresinin tamamina yakin
 * buyuyebilir. */
#define USER_HEAP_ADIM    (512ULL * 1024 * 1024)  /* gorev basina pencere (1.5: 512 MB, 32 x 512 MB = 16 GB sanal) */
#define USER_HEAP_AZAMI   (4ULL * 1024 * 1024)    /* varsayilan gorev siniri */

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
        kullanici_ac();
        for (uint64_t i = 0; i < uzunluk; i++) h[i] = k[i];
        kullanici_kapat();
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
    uint64_t sinir = gorev->heap_azami ? gorev->heap_azami : USER_HEAP_AZAMI;
    if (sinir > USER_HEAP_ADIM) sinir = USER_HEAP_ADIM;          /* pencereyi asamaz */
    if (increment > sinir) return (uint64_t)-1;
    uint64_t yeni_son = eski_son + increment;
    if (yeni_son < eski_son) return (uint64_t)-1;                /* tasma */
    if (yeni_son > gorev->heap_baslangic + sinir) {
        log_write(LOG_UYARI, "SYSCALL", "sbrk: gorev heap siniri asildi");
        return (uint64_t)-1;
    }

    uint64_t ilk = eski_son & ~0xFFFULL;
    uint64_t son = (yeni_son + 0xFFF) & ~0xFFFULL;
    for (uint64_t va = ilk; va < son; va += 4096) {
        if (va < eski_son && eski_son != gorev->heap_baslangic) continue;
        uint64_t eski_fiz = vmm_get_physical(va);
        if (eski_fiz != 0) {
            /* Denetim (1.4.6): yuva iade edilirken sayfalari haritada
             * kaliyor; yuvanin yeni sahibi eski programin verisini
             * goruyordu. Bu gorevin sinirinin OTESINDEKI haritali sayfa
             * oncekinden kalmadir: sifirlanir. */
            /* 1.4.9 SMAP: kullanici adresinden degil, KIMLIK haritasindan */
            uint8_t *eski = (uint8_t *)(uintptr_t)(eski_fiz & ~0xFFFULL);
            for (int z = 0; z < 4096; z++) eski[z] = 0;
            continue;
        }
        uint64_t fiz = pmm_alloc_frame();
        if (fiz == 0) return (uint64_t)-1;
        uint8_t *pg = (uint8_t *)(uintptr_t)fiz;          /* 1.4.9: haritalamadan ONCE, kimlikten */
        for (int i = 0; i < 4096; i++) pg[i] = 0;
        if (vmm_map_page(va, fiz, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            pmm_free_frame(fiz);
            return (uint64_t)-1;
        }
    }
    gorev->heap_son = yeni_son;
    return eski_son;
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
    /* 1.4.8 pencere yoneticisi kurali: uygulama penceresinin basligi ust
     * cubugun ALTINDA baslar (ust cubuk her zaman ustte; baslik oraya
     * denk gelirse pencere tasinamaz/kapatilamaz olurdu). */
    if (y < ULGEN_UST_BAR + ULGEN_BASLIK_YUK) y = ULGEN_UST_BAR + ULGEN_BASLIK_YUK;
    int yid = ulgen_yuzey_olustur(guv_baslik, x, y, w, h);
    /* uygulama yuzeyi: yalniz SYS_PEN_SUN ile sunulan kare ekrana cikar */
    if (yid >= 0 && ulgen_elle_sun(yid) != 0) { ulgen_yuzey_yoket(yid); yid = -1; }
    return (uint64_t)(int64_t)yid;
}
/* 1.4.7 — PENCERE NESNE SAHIPLIGI: Ring3 cagirani yalniz KENDI actigi
 * yuzeye cizebilir/kapatabilir/olayini okuyabilir. Eskiden herhangi bir
 * program baska programin (ya da cekirdegin) penceresine yaziyor,
 * olaylarini (tuslarini) okuyor ya da onu kapatiyordu. Ring0 cekirdek
 * gorevi hepsine erisir. */
static int yuzey_benim(uint64_t id) {
    task_t *t = scheduler_current_task();
    if (!t || !t->kullanici) return 1;
    uint32_t sahip = 0;
    if (id > 0x7FFFFFFFULL || ulgen_yuzey_sahibi((int)id, &sahip) != 0) return 0;
    return sahip == t->id;
}

static uint64_t sys_pen_doldur_impl(uint64_t id, uint64_t renk, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (!yuzey_benim(id)) return (uint64_t)-1;
    ulgen_doldur((int)id, (uint32_t)renk);
    return 0;
}
static uint64_t sys_pen_yazi_impl(uint64_t id, uint64_t xy, uint64_t txt, uint64_t renk) {
    int x = (int)((xy >> 16) & 0xFFFF), y = (int)(xy & 0xFFFF);
    /* DENETIM BULGUSU (HIGH): metin de dogrulanmadan dereference
     * ediliyordu - ayni bilgi sizintisi riski. */
    if (!yuzey_benim(id)) return (uint64_t)-1;
    char guv_metin[128];
    if (kullanici_dize_kopyala(txt, guv_metin, sizeof(guv_metin)) < 0) {
        log_write(LOG_UYARI, "SYSCALL", "pen_yazi: gecersiz metin isaretcisi");
        return (uint64_t)-1;
    }
    ulgen_yazi((int)id, x, y, guv_metin, (uint32_t)renk & 0xFFFFFF, ULGEN_SAYDAM);
    return 0;
}
static uint64_t sys_pen_dik_impl(uint64_t id, uint64_t xy, uint64_t wh, uint64_t renk) {
    int x = (int)((xy >> 16) & 0xFFFF), y = (int)(xy & 0xFFFF);
    int w = (int)((wh >> 16) & 0xFFFF), h = (int)(wh & 0xFFFF);
    if (!yuzey_benim(id)) return (uint64_t)-1;
    ulgen_dikdortgen((int)id, x, y, w, h, (uint32_t)renk);
    return 0;
}
static uint64_t sys_pen_kapat_impl(uint64_t id, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    if (!yuzey_benim(id)) return (uint64_t)-1;
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
        case SYSBIL_SES_VAR:       { const ses_denetleyici_t *d = ses_durum(); return d && d->var; }
        case SYSBIL_DEDE_MESGUL:   return (uint64_t)dede_mesgul();
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
    if (!tore_yol_yazilabilir(cagiran_pid(), yol, 1)) return (uint64_t)-1;   /* 1.4.9 yazma koku */
    /* 1.4.7: KUT_DISK_WRITE denetimi politika kapisinda (syscall_dispatch). */
    return (uint64_t)(int64_t)vfs_mkdir(yol);
}

/* Cagiranin yetenegi var mi. TORE denetiminden GECMEZ: yetkinin
 * kendisini sorar, reddedilen erisim kaydi olusturmamalidir. */
static uint64_t sys_yetki_impl(uint64_t yetenek, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    uint32_t sahip = tore_get_capabilities(cagiran_pid());
    return (sahip & (uint32_t)yetenek) ? 1u : 0u;
}

/* Yalniz KOK dizine yazar ("/AD.UZN", 8.3): FAT16 surucusunde alt
 * dizine dosya yazimi YOK. Alt dizin yolu acikca reddedilir. */
#define YAZ_AZAMI 4096
static uint64_t sys_write_file_impl(uint64_t yol_p, uint64_t tampon, uint64_t n, uint64_t a4) {
    (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    /* 1.4.7: KUT_DISK_WRITE denetimi politika kapisinda (syscall_dispatch). */
    if (yol[0] != '/' || yol[1] == 0) return (uint64_t)-1;
    if (!tore_yol_yazilabilir(cagiran_pid(), yol, 0)) return (uint64_t)-1;   /* 1.4.9 yazma koku */
    int alt = 0;
    for (int i = 1; yol[i]; i++) if (yol[i] == '/') alt = 1;
    if (n > YAZ_AZAMI) return (uint64_t)-1;
    uint8_t *veri = (uint8_t *)gorev_tamponu();
    if (!veri) return (uint64_t)-1;
    if (kullanicidan_atomik_oku(veri, tampon, n) != 0) return (uint64_t)-1;
    /* fat16_write_file BASARIDA YAZILAN BAYT SAYISINI dondurur (0 degil).
     * Ilk surumde "!= 0 -> hata" yazmistim: her basarili yazim -1
     * donuyordu. SMP-YARIS sinamasi yakaladi.
     * 1.4.9: alt dizin yollari fat16_yol_yaz ile (eskiden reddediliyordu). */
    int yazilan = alt ? fat16_yol_yaz(yol, veri, (uint32_t)n) : fat16_write_file(yol + 1, veri, (uint32_t)n);
    if (yazilan < 0) return (uint64_t)-1;
    return (uint64_t)yazilan;
}

/* 1.4.9: agdan indirilen paket (Ring3 bellegi) cekirdege kopyalanir ve
 * dosyadaki ile AYNI dogrulamayla kurulur. Kopya 4 KB'lik parcalarla
 * (her parca kesmesiz, dogrulamali). */
static uint64_t sys_kervan_yukle_impl(uint64_t veri, uint64_t boy, uint64_t neden_p, uint64_t a4) {
    (void)a4;
    char neden[64];
    neden[0] = 0;
    if (boy == 0 || boy > KERVAN2_AZAMI_BOY) return (uint64_t)-1;
    uint8_t *k = (uint8_t *)kmalloc(boy);
    if (!k) return (uint64_t)-1;
    for (uint64_t o = 0; o < boy; o += 4096) {
        uint64_t n = boy - o < 4096 ? boy - o : 4096;
        if (kullanicidan_atomik_oku(k + o, veri + o, n) != 0) { kfree(k); return (uint64_t)-1; }
    }
    int r = kervan2_kur_bellek(k, (uint32_t)boy, neden);
    kfree(k);
    if (neden_p) kullaniciya_atomik_yaz(neden_p, neden, sizeof neden);
    return (uint64_t)(int64_t)r;
}

/* 1.4.9: dosyanin SONUNA ekle (gunluk dosyalari: yalniz yeni bayt yazilir) */
static uint64_t sys_dosya_ekle_impl(uint64_t yol_p, uint64_t tampon, uint64_t n, uint64_t a4) {
    (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    if (yol[0] != '/' || yol[1] == 0 || n > YAZ_AZAMI) return (uint64_t)-1;
    if (!tore_yol_yazilabilir(cagiran_pid(), yol, 0)) return (uint64_t)-1;
    uint8_t *veri = (uint8_t *)gorev_tamponu();
    if (!veri) return (uint64_t)-1;
    if (kullanicidan_atomik_oku(veri, tampon, n) != 0) return (uint64_t)-1;
    int r = fat16_yol_ekle(yol, veri, (uint32_t)n);
    return r < 0 ? (uint64_t)-1 : (uint64_t)r;
}

/* 1.4.9: dosya ya da BOS dizin sil */
static uint64_t sys_dosya_sil_impl(uint64_t yol_p, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char yol[VFS_MAX_PATH];
    if (kullanici_dize_kopyala(yol_p, yol, sizeof(yol)) < 0) return (uint64_t)-1;
    if (yol[0] != '/' || yol[1] == 0) return (uint64_t)-1;
    if (!tore_yol_yazilabilir(cagiran_pid(), yol, 0)) return (uint64_t)-1;
    return fat16_yol_sil(yol) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t sys_proc_liste_impl(uint64_t cikti, uint64_t azami, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    /* 1.4.7: KUT_PROC_CREATE denetimi politika kapisinda (syscall_dispatch). */
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
    (void)a2; (void)a3; (void)a4;
    /* 1.4.8: kimlik artik kullaniliyor: YALNIZ kendi yuzeyini sunabilir */
    if (!yuzey_benim(id) || ulgen_sun((int)id) != 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)ulgen_birlestir();
}

static uint64_t sys_pen_olay_impl(uint64_t id, uint64_t cikti, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    sys_olay_t o = { 0, 0, 0, 0 };
    ulgen_olay_t e;
    uint64_t sonuc;
    if (!yuzey_benim(id)) return (uint64_t)-1;     /* 1.4.7: baskasinin olaylari okunamaz */
    if (ulgen_olay_cek((int)id, &e)) {
        o.tur = e.tur; o.tus = e.tus; o.x = e.x; o.y = e.y;
        sonuc = 1;
    } else {
        /* yalniz klavye odagi penceredeyse (Tab) VE bu yuzey en ustteyse
         * (1.4.9) tus al; digerleri kuyruga dokunmaz */
        char c = (ulgen_odak_yuzey() == (int)id) ? keyboard_pop_for(KLAVYE_ODAK_PENCERE) : 0;
        if (c == 0) { sonuc = 0; }
        else { o.tur = 1; o.tus = klavye_kod_noktasi((unsigned char)c); sonuc = 1; }   /* 1.4.8: Unicode */
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
    /* 1.4.9: sıraya koyar, konuşmayı BEKLEMEZ (bkz. dede_konus_sirala) */
    return (uint64_t)(int64_t)dede_konus_sirala(metin);
}

/* 1.5: UMAY'in urettigi konusma PCM'i (Dede Korkut, OSES). Beklemez:
 * halkada yer kalmadiysa 0 doner, uygulama sonra yine dener. Veri gorev
 * tamponundan (4 KB = 1024 kare) gecer; kullanici isaretcisi dogrulanir. */
static uint64_t sys_ses_pcm_impl(uint64_t pcm_p, uint64_t kare, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (kare == 0 || kare > SC_TAMPON_BOYUT / 4) return (uint64_t)-1;
    int16_t *t = (int16_t *)gorev_tamponu();
    if (!t) return (uint64_t)-1;
    if (kullanicidan_atomik_oku(t, pcm_p, kare * 4) != 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)ses_cal_dene(t, (uint32_t)kare);
}

static uint64_t sys_tema_impl(uint64_t ad_p, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    char ad[32];
    if (kullanici_dize_kopyala(ad_p, ad, sizeof(ad)) < 0) return (uint64_t)-1;
    int r = tema_uygula(ad);
    if (r == 0) {             /* 1.4.8: masaustu katmani yeni renklerle yeniden */
        ozkilit_al(&gorsel_kilit);
        ekran_masaustu_hazirla();
        ekran_isle();
        ozkilit_birak(&gorsel_kilit);
    }
    return (uint64_t)(int64_t)r;
}

/* 1.4.8: tema adlari (Gorunum uygulamasi). sira=-1: simdiki tema. */
static uint64_t sys_tema_bil_impl(uint64_t sira, uint64_t cikti, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    const tema_t *t = ((int64_t)sira == -1) ? tema_simdiki()
                    : (sira < (uint64_t)tema_sayisi() ? tema_sirali((int)sira) : 0);
    if (!t || !t->ad) return (uint64_t)-1;
    char ad[32];
    int i = 0;
    for (; t->ad[i] && i < 31; i++) ad[i] = t->ad[i];
    for (; i < 32; i++) ad[i] = 0;
    if (kullaniciya_atomik_yaz(cikti, ad, sizeof(ad)) != 0) return (uint64_t)-1;
    return 0;
}

/* --- 1.4.9: ag cagrilari (ag.c). Veri gorev tamponundan gecer (4 KB). --- */
static uint32_t ben_id(void) {
    task_t *t = scheduler_current_task();
    return t ? t->id : 0;
}

static uint64_t sys_ag_coz_impl(uint64_t ad_p, uint64_t cikti, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    char ad[256];
    if (kullanici_dize_kopyala(ad_p, ad, sizeof(ad)) < 0 || ad[0] == 0) return (uint64_t)-1;
    uint8_t ip[4];
    if (ag_coz(ad, ip) != 0) return (uint64_t)-1;
    if (kullaniciya_atomik_yaz(cikti, ip, 4) != 0) return (uint64_t)-1;
    return 0;
}

static uint64_t sys_ag_baglan_impl(uint64_t ip32, uint64_t port, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (port == 0 || port > 65535 || ip32 > 0xFFFFFFFFULL) return (uint64_t)-1;
    uint8_t ip[4] = { (uint8_t)(ip32 >> 24), (uint8_t)(ip32 >> 16), (uint8_t)(ip32 >> 8), (uint8_t)ip32 };
    return (uint64_t)(int64_t)ag_baglan(ben_id(), ip, (uint16_t)port);
}

static uint64_t sys_ag_gonder_impl(uint64_t t, uint64_t tampon, uint64_t n, uint64_t a4) {
    (void)a4;
    if (t > 0x7FFFFFFFULL || n > SC_TAMPON_BOYUT) return (uint64_t)-1;
    uint8_t *ara = (uint8_t *)gorev_tamponu();
    if (!ara) return (uint64_t)-1;
    if (n && kullanicidan_atomik_oku(ara, tampon, n) != 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)ag_gonder(ben_id(), (int)t, ara, (uint32_t)n);
}

static uint64_t sys_ag_al_impl(uint64_t t, uint64_t tampon, uint64_t azami, uint64_t a4) {
    (void)a4;
    if (t > 0x7FFFFFFFULL) return (uint64_t)-1;
    if (azami > SC_TAMPON_BOYUT) azami = SC_TAMPON_BOYUT;
    if (azami == 0 || !kullanici_tampon_yazilabilir(tampon, azami)) return (uint64_t)-1;
    uint8_t *ara = (uint8_t *)gorev_tamponu();
    if (!ara) return (uint64_t)-1;
    int n = ag_al(ben_id(), (int)t, ara, (uint32_t)azami);
    if (n > 0 && kullaniciya_atomik_yaz(tampon, ara, (uint64_t)n) != 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)n;
}

static uint64_t sys_ag_kapat_impl(uint64_t t, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    if (t > 0x7FFFFFFFULL) return (uint64_t)-1;
    return (uint64_t)(int64_t)ag_kapat(ben_id(), (int)t);
}

static uint64_t sys_ag_bilgi_impl(uint64_t cikti, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    ag_bilgi_t b;
    ag_bilgi(&b);
    if (kullaniciya_atomik_yaz(cikti, &b, sizeof(b)) != 0) return (uint64_t)-1;
    return 0;
}

/* --- 1.4.9: donanim rastgeleligi ve gercek saat (TLS icin) --- */
static int rng_turu = -1;             /* 0 yok, 1 RDRAND, 2 RDSEED (RDRAND ayrica olabilir) */
static int rng_rdrand = 0;            /* 1.5: RDRAND AYRI CPUID biti; RDSEED varken de olmayabilir */
static void rng_bul(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    int rdrand = (c >> 30) & 1;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    int rdseed = (b >> 18) & 1;
    rng_rdrand = rdrand;
    rng_turu = rdseed ? 2 : rdrand ? 1 : 0;
}
/* 1.5 (Codex): RDSEED var, RDRAND yoksa (CPUID'de RDRAND'i gizleyen sanal
 * makine) yedek RDRAND yolu #UD verirdi. Yedeğe yalnız RDRAND varsa geçilir;
 * yoksa bütün denemeler RDSEED ile yapılır, hepsi başarısızsa -1. */
static int rng_64(uint64_t *v) {
    for (int deneme = 0; deneme < 32; deneme++) {
        uint8_t ok;
        if (rng_turu == 2 && (deneme < 16 || !rng_rdrand))
            __asm__ volatile ("rdseed %0; setc %1" : "=r"(*v), "=qm"(ok));
        else if (rng_rdrand)
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
        else
            return -1;
        if (ok) return 0;
        __asm__ volatile ("pause");
    }
    return -1;
}
/* SYS_RASTGELE: a1=tampon a2=n (<=256) -> n / -1 (donanim RNG yoksa -1:
 * zayif kaynakla SESSIZCE devam etmek yerine acik ret). */
static uint64_t sys_rastgele_impl(uint64_t tampon, uint64_t n, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (n == 0 || n > 256) return (uint64_t)-1;
    if (rng_turu < 0) rng_bul();
    if (rng_turu == 0) return (uint64_t)-1;
    uint8_t ara[256];
    for (uint64_t i = 0; i < n; i += 8) {
        uint64_t v;
        if (rng_64(&v) != 0) return (uint64_t)-1;
        for (int k = 0; k < 8 && i + (uint64_t)k < n; k++) ara[i + k] = (uint8_t)(v >> (8 * k));
    }
    int r = kullaniciya_atomik_yaz(tampon, ara, n);
    for (int i = 0; i < 256; i++) ((volatile uint8_t *)ara)[i] = 0;
    return r == 0 ? n : (uint64_t)-1;
}

/* SYS_ZAMAN: gercek saat (UTC; CMOS dogrudan okunur, bkz. rtc.c).
 * a1=&sys_zaman_t -> 0/-1 */
static uint64_t sys_zaman_impl(uint64_t cikti, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a2; (void)a3; (void)a4;
    sys_zaman_t z;
    if (rtc_oku(&z) != 0) return (uint64_t)-1;
    return kullaniciya_atomik_yaz(cikti, &z, sizeof(z)) == 0 ? 0 : (uint64_t)-1;
}

/* --- 1.4.9: KERVAN v2 --- */
static uint64_t sys_kervan_liste_impl(uint64_t cikti, uint64_t azami, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (azami == 0) return 0;
    if (azami > KERVAN2_LISTE_AZAMI) azami = KERVAN2_LISTE_AZAMI;
    kervan2_oge_t *t = (kervan2_oge_t *)kmalloc(sizeof(kervan2_oge_t) * (uint64_t)azami);
    if (!t) return (uint64_t)-1;
    int n = kervan2_liste(t, (int)azami);
    uint64_t r = (uint64_t)n;
    if (n > 0 && kullaniciya_atomik_yaz(cikti, t, (uint64_t)n * sizeof(kervan2_oge_t)) != 0) r = (uint64_t)-1;
    kfree(t);
    return r;
}
static uint64_t kervan_ortak(uint64_t ad_p, uint64_t neden_p, int kur) {
    char ad[32], neden[64];
    neden[0] = 0;
    if (kullanici_dize_kopyala(ad_p, ad, sizeof(ad)) < 0) return (uint64_t)-1;
    int r = kur ? kervan2_kur(ad, neden) : kervan2_kaldir(ad, neden);
    neden[63] = 0;
    if (neden_p) kullaniciya_atomik_yaz(neden_p, neden, sizeof(neden));
    return (uint64_t)(int64_t)r;
}
static uint64_t sys_kervan_kur_impl(uint64_t a, uint64_t n, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4; return kervan_ortak(a, n, 1);
}
static uint64_t sys_kervan_kaldir_impl(uint64_t a, uint64_t n, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4; return kervan_ortak(a, n, 0);
}

/* =====================================================================
 * 1.4.7 — SYSCALL POLITIKA TABLOSU (TORE / KUT)
 *
 * Her cagri icin BILINCLI bir karar yazilir: gerekli TORE yetenegi ya da
 * SERBEST. Politikasi YAZILMAMIS cagri (tanimli=0) REDDEDILIR: VARSAYILAN
 * IZIN YOKTUR. Eskiden 36 cagridan yalniz 3'u (mkdir, write_file,
 * proc_liste) yetki denetliyordu; digerleri (dosya ac/oku, heap, IPC,
 * pencere, klavye, ses, tema...) HERKESE acikti.
 *
 * Kapi Ring3 cagirana uygulanir (task->kullanici): Ring3 programi AÇIK
 * profilini ring3_hazirla ile alir (ring3.c). Ring3'e hic gecmemis
 * cekirdek gorevi Ring0'da calisir (kendi syscall katmanini dogrudan
 * cagirir: iskence sinamalari isaretci dogrulamasini boyle sinar); ona
 * yetki kapisi anlamsizdir.
 *
 * Isaretci dogrulamasi ve NESNE SAHIPLIGI (dosya taniticisi, pencere,
 * IPC portu) cagrinin kendi icinde yapilir; tablo bunlari belgeler.
 * Tam tablo: docs/FD-YETKILENDIRME.md.
 * ===================================================================== */
#define SERBEST 0u
typedef struct {
    uint32_t    yetenek;     /* gerekli KUT biti; SERBEST = herkes */
    uint8_t     tanimli;     /* politika bilincli yazildi mi (0: RED) */
    const char *ad;          /* ret kaydinda */
} sc_politika_t;

static const sc_politika_t sc_politika[SYS_MAX] = {
    [SYS_WRITE]        = { KUT_EKRAN,      1, "konsol yaz" },
    [SYS_GETPID]       = { SERBEST,        1, "getpid" },
    [SYS_YIELD]        = { SERBEST,        1, "yield" },
    [SYS_SLEEP]        = { SERBEST,        1, "uyu" },
    [SYS_RING3_PROOF]  = { KUT_TANI,       1, "ring3 kaniti" },
    [SYS_EXIT]         = { SERBEST,        1, "cik" },
    [SYS_OPEN]         = { KUT_DISK_READ,  1, "dosya ac" },
    [SYS_READ]         = { KUT_DISK_READ,  1, "dosya oku" },
    [SYS_CLOSE]        = { KUT_DISK_READ,  1, "dosya kapat" },
    [SYS_SBRK]         = { KUT_MEM_ALLOC,  1, "heap buyut" },
    [SYS_READKEY]      = { KUT_KLAVYE,     1, "tus oku" },
    [SYS_LISTDIR]      = { KUT_DISK_READ,  1, "dizin listele" },
    [SYS_PUTPIXEL]     = { KUT_EKRAN,      1, "piksel" },
    [SYS_TICKS]        = { SERBEST,        1, "tik" },
    [SYS_PENCERE_AC]   = { KUT_EKRAN,      1, "pencere ac" },
    [SYS_PEN_DOLDUR]   = { KUT_EKRAN,      1, "pencere doldur" },
    [SYS_PEN_YAZI]     = { KUT_EKRAN,      1, "pencere yazi" },
    [SYS_PEN_DIK]      = { KUT_EKRAN,      1, "pencere dikdortgen" },
    [SYS_PEN_KAPAT]    = { KUT_EKRAN,      1, "pencere kapat" },
    [SYS_SERVIS_LISTE] = { KUT_SISTEM,     1, "servis listele" },
    [SYS_IPC_ACS]      = { KUT_IPC,        1, "ipc port ac" },
    [SYS_IPC_BUL]      = { KUT_IPC,        1, "ipc port bul" },
    [SYS_IPC_GONDER]   = { KUT_IPC,        1, "ipc gonder" },
    [SYS_IPC_AL]       = { KUT_IPC,        1, "ipc al" },
    [SYS_SISTEM_BIL]   = { KUT_SISTEM,     1, "sistem bilgisi" },
    [SYS_WRITE_FILE]   = { KUT_DISK_WRITE, 1, "dosya yaz" },
    [SYS_STAT]         = { KUT_DISK_READ,  1, "dosya bilgisi" },
    [SYS_MKDIR]        = { KUT_DISK_WRITE, 1, "dizin olustur" },
    [SYS_YETKI]        = { SERBEST,        1, "kendi yetkisini sor" },
    [SYS_PROC_LISTE]   = { KUT_PROC_CREATE,1, "surec listele" },
    [SYS_PEN_SUN]      = { KUT_EKRAN,      1, "pencere birlestir" },
    [SYS_PEN_OLAY]     = { KUT_KLAVYE,     1, "pencere olayi" },
    [SYS_SES_NOTA]     = { KUT_SES,        1, "ses tonu" },
    [SYS_OZAN_SOYLE]   = { KUT_SES,        1, "ozan soyle" },
    [SYS_TEMA]         = { KUT_AYAR,       1, "tema degistir" },
    [SYS_DEDE_KONUS]   = { KUT_SES,        1, "dede konus" },
    [SYS_TEMA_BIL]     = { KUT_SISTEM,     1, "tema adlari (salt okunur)" },
    [SYS_AG_COZ]       = { KUT_NET_SEND,   1, "ad cozumle (DNS sorusu gider)" },
    [SYS_AG_BAGLAN]    = { KUT_NET_SEND,   1, "TCP baglan" },
    [SYS_AG_GONDER]    = { KUT_NET_SEND,   1, "TCP gonder" },
    [SYS_AG_AL]        = { KUT_NET_RECV,   1, "TCP al" },
    [SYS_AG_KAPAT]     = { KUT_NET_SEND,   1, "TCP kapat" },
    [SYS_AG_BILGI]     = { KUT_SISTEM,     1, "ag durumu (salt okunur)" },
    [SYS_RASTGELE]     = { SERBEST,        1, "donanim rastgele sayilari" },
    [SYS_ZAMAN]        = { SERBEST,        1, "gercek saat (salt okunur)" },
    [SYS_KERVAN_LISTE] = { KUT_KERVAN,     1, "paketleri listele (dogrulayarak)" },
    [SYS_KERVAN_KUR]   = { KUT_KERVAN,     1, "imzali paket kur" },
    [SYS_KERVAN_KALDIR]= { KUT_KERVAN,     1, "paket kaldir" },
    [SYS_AG_BAGLANTILAR]={ KUT_SISTEM,     1, "TCP baglanti listesi" },
    [SYS_GUVENLIK_BILGI]={ KUT_SISTEM,     1, "guvenlik durumu (salt okunur)" },
    [SYS_ASENA_DENETLE]= { KUT_SISTEM,     1, "disk denetimi (salt okunur)" },
    [SYS_ASENA_ONAR]   = { KUT_KURTARMA,   1, "disk onarimi / dosya kurtarma" },
    [SYS_ERGENEKON]    = { KUT_KURTARMA,   1, "yeniden kurulum" },
    [SYS_TAMGA_BILGI]  = { KUT_SISTEM,     1, "kimlik bilgisi (salt okunur)" },
    [SYS_DOSYA_EKLE]   = { KUT_DISK_WRITE, 1, "dosya sonuna ekle" },
    [SYS_DOSYA_SIL]    = { KUT_DISK_WRITE, 1, "dosya / bos dizin sil" },
    [SYS_KERVAN_YUKLE] = { KUT_KERVAN,     1, "indirilen imzali paketi kur" },
    [SYS_SES_PCM]      = { KUT_SES,        1, "konusma PCM'i cal (beklemez)" },
};

/* --- 1.4.9: ULAK / TORE / TIMUR / KUT uygulamalari (salt okunur) --- */
static uint64_t sys_ag_baglantilar_impl(uint64_t cikti, uint64_t azami, uint64_t a3, uint64_t a4) {
    (void)a3; (void)a4;
    if (azami > 16) azami = 16;
    sys_tcp_ozet_t t[16];
    uint64_t n = 0;
    for (; n < azami; n++) {
        int durum, sahipli;
        sys_tcp_ozet_t *o = &t[n];
        o->b1 = o->b2 = 0;
        if (tcp_ozet((int)n, o->ip, &o->yerel, &o->uzak, &durum, &sahipli, &o->alinan, &o->gonderilen, &o->yeniden) != 0) break;
        o->durum = (uint8_t)durum; o->sahipli = (uint8_t)sahipli;
    }
    if (n && kullaniciya_atomik_yaz(cikti, t, n * sizeof(sys_tcp_ozet_t)) != 0) return (uint64_t)-1;
    return n;
}

static uint64_t oku_cr4(void) { uint64_t v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v; }


/* ===================== 1.4.9: ASENA, ERGENEKON, TAMGA ===================== */
static uint64_t sys_asena_denetle_impl(uint64_t tur, uint64_t sira, uint64_t cikti, uint64_t a4) {
    if (tur == 0) {
        asena_rapor_t r;
        int t = asena_tara(0, &r);
        /* rapor her durumda yazılır (hata kodu arayüzde gösterilir), ama
         * tarama başarısızsa çağrı da başarısız döner: yarım rapor temiz görünmesin */
        if (kullaniciya_atomik_yaz(cikti, &r, sizeof r) != 0) return (uint64_t)-1;
        return (t != 0 || r.hata) ? (uint64_t)-1 : 0;
    }
    if (tur == 1) {
        asena_bulgu_t b;
        if (asena_bulgu((int)sira, (uint32_t)a4, &b) != 0) return (uint64_t)-1;   /* a4: raporun kuşağı */
        return kullaniciya_atomik_yaz(cikti, &b, sizeof b) == 0 ? 0 : (uint64_t)-1;
    }
    if (tur == 2) {
        ergenekon_dosya_t d;
        if (sira > 1024 || ergenekon_dosya((int)sira, &d) != 0) return (uint64_t)-1;
        return kullaniciya_atomik_yaz(cikti, &d, sizeof d) == 0 ? 0 : (uint64_t)-1;
    }
    return (uint64_t)-1;
}

static uint64_t sys_asena_onar_impl(uint64_t tur, uint64_t ad_p, uint64_t cikti, uint64_t onay) {
    /* diske yazan cagri: acik onay sozcugu ve cikti yeri SART (butun
     * argumanlari 0 olan bir cagri - ISKENCE sinamasi boyle cagirir -
     * hicbir sey yazmamali) */
    if (onay != SYS_ONAY || cikti == 0) return (uint64_t)-1;
    if (tur == 0) {
        asena_rapor_t r;
        asena_tara(1, &r);
        return kullaniciya_atomik_yaz(cikti, &r, sizeof r) == 0 ? (r.hata ? (uint64_t)-1 : 0) : (uint64_t)-1;
    }
    if (tur == 1) {
        char ad[16], neden[64];
        if (kullanici_dize_kopyala(ad_p, ad, 13) < 0) return (uint64_t)-1;
        int r = ergenekon_dosya_kurtar(ad, neden);
        if (kullaniciya_atomik_yaz(cikti, neden, sizeof neden) != 0) return (uint64_t)-1;
        return (uint64_t)(int64_t)r;
    }
    return (uint64_t)-1;
}

static uint64_t sys_ergenekon_impl(uint64_t islem, uint64_t cikti, uint64_t onay, uint64_t a4) {
    (void)a4;
    if (cikti == 0) return (uint64_t)-1;
    if (islem != 0 && onay != SYS_ONAY) return (uint64_t)-1;   /* yikici islemler onay ister */
    ergenekon_durum_t d;
    int r;
    switch (islem) {
    case 0: r = ergenekon_durum(&d); break;
    case 1: r = ergenekon_yenile(&d); break;
    case 2: r = ergenekon_sifirdan(&d); break;
    case 3: ergenekon_yeniden_baslat();
    default: return (uint64_t)-1;
    }
    if (kullaniciya_atomik_yaz(cikti, &d, sizeof d) != 0) return (uint64_t)-1;
    return (uint64_t)(int64_t)r;
}

extern const char cekirdek_metin_bas[], cekirdek_metin_son[], cekirdek_salt_bas[], cekirdek_salt_son[];
static uint64_t sys_tamga_bilgi_impl(uint64_t tur, uint64_t sira, uint64_t cikti, uint64_t a4) {
    (void)a4;
    sys_tamga_t t;
    for (unsigned i = 0; i < sizeof t; i++) ((uint8_t *)&t)[i] = 0;
    br_sha256_context h;
    if (tur == TB_ANAHTAR && sira < 2) {
        const uint8_t *a = sira == 0 ? KERVAN_ANAHTAR_0 : KERVAN_ANAHTAR_1;
        for (int i = 0; i < 65; i++) t.anahtar[i] = a[i];
        br_sha256_init(&h); br_sha256_update(&h, a, 65); br_sha256_out(&h, t.ozet);
        t.gecerli = (uint8_t)(sira == 0 ? 1 : OTUKEN_TANI_MODU);
    } else if (tur == TB_CEKIRDEK && sira == 0) {
        uint64_t mb = (uint64_t)(cekirdek_metin_son - cekirdek_metin_bas);
        uint64_t sb = (uint64_t)(cekirdek_salt_son - cekirdek_salt_bas);
        br_sha256_init(&h);
        br_sha256_update(&h, cekirdek_metin_bas, mb);
        br_sha256_update(&h, cekirdek_salt_bas, sb);
        br_sha256_out(&h, t.ozet);
        t.boy = (uint32_t)(mb + sb);
        t.gecerli = 1;
    } else return (uint64_t)-1;
    return kullaniciya_atomik_yaz(cikti, &t, sizeof t) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t sys_guvenlik_bilgi_impl(uint64_t tur, uint64_t sira, uint64_t cikti, uint64_t a4) {
    (void)a4;
    sys_guvenlik_t g;
    for (unsigned i = 0; i < sizeof g; i++) g.ham[i] = 0;
    int ok = 0;
    switch (tur) {
    case GB_POLITIKA:
        if (sira < SYS_MAX) {
            const sc_politika_t *p = &sc_politika[sira];
            g.politika.no = (int32_t)sira;
            g.politika.yetki = (!p->tanimli || !syscall_table[sira]) ? -1 : (int32_t)p->yetenek;
            const char *a = p->tanimli ? p->ad : "(tanimsiz)";
            for (int i = 0; a && a[i] && i < 39; i++) g.politika.ad[i] = a[i];
            ok = 1;
        }
        break;
    case GB_GOREV: {
        uint32_t id, durum, kb;
        char ad[16];
        if (sira < 0xFFFFFFFFULL && scheduler_gorev_bilgi((uint32_t)sira, &id, ad, &durum, &kb) == 0) {
            g.gorev.id = id; g.gorev.durum = (uint8_t)durum;
            g.gorev.kayitli = (uint8_t)tore_kayit_var_mi(id);
            g.gorev.yetki = g.gorev.kayitli ? tore_get_capabilities(id) : 0;
            int k = scheduler_gorev_kullanici((uint32_t)sira);
            g.gorev.kullanici = (uint8_t)(k > 0);
            const kut_user_t *u = kut_owner_of(id);
            g.gorev.uid = u ? u->uid : 0;
            for (int i = 0; i < 16; i++) g.gorev.ad[i] = ad[i];
            g.gorev.ad[15] = 0;
            ok = 1;
        }
        break;
    }
    case GB_YETKI_ADI:
        if (sira < KUT_YETKI_SAYISI) {
            const char *a = tore_capability_name((kut_capability_t)(1u << sira));
            for (int i = 0; a && a[i] && i < 23; i++) g.yetki_adi.ad[i] = a[i];
            ok = 1;
        }
        break;
    case GB_TIMUR:
        if (sira == 0) {
            uint32_t ea, eb, ec, ed;
            __asm__ volatile ("cpuid" : "=a"(ea), "=b"(eb), "=c"(ec), "=d"(ed) : "a"(7), "c"(0));
            uint64_t cr4 = oku_cr4();
            g.timur.nx_destek = (uint8_t)timur_nx_available();
            g.timur.nx_acik = (uint8_t)((rdmsr(0xC0000080) >> 11) & 1);
            g.timur.smep_destek = (uint8_t)((eb >> 7) & 1);
            g.timur.smap_destek = (uint8_t)((eb >> 20) & 1);
            g.timur.smep_acik = (uint8_t)((cr4 >> 20) & 1);
            g.timur.smap_acik = (uint8_t)((cr4 >> 21) & 1);
            if (rng_turu < 0) rng_bul();
            g.timur.rng = (uint8_t)rng_turu;
            g.timur.rtc = (uint8_t)zaman_var();
            g.timur.reddedilen = tore_denied_count();
            g.timur.politikasiz = (uint32_t)syscall_politikasiz_sayisi();
            g.timur.kervan_kurulu = (uint32_t)kervan2_kurulu_sayisi();
            g.timur.cagri_sayisi = SYS_MAX;
            { uint64_t cr0; __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0)); g.timur.wp = (uint32_t)((cr0 >> 16) & 1); }
            uint32_t r3 = 0;
            for (uint32_t i = 0; i < 256; i++) { int k = scheduler_gorev_kullanici(i); if (k < 0) break; r3 += (k > 0); }
            g.timur.ring3_gorev = r3;
            ok = 1;
        }
        break;
    case GB_KULLANICI: {
        kut_user_t u;
        if (sira < 64 && kut_kullanici_al((int)sira, &u) == 0) {
            g.kullanici.uid = u.uid; g.kullanici.rol = (uint32_t)u.rol;
            for (int i = 0; i < 15 && u.name[i]; i++) g.kullanici.ad[i] = u.name[i];
            const char *r = kut_rol_name(u.rol);
            for (int i = 0; i < 15 && r[i]; i++) g.kullanici.rol_ad[i] = r[i];
            ok = 1;
        }
        break;
    }
    case GB_KUT_BAG:
        if (sira < 64 && kut_bag_al((int)sira, &g.bag.pid, &g.bag.uid) == 0) ok = 1;
        break;
    default: break;
    }
    if (!ok) return (uint64_t)-1;
    return kullaniciya_atomik_yaz(cikti, &g, sizeof g) == 0 ? 0 : (uint64_t)-1;
}

/* Politika kapisi: Ring3 cagiraninin yetkisi var mi (tek karar noktasi). */
static int sc_izinli(uint64_t num) {
    const sc_politika_t *p = &sc_politika[num];
    if (!p->tanimli) {
        log_write_num(LOG_UYARI, "SYSCALL", "politikasi olmayan cagri reddedildi", num);
        return 0;
    }
    if (p->yetenek == SERBEST) return 1;
    task_t *t = scheduler_current_task();
    if (!t || !t->kullanici) return 1;           /* Ring0 cekirdek gorevi */
    return tore_check((uint32_t)t->id, (kut_capability_t)p->yetenek, p->ad);
}

/* Sinama/denetim: kayitli olup politikasi yazilmamis cagri sayisi (0 olmali). */
int syscall_politikasiz_sayisi(void) {
    int n = 0;
    for (int i = 0; i < SYS_MAX; i++)
        if (syscall_table[i] && !sc_politika[i].tanimli) n++;
    return n;
}

/* Sinama: politikadaki cagri adi (tanimsizsa 0) */
const char *syscall_politika_adi(uint64_t num) {
    return (num < SYS_MAX && sc_politika[num].tanimli) ? sc_politika[num].ad : 0;
}

/* Sinama: bir cagrinin gerektirdigi yetenek (SERBEST=0); politikasizsa -1. */
int64_t syscall_gereken_yetki(uint64_t num) {
    if (num >= SYS_MAX || !sc_politika[num].tanimli) return -1;
    return (int64_t)sc_politika[num].yetenek;
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    if (num >= SYS_MAX || syscall_table[num] == 0) {
        log_write_num(LOG_UYARI, "SYSCALL", "gecersiz cagri no", num);
        return (uint64_t)-1;
    }
    if (!sc_izinli(num)) return (uint64_t)-1;
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
    /* SYSCALL sirasinda IF (bit9) kapatilsin; 1.4.9: AC (bit18, SMAP'i
     * delerdi: kullanici popf ile AC=1 yapip cekirdege girebilirdi) ve DF de */
    wrmsr(MSR_FMASK, 0x200 | 0x400 | 0x40000);
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
    syscall_table[SYS_TEMA_BIL]    = sys_tema_bil_impl;
    syscall_table[SYS_AG_COZ]      = sys_ag_coz_impl;
    syscall_table[SYS_AG_BAGLAN]   = sys_ag_baglan_impl;
    syscall_table[SYS_AG_GONDER]   = sys_ag_gonder_impl;
    syscall_table[SYS_AG_AL]       = sys_ag_al_impl;
    syscall_table[SYS_AG_KAPAT]    = sys_ag_kapat_impl;
    syscall_table[SYS_AG_BILGI]    = sys_ag_bilgi_impl;
    syscall_table[SYS_RASTGELE]    = sys_rastgele_impl;
    syscall_table[SYS_ZAMAN]       = sys_zaman_impl;
    syscall_table[SYS_KERVAN_LISTE]= sys_kervan_liste_impl;
    syscall_table[SYS_KERVAN_KUR]  = sys_kervan_kur_impl;
    syscall_table[SYS_KERVAN_KALDIR]= sys_kervan_kaldir_impl;
    syscall_table[SYS_AG_BAGLANTILAR]= sys_ag_baglantilar_impl;
    syscall_table[SYS_GUVENLIK_BILGI]= sys_guvenlik_bilgi_impl;
    syscall_table[SYS_ASENA_DENETLE] = sys_asena_denetle_impl;
    syscall_table[SYS_ASENA_ONAR]    = sys_asena_onar_impl;
    syscall_table[SYS_ERGENEKON]     = sys_ergenekon_impl;
    syscall_table[SYS_TAMGA_BILGI]   = sys_tamga_bilgi_impl;
    syscall_table[SYS_DOSYA_EKLE]    = sys_dosya_ekle_impl;
    syscall_table[SYS_DOSYA_SIL]     = sys_dosya_sil_impl;
    syscall_table[SYS_KERVAN_YUKLE]  = sys_kervan_yukle_impl;
    syscall_table[SYS_SES_PCM]       = sys_ses_pcm_impl;
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
