#ifndef OTUKENOS_CPU_H
#define OTUKENOS_CPU_H

#include <stdint.h>
#include "syscall.h"
#include "ipc2.h"

/* --- CEKIRDEK-BASINA VERI (SMP hazirligi, adim 2) ---
 * syscall yollari 4 adet STATIC cekirdek tamponu kullaniyordu. Tek
 * cekirdekte guvenli (kopyalama kesmeler kapaliyken yapiliyor), ama
 * iki cekirdek AYNI ANDA syscall yaparsa ayni tampona yazar ve
 * birbirinin verisini EZER - bir surec baska surecin dosya icerigini
 * okuyabilir. Her cekirdege kendi tampon blogu verilir. Kimlik,
 * CPUID yaprak 1 EBX[31:24] (ilk APIC kimligi) ile alinir; APIC
 * baslatilmadan da calisir. */

#define CPU_AZAMI 64

/* --- SMP adim 4: GS tabani ---
 * Cekirdek kipinde GS_BASE = bu cekirdegin blogu; kullanici kipindeyken
 * blok adresi KERNEL_GS_BASE'de bekler (swapgs duzeni). Ring3 sinirini
 * gecen her giris/cikis (syscall, kesme/istisna, enter_ring3) swapgs
 * yapar. Asm'nin eristigi alanlar SABIT ofsetlerdedir - degistirme. */
#define CPU_OFS_KENDI        0   /* bloga isaretci (gs:0)          */
#define CPU_OFS_CEKIRDEK_RSP 8   /* gecerli gorevin cekirdek yigini */
#define CPU_OFS_ARA_RSP      16  /* syscall girisinde kullanici RSP */
#define CPU_OFS_SON_RIP      24  /* son syscall donus RIP'i (tani)  */

typedef struct {
    struct cpu_yerel *kendi;         /* 0  */
    uint64_t      cekirdek_rsp;      /* 8  */
    uint64_t      ara_rsp;           /* 16 */
    uint64_t      son_syscall_rip;   /* 24 */
    uint32_t      indeks;            /* 0 = BSP, AP'ler baslatma sirasiyla */
    uint32_t      apic_id;
    int           kullanimda;
    uint64_t      syscall_sayisi;
    uint64_t      ipi_sayisi;        /* islenen cagri IPI'leri */
    void         *simdiki_gorev;     /* task_t*: bu cekirdekte calisan gorev */
    void         *onceki_gorev;      /* gecis sirasinda: birakilacak gorev */
    uint32_t      rr_indeks;         /* round-robin baslangici */
    uint64_t      gecis_sayisi;      /* baglam gecisi sayaci (kanit) */
    uint64_t      yerel_tik;         /* LAPIC zamanlayici tiki (AP) */
    uint64_t      is_gecis;          /* BOS OLMAYAN goreve gecis sayisi (dagitim kaniti) */
    /* (syscall ara tamponlari buradaydi; kesilme altinda paylasildiklari
     *  icin SMP adim 6'da GOREV-BASINA tasindi: task_t.sc_tampon) */
} cpu_yerel_t;

/* Hizli erisim (gs:0), sayac artirmaz. Cekirdek kipinde her yerde gecerli. */
static inline cpu_yerel_t *cpu_bu(void) {
    cpu_yerel_t *c;
    __asm__ volatile ("mov %%gs:0, %0" : "=r"(c));
    return c;
}

cpu_yerel_t *cpu_yerel(void);        /* cpu_bu + syscall sayaci (geriye uyum) */
cpu_yerel_t *cpu_indeksle(uint32_t indeks);
uint32_t     cpu_apic_id(void);      /* CPUID ile (GS'ten bagimsiz) */
int          cpu_sayisi(void);
/* BSP: kernel_main'in EN BASINDA, syscall/gorevlerden once. */
void         cpu_bsp_hazirla(void);
/* AP: ap_giris'te, GDT yuklendikten sonra. */
void         cpu_ap_hazirla(uint32_t indeks, uint32_t apic_id);

#endif
