/* 1.4.9 — takvim hesabi (saf; cekirdek ve ana makine sinamasi ortak) */
#include "rtc.h"

/* H. Hinnant: 1970-01-01'den gün <-> takvim */
static int64_t gun_yap(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t zaman_epoch_yap(const sys_zaman_t *z) {
    return gun_yap(z->yil, z->ay, z->gun) * 86400 + z->saat * 3600 + z->dakika * 60 + z->saniye;
}

void zaman_coz(int64_t e, int dilim_dk, sys_zaman_t *z, int *hafta_gunu) {
    e += (int64_t)dilim_dk * 60;
    int64_t gun = e >= 0 ? e / 86400 : -((-e + 86399) / 86400);
    int64_t sn = e - gun * 86400;
    if (hafta_gunu) *hafta_gunu = (int)(((gun % 7) + 7 + 3) % 7);    /* 1970-01-01 Perşembe */
    int64_t g = gun + 719468;
    int64_t era = (g >= 0 ? g : g - 146096) / 146097;
    int64_t doe = g - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    int64_t y = yoe + era * 400 + (m <= 2);
    z->yil = (uint32_t)y; z->ay = (uint32_t)m; z->gun = (uint32_t)d;
    z->saat = (uint32_t)(sn / 3600); z->dakika = (uint32_t)(sn / 60 % 60); z->saniye = (uint32_t)(sn % 60);
}

