#include "ses.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"
#include "log.h"
#include "serial.h"

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

/* TAMPON BOYU SAYFAYA UYMAK ZORUNDA
 * dma_sayfa() TEK bir 4096 baytlik sayfa ayirir. Bir kare stereo
 * 16-bit = 4 bayttir, yani bir sayfaya 1024 KARE sigar.
 *
 * Burada SES_TAMPON_KARE 4096 yaziliydi: ses_cal() 4096 kareye kadar
 * kopyaliyor, yani 16384 bayti 4096 baytlik bir sayfaya yaziyordu —
 * dort kat tasma, cekirdek bellegine. Ardisik sayfalar olarak
 * eslendigi icin once komsu ses tamponlarini, sonunda eslenmemis
 * bellegi eziyordu. Canli cagiranlar:
 *     ses_ton()  : 21 ms'den uzun her nota (ozan_ses.c 160-420 ms
 *                  kullanir) -> 4 kat tasma
 *     ezgi.c     : PARCA 2048 -> 2 kat tasma
 *     dede.c     : PARCA 1024 -> tam siginiyordu, sansla.
 * Artik sinir sayfanin gercek kapasitesidir. */
#define SES_TAMPON_KARE   1024          /* 1024 * 4 bayt = 4096 = bir sayfa */
#define SES_TAMPON_BAYT   (SES_TAMPON_KARE * 4)

/* Her BDL girisi KENDI tamponunu gosterir: giris indeksi = tampon
 * indeksi. Eskiden 32 giris ama yalnizca 4 tampon vardi ve indeks
 * mod 4 doluyordu; 5. giristen sonra kart hala okurken ayni bellek
 * yeniden yaziliyordu. 16 tampon = 16 * 21.3 ms = ~341 ms kuyruk. */
#define SES_TAMPON_SAYISI 16

/* DONANIMIN HALKASI 32 GIRISLIKTIR; SURUCU DE 32 KULLANMAK ZORUNDA.
 * AC'97'de CIV 32'ye gore doner. Surucu yalnizca 16 giris doldurursa,
 * LVI 0..15 arasinda dolanirken CIV 15'i gectikten sonra hic
 * yazilmamis 16 tanimlayiciyi dolasir ve LVI'yi BIR DAHA YAKALAMAZ:
 * motor durmaz, bos tanimlayicilari calar. Olculdu (ses koşumu):
 *     EZME giris=0 CIV=21 LVI=0
 * yani kart 16'nin otesine gecmisti.
 * Cozum: tanimlayici sayisi 32, tampon sayisi 16 — giris i, tampon
 * (i & 15)'i gosterir. Boylece iki halkanin donemi AYNI olur ve bir
 * tampon ancak 16 giris sonra yeniden kullanilir. */
#define BDL_GIRIS   32
#define TAMPON_IM(g) ((g) & (SES_TAMPON_SAYISI - 1))

/* Dongusel bekleme tavani: olu bir kart cekirdegi kilitlemesin. */
#define BEKLE_TAVAN  20000000u

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

/* --- Dongusel kuyruk durumu ---
 * g_yaz : siradaki YAZILACAK BDL girisi (= tampon indeksi)
 * g_lvi : karta bildirilmis son gecerli giris
 * Kart CIV'den LVI'ye kadar calar, esitlenince DCH ile durur. */
static uint32_t g_yaz;
static int32_t  g_lvi = -1;

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
    g_yaz = 0; g_lvi = -1;

    /* PCI sinifi 0x04 (multimedya), alt sinif 0x01 (ses) */
    const pci_device_t *p = pci_find_by_class(0x04, 0x01);
    if (!p) {
        log_write(LOG_BILGI, "SES", "AC97 ses denetleyicisi bulunamadi");
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
    /* AC'97 BDL adresleri 32 BITTIR. 4 GB ustu bir cerceve gelirse
     * (uint32_t) donusumu adresi SESSIZCE kirpar ve kart bambaska bir
     * yeri okur. Kirpmak yerine basarisiz olunur. */
    if (bdl_fiz >= 0x100000000ULL) {
        log_write(LOG_HATA, "SES", "BDL cercevesi 4GB ustunde (AC97 32-bit adresler)");
        return -1;
    }
    for (int i = 0; i < SES_TAMPON_SAYISI; i++) {
        void *tv;
        if (dma_sayfa(&tampon_fiz[i], &tv) != 0) return -1;
        if (tampon_fiz[i] >= 0x100000000ULL) {
            log_write(LOG_HATA, "SES", "ses tamponu 4GB ustunde (AC97 32-bit adresler)");
            return -1;
        }
        tampon[i] = (int16_t *)tv;
    }

    /* PCM OUT kutusunu sifirla */
    outb(d.nabm_taban + NABM_PO_CR, CR_RR);
    bekle_kisa(200000);

    /* BDL adresi BIR KEZ bildirilir. Eskiden her ses_cal cagrisinda
     * yeniden yaziliyordu ve kartin okuma konumu sifirlaniyordu. */
    outl(d.nabm_taban + NABM_PO_BDBAR, (uint32_t)bdl_fiz);

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

void ses_seviye(uint8_t yuzde) {
    if (!d.var) return;
    if (yuzde > 100) yuzde = 100;
    /* AC'97: 0=en yuksek, 63=en dusuk (6 bit azaltma), her kanal ayri */
    uint8_t azalt = (uint8_t)(63 - (yuzde * 63) / 100);
    uint16_t deger = (uint16_t)((azalt << 8) | azalt);
    outw(d.nam_taban + NAM_MASTER_VOL, deger);
    outw(d.nam_taban + NAM_PCM_VOL, deger);
}

void ses_dur(void) {
    if (!d.var) return;
    outb(d.nabm_taban + NABM_PO_CR, 0);
    bekle_kisa(100000);
    outb(d.nabm_taban + NABM_PO_CR, CR_RR);
    d.caliyor = 0;
}

/* Kartin su an caldigi BDL girisi. */
static uint8_t civ_oku(void) { return inb(d.nabm_taban + NABM_PO_CIV); }

/* Motor durdu mu (DMA controller halted). */
static int durdu_mu(void) { return (inw(d.nabm_taban + NABM_PO_SR) & SR_DCH) != 0; }

/* Motoru calistir: LVI bildirildikten SONRA cagrilir.
 * BDBAR'a yalniz bir kez yazilir (init'ten sonra degismez); her
 * cagrida yeniden yazmak kartin okuma konumunu sifirliyordu. */
static void motoru_surdur(void) {
    outb(d.nabm_taban + NABM_PO_SR, 0x1C);     /* durum bitlerini temizle */
    outb(d.nabm_taban + NABM_PO_CR, CR_RPBM);
    d.caliyor = 1;
}

/* PCM veriyi KUYRUGA EKLER; kuyruk dolu ise yer acilmasini bekler.
 *
 * ESKI DAVRANIS VE NEDEN YANLISTI
 * Her cagrida BDBAR yeniden yaziliyor, LVI = giris-1 kuruluyor ve
 * CR'ye RPBM basiliyordu: yani motor HER CAGRIDA SIFIRDAN BASLIYORDU.
 * DEDE 1024 karelik (21.3 ms) parcalar halinde uretir ve her parca
 * icin ses_cal cagirir; sentez gercek zamandan cok daha hizli
 * oldugundan her 21 ms'lik parca, oncekini calarken kesip yerine
 * geciyordu. Kulaga butun bir cumle degil, son parcanin kirintisi
 * gidiyordu — kaldi ki dede_konus() hemen ardindan ses_dur() cagirip
 * onu da susturuyordu. Olculen her sey (perde, formant, unsuz, vurgu,
 * hece hizi) dogruyken ses yine anlasilmiyordu; sebeplerden biri
 * buydu.
 *
 * Artik giris indeksi dongusel ilerler, motor yalnizca DURMUSSA
 * surdurulur ve uzerine yazilacak tampon kartin arkasinda kalana
 * kadar beklenir. */
int ses_cal(const int16_t *pcm, uint32_t kare_sayisi) {
    if (!d.var || !pcm || kare_sayisi == 0) return -1;

    uint32_t yazilan = 0;
    while (yazilan < kare_sayisi) {
        uint32_t bu_kare = kare_sayisi - yazilan;
        if (bu_kare > SES_TAMPON_KARE) bu_kare = SES_TAMPON_KARE;

        /* AKIS DENETIMI
         * Tampon sayisi (16) tanimlayici sayisindan (32) azdir, yani
         * giris g'nin tamponu 16 giris once de kullanilmisti. Kart o
         * kadar geride kalmissa uzerine yazmak, HALA OKUNAN bellegi
         * ezer. Kuyruk, kartin g_yaz'a uzakligi tampon sayisina
         * ulastiginda doludur.
         * Eskiden yalniz "CIV == g_yaz" bakiliyordu; 16 tampon 32
         * girise dagildigi icin bu yetmiyordu. */
        if (d.caliyor && !durdu_mu()) {
            uint32_t tavan = 0;
            while (!durdu_mu() &&
                   ((g_yaz + BDL_GIRIS - civ_oku()) % BDL_GIRIS)
                       >= SES_TAMPON_SAYISI) {
                if (++tavan > BEKLE_TAVAN) return -1;   /* olu kart */
            }
        }

        uint32_t t = TAMPON_IM(g_yaz);
        int16_t *hedef = tampon[t];
        for (uint32_t i = 0; i < bu_kare * 2; i++)
            hedef[i] = pcm[(yazilan * 2) + i];

        bdl[g_yaz].adres   = (uint32_t)tampon_fiz[t];
        /* uzunluk ORNEK adedidir (kare degil): stereo -> kare*2 */
        bdl[g_yaz].uzunluk = (uint16_t)(bu_kare * 2);
        bdl[g_yaz].kontrol = 0;

        g_lvi = (int32_t)g_yaz;
        outb(d.nabm_taban + NABM_PO_LVI, (uint8_t)g_lvi);
        if (!d.caliyor || durdu_mu()) motoru_surdur();

        g_yaz = (g_yaz + 1) % BDL_GIRIS;
        yazilan += bu_kare;
        d.calinan_ornek += bu_kare;
    }
    return 0;
}

/* KUYRUK BOSALANA KADAR BEKLE.
 * dede_konus() ve ozan_soyle() sonlarinda dogrudan ses_dur()
 * cagiriyordu: son parcalar kuyruga alinmisken DMA durduruluyor, yani
 * cumlenin sonu (bazen tamami) hic duyulmuyordu. Susturmadan once
 * BOSALMA beklenir. */
void ses_bekle(void) {
    if (!d.var || !d.caliyor) return;
    uint32_t tavan = 0;
    while (!durdu_mu()) {
        if (++tavan > BEKLE_TAVAN) break;
    }
    d.caliyor = 0;
}

/* Sinama/ezgi amacli kare dalga. frekans_hz = 0 SESSIZLIK uretir.
 *
 * ESKIDEN: 64 KB'lik statik dizi (4096*4*2 int16) dolduruluyor ve tek
 * seferde ses_cal'a veriliyordu; ses_cal onu 4096 karelik parcalara
 * boluyor, her parcayi 4096 BAYTLIK bir sayfaya yaziyordu — dort kat
 * tasma. ozan_ses.c 160-420 ms'lik notalar caliyor, yani Dede
 * Korkut'un kopuz ezgisi her calisinda cekirdek bellegi bozuluyordu.
 * Ayrica frekans 0 iken -1 ile cikiliyordu: sozcuk arasi soluk
 * (ses_ton(0, 60)) hic calinmiyordu.
 *
 * Artik bir sayfalik parcalar halinde uretilip kuyruga verilir. */
int ses_ton(uint32_t frekans_hz, uint32_t sure_ms) {
    if (!d.var) return -1;
    if (sure_ms == 0) return 0;

    uint32_t kalan = (SES_ORNEK_HIZ / 1000u) * sure_ms;
    uint32_t periyot = frekans_hz ? (SES_ORNEK_HIZ / frekans_hz) : 0;
    if (frekans_hz && periyot < 2) periyot = 2;

    static int16_t parca[SES_TAMPON_KARE * 2];
    uint32_t evre = 0;
    while (kalan) {
        uint32_t n = (kalan > SES_TAMPON_KARE) ? SES_TAMPON_KARE : kalan;
        for (uint32_t i = 0; i < n; i++) {
            int16_t deger = 0;
            if (periyot) {
                deger = ((evre % periyot) < (periyot / 2)) ? 8000 : -8000;
                evre++;
            }
            parca[i * 2]     = deger;   /* sol */
            parca[i * 2 + 1] = deger;   /* sag */
        }
        if (ses_cal(parca, n) != 0) return -1;
        kalan -= n;
    }
    return 0;
}
