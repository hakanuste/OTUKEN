#ifndef OTUKENOS_TORE_H
#define OTUKENOS_TORE_H

#include <stdint.h>

/* TORE - otukenOS guvenlik politikalari ve yetki yonetimi (KUT).
 * Her surece bir yetki kumesi (capability set) atanir; ayricalikli
 * islemler bu kume uzerinden denetlenir. */

typedef enum {
    KUT_DISK_READ    = (1 << 0),
    KUT_DISK_WRITE   = (1 << 1),
    KUT_NET_SEND     = (1 << 2),
    KUT_NET_RECV     = (1 << 3),
    KUT_MEM_ALLOC    = (1 << 4),
    KUT_PROC_CREATE  = (1 << 5),
    KUT_PROC_KILL    = (1 << 6),
    KUT_HW_DIRECT    = (1 << 7),  /* dogrudan donanim erisimi */
    /* --- 1.4.7: eskiden HIC denetlenmeyen syscall'lar icin yetkiler ---
     * (0..7 bitlerinin degerleri DEGISMEDI: ABI ve UMAY'in eslemesi ayni) */
    KUT_IPC          = (1 << 8),  /* IPC portu ac/bul/gonder/al */
    KUT_EKRAN        = (1 << 9),  /* konsol yazisi, pencere, piksel */
    KUT_KLAVYE       = (1 << 10), /* tus ve pencere olayi okuma */
    KUT_SES          = (1 << 11), /* ses tonu, OZAN/DEDE konusma */
    KUT_SISTEM       = (1 << 12), /* salt-okunur sistem/servis bilgisi */
    KUT_AYAR         = (1 << 13), /* KURESEL ayar degisikligi (tema) */
    KUT_TANI         = (1 << 14), /* yalniz sinama programlari (RING3_PROOF) */
    KUT_KERVAN       = (1 << 15), /* 1.4.9: IMZALI paket kur/kaldir (cekirdek dogrular) */
    KUT_KURTARMA     = (1 << 16)  /* 1.4.9: ASENA onarimi, ERGENEKON yeniden kurulum */
} kut_capability_t;

#define KUT_YETKI_SAYISI 17
#define KUT_TUMU  ((1u << KUT_YETKI_SAYISI) - 1u)   /* tanimli butun yetkiler */

/* Onceden tanimli yetki profilleri */
#define TORE_PROFILE_CEKIRDEK  KUT_TUMU   /* tam yetki (1.4.7: yeni bitler dahil) */
#define TORE_PROFILE_HIZMET    (KUT_DISK_READ | KUT_NET_SEND | KUT_NET_RECV | KUT_MEM_ALLOC)
#define TORE_PROFILE_KULLANICI (KUT_DISK_READ | KUT_MEM_ALLOC)
#define TORE_PROFILE_KISITLI   (KUT_MEM_ALLOC)

/* --- 1.4.7: Ring3 program profilleri (AÇIK, en az yetki) ---
 * Her Ring3 programi Ring3'e gecmeden ONCE yalniz kullandigi cagrilarin
 * yetkisini alir; tanimsiz her sey REDDEDILIR (bkz. syscall.c politika
 * tablosu). DISK_YAZ ve SUREC_OLUSTUR UMAY'a 1.4.6'da da VERILMIYORDU;
 * davranis korunur. */
#define TORE_PROFILE_KABUK  (KUT_DISK_READ | KUT_MEM_ALLOC | KUT_IPC | KUT_EKRAN | \
                             KUT_KLAVYE | KUT_SISTEM)
/* 1.4.9: UMAY DISK_YAZ aldi, ama YALNIZ /KUT/UMAY altina (tore_yazma_koku):
 * BENGI (kalici bellek), ayar ve gunluk diske yazilir. */
#define TORE_PROFILE_UMAY   (KUT_DISK_READ | KUT_DISK_WRITE | KUT_MEM_ALLOC | KUT_NET_SEND | KUT_NET_RECV | \
                             KUT_IPC | KUT_EKRAN | KUT_KLAVYE | KUT_SES | KUT_SISTEM)
/* 1.4.8: masaustu uygulamalari (uygulama.c). Disk, ag, IPC, heap YOK:
 * yalniz kendi penceresi, kendi olaylari ve salt-okunur sistem bilgisi.
 * GORUNUM ek olarak KURESEL ayari (tema) degistirebilir. */
#define TORE_PROFILE_SISTEM_UYG  (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM)
#define TORE_PROFILE_GORUNUM_UYG (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_AYAR)
/* 1.4.9: ag sinama programi (yalniz TANI; CFG'yi diskten okur) */
#define TORE_PROFILE_AGSINA_UYG  (KUT_EKRAN | KUT_SISTEM | KUT_DISK_READ | KUT_NET_SEND | KUT_NET_RECV)
/* 1.4.9: TULPAR ag gezgini. Disk YOK (indirilen hicbir sey diske yazilmaz,
 * disk de okunmaz), IPC yok, heap yok (tamponlari .bss'te). */
#define TORE_PROFILE_TULPAR_UYG  (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_NET_SEND | KUT_NET_RECV)
/* 1.4.9: KERVAN paket yoneticisi. Diske DOGRUDAN yazamaz: yalniz cekirdegin
 * imza dogrulayan kur/kaldir cagrilari (KUT_KERVAN). */
#define TORE_PROFILE_KERVAN_UYG  (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_KERVAN | KUT_NET_SEND | KUT_NET_RECV)   /* 1.4.9: agdan indirir */
/* 1.4.9: ULAK/DEDE/TÖRE/TİMUR/KUT pencereleri (en az yetki) */
#define TORE_PROFILE_ULAK_UYG    (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_NET_SEND | KUT_NET_RECV)
#define TORE_PROFILE_DEDE_UYG    (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_SES)
#define TORE_PROFILE_TORE_UYG    (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM)
#define TORE_PROFILE_TIMUR_UYG   (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM)
#define TORE_PROFILE_KUT_UYG     (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM)
/* 1.4.9: ASENA (onarim), ERGENEKON (yeniden kurulum), TAMGA (kimlik; dosya
 * muhru icin yalniz OKUMA) */
#define TORE_PROFILE_ASENA_UYG     (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_KURTARMA)
#define TORE_PROFILE_ERGENEKON_UYG (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_KURTARMA)
#define TORE_PROFILE_TAMGA_UYG     (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM | KUT_DISK_READ)
#define UMAY_YAZMA_KOKU "/KUT/UMAY"
/* 1.4.9: KERVAN paketinden gelen uygulama: imzali olsa da en dar profil */
#define TORE_PROFILE_PAKET_UYG     (KUT_EKRAN | KUT_KLAVYE | KUT_SISTEM)
/* 1.5: ARAYICI — UMAY'in ag arastirmacisi. Ekran/klavye YOK; diske YALNIZ
 * /KUT/UMAY/AG altina yazar (tore_yazma_koku). */
#define TORE_PROFILE_ARAYICI (KUT_NET_SEND | KUT_NET_RECV | KUT_IPC | KUT_DISK_READ | KUT_DISK_WRITE | KUT_MEM_ALLOC)
#define ARAYICI_YAZMA_KOKU "/KUT/UMAY/AG"
#define TORE_PROFILE_SINAMA (KUT_EKRAN | KUT_TANI)   /* ring3 sinama / ELF gosterimi */

void tore_init(void);

/* Bir surece (pid) yetki profili atar. */
int tore_set_capabilities(uint32_t pid, uint32_t capabilities);

/* Surecin belirtilen yetkiye sahip olup olmadigini denetler.
 * 1=izinli, 0=REDDEDILDI (ve reddedilenler loglanir). */
int tore_check(uint32_t pid, kut_capability_t cap, const char *action);

uint32_t tore_get_capabilities(uint32_t pid);
uint32_t tore_denied_count(void);
/* 1.4.9: DISK_WRITE'i bir dizin agacina daraltir ("/KUT/UMAY"). Kok yoksa
 * daraltma yok. dizin_olustur=1: kokun ust dizinleri de olusturulabilir. */
int tore_yazma_koku(uint32_t pid, const char *kok);
int tore_yazma_koku_al(uint32_t pid, char *cikti, int boy);
int tore_yol_yazilabilir(uint32_t pid, const char *yol, int dizin_olustur);
/* 1.4.7: olen gorevin yetki kaydini siler (reap'ten; donen kilit). */
void     tore_gorev_bitti(uint32_t pid);
/* Sinama: dolu yetki kaydi sayisi. */
int      tore_kayit_sayisi(void);
/* Sinama: pid'nin yetki kaydi var mi (1/0). */
int      tore_kayit_var_mi(uint32_t pid);
const char *tore_capability_name(kut_capability_t cap);

#endif
