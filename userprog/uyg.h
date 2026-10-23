/* 1.4.8 — Masaüstü uygulamaları için ek kitaplık (ulib_uyg.c).
 * ulib.c'ye DOKUNULMADI: kabuk ikilisi (shell.elf) bayt bayt aynı kalır. */
#ifndef OTUKEN_UYG_H
#define OTUKEN_UYG_H
#include "ulib.h"

#define SYS_PEN_SUN   30
#define SYS_PEN_OLAY  31
#define SYS_TEMA      34
#define SYS_TEMA_BIL  36
#define SYSBIL_TIK_HZ 4

#define OLAY_YOK   0
#define OLAY_TUS   1
#define OLAY_TIK   2
#define OLAY_KAPAT 3

typedef struct {
    uint32_u tur;      /* OLAY_* */
    uint32_u tus;      /* Unicode kod noktası */
    int      x, y;     /* TIK: pencere içi konum */
} olay_u;

int  pencere_sun(int id);                   /* çizilen kareyi ekrana bas */
int  pencere_olay(int id, olay_u *o);       /* 1 olay var, 0 yok */
int  tema_bil(int sira, char ad[32]);       /* sira=-1: şimdiki; 0/-1 */
int  tema_degistir(const char *ad);         /* 0/-1 */
/* --- 1.4.9: ağ (ULAK). Yetki: NET_SEND / NET_RECV --- */
#define SYS_AG_COZ     37
#define SYS_AG_BAGLAN  38
#define SYS_AG_GONDER  39
#define SYS_AG_AL      40
#define SYS_AG_KAPAT   41
#define SYS_AG_BILGI   42
#define AG_SON        (-2)    /* ag_al: karşı taraf kapattı, veri bitti */
typedef struct {
    uint8_u  ip[4], maske[4], gecit[4], dns[4];
    uint8_u  mac[6];
    uint8_u  bagli, yapilandi;
    uint32_u rx_paket, tx_paket, rx_tcp, acik_tcp;
} ag_bilgi_u;
int ag_coz(const char *ad, uint8_u ip[4]);            /* 0 / -1 (bloklar) */
int ag_baglan(const uint8_u ip[4], int port);          /* tutamak / -1 (bloklar) */
int ag_gonder(int t, const void *v, int n);            /* kabul edilen / -1 */
int ag_gonder_tum(int t, const void *v, int n, long zaman_tik); /* hepsi: 0 / -1 */
int ag_al(int t, void *tampon, int azami);             /* n / 0 yok / AG_SON / -1 */
int ag_kapat(int t);
int ag_bilgi(ag_bilgi_u *b);

void *memmove(void *d, const void *k, size_t_u n);
int   memcmp(const void *a, const void *b, size_t_u n);

/* --- 1.4.9: ULAK / TÖRE / TİMUR / KUT / DEDE KORKUT --- */
#define SYS_SES_NOTA       32
#define SYS_DEDE_KONUS     35
#define SYS_AG_BAGLANTILAR 48
#define SYS_GUVENLIK_BILGI 49
#define SYSBIL_SES_VAR     5
#define SYSBIL_DEDE_MESGUL 6
typedef struct {
    uint8_u  ip[4];
    unsigned short yerel, uzak;
    uint8_u  durum, sahipli, b1, b2;
    uint32_u alinan, gonderilen, yeniden;
} tcp_ozet_u;
#define GB_POLITIKA  0
#define GB_GOREV     1
#define GB_YETKI_ADI 2
#define GB_TIMUR     3
#define GB_KULLANICI 4
#define GB_KUT_BAG   5
typedef union {
    struct { int no; int yetki; char ad[40]; } politika;
    struct { uint32_u id, yetki, uid; uint8_u durum, kayitli, kullanici, b; char ad[16]; } gorev;
    struct { char ad[24]; } yetki_adi;
    struct { uint8_u nx_destek, nx_acik, smep_destek, smep_acik, smap_destek, smap_acik, rng, rtc;
             uint32_u reddedilen, politikasiz, ring3_gorev, kervan_kurulu, cagri_sayisi, wp; } timur;
    struct { uint32_u uid, rol; char ad[16]; char rol_ad[16]; } kullanici;
    struct { uint32_u pid, uid; } bag;
    uint8_u ham[64];
} guvenlik_u;
static inline int guvenlik_bilgi(int tur, int sira, guvenlik_u *g) {
    return (int)syscall(SYS_GUVENLIK_BILGI, tur, sira, (long)g, 0);
}

/* 1.4.9: ASENA, ERGENEKON, TAMGA */
#define SYS_ASENA_DENETLE 50
#define SYS_ASENA_ONAR    51
#define SYS_ERGENEKON     52
#define SYS_TAMGA_BILGI   53
#define SYS_ONAY 0x59414E4FL   /* ASENA onar / ERGENEKON islemleri */
#define ASENA_TUR_SAYISI  8
typedef struct {
    uint32_u sayi[ASENA_TUR_SAYISI];
    uint32_u dosya, dizin, onarilan, bulgu, bos_kume, kume_sayisi, hata, kusak;
} asena_rapor_u;
typedef struct { uint32_u tur, deger; char yol[40]; } asena_bulgu_u;
typedef struct { char ad[13]; uint8_u durum; unsigned short b; uint32_u boy, disk_boy; } dizge_dosya_u;
typedef struct {
    uint8_u  var, gecerli, anahtar_no, b;
    uint32_u dosya, boy, yazilan, ayni, hata;
    char ad[24]; char surum[16]; char neden[64];
} ergenekon_u;
typedef struct {
    uint8_u ozet[32]; uint8_u anahtar[65]; uint8_u gecerli; unsigned short b;
    uint32_u boy; uint8_u bos[24];
} tamga_u;
#define KUT_YETKI_SAYISI 17

/* ortak pencere renkleri (Sistem, KERVAN, ULAK, TÖRE, TİMUR, KUT, DEDE) */
#define R_ARKA    0x14232Bu
#define R_PANO    0x1B313Au
#define R_YAZI    0xE8E2D0u
#define R_SONUK   0x8FA3A8u
#define R_VURGU   0x4FB3A9u
#define R_UYARI   0xE0A060u
#define R_KIRMIZI 0xD9534Fu

/* Sayıyı ondalık yazıya çevirir (en az 'hane' basamak, önü 0). Dönüş: uzunluk */
int  sayi_yaz(char *s, uint64_u v, int hane);
/* dize birleştirme: d'nin sonuna k'yı ekler, d'nin boyu 'boy' */
void dize_ekle(char *d, int boy, const char *k);
#endif
