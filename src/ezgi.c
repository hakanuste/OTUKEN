/* =====================================================================
 * EZGİ — WAV çalar.
 *
 * ÖTÜKEN'in ilk medya oynatıcısı. Diskteki bir RIFF/WAVE dosyasını
 * okur, PCM örneklerini KOPUZ (AC'97) sürücüsüne verir.
 *
 * KAPSAM: yalnız sıkıştırılmamış PCM (format 1), 8 ya da 16 bit,
 * tek ya da iki kanal. Kaynak örnekleme hızı 48 kHz değilse en yakın
 * komşu ile dönüştürülür; tek kanal ise iki kanala çoğaltılır. Bu
 * dönüşüm tamsayıdır, çekirdekte kayan nokta birimi kapalıdır.
 *
 * SINIR: MP3, FLAC, Vorbis YOK. Kap formatı (MP4/MKV) YOK. Çözücü
 * gerektiren her şey ayrı bir iştir; burada yalnız ham PCM çalınır.
 * ===================================================================== */
#include "ezgi.h"
#include "ses.h"
#include "vfs.h"
#include "serial.h"

#define CIKIS_HZ    48000
#define PARCA       2048          /* tek seferde çalınan çerçeve sayısı */

static uint32_t oku32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t oku16(const uint8_t *p) {
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

int ezgi_basligi_coz(const uint8_t *veri, uint32_t boy, ezgi_bilgi_t *bilgi)
{
    if (!veri || !bilgi || boy < 44) return -1;
    if (!(veri[0] == 'R' && veri[1] == 'I' && veri[2] == 'F' && veri[3] == 'F'))
        return -1;
    if (!(veri[8] == 'W' && veri[9] == 'A' && veri[10] == 'V' && veri[11] == 'E'))
        return -1;

    /* Yığın yığın dolaş: "fmt " ve "data" aranır. Bazı yazıcılar araya
     * LIST gibi başka yığınlar koyar; sabit 44 bayt varsaymak yanlış. */
    uint32_t ofs = 12;
    int fmt_bulundu = 0;
    bilgi->veri_ofset = 0;
    bilgi->veri_boyu = 0;

    while (ofs + 8 <= boy) {
        const uint8_t *y = veri + ofs;
        uint32_t uzunluk = oku32(y + 4);
        if (y[0] == 'f' && y[1] == 'm' && y[2] == 't' && y[3] == ' ') {
            if (ofs + 8 + 16 > boy) return -1;
            bilgi->bicim   = oku16(y + 8);
            bilgi->kanal   = oku16(y + 10);
            bilgi->hz      = oku32(y + 12);
            bilgi->bit     = oku16(y + 22);
            fmt_bulundu = 1;
        } else if (y[0] == 'd' && y[1] == 'a' && y[2] == 't' && y[3] == 'a') {
            bilgi->veri_ofset = ofs + 8;
            bilgi->veri_boyu  = uzunluk;
            if (bilgi->veri_ofset + bilgi->veri_boyu > boy)
                bilgi->veri_boyu = boy - bilgi->veri_ofset;
            break;
        }
        ofs += 8 + uzunluk + (uzunluk & 1);   /* yığınlar çift hizalı */
    }

    if (!fmt_bulundu || bilgi->veri_boyu == 0) return -1;
    if (bilgi->bicim != 1) return -2;                 /* sıkıştırılmış */
    if (bilgi->kanal < 1 || bilgi->kanal > 2) return -2;
    if (bilgi->bit != 8 && bilgi->bit != 16) return -2;
    if (bilgi->hz < 4000 || bilgi->hz > 96000) return -2;
    return 0;
}

/* Kaynaktan tek bir çerçeveyi (sol, sağ) 16 bit imzalı olarak okur. */
static void cerceve_al(const uint8_t *d, const ezgi_bilgi_t *b,
                       uint32_t indeks, int16_t *sol, int16_t *sag)
{
    uint32_t bayt = (uint32_t)(b->bit / 8) * b->kanal;
    const uint8_t *p = d + (uint64_t)indeks * bayt;
    if (b->bit == 16) {
        *sol = (int16_t)oku16(p);
        *sag = (b->kanal == 2) ? (int16_t)oku16(p + 2) : *sol;
    } else {
        /* 8 bit WAV işaretsizdir, orta nokta 128'dir. */
        *sol = (int16_t)(((int)p[0] - 128) << 8);
        *sag = (b->kanal == 2) ? (int16_t)(((int)p[1] - 128) << 8) : *sol;
    }
}

int ezgi_cal_bellekten(const uint8_t *veri, uint32_t boy)
{
    ezgi_bilgi_t b;
    int r = ezgi_basligi_coz(veri, boy, &b);
    if (r != 0) return r;
    if (!ses_var()) return -3;

    uint32_t bayt_cerceve = (uint32_t)(b.bit / 8) * b.kanal;
    uint32_t cerceve_sayi = b.veri_boyu / bayt_cerceve;
    const uint8_t *d = veri + b.veri_ofset;

    /* Çıkış çerçeve sayısı: hız dönüşümü tamsayı oranla yapılır. */
    uint64_t cikis_sayi = ((uint64_t)cerceve_sayi * CIKIS_HZ) / b.hz;

    static int16_t tampon[PARCA * 2];
    uint64_t yazilan = 0;
    while (yazilan < cikis_sayi) {
        uint32_t n = (uint32_t)((cikis_sayi - yazilan < PARCA)
                                ? (cikis_sayi - yazilan) : PARCA);
        for (uint32_t i = 0; i < n; i++) {
            uint64_t kaynak = ((yazilan + i) * b.hz) / CIKIS_HZ;
            if (kaynak >= cerceve_sayi) kaynak = cerceve_sayi - 1;
            int16_t sol, sag;
            cerceve_al(d, &b, (uint32_t)kaynak, &sol, &sag);
            tampon[i * 2]     = sol;
            tampon[i * 2 + 1] = sag;
        }
        ses_cal(tampon, n);   /* kare sayisi: iki kanal serpistirilmis */
        yazilan += n;
    }
    return 0;
}

int ezgi_cal(const char *yol)
{
    static uint8_t dosya[512 * 1024];
    int fd = vfs_open(yol);
    if (fd < 0) {
        serial_write("ezgi: dosya acilamadi\n");
        return -1;
    }
    int okunan = vfs_read(fd, dosya, sizeof dosya);
    vfs_close(fd);
    if (okunan <= 0) {
        serial_write("ezgi: dosya bos\n");
        return -1;
    }
    return ezgi_cal_bellekten(dosya, (uint32_t)okunan);
}
