#ifndef OTUKENOS_LOG_H
#define OTUKENOS_LOG_H

#include <stdint.h>

/* otukenOS cekirdek gunlukleme (logging) altyapisi.
 * - Seviye tabanli filtreleme
 * - Halka tampon (ring buffer): son N kayit bellekte tutulur, boylece
 *   bir cokme aninda "az once ne oldu" gorulebilir
 * - Alt sistem etiketi (BITIG, ULAK, TIMUR ...) */

typedef enum {
    LOG_HATA   = 0,  /* kritik hata */
    LOG_UYARI  = 1,
    LOG_BILGI  = 2,
    LOG_AYIKLA = 3   /* ayrintili hata ayiklama */
} log_level_t;

#define LOG_MAX_LINE     96
#define LOG_RING_ENTRIES 64

typedef struct {
    log_level_t level;
    char subsystem[12];
    char message[LOG_MAX_LINE];
    uint64_t tick;
} log_entry_t;

void log_init(void);

/* Bu seviyeden DAHA AYRINTILI kayitlar yok sayilir. */
void log_set_level(log_level_t max_level);
log_level_t log_get_level(void);

void log_write(log_level_t level, const char *subsystem, const char *message);
/* Mesaj + tek bir sayisal deger (en sik ihtiyac duyulan bicim). */
void log_write_num(log_level_t level, const char *subsystem,
                    const char *message, uint64_t value);

int  log_entry_count(void);
const log_entry_t *log_get_entry(int index_from_newest);
const char *log_level_name(log_level_t level);

uint32_t log_count_by_level(log_level_t level);

#endif
