#include "ahci.h"
#include "depo_kilit.h"
#include "pci.h"
#include "vmm.h"
#include "serial.h"

/* AHCI HBA (Host Bus Adapter) genel kayit ofsetleri */
#define AHCI_REG_CAP    0x00  /* yetenekler */
#define AHCI_REG_GHC    0x04  /* genel host kontrol */
#define AHCI_REG_IS     0x08  /* kesme durumu */
#define AHCI_REG_PI     0x0C  /* uygulanan portlar */
#define AHCI_REG_VS     0x10  /* surum */

/* Port kayitlari: port N -> 0x100 + N*0x80 */
#define AHCI_PORT_BASE(n)   (0x100 + (n) * 0x80)
#define AHCI_PORT_SSTS      0x28  /* SATA durumu */
#define AHCI_PORT_SIG       0x24  /* imza (cihaz turu) */

/* AHCI MMIO icin ayrilmis sanal adres (mevcut testlerin/heap'in
 * disinda, cakisma olmayan taze bir bolge) */
#define AHCI_VIRT_BASE 0xA00000000ULL

static ahci_controller_t ctrl;

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}

static int ahci_basladi = 0;
static int ahci_sonuc = -1;
static int ahci_init__ic(void);
int ahci_init(void) {
    if (ahci_basladi) return ahci_sonuc;   /* tekrar cagri guvenli */
    ahci_basladi = 1;
    ahci_sonuc = ahci_init__ic();
    return ahci_sonuc;
}
static int ahci_init__ic(void) {
    ctrl.present = 0;
    ctrl.active_port_count = 0;

    const pci_device_t *dev = pci_find_by_class(0x01, 0x06); /* SATA/AHCI */
    if (!dev) {
        serial_write("ahci: PCI'de AHCI denetleyici yok\n");
        return -1;
    }

    /* ABAR = BAR5 (AHCI spesifikasyonu geregi). Alt bitler bayrak,
     * temizlenmeli. */
    uint64_t abar_phys = (uint64_t)(dev->bar[5] & 0xFFFFFFF0);
    if (abar_phys == 0) {
        serial_write("ahci: BAR5 (ABAR) bos - denetleyici yapilandirilmamis\n");
        return -1;
    }

    /* Denetim eklemesi: Bus Master (DMA) ve Memory Space bitleri
     * yazilimca ACIK OLMALIDIR. Bazi yazilimlar (ve yan yana calisan
     * onyukleyiciler) bunu acik birakmaz; kapaliyken HBA DMA yapamaz ve
     * komutlar sessizce zaman asimina ugrar. */
    {
        uint32_t cmd_reg = pci_config_read32(dev->bus, dev->slot, dev->func, 0x04);
        cmd_reg |= (1u << 2) | (1u << 1);
        pci_config_write32(dev->bus, dev->slot, dev->func, 0x04, cmd_reg);
    }

    /* MMIO bolgesini haritala (cache DEVRE DISI olmali - MMIO'da
     * onbellek kullanmak donanim kayitlarinda yanlis/bayat deger
     * okumaya yol acar; PCD biti (bit4) bunu engeller). */
    for (int i = 0; i < 2; i++) { /* 8KB yeterli: genel kayitlar + portlar */
        if (vmm_map_page(AHCI_VIRT_BASE + (uint64_t)i * 4096,
                          abar_phys + (uint64_t)i * 4096,
                          VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
            serial_write("ahci: MMIO haritalama basarisiz\n");
            return -1;
        }
    }

    /* GHC.AE (bit31): AHCI modu acik olmali (HBA kayitlari ancak o zaman
     * gecerli). Zaten aciksa degismez. */
    {
        uint32_t ghc = mmio_read32(AHCI_VIRT_BASE, AHCI_REG_GHC);
        if (!(ghc & 0x80000000u))
            *(volatile uint32_t *)(uintptr_t)(AHCI_VIRT_BASE + AHCI_REG_GHC) = ghc | 0x80000000u;
    }
    ctrl.abar             = AHCI_VIRT_BASE;
    ctrl.capabilities     = mmio_read32(AHCI_VIRT_BASE, AHCI_REG_CAP);
    ctrl.global_host_ctrl = mmio_read32(AHCI_VIRT_BASE, AHCI_REG_GHC);
    ctrl.ports_implemented= mmio_read32(AHCI_VIRT_BASE, AHCI_REG_PI);
    ctrl.version          = mmio_read32(AHCI_VIRT_BASE, AHCI_REG_VS);
    ctrl.num_ports        = (int)((ctrl.capabilities & 0x1F) + 1);
    ctrl.present          = 1;

    serial_write("ahci: denetleyici bulundu, ABAR(fiziksel)=");
    serial_write_hex((uint32_t)abar_phys);
    serial_write(" surum=");
    serial_write_hex(ctrl.version);
    serial_write(" port_sayisi=");
    serial_write_hex((uint32_t)ctrl.num_ports);
    serial_write("\n");

    /* Hangi portlarda GERCEKTEN bagli bir surucu var? SSTS.DET==3
     * (cihaz var + iletisim kuruldu) ve IPM==1 (aktif) olmali. */
    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        ctrl.active_ports[p] = 0;
        if (!(ctrl.ports_implemented & (1u << p))) continue;

        uint32_t ssts = mmio_read32(AHCI_VIRT_BASE, (uint32_t)(AHCI_PORT_BASE(p) + AHCI_PORT_SSTS));
        uint32_t det = ssts & 0x0F;
        uint32_t ipm = (ssts >> 8) & 0x0F;
        if (det == 3 && ipm == 1) {
            uint32_t sig = mmio_read32(AHCI_VIRT_BASE, (uint32_t)(AHCI_PORT_BASE(p) + AHCI_PORT_SIG));
            ctrl.active_ports[p] = 1;
            ctrl.active_port_count++;
            ctrl.present = 1;          /* hazirlama icin gerekli */
            ahci_port_hazirla(p);      /* komut yapilarini kur */
            serial_write("ahci:   port ");
            serial_write_hex((uint32_t)p);
            serial_write(" AKTIF, imza=");
            serial_write_hex(sig);
            serial_write(sig == 0x00000101 ? " (SATA disk)\n" :
                          (sig == 0xEB140101 ? " (SATAPI/CD)\n" : " (bilinmeyen)\n"));
        }
    }

    serial_write("ahci: bagli surucu sayisi=");
    serial_write_hex((uint32_t)ctrl.active_port_count);
    serial_write("\n");
    return 0;
}

const ahci_controller_t *ahci_get_controller(void) {
    return ctrl.present ? &ctrl : 0;
}

/* ================= Gercek AHCI veri okuma (DMA) ================= */

#include "pmm.h"

/* HBA port kayitlari (port tabanina gore) */
#define PORT_CLB    0x00   /* komut listesi taban adresi */
#define PORT_CLBU   0x04
#define PORT_FB     0x08   /* FIS alani taban adresi */
#define PORT_FBU    0x0C
#define PORT_IS     0x10   /* kesme durumu */
#define PORT_IE     0x14
#define PORT_CMD    0x18   /* komut ve durum */
#define PORT_TFD    0x20   /* gorev dosyasi verisi */
#define PORT_CI     0x38   /* komut verildi (issue) */

#define PORT_CMD_ST   (1u << 0)    /* baslat */
#define PORT_CMD_FRE  (1u << 4)    /* FIS alimi etkin */
#define PORT_CMD_FR   (1u << 14)   /* FIS alimi calisiyor */
#define PORT_CMD_CR   (1u << 15)   /* komut listesi calisiyor */

#define TFD_BSY  (1u << 7)
#define TFD_DRQ  (1u << 3)
#define TFD_ERR  (1u << 0)

#define ATA_CMD_READ_DMA_EX 0x25

#define AHCI_YAPI_VIRT 0xA20000000ULL   /* komut/FIS yapilarina ayrilmis alan */

/* Komut basligi (32 bayt) - port basina 32 adet */
typedef struct __attribute__((packed)) {
    uint8_t  cfl_a_w_p;      /* [4:0]=FIS uzunlugu(dword), bit6=W(yazma) */
    uint8_t  r_p_c_b_r_pmp;
    uint16_t prdtl;          /* PRDT giris sayisi */
    volatile uint32_t prdbc; /* aktarilan bayt */
    uint32_t ctba;           /* komut tablosu adresi */
    uint32_t ctbau;
    uint32_t rsv[4];
} ahci_cmd_header_t;

/* Fiziksel bolge tanimlayicisi */
typedef struct __attribute__((packed)) {
    uint32_t dba;            /* veri tamponu adresi */
    uint32_t dbau;
    uint32_t rsv;
    uint32_t dbc_i;          /* [21:0]=bayt sayisi-1, bit31=kesme */
} ahci_prdt_t;

/* Komut tablosu: 64 bayt CFIS + 16 ACMD + 48 rezerve + PRDT */
typedef struct __attribute__((packed)) {
    uint8_t     cfis[64];
    uint8_t     acmd[16];
    uint8_t     rsv[48];
    ahci_prdt_t prdt[8];
} ahci_cmd_table_t;

typedef struct {
    int      hazir;
    uint64_t clb_fiz;        /* komut listesi (1KB hizali) */
    uint64_t fb_fiz;         /* FIS alani (256B hizali) */
    uint64_t ctb_fiz;        /* komut tablosu */
    uint64_t veri_fiz;       /* DMA veri tamponu */
    ahci_cmd_header_t *clb;
    ahci_cmd_table_t  *ctb;
    uint8_t           *veri;
} ahci_port_yapi_t;

static ahci_port_yapi_t port_yapi[AHCI_MAX_PORTS];
static uint64_t sonraki_yapi_virt = AHCI_YAPI_VIRT;

static void mw32(uint64_t taban, uint32_t ofs, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(taban + ofs) = v;
}
static uint32_t mr32p(uint64_t taban, uint32_t ofs) {
    return *(volatile uint32_t *)(uintptr_t)(taban + ofs);
}

/* DMA icin onbelleksiz (PCD) bir sayfa ayirir. */
static int dma_sayfa(uint64_t *fiz_out, void **virt_out) {
    uint64_t f = pmm_alloc_frame();
    if (f == 0) return -1;
    /* Denetim duzeltmesi: HBA 64-bit adreslemeyi (CAP.S64A, bit31)
     * desteklemiyorsa 4GB ustu bir DMA yapisi sessizce yanlis adrese
     * gider. Buyuk bellekli makinelerde cerceveler 4GB'i asabilir. */
    if (!(ctrl.capabilities & 0x80000000u) && f >= 0x100000000ULL) {
        pmm_free_frame(f);
        serial_write("ahci: HBA 64-bit DMA desteklemiyor, 4GB ustu cerceve reddedildi\n");
        return -1;
    }
    uint64_t v = sonraki_yapi_virt;
    sonraki_yapi_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
        pmm_free_frame(f);
        return -1;
    }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz_out = f;
    *virt_out = p;
    return 0;
}

static void port_durdur(uint64_t pbase) {
    uint32_t cmd = mr32p(pbase, PORT_CMD);
    cmd &= ~PORT_CMD_ST;
    cmd &= ~PORT_CMD_FRE;
    mw32(pbase, PORT_CMD, cmd);
    for (int i = 0; i < 1000000; i++) {
        uint32_t c = mr32p(pbase, PORT_CMD);
        if (!(c & PORT_CMD_CR) && !(c & PORT_CMD_FR)) break;
    }
}

static void port_baslat(uint64_t pbase) {
    for (int i = 0; i < 1000000; i++)
        if (!(mr32p(pbase, PORT_CMD) & PORT_CMD_CR)) break;
    uint32_t cmd = mr32p(pbase, PORT_CMD);
    cmd |= PORT_CMD_FRE;
    cmd |= PORT_CMD_ST;
    mw32(pbase, PORT_CMD, cmd);
}

int ahci_port_hazirla(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return -1;
    const ahci_controller_t *c = ahci_get_controller();
    if (!c) return -1;

    ahci_port_yapi_t *y = &port_yapi[port];
    if (y->hazir) return 0;

    void *v1, *v2, *v3;
    if (dma_sayfa(&y->clb_fiz, &v1) != 0) return -1;
    if (dma_sayfa(&y->fb_fiz,  &v2) != 0) return -1;
    if (dma_sayfa(&y->ctb_fiz, &v3) != 0) return -1;
    void *v4;
    if (dma_sayfa(&y->veri_fiz, &v4) != 0) return -1;

    y->clb  = (ahci_cmd_header_t *)v1;
    y->ctb  = (ahci_cmd_table_t *)v3;
    y->veri = (uint8_t *)v4;

    uint64_t pbase = c->abar + (uint64_t)AHCI_PORT_BASE(port);
    port_durdur(pbase);

    mw32(pbase, PORT_CLB,  (uint32_t)(y->clb_fiz & 0xFFFFFFFF));
    mw32(pbase, PORT_CLBU, (uint32_t)(y->clb_fiz >> 32));
    mw32(pbase, PORT_FB,   (uint32_t)(y->fb_fiz & 0xFFFFFFFF));
    mw32(pbase, PORT_FBU,  (uint32_t)(y->fb_fiz >> 32));
    mw32(pbase, PORT_IS,   0xFFFFFFFF);   /* bekleyen kesmeleri temizle */

    port_baslat(pbase);
    y->hazir = 1;

    serial_write("ahci: port ");
    serial_write_hex((uint32_t)port);
    serial_write(" komut yapilari hazir (CLB=");
    serial_write_hex((uint32_t)y->clb_fiz);
    serial_write(")\n");
    return 0;
}

/* --- Genel AHCI komut yurutucu (yuva 0, yoklamali) ---
 * yon: 0=veri yok, 1=cihazdan oku, 2=cihaza yaz. Veri tek 4 KB'lik
 * DMA sayfasindan gecer (en fazla 8 sektor). */
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_FLUSH_EX     0xEA
#define ATA_CMD_IDENTIFY_DMA 0xEC
#define PORT_SERR 0x30

static void port_hata_kurtar(uint64_t pbase) {
    /* Hata sonrasi: ST'yi kapatip yeniden ac, SERR/IS temizle. */
    uint32_t cmd = mr32p(pbase, PORT_CMD);
    mw32(pbase, PORT_CMD, cmd & ~PORT_CMD_ST);
    for (int i = 0; i < 1000000; i++) if (!(mr32p(pbase, PORT_CMD) & PORT_CMD_CR)) break;
    mw32(pbase, PORT_SERR, 0xFFFFFFFF);
    mw32(pbase, PORT_IS, 0xFFFFFFFF);
    mw32(pbase, PORT_CMD, mr32p(pbase, PORT_CMD) | PORT_CMD_ST);
}

static int ahci_calistir__ic(int port, uint8_t komut, uint64_t lba, uint32_t sayi,
                              int yon, void *tampon, uint32_t bayt) {
    if (port < 0 || port >= AHCI_MAX_PORTS) return -1;
    if (bayt > 4096) return -1;
    const ahci_controller_t *c = ahci_get_controller();
    if (!c || !port_yapi[port].hazir) return -1;
    ahci_port_yapi_t *y = &port_yapi[port];
    uint64_t pbase = c->abar + (uint64_t)AHCI_PORT_BASE(port);

    /* Surucunun bosalmasini bekle */
    int bos = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(mr32p(pbase, PORT_TFD) & (TFD_BSY | TFD_DRQ))) { bos = 1; break; }
    }
    if (!bos) { serial_write("ahci: surucu mesgul (zaman asimi)\n"); port_hata_kurtar(pbase); return -1; }
    mw32(pbase, PORT_IS, 0xFFFFFFFF);

    if (yon == 2) {
        uint8_t *v = y->veri; const uint8_t *k = (const uint8_t *)tampon;
        for (uint32_t i = 0; i < bayt; i++) v[i] = k[i];
    }

    ahci_cmd_header_t *h = &y->clb[0];
    h->cfl_a_w_p = (uint8_t)(5 | (yon == 2 ? 0x40 : 0));
    h->r_p_c_b_r_pmp = 0;
    h->prdtl = (yon != 0) ? 1 : 0;
    h->prdbc = 0;
    h->ctba  = (uint32_t)(y->ctb_fiz & 0xFFFFFFFF);
    h->ctbau = (uint32_t)(y->ctb_fiz >> 32);

    ahci_cmd_table_t *t = y->ctb;
    for (int i = 0; i < 64; i++) t->cfis[i] = 0;
    if (yon != 0) {
        t->prdt[0].dba   = (uint32_t)(y->veri_fiz & 0xFFFFFFFF);
        t->prdt[0].dbau  = (uint32_t)(y->veri_fiz >> 32);
        t->prdt[0].rsv   = 0;
        t->prdt[0].dbc_i = bayt - 1;
    }
    uint8_t *f = t->cfis;
    f[0] = 0x27; f[1] = 0x80; f[2] = komut;
    f[4] = (uint8_t)(lba & 0xFF);        f[5] = (uint8_t)((lba >> 8) & 0xFF);
    f[6] = (uint8_t)((lba >> 16) & 0xFF);
    f[7] = (komut == ATA_CMD_IDENTIFY_DMA || komut == ATA_CMD_FLUSH_EX) ? 0 : (1 << 6);
    f[8] = (uint8_t)((lba >> 24) & 0xFF); f[9] = (uint8_t)((lba >> 32) & 0xFF);
    f[10]= (uint8_t)((lba >> 40) & 0xFF);
    f[12]= (uint8_t)(sayi & 0xFF);        f[13]= (uint8_t)((sayi >> 8) & 0xFF);

    mw32(pbase, PORT_CI, 1u);

    int zamanasimi = 1;
    for (int i = 0; i < 20000000; i++) {
        if (!(mr32p(pbase, PORT_CI) & 1u)) { zamanasimi = 0; break; }
        if (mr32p(pbase, PORT_IS) & (1u << 30)) {
            serial_write("ahci: aktarim hatasi (TFES)\n");
            port_hata_kurtar(pbase);
            return -1;
        }
    }
    if (zamanasimi) { serial_write("ahci: komut zaman asimi\n"); port_hata_kurtar(pbase); return -1; }
    if (mr32p(pbase, PORT_TFD) & (TFD_ERR | (1u << 5))) {   /* ERR veya DF */
        serial_write("ahci: surucu hata bildirdi\n");
        port_hata_kurtar(pbase);
        return -1;
    }
    if (yon == 1) {
        uint8_t *cikti = (uint8_t *)tampon;
        for (uint32_t i = 0; i < bayt; i++) cikti[i] = y->veri[i];
    }
    return 0;
}

static int ahci_oku__ic(int port, uint64_t lba, uint32_t sayi, void *tampon) {
    if (sayi == 0 || sayi > 8) return -1;          /* tek sayfa siniri */
    return ahci_calistir__ic(port, ATA_CMD_READ_DMA_EX, lba, sayi, 1, tampon, sayi * 512);
}

static int ahci_yaz__ic(int port, uint64_t lba, uint32_t sayi, const void *tampon) {
    if (sayi == 0 || sayi > 8) return -1;
    if (ahci_calistir__ic(port, ATA_CMD_WRITE_DMA_EX, lba, sayi, 2, (void *)tampon, sayi * 512) != 0)
        return -1;
    /* Onbellek bosaltma: yoksa guc kesintisinde veri kaybi */
    return ahci_calistir__ic(port, ATA_CMD_FLUSH_EX, 0, 0, 0, 0, 0);
}

static int ahci_sektor_sayisi__ic(int port, uint64_t *sektor) {
    static uint16_t kimlik[256];
    if (ahci_calistir__ic(port, ATA_CMD_IDENTIFY_DMA, 0, 0, 1, kimlik, 512) != 0) return -1;
    uint64_t n;
    if (kimlik[83] & (1 << 10))
        n = (uint64_t)kimlik[100] | ((uint64_t)kimlik[101] << 16) |
            ((uint64_t)kimlik[102] << 32) | ((uint64_t)kimlik[103] << 48);
    else
        n = (uint64_t)kimlik[60] | ((uint64_t)kimlik[61] << 16);
    *sektor = n;
    return 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int ahci_oku(int port, uint64_t lba, uint32_t sayi, void *tampon) {
    ozkilit_al(&depo_kilit);
    int r = ahci_oku__ic(port, lba, sayi, tampon);
    ozkilit_birak(&depo_kilit);
    return r;
}

int ahci_yaz(int port, uint64_t lba, uint32_t sayi, const void *tampon) {
    ozkilit_al(&depo_kilit);
    int r = ahci_yaz__ic(port, lba, sayi, tampon);
    ozkilit_birak(&depo_kilit);
    return r;
}

int ahci_sektor_sayisi(int port, uint64_t *sektor) {
    ozkilit_al(&depo_kilit);
    int r = ahci_sektor_sayisi__ic(port, sektor);
    ozkilit_birak(&depo_kilit);
    return r;
}

/* sira'inci (0'dan) SATA DISK portunu dondurur (ATAPI/CD atlanir); yoksa -1. */
int ahci_disk_portu(int sira) {
    const ahci_controller_t *c = ahci_get_controller();
    if (!c) return -1;
    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!c->active_ports[p]) continue;
        if (mr32p(c->abar + (uint64_t)AHCI_PORT_BASE(p), AHCI_PORT_SIG) != 0x00000101u) continue;
        if (sira-- == 0) return p;
    }
    return -1;
}
