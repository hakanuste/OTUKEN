#ifndef OTUKENOS_ULGEN_H
#define OTUKENOS_ULGEN_H

#include <stdint.h>

/* --- Asama 33: ÜLGEN Grafik Sistemi (compositor) ---
 * Onceki GUI dogrudan ekrana ciziyordu; her degisiklikte TUM ekran
 * yeniden ciziliyordu. Compositor mimarisi:
 *   - Her pencerenin KENDI arka tamponu (off-screen buffer) vardir
 *   - Uygulamalar yalnizca kendi tamponuna cizer (ekrani bilmez)
 *   - Compositor tamponlari z-sirasina gore BIRLESTIRIR
 *   - Hasar takibi (damage): sadece DEGISEN bolge yeniden birlestirilir
 */

#define ULGEN_MAX_YUZEY   8
#define ULGEN_BASLIK_LEN 32

typedef struct {
    int      x, y;
    int      w, h;
} ulgen_dikdortgen_t;

/* --- Olay kuyrugu ---
 * Her yuzeyin kendi halka kuyrugu vardir: cekirdek olay iter, Ring3
 * uygulamasi ceker. Olay yolu olmadan UMAY penceresi kapatilamiyordu. */
#define ULGEN_OLAY_KUYRUK 16
#define ULGEN_OLAY_YOK    0
#define ULGEN_OLAY_TUS    1
#define ULGEN_OLAY_TIK    2
#define ULGEN_OLAY_KAPAT  3

typedef struct {
    uint32_t tur, tus;
    int32_t  x, y;
} ulgen_olay_t;

typedef struct {
    int       used;
    int       gorunur;
    int       x, y, w, h;
    uint32_t *tampon;              /* w*h piksel, sahibi bu yuzey */
    char      baslik[ULGEN_BASLIK_LEN];
    int       z;                   /* buyuk = onde */
    int       kirli;               /* yeniden birlestirilmeli mi */
    ulgen_olay_t olay[ULGEN_OLAY_KUYRUK];
    int       olay_bas, olay_son;
    uint32_t  sahip;               /* 1.4.7: olusturan gorevin kimligi */
    /* 1.4.8 SUNUM: uygulama yuzeyleri (elle_sun) 'tampon'a cizer; ekrana
     * yalniz ulgen_sun() ile 'sunulan'a kopyalanan TAM kare cikar. Eskiden
     * birlestirici yarim cizilmis tamponu gosteriyordu (UMAY yan sutunu
     * eksik kareler). Cekirdek yuzeyleri (elle_sun=0) tampondan gosterilir. */
    uint32_t *sunulan;
    int       elle_sun;
} ulgen_yuzey_t;

void ulgen_init(void);

/* Yeni bir yuzey (pencere) olusturur; kimlik (>=0) doner. */
int  ulgen_yuzey_olustur(const char *baslik, int x, int y, int w, int h);
int  ulgen_yuzey_yoket(int id);

/* Yuzeyin KENDI tamponuna cizim (ekran koordinati DEGIL, yuzey-yerel) */
void ulgen_doldur(int id, uint32_t renk);
void ulgen_piksel(int id, int x, int y, uint32_t renk);
void ulgen_dikdortgen(int id, int x, int y, int w, int h, uint32_t renk);
/* bg == ULGEN_SAYDAM: zemin boyanmaz, yazi yuzeydeki piksellerle karisir. */
#define ULGEN_SAYDAM 0xFF000000u
void ulgen_yazi(int id, int x, int y, const char *s, uint32_t fg, uint32_t bg);

void ulgen_tasi(int id, int x, int y);
void ulgen_one_getir(int id);
void ulgen_gorunurluk(int id, int gorunur);

/* Kirli yuzeyleri ekrana birlestirir. Birlestirilen yuzey sayisini
 * dondurur (0 = degisiklik yok, is yapilmadi). */
int  ulgen_birlestir(void);

/* Tum ekrani zorla yeniden birlestir. */
void ulgen_tam_yenile(void);

int  ulgen_yuzey_sayisi(void);
const ulgen_yuzey_t *ulgen_yuzey_al(int id);
uint32_t ulgen_birlestirme_sayisi(void);

/* Yuzeyin ekrandaki dis cercevesi (kenar dahil); gecersizse -1 */
int  ulgen_cerceve(int id, int *x, int *y, int *w, int *h);
#define ULGEN_BASLIK_YUK 20
/* Birlestirici icin (gorsel_kilit tutulurken): yuzeyi mevcut kirpmayla cizer */
void ulgen_yuzey_ciz_ic(int id);
/* GUI pencereleri ve ULGEN yuzeyleri icin ORTAK z-sayaci */
int  ulgen_z_ver(void);
/* Yuzeyi "elle sun" kipine alir (uygulama yuzeyleri; SYS_PENCERE_AC). */
int  ulgen_elle_sun(int id);
/* Uygulamanin bitirdigi kareyi sunar: tampon -> sunulan, yuzey kirli. */
int  ulgen_sun(int id);

void ulgen_olay_it(int id, uint32_t tur, uint32_t tus, int x, int y);
int  ulgen_olay_cek(int id, ulgen_olay_t *cikti);
/* 1.4.7 sahiplik: yuzeyin sahibini yazar (0: gecerli, -1: yok). */
int  ulgen_yuzey_sahibi(int id, uint32_t *sahip);
/* Reap'ten: olen gorevin yuzeylerini yok eder (yok edilen sayi). Gorev
 * baglaminda cagrilir (gorsel_kilit uyuyan kilittir). */
int  ulgen_gorev_bitti(uint32_t gorev_id);
int  ulgen_noktadaki_yuzey(int x, int y);
int  ulgen_kapat_dugmesi_mi(int id, int x, int y);
/* 1.4.9: klavye tuslarini alan yuzey (gorunur, en ustte); yoksa -1 */
int  ulgen_odak_yuzey(void);
#endif
