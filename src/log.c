#include "log.h"
#include "serial.h"
#include "timer.h"

/* SMP adim 6: gunluk halkasi ve sayaclari; her baglamdan yazilir. Kisa islemler -> ozyinelemeli donen kilit. */
#include "spinlock.h"
static ozspin_t log_kilit = OZSPIN_BASLANGIC;

static log_entry_t ring[LOG_RING_ENTRIES];
static int ring_head = 0;      /* bir sonraki yazilacak yer */
static int ring_count = 0;     /* toplam gecerli kayit (max RING_ENTRIES) */
static log_level_t current_level = LOG_BILGI;
static uint32_t level_counts[4];

static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void log_init(void) {
    ring_head = 0;
    ring_count = 0;
    current_level = LOG_BILGI;
    for (int i = 0; i < 4; i++) level_counts[i] = 0;
    serial_write("log: cekirdek gunlukleyici baslatildi (seviye=BILGI, "
                 "halka tampon=64 kayit)\n");
}

void log_set_level(log_level_t max_level) {
    current_level = max_level;
    serial_write("log: gunlukleme seviyesi degistirildi -> ");
    serial_write(log_level_name(max_level));
    serial_write("\n");
}

log_level_t log_get_level(void) { return current_level; }

const char *log_level_name(log_level_t level) {
    switch (level) {
        case LOG_HATA:   return "HATA";
        case LOG_UYARI:  return "UYARI";
        case LOG_BILGI:  return "BILGI";
        default:         return "AYIKLA";
    }
}

static void emit_serial(const log_entry_t *e) {
    serial_write("[");
    serial_write(log_level_name(e->level));
    serial_write("][");
    serial_write(e->subsystem);
    serial_write("] ");
    serial_write(e->message);
    serial_write("\n");
}

static void log_write__ic(log_level_t level, const char *subsystem, const char *message) {
    if (level > current_level) return; /* seviye filtresi */

    log_entry_t *e = &ring[ring_head];
    e->level = level;
    str_copy_n(e->subsystem, subsystem, sizeof(e->subsystem));
    str_copy_n(e->message, message, sizeof(e->message));
    e->tick = timer_get_ticks();

    ring_head = (ring_head + 1) % LOG_RING_ENTRIES;
    if (ring_count < LOG_RING_ENTRIES) ring_count++;
    if ((int)level >= 0 && (int)level < 4) level_counts[level]++;

    emit_serial(e);
}

static void log_write_num__ic(log_level_t level, const char *subsystem,
                    const char *message, uint64_t value) {
    if (level > current_level) return;

    char buf[LOG_MAX_LINE];
    int p = 0;
    while (message[p] && p < LOG_MAX_LINE - 20) { buf[p] = message[p]; p++; }
    buf[p++] = '=';
    buf[p++] = '0'; buf[p++] = 'x';

    static const char hex[] = "0123456789ABCDEF";
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nib = (uint8_t)((value >> shift) & 0xF);
        if (nib || started || shift == 0) {
            buf[p++] = hex[nib];
            started = 1;
        }
    }
    buf[p] = 0;

    log_write(level, subsystem, buf);
}

int log_entry_count(void) { return ring_count; }

/* index 0 = EN YENI kayit */
const log_entry_t *log_get_entry(int index_from_newest) {
    if (index_from_newest < 0 || index_from_newest >= ring_count) return 0;
    int idx = ring_head - 1 - index_from_newest;
    while (idx < 0) idx += LOG_RING_ENTRIES;
    return &ring[idx];
}

uint32_t log_count_by_level(log_level_t level) {
    if ((int)level < 0 || (int)level >= 4) return 0;
    return level_counts[level];
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
void log_write(log_level_t level, const char *subsystem, const char *message) {
    ozspin_al(&log_kilit);
    log_write__ic(level, subsystem, message);
    ozspin_birak(&log_kilit);
}

void log_write_num(log_level_t level, const char *subsystem, const char *message, uint64_t value) {
    ozspin_al(&log_kilit);
    log_write_num__ic(level, subsystem, message, value);
    ozspin_birak(&log_kilit);
}
