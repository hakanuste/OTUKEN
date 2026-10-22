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

#define SYS_TEMA_BIL     36  /* a1=sira (-1: simdiki) a2=&char[32] -> 0/-1 */

/* 1.4.9: ag (ULAK) - Ring3 tutamaklari (bkz. ag.h) */
#define SYS_AG_COZ       37  /* a1=ad a2=&uint8_t[4] -> 0/-1 */
#define SYS_AG_BAGLAN    38  /* a1=ip (a<<24|b<<16|c<<8|d) a2=port -> tutamak/-1 */
#define SYS_AG_GONDER    39  /* a1=tutamak a2=tampon a3=n (<=4096) -> kabul/-1 */
#define SYS_AG_AL        40  /* a1=tutamak a2=tampon a3=azami -> n / 0 yok / -2 son / -1 */
#define SYS_AG_KAPAT     41  /* a1=tutamak -> 0/-1 */
#define SYS_AG_BILGI     42  /* a1=&ag_bilgi_t -> 0/-1 */

/* 1.4.9: TLS icin */
#define SYS_RASTGELE     43  /* a1=tampon a2=n(<=256) -> n / -1 (donanim RNG yoksa -1) */
#define SYS_ZAMAN        44  /* a1=&sys_zaman_t -> 0/-1 (CMOS RTC, UTC) */

/* 1.4.9: KERVAN v2 (imzali paketler; bkz. kervan2.h). neden: 64 baytlik tampon */
#define SYS_KERVAN_LISTE 45  /* a1=&kervan2_oge_t[] a2=azami -> adet / -1 */
#define SYS_KERVAN_KUR   46  /* a1=".KRV adi" a2=neden -> 0/-1 */
#define SYS_KERVAN_KALDIR 47 /* a1=paket adi a2=neden -> 0/-1 */

/* 1.4.9: ULAK / TORE / TIMUR / KUT uygulamalari (salt okunur, KUT_SISTEM) */
#define SYS_AG_BAGLANTILAR 48  /* a1=&sys_tcp_ozet_t[] a2=azami -> adet */
#define SYS_GUVENLIK_BILGI 49  /* a1=tur a2=sira a3=&sys_guvenlik_t -> 0 / -1 (son) */

#define SYS_ASENA_DENETLE 50 /* 1.4.9 a1=tur(0 tara,1 bulgu,2 dizge dosyasi) a2=sira a3=&cikti */
#define SYS_ASENA_ONAR    51 /* 1.4.9 a1=tur(0 onar -> asena_rapor_t, 1 dosya kurtar a2=&ad) a3=&cikti */
#define SYS_ERGENEKON     52 /* 1.4.9 a1=islem(0 durum,1 yenile,2 sifirdan,3 yeniden baslat) a2=&durum */
#define SYS_TAMGA_BILGI   53 /* 1.4.9 a1=tur a2=sira a3=&sys_tamga_t */
#define SYS_DOSYA_EKLE    54 /* 1.4.9 a1=&yol a2=&veri a3=n -> eklenen / -1 */
#define SYS_DOSYA_SIL     55 /* 1.4.9 a1=&yol -> 0 / -1 (dizin yalniz bossa) */
#define SYS_KERVAN_YUKLE  56 /* 1.4.9 a1=&veri a2=boy a3=&neden[64] -> 0 / -1 */
#define SYS_MAX          57
/* 1.4.9: diske yazan / makineyi yeniden baslatan cagrilarin onay sozcugu ("ONAY") */
#define SYS_ONAY 0x59414E4FULL

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

typedef struct {
    uint32_t yil, ay, gun, saat, dakika, saniye;
} sys_zaman_t;

typedef struct {
    uint8_t  ip[4];
    uint16_t yerel, uzak;
    uint8_t  durum, sahipli, b1, b2;
    uint32_t alinan, gonderilen, yeniden;
} sys_tcp_ozet_t;

/* SYS_GUVENLIK_BILGI turleri */
#define GB_POLITIKA  0   /* i. cagri: no, gerekli yetki (-1 politikasiz, 0 serbest), ad */
#define GB_GOREV     1   /* i. gorev: kimlik, ad, TORE yetkisi, Ring3 mu, KUT uid */
#define GB_YETKI_ADI 2   /* i. yetki bitinin adi */
#define GB_TIMUR     3   /* korumalarin durumu (sira 0) */
#define GB_KULLANICI 4   /* i. KUT kullanicisi */
#define GB_KUT_BAG   5   /* i. surec-kullanici bagi */
typedef union {
    struct { int32_t no; int32_t yetki; char ad[40]; } politika;
    struct { uint32_t id, yetki, uid; uint8_t durum, kayitli, kullanici, b; char ad[16]; } gorev;
    struct { char ad[24]; } yetki_adi;
    struct { uint8_t nx_destek, nx_acik, smep_destek, smep_acik, smap_destek, smap_acik, rng, rtc;
             uint32_t reddedilen, politikasiz, ring3_gorev, kervan_kurulu, cagri_sayisi, wp; } timur;
    struct { uint32_t uid, rol; char ad[16]; char rol_ad[16]; } kullanici;
    struct { uint32_t pid, uid; } bag;
    uint8_t ham[64];
} sys_guvenlik_t;

/* 1.4.9 SYS_TAMGA_BILGI turleri */
#define TB_ANAHTAR   0   /* sira 0 uretim, 1 sinama: acik anahtar + gecerli mi */
#define TB_CEKIRDEK  1   /* cekirdek .text+.rodata SHA-256 */
typedef struct {
    uint8_t  ozet[32];      /* anahtar: acik anahtarin SHA-256 parmak izi; cekirdek: ozet */
    uint8_t  anahtar[65];   /* sikistirilmamis nokta (TB_ANAHTAR) */
    uint8_t  gecerli;       /* bu yapida imza kabul ediliyor mu */
    uint16_t b;
    uint32_t boy;           /* TB_CEKIRDEK: ozetlenen bayt */
    uint8_t  bos[24];
} sys_tamga_t;
_Static_assert(sizeof(sys_tamga_t) == 128, "tamga");

/* SYS_SISTEM_BIL alanlari */
#define SYSBIL_BOS_CERCEVE   0
#define SYSBIL_TOPLAM_BELLEK 1
#define SYSBIL_GOREV_SAYISI  2
#define SYSBIL_TIK           3
#define SYSBIL_TIK_HZ        4   /* tik frekansi (Hz); eski cekirdek 0 dondurur */
#define SYSBIL_SES_VAR       5   /* 1.4.9: ses denetleyicisi (AC'97) bulundu mu */
#define SYSBIL_DEDE_MESGUL   6   /* 1.4.9: DEDE sirasinda/soylenmekte metin var mi */


/* SYSCALL/SYSRET donanim mekanizmasini kurar (STAR/LSTAR/FMASK MSR'lari,
 * EFER.SCE) ve syscall tablosunu doldurur. */
void syscall_init(void);
/* SMP: cagiran cekirdegin SYSCALL MSR'lerini (EFER.SCE/STAR/LSTAR/FMASK) kurar. */
void syscall_msr_kur(void);

/* syscall_entry.asm tarafindan cagirilir - dogrudan kullanici kodundan
 * cagirilmasi AMACLANMAMISTIR. */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);

/* 1.4.7 politika tablosu denetimi (bkz. syscall.c):
 * kayitli olup politikasi YAZILMAMIS cagri sayisi (0 olmali). */
int     syscall_politikasiz_sayisi(void);
/* heap penceresi yuvasini iade eder (reap cagirir) */
void    heap_yuva_birak(int yuva);
const char *syscall_politika_adi(uint64_t num);
/* bir cagrinin gerektirdigi TORE yetenegi (0 = SERBEST); politikasizsa -1 */
int64_t syscall_gereken_yetki(uint64_t num);

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
