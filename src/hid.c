/* =====================================================================
 * USB HID -> ÖTÜKEN girdi katmanı (1.4.9)
 *
 * Klavye: HID önyükleme raporu (8 bayt: değiştiriciler, ayrılmış, 6 tuş)
 * set-1 tarama kodlarına çevrilir ve PS/2 klavyeyle AYNI çeviriciden
 * geçer (keyboard_tarama_kodu): TR-Q, AltGr, Shift, Ctrl, geniş tuşlar.
 * USB klavye kendisi yinelemez: basılı tutulan tuş 500 ms sonra 40 ms'de
 * bir yinelenir (PS/2'nin donanım yinelemesinin karşılığı).
 *
 * Fare/tablet: rapor tanımlayıcısı (report descriptor) ayrıştırılır;
 * düğmeler, X, Y alanlarının bit konumu, boyu ve göreli/mutlak oluşu
 * bulunur (QEMU usb-tablet mutlak, usb-mouse ve gerçek fareler göreli).
 * ===================================================================== */
#include "hid.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"

/* HID kullanım kodu (0x04..0x65) -> set-1 tarama kodu. 0x100 biti: E0 öneki */
static const uint16_t KOD[0x66] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21,
    [0x0A] = 0x22, [0x0B] = 0x23, [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13,
    [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06, [0x23] = 0x07,
    [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B,
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F, [0x2C] = 0x39, [0x2D] = 0x0C,
    [0x2E] = 0x0D, [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35, [0x39] = 0x3A,
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F, [0x3F] = 0x40,
    [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,
    [0x47] = 0x46,
    [0x49] = 0x152, [0x4A] = 0x147, [0x4B] = 0x149, [0x4C] = 0x153, [0x4D] = 0x14F, [0x4E] = 0x151,
    [0x4F] = 0x14D, [0x50] = 0x14B, [0x51] = 0x150, [0x52] = 0x148,
    [0x53] = 0x45, [0x54] = 0x135, [0x55] = 0x37, [0x56] = 0x4A, [0x57] = 0x4E, [0x58] = 0x11C,
    [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C, [0x5E] = 0x4D,
    [0x5F] = 0x47, [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53, [0x64] = 0x56,
};
/* değiştirici bitleri: LCtrl LShift LAlt LGUI RCtrl RShift RAlt(AltGr) RGUI */
static const uint16_t DEGISTIRICI[8] = { 0x1D, 0x2A, 0x38, 0x15B, 0x11D, 0x36, 0x138, 0x15C };

static void gonder(uint16_t k, int birakma) {
    if (!k) return;
    if (k & 0x100) keyboard_tarama_kodu(0xE0);
    keyboard_tarama_kodu((uint8_t)((k & 0x7F) | (birakma ? 0x80 : 0)));
}

/* Her klavyenin (kaynak) son raporu ayrı tutulur; olaylar TÜM klavyelerin
 * birleşik durumundan üretilir. Eskiden tek ortak "önceki rapor" vardı:
 * B klavyesinin boş raporu A'da hâlâ basılı tuşa bırakma üretiyordu. */
#define HID_KAYNAK 8
static uint8_t kaynak_rapor[HID_KAYNAK][8];
static uint8_t onceki_mod;               /* birleşik değiştiriciler */
static uint8_t onceki_bas[0x66];         /* birleşik basılı tuşlar (kullanım kodu) */
static uint8_t yinelenen;               /* basılı tutulan son tuş (kullanım kodu) */
static uint64_t yineleme_ms;            /* sonraki yineleme zamanı */
static uint32_t rapor_sayisi;

static uint64_t simdi_ms(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    return timer_get_ticks() * 1000u / hz;
}

void hid_klavye_rapor(int kaynak, const uint8_t *r, int n) {
    if (n < 8 || kaynak < 0 || kaynak >= HID_KAYNAK) return;
    if (r[2] == 0x01) return;                     /* ErrorRollOver: çok tuş, rapor geçersiz */
    rapor_sayisi++;
    for (int i = 0; i < 8; i++) kaynak_rapor[kaynak][i] = r[i];
    uint8_t m = 0, bas[0x66];
    for (int k = 0; k < 0x66; k++) bas[k] = 0;
    for (int s = 0; s < HID_KAYNAK; s++) {
        m |= kaynak_rapor[s][0];
        for (int i = 2; i < 8; i++) { uint8_t k = kaynak_rapor[s][i]; if (k >= 0x04 && k < 0x66) bas[k] = 1; }
    }
    for (int b = 0; b < 8; b++) {                 /* önce değiştiriciler */
        int simdi = (m >> b) & 1, once = (onceki_mod >> b) & 1;
        if (simdi && !once) gonder(DEGISTIRICI[b], 0);
        if (!simdi && once) gonder(DEGISTIRICI[b], 1);
    }
    for (int k = 0x04; k < 0x66; k++)             /* bırakılanlar */
        if (onceki_bas[k] && !bas[k]) { gonder(KOD[k], 1); if (k == yinelenen) yinelenen = 0; }
    for (int i = 2; i < 8; i++) {                 /* yeni basılanlar (raporun sırasıyla) */
        uint8_t k = r[i];
        if (k >= 0x04 && k < 0x66 && bas[k] && !onceki_bas[k]) {
            gonder(KOD[k], 0);
            yinelenen = k; yineleme_ms = simdi_ms() + 500;
            onceki_bas[k] = 1;
        }
    }
    onceki_mod = m;
    for (int k = 0; k < 0x66; k++) onceki_bas[k] = bas[k];
}

void hid_tik(void) {
    if (!yinelenen) return;
    uint64_t t = simdi_ms();
    if (t < yineleme_ms) return;
    gonder(KOD[yinelenen], 0);                    /* PS/2 gibi: yineleme = yeniden basma kodu */
    yineleme_ms = t + 40;
}

uint32_t hid_klavye_rapor_sayisi(void) { return rapor_sayisi; }

/* ------------------------- fare / tablet ------------------------- */

static uint32_t bitler(const uint8_t *r, int n, uint32_t konum, uint32_t boy) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < boy && i < 32; i++) {
        uint32_t b = konum + i;
        if (b / 8 >= (uint32_t)n) break;
        if (r[b / 8] & (1u << (b % 8))) v |= 1u << i;
    }
    return v;
}
static int32_t isaretli(uint32_t v, uint32_t boy) {
    if (boy == 0 || boy >= 32) return (int32_t)v;
    if (v & (1u << (boy - 1))) return (int32_t)(v | ~((1u << boy) - 1));
    return (int32_t)v;
}

int hid_fare_cozumle(const uint8_t *d, int n, hid_fare_t *f) {
    for (unsigned i = 0; i < sizeof *f; i++) ((uint8_t *)f)[i] = 0;
    uint32_t sayfa = 0, boy = 0, sayi = 0, konum = 0, rid = 0;
    int32_t lmin = 0, lmax = 0;
    uint32_t kullanim[16]; int ku = 0;
    uint32_t kmin = 0, kmax = 0; int aralik = 0;
    int i = 0;
    while (i < n) {
        uint8_t on = d[i++];
        if (on == 0xFE) { if (i < n) i += 2 + d[i]; continue; }   /* uzun öğe */
        int sz = on & 3; if (sz == 3) sz = 4;
        if (i + sz > n) break;
        uint32_t v = 0;
        for (int k = 0; k < sz; k++) v |= (uint32_t)d[i + k] << (8 * k);
        int32_t sv = (sz == 1) ? (int8_t)v : (sz == 2) ? (int16_t)v : (int32_t)v;
        i += sz;
        uint8_t tur = on & 0xFC;
        switch (tur) {
        case 0x04: sayfa = v; break;                      /* Usage Page */
        case 0x14: lmin = sv; break;                      /* Logical Min */
        case 0x24: lmax = (sz == 1 || sz == 2) && lmin >= 0 ? (int32_t)v : sv; break;   /* Logical Max */
        case 0x74: boy = v; break;                        /* Report Size */
        case 0x94: sayi = v; break;                       /* Report Count */
        case 0x84:                                        /* Report ID */
            if (!f->rapor_kimligi) { f->rapor_kimligi = v; rid = v; konum = 0; }
            else rid = v;
            break;
        case 0x08: if (ku < 16) kullanim[ku++] = (sz == 4) ? v : (sayfa << 16) | v; break;   /* Usage */
        case 0x18: kmin = (sz == 4) ? v : (sayfa << 16) | v; aralik = 1; break;              /* Usage Min */
        case 0x28: kmax = (sz == 4) ? v : (sayfa << 16) | v; break;                          /* Usage Max */
        case 0x80: {                                      /* Input */
            int sabit = v & 1, goreli = (v >> 2) & 1;
            if (rid == f->rapor_kimligi || !f->rapor_kimligi) {
                for (uint32_t s = 0; s < sayi; s++) {
                    uint32_t u = 0;
                    if (aralik) u = kmin + s <= kmax ? kmin + s : 0;
                    else if (ku) u = kullanim[s < (uint32_t)ku ? s : (uint32_t)ku - 1];
                    uint32_t b = konum + s * boy;
                    if (!sabit) {
                        if ((u >> 16) == 0x09 && (u & 0xFFFF) >= 1 && (u & 0xFFFF) <= 3) {
                            int dn = (int)(u & 0xFFFF) - 1;
                            f->dugme_konum[dn] = b; f->dugme_var |= 1 << dn;
                        } else if (u == 0x00010030 || u == 0x00010031) {
                            int y = (u == 0x00010031);
                            f->eksen_konum[y] = b; f->eksen_boy[y] = boy;
                            f->eksen_azami[y] = (uint32_t)(lmax > 0 ? lmax : 0);
                            f->goreli = goreli; f->eksen_var |= 1 << y;
                        }
                    }
                }
                konum += sayi * boy;
            }
            ku = 0; aralik = 0;
            break; }
        case 0x90: case 0xB0: ku = 0; aralik = 0; break;  /* Output / Feature */
        case 0xA0: case 0xC0: break;                      /* Collection */
        default: break;
        }
    }
    f->gecerli = (f->eksen_var == 3);
    return f->gecerli ? 0 : -1;
}

static uint32_t fare_rapor_sayisi;
void hid_fare_rapor(const hid_fare_t *f, const uint8_t *r, int n) {
    if (!f->gecerli) return;
    int o = 0;
    if (f->rapor_kimligi) { if (n < 1 || r[0] != f->rapor_kimligi) return; o = 1; }
    const uint8_t *p = r + o; int pn = n - o;
    int dugme = 0;
    for (int b = 0; b < 3; b++)
        if ((f->dugme_var >> b) & 1) dugme |= (int)bitler(p, pn, f->dugme_konum[b], 1) << b;
    uint32_t x = bitler(p, pn, f->eksen_konum[0], f->eksen_boy[0]);
    uint32_t y = bitler(p, pn, f->eksen_konum[1], f->eksen_boy[1]);
    fare_rapor_sayisi++;
    if (f->goreli) mouse_usb_goreli(isaretli(x, f->eksen_boy[0]), isaretli(y, f->eksen_boy[1]), dugme);
    else mouse_usb_mutlak(x, y, f->eksen_azami[0] ? f->eksen_azami[0] : 0x7FFF,
                          f->eksen_azami[1] ? f->eksen_azami[1] : 0x7FFF, dugme);
}
uint32_t hid_fare_rapor_sayisi(void) { return fare_rapor_sayisi; }
