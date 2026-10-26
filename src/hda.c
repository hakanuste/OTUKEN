/* =====================================================================
 * Intel HD Audio (HDA) sürücüsü (1.4.9)
 *
 * Eskiden yalnız AC'97 vardı; 2006'dan sonraki hemen bütün bilgisayarlar
 * (ve VirtualBox'ın varsayılanı, QEMU'nun intel-hda'sı) HDA kullanır.
 * Bu sürücü:
 *   - denetleyiciyi sıfırlar, CORB/RIRB komut halkalarını kurar (yoklama),
 *   - kodekleri bulur; ses işlev grubunda bağlı bir çıkış iğnesi (hat
 *     çıkışı, hoparlör, kulaklık) ve ona bağlantı listesiyle ulaşan bir
 *     DAC arar (en çok 3 basamak: iğne <- karıştırıcı/seçici <- DAC);
 *     yol üstündeki düğümleri D0'a alır, bağlantıyı seçer, çıkış
 *     yükselticilerini açar (sessiz değil, en yüksek kazanç), iğneyi açar,
 *   - 48 kHz / 16 bit / 2 kanal çıkış akışı kurar: 32 x 4 KB'lık döngüsel
 *     tampon (~0,68 sn). AC'97'den farkı: HDA döngüsel tamponu durmadan
 *     çalar; çalınmış bölge sıfırlanır, yazılmamış yer SESSİZLİKTİR. Çalma
 *     konumu LPIB'den, tur sayısı zamanlayıcıdan doğrulanır.
 * ses.c'deki genel arayüz (ses_cal, ses_cal_dene, ses_bitir...) AC'97 yoksa
 * buraya düşer: üst katmanlar kartın türünü bilmez.
 * ===================================================================== */
#include "hda.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "log.h"
#include "timer.h"
#include "thread.h"
#include "gecikme.h"

#define HDA_MMIO_VIRT 0xA80000000ULL
#define HDA_YAPI_VIRT 0xA90000000ULL

#define GCAP      0x00
#define GCTL      0x08
#define STATESTS  0x0E
#define INTCTL    0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP    0x48
#define CORBRP    0x4A
#define CORBCTL   0x4C
#define CORBSIZE  0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP    0x58
#define RINTCNT   0x5A
#define RIRBCTL   0x5C
#define RIRBSTS   0x5D
#define RIRBSIZE  0x5E

#define TAMPON_SAYI 32
#define TAMPON_BAYT 4096
#define HALKA_BAYT  (TAMPON_SAYI * TAMPON_BAYT)
#define BAYT_SN     (48000u * 4u)

static uint64_t m;                     /* MMIO sanal taban */
static uint64_t sd;                    /* çıkış akışı tanımlayıcısı tabanı */
static uint64_t sonraki_virt = HDA_YAPI_VIRT;
static uint32_t *corb; static uint64_t corb_fiz;
static uint64_t *rirb; static uint64_t rirb_fiz;
static uint16_t corb_wp, rirb_rp;
static uint64_t *bdl; static uint64_t bdl_fiz;
static uint8_t *tampon[TAMPON_SAYI]; static uint64_t tampon_fiz[TAMPON_SAYI];
static int hazir, caliyor;
static uint32_t kodek, dac, igne, akis_no = 1;
static uint64_t yaz_ab, cal_ab;        /* mutlak bayt: yazılan / çalınan */
static uint32_t son_lpib;
static uint64_t bas_tik;
static uint8_t kazanc_azami[128];      /* düğüm başına çıkış yükseltici adımı */
static uint16_t vendor, aygit;

static uint8_t  r8(uint32_t o)  { return *(volatile uint8_t  *)(uintptr_t)(m + o); }
static uint16_t r16(uint32_t o) { return *(volatile uint16_t *)(uintptr_t)(m + o); }
static uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(uintptr_t)(m + o); }
static void w8(uint32_t o, uint8_t v)   { *(volatile uint8_t  *)(uintptr_t)(m + o) = v; }
static void w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(uintptr_t)(m + o) = v; }
static void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(m + o) = v; }
static void bekle_ms(uint32_t ms) { while (ms--) pit2_bekle_us(1000); }

static int dma64;                     /* GCAP.64OK: denetleyici 64 bit adres verebilir */
static uint32_t corb_maske = 0xFF, rirb_maske = 0xFF;   /* halka girdi sayısı - 1 */

/* DMA sayfası. Denetleyici 64 bit adresleyemiyorsa (GCAP.64OK = 0) 4 GB'ın
 * altında olmalı: üstteki çerçeveler geri verilip yenisi istenir. */
static void *dma_sayfa(uint64_t *fiz) {
    uint64_t f = pmm_alloc_frame(), red[32]; int rn = 0;
    while (f && !dma64 && f >= 0x100000000ULL && rn < 32) { red[rn++] = f; f = pmm_alloc_frame(); }
    for (int i = 0; i < rn; i++) pmm_free_frame(red[i]);
    if (f && !dma64 && f >= 0x100000000ULL) { pmm_free_frame(f); f = 0; }
    if (!f) { serial_write("HDA: DMA sayfasi yok (32 bit adres siniri)\n"); return 0; }
    uint64_t v = sonraki_virt; sonraki_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) { pmm_free_frame(f); return 0; }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f;
    return p;
}

/* Kodeğe komut (fiil) gönderir, yanıtı döner. Zaman aşımında 0xFFFFFFFF. */
static uint32_t fiil(uint32_t ham) {
    corb_wp = (uint16_t)((corb_wp + 1) & corb_maske);
    corb[corb_wp] = ham;
    __asm__ volatile ("mfence" ::: "memory");
    w16(CORBWP, corb_wp);
    for (int i = 0; i < 20000; i++) {
        uint16_t wp = (uint16_t)(r16(RIRBWP) & rirb_maske);
        if (wp != rirb_rp) {
            rirb_rp = (uint16_t)((rirb_rp + 1) & rirb_maske);
            uint32_t yanit = (uint32_t)rirb[rirb_rp];
            w8(RIRBSTS, 0x05);
            return yanit;
        }
        pit2_bekle_us(10);
    }
    return 0xFFFFFFFFu;
}
static uint32_t f12(uint32_t c, uint32_t n, uint32_t v, uint32_t y) { return fiil((c << 28) | (n << 20) | (v << 8) | (y & 0xFF)); }
static uint32_t f4(uint32_t c, uint32_t n, uint32_t v, uint32_t y)  { return fiil((c << 28) | (n << 20) | (v << 16) | (y & 0xFFFF)); }
static uint32_t param(uint32_t c, uint32_t n, uint32_t p) { return f12(c, n, 0xF00, p); }

static uint32_t tur(uint32_t c, uint32_t n) { return (param(c, n, 0x09) >> 20) & 0xF; }

/* düğümün bağlantı listesi (en çok 16) */
static int baglantilar(uint32_t c, uint32_t n, uint32_t *liste) {
    uint32_t uz = param(c, n, 0x0E);
    int uzun = (uz >> 7) & 1, say = (int)(uz & 0x7F);
    if (say > 16) say = 16;
    int k = 0;
    for (int i = 0; i < say; ) {
        uint32_t y = f12(c, n, 0xF02, (uint32_t)i);
        int adim = uzun ? 2 : 4;
        for (int j = 0; j < adim && i < say; j++, i++)
            liste[k++] = uzun ? ((y >> (16 * j)) & 0xFFFF) : ((y >> (8 * j)) & 0xFF);
    }
    return k;
}

static void yukseltici_ac(uint32_t c, uint32_t n) {
    uint32_t cap = param(c, n, 0x09);
    if (!(cap & (1u << 2))) return;                          /* çıkış yükselticisi yok */
    uint32_t amp = (cap & (1u << 3)) ? param(c, n, 0x12) : param(c, 1, 0x12);
    uint32_t adim = (amp >> 8) & 0x7F;
    if (n < 128) kazanc_azami[n] = (uint8_t)adim;
    f4(c, n, 0x3, 0xB000u | adim);                           /* çıkış, iki kanal, sessiz değil */
}

/* iğneden geriye DAC ara; bulunca yolu kurar */
static int yol_kur(uint32_t c, uint32_t n, int derinlik) {
    uint32_t t = tur(c, n);
    if (t == 0) { dac = n; f12(c, n, 0x705, 0); yukseltici_ac(c, n); return 1; }   /* ses çıkışı */
    if (derinlik >= 3) return 0;
    uint32_t l[16]; int k = baglantilar(c, n, l);
    for (int i = 0; i < k; i++) {
        uint32_t a = l[i];
        if (a == 0 || a > 0x7F) continue;
        if (yol_kur(c, a, derinlik + 1)) {
            f12(c, n, 0x705, 0);                             /* D0 */
            if (k > 1 && t != 2) f12(c, n, 0x701, (uint32_t)i);   /* seçici (karıştırıcı değilse) */
            yukseltici_ac(c, n);
            if (t == 2) f4(c, n, 0x3, 0x7000u | ((uint32_t)i << 8) | (kazanc_azami[n] ? kazanc_azami[n] : 0));   /* karıştırıcı girişi */
            return 1;
        }
    }
    return 0;
}

static int kodek_kur(uint32_t c) {
    uint32_t kok = param(c, 0, 0x04);
    uint32_t ilk = (kok >> 16) & 0xFF, say = kok & 0xFF;
    for (uint32_t g = ilk; g < ilk + say; g++) {
        uint32_t fg = param(c, g, 0x05);
        if ((fg & 0xFF) != 1) continue;       /* ses işlev grubu değil */
        f12(c, g, 0x705, 0);                                 /* D0 */
        bekle_ms(10);
        uint32_t w = param(c, g, 0x04);
        uint32_t wi = (w >> 16) & 0xFF, ws = w & 0xFF;
        /* öncelik: hat çıkışı (0), hoparlör (1), kulaklık (2) */
        for (int oncelik = 0; oncelik < 3; oncelik++)
            for (uint32_t n = wi; n < wi + ws; n++) {
                if (tur(c, n) != 4) continue;                /* iğne değil */
                uint32_t pcap = param(c, n, 0x0C);
                if (!(pcap & (1u << 4))) continue;           /* çıkış yeteneği yok */
                uint32_t cfg = f12(c, n, 0xF1C, 0);
                if (((cfg >> 30) & 3) == 1) continue;        /* bağlantısız */
                if ((int)((cfg >> 20) & 0xF) != oncelik) continue;
                dac = 0;
                if (!yol_kur(c, n, 0) || !dac) continue;
                igne = n;
                f12(c, n, 0x705, 0);
                f12(c, n, 0x707, oncelik == 2 ? 0xC0 : 0x40); /* çıkış (+kulaklık sürücüsü) */
                if (pcap & (1u << 16)) f12(c, n, 0x70C, 2);  /* EAPD */
                kodek = c;
                serial_write("HDA: kodek="); serial_write_hex(c);
                serial_write(" ignes="); serial_write_hex(n);
                serial_write(" DAC="); serial_write_hex(dac);
                serial_write(oncelik == 0 ? " (hat cikisi)\n" : oncelik == 1 ? " (hoparlor)\n" : " (kulaklik)\n");
                return 0;
            }
    }
    return -1;
}

int hda_init(void) {
    const pci_device_t *p = pci_find_by_class(0x04, 0x03);
    if (!p) return -1;
    vendor = p->vendor_id; aygit = p->device_id;
    uint32_t cmd = pci_config_read32(p->bus, p->slot, p->func, 0x04);
    pci_config_write32(p->bus, p->slot, p->func, 0x04, (cmd & 0xFFFF) | (1u << 2) | (1u << 1));
    uint64_t fiz = (uint64_t)(p->bar[0] & 0xFFFFFFF0);
    if ((p->bar[0] & 0x6) == 0x4) fiz |= ((uint64_t)p->bar[1]) << 32;
    if (!fiz) return -1;
    for (int s = 0; s < 4; s++)
        if (vmm_map_page(HDA_MMIO_VIRT + s * 4096ULL, fiz + s * 4096ULL, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) return -1;
    m = HDA_MMIO_VIRT;

    /* sıfırla: CRST 0 -> 1 */
    w32(GCTL, r32(GCTL) & ~1u);
    for (int i = 0; i < 100 && (r32(GCTL) & 1); i++) bekle_ms(1);
    bekle_ms(1);
    w32(GCTL, r32(GCTL) | 1u);
    for (int i = 0; i < 100 && !(r32(GCTL) & 1); i++) bekle_ms(1);
    if (!(r32(GCTL) & 1)) { serial_write("HDA: sifirlama bitmedi\n"); return -1; }
    bekle_ms(2);                                              /* kodekler kendini bildirsin */
    uint16_t kodekler = r16(STATESTS);
    dma64 = r16(GCAP) & 1;
    w32(INTCTL, 0);

    /* CORB / RIRB (256 girdi) */
    corb = (uint32_t *)dma_sayfa(&corb_fiz);
    rirb = (uint64_t *)dma_sayfa(&rirb_fiz);
    bdl = (uint64_t *)dma_sayfa(&bdl_fiz);
    if (!corb || !rirb || !bdl) return -1;
    for (int i = 0; i < TAMPON_SAYI; i++) if (!(tampon[i] = (uint8_t *)dma_sayfa(&tampon_fiz[i]))) return -1;
    w8(CORBCTL, 0); w8(RIRBCTL, 0);
    bekle_ms(1);
    w32(CORBLBASE, (uint32_t)corb_fiz); w32(CORBUBASE, (uint32_t)(corb_fiz >> 32));
    /* halka boyu: denetleyicinin BİLDİRDİĞİ en büyük (SZCAP bit 6/5/4 =
     * 256/16/2 girdi); hiçbiri yoksa sürücü kurulmaz */
    {
        uint8_t sc = r8(CORBSIZE), rc = r8(RIRBSIZE);
        int ck = (sc & 0x40) ? 2 : (sc & 0x20) ? 1 : (sc & 0x10) ? 0 : -1;
        int rk = (rc & 0x40) ? 2 : (rc & 0x20) ? 1 : (rc & 0x10) ? 0 : -1;
        if (ck < 0 || rk < 0) { serial_write("HDA: CORB/RIRB boyu bildirilmiyor\n"); return -1; }
        static const uint32_t MASKE[3] = { 1, 15, 255 };
        corb_maske = MASKE[ck]; rirb_maske = MASKE[rk];
        w8(CORBSIZE, (uint8_t)((sc & ~3u) | (uint8_t)ck));
        w8(RIRBSIZE, (uint8_t)((rc & ~3u) | (uint8_t)rk));
    }
    w16(CORBRP, 0x8000);
    for (int i = 0; i < 100 && !(r16(CORBRP) & 0x8000); i++) bekle_ms(1);
    w16(CORBRP, 0);
    for (int i = 0; i < 100 && (r16(CORBRP) & 0x8000); i++) bekle_ms(1);
    w16(CORBWP, 0); corb_wp = 0;
    w32(RIRBLBASE, (uint32_t)rirb_fiz); w32(RIRBUBASE, (uint32_t)(rirb_fiz >> 32));
    w16(RIRBWP, 0x8000); rirb_rp = 0;
    /* RINTCNT yanıtında denetleyici (QEMU dahil) yanıt kesme bayrağını
     * kurup durur; bayrak RIRBSTS'e 1 yazınca kalkar. Bayrağın kurulması
     * için RINTCTL açık olmalı (genel kesme INTCTL kapalı: kesme gelmez). */
    w16(RINTCNT, (uint16_t)(rirb_maske ? rirb_maske : 1));
    w8(CORBCTL, 2); w8(RIRBCTL, 3);                           /* DMA çalıştır + yanıt bayrağı */
    bekle_ms(1);

    int bulundu = -1;
    for (uint32_t c = 0; c < 15 && bulundu != 0; c++)
        if (kodekler & (1u << c)) bulundu = kodek_kur(c);
    if (bulundu != 0) { serial_write("HDA: cikis yolu (igne <- DAC) bulunamadi\n"); return -1; }

    /* ilk çıkış akışı */
    uint16_t gcap = r16(GCAP);
    uint32_t iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF;
    if (!oss) { serial_write("HDA: cikis akisi yok\n"); return -1; }
    sd = 0x80 + 0x20 * iss;
    for (int i = 0; i < TAMPON_SAYI; i++) {
        bdl[i * 2] = tampon_fiz[i];
        bdl[i * 2 + 1] = TAMPON_BAYT;                         /* uzunluk, IOC yok */
    }
    /* DAC: akış 1 kanal 0, biçim 48k/16/2 */
    f12(kodek, dac, 0x706, akis_no << 4);
    f4(kodek, dac, 0x2, 0x0011);
    hazir = 1;
    serial_write("HDA: bulundu vendor="); serial_write_hex(vendor);
    serial_write(" device="); serial_write_hex(aygit);
    serial_write(" cikis akisi="); serial_write_hex(iss); serial_write(" (48kHz 16 bit stereo)\n");
    log_write(LOG_BILGI, "SES", "HDA ses surucusu hazir (48kHz stereo)");
    return 0;
}

static void akis_durdur(void) {
    w8((uint32_t)sd, (uint8_t)(r8((uint32_t)sd) & ~2u));     /* RUN 0 */
    for (int i = 0; i < 100 && (r8((uint32_t)sd) & 2); i++) pit2_bekle_us(100);
    caliyor = 0;
}

static void akis_baslat(void) {
    uint32_t o = (uint32_t)sd;
    akis_durdur();
    w8(o, 1);                                                 /* SRST */
    for (int i = 0; i < 100 && !(r8(o) & 1); i++) pit2_bekle_us(100);
    w8(o, 0);
    for (int i = 0; i < 100 && (r8(o) & 1); i++) pit2_bekle_us(100);
    w8(o + 3, 0x1C);                                          /* durum bitlerini temizle */
    w32(o + 0x18, (uint32_t)bdl_fiz); w32(o + 0x1C, (uint32_t)(bdl_fiz >> 32));
    w32(o + 8, HALKA_BAYT);
    w16(o + 0xC, TAMPON_SAYI - 1);
    w16(o + 0x12, 0x0011);
    w8(o + 2, (uint8_t)(akis_no << 4));                       /* akış numarası */
    son_lpib = 0; yaz_ab = 0; cal_ab = 0;
    bas_tik = timer_get_ticks();
}

/* çalma konumunu günceller; çalınmış bölgeyi sıfırlar (sonraki tur sessiz) */
static void konum(void) {
    if (!caliyor) return;
    uint32_t lpib = r32((uint32_t)sd + 4) % HALKA_BAYT;
    uint32_t ilerleme = (lpib + HALKA_BAYT - son_lpib) % HALKA_BAYT;
    /* yoklama bir turdan uzun gecikmişse zamanlayıcıdan tur ekle */
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    uint64_t tahmin = (timer_get_ticks() - bas_tik) * BAYT_SN / hz;
    uint64_t yeni = cal_ab + ilerleme;
    while (tahmin > yeni + HALKA_BAYT / 2 + BAYT_SN / 10) yeni += HALKA_BAYT;
    for (uint64_t b = cal_ab; b < yeni && b < cal_ab + HALKA_BAYT; b++) {
        uint32_t i = (uint32_t)(b % HALKA_BAYT);
        tampon[i / TAMPON_BAYT][i % TAMPON_BAYT] = 0;         /* çalındı: sonraki tur sessiz */
    }
    cal_ab = yeni; son_lpib = lpib;
}

int hda_var(void) { return hazir; }

int hda_bosaldi(void) {
    if (!hazir || !caliyor) return 1;
    konum();
    return cal_ab >= yaz_ab;
}

/* yer olduğu kadar yazar (bayt hizasında kare), alınan kare sayısı */
int hda_cal_dene(const int16_t *pcm, uint32_t kare) {
    if (!hazir) return -1;
    if (!caliyor) akis_baslat();
    else konum();
    if (caliyor && cal_ab >= yaz_ab) {                        /* boşalmıştı: hemen arkasına yaz */
        yaz_ab = cal_ab + 2048;                               /* ~10 ms öne: DMA önden okur */
    }
    uint64_t dolu = yaz_ab > cal_ab ? yaz_ab - cal_ab : 0;
    uint64_t bos = HALKA_BAYT > dolu + TAMPON_BAYT ? HALKA_BAYT - dolu - TAMPON_BAYT : 0;
    uint32_t alinan = (uint32_t)(bos / 4);
    if (alinan > kare) alinan = kare;
    const uint8_t *k = (const uint8_t *)pcm;
    for (uint32_t b = 0; b < alinan * 4; b++) {
        uint32_t i = (uint32_t)((yaz_ab + b) % HALKA_BAYT);
        tampon[i / TAMPON_BAYT][i % TAMPON_BAYT] = k[b];
    }
    __asm__ volatile ("mfence" ::: "memory");
    yaz_ab += (uint64_t)alinan * 4;
    if (!caliyor && alinan) {
        w8((uint32_t)sd, (uint8_t)(r8((uint32_t)sd) | 2u));  /* RUN */
        caliyor = 1;
        bas_tik = timer_get_ticks();
    }
    return (int)alinan;
}

int hda_cal(const int16_t *pcm, uint32_t kare) {
    uint32_t yazilan = 0;
    uint64_t t0 = timer_get_ticks(), hz = timer_hz() ? timer_hz() : 100;
    while (yazilan < kare) {
        int r = hda_cal_dene(pcm + (uint64_t)yazilan * 2, kare - yazilan);
        if (r < 0) return -1;
        if (r == 0) {
            if (timer_get_ticks() - t0 > 3 * hz + (uint64_t)kare * hz / 48000) { serial_write("HDA: akis ilerlemiyor\n"); hda_dur(); return -1; }
            thread_sleep(1);
            continue;
        }
        yazilan += (uint32_t)r;
    }
    return 0;
}

void hda_dur(void) {
    if (!hazir) return;
    akis_durdur();
    for (int t = 0; t < TAMPON_SAYI; t++) for (int i = 0; i < TAMPON_BAYT; i++) tampon[t][i] = 0;
    yaz_ab = cal_ab = 0;
}

void hda_seviye(uint8_t yuzde) {
    if (!hazir) return;
    if (yuzde > 100) yuzde = 100;
    uint32_t n[2] = { dac, igne };
    for (int i = 0; i < 2; i++) {
        uint32_t a = n[i] < 128 ? kazanc_azami[n[i]] : 0;
        if (!a) continue;
        uint32_t g = a * yuzde / 100;
        f4(kodek, n[i], 0x3, 0xB000u | (yuzde == 0 ? 0x80u : 0u) | g);
    }
}

uint16_t hda_vendor(void) { return vendor; }
uint16_t hda_aygit(void) { return aygit; }
