#include "xhci.h"
#include "keyboard.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "log.h"
#include "serial.h"

/* --- Yetenek kayitlari --- */
#define CAP_LENGTH    0x00
#define CAP_HCSPARAMS1 0x04
#define CAP_HCSPARAMS2 0x08
#define CAP_HCCPARAMS1 0x10
#define CAP_DBOFF     0x14
#define CAP_RTSOFF    0x18

/* --- Calisma kayitlari (op = taban + CAPLENGTH) --- */
#define OP_USBCMD   0x00
#define OP_USBSTS   0x04
#define OP_CRCR     0x18
#define OP_DCBAAP   0x30
#define OP_CONFIG   0x38
#define OP_PORTSC(n) (0x400 + (n) * 0x10)

#define CMD_RS      (1u << 0)
#define CMD_HCRST   (1u << 1)
#define STS_HCH     (1u << 0)
#define STS_CNR     (1u << 11)

#define PORT_CCS    (1u << 0)
#define PORT_PED    (1u << 1)
#define PORT_PR     (1u << 4)
#define PORT_PRC    (1u << 21)
/* Yazarak temizlenen (RW1C) bitler: PED, CSC, PEC, WRC, OCC, PRC, PLC, CEC */
#define PORT_RW1C   0x00FE0002u

/* --- TRB turleri --- */
#define TRB_NORMAL   1
#define TRB_SETUP    2
#define TRB_DATA     3
#define TRB_STATUS   4
#define TRB_LINK     6
#define CMD_ENABLE_SLOT 9
#define CMD_ADDRESS     11
#define CMD_CONFIGURE   12
#define CMD_EVALUATE    13
#define EVT_TRANSFER    32
#define EVT_CMD_DONE    33

#define HALKA_BOYUT 256
#define CC_BASARI   1
#define CC_KISA     13

#define XHCI_MMIO_VIRT 0xA50000000ULL
#define XHCI_DMA_VIRT  0xA50100000ULL
#define XHCI_MMIO_SAYFA 16

typedef struct { uint32_t p0, p1, durum, kontrol; } trb_t;

typedef struct {
    trb_t   *trb;
    uint64_t fiz;
    int      enq;
    int      dongu;   /* cycle bit */
} halka_t;

static volatile uint8_t *mmio;
static volatile uint8_t *op;
static volatile uint8_t *rt;
static volatile uint32_t *db;
static int ctx_boyut;              /* 32 veya 64 */
static int hazir = 0, klavye_hazir = 0;
static int max_port = 0;

static uint64_t *dcbaa;  static uint64_t dcbaa_fiz;
static halka_t   komut;
static trb_t    *olay;   static uint64_t olay_fiz;
static int       olay_deq = 0, olay_ccs = 1;
static uint64_t  dma_virt = XHCI_DMA_VIRT;

/* Klavye durumu */
static int      kb_slot, kb_dci;
static halka_t  kb_halka;
static uint8_t *kb_rapor;  static uint64_t kb_rapor_fiz;
static uint8_t  onceki[8];
static int      caps = 0;
static int      hata_say = 0;

static uint32_t r32(volatile uint8_t *b, uint32_t o) { return *(volatile uint32_t *)(b + o); }
static void w32(volatile uint8_t *b, uint32_t o, uint32_t v) { *(volatile uint32_t *)(b + o) = v; }
static void w64(volatile uint8_t *b, uint32_t o, uint64_t v) {
    w32(b, o, (uint32_t)v);
    w32(b, o + 4, (uint32_t)(v >> 32));
}

/* Kesmeler acilmadan once de calisan, zamanlayicidan bagimsiz bekleme:
 * her io_wait yaklasik 1 mikrosaniyedir. */
static void bekle_us(uint32_t us) { for (uint32_t i = 0; i < us; i++) io_wait(); }
static void bekle_ms(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) bekle_us(1000); }

static int dma_sayfa(uint64_t *fiz, void **virt) {
    uint64_t f = pmm_alloc_frame();
    if (f == 0) return -1;
    uint64_t v = dma_virt; dma_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
        pmm_free_frame(f); return -1;
    }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f; *virt = p;
    return 0;
}

static int halka_kur(halka_t *h) {
    void *v;
    if (dma_sayfa(&h->fiz, &v) != 0) return -1;
    h->trb = (trb_t *)v; h->enq = 0; h->dongu = 1;
    return 0;
}

/* Halkaya bir TRB koyar; son giris Link TRB'dir. */
static void halka_ekle(halka_t *h, uint32_t p0, uint32_t p1, uint32_t durum,
                       uint32_t kontrol) {
    trb_t *t = &h->trb[h->enq];
    t->p0 = p0; t->p1 = p1; t->durum = durum;
    __sync_synchronize();
    t->kontrol = kontrol | (uint32_t)h->dongu;
    h->enq++;
    if (h->enq == HALKA_BOYUT - 1) {
        trb_t *l = &h->trb[h->enq];
        l->p0 = (uint32_t)h->fiz; l->p1 = (uint32_t)(h->fiz >> 32); l->durum = 0;
        __sync_synchronize();
        l->kontrol = (TRB_LINK << 10) | (1u << 1) | (uint32_t)h->dongu;  /* TC */
        h->enq = 0;
        h->dongu ^= 1;
    }
}

static void kapi_cal(uint32_t slot, uint32_t hedef) {
    __sync_synchronize();
    db[slot] = hedef;
}

/* Olay halkasindan bir giris al (varsa). */
static int olay_al(trb_t *o) {
    trb_t *t = &olay[olay_deq];
    if ((t->kontrol & 1u) != (uint32_t)olay_ccs) return 0;
    __sync_synchronize();
    *o = *t;
    olay_deq++;
    if (olay_deq == HALKA_BOYUT) { olay_deq = 0; olay_ccs ^= 1; }
    w64(rt, 0x20 + 0x18, (olay_fiz + (uint64_t)olay_deq * sizeof(trb_t)) | (1u << 3));
    return 1;
}

/* Belirli turde bir olay bekler; baska olaylar atlanir. */
static int olay_bekle(uint32_t tip, trb_t *o, uint32_t ms) {
    for (uint32_t i = 0; i < ms * 10; i++) {
        trb_t e;
        while (olay_al(&e)) {
            if (((e.kontrol >> 10) & 0x3F) == tip) { *o = e; return 0; }
        }
        bekle_us(100);
    }
    return -1;
}

static int komut_gonder(uint32_t p0, uint32_t p1, uint32_t kontrol, uint32_t *slot_id) {
    halka_ekle(&komut, p0, p1, 0, kontrol);
    kapi_cal(0, 0);
    trb_t e;
    if (olay_bekle(EVT_CMD_DONE, &e, 1000) != 0) return -1;
    if (slot_id) *slot_id = e.kontrol >> 24;
    return (int)(e.durum >> 24) == CC_BASARI ? 0 : -(int)(e.durum >> 24);
}

/* --- Cihaz baglami --- */
typedef struct {
    uint8_t *giris;  uint64_t giris_fiz;
    uint8_t *cihaz;  uint64_t cihaz_fiz;
    halka_t  ep0;
    int      hiz, port, slot;
} cihaz_t;

static uint8_t *tamp;  static uint64_t tamp_fiz;   /* 4 KB aktarim tamponu */

static int kontrol_aktar(cihaz_t *c, const uint8_t s[8], uint32_t uzunluk, int giris) {
    uint32_t trt = uzunluk ? (giris ? 3u : 2u) : 0u;
    uint32_t a = (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24;
    uint32_t b = (uint32_t)s[4] | (uint32_t)s[5] << 8 | (uint32_t)s[6] << 16 | (uint32_t)s[7] << 24;
    halka_ekle(&c->ep0, a, b, 8, (TRB_SETUP << 10) | (1u << 6) | (trt << 16));
    if (uzunluk)
        halka_ekle(&c->ep0, (uint32_t)tamp_fiz, (uint32_t)(tamp_fiz >> 32), uzunluk,
                   (TRB_DATA << 10) | (giris ? (1u << 16) : 0));
    halka_ekle(&c->ep0, 0, 0, 0,
               (TRB_STATUS << 10) | (1u << 5) | ((uzunluk && giris) ? 0 : (1u << 16)));
    kapi_cal((uint32_t)c->slot, 1);
    trb_t e;
    if (olay_bekle(EVT_TRANSFER, &e, 1000) != 0) return -1;
    int cc = (int)(e.durum >> 24);
    return (cc == CC_BASARI || cc == CC_KISA) ? 0 : -cc;
}

static uint32_t ep0_azami(int hiz) {
    switch (hiz) { case 2: return 8; case 4: return 512; default: return 64; }
}

static uint32_t *ctx_ptr(uint8_t *taban, int indeks) {
    return (uint32_t *)(taban + (uint32_t)indeks * (uint32_t)ctx_boyut);
}

static void ep_baglam_yaz(uint32_t *ep, uint32_t dw0, uint32_t tip, uint32_t azami,
                          uint64_t halka_fiz, uint32_t ort_uzunluk) {
    ep[0] = dw0;
    ep[1] = (3u << 1) | (tip << 3) | (azami << 16);
    ep[2] = (uint32_t)halka_fiz | 1u;       /* DCS=1 */
    ep[3] = (uint32_t)(halka_fiz >> 32);
    ep[4] = ort_uzunluk | (ort_uzunluk << 16);
}

static int cihaz_ac(cihaz_t *c, int port, int hiz) {
    uint32_t slot;
    if (komut_gonder(0, 0, (CMD_ENABLE_SLOT << 10), &slot) != 0 || slot == 0) return -1;
    c->slot = (int)slot; c->port = port; c->hiz = hiz;
    void *v;
    if (dma_sayfa(&c->giris_fiz, &v) != 0) return -1;
    c->giris = v;
    if (dma_sayfa(&c->cihaz_fiz, &v) != 0) return -1;
    c->cihaz = v;
    if (halka_kur(&c->ep0) != 0) return -1;
    dcbaa[slot] = c->cihaz_fiz;

    uint32_t *ic = ctx_ptr(c->giris, 0);
    ic[1] = 3;                                  /* A0 | A1 */
    uint32_t *sl = ctx_ptr(c->giris, 1);
    sl[0] = ((uint32_t)hiz << 20) | (1u << 27);
    sl[1] = ((uint32_t)(port + 1) << 16);
    ep_baglam_yaz(ctx_ptr(c->giris, 2), 0, 4, ep0_azami(hiz), c->ep0.fiz, 8);

    int r = komut_gonder((uint32_t)c->giris_fiz, (uint32_t)(c->giris_fiz >> 32),
                         (CMD_ADDRESS << 10) | (slot << 24), 0);
    if (r != 0) { serial_write("XHCI: adres atama komutu basarisiz\n"); return -1; }

    /* Gercek EP0 azami paket boyutunu oku, farkliysa baglami guncelle. */
    static const uint8_t gd[8] = {0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 8, 0};
    if (kontrol_aktar(c, gd, 8, 1) != 0) return -1;
    uint32_t gercek = (hiz == 4) ? (1u << tamp[7]) : tamp[7];
    if (gercek != 0 && gercek != ep0_azami(hiz)) {
        for (int i = 0; i < 4096; i++) c->giris[i] = 0;
        ctx_ptr(c->giris, 0)[1] = 2;            /* yalniz A1 (EP0) */
        ep_baglam_yaz(ctx_ptr(c->giris, 2), 0, 4, gercek, c->ep0.fiz, 8);
        /* Halka ilerledi: DCS ve dequeue imlecini guncel konumdan ver. */
        uint32_t *ep = ctx_ptr(c->giris, 2);
        uint64_t guncel = c->ep0.fiz + (uint64_t)c->ep0.enq * sizeof(trb_t);
        ep[2] = (uint32_t)guncel | (uint32_t)c->ep0.dongu;
        ep[3] = (uint32_t)(guncel >> 32);
        komut_gonder((uint32_t)c->giris_fiz, (uint32_t)(c->giris_fiz >> 32),
                     (CMD_EVALUATE << 10) | (slot << 24), 0);
    }
    return 0;
}

/* HID kullanim kodu -> set-1 tarama kodu (yalniz ana blok), yoksa 0 */
static uint8_t hid_tarama(uint8_t k) {
    static const uint8_t harf[26] = {
        0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
    if (k >= 0x04 && k <= 0x1D) return harf[k - 0x04];
    if (k >= 0x1E && k <= 0x26) return (uint8_t)(0x02 + (k - 0x1E));
    switch (k) {
        case 0x27: return 0x0B;  case 0x28: return 0x1C;  case 0x29: return 0x01;
        case 0x2A: return 0x0E;  case 0x2B: return 0x0F;  case 0x2C: return 0x39;
        case 0x2D: return 0x0C;  case 0x2E: return 0x0D;  case 0x2F: return 0x1A;
        case 0x30: return 0x1B;  case 0x31: return 0x2B;  case 0x32: return 0x2B;
        case 0x33: return 0x27;  case 0x34: return 0x28;  case 0x35: return 0x29;
        case 0x36: return 0x33;  case 0x37: return 0x34;  case 0x38: return 0x35;
        default:   return 0;
    }
}

static int harf_tusu(uint8_t k) {
    return (k >= 0x04 && k <= 0x1D) || k == 0x2F || k == 0x30 || k == 0x33 ||
           k == 0x34 || k == 0x36 || k == 0x37;
}

static void kb_sirala(void) {
    halka_ekle(&kb_halka, (uint32_t)kb_rapor_fiz, (uint32_t)(kb_rapor_fiz >> 32), 8,
               (TRB_NORMAL << 10) | (1u << 5) | (1u << 2));   /* IOC | ISP */
    kapi_cal((uint32_t)kb_slot, (uint32_t)kb_dci);
}

/* Yapilandirma tanimlayicisini tarayip BOOT klavyesi arar. */
static int klavye_bul(const uint8_t *d, uint32_t n, int *arayuz, int *ep_adres,
                      int *azami, int *aralik, int *hub) {
    int bulundu = 0, mevcut = -1, klavye_ar = 0;
    for (uint32_t i = 0; i + 2 <= n && d[i] != 0; i += d[i]) {
        if (d[i + 1] == 4 && i + 9 <= n) {
            mevcut = d[i + 2];
            klavye_ar = (d[i + 5] == 3 && d[i + 7] == 1);
            if (d[i + 5] == 9) *hub = 1;
        } else if (d[i + 1] == 5 && klavye_ar && !bulundu && i + 7 <= n) {
            if ((d[i + 3] & 3) == 3 && (d[i + 2] & 0x80)) {
                *arayuz = mevcut; *ep_adres = d[i + 2] & 0x0F;
                *azami = d[i + 4] | (d[i + 5] << 8); *aralik = d[i + 6];
                bulundu = 1;
            }
        }
    }
    return bulundu;
}

static int klavye_kur(cihaz_t *c) {
    static const uint8_t gc9[8] = {0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 9, 0};
    if (kontrol_aktar(c, gc9, 9, 1) != 0) return -1;
    uint32_t toplam = tamp[2] | (tamp[3] << 8);
    if (toplam < 9) return -1;
    if (toplam > 512) toplam = 512;
    uint8_t gc[8] = {0x80, 0x06, 0x00, 0x02, 0x00, 0x00, (uint8_t)toplam, (uint8_t)(toplam >> 8)};
    if (kontrol_aktar(c, gc, toplam, 1) != 0) return -1;
    uint8_t yapilandirma = tamp[5];

    int ar = 0, epn = 0, azami = 8, aralik = 10, hub = 0;
    if (!klavye_bul(tamp, toplam, &ar, &epn, &azami, &aralik, &hub)) {
        if (hub) serial_write("XHCI: hub bulundu - hub destegi yok, atlandi\n");
        return -1;
    }

    uint8_t sc[8] = {0x00, 0x09, yapilandirma, 0, 0, 0, 0, 0};
    if (kontrol_aktar(c, sc, 0, 0) != 0) return -1;
    uint8_t sp[8] = {0x21, 0x0B, 0, 0, (uint8_t)ar, 0, 0, 0};   /* BOOT protokolu */
    kontrol_aktar(c, sp, 0, 0);
    uint8_t si[8] = {0x21, 0x0A, 0, 0, (uint8_t)ar, 0, 0, 0};   /* bosta=0 */
    kontrol_aktar(c, si, 0, 0);

    int dci = epn * 2 + 1;
    uint32_t aralik_alan;
    if (c->hiz >= 3) {
        aralik_alan = (uint32_t)(aralik > 0 ? aralik - 1 : 0);
    } else {
        uint32_t cerceve = (uint32_t)(aralik > 0 ? aralik : 1) * 8;
        aralik_alan = 3;
        while (aralik_alan < 10 && (1u << (aralik_alan + 1)) <= cerceve) aralik_alan++;
    }
    if (halka_kur(&kb_halka) != 0) return -1;

    for (int i = 0; i < 4096; i++) c->giris[i] = 0;
    ctx_ptr(c->giris, 0)[1] = 1u | (1u << dci);
    uint32_t *sl = ctx_ptr(c->giris, 1);
    sl[0] = ((uint32_t)c->hiz << 20) | ((uint32_t)dci << 27);
    sl[1] = ((uint32_t)(c->port + 1) << 16);
    ep_baglam_yaz(ctx_ptr(c->giris, dci + 1), aralik_alan << 16, 7,
                  (uint32_t)(azami & 0x7FF), kb_halka.fiz, 8);
    if (komut_gonder((uint32_t)c->giris_fiz, (uint32_t)(c->giris_fiz >> 32),
                     (CMD_CONFIGURE << 10) | ((uint32_t)c->slot << 24), 0) != 0) {
        serial_write("XHCI: uc nokta yapilandirmasi basarisiz\n");
        return -1;
    }
    kb_slot = c->slot; kb_dci = dci;
    kb_sirala();
    return 0;
}

static int port_sifirla(int p) {
    uint32_t o = OP_PORTSC(p);
    uint32_t v = r32(op, o);
    w32(op, o, (v & ~PORT_RW1C) | PORT_PR);
    for (int i = 0; i < 500; i++) {
        bekle_ms(1);
        v = r32(op, o);
        if (v & PORT_PRC) break;
    }
    if (!(v & PORT_PRC)) return -1;
    w32(op, o, (v & ~PORT_RW1C) | PORT_PRC);     /* PRC'yi temizle */
    bekle_ms(20);
    v = r32(op, o);
    if (!(v & PORT_PED)) return -1;
    return (int)((v >> 10) & 0xF);               /* port hizi */
}

/* BIOS/UEFI'den denetleyici sahipligini devral (USB Legacy Support). */
static void sahiplik_al(uint32_t hcc) {
    uint32_t ofs = ((hcc >> 16) & 0xFFFF) << 2;
    for (int guvenlik = 0; ofs && guvenlik < 64; guvenlik++) {
        uint32_t cap = r32(mmio, ofs);
        if ((cap & 0xFF) == 1) {
            w32(mmio, ofs, cap | (1u << 24));            /* OS sahiplik */
            for (int i = 0; i < 1000; i++) {
                if (!(r32(mmio, ofs) & (1u << 16))) break;   /* BIOS biraktı */
                bekle_ms(1);
            }
            w32(mmio, ofs + 4, 0);                       /* SMI'lari kapat */
            return;
        }
        uint32_t sonraki = (cap >> 8) & 0xFF;
        if (!sonraki) break;
        ofs += sonraki << 2;
    }
}

static int denetleyici_baslat(const pci_device_t *d) {
    uint64_t fiz = (uint64_t)(d->bar[0] & 0xFFFFFFF0u);
    if ((d->bar[0] & 0x6) == 0x4) fiz |= ((uint64_t)d->bar[1]) << 32;
    if (!fiz) return -1;

    uint32_t cmd = pci_config_read32(d->bus, d->slot, d->func, 0x04);
    pci_config_write32(d->bus, d->slot, d->func, 0x04, cmd | (1u << 1) | (1u << 2));

    for (int s = 0; s < XHCI_MMIO_SAYFA; s++)
        if (vmm_map_page(XHCI_MMIO_VIRT + (uint64_t)s * 4096, fiz + (uint64_t)s * 4096,
                         VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) return -1;
    mmio = (volatile uint8_t *)(uintptr_t)XHCI_MMIO_VIRT;

    uint32_t cap0 = r32(mmio, CAP_LENGTH);
    uint32_t caplen = cap0 & 0xFF;
    op = mmio + caplen;
    uint32_t hcs1 = r32(mmio, CAP_HCSPARAMS1);
    uint32_t hcs2 = r32(mmio, CAP_HCSPARAMS2);
    uint32_t hcc  = r32(mmio, CAP_HCCPARAMS1);
    uint32_t rtsoff = r32(mmio, CAP_RTSOFF) & ~0x1Fu;
    uint32_t dboff  = r32(mmio, CAP_DBOFF) & ~0x3u;
    if (rtsoff + 0x40 > XHCI_MMIO_SAYFA * 4096 || dboff + 0x400 > XHCI_MMIO_SAYFA * 4096) {
        serial_write("XHCI: kayit alani beklenenden buyuk, atlandi\n");
        return -1;
    }
    rt = mmio + rtsoff;
    db = (volatile uint32_t *)(mmio + dboff);
    ctx_boyut = (hcc & (1u << 2)) ? 64 : 32;
    int max_slot = (int)(hcs1 & 0xFF);
    max_port = (int)((hcs1 >> 24) & 0xFF);
    if (max_slot == 0 || max_port == 0) return -1;

    sahiplik_al(hcc);

    /* Dur, sifirla */
    if (!(r32(op, OP_USBSTS) & STS_HCH)) {
        w32(op, OP_USBCMD, r32(op, OP_USBCMD) & ~CMD_RS);
        for (int i = 0; i < 100 && !(r32(op, OP_USBSTS) & STS_HCH); i++) bekle_ms(1);
    }
    w32(op, OP_USBCMD, CMD_HCRST);
    for (int i = 0; i < 1000; i++) {
        bekle_ms(1);
        if (!(r32(op, OP_USBCMD) & CMD_HCRST) && !(r32(op, OP_USBSTS) & STS_CNR)) break;
    }
    if ((r32(op, OP_USBCMD) & CMD_HCRST) || (r32(op, OP_USBSTS) & STS_CNR)) {
        serial_write("XHCI: sifirlama tamamlanmadi\n");
        return -1;
    }

    int kullan = max_slot < 16 ? max_slot : 16;
    w32(op, OP_CONFIG, (uint32_t)kullan);

    void *v;
    if (dma_sayfa(&dcbaa_fiz, &v) != 0) return -1;
    dcbaa = (uint64_t *)v;

    /* Scratchpad tamponlari */
    uint32_t sp_say = ((hcs2 >> 21) & 0x1F) | (((hcs2 >> 27) & 0x1F) << 5);
    if (sp_say) {
        if (sp_say > 255) return -1;
        uint64_t dizi_fiz; void *dv;
        if (dma_sayfa(&dizi_fiz, &dv) != 0) return -1;
        uint64_t *dizi = (uint64_t *)dv;
        for (uint32_t i = 0; i < sp_say; i++) {
            uint64_t f = pmm_alloc_frame();
            if (!f) return -1;
            dizi[i] = f;
        }
        dcbaa[0] = dizi_fiz;
    }
    w64(op, OP_DCBAAP, dcbaa_fiz);

    if (halka_kur(&komut) != 0) return -1;
    w64(op, OP_CRCR, komut.fiz | 1u);                /* RCS=1 */

    uint64_t erst_fiz; void *ev;
    if (dma_sayfa(&erst_fiz, &ev) != 0) return -1;
    uint32_t *erst = (uint32_t *)ev;
    if (dma_sayfa(&olay_fiz, &ev) != 0) return -1;
    olay = (trb_t *)ev; olay_deq = 0; olay_ccs = 1;
    erst[0] = (uint32_t)olay_fiz; erst[1] = (uint32_t)(olay_fiz >> 32);
    erst[2] = HALKA_BOYUT;
    w32(rt, 0x20 + 0x08, 1);                          /* ERSTSZ */
    w64(rt, 0x20 + 0x18, olay_fiz);                   /* ERDP */
    w64(rt, 0x20 + 0x10, erst_fiz);                   /* ERSTBA */

    if (dma_sayfa(&tamp_fiz, &v) != 0) return -1;
    tamp = (uint8_t *)v;
    if (dma_sayfa(&kb_rapor_fiz, &v) != 0) return -1;
    kb_rapor = (uint8_t *)v;

    w32(op, OP_USBCMD, CMD_RS);
    for (int i = 0; i < 100 && (r32(op, OP_USBSTS) & STS_HCH); i++) bekle_ms(1);
    if (r32(op, OP_USBSTS) & STS_HCH) return -1;
    return 0;
}

int xhci_init(void) {
    hazir = 0; klavye_hazir = 0;
    for (int i = 0; i < pci_device_count(); i++) {
        const pci_device_t *d = pci_get_device(i);
        if (d->class_code != 0x0C || d->subclass != 0x03 || d->prog_if != 0x30) continue;
        if (denetleyici_baslat(d) != 0) {
            serial_write("XHCI: denetleyici baslatilamadi\n");
            continue;
        }
        hazir = 1;
        serial_write("XHCI: denetleyici calisiyor, port=");
        serial_write_hex((uint32_t)max_port);
        serial_write(" baglam=");
        serial_write_hex((uint32_t)ctx_boyut);
        serial_write("\n");

        bekle_ms(300);                      /* port algilama/oturma suresi */
        for (int p = 0; p < max_port && !klavye_hazir; p++) {
            if (!(r32(op, OP_PORTSC(p)) & PORT_CCS)) continue;
            int hiz = port_sifirla(p);
            if (hiz <= 0) continue;
            serial_write("XHCI: port ");
            serial_write_hex((uint32_t)p);
            serial_write(" cihaz, hiz=");
            serial_write_hex((uint32_t)hiz);
            serial_write("\n");
            static cihaz_t c;
            if (cihaz_ac(&c, p, hiz) != 0) { serial_write("XHCI: cihaz acilamadi\n"); continue; }
            if (klavye_kur(&c) == 0) {
                klavye_hazir = 1;
                serial_write("XHCI: USB klavye hazir (BOOT kipi)\n");
                log_write(LOG_BILGI, "XHCI", "USB klavye hazir");
            }
        }
        if (klavye_hazir) break;
    }
    return hazir ? 0 : -1;
}

int xhci_var(void)        { return hazir; }
int xhci_klavye_var(void) { return klavye_hazir; }

static void rapor_isle(const uint8_t *r) {
    int shift = (r[0] & 0x22) ? 1 : 0;
    for (int i = 2; i < 8; i++) {
        uint8_t k = r[i];
        if (k == 0 || k == 1) continue;
        int eskiydi = 0;
        for (int j = 2; j < 8; j++) if (onceki[j] == k) eskiydi = 1;
        if (eskiydi) continue;
        if (k == 0x39) { caps ^= 1; continue; }
        uint8_t sc = hid_tarama(k);
        if (!sc) continue;
        int s = shift;
        if (harf_tusu(k) && caps) s ^= 1;
        char c = keyboard_scancode_to_char(sc, s);
        if (c) {
            keyboard_push_char(c);
            static int gunluk = 0;
            if (gunluk < 24) {
                gunluk++;
                serial_write("XHCI: tus hid=");
                serial_write_hex(k);
                serial_write(" kod=");
                serial_write_hex((uint8_t)c);
                serial_write("\n");
            }
        }
    }
    for (int i = 0; i < 8; i++) onceki[i] = r[i];
}

void xhci_yokla(void) {
    if (!klavye_hazir) return;
    trb_t e;
    while (olay_al(&e)) {
        if (((e.kontrol >> 10) & 0x3F) != EVT_TRANSFER) continue;
        if ((int)(e.kontrol >> 24) != kb_slot) continue;
        int cc = (int)(e.durum >> 24);
        if (cc == CC_BASARI || cc == CC_KISA) {
            rapor_isle(kb_rapor);
            for (int i = 0; i < 8; i++) kb_rapor[i] = 0;
            kb_sirala();
        } else if (hata_say < 64) {
            hata_say++;
            kb_sirala();                    /* gecici hata: yeniden sirala */
        }
    }
}
