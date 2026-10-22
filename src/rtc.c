/* 1.4.9 — gerçek saat (bkz. include/rtc.h) */
#include "rtc.h"
#include "io.h"
#include "cpu.h"
#include "timer.h"
#include "serial.h"

static uint8_t cmos_oku(uint8_t r) { outb(0x70, r); return inb(0x71); }

int rtc_oku(sys_zaman_t *z) {
    uint8_t s[7], t[7];
    uint64_t f = irq_kaydet_kapat();
    int deneme = 0;
    do {
        while ((cmos_oku(0x0A) & 0x80) && deneme++ < 100000) ;      /* güncelleme sürüyor */
        s[0] = cmos_oku(0x00); s[1] = cmos_oku(0x02); s[2] = cmos_oku(0x04);
        s[3] = cmos_oku(0x07); s[4] = cmos_oku(0x08); s[5] = cmos_oku(0x09); s[6] = cmos_oku(0x32);
        while ((cmos_oku(0x0A) & 0x80) && deneme++ < 100000) ;
        t[0] = cmos_oku(0x00); t[1] = cmos_oku(0x02); t[2] = cmos_oku(0x04);
        t[3] = cmos_oku(0x07); t[4] = cmos_oku(0x08); t[5] = cmos_oku(0x09); t[6] = cmos_oku(0x32);
    } while ((s[0] != t[0] || s[1] != t[1] || s[2] != t[2] || s[3] != t[3] || s[4] != t[4] ||
              s[5] != t[5]) && deneme++ < 100000);
    uint8_t b = cmos_oku(0x0B);
    irq_geri_yukle(f);
    int ikili = b & 0x04, saat24 = b & 0x02;
    int pm = s[2] & 0x80;
    s[2] &= 0x7F;
    if (!ikili) for (int i = 0; i < 7; i++) s[i] = (uint8_t)((s[i] & 0x0F) + (s[i] >> 4) * 10);
    if (!saat24) { if (pm && s[2] < 12) s[2] += 12; else if (!pm && s[2] == 12) s[2] = 0; }
    z->yil = (s[6] >= 19 && s[6] <= 22) ? (uint32_t)s[6] * 100u + s[5] : 2000u + s[5];
    z->ay = s[4]; z->gun = s[3]; z->saat = s[2]; z->dakika = s[1]; z->saniye = s[0];
    if (z->ay < 1 || z->ay > 12 || z->gun < 1 || z->gun > 31 || z->saat > 23 || z->dakika > 59 ||
        z->saniye > 60) return -1;
    return 0;
}

static volatile int64_t taban_epoch = -1;
static volatile uint64_t taban_tik;
static volatile uint64_t son_esitleme;

static void esitle(void) {
    sys_zaman_t z;
    if (rtc_oku(&z) != 0 || z.yil < 2000 || z.yil > 2200) return;
    taban_tik = timer_get_ticks();
    __atomic_store_n(&taban_epoch, zaman_epoch_yap(&z), __ATOMIC_RELEASE);
    son_esitleme = taban_tik;
}

void zaman_baslat(void) {
    esitle();
    if (taban_epoch < 0) { serial_write("ZAMAN: RTC okunamadi, ust cubuk calisma suresini gosterecek\n"); return; }
    sys_zaman_t z;
    zaman_coz(taban_epoch, 0, &z, 0);
    serial_write("ZAMAN: RTC (UTC) ");
    serial_write_hex(z.yil); serial_write("-"); serial_write_hex(z.ay); serial_write("-");
    serial_write_hex(z.gun); serial_write(" "); serial_write_hex(z.saat); serial_write(":");
    serial_write_hex(z.dakika); serial_write("\n");
}

void zaman_esitle_gerekirse(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    if (taban_epoch >= 0 && timer_get_ticks() - son_esitleme >= 3600ull * hz) esitle();
}

int zaman_var(void) { return __atomic_load_n(&taban_epoch, __ATOMIC_ACQUIRE) >= 0; }

int64_t zaman_epoch(void) {
    int64_t e = __atomic_load_n(&taban_epoch, __ATOMIC_ACQUIRE);
    if (e < 0) return -1;
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    return e + (int64_t)((timer_get_ticks() - taban_tik) / hz);
}
