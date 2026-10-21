#include "servis.h"
#include "thread.h"
#include "sched.h"
#include "timer.h"
#include "log.h"
#include "serial.h"

static servis_t servisler[SERVIS_MAX];
static int servis_count = 0;

static int name_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

static void copy_name(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

const char *servis_durum_adi(servis_durum_t d) {
    switch (d) {
        case SERVIS_DURDU:      return "DURDU";
        case SERVIS_BASLIYOR:   return "BASLIYOR";
        case SERVIS_CALISIYOR:  return "CALISIYOR";
        case SERVIS_COKTU:      return "COKTU";
        default:                return "DEVRE_DISI";
    }
}

void servis_init(void) {
    for (int i = 0; i < SERVIS_MAX; i++) servisler[i].used = 0;
    servis_count = 0;
    log_write(LOG_BILGI, "SERVIS", "servis yoneticisi (init) baslatildi");
}

int servis_tanimla(const char *name, const char *depends_on,
                    void (*entry)(void), uint32_t max_restarts) {
    if (servis_count >= SERVIS_MAX) {
        log_write(LOG_HATA, "SERVIS", "servis tablosu dolu");
        return -1;
    }
    servis_t *s = &servisler[servis_count];
    s->used = 1;
    copy_name(s->name, name, SERVIS_NAME_LEN);
    copy_name(s->depends_on, depends_on ? depends_on : "", SERVIS_NAME_LEN);
    s->entry = entry;
    s->durum = SERVIS_DURDU;
    s->restart_count = 0;
    s->max_restarts = max_restarts;
    s->heartbeat = 0;
    s->last_seen_heartbeat = 0;

    serial_write("SERVIS: tanimlandi '");
    serial_write(s->name);
    if (s->depends_on[0]) {
        serial_write("' (bagimlilik: ");
        serial_write(s->depends_on);
        serial_write(")");
    } else {
        serial_write("'");
    }
    serial_write("\n");
    return servis_count++;
}

int servis_bul(const char *name) {
    for (int i = 0; i < servis_count; i++)
        if (servisler[i].used && name_eq(servisler[i].name, name)) return i;
    return -1;
}

static int servis_baslat_tek(int id) {
    servis_t *s = &servisler[id];
    if (!s->used || !s->entry) return -1;
    if (s->durum == SERVIS_CALISIYOR) return 0;

    s->durum = SERVIS_BASLIYOR;
    task_t *t = thread_create(s->name, s->entry);
    if (!t) {
        s->durum = SERVIS_COKTU;
        serial_write("SERVIS: '");
        serial_write(s->name);
        serial_write("' BASLATILAMADI (gorev olusturulamadi)\n");
        return -1;
    }
    s->durum = SERVIS_CALISIYOR;
    s->started_tick = timer_get_ticks();
    s->last_seen_heartbeat = s->heartbeat;

    serial_write("SERVIS: '");
    serial_write(s->name);
    serial_write("' CALISIYOR\n");
    return 0;
}

int servis_hepsini_baslat(void) {
    int started = 0;
    /* Bagimlilik sirasi: en fazla SERVIS_MAX tur donerek, bagimliligi
     * karsilanan servisleri baslat. Bu basit yaklasim dongusel
     * bagimlilikta sonsuza girmez (tur sayisi sinirli). */
    for (int pass = 0; pass < SERVIS_MAX; pass++) {
        int progress = 0;
        for (int i = 0; i < servis_count; i++) {
            servis_t *s = &servisler[i];
            if (!s->used || s->durum == SERVIS_CALISIYOR) continue;
            if (s->durum == SERVIS_DEVRE_DISI) continue;

            if (s->depends_on[0]) {
                int dep = servis_bul(s->depends_on);
                if (dep < 0) {
                    serial_write("SERVIS: '");
                    serial_write(s->name);
                    serial_write("' bagimliligi BULUNAMADI, devre disi\n");
                    s->durum = SERVIS_DEVRE_DISI;
                    continue;
                }
                if (servisler[dep].durum != SERVIS_CALISIYOR) continue;  /* sonraki tur */
            }
            if (servis_baslat_tek(i) == 0) { started++; progress++; }
        }
        if (!progress) break;
    }
    serial_write("SERVIS: baslatma tamamlandi, calisan servis sayisi=");
    serial_write_hex((uint32_t)started);
    serial_write("\n");
    return started;
}

void servis_nabiz(int id) {
    if (id < 0 || id >= servis_count) return;
    servisler[id].heartbeat++;
}

int servis_izle(void) {
    int restarted = 0;
    for (int i = 0; i < servis_count; i++) {
        servis_t *s = &servisler[i];
        if (!s->used || s->durum != SERVIS_CALISIYOR) continue;

        /* Nabiz ilerlemediyse servis takilmis/cokmus demektir */
        if (s->heartbeat == s->last_seen_heartbeat) {
            serial_write("SERVIS: '");
            serial_write(s->name);
            serial_write("' NABIZ YOK -> cokmus sayiliyor\n");
            s->durum = SERVIS_COKTU;

            if (s->restart_count < s->max_restarts) {
                s->restart_count++;
                serial_write("SERVIS: '");
                serial_write(s->name);
                serial_write("' YENIDEN BASLATILIYOR (deneme ");
                serial_write_hex(s->restart_count);
                serial_write(")\n");
                if (servis_baslat_tek(i) == 0) restarted++;
            } else {
                serial_write("SERVIS: '");
                serial_write(s->name);
                serial_write("' yeniden baslatma siniri asildi -> DEVRE DISI\n");
                s->durum = SERVIS_DEVRE_DISI;
                log_write(LOG_HATA, "SERVIS", "servis kalici olarak devre disi");
            }
        } else {
            s->last_seen_heartbeat = s->heartbeat;
        }
    }
    return restarted;
}

const servis_t *servis_al(int id) {
    if (id < 0 || id >= servis_count) return 0;
    return &servisler[id];
}

int servis_sayisi(void) { return servis_count; }
