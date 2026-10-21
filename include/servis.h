#ifndef OTUKENOS_SERVIS_H
#define OTUKENOS_SERVIS_H

#include <stdint.h>

/* --- Asama 32: ÖTÜKEN Servis Sistemi (init / service manager) ---
 * Sistem hizmetlerini tanimlar, bagimlilik sirasina gore baslatir,
 * durumlarini izler ve cokerse YENIDEN BASLATIR. */

#define SERVIS_MAX       8
#define SERVIS_NAME_LEN 20

typedef enum {
    SERVIS_DURDU     = 0,
    SERVIS_BASLIYOR  = 1,
    SERVIS_CALISIYOR = 2,
    SERVIS_COKTU     = 3,
    SERVIS_DEVRE_DISI= 4
} servis_durum_t;

typedef struct {
    int      used;
    char     name[SERVIS_NAME_LEN];
    char     depends_on[SERVIS_NAME_LEN];  /* bos ise bagimlilik yok */
    void   (*entry)(void);
    servis_durum_t durum;
    uint32_t restart_count;
    uint32_t max_restarts;
    uint64_t started_tick;
    uint32_t heartbeat;                    /* servis kendi ilerlemesini bildirir */
    uint32_t last_seen_heartbeat;
} servis_t;

void servis_init(void);

/* Servis tanimlar (henuz baslatmaz). Kimlik (>=0) doner. */
int  servis_tanimla(const char *name, const char *depends_on,
                     void (*entry)(void), uint32_t max_restarts);

/* Bagimlilik sirasina gore TUM servisleri baslatir.
 * Baslatilan servis sayisini dondurur. */
int  servis_hepsini_baslat(void);

/* Servisin "yasadigini" bildirmesi icin (izleme). */
void servis_nabiz(int id);

/* Coken/takilan servisleri tespit edip yeniden baslatir.
 * Periyodik olarak (init dongusu) cagirilmalidir. */
int  servis_izle(void);

int  servis_bul(const char *name);
const servis_t *servis_al(int id);
int  servis_sayisi(void);
const char *servis_durum_adi(servis_durum_t d);

#endif
