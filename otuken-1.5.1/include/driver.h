#ifndef OTUKENOS_DRIVER_H
#define OTUKENOS_DRIVER_H

#include <stdint.h>

#define DRIVER_MAX          16
#define DRIVER_NAME_LEN     24

typedef enum {
    DRIVER_CLASS_INPUT   = 0,
    DRIVER_CLASS_STORAGE = 1,
    DRIVER_CLASS_TIMER   = 2,
    DRIVER_CLASS_DISPLAY = 3,
    DRIVER_CLASS_OTHER   = 4
} driver_class_t;

typedef enum {
    DRIVER_STATE_REGISTERED = 0,  /* kayitli ama henuz baslatilmadi */
    DRIVER_STATE_ACTIVE     = 1,  /* init basarili, calisiyor */
    DRIVER_STATE_FAILED     = 2   /* init basarisiz */
} driver_state_t;

typedef struct driver {
    char name[DRIVER_NAME_LEN];
    driver_class_t klass;
    /* Donanimi baslatir. 0=basari, !=0 hata. Cekirdek, ayni surucuyu
     * iki kez baslatmaz (durum takibi yapilir). */
    int (*init)(void);
    /* Istege bagli: donanimin gercekten var olup olmadigini sinar.
     * 0 dondurursa surucu init edilmez (donanim yok kabul edilir).
     * NULL ise "her zaman var" varsayilir. */
    int (*probe)(void);
    driver_state_t state;
} driver_t;

void driver_manager_init(void);

/* Surucuyu kaydeder (henuz baslatmaz). Basari:0, hata:-1 (tablo dolu). */
int driver_register(const char *name, driver_class_t klass,
                     int (*init_fn)(void), int (*probe_fn)(void));

/* Kayitli TUM surucular icin probe+init calistirir. Basariyla
 * baslatilan surucu sayisini dondurur. */
int driver_load_all(void);

int driver_count(void);
const driver_t *driver_get(int index);
const char *driver_class_name(driver_class_t k);
const char *driver_state_name(driver_state_t s);

#endif
