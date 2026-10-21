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
} ulgen_yuzey_t;

void ulgen_init(void);

/* Yeni bir yuzey (pencere) olusturur; kimlik (>=0) doner. */
int  ulgen_yuzey_olustur(const char *baslik, int x, int y, int w, int h);
int  ulgen_yuzey_yoket(int id);

/* Yuzeyin KENDI tamponuna cizim (ekran koordinati DEGIL, yuzey-yerel) */
void ulgen_doldur(int id, uint32_t renk);
void ulgen_piksel(int id, int x, int y, uint32_t renk);
void ulgen_dikdortgen(int id, int x, int y, int w, int h, uint32_t renk);
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

void ulgen_olay_it(int id, uint32_t tur, uint32_t tus, int x, int y);
int  ulgen_olay_cek(int id, ulgen_olay_t *cikti);
int  ulgen_noktadaki_yuzey(int x, int y);
int  ulgen_kapat_dugmesi_mi(int id, int x, int y);
#endif
