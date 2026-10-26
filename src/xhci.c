/* =====================================================================
 * XHCI (USB 3.x) sürücüsü — aktarım katmanı + HID (1.4.9)
 *
 * Eskiden XHCI yalnız tanınıyordu (src/usb.c); klavye ve fare yalnız
 * PS/2'den (ya da UHCI'den) geliyordu. Gerçek bilgisayarların ve
 * dizüstülerin çoğunda PS/2 yok: klavye ve dokunmatik yüzey USB'dedir.
 *
 * Bu sürücü:
 *   - BIOS'tan sahipliği alır (USB eski destek yeteneği), denetleyiciyi
 *     sıfırlar; DCBAA, karalama (scratchpad) tamponları, komut halkası,
 *     olay halkası (kesici 0) kurar; kesme kullanmaz, olaylar yoklanır;
 *   - kök portlardaki aygıtları sıfırlar, yuva açar (Enable Slot),
 *     adres verir (Address Device), aygıt ve yapılandırma tanımlayıcısını
 *     okur (kontrol aktarımı: Setup/Data/Status TRB);
 *   - HID arayüzlerini yapılandırır: önyükleme klavyesi (8 baytlık rapor)
 *     ve fare/tablet (rapor tanımlayıcısı ayrıştırılır, src/hid.c);
 *     kesme (interrupt IN) uç noktasını Configure Endpoint ile açar,
 *     Normal TRB'lerle raporları alır.
 * Sınırlar: hub arkasındaki aygıtlar (rota dizgisi) yok; toplu (bulk)
 * aktarım yok (USB bellek yok); tek denetleyici; 64 baytı aşan rapor yok.
 * ===================================================================== */
#include "xhci.h"
#include "hid.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "log.h"
#include "gecikme.h"

#define XHCI_MMIO_VIRT 0xA60000000ULL
#define XHCI_YAPI_VIRT 0xA70000000ULL

/* TRB türleri */
#define T_NORMAL   1
#define T_SETUP    2
#define T_DATA     3
#define T_STATUS   4
#define T_LINK     6
#define T_SLOT_AC  9
#define T_ADRES    11
#define T_YAPILAN  12
#define T_DEGERLE  13
#define T_E_AKTAR  32
#define T_E_KOMUT  33
#define T_E_PORT   34
#define T_SLOT_KAPA 10

#define HALKA 256                         /* bir sayfa: 256 x 16 bayt */

typedef struct { uint64_t p; uint32_t s, c; } trb_t;
_Static_assert(sizeof(trb_t) == 16, "TRB 16 bayt");

typedef struct { trb_t *t; uint64_t fiz; uint32_t i; uint32_t dongu; } halka_t;

static uint64_t mmio, op, rt, db;         /* sanal taban adresleri */
static uint32_t maks_yuva, maks_port, bglm_boy;   /* bağlam boyu: 32/64 */
static int ac64;
static uint64_t sonraki_virt = XHCI_YAPI_VIRT;
static uint64_t *dcbaa; static uint64_t dcbaa_fiz;
static halka_t komut;
static trb_t *olay; static uint64_t olay_fiz; static uint32_t olay_i, olay_dongu;
static int hazir;

#define AYGIT_AZAMI 8
typedef struct {
    int var, yuva, port, hiz, tur;        /* tur: 1 klavye, 2 fare/tablet */
    uint32_t mps0;
    uint8_t *giris; uint64_t giris_fiz;   /* giriş bağlamı */
    uint8_t *cikis; uint64_t cikis_fiz;   /* aygıt bağlamı */
    halka_t ep0;
    halka_t ep_in; uint32_t dci, ep_mps;
    uint8_t *tampon; uint64_t tampon_fiz; /* 0..255 kontrol, 512.. rapor */
    hid_fare_t fare;
    int bekliyor;                         /* rapor TRB'si kuyrukta */
} aygit_t;
static aygit_t ayg[AYGIT_AZAMI];
static int ayg_sayi;                    /* kullanilmis girdi sayisi (en yuksek) */
static uint32_t bekleyen_port;          /* 1.4.9: islenmemis port degisimleri (bit = port) */

static uint32_t r32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void w32(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static void w64(uint64_t a, uint64_t v) { w32(a, (uint32_t)v); w32(a + 4, (uint32_t)(v >> 32)); }
static void bekle_ms(uint32_t ms) { while (ms--) pit2_bekle_us(1000); }

static void *dma_sayfa(uint64_t *fiz) {
    uint64_t f = pmm_alloc_frame();
    if (!f) return 0;
    if (!ac64 && f >= 0x100000000ULL) { pmm_free_frame(f); return 0; }
    uint64_t v = sonraki_virt; sonraki_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) { pmm_free_frame(f); return 0; }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f;
    return p;
}

static int halka_kur(halka_t *h) {
    h->t = (trb_t *)dma_sayfa(&h->fiz);
    if (!h->t) return -1;
    h->i = 0; h->dongu = 1;
    trb_t *l = &h->t[HALKA - 1];          /* son girdi: başa bağ, döngü biti çevrilir */
    l->p = h->fiz; l->s = 0; l->c = (T_LINK << 10) | 2u;
    return 0;
}

/* var olan halkayi bastan kullanima hazirlar (yeniden takilan aygit) */
static void halka_sifirla(halka_t *h) {
    for (int i = 0; i < HALKA; i++) { h->t[i].p = 0; h->t[i].s = 0; h->t[i].c = 0; }
    h->i = 0; h->dongu = 1;
    trb_t *l = &h->t[HALKA - 1];
    l->p = h->fiz; l->s = 0; l->c = (T_LINK << 10) | 2u;
}

/* TRB'yi halkaya koyar; konduğu fiziksel adresi döner */
static uint64_t halka_koy(halka_t *h, uint64_t p, uint32_t s, uint32_t c) {
    trb_t *t = &h->t[h->i];
    uint64_t adr = h->fiz + (uint64_t)h->i * 16;
    t->p = p; t->s = s;
    __asm__ volatile ("" ::: "memory");
    t->c = (c & ~1u) | h->dongu;
    if (++h->i == HALKA - 1) {            /* bağ TRB'si: döngü bitini ver, başa dön */
        trb_t *l = &h->t[HALKA - 1];
        l->c = (l->c & ~1u) | h->dongu;
        h->i = 0; h->dongu ^= 1;
    }
    return adr;
}

static void zil(uint32_t yuva, uint32_t hedef) {
    __asm__ volatile ("mfence" ::: "memory");
    w32(db + 4u * yuva, hedef);
}

/* Bir olay alır (yoksa 0). Olay halkası işaretçisini ilerletir. */
static int olay_al(trb_t *cikti) {
    volatile trb_t *e = &olay[olay_i];
    if ((e->c & 1u) != olay_dongu) return 0;
    cikti->p = e->p; cikti->s = e->s; cikti->c = e->c;
    if (++olay_i == HALKA) { olay_i = 0; olay_dongu ^= 1; }
    w64(rt + 0x20 + 0x18, (olay_fiz + (uint64_t)olay_i * 16) | 8u);   /* ERDP, EHB temizle */
    return 1;
}

static void aktarim_olayi(const trb_t *e);

/* Belirli türde ve TRB adresinde olay bekler; arada gelen aktarım olaylarını
 * (HID) işler. Tamamlanma kodunu döner, zaman aşımında -1. */
static int olay_bekle(int tur, uint64_t trb_adr, uint32_t yuva, trb_t *sonuc, uint32_t ms) {
    for (uint32_t t = 0; t < ms * 10; t++) {
        trb_t e;
        while (olay_al(&e)) {
            int et = (int)((e.c >> 10) & 0x3F);
            if (et == tur && (trb_adr == 0 || e.p == trb_adr) &&
                (yuva == 0 || ((e.c >> 24) & 0xFF) == yuva)) {
                if (sonuc) *sonuc = e;
                return (int)(e.s >> 24);
            }
            if (et == T_E_AKTAR) aktarim_olayi(&e);
            else if (et == T_E_PORT) bekleyen_port |= 1u << (((e.p >> 24) & 0xFF) - 1u);
        }
        pit2_bekle_us(100);
    }
    return -1;
}

static int komut_ver(uint64_t p, uint32_t s, uint32_t c, trb_t *sonuc) {
    uint64_t adr = halka_koy(&komut, p, s, c);
    zil(0, 0);
    int k = olay_bekle(T_E_KOMUT, adr, 0, sonuc, 500);
    if (k != 1) { serial_write("xhci: komut basarisiz tur="); serial_write_hex((c >> 10) & 0x3F);
                  serial_write(" kod="); serial_write_hex((uint32_t)k); serial_write("\n"); }
    return k == 1 ? 0 : -1;
}

/* bağlam işaretçileri (giriş: 0 denetim, 1 yuva, 2.. uç noktalar) */
static uint32_t *giris_bglm(aygit_t *a, int i) { return (uint32_t *)(a->giris + (uint32_t)i * bglm_boy); }

/* Kontrol aktarımı EP0'da. veri_boy kadar okur/yazar (tampon[0..255]). */
static int kontrol(aygit_t *a, uint8_t rt_, uint8_t istek, uint16_t deger, uint16_t indis, uint16_t boy) {
    uint64_t kurulum = (uint64_t)rt_ | ((uint64_t)istek << 8) | ((uint64_t)deger << 16) |
                       ((uint64_t)indis << 32) | ((uint64_t)boy << 48);
    int giris_yonu = (rt_ & 0x80) != 0;
    uint32_t trt = boy == 0 ? 0 : (giris_yonu ? 3u : 2u);
    halka_koy(&a->ep0, kurulum, 8, (T_SETUP << 10) | (1u << 6) | (trt << 16));
    if (boy) halka_koy(&a->ep0, a->tampon_fiz, boy, (T_DATA << 10) | (giris_yonu ? (1u << 16) : 0));
    uint32_t durum_yon = (boy == 0 || !giris_yonu) ? (1u << 16) : 0;
    uint64_t adr = halka_koy(&a->ep0, 0, 0, (T_STATUS << 10) | (1u << 5) | durum_yon);
    zil((uint32_t)a->yuva, 1);
    int k = olay_bekle(T_E_AKTAR, adr, (uint32_t)a->yuva, 0, 500);
    if (k != 1 && k != 13) {
        serial_write("xhci: kontrol aktarimi basarisiz istek="); serial_write_hex(istek);
        serial_write(" kod="); serial_write_hex((uint32_t)k); serial_write("\n");
        return -1;
    }
    return 0;
}

static uint32_t mps_hiz(int hiz) {        /* PORTSC hız kodu -> EP0 azami paket */
    switch (hiz) { case 2: return 8; case 1: return 8; case 3: return 64; default: return 512; }
}

static void rapor_iste(aygit_t *a) {
    if (a->bekliyor) return;
    halka_koy(&a->ep_in, a->tampon_fiz + 512, a->ep_mps, (T_NORMAL << 10) | (1u << 5) | (1u << 2));
    a->bekliyor = 1;
    zil((uint32_t)a->yuva, a->dci);
}

static void aktarim_olayi(const trb_t *e) {
    uint32_t yuva = (e->c >> 24) & 0xFF, dci = (e->c >> 16) & 0x1F;
    for (int i = 0; i < ayg_sayi; i++) {
        aygit_t *a = &ayg[i];
        if (!a->var || (uint32_t)a->yuva != yuva || a->dci != dci) continue;
        a->bekliyor = 0;
        uint32_t kod = e->s >> 24;
        if (kod == 1 || kod == 13) {
            int n = (int)a->ep_mps - (int)(e->s & 0xFFFFFF);
            if (n < 0) n = 0;
            const uint8_t *r = a->tampon + 512;
            if (a->tur == 1) hid_klavye_rapor(i, r, n);
            else hid_fare_rapor(&a->fare, r, n);
        }
        rapor_iste(a);
        return;
    }
}

/* yapılandırma tanımlayıcısında HID arayüzü + kesme IN uç noktası ara */
static int hid_bul(aygit_t *a, const uint8_t *d, int n, int *arayuz, int *protokol, uint8_t *ep,
                   uint32_t *ep_mps, uint8_t *aralik, int *rapor_boy, int *altsinif) {
    int ar = -1, ep_bulundu = 0;
    for (int i = 0; i + 2 <= n && d[i] >= 2; i += d[i]) {
        uint8_t tur = d[i + 1];
        if (tur == 4 && i + 9 <= n) {                         /* arayüz */
            if (ep_bulundu) break;
            if (d[i + 5] == 3) { ar = d[i + 2]; *altsinif = d[i + 6]; *protokol = d[i + 7]; }
            else ar = -1;
        } else if (tur == 0x21 && ar >= 0 && i + 9 <= n) {    /* HID tanımlayıcısı */
            if (d[i + 6] == 0x22) *rapor_boy = d[i + 7] | (d[i + 8] << 8);
        } else if (tur == 5 && ar >= 0 && i + 7 <= n) {       /* uç nokta */
            if ((d[i + 2] & 0x80) && (d[i + 3] & 3) == 3) {
                *ep = d[i + 2]; *ep_mps = (uint32_t)(d[i + 4] | (d[i + 5] << 8)) & 0x7FF;
                *aralik = d[i + 6]; *arayuz = ar; ep_bulundu = 1;
            }
        }
    }
    (void)a;
    return ep_bulundu ? 0 : -1;
}

static uint32_t aralik_kodu(int hiz, uint8_t b) {
    if (hiz == 1 || hiz == 2) {               /* FS/LS: çerçeve (ms) -> 125 us kuvveti */
        uint32_t m = (uint32_t)(b ? b : 1) * 8u, k = 0;
        while ((1u << (k + 1)) <= m) k++;
        if (k < 3) k = 3;
        if (k > 10) k = 10;
        return k;
    }
    return b ? (uint32_t)b - 1u : 0u;          /* HS/SS */
}


static int aygit_kur(int port, int hiz) {
    int j = 0;
    while (j < AYGIT_AZAMI && ayg[j].var) j++;            /* bos (ya da cikarilmis) girdi */
    if (j >= AYGIT_AZAMI) return -1;
    aygit_t *a = &ayg[j];
    /* yeniden kullanilan girdinin DMA sayfalari korunur (sizinti yok) */
    uint8_t *gi = a->giris, *ci = a->cikis, *ta = a->tampon;
    uint64_t gif = a->giris_fiz, cif = a->cikis_fiz, taf = a->tampon_fiz;
    halka_t e0h = a->ep0, eih = a->ep_in;
    for (unsigned i = 0; i < sizeof *a; i++) ((uint8_t *)a)[i] = 0;
    a->port = port; a->hiz = hiz;
    if (gi) { a->giris = gi; a->giris_fiz = gif; for (int i = 0; i < 4096; i++) gi[i] = 0; }
    if (ci) { a->cikis = ci; a->cikis_fiz = cif; for (int i = 0; i < 4096; i++) ci[i] = 0; }
    if (ta) { a->tampon = ta; a->tampon_fiz = taf; }
    trb_t sn;
    if (komut_ver(0, 0, T_SLOT_AC << 10, &sn) != 0) return -1;
    a->yuva = (int)((sn.c >> 24) & 0xFF);
    if (a->yuva <= 0 || (uint32_t)a->yuva > maks_yuva) return -1;
    if (!a->giris) a->giris = (uint8_t *)dma_sayfa(&a->giris_fiz);
    if (!a->cikis) a->cikis = (uint8_t *)dma_sayfa(&a->cikis_fiz);
    if (!a->tampon) a->tampon = (uint8_t *)dma_sayfa(&a->tampon_fiz);
    if (!a->giris || !a->cikis || !a->tampon) return -1;
    if (e0h.t) { a->ep0 = e0h; halka_sifirla(&a->ep0); } else if (halka_kur(&a->ep0) != 0) return -1;
    a->ep_in = eih;
    dcbaa[a->yuva] = a->cikis_fiz;
    a->mps0 = mps_hiz(hiz);

    /* Address Device: yuva + EP0 bağlamı */
    uint32_t *dn = giris_bglm(a, 0); dn[0] = 0; dn[1] = 3;    /* ekle: yuva, EP0 */
    uint32_t *y = giris_bglm(a, 1);
    y[0] = (1u << 27) | ((uint32_t)hiz << 20);
    y[1] = (uint32_t)(port + 1) << 16;
    uint32_t *e0 = giris_bglm(a, 2);
    e0[1] = (3u << 1) | (4u << 3) | (a->mps0 << 16);          /* CErr 3, kontrol */
    e0[2] = (uint32_t)a->ep0.fiz | 1u; e0[3] = (uint32_t)(a->ep0.fiz >> 32);
    e0[4] = 8;
    if (komut_ver(a->giris_fiz, 0, (T_ADRES << 10) | ((uint32_t)a->yuva << 24), 0) != 0) return -1;

    /* aygıt tanımlayıcısı: önce 8 bayt (EP0 azami paketi) */
    if (kontrol(a, 0x80, 6, 0x0100, 0, 8) != 0) return -1;
    uint32_t gercek = a->tampon[7];
    if (hiz >= 4) gercek = 1u << gercek;
    if (gercek && gercek != a->mps0) {                         /* Evaluate Context */
        a->mps0 = gercek;
        dn[0] = 0; dn[1] = 2;
        e0[1] = (e0[1] & 0xFFFFu) | (a->mps0 << 16);
        if (komut_ver(a->giris_fiz, 0, (T_DEGERLE << 10) | ((uint32_t)a->yuva << 24), 0) != 0) return -1;
    }
    if (kontrol(a, 0x80, 6, 0x0100, 0, 18) != 0) return -1;
    uint8_t sinif = a->tampon[4];
    if (sinif == 9) { serial_write("xhci: hub bulundu - hub arkasindaki aygitlar desteklenmiyor\n"); return -1; }

    /* yapılandırma tanımlayıcısı */
    if (kontrol(a, 0x80, 6, 0x0200, 0, 9) != 0) return -1;
    uint32_t top = a->tampon[2] | (a->tampon[3] << 8);
    if (top > 255) top = 255;
    uint8_t yap_deger = a->tampon[5];
    if (kontrol(a, 0x80, 6, 0x0200, 0, (uint16_t)top) != 0) return -1;
    static uint8_t yap[256];
    for (uint32_t i = 0; i < top; i++) yap[i] = a->tampon[i];
    int arayuz = 0, protokol = 0, rapor_boy = 0, altsinif = 0; uint8_t ep = 0, aralik = 0; uint32_t ep_mps = 0;
    if (hid_bul(a, yap, (int)top, &arayuz, &protokol, &ep, &ep_mps, &aralik, &rapor_boy, &altsinif) != 0) {
        serial_write("xhci: HID olmayan aygit, yapilandirilmadi (sinif=");
        serial_write_hex(sinif); serial_write(")\n");
        return -1;
    }
    if (kontrol(a, 0x00, 9, yap_deger, 0, 0) != 0) return -1;            /* SET_CONFIGURATION */
    if (protokol == 1 && altsinif == 1) {                                 /* önyükleme klavyesi */
        a->tur = 1;
        kontrol(a, 0x21, 0x0B, 0, (uint16_t)arayuz, 0);                  /* SET_PROTOCOL boot */
        kontrol(a, 0x21, 0x0A, 0, (uint16_t)arayuz, 0);                  /* SET_IDLE 0 */
    } else {                                                              /* fare / tablet */
        a->tur = 2;
        if (altsinif == 1) kontrol(a, 0x21, 0x0B, 1, (uint16_t)arayuz, 0);   /* rapor protokolü */
        if (rapor_boy <= 0 || rapor_boy > 255) rapor_boy = 255;
        if (kontrol(a, 0x81, 6, 0x2200, (uint16_t)arayuz, (uint16_t)rapor_boy) != 0) return -1;
        if (hid_fare_cozumle(a->tampon, rapor_boy, &a->fare) != 0) {
            serial_write("xhci: HID rapor tanimlayicisinda X/Y yok - aygit kullanilmiyor\n");
            return -1;
        }
    }
    if (ep_mps == 0 || ep_mps > 64) ep_mps = 64;
    a->ep_mps = ep_mps;
    a->dci = (uint32_t)(ep & 0x0F) * 2u + 1u;

    /* Configure Endpoint: kesme IN */
    if (a->ep_in.t) halka_sifirla(&a->ep_in); else if (halka_kur(&a->ep_in) != 0) return -1;
    for (unsigned i = 0; i < 33u * bglm_boy && i < 4096; i++) a->giris[i] = 0;
    dn = giris_bglm(a, 0); dn[0] = 0; dn[1] = 1u | (1u << a->dci);
    y = giris_bglm(a, 1);
    y[0] = (a->dci << 27) | ((uint32_t)hiz << 20);
    y[1] = (uint32_t)(port + 1) << 16;
    uint32_t *ec = giris_bglm(a, (int)a->dci + 1);
    ec[0] = aralik_kodu(hiz, aralik) << 16;
    ec[1] = (3u << 1) | (7u << 3) | (ep_mps << 16);           /* kesme IN */
    ec[2] = (uint32_t)a->ep_in.fiz | 1u; ec[3] = (uint32_t)(a->ep_in.fiz >> 32);
    ec[4] = (ep_mps << 16) | ep_mps;                          /* ESIT yükü, ortalama TRB */
    if (komut_ver(a->giris_fiz, 0, (T_YAPILAN << 10) | ((uint32_t)a->yuva << 24), 0) != 0) return -1;

    a->var = 1;
    if (j + 1 > ayg_sayi) ayg_sayi = j + 1;
    serial_write(a->tur == 1 ? "xhci: USB klavye hazir (port=" : "xhci: USB fare/tablet hazir (port=");
    serial_write_hex((uint32_t)port + 1);
    serial_write(" yuva="); serial_write_hex((uint32_t)a->yuva);
    serial_write(" hiz="); serial_write_hex((uint32_t)hiz);
    if (a->tur == 2) serial_write(a->fare.goreli ? " goreli)\n" : " mutlak)\n");
    else serial_write(")\n");
    rapor_iste(a);
    return 0;
}

static void port_hazirla(int p) {
    uint64_t ps = op + 0x400 + 0x10u * (uint32_t)p;
    uint32_t v = r32(ps);
    if (!(v & 1)) return;                                     /* bağlı değil */
    const uint32_t KORU = ~((1u << 1) | (0x7Fu << 17));       /* PED ve RW1C bitlerine 1 yazma */
    if (!(v & 2)) {                                           /* USB2: sıfırla */
        w32(ps, (v & KORU) | (1u << 4));
        for (int i = 0; i < 200 && !(r32(ps) & (1u << 21)); i++) bekle_ms(1);
        bekle_ms(10);
    }
    v = r32(ps);
    w32(ps, (v & KORU) | (v & (0x7Fu << 17)));                /* değişim bitlerini temizle */
    v = r32(ps);
    if (!(v & 2)) { serial_write("xhci: port etkinlesmedi\n"); return; }
    int hiz = (int)((v >> 10) & 0xF);
    if (aygit_kur(p, hiz) != 0) serial_write("xhci: aygit kurulamadi\n");
}

static void bios_birak(void) {
    uint32_t hcc = r32(mmio + 0x10);
    uint32_t x = (hcc >> 16) & 0xFFFF;
    for (int guv = 0; x && guv < 64; guv++) {
        uint64_t a = mmio + (uint64_t)x * 4;
        uint32_t c = r32(a);
        if ((c & 0xFF) == 1) {                                /* USB eski destek */
            w32(a, c | (1u << 24));                           /* işletim sistemi sahibi */
            for (int i = 0; i < 1000 && (r32(a) & (1u << 16)); i++) bekle_ms(1);
            w32(a + 4, r32(a + 4) & 0xFFFF1FEEu);             /* SMI'leri kapat */
            if (r32(a) & (1u << 16)) serial_write("xhci: BIOS sahipligi birakmadi\n");
            else serial_write("xhci: sahiplik BIOS'tan alindi\n");
            return;
        }
        uint32_t s = (c >> 8) & 0xFF;
        if (!s) return;
        x += s;
    }
}

int xhci_init(void) {
    const pci_device_t *d = 0;
    for (int i = 0; i < pci_device_count(); i++) {
        const pci_device_t *p = pci_get_device(i);
        if (p->class_code == 0x0C && p->subclass == 0x03 && p->prog_if == 0x30) { d = p; break; }
    }
    if (!d) return -1;
    uint32_t cmd = pci_config_read32(d->bus, d->slot, d->func, 0x04);
    pci_config_write32(d->bus, d->slot, d->func, 0x04, (cmd & 0xFFFF) | (1u << 2) | (1u << 1));
    uint64_t fiz = (uint64_t)(d->bar[0] & 0xFFFFFFF0);
    if ((d->bar[0] & 0x6) == 0x4) fiz |= ((uint64_t)d->bar[1]) << 32;
    if (!fiz) return -1;
    /* önce bir sayfa: yetenek kayıtları; sonra gereken boy kadar */
    if (vmm_map_page(XHCI_MMIO_VIRT, fiz, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) return -1;
    mmio = XHCI_MMIO_VIRT;
    uint32_t cap0 = r32(mmio);
    uint32_t caplen = cap0 & 0xFF;
    uint32_t hcs1 = r32(mmio + 4), hcs2 = r32(mmio + 8), hcc = r32(mmio + 0x10);
    uint32_t dboff = r32(mmio + 0x14) & ~3u, rtsoff = r32(mmio + 0x18) & ~0x1Fu;
    maks_yuva = hcs1 & 0xFF; maks_port = (hcs1 >> 24) & 0xFF;
    ac64 = hcc & 1; bglm_boy = (hcc & 4) ? 64 : 32;
    uint32_t gereken = caplen + 0x400 + 0x10 * maks_port;
    if (dboff + 4 * 256 > gereken) gereken = dboff + 4 * 256;
    if (rtsoff + 0x40 > gereken) gereken = rtsoff + 0x40;
    uint32_t sayfa = (gereken + 4095) / 4096;
    if (sayfa > 64) sayfa = 64;
    for (uint32_t s = 1; s < sayfa; s++)
        if (vmm_map_page(XHCI_MMIO_VIRT + s * 4096ULL, fiz + s * 4096ULL, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) return -1;
    op = mmio + caplen; rt = mmio + rtsoff; db = mmio + dboff;
    serial_write("xhci: denetleyici surum="); serial_write_hex(cap0 >> 16);
    serial_write(" yuva="); serial_write_hex(maks_yuva);
    serial_write(" port="); serial_write_hex(maks_port);
    serial_write(" baglam="); serial_write_hex(bglm_boy); serial_write("\n");

    bios_birak();
    /* durdur ve sıfırla */
    w32(op, r32(op) & ~1u);
    for (int i = 0; i < 100 && !(r32(op + 4) & 1); i++) bekle_ms(1);
    w32(op, r32(op) | 2u);
    for (int i = 0; i < 1000 && ((r32(op) & 2) || (r32(op + 4) & (1u << 11))); i++) bekle_ms(1);
    if ((r32(op) & 2) || (r32(op + 4) & (1u << 11))) { serial_write("xhci: sifirlama bitmedi\n"); return -1; }

    if (maks_yuva > 32) maks_yuva = 32;                       /* DCBAA tek sayfa, az yuva yeter */
    w32(op + 0x38, maks_yuva);
    dcbaa = (uint64_t *)dma_sayfa(&dcbaa_fiz);
    if (!dcbaa) return -1;
    uint32_t karalama = ((hcs2 >> 27) & 0x1F) | (((hcs2 >> 21) & 0x1F) << 5);
    if (karalama) {
        uint64_t dizi_fiz; uint64_t *dizi = (uint64_t *)dma_sayfa(&dizi_fiz);
        if (!dizi || karalama > 512) return -1;
        for (uint32_t i = 0; i < karalama; i++) { uint64_t f; if (!dma_sayfa(&f)) return -1; dizi[i] = f; }
        dcbaa[0] = dizi_fiz;
    }
    w64(op + 0x30, dcbaa_fiz);
    if (halka_kur(&komut) != 0) return -1;
    w64(op + 0x18, komut.fiz | 1u);
    /* olay halkası + ERST (tek bölüt) */
    uint64_t erst_fiz; uint64_t *erst = (uint64_t *)dma_sayfa(&erst_fiz);
    olay = (trb_t *)dma_sayfa(&olay_fiz);
    if (!erst || !olay) return -1;
    erst[0] = olay_fiz; erst[1] = HALKA;
    olay_i = 0; olay_dongu = 1;
    w32(rt + 0x20 + 8, 1);                                    /* ERSTSZ */
    w64(rt + 0x20 + 0x18, olay_fiz);                          /* ERDP */
    w64(rt + 0x20 + 0x10, erst_fiz);                          /* ERSTBA (en son) */
    w32(rt + 0x20, 0);                                        /* IMAN: kesme yok, yoklama */
    w32(op, r32(op) | 1u);                                    /* çalıştır */
    for (int i = 0; i < 100 && (r32(op + 4) & 1); i++) bekle_ms(1);
    if (r32(op + 4) & 1) { serial_write("xhci: denetleyici calismadi\n"); return -1; }
    hazir = 1;
    bekle_ms(50);
    /* güç: kapalı portlara PP ver */
    for (uint32_t p = 0; p < maks_port; p++) {
        uint64_t ps = op + 0x400 + 0x10 * p; uint32_t v = r32(ps);
        if (!(v & (1u << 9))) w32(ps, (v & ~((1u << 1) | (0x7Fu << 17))) | (1u << 9));
    }
    bekle_ms(100);
    for (uint32_t p = 0; p < maks_port; p++) port_hazirla((int)p);
    { trb_t e; while (olay_al(&e)) if (((e.c >> 10) & 0x3F) == T_E_AKTAR) aktarim_olayi(&e); }
    log_write_num(LOG_BILGI, "XHCI", "HID aygit sayisi", (uint64_t)ayg_sayi);
    return ayg_sayi;
}

int xhci_hazir(void) { return hazir; }
int xhci_aygit_sayisi(void) {
    int n = 0;
    for (int i = 0; i < ayg_sayi; i++) if (ayg[i].var) n++;
    return n;
}

/* 1.4.9: sonradan takilan / cikarilan aygit. Port durum degisimi olayinda
 * port okunur: bagliysa ve uzerinde aygit yoksa numaralandirilir; bagli
 * degilse o porttaki aygit kapatilir (yuva birakilir, basili tuslar
 * birakilir). */
static void port_degisti(int p) {
    if (p < 0 || (uint32_t)p >= maks_port) return;
    uint64_t ps = op + 0x400 + 0x10u * (uint32_t)p;
    uint32_t v = r32(ps);
    const uint32_t KORU = ~((1u << 1) | (0x7Fu << 17));
    w32(ps, (v & KORU) | (v & (0x7Fu << 17)));                /* degisim bitlerini temizle */
    int var_olan = -1;
    for (int i = 0; i < ayg_sayi; i++) if (ayg[i].var && ayg[i].port == p) var_olan = i;
    if (v & 1) {
        if (var_olan < 0) {
            serial_write("xhci: porta aygit takildi (port="); serial_write_hex((uint32_t)p + 1); serial_write(")\n");
            port_hazirla(p);
        }
    } else if (var_olan >= 0) {
        aygit_t *a = &ayg[var_olan];
        a->var = 0;
        if (a->tur == 1) { static const uint8_t bos[8]; hid_klavye_rapor(var_olan, bos, 8); }
        komut_ver(0, 0, (T_SLOT_KAPA << 10) | ((uint32_t)a->yuva << 24), 0);
        dcbaa[a->yuva] = 0;
        serial_write("xhci: aygit cikarildi (port="); serial_write_hex((uint32_t)p + 1); serial_write(")\n");
    }
}

void xhci_yokla(void) {
    if (!hazir) return;
    trb_t e;
    while (olay_al(&e)) {
        int et = (int)((e.c >> 10) & 0x3F);
        if (et == T_E_AKTAR) aktarim_olayi(&e);
        else if (et == T_E_PORT) bekleyen_port |= 1u << (((e.p >> 24) & 0xFF) - 1u);
    }
    while (bekleyen_port) {
        int p = __builtin_ctz(bekleyen_port);
        bekleyen_port &= ~(1u << p);
        port_degisti(p);
    }
    hid_tik();
}
