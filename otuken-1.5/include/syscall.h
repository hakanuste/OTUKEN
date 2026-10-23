#ifndef OTUKENOS_SYSCALL_H
#define OTUKENOS_SYSCALL_H

#include <stdint.h>

/* Cekirdek ile userland ARASINDA paylasilan dizin girdisi yapisi.
 * Bu yapinin duzeni ABI'nin parcasidir - degistirilirse userland
 * programlari YENIDEN DERLENMELIDIR. */
typedef struct {
    char     name[32];
    uint64_t size;
    uint32_t is_dir;
    uint32_t reserved;
} sys_dirent_t;

/* Asama 34: userland'in servis durumunu gorebilmesi icin */
typedef struct {
    char     name[20];
    uint32_t durum;
    uint32_t nabiz;
} sys_servis_t;

#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_SLEEP       3
#define SYS_RING3_PROOF 4
#define SYS_EXIT        5
/* --- Asama 24: userland runtime icin gereken cagrilar --- */
#define SYS_OPEN        6   /* a1=yol -> fd (>=0) ya da -1 */
#define SYS_READ        7   /* a1=fd a2=tampon a3=boyut -> okunan bayt */
#define SYS_CLOSE       8   /* a1=fd */
#define SYS_SBRK        9   /* a1=artis -> eski heap sonu (userland malloc) */
#define SYS_READKEY    10   /* -> tus kodu, yoksa 0 (bloklamaz) */
#define SYS_LISTDIR    11   /* a1=yol a2=cikti a3=max -> girdi sayisi */
#define SYS_PUTPIXEL   12   /* a1=x a2=y a3=rgb */
#define SYS_TICKS      13   /* -> zamanlayici tik sayisi */
/* --- Asama 34: Uygulama API'si (ULGEN pencere sistemi) --- */
#define SYS_PENCERE_AC   14  /* a1=baslik a2=(x<<16|y) a3=(w<<16|h) -> id */
#define SYS_PEN_DOLDUR   15  /* a1=id a2=renk */
#define SYS_PEN_YAZI     16  /* a1=id a2=(x<<16|y) a3=metin a4=renk */
#define SYS_PEN_DIK      17  /* a1=id a2=(x<<16|y) a3=(w<<16|h) a4=renk */
#define SYS_PEN_KAPAT    18  /* a1=id */
#define SYS_SERVIS_LISTE 19  /* a1=cikti a2=max -> servis sayisi */

/* --- UMAY'in AYRI bir kullanici programi olarak calisabilmesi icin
 * gereken cagrilar. 14-19 pencere API'sinde kullanildigi icin
 * 20'den devam edilir. */
#define SYS_IPC_ACS      20  /* a1=isim -> port kimligi */
#define SYS_IPC_BUL      21  /* a1=isim -> port kimligi ya da -1 */
#define SYS_IPC_GONDER   22  /* a1=hedef a2=opkod a3=veri a4=uzunluk */
#define SYS_IPC_AL       23  /* a1=port a2=cikti -> 1/0 */
#define SYS_SISTEM_BIL   24  /* a1=alan -> sistem bilgisi */

/* --- UMAY paketi (Asama 42) cagrilari ---
 * Numaralar UMAY'in port katmaniyla (umay/port/umay_port_otuken.c)
 * AYNI olmalidir. 25 ve 29 paketteki cekirdekte tanimsizdi; burada
 * gercekten yazildi. */
#define SYS_WRITE_FILE   25  /* a1=yol a2=tampon a3=uzunluk -> yazilan / -1 */
#define SYS_STAT         26  /* a1=yol a2=&sys_stat_t -> 0/-1 */
#define SYS_MKDIR        27  /* a1=yol -> 0 / hata */
#define SYS_YETKI        28  /* a1=yetenek biti -> 1 var, 0 yok */
#define SYS_PROC_LISTE   29  /* a1=&sys_surec_t[] a2=azami -> adet / -1 */
#define SYS_PEN_SUN      30  /* a1=id -> yuzeyi ekrana bas */
#define SYS_PEN_OLAY     31  /* a1=id a2=&sys_olay_t -> 1 olay / 0 yok */
#define SYS_SES_NOTA     32  /* a1=Hz a2=ms */
#define SYS_OZAN_SOYLE   33  /* a1=metin a2=hiz */
#define SYS_TEMA         34  /* a1=tema adi */
#define SYS_DEDE_KONUS   35  /* a1=metin -> 0/-1 (formant sentezi) */

#define SYS_MAX          36

typedef struct {
    uint32_t tur;        /* 0=yok 1=tus 2=tik 3=kapat */
    uint32_t tus;
    int32_t  x, y;
} sys_olay_t;

typedef struct {
    uint64_t boyut;
    uint32_t dizin_mi;
    uint32_t ayrilmis;
} sys_stat_t;

/* UMAY: struct { uint32_t pid; char ad[16]; uint32_t durum, bellek_kb; } */
typedef struct {
    uint32_t pid;
    char     ad[16];
    uint32_t durum;      /* task_state_t */
    uint32_t bellek_kb;  /* cekirdek yigini + kullanici heap penceresi */
} sys_surec_t;

/* SYS_SISTEM_BIL alanlari */
#define SYSBIL_BOS_CERCEVE   0
#define SYSBIL_TOPLAM_BELLEK 1
#define SYSBIL_GOREV_SAYISI  2
#define SYSBIL_TIK           3
#define SYSBIL_TIK_HZ        4   /* tik frekansi (Hz); eski cekirdek 0 dondurur */


/* SYSCALL/SYSRET donanim mekanizmasini kurar (STAR/LSTAR/FMASK MSR'lari,
 * EFER.SCE) ve syscall tablosunu doldurur. */
void syscall_init(void);
/* SMP: cagiran cekirdegin SYSCALL MSR'lerini (EFER.SCE/STAR/LSTAR/FMASK) kurar. */
void syscall_msr_kur(void);

/* syscall_entry.asm tarafindan cagirilir - dogrudan kullanici kodundan
 * cagirilmasi AMACLANMAMISTIR. */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);

/* Test/dogrulama icin: kullanici tarafi sozlesmesiyle (RAX=no,
 * RDI/RSI/RDX/R10=a1..a4) gercek SYSCALL komutunu calistirir. */
static inline uint64_t do_syscall(uint64_t num, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#endif
