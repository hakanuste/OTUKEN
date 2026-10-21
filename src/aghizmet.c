#include "aghizmet.h"
#include "socket.h"
#include "ulak.h"
#include "timer.h"
#include "pmm.h"
#include "sched.h"
#include "log.h"
#include "serial.h"

static int sok_yanki = -1;
static int sok_zaman = -1;
static int sok_durum = -1;
static uint32_t istek_sayisi = 0;
static uint32_t yanit_sayisi = 0;

void aghizmet_init(void) {
    sok_yanki = socket_create(SOCK_UDP);
    sok_zaman = socket_create(SOCK_UDP);
    sok_durum = socket_create(SOCK_UDP);

    if (sok_yanki >= 0) socket_bind(sok_yanki, AGHIZMET_YANKI_PORT);
    if (sok_zaman >= 0) socket_bind(sok_zaman, AGHIZMET_ZAMAN_PORT);
    if (sok_durum >= 0) socket_bind(sok_durum, AGHIZMET_DURUM_PORT);

    istek_sayisi = yanit_sayisi = 0;

    serial_write("AGHIZMET: hizmetler dinlemede - YANKI(7) ZAMAN(13) DURUM(1900)\n");
    log_write(LOG_BILGI, "AGHIZMET", "ag hizmetleri baslatildi");
}

static void say_ekle(char *buf, int *p, uint64_t v) {
    char tmp[24];
    int i = 0;
    if (v == 0) { buf[(*p)++] = '0'; return; }
    while (v > 0 && i < 23) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) buf[(*p)++] = tmp[--i];
}

static void metin_ekle(char *buf, int *p, const char *s) {
    while (*s) buf[(*p)++] = *s++;
}

int aghizmet_isle(void) {
    uint8_t gelen[256];
    int islenen = 0;

    /* --- YANKI hizmeti: aldigini oldugu gibi geri gonder --- */
    if (sok_yanki >= 0) {
        uint8_t kaynak_ip[4]; uint16_t kaynak_port = 0;
        int n = socket_recvfrom(sok_yanki, gelen, sizeof(gelen), kaynak_ip, &kaynak_port);
        if (n > 0) {
            istek_sayisi++;
            serial_write("AGHIZMET: YANKI istegi (");
            serial_write_hex((uint32_t)n);
            serial_write(" bayt) -> gonderene geri yollaniyor port=");
            serial_write_hex(kaynak_port);
            serial_write("\n");
            /* DUZELTME: yanit, isteği GONDEREN adrese ve PORTA gitmeli.
             * Sabit bir adres/port kullanmak yanlisti - istemci her
             * zaman farkli bir gecici porttan gelir. */
            socket_sendto(sok_yanki, kaynak_ip, kaynak_port, gelen, (uint16_t)n);
            yanit_sayisi++;
            islenen++;
        }
    }

    /* --- ZAMAN hizmeti: calisma suresini bildir --- */
    if (sok_zaman >= 0) {
        uint8_t z_ip[4]; uint16_t z_port = 0;
        int n = socket_recvfrom(sok_zaman, gelen, sizeof(gelen), z_ip, &z_port);
        if (n > 0) {
            istek_sayisi++;
            char yanit[128];
            int p = 0;
            metin_ekle(yanit, &p, "OTUKEN ZAMAN: tik=");
            say_ekle(yanit, &p, timer_get_ticks());
            metin_ekle(yanit, &p, " (50Hz, ~");
            say_ekle(yanit, &p, timer_get_ticks() / 50);
            metin_ekle(yanit, &p, " saniye)\n");
            socket_sendto(sok_zaman, z_ip, z_port, (const uint8_t *)yanit, (uint16_t)p);
            yanit_sayisi++;
            islenen++;
            serial_write("AGHIZMET: ZAMAN istegi yanitlandi\n");
        }
    }

    /* --- DURUM hizmeti: sistem ozeti --- */
    if (sok_durum >= 0) {
        uint8_t d_ip[4]; uint16_t d_port = 0;
        int n = socket_recvfrom(sok_durum, gelen, sizeof(gelen), d_ip, &d_port);
        if (n > 0) {
            istek_sayisi++;
            char yanit[256];
            int p = 0;
            metin_ekle(yanit, &p, "OTUKEN DURUM\n  bos cerceve: ");
            say_ekle(yanit, &p, pmm_free_frames());
            metin_ekle(yanit, &p, "\n  toplam bellek: ");
            say_ekle(yanit, &p, pmm_total_bytes() / 1024);
            metin_ekle(yanit, &p, " KB\n  gorev sayisi: ");
            say_ekle(yanit, &p, scheduler_task_count());
            metin_ekle(yanit, &p, "\n  calisma tik: ");
            say_ekle(yanit, &p, timer_get_ticks());
            metin_ekle(yanit, &p, "\n");
            socket_sendto(sok_durum, d_ip, d_port, (const uint8_t *)yanit, (uint16_t)p);
            yanit_sayisi++;
            islenen++;
            serial_write("AGHIZMET: DURUM istegi yanitlandi\n");
        }
    }

    return islenen;
}

uint32_t aghizmet_istek_sayisi(void) { return istek_sayisi; }
uint32_t aghizmet_yanit_sayisi(void) { return yanit_sayisi; }
