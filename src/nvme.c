/* =====================================================================
 * NVMe sürücüsü (1.4.9: veri yolu).
 *
 * Eskiden yalnız denetleyici tanınıyordu. Şimdi:
 *   - denetleyici sıfırlanır, yönetim (admin) kuyruk çifti kurulur,
 *   - IDENTIFY ile ad alanı 1'in boyu ve blok boyu öğrenilir,
 *   - bir G/Ç kuyruk çifti (tamamlama + gönderim) oluşturulur,
 *   - READ / WRITE (+ FLUSH) komutlarıyla 512 baytlık sektör okunur, yazılır.
 * Kesme yok: tamamlama kuyruğu faz biti ile yoklanır. Tek komut, tek
 * 4 KB DMA sayfası (en çok 8 sektör); disk katmanı daha büyük istekleri
 * parçalar. Yalnız 512 baytlık blok biçimi desteklenir (yaygın olan);
 * 4 KB blok biçimli ad alanı REDDEDİLİR (sessiz yanlış sektör yok).
 * ===================================================================== */
#include "nvme.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "depo_kilit.h"

#define NVME_REG_CAP   0x00  /* yetenekler (64-bit) */
#define NVME_REG_VS    0x08  /* surum */
#define NVME_REG_CC    0x14  /* denetleyici yapilandirmasi */
#define NVME_REG_CSTS  0x1C  /* denetleyici durumu */
#define NVME_REG_AQA   0x24  /* yonetim kuyrugu boylari */
#define NVME_REG_ASQ   0x28  /* yonetim gonderim kuyrugu (64-bit) */
#define NVME_REG_ACQ   0x30  /* yonetim tamamlama kuyrugu (64-bit) */

#define NVME_VIRT_BASE 0xA10000000ULL
#define NVME_YAPI_VIRT 0xA50000000ULL   /* kuyruklar + DMA sayfasi (UHCI 0xA30000000 ile cakisiyordu) */
#define NVME_MMIO_SAYFA 4               /* 16 KB: kayitlar + kapi zilleri */

#define KUYRUK 16                        /* her kuyrukta 16 girdi */

static nvme_controller_t ctrl;

typedef struct __attribute__((packed)) {
    uint32_t cdw0;            /* [7:0] komut, [31:16] komut kimligi */
    uint32_t nsid;
    uint64_t rsv;
    uint64_t mptr;
    uint64_t prp1, prp2;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} nvme_komut_t;
_Static_assert(sizeof(nvme_komut_t) == 64, "NVMe gonderim girdisi 64 bayt");

typedef struct __attribute__((packed)) {
    uint32_t dw0, dw1;
    uint16_t sq_bas, sq_kimlik;
    uint16_t komut_kimlik;
    uint16_t durum;           /* bit0 = faz, [15:1] durum kodu */
} nvme_tamam_t;
_Static_assert(sizeof(nvme_tamam_t) == 16, "NVMe tamamlama girdisi 16 bayt");

typedef struct {
    nvme_komut_t *sq; uint64_t sq_fiz;
    nvme_tamam_t *cq; uint64_t cq_fiz;
    uint16_t sq_kuyruk, cq_bas;   /* gonderim kuyrugu sonu, tamamlama basi */
    uint8_t  faz;
    uint16_t qid;
} kuyruk_t;

static kuyruk_t yonetim, gc;          /* yonetim ve G/C kuyruk ciftleri */
static uint8_t *veri; static uint64_t veri_fiz;
static uint64_t sonraki_virt = NVME_YAPI_VIRT;
static uint16_t komut_sayac = 1;
static int hazir = 0;
static uint64_t ad_sektor = 0;        /* ad alani 1: sektor sayisi */

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}
static void mmio_write32(uint64_t base, uint32_t offset, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + offset) = v;
}
static uint64_t mmio_read64(uint64_t base, uint32_t offset) {
    uint64_t lo = mmio_read32(base, offset);
    uint64_t hi = mmio_read32(base, offset + 4);
    return (hi << 32) | lo;
}
static void mmio_write64(uint64_t base, uint32_t offset, uint64_t v) {
    mmio_write32(base, offset, (uint32_t)v);
    mmio_write32(base, offset + 4, (uint32_t)(v >> 32));
}

/* DMA icin onbelleksiz (PCD), sifirlanmis bir sayfa */
static void *dma_sayfa(uint64_t *fiz) {
    uint64_t f = pmm_alloc_frame();
    if (!f) return 0;
    uint64_t v = sonraki_virt; sonraki_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) { pmm_free_frame(f); return 0; }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f;
    return p;
}

static uint32_t zil(uint16_t qid, int tamamlama) {
    return 0x1000u + (uint32_t)(2u * qid + (tamamlama ? 1u : 0u)) * (4u << ctrl.doorbell_stride);
}

/* Komutu gonderir, tamamlanmasini (faz biti) bekler. 0 basari, -1 hata */
static int komut_ver(kuyruk_t *q, nvme_komut_t *k) {
    uint16_t kimlik = komut_sayac++;
    if (komut_sayac == 0) komut_sayac = 1;
    k->cdw0 = (k->cdw0 & 0xFFFF) | ((uint32_t)kimlik << 16);
    q->sq[q->sq_kuyruk] = *k;
    q->sq_kuyruk = (uint16_t)((q->sq_kuyruk + 1) % KUYRUK);
    __asm__ volatile ("mfence" ::: "memory");
    mmio_write32(ctrl.mmio_base, zil(q->qid, 0), q->sq_kuyruk);
    for (uint32_t i = 0; i < 50000000u; i++) {
        volatile nvme_tamam_t *t = &q->cq[q->cq_bas];
        uint16_t d = t->durum;
        if ((d & 1) != q->faz) { __asm__ volatile ("pause"); continue; }
        uint16_t kk = t->komut_kimlik;
        q->cq_bas = (uint16_t)((q->cq_bas + 1) % KUYRUK);
        if (q->cq_bas == 0) q->faz ^= 1;
        mmio_write32(ctrl.mmio_base, zil(q->qid, 1), q->cq_bas);
        if (kk != kimlik) { serial_write("nvme: beklenmeyen tamamlama kimligi\n"); return -1; }
        if ((d >> 1) != 0) {
            serial_write("nvme: komut hatasi durum=");
            serial_write_hex((uint32_t)(d >> 1));
            serial_write("\n");
            return -1;
        }
        return 0;
    }
    serial_write("nvme: komut zaman asimi\n");
    return -1;
}

static void kuyruk_sifirla(kuyruk_t *q, uint16_t qid) {
    q->sq_kuyruk = 0; q->cq_bas = 0; q->faz = 1; q->qid = qid;
}

static int csts_bekle(int rdy) {
    /* CAP.TO: 500 ms birimi; yoklama dongusu kaba ust sinir */
    uint32_t to = (uint32_t)((ctrl.capabilities >> 24) & 0xFF) + 1;
    for (uint64_t i = 0; i < (uint64_t)to * 20000000ULL; i++) {
        uint32_t s = mmio_read32(ctrl.mmio_base, NVME_REG_CSTS);
        if (s & 2) { serial_write("nvme: denetleyici olumcul hata (CSTS.CFS)\n"); return -1; }
        if ((int)(s & 1) == rdy) return 0;
        __asm__ volatile ("pause");
    }
    serial_write("nvme: CSTS.RDY zaman asimi\n");
    return -1;
}

static int veri_yolunu_kur(void) {
    uint64_t b = ctrl.mmio_base;
    /* 1) sifirla */
    uint32_t cc = mmio_read32(b, NVME_REG_CC);
    if (cc & 1) { mmio_write32(b, NVME_REG_CC, cc & ~1u); if (csts_bekle(0) != 0) return -1; }
    /* 2) yonetim kuyruklari */
    yonetim.sq = (nvme_komut_t *)dma_sayfa(&yonetim.sq_fiz);
    yonetim.cq = (nvme_tamam_t *)dma_sayfa(&yonetim.cq_fiz);
    gc.sq = (nvme_komut_t *)dma_sayfa(&gc.sq_fiz);
    gc.cq = (nvme_tamam_t *)dma_sayfa(&gc.cq_fiz);
    veri = (uint8_t *)dma_sayfa(&veri_fiz);
    if (!yonetim.sq || !yonetim.cq || !gc.sq || !gc.cq || !veri) { serial_write("nvme: DMA bellegi yok\n"); return -1; }
    kuyruk_sifirla(&yonetim, 0);
    mmio_write32(b, NVME_REG_AQA, ((uint32_t)(KUYRUK - 1) << 16) | (KUYRUK - 1));
    mmio_write64(b, NVME_REG_ASQ, yonetim.sq_fiz);
    mmio_write64(b, NVME_REG_ACQ, yonetim.cq_fiz);
    /* 3) etkinlestir: IOCQES=4 (16 B), IOSQES=6 (64 B), MPS=0 (4 KB), NVM komut seti */
    mmio_write32(b, NVME_REG_CC, (4u << 20) | (6u << 16) | 1u);
    if (csts_bekle(1) != 0) return -1;

    /* 4) IDENTIFY ad alani 1 (CNS=0) */
    nvme_komut_t k;
    for (unsigned i = 0; i < sizeof k; i++) ((uint8_t *)&k)[i] = 0;
    k.cdw0 = 0x06; k.nsid = 1; k.prp1 = veri_fiz; k.cdw10 = 0;
    if (komut_ver(&yonetim, &k) != 0) { serial_write("nvme: IDENTIFY basarisiz\n"); return -1; }
    uint64_t nsze = *(volatile uint64_t *)veri;
    uint8_t flbas = veri[26] & 0x0F;
    uint32_t lbaf = *(volatile uint32_t *)(veri + 128 + 4u * flbas);
    uint32_t lbads = (lbaf >> 16) & 0xFF;
    serial_write("nvme: ad alani 1: blok=");
    serial_write_hex(1u << lbads);
    serial_write(" sayi=");
    serial_write_hex((uint32_t)(nsze >> 32)); serial_write_hex((uint32_t)nsze);
    serial_write("\n");
    if (lbads != 9) { serial_write("nvme: yalniz 512 baytlik blok destekleniyor - veri yolu kapali\n"); return -1; }
    ad_sektor = nsze;

    /* 5) G/C tamamlama kuyrugu (qid 1, bitisik, kesmesiz) */
    kuyruk_sifirla(&gc, 1);
    for (unsigned i = 0; i < sizeof k; i++) ((uint8_t *)&k)[i] = 0;
    k.cdw0 = 0x05; k.prp1 = gc.cq_fiz; k.cdw10 = ((uint32_t)(KUYRUK - 1) << 16) | 1u; k.cdw11 = 1u;
    if (komut_ver(&yonetim, &k) != 0) { serial_write("nvme: G/C tamamlama kuyrugu kurulamadi\n"); return -1; }
    /* 6) G/C gonderim kuyrugu (qid 1 -> tamamlama 1) */
    for (unsigned i = 0; i < sizeof k; i++) ((uint8_t *)&k)[i] = 0;
    k.cdw0 = 0x01; k.prp1 = gc.sq_fiz; k.cdw10 = ((uint32_t)(KUYRUK - 1) << 16) | 1u; k.cdw11 = (1u << 16) | 1u;
    if (komut_ver(&yonetim, &k) != 0) { serial_write("nvme: G/C gonderim kuyrugu kurulamadi\n"); return -1; }
    hazir = 1;
    serial_write("nvme: veri yolu hazir (yonetim + G/C kuyruk cifti)\n");
    return 0;
}

int nvme_init(void) {
    ctrl.present = 0;

    const pci_device_t *dev = pci_find_by_class(0x01, 0x08); /* NVMe */
    if (!dev) {
        serial_write("nvme: PCI'de NVMe denetleyici yok\n");
        return -1;
    }
    {   /* bellek alani + veri yolu hakimiyeti (DMA) */
        uint32_t cmd = pci_config_read32(dev->bus, dev->slot, dev->func, 0x04);
        pci_config_write32(dev->bus, dev->slot, dev->func, 0x04, (cmd & 0xFFFF) | (1u << 2) | (1u << 1));
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
    serial_write("\n");

    /* kapi zilleri haritalanan 16 KB icinde mi (qid 1 tamamlama zili) */
    if (zil(1, 1) + 4 > NVME_MMIO_SAYFA * 4096u || ctrl.max_queue_entries < KUYRUK) {
        serial_write("nvme: kapi zili adimi/kuyruk boyu desteklenmiyor - veri yolu kapali\n");
        return 0;
    }
    if (veri_yolunu_kur() != 0) { hazir = 0; return 0; }
    ctrl.ready = 1;
    return 0;
}

const nvme_controller_t *nvme_get_controller(void) {
    return ctrl.present ? &ctrl : 0;
}

int nvme_hazir(void) { return hazir; }
uint64_t nvme_sektor_sayisi(void) { return hazir ? ad_sektor : 0; }

/* opkod: 0x02 okuma, 0x01 yazma (ardindan 0x00 FLUSH) */
static int gc_komut(uint8_t opkod, uint64_t lba, uint32_t sayi) {
    nvme_komut_t k;
    for (unsigned i = 0; i < sizeof k; i++) ((uint8_t *)&k)[i] = 0;
    k.cdw0 = opkod; k.nsid = 1; k.prp1 = veri_fiz;
    k.cdw10 = (uint32_t)lba; k.cdw11 = (uint32_t)(lba >> 32);
    k.cdw12 = sayi - 1u;
    return komut_ver(&gc, &k);
}

int nvme_oku(uint64_t lba, uint32_t sayi, void *tampon) {
    if (!hazir || sayi == 0 || sayi > 8 || lba + sayi > ad_sektor) return -1;
    ozkilit_al(&depo_kilit);
    int r = gc_komut(0x02, lba, sayi);
    if (r == 0) { uint8_t *c = (uint8_t *)tampon; for (uint32_t i = 0; i < sayi * 512u; i++) c[i] = veri[i]; }
    ozkilit_birak(&depo_kilit);
    return r;
}

int nvme_yaz(uint64_t lba, uint32_t sayi, const void *tampon) {
    if (!hazir || sayi == 0 || sayi > 8 || lba + sayi > ad_sektor) return -1;
    ozkilit_al(&depo_kilit);
    const uint8_t *g = (const uint8_t *)tampon;
    for (uint32_t i = 0; i < sayi * 512u; i++) veri[i] = g[i];
    int r = gc_komut(0x01, lba, sayi);
    if (r == 0) {                                  /* FLUSH: aygit onbelleginde kalmasin */
        nvme_komut_t k;
        for (unsigned i = 0; i < sizeof k; i++) ((uint8_t *)&k)[i] = 0;
        k.cdw0 = 0x00; k.nsid = 1;
        r = komut_ver(&gc, &k);
    }
    ozkilit_birak(&depo_kilit);
    return r;
}
