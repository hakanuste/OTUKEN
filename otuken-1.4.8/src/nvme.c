#include "nvme.h"
#include "pci.h"
#include "vmm.h"
#include "serial.h"
#include "pmm.h"
#include "depo_kilit.h"
#include "gecikme.h"

#define NVME_REG_CAP   0x00  /* yetenekler (64-bit) */
#define NVME_REG_VS    0x08  /* surum */
#define NVME_REG_CC    0x14  /* denetleyici yapilandirmasi */
#define NVME_REG_CSTS  0x1C  /* denetleyici durumu */

#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30
#define NVME_VIRT_BASE 0xA10000000ULL
#define NVME_MMIO_SAYFA 4          /* kayitlar + ilk doorbell'lar */
#define NVME_KUYRUK    16          /* admin ve I/O kuyruk girisi sayisi */
#define NVME_ZAMAN_US  5000000u

static nvme_controller_t ctrl;

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}
static uint64_t mmio_read64(uint64_t base, uint32_t offset) {
    uint64_t lo = mmio_read32(base, offset);
    uint64_t hi = mmio_read32(base, offset + 4);
    return (hi << 32) | lo;
}

static int nvme_basladi = 0;
static int nvme_sonuc = -1;
static int nvme_init__ic(void);
static int nvme_kuyruk_kur(void);
int nvme_init(void) {
    if (nvme_basladi) return nvme_sonuc;   /* tekrar cagri guvenli */
    nvme_basladi = 1;
    nvme_sonuc = nvme_init__ic();
    return nvme_sonuc;
}
static int nvme_init__ic(void) {
    ctrl.present = 0;

    const pci_device_t *dev = pci_find_by_class(0x01, 0x08); /* NVMe */
    if (!dev) {
        serial_write("nvme: PCI'de NVMe denetleyici yok\n");
        return -1;
    }

    /* NVMe MMIO tabani BAR0/BAR1 (64-bit BAR olabilir) */
    uint64_t mmio_phys = (uint64_t)(dev->bar[0] & 0xFFFFFFF0);
    if ((dev->bar[0] & 0x6) == 0x4) { /* 64-bit BAR isaretcisi */
        mmio_phys |= ((uint64_t)dev->bar[1]) << 32;
    }
    if (mmio_phys == 0) {
        serial_write("nvme: BAR0 bos - denetleyici yapilandirilmamis\n");
        return -1;
    }

    for (int i = 0; i < NVME_MMIO_SAYFA; i++) {
        if (vmm_map_page(NVME_VIRT_BASE + (uint64_t)i * 4096,
                          mmio_phys + (uint64_t)i * 4096,
                          VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
            serial_write("nvme: MMIO haritalama basarisiz\n");
            return -1;
        }
    }

    ctrl.mmio_base         = NVME_VIRT_BASE;
    ctrl.capabilities      = mmio_read64(NVME_VIRT_BASE, NVME_REG_CAP);
    ctrl.version           = mmio_read32(NVME_VIRT_BASE, NVME_REG_VS);
    ctrl.controller_config = mmio_read32(NVME_VIRT_BASE, NVME_REG_CC);
    ctrl.controller_status = mmio_read32(NVME_VIRT_BASE, NVME_REG_CSTS);
    ctrl.max_queue_entries = (uint32_t)((ctrl.capabilities & 0xFFFF) + 1);
    ctrl.doorbell_stride   = (uint32_t)((ctrl.capabilities >> 32) & 0x0F);
    ctrl.ready             = (ctrl.controller_status & 1) ? 1 : 0;
    ctrl.present           = 1;

    serial_write("nvme: denetleyici bulundu, MMIO(fiziksel)=");
    serial_write_hex((uint32_t)mmio_phys);
    serial_write(" surum=");
    serial_write_hex(ctrl.version);
    serial_write("\n");
    serial_write("nvme: max_kuyruk_girisi=");
    serial_write_hex(ctrl.max_queue_entries);
    serial_write(" doorbell_stride=");
    serial_write_hex(ctrl.doorbell_stride);
    serial_write(" CSTS.RDY=");
    serial_write_hex((uint32_t)ctrl.ready);
    serial_write("\n");

    /* Bus Master (DMA) + Memory Space: kapaliysa kuyruk DMA'si calismaz. */
    {
        uint32_t cmd_reg = pci_config_read32(dev->bus, dev->slot, dev->func, 0x04);
        cmd_reg |= (1u << 2) | (1u << 1);
        pci_config_write32(dev->bus, dev->slot, dev->func, 0x04, cmd_reg);
    }
    /* Kuyruk kurulumu basarisiz olsa da denetleyici BULUNDU sayilir
     * (nvme_hazir() surucunun kullanilabilirligini ayrica bildirir). */
    if (nvme_kuyruk_kur() != 0)
        serial_write("nvme: kuyruk kurulumu basarisiz - surucu kullanilamaz\n");
    return 0;
}

const nvme_controller_t *nvme_get_controller(void) {
    return ctrl.present ? &ctrl : 0;
}

/* ===================== NVMe SURUCUSU: kuyruklar + G/C =====================
 * Tek admin + tek I/O kuyruk cifti (kuyruk kimligi 1), yoklamali (kesmesiz),
 * isim alani 1. Veri aktarimi PRP1 ile tek 4 KB sayfadan gecer (en fazla
 * 4096 bayt/komut). Bellek kimlik haritali oldugundan fiziksel adres =
 * sanal adres; x86'da DMA onbellek tutarlidir. */
typedef struct __attribute__((packed)) {
    uint32_t cdw0, nsid, rsv0, rsv1;
    uint64_t mptr, prp1, prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} nvme_sqe_t;

typedef struct __attribute__((packed)) {
    uint32_t dw0, dw1;
    uint16_t sq_head, sq_id;
    uint16_t cid;
    uint16_t durum;       /* bit0 = faz, bit1..15 = durum alani */
} nvme_cqe_t;

static nvme_sqe_t *asq, *iosq;
static volatile nvme_cqe_t *acq, *iocq;
static uint16_t a_tail = 0, a_head = 0, a_faz = 1;
static uint16_t io_tail = 0, io_head = 0, io_faz = 1;
static uint16_t sonraki_cid = 1;
static uint8_t *nvme_veri;            /* 4 KB DMA tamponu */
static uint8_t *nvme_kimlik_tamponu;  /* 4 KB IDENTIFY tamponu */
static int      nvme_kullanilabilir = 0;
static uint32_t nvme_blok_boyu = 512;
static uint64_t nvme_blok_sayisi = 0;
static uint32_t nvme_dstrd = 0;

static void nvme_w32(uint32_t ofs, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(NVME_VIRT_BASE + ofs) = v;
}
static void nvme_w64(uint32_t ofs, uint64_t v) {
    nvme_w32(ofs, (uint32_t)(v & 0xFFFFFFFFu));
    nvme_w32(ofs + 4, (uint32_t)(v >> 32));
}
static uint32_t nvme_r32(uint32_t ofs) { return mmio_read32(NVME_VIRT_BASE, ofs); }

static void *nvme_sayfa(void) {
    uint64_t f = pmm_alloc_frame();
    if (f == 0) return 0;
    uint8_t *p = (uint8_t *)(uintptr_t)f;      /* kimlik haritasi */
    for (int i = 0; i < 4096; i++) p[i] = 0;
    return p;
}

static int nvme_rdy_bekle(uint32_t istenen) {
    for (uint32_t t = 0; t < NVME_ZAMAN_US; t += 100) {
        if ((nvme_r32(NVME_REG_CSTS) & 1u) == istenen) return 0;
        pit2_bekle_us(100);
    }
    return -1;
}

static void nvme_kapi(uint32_t kuyruk_id, int cq, uint32_t deger) {
    uint32_t ofs = 0x1000 + (2u * kuyruk_id + (cq ? 1u : 0u)) * (4u << nvme_dstrd);
    nvme_w32(ofs, deger);
}

/* Komut gonder, tamamlanmasini yokla. kuyruk: 0=admin, 1=I/O. 0=basari. */
static int nvme_komut(int kuyruk, nvme_sqe_t *k) {
    nvme_sqe_t *sq = kuyruk ? iosq : asq;
    volatile nvme_cqe_t *cq = kuyruk ? iocq : acq;
    uint16_t *tail = kuyruk ? &io_tail : &a_tail;
    uint16_t *head = kuyruk ? &io_head : &a_head;
    uint16_t *faz  = kuyruk ? &io_faz  : &a_faz;
    uint16_t cid = sonraki_cid++;
    if (sonraki_cid == 0) sonraki_cid = 1;

    k->cdw0 = (k->cdw0 & 0xFFFFu) | ((uint32_t)cid << 16);
    sq[*tail] = *k;
    *tail = (uint16_t)((*tail + 1) % NVME_KUYRUK);
    nvme_kapi((uint32_t)kuyruk, 0, *tail);          /* SQ tail doorbell */

    for (uint32_t t = 0; t < NVME_ZAMAN_US; t += 20) {
        uint16_t durum = cq[*head].durum;
        if ((durum & 1u) == *faz) {
            uint16_t sc = (uint16_t)(durum >> 1);
            *head = (uint16_t)((*head + 1) % NVME_KUYRUK);
            if (*head == 0) *faz ^= 1u;
            nvme_kapi((uint32_t)kuyruk, 1, *head);  /* CQ head doorbell */
            if (sc != 0) {
                serial_write("nvme: komut hata durumu=");
                serial_write_hex(sc);
                serial_write("\n");
                return -1;
            }
            return 0;
        }
        pit2_bekle_us(20);
    }
    serial_write("nvme: komut zaman asimi\n");
    return -1;
}

static int nvme_kuyruk_kur(void) {
    nvme_kullanilabilir = 0;
    if (!ctrl.present) return -1;
    uint64_t cap = ctrl.capabilities;
    nvme_dstrd = (uint32_t)((cap >> 32) & 0xF);
    if ((cap >> 48 & 0xF) != 0) {          /* MPSMIN > 0: 4 KB sayfa desteklenmiyor */
        serial_write("nvme: 4 KB bellek sayfasi desteklenmiyor\n");
        return -1;
    }
    if (!(cap & (1ULL << 37))) {           /* CSS: NVM komut seti */
        serial_write("nvme: NVM komut seti yok\n");
        return -1;
    }

    /* Denetleyiciyi kapat */
    if (nvme_r32(NVME_REG_CC) & 1u) {
        nvme_w32(NVME_REG_CC, nvme_r32(NVME_REG_CC) & ~1u);
        if (nvme_rdy_bekle(0) != 0) { serial_write("nvme: kapatma zaman asimi\n"); return -1; }
    }

    asq  = (nvme_sqe_t *)nvme_sayfa();
    acq  = (volatile nvme_cqe_t *)nvme_sayfa();
    iosq = (nvme_sqe_t *)nvme_sayfa();
    iocq = (volatile nvme_cqe_t *)nvme_sayfa();
    nvme_veri = (uint8_t *)nvme_sayfa();
    nvme_kimlik_tamponu = (uint8_t *)nvme_sayfa();
    if (!asq || !acq || !iosq || !iocq || !nvme_veri || !nvme_kimlik_tamponu) {
        serial_write("nvme: kuyruk bellegi ayrilamadi\n");
        return -1;
    }
    a_tail = a_head = io_tail = io_head = 0;
    a_faz = io_faz = 1;

    nvme_w32(NVME_REG_AQA, ((NVME_KUYRUK - 1) << 16) | (NVME_KUYRUK - 1));
    nvme_w64(NVME_REG_ASQ, (uint64_t)(uintptr_t)asq);
    nvme_w64(NVME_REG_ACQ, (uint64_t)(uintptr_t)acq);
    /* CC: EN=1, CSS=0 (NVM), MPS=0 (4 KB), AMS=0, IOSQES=6 (64 B), IOCQES=4 (16 B) */
    nvme_w32(NVME_REG_CC, 1u | (6u << 16) | (4u << 20));
    if (nvme_rdy_bekle(1) != 0) { serial_write("nvme: RDY zaman asimi\n"); return -1; }

    /* I/O tamamlama kuyrugu (kimlik 1), sonra gonderim kuyrugu */
    nvme_sqe_t k;
    uint8_t *kb = (uint8_t *)&k;
    for (unsigned i = 0; i < sizeof(k); i++) kb[i] = 0;
    k.cdw0 = 0x05;                                  /* Create I/O CQ */
    k.prp1 = (uint64_t)(uintptr_t)iocq;
    k.cdw10 = ((NVME_KUYRUK - 1) << 16) | 1u;
    k.cdw11 = 1u;                                   /* PC=1, kesme kapali */
    if (nvme_komut(0, &k) != 0) { serial_write("nvme: I/O CQ olusturulamadi\n"); return -1; }

    for (unsigned i = 0; i < sizeof(k); i++) kb[i] = 0;
    k.cdw0 = 0x01;                                  /* Create I/O SQ */
    k.prp1 = (uint64_t)(uintptr_t)iosq;
    k.cdw10 = ((NVME_KUYRUK - 1) << 16) | 1u;
    k.cdw11 = (1u << 16) | 1u;                      /* CQID=1, PC=1 */
    if (nvme_komut(0, &k) != 0) { serial_write("nvme: I/O SQ olusturulamadi\n"); return -1; }

    /* Isim alani 1: Identify Namespace */
    for (unsigned i = 0; i < sizeof(k); i++) kb[i] = 0;
    k.cdw0 = 0x06;
    k.nsid = 1;
    k.prp1 = (uint64_t)(uintptr_t)nvme_kimlik_tamponu;
    k.cdw10 = 0;                                    /* CNS=0: isim alani */
    if (nvme_komut(0, &k) != 0) { serial_write("nvme: Identify NS basarisiz\n"); return -1; }

    uint64_t nsze = 0;
    for (int i = 7; i >= 0; i--) nsze = (nsze << 8) | nvme_kimlik_tamponu[i];
    uint8_t flbas = nvme_kimlik_tamponu[26] & 0x0F;
    uint32_t lbaf = (uint32_t)nvme_kimlik_tamponu[128 + 4 * flbas] |
                    ((uint32_t)nvme_kimlik_tamponu[129 + 4 * flbas] << 8) |
                    ((uint32_t)nvme_kimlik_tamponu[130 + 4 * flbas] << 16) |
                    ((uint32_t)nvme_kimlik_tamponu[131 + 4 * flbas] << 24);
    uint32_t lbads = (lbaf >> 16) & 0xFF;
    if (nsze == 0 || lbads < 9 || lbads > 12) {
        serial_write("nvme: isim alani 1 kullanilamaz (bos/ garip blok boyu)\n");
        return -1;
    }
    nvme_blok_boyu = 1u << lbads;
    nvme_blok_sayisi = nsze;
    nvme_kullanilabilir = 1;

    serial_write("nvme: isim alani 1 hazir, blok=");
    serial_write_hex(nvme_blok_boyu);
    serial_write(" blok_sayisi=");
    serial_write_hex((uint32_t)(nvme_blok_sayisi >> 32));
    serial_write_hex((uint32_t)nvme_blok_sayisi);
    serial_write("\n");
    return 0;
}

int nvme_hazir(void) { return nvme_kullanilabilir; }

uint64_t nvme_sektor_sayisi(void) {
    if (!nvme_kullanilabilir) return 0;
    return nvme_blok_sayisi * nvme_blok_boyu / 512;
}

/* Bir mantiksal blok G/C (blok_lba: cihazin kendi blok numarasi). */
static int nvme_blok_gc(int yaz, uint64_t blok_lba, uint32_t blok_adedi, void *tampon) {
    nvme_sqe_t k;
    uint8_t *kb = (uint8_t *)&k;
    for (unsigned i = 0; i < sizeof(k); i++) kb[i] = 0;
    k.cdw0 = yaz ? 0x01 : 0x02;
    k.nsid = 1;
    k.prp1 = (uint64_t)(uintptr_t)nvme_veri;
    k.cdw10 = (uint32_t)(blok_lba & 0xFFFFFFFFu);
    k.cdw11 = (uint32_t)(blok_lba >> 32);
    k.cdw12 = (blok_adedi - 1) & 0xFFFFu;
    uint32_t bayt = blok_adedi * nvme_blok_boyu;
    if (bayt > 4096) return -1;
    if (yaz) { const uint8_t *s = (const uint8_t *)tampon; for (uint32_t i = 0; i < bayt; i++) nvme_veri[i] = s[i]; }
    int r = nvme_komut(1, &k);
    if (r == 0 && !yaz) { uint8_t *d = (uint8_t *)tampon; for (uint32_t i = 0; i < bayt; i++) d[i] = nvme_veri[i]; }
    return r;
}

static int nvme_gc__ic(int yaz, uint64_t lba, uint32_t sayi, void *tampon) {
    if (!nvme_kullanilabilir || sayi == 0 || sayi > 8) return -1;
    if (lba + sayi > nvme_sektor_sayisi()) return -1;
    uint8_t *t = (uint8_t *)tampon;
    if (nvme_blok_boyu == 512)
        return nvme_blok_gc(yaz, lba, sayi, t);
    /* 4 KB'lik bloklar: 512 baytlik sektorler icin oku-degistir-yaz */
    uint32_t oran = nvme_blok_boyu / 512;
    static uint8_t blok[4096];
    for (uint32_t i = 0; i < sayi; i++) {
        uint64_t s = lba + i;
        uint64_t b = s / oran;
        uint32_t of = (uint32_t)(s % oran) * 512;
        if (nvme_blok_gc(0, b, 1, blok) != 0) return -1;
        if (yaz) {
            for (int z = 0; z < 512; z++) blok[of + z] = t[i * 512 + z];
            if (nvme_blok_gc(1, b, 1, blok) != 0) return -1;
        } else {
            for (int z = 0; z < 512; z++) t[i * 512 + z] = blok[of + z];
        }
    }
    return 0;
}

int nvme_oku(uint64_t lba, uint32_t sayi, void *tampon) {
    ozkilit_al(&depo_kilit);
    int r = nvme_gc__ic(0, lba, sayi, tampon);
    ozkilit_birak(&depo_kilit);
    return r;
}

int nvme_yaz(uint64_t lba, uint32_t sayi, const void *tampon) {
    ozkilit_al(&depo_kilit);
    int r = nvme_gc__ic(1, lba, sayi, (void *)tampon);
    if (r == 0) {
        nvme_sqe_t k;
        uint8_t *kb = (uint8_t *)&k;
        for (unsigned i = 0; i < sizeof(k); i++) kb[i] = 0;
        k.cdw0 = 0x00;                  /* Flush */
        k.nsid = 1;
        r = nvme_komut(1, &k);
    }
    ozkilit_birak(&depo_kilit);
    return r;
}
