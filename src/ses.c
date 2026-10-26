#include "ses.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "log.h"
#include "serial.h"
#include "thread.h"
#include "timer.h"

#include "ses_kilit.h"
#include "hda.h"
ozkilit_t ses_kilit;   /* bkz. include/ses_kilit.h */

/* --- NAM (karistirici) kayitlari, nam_taban'a gore --- */
#define NAM_RESET        0x00
#define NAM_MASTER_VOL   0x02
#define NAM_PCM_VOL      0x18
#define NAM_EXT_ID       0x28
#define NAM_EXT_CTRL     0x2A
#define NAM_PCM_ORNEK    0x2C   /* on DAC ornekleme hizi */

/* --- NABM (veri yolu) kayitlari: PCM OUT kutusu 0x10 ofsetinde --- */
#define NABM_PO_BDBAR    0x10   /* tampon tanimlayici listesi adresi */
#define NABM_PO_CIV      0x14   /* su anki giris */
#define NABM_PO_LVI      0x15   /* son gecerli giris */
#define NABM_PO_SR       0x16   /* durum */
#define NABM_PO_PICB     0x18   /* kalan ornek sayisi */
#define NABM_PO_CR       0x1B   /* kontrol */
#define NABM_GLOB_CNT    0x2C
#define NABM_GLOB_STA    0x30

#define CR_RPBM   (1 << 0)   /* veri yolu ustasi calistir */
#define CR_RR     (1 << 1)   /* sifirla */
#define CR_IOCE   (1 << 4)   /* tamamlanma kesmesi */

#define SR_DCH    (1 << 0)   /* DMA denetleyici durdu */

#define BDL_GIRIS   32       /* AC'97 en fazla 32 tanimlayici destekler */
/* 1.4.9: her tanimlayiciya KENDI 4 KB sayfasi (1024 stereo kare, ~21 ms).
 * Eskiden 4 tampon x 16 KB yaziliyor ama her birine TEK sayfa ayriliyordu:
 * ilk uzun ton komsu tamponlari, ikincisi haritasiz sayfayi eziyordu
 * (ses_init hic cagrilmadigi icin gorulmemisti; UYG-TEST AC97 ile yakaladi). */
#define SES_TAMPON_SAYISI BDL_GIRIS
#define SES_TAMPON_KARE   1024          /* kare = 4 bayt (stereo 16-bit) */
#define SES_TAMPON_BAYT   (SES_TAMPON_KARE * 4)
_Static_assert(SES_TAMPON_BAYT == 4096, "her tampon tam bir sayfa olmali");

/* Tampon tanimlayicisi: 8 bayt */
typedef struct __attribute__((packed)) {
    uint32_t adres;      /* fiziksel tampon adresi */
    uint16_t uzunluk;    /* ORNEK sayisi (16-bit ornek adedi, kare degil) */
    uint16_t kontrol;
} ac97_bd_t;

#define SES_VIRT 0xA40000000ULL

static ses_denetleyici_t d;
static ac97_bd_t *bdl;
static uint64_t   bdl_fiz;
static int16_t   *tampon[SES_TAMPON_SAYISI];
static uint64_t   tampon_fiz[SES_TAMPON_SAYISI];
static uint64_t   sonraki_virt = SES_VIRT;
int ses_bosaldi(void);
static int        akis_basladi = 0;   /* DMA halkasi kuruldu mu */
static uint32_t   son_lvi = 0;        /* en son doldurulan tanimlayici */

static int dma_sayfa(uint64_t *fiz, void **virt) {
    uint64_t f = pmm_alloc_frame();
    if (f == 0) return -1;
    uint64_t v = sonraki_virt; sonraki_virt += 4096;
    /* Ses tamponlari DMA ile okunur: onbellek DEVRE DISI (PCD) olmali,
     * aksi halde kart CPU onbelleginde bekleyen veriyi goremez. */
    if (vmm_map_page(v, f, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
        pmm_free_frame(f); return -1;
    }
    uint8_t *p = (uint8_t *)(uintptr_t)v;
    for (int i = 0; i < 4096; i++) p[i] = 0;
    *fiz = f; *virt = p;
    return 0;
}

static void bekle_kisa(uint32_t dongu) {
    for (volatile uint32_t i = 0; i < dongu; i++) { }
}

int ses_init(void) {
    d.var = 0; d.caliyor = 0; d.calinan_ornek = 0;

    /* PCI sinifi 0x04 (multimedya), alt sinif 0x01 (ses) */
    const pci_device_t *p = pci_find_by_class(0x04, 0x01);
    if (!p) {
        /* 1.4.9: AC'97 yoksa Intel HDA (gunumuz bilgisayarlari, VirtualBox) */
        if (hda_init() == 0) {
            d.var = 1; d.hda = 1;
            d.vendor_id = hda_vendor(); d.device_id = hda_aygit();
            return 0;
        }
        log_write(LOG_BILGI, "SES", "AC97 ya da HDA ses denetleyicisi bulunamadi");
        return -1;
    }

    d.vendor_id = p->vendor_id;
    d.device_id = p->device_id;
    d.nam_taban  = (uint16_t)(p->bar[0] & ~0x3u);
    d.nabm_taban = (uint16_t)(p->bar[1] & ~0x3u);
    if (d.nam_taban == 0 || d.nabm_taban == 0) {
        log_write(LOG_HATA, "SES", "AC97 I/O tabanlari bos");
        return -1;
    }

    /* DMA icin Bus Master SART */
    uint32_t cmd = pci_config_read32(p->bus, p->slot, p->func, 0x04);
    pci_config_write32(p->bus, p->slot, p->func, 0x04, cmd | (1u << 2) | 1u);

    /* Denetleyiciyi ve kodegi uyandir */
    outl(d.nabm_taban + NABM_GLOB_CNT, 0x00000002);   /* soguk sifirlama */
    bekle_kisa(500000);
    outw(d.nam_taban + NAM_RESET, 0);                  /* kodek sifirla */
    bekle_kisa(500000);

    /* Degisken ornekleme hizi destegi varsa 48kHz'i acikca ayarla */
    uint16_t ext = inw(d.nam_taban + NAM_EXT_ID);
    if (ext & 1) {
        outw(d.nam_taban + NAM_EXT_CTRL, (uint16_t)(inw(d.nam_taban + NAM_EXT_CTRL) | 1));
        bekle_kisa(100000);
        outw(d.nam_taban + NAM_PCM_ORNEK, SES_ORNEK_HIZ);
        bekle_kisa(100000);
    }

    /* Sesi ac: AC'97'de deger AZALTMA'dir, 0 = en yuksek */
    outw(d.nam_taban + NAM_MASTER_VOL, 0x0000);
    outw(d.nam_taban + NAM_PCM_VOL,    0x0000);

    /* BDL ve ses tamponlarini ayir */
    void *v;
    if (dma_sayfa(&bdl_fiz, &v) != 0) return -1;
    bdl = (ac97_bd_t *)v;
    for (int i = 0; i < SES_TAMPON_SAYISI; i++) {
        void *tv;
        if (dma_sayfa(&tampon_fiz[i], &tv) != 0) return -1;
        tampon[i] = (int16_t *)tv;
    }

    /* PCM OUT kutusunu sifirla */
    outb(d.nabm_taban + NABM_PO_CR, CR_RR);
    bekle_kisa(200000);

    d.var = 1;
    serial_write("SES: AC97 bulundu vendor=");
    serial_write_hex(d.vendor_id);
    serial_write(" device=");
    serial_write_hex(d.device_id);
    serial_write(" NAM=");
    serial_write_hex(d.nam_taban);
    serial_write(" NABM=");
    serial_write_hex(d.nabm_taban);
    serial_write(" ext_id=");
    serial_write_hex(ext);
    serial_write("\n");
    log_write(LOG_BILGI, "SES", "AC97 ses surucusu hazir (48kHz stereo)");
    return 0;
}

int ses_var(void) { return d.var; }
const ses_denetleyici_t *ses_durum(void) { return &d; }

/* 1.4.9: kuyruktaki son kare de calinana kadar UYUYARAK bekler, sonra
 * durdurur. Eskiden DEDE/OZAN sonunda ses_dur() cagiriyor, halkada bekleyen
 * son ~0,7 saniyeyi kesiyordu. En cok 3 saniye bekler. */
void ses_bitir(void) {
    if (!d.var) return;
    if (d.hda) {
        uint64_t t0 = timer_get_ticks(), hz = timer_hz() ? timer_hz() : 100;
        while (!hda_bosaldi() && timer_get_ticks() - t0 < 3 * hz) thread_sleep(1);
        /* LPIB DMA'nin OKUDUGU yerdir; kart FIFO'su ve DAC henuz caliyor
         * olabilir: hemen durdurmak sozun son ~25 ms'sini keser. Akis
         * 150 ms daha (sessizlik) calar, sonra durur. */
        uint64_t t1 = timer_get_ticks();
        while (timer_get_ticks() - t1 < hz * 15 / 100 + 1) { hda_bosaldi(); thread_sleep(1); }
        hda_dur(); d.caliyor = 0;
        return;
    }
    uint64_t t0 = timer_get_ticks(), hz = timer_hz() ? timer_hz() : 100;
    while (akis_basladi && !ses_bosaldi() && timer_get_ticks() - t0 < 3 * hz) thread_sleep(1);
    ses_dur();
}

/* 1.4.9 sinama: kuyruktaki her sey calindi mi (denetleyici son girisi
 * bitirip durdu). Kart yoksa 1. */
int ses_bosaldi(void) {
    if (d.var && d.hda) return hda_bosaldi();
    if (!d.var || !akis_basladi) return 1;
    uint8_t sr = inb(d.nabm_taban + NABM_PO_SR);
    uint8_t civ = (uint8_t)(inb(d.nabm_taban + NABM_PO_CIV) & 31);
    return (sr & SR_DCH) && civ == (uint8_t)son_lvi;
}

void ses_seviye(uint8_t yuzde) {
    if (!d.var) return;
    if (d.hda) { hda_seviye(yuzde); return; }
    if (yuzde > 100) yuzde = 100;
    /* AC'97: 0=en yuksek, 63=en dusuk (6 bit azaltma), her kanal ayri */
    uint8_t azalt = (uint8_t)(63 - (yuzde * 63) / 100);
    uint16_t deger = (uint16_t)((azalt << 8) | azalt);
    outw(d.nam_taban + NAM_MASTER_VOL, deger);
    outw(d.nam_taban + NAM_PCM_VOL, deger);
}

void ses_dur(void) {
    if (!d.var) return;
    if (d.hda) { hda_dur(); d.caliyor = 0; return; }
    outb(d.nabm_taban + NABM_PO_CR, 0);
    bekle_kisa(100000);
    outb(d.nabm_taban + NABM_PO_CR, CR_RR);
    d.caliyor = 0;
    akis_basladi = 0;
}

/* 1.4.9: AKAN cikis. 32 tanimlayicilik halka; her cagri kareleri sirayla
 * bos tamponlara yazar ve LVI'yi ilerletir. Halka doluysa (yazilacak giris
 * denetleyicinin su an caldigi giris ise) UYUYARAK bekler: ardisik cagrilar
 * artik birbirini kesmez (eskiden her cagri DMA'yi bastan baslatiyordu).
 * Denetleyici son girisi calip durduysa (DCH), RPBM acik kaldigi icin LVI
 * yazimi onu yeniden baslatir. */
static int halka_bekle(uint32_t giris) {
    uint64_t t0 = timer_get_ticks(), hz = timer_hz() ? timer_hz() : 100;
    for (;;) {
        uint8_t sr = inb(d.nabm_taban + NABM_PO_SR);
        uint8_t civ = (uint8_t)(inb(d.nabm_taban + NABM_PO_CIV) & 31);
        if ((sr & SR_DCH) || civ != giris) return 0;
        if (timer_get_ticks() - t0 > 2 * hz) return -1;      /* denetleyici takildi */
        thread_sleep(1);
    }
}

static int ses_cal__ic(const int16_t *pcm, uint32_t kare_sayisi) {
    if (!d.var || kare_sayisi == 0) return -1;
    if (d.hda) {
        int r = hda_cal(pcm, kare_sayisi);
        if (r == 0) { d.caliyor = 1; d.calinan_ornek += kare_sayisi; }
        return r;
    }
    uint32_t yazilan = 0, giris_sayisi = 0;
    while (yazilan < kare_sayisi) {
        uint32_t bu_kare = kare_sayisi - yazilan;
        if (bu_kare > SES_TAMPON_KARE) bu_kare = SES_TAMPON_KARE;
        uint32_t giris = akis_basladi ? ((son_lvi + 1) & 31) : 0;
        if (akis_basladi && halka_bekle(giris) != 0) {
            serial_write("SES: denetleyici ilerlemiyor, akis sifirlandi\n");
            ses_dur();
            return -1;
        }
        int16_t *hedef = tampon[giris];
        for (uint32_t i = 0; i < bu_kare * 2; i++) hedef[i] = pcm[yazilan * 2 + i];
        bdl[giris].adres   = (uint32_t)tampon_fiz[giris];
        bdl[giris].uzunluk = (uint16_t)(bu_kare * 2);   /* ORNEK adedi (kare*2) */
        bdl[giris].kontrol = 0;
        __asm__ volatile ("mfence" ::: "memory");
        if (!akis_basladi) {
            outb(d.nabm_taban + NABM_PO_CR, CR_RR);
            bekle_kisa(20000);
            outl(d.nabm_taban + NABM_PO_BDBAR, (uint32_t)bdl_fiz);
            outb(d.nabm_taban + NABM_PO_LVI, (uint8_t)giris);
            outb(d.nabm_taban + NABM_PO_SR, 0x1C);    /* durum bitlerini temizle */
            outb(d.nabm_taban + NABM_PO_CR, CR_RPBM); /* calmayi baslat */
            akis_basladi = 1;
        } else {
            outb(d.nabm_taban + NABM_PO_SR, 0x1C);
            outb(d.nabm_taban + NABM_PO_LVI, (uint8_t)giris);
        }
        son_lvi = giris;
        yazilan += bu_kare;
        giris_sayisi++;
    }
    d.caliyor = 1;
    d.calinan_ornek += yazilan;
    return (int)giris_sayisi > 0 ? 0 : -1;
}

/* 1.5: BEKLEMEYEN calma (UMAY'in Dede Korkut sesi). Halkada bos yer
 * oldugu kadarini alir, dolunca durur ve alinan kare sayisini dondurur
 * (0 = halka dolu, sonra yine dene). Uygulama kendi dongusunu
 * durdurmadan sesi besler; cekirdekte uyuyarak beklenmez. */
static int ses_cal_dene__ic(const int16_t *pcm, uint32_t kare_sayisi) {
    if (!d.var) return -1;
    if (d.hda) {
        int r = hda_cal_dene(pcm, kare_sayisi);
        if (r > 0) { d.caliyor = 1; d.calinan_ornek += (uint32_t)r; }
        return r;
    }
    uint32_t yazilan = 0;
    while (yazilan < kare_sayisi) {
        uint32_t bu_kare = kare_sayisi - yazilan;
        if (bu_kare > SES_TAMPON_KARE) bu_kare = SES_TAMPON_KARE;
        uint32_t giris = akis_basladi ? ((son_lvi + 1) & 31) : 0;
        if (akis_basladi) {
            uint8_t sr = inb(d.nabm_taban + NABM_PO_SR);
            uint8_t civ = (uint8_t)(inb(d.nabm_taban + NABM_PO_CIV) & 31);
            if (!(sr & SR_DCH) && civ == giris) break;          /* halka dolu */
        }
        int r = ses_cal__ic(pcm + (uint64_t)yazilan * 2, bu_kare);  /* yer var: beklemez */
        if (r != 0) return yazilan ? (int)yazilan : -1;
        yazilan += bu_kare;
    }
    return (int)yazilan;
}

int ses_cal_dene(const int16_t *pcm, uint32_t kare_sayisi) {
    ozkilit_al(&ses_kilit);
    int r = ses_cal_dene__ic(pcm, kare_sayisi);
    ozkilit_birak(&ses_kilit);
    return r;
}

static int ses_ton__ic(uint32_t frekans_hz, uint32_t sure_ms) {
    if (!d.var) return -1;
    /* 1.4.9: frekans 0 = SESSIZLIK (OZAN'in sozcuk arasi soluklari);
     * eskiden -1 donup hic calmiyordu. */

    uint32_t kare = (SES_ORNEK_HIZ / 1000) * sure_ms;
    if (kare > SES_TAMPON_KARE * SES_TAMPON_SAYISI)
        kare = SES_TAMPON_KARE * SES_TAMPON_SAYISI;

    /* Kare dalga: kayan nokta YOK (cekirdek -mno-sse ile derleniyor),
     * bu yuzden tamsayi periyot sayaciyla uretiyoruz. */
    uint32_t periyot = frekans_hz ? SES_ORNEK_HIZ / frekans_hz : 2;
    if (periyot < 2) periyot = 2;

    static int16_t ornekler[SES_TAMPON_KARE * SES_TAMPON_SAYISI * 2];
    for (uint32_t i = 0; i < kare; i++) {
        int16_t deger = frekans_hz == 0 ? 0 : ((i % periyot) < (periyot / 2)) ? 8000 : -8000;
        ornekler[i * 2]     = deger;   /* sol */
        ornekler[i * 2 + 1] = deger;   /* sag */
    }

    serial_write("SES: ton uretiliyor frekans=");
    serial_write_hex(frekans_hz);
    serial_write("Hz sure=");
    serial_write_hex(sure_ms);
    serial_write("ms kare=");
    serial_write_hex(kare);
    serial_write("\n");

    return ses_cal(ornekler, kare);
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int ses_cal(const int16_t *pcm, uint32_t kare_sayisi) {
    ozkilit_al(&ses_kilit);
    int r = ses_cal__ic(pcm, kare_sayisi);
    ozkilit_birak(&ses_kilit);
    return r;
}

int ses_ton(uint32_t frekans_hz, uint32_t sure_ms) {
    ozkilit_al(&ses_kilit);
    int r = ses_ton__ic(frekans_hz, sure_ms);
    ozkilit_birak(&ses_kilit);
    return r;
}
