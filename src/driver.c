#include "driver.h"
#include "serial.h"

static driver_t drivers[DRIVER_MAX];
static int registered_count = 0;

void driver_manager_init(void) {
    registered_count = 0;
    serial_write("driver: yonetici baslatildi\n");
}

int driver_register(const char *name, driver_class_t klass,
                     int (*init_fn)(void), int (*probe_fn)(void)) {
    if (registered_count >= DRIVER_MAX) {
        serial_write("driver: HATA - surucu tablosu dolu\n");
        return -1;
    }
    driver_t *d = &drivers[registered_count];
    int i = 0;
    while (name[i] && i < DRIVER_NAME_LEN - 1) { d->name[i] = name[i]; i++; }
    d->name[i] = 0;
    d->klass = klass;
    d->init = init_fn;
    d->probe = probe_fn;
    d->state = DRIVER_STATE_REGISTERED;
    registered_count++;

    serial_write("driver: kaydedildi '");
    serial_write(d->name);
    serial_write("' sinif=");
    serial_write(driver_class_name(klass));
    serial_write("\n");
    return 0;
}

int driver_load_all(void) {
    int activated = 0;
    for (int i = 0; i < registered_count; i++) {
        driver_t *d = &drivers[i];
        if (d->state != DRIVER_STATE_REGISTERED) continue;

        if (d->probe && d->probe() == 0) {
            d->state = DRIVER_STATE_FAILED;
            serial_write("driver: '");
            serial_write(d->name);
            serial_write("' donanimi bulunamadi (probe basarisiz)\n");
            continue;
        }
        if (!d->init) { d->state = DRIVER_STATE_FAILED; continue; }

        if (d->init() == 0) {
            d->state = DRIVER_STATE_ACTIVE;
            activated++;
            serial_write("driver: '");
            serial_write(d->name);
            serial_write("' AKTIF\n");
        } else {
            d->state = DRIVER_STATE_FAILED;
            serial_write("driver: '");
            serial_write(d->name);
            serial_write("' init BASARISIZ\n");
        }
    }
    return activated;
}

int driver_count(void) { return registered_count; }

const driver_t *driver_get(int index) {
    if (index < 0 || index >= registered_count) return 0;
    return &drivers[index];
}

const char *driver_class_name(driver_class_t k) {
    switch (k) {
        case DRIVER_CLASS_INPUT:   return "GIRIS";
        case DRIVER_CLASS_STORAGE: return "DEPOLAMA";
        case DRIVER_CLASS_TIMER:   return "ZAMANLAYICI";
        case DRIVER_CLASS_DISPLAY: return "EKRAN";
        default:                   return "DIGER";
    }
}

const char *driver_state_name(driver_state_t s) {
    switch (s) {
        case DRIVER_STATE_REGISTERED: return "KAYITLI";
        case DRIVER_STATE_ACTIVE:     return "AKTIF";
        default:                      return "BASARISIZ";
    }
}
