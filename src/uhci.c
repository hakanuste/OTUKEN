#include "uhci.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "log.h"
#include "serial.h"
#include "hid.h"

/* --- UHCI I/O kayitlari (taban + ofset) --- */
#define UHCI_USBCMD     0x00
#define UHCI_USBSTS     0x02
#define UHCI_USBINTR    0x04
#define UHCI_FRNUM      0x06
#define UHCI_FLBASEADD  0x08
#define UHCI_SOFMOD     0x0C
#define UHCI_PORTSC1    0x10

#define CMD_RS      (1 << 0)   /* calistir/dur */
#define CMD_HCRESET (1 << 1)
#define CMD_MAXP    (1 << 7)

#define PORT_CCS    (1 << 0)   /* cihaz bagli */
#define PORT_CSC    (1 << 1)   /* baglanti degisti */
#define PORT_PE     (1 << 2)   /* port etkin */
#define PORT_RESET  (1 << 9)
#define PORT_LSDA   (1 << 8)   /* dusuk hizli cihaz */

/* --- Aktarim tanimlayicisi (TD) - 32 bayt, 16 bayt hizali --- */
typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t link;
    volatile uint32_t ctrl_sts;
    volatile uint32_t token;
    volatile uint32_t tampon;
    uint32_t yedek[4];
} uhci_td_t;

/* --- Kuyruk basligi (QH) --- */
typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t head_link;
    volatile uint32_t elem_link;
    uint32_t yedek[2];
} uhci_qh_t;

#define TD_TERM        1u
#define TD_QH_BIT      2u
#define TD_STS_ACTIVE  (1u << 23)
#define TD_STS_STALLED (1u << 22)
#define TD_CTRL_LS     (1u << 26)   /* dusuk hiz */
#define TD_CTRL_IOC    (1u << 24)
#define TD_ERR_3       (3u << 27)   /* 3 kez yeniden dene */

#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

#define UHCI_VIRT 0xA30000000ULL

static uint16_t io_taban = 0;
static int      hazir = 0;
static int      klavye_hazir = 0;
static int      klavye_dusuk_hiz = 0;
static uint8_t  klavye_adres = 0;

static uint32_t *cerceve_listesi;      /* 1024 giris, 4KB hizali */
static uint64_t  cerceve_fiz;
static uhci_qh_t *qh;                   /* kontrol/kesme kuyrugu */
static uint64_t   qh_fiz;
static uhci_td_t *td;                   /* TD havuzu (8 adet) */
static uint64_t   td_fiz;
static uint8_t   *veri;                 /* DMA veri tamponu */
static uint64_t   veri_fiz;


static uint64_t sonraki_virt = UHCI_VIRT;

static int dma_sayfa(uint64_t *fiz, void **virt) {
    uint64_t f = pmm_alloc_frame();
    if (f == 0) return -1;
    uint64_t v = sonraki_virt; sonraki_virt += 4096;
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
        pmm_free_frame(f); return -1;
    }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f; *virt = p;
    return 0;
}

/* DIKKAT: uhci_init, kesmeler ACILMADAN once calisir; bu yuzden
 * zamanlayici tikine dayali bekleme SONSUZ DONGUYE girer (gercekten
 * yasandi - sistem burada asildi). Bunun yerine donanimdan bagimsiz,
 * yogun-bekleme tabanli bir gecikme kullaniyoruz. */
static void bekle_ms(uint32_t ms) {
    /* Kaba kalibrasyon: ~1 milyon bos dongu ≈ 1ms (QEMU'da yeterli) */
    for (uint32_t m = 0; m < ms; m++) {
        for (volatile uint32_t i = 0; i < 200000; i++) { }
    }
}

/* --- TD kurulumu --- */
static void td_kur(int i, uint32_t sonraki, uint8_t pid, uint8_t adres,
                    uint8_t ep, int toggle, uint32_t uzunluk, uint64_t tampon_fiz) {
    td[i].link = sonraki;
    td[i].ctrl_sts = TD_STS_ACTIVE | TD_ERR_3 |
                     (klavye_dusuk_hiz ? TD_CTRL_LS : 0);
    uint32_t maxlen = (uzunluk == 0) ? 0x7FF : (uzunluk - 1);
    td[i].token = ((maxlen & 0x7FF) << 21) | ((uint32_t)toggle << 19) |
                  ((uint32_t)ep << 15) | ((uint32_t)adres << 8) | pid;
    td[i].tampon = (uint32_t)tampon_fiz;
}

/* TD zincirini kuyruga baglayip tamamlanmasini bekler. */
static int zinciri_calistir(int ilk_td) {
    qh->elem_link = (uint32_t)(td_fiz + (uint64_t)ilk_td * sizeof(uhci_td_t));
    for (int d = 0; d < 200; d++) {
        bekle_ms(5);
        if (!(td[ilk_td].ctrl_sts & TD_STS_ACTIVE)) {
            /* zincirin TAMAMI bitti mi: son TD'yi kontrol et */
            int hata = 0;
            for (int i = 0; i < 4; i++)
                if (td[i].ctrl_sts & TD_STS_STALLED) hata = 1;
            qh->elem_link = TD_TERM;
            return hata ? -1 : 0;
        }
    }
    qh->elem_link = TD_TERM;
    return -1;   /* zaman asimi */
}

/* --- Kontrol transferi (SETUP + DATA + STATUS) --- */
static int kontrol_transfer(uint8_t adres, const uint8_t kurulum[8],
                             uint8_t *alinan, uint32_t alinan_uzunluk) {
    for (int i = 0; i < 8; i++) veri[i] = kurulum[i];

    int n = 0;
    /* SETUP */
    td_kur(n, (uint32_t)(td_fiz + sizeof(uhci_td_t)), PID_SETUP, adres, 0, 0, 8, veri_fiz);
    n++;
    /* DATA IN (varsa) */
    if (alinan_uzunluk > 0) {
        td_kur(n, (uint32_t)(td_fiz + (uint64_t)(n + 1) * sizeof(uhci_td_t)),
               PID_IN, adres, 0, 1, alinan_uzunluk, veri_fiz + 64);
        n++;
    }
    /* STATUS: veri IN ise OUT, veri yoksa IN */
    td_kur(n, TD_TERM, (alinan_uzunluk > 0) ? PID_OUT : PID_IN, adres, 0, 1, 0, 0);
    n++;

    if (zinciri_calistir(0) != 0) return -1;

    if (alinan_uzunluk > 0 && alinan) {
        for (uint32_t i = 0; i < alinan_uzunluk; i++) alinan[i] = veri[64 + i];
    }
    return 0;
}

int uhci_init(void) {
    hazir = 0; klavye_hazir = 0;

    const pci_device_t *d = 0;
    for (int i = 0; i < pci_device_count(); i++) {
        const pci_device_t *p = pci_get_device(i);
        if (p->class_code == 0x0C && p->subclass == 0x03 && p->prog_if == 0x00) {
            d = p; break;
        }
    }
    if (!d) { log_write(LOG_BILGI, "UHCI", "UHCI denetleyici yok"); return -1; }

    io_taban = (uint16_t)(d->bar[4] & ~0x3u);
    if (io_taban == 0) return -1;

    /* PCI Bus Master ac - DMA icin SART */
    uint32_t cmd = pci_config_read32(d->bus, d->slot, d->func, 0x04);
    pci_config_write32(d->bus, d->slot, d->func, 0x04, cmd | (1u << 2) | 1u);

    void *v;
    if (dma_sayfa(&cerceve_fiz, &v) != 0) return -1;
    cerceve_listesi = (uint32_t *)v;
    if (dma_sayfa(&qh_fiz, &v) != 0) return -1;
    qh = (uhci_qh_t *)v;
    if (dma_sayfa(&td_fiz, &v) != 0) return -1;
    td = (uhci_td_t *)v;
    if (dma_sayfa(&veri_fiz, &v) != 0) return -1;
    veri = (uint8_t *)v;

    /* Denetleyiciyi sifirla */
    outw(io_taban + UHCI_USBCMD, CMD_HCRESET);
    bekle_ms(50);
    outw(io_taban + UHCI_USBCMD, 0);
    outw(io_taban + UHCI_USBINTR, 0);

    /* Kuyruk basligini kur, cerceve listesinin TUM girislerini ona baglat */
    qh->head_link = TD_TERM;
    qh->elem_link = TD_TERM;
    for (int i = 0; i < 1024; i++)
        cerceve_listesi[i] = (uint32_t)qh_fiz | TD_QH_BIT;

    outw(io_taban + UHCI_FRNUM, 0);
    outl(io_taban + UHCI_FLBASEADD, (uint32_t)cerceve_fiz);
    outw(io_taban + UHCI_SOFMOD, 64);
    outw(io_taban + UHCI_USBSTS, 0xFFFF);
    outw(io_taban + UHCI_USBCMD, CMD_RS | CMD_MAXP);

    hazir = 1;
    serial_write("UHCI: denetleyici hazir, I/O taban=");
    serial_write_hex(io_taban);
    serial_write("\n");

    /* --- Portlari tara, cihaz varsa numaralandir --- */
    for (int p = 0; p < 2; p++) {
        uint16_t portadr = (uint16_t)(io_taban + UHCI_PORTSC1 + p * 2);
        uint16_t st = inw(portadr);
        if (!(st & PORT_CCS)) continue;

        klavye_dusuk_hiz = (st & PORT_LSDA) ? 1 : 0;
        serial_write("UHCI: port ");
        serial_write_hex((uint32_t)p);
        serial_write(" cihaz bagli (");
        serial_write(klavye_dusuk_hiz ? "dusuk hiz" : "tam hiz");
        serial_write(")\n");

        /* Port sifirlama dizisi */
        outw(portadr, PORT_RESET);
        bekle_ms(60);
        outw(portadr, (uint16_t)(inw(portadr) & ~PORT_RESET));
        bekle_ms(20);
        outw(portadr, (uint16_t)(inw(portadr) | PORT_PE));
        bekle_ms(20);
        outw(portadr, (uint16_t)(inw(portadr) | PORT_CSC));  /* degisimi temizle */

        if (!(inw(portadr) & PORT_PE)) {
            serial_write("UHCI: port etkinlestirilemedi\n");
            continue;
        }

        /* 1) Cihaz tanimlayicisini oku (adres 0) */
        uint8_t gd[8] = {0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 8, 0};
        uint8_t tanim[8];
        if (kontrol_transfer(0, gd, tanim, 8) != 0) {
            serial_write("UHCI: cihaz tanimlayicisi okunamadi\n");
            continue;
        }

        /* 2) Adres ata */
        uint8_t sa[8] = {0x00, 0x05, 1, 0, 0, 0, 0, 0};
        if (kontrol_transfer(0, sa, 0, 0) != 0) {
            serial_write("UHCI: adres atanamadi\n");
            continue;
        }
        bekle_ms(20);
        klavye_adres = 1;

        /* 3) Yapilandirmayi etkinlestir */
        uint8_t sc[8] = {0x00, 0x09, 1, 0, 0, 0, 0, 0};
        if (kontrol_transfer(klavye_adres, sc, 0, 0) != 0) {
            serial_write("UHCI: yapilandirma ayarlanamadi\n");
            continue;
        }

        /* 4) HID BOOT protokolu (klavyeyi basit rapor kipine al) */
        uint8_t sp[8] = {0x21, 0x0B, 0, 0, 0, 0, 0, 0};
        kontrol_transfer(klavye_adres, sp, 0, 0);

        klavye_hazir = 1;
        serial_write("UHCI: USB klavye numaralandirildi (adres=1, BOOT kipi)\n");
        log_write(LOG_BILGI, "UHCI", "USB klavye hazir");
        break;
    }
    return hazir ? 0 : -1;
}

int uhci_var(void)         { return hazir; }
int uhci_klavye_var(void)  { return klavye_hazir; }

void uhci_yokla(void) {
    if (!klavye_hazir) return;

    /* Kesme transferi: EP1 IN, 8 baytlik BOOT raporu */
    static int toggle = 0;
    for (int i = 0; i < 8; i++) veri[128 + i] = 0;

    td_kur(0, TD_TERM, PID_IN, klavye_adres, 1, toggle, 8, veri_fiz + 128);
    if (zinciri_calistir(0) != 0) return;
    toggle ^= 1;

    /* 1.4.9: rapor ortak HID ceviricisine (TR-Q, Shift/AltGr, ozel tuslar,
     * birakma, yineleme); eskiden yalniz harf/rakam ve son tus tutuluyordu */
    hid_klavye_rapor(&veri[128], 8);
}

/* 1.4.9: tuslar artik hid_klavye_rapor ile dogrudan klavye kuyruguna girer */
uint8_t uhci_klavye_oku(void) { return 0; }
