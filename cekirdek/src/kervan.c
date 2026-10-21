#include "kervan.h"
#include "fat16.h"
#include "log.h"
#include "serial.h"

#define KERVAN_MAX_KURULU 8

static kervan_paket_t kurulu[KERVAN_MAX_KURULU];
static int kurulu_sayisi = 0;

void kervan_init(void) {
    for (int i = 0; i < KERVAN_MAX_KURULU; i++) kurulu[i].kurulu = 0;
    kurulu_sayisi = 0;
    log_write(LOG_BILGI, "KERVAN", "paket sistemi baslatildi");
}

static uint32_t saglama_hesapla(const uint8_t *veri, uint32_t bas, uint32_t boyut) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < boyut; i++) {
        s += veri[bas + i];
        s = (s << 1) | (s >> 31);   /* dondurerek karistir */
    }
    return s;
}

int kervan_dogrula(const uint8_t *veri, uint32_t boyut) {
    if (boyut < sizeof(kervan_baslik_t)) {
        serial_write("KERVAN: paket cok kucuk\n");
        return -1;
    }
    const kervan_baslik_t *h = (const kervan_baslik_t *)veri;

    if (h->sihir != KERVAN_SIHIR) {
        serial_write("KERVAN: gecersiz sihirli imza -> paket REDDEDILDI\n");
        return -1;
    }
    if (h->surum != KERVAN_SURUM) {
        serial_write("KERVAN: desteklenmeyen paket surumu -> REDDEDILDI\n");
        return -1;
    }
    if (h->girdi_sayisi == 0 || h->girdi_sayisi > KERVAN_MAX_GIRDI) {
        serial_write("KERVAN: gecersiz girdi sayisi -> REDDEDILDI\n");
        return -1;
    }
    if (h->toplam_boyut > boyut) {
        serial_write("KERVAN: bildirilen boyut dosyadan BUYUK -> REDDEDILDI\n");
        return -1;
    }

    /* Girdi tablosu ve veri bloklarinin paket SINIRLARI icinde
     * kaldigini dogrula - aksi halde kotu niyetli bir paket bellegin
     * disini okutabilirdi. */
    uint32_t tablo_bas = sizeof(kervan_baslik_t);
    const kervan_girdi_t *g = (const kervan_girdi_t *)(veri + tablo_bas);
    uint32_t veri_bas = tablo_bas + h->girdi_sayisi * sizeof(kervan_girdi_t);

    uint32_t hesap = 0;
    for (uint32_t i = 0; i < h->girdi_sayisi; i++) {
        if (g[i].offset < veri_bas || g[i].offset + g[i].boyut > h->toplam_boyut) {
            serial_write("KERVAN: girdi paket SINIRLARININ disinda -> REDDEDILDI\n");
            return -1;
        }
        hesap += saglama_hesapla(veri, g[i].offset, g[i].boyut);
    }

    if (hesap != h->saglama) {
        serial_write("KERVAN: SAGLAMA UYUSMUYOR (paket bozuk) -> REDDEDILDI\n");
        return -1;
    }
    return 0;
}

int kervan_kur(const uint8_t *veri, uint32_t boyut) {
    if (kervan_dogrula(veri, boyut) != 0) return -1;
    if (kurulu_sayisi >= KERVAN_MAX_KURULU) {
        serial_write("KERVAN: kurulu paket tablosu dolu\n");
        return -1;
    }

    const kervan_baslik_t *h = (const kervan_baslik_t *)veri;
    const kervan_girdi_t *g =
        (const kervan_girdi_t *)(veri + sizeof(kervan_baslik_t));

    serial_write("KERVAN: '");
    serial_write(h->paket_adi);
    serial_write("' surum ");
    serial_write(h->paket_surum);
    serial_write(" kuruluyor (");
    serial_write_hex(h->girdi_sayisi);
    serial_write(" girdi)\n");

    int yazilan = 0;
    for (uint32_t i = 0; i < h->girdi_sayisi; i++) {
        int r = fat16_write_file(g[i].ad, veri + g[i].offset, g[i].boyut);
        if (r < 0) {
            serial_write("KERVAN: girdi yazilamadi: ");
            serial_write(g[i].ad);
            serial_write("\n");
            return -1;
        }
        serial_write("KERVAN:   + ");
        serial_write(g[i].ad);
        serial_write(" (");
        serial_write_hex(g[i].boyut);
        serial_write(" bayt) diske yazildi\n");
        yazilan++;
    }

    kervan_paket_t *p = &kurulu[kurulu_sayisi++];
    p->kurulu = 1;
    int j = 0;
    while (h->paket_adi[j] && j < KERVAN_AD_LEN - 1) { p->ad[j] = h->paket_adi[j]; j++; }
    p->ad[j] = 0;
    j = 0;
    while (h->paket_surum[j] && j < 15) { p->surum[j] = h->paket_surum[j]; j++; }
    p->surum[j] = 0;
    p->girdi_sayisi = h->girdi_sayisi;
    p->boyut = h->toplam_boyut;

    serial_write("KERVAN: kurulum TAMAM, ");
    serial_write_hex((uint32_t)yazilan);
    serial_write(" dosya yazildi\n");
    log_write(LOG_BILGI, "KERVAN", "paket kuruldu");
    return yazilan;
}

int kervan_liste(kervan_paket_t *out, int max) {
    int n = 0;
    for (int i = 0; i < KERVAN_MAX_KURULU && n < max; i++) {
        if (kurulu[i].kurulu) out[n++] = kurulu[i];
    }
    return n;
}

int kervan_kurulu_sayisi(void) { return kurulu_sayisi; }
