#include "disk.h"
#include "depo_kilit.h"
#include "io.h"
#include "serial.h"
#include "gecikme.h"
#include "ahci.h"
#include "nvme.h"

/* Birincil ATA kanali, master surucu, PIO mod, LBA28 adresleme.
 * Bu, herhangi bir gercek IDE/SATA (AHCI uyumluluk modu) veya QEMU'nun
 * emule ettigi sanal diskle calisan, standart/klasik bir yontemdir. */

#define ATA_IO_BASE   0x1F0
#define ATA_CTRL_BASE 0x3F6

#define ATA_REG_DATA       (ATA_IO_BASE + 0)
#define ATA_REG_ERROR      (ATA_IO_BASE + 1)
#define ATA_REG_SECCOUNT   (ATA_IO_BASE + 2)
#define ATA_REG_LBA0       (ATA_IO_BASE + 3)
#define ATA_REG_LBA1       (ATA_IO_BASE + 4)
#define ATA_REG_LBA2       (ATA_IO_BASE + 5)
#define ATA_REG_DRIVE      (ATA_IO_BASE + 6)
#define ATA_REG_STATUS     (ATA_IO_BASE + 7)
#define ATA_REG_COMMAND    (ATA_IO_BASE + 7)

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_PIO      0x20   /* LBA28 (eski, 128GB tavan) */
#define ATA_CMD_READ_PIO_EXT  0x24   /* LBA48 - buyuk diskler */
#define ATA_CMD_WRITE_PIO_EXT 0x34   /* LBA48 yazma */
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_FLUSH     0xE7

static void ata_400ns_delay(void) {
    /* Kontrol portundan 4 kez okumak, gercek donanimda ~400ns
     * gecikme saglayan klasik bir tekniktir. */
    inb(ATA_CTRL_BASE);
    inb(ATA_CTRL_BASE);
    inb(ATA_CTRL_BASE);
    inb(ATA_CTRL_BASE);
}

/* Zaman tabanli bekleme. Eskiden salt 1M yineleme sayisina bagliydi:
 * yuklu bir QEMU'da (8 TB seyrek imaj) veya yavas bir ilk erisimde
 * sure asiliyor, okuma rastgele basarisiz oluyordu. Once hizli yoklama,
 * sonra 100 us araliklarla toplam ~5 sn. */
#define ATA_ZAMANASIMI_US 5000000u

static int ata_wait_ready(void) {
    uint8_t status;
    for (int i = 0; i < 100000; i++) {
        status = inb(ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
        if (status & ATA_SR_ERR) return -1;
    }
    for (uint32_t t = 0; t < ATA_ZAMANASIMI_US; t += 100) {
        pit2_bekle_us(100);
        status = inb(ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
        if (status & ATA_SR_ERR) return -1;
    }
    return -1;
}

static int ata_bsy_bekle(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(ATA_REG_STATUS) & ATA_SR_BSY)) return 0;
    for (uint32_t t = 0; t < ATA_ZAMANASIMI_US; t += 100) {
        pit2_bekle_us(100);
        if (!(inb(ATA_REG_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* --- Surucu bilgileri (IDENTIFY ile doldurulur) --- */
typedef struct {
    int      var;
    int      lba48;
    uint64_t sektor_sayisi;
} surucu_bilgi_t;

static surucu_bilgi_t surucu[2];   /* 0=master, 1=slave */

/* LBA48 adresini ATA kayitlarina yazar.
 * LBA48'de her kayit IKI kez yazilir: once YUKSEK bayt, sonra DUSUK.
 * Surucu bunlari bir FIFO gibi tutar; bu sayede 48 bit adres ve
 * 16-bit sektor sayisi aktarilir. */
static void lba48_yaz(uint8_t surucu_sec, uint64_t lba, uint16_t count) {
    outb(ATA_REG_DRIVE, surucu_sec);          /* LBA modu, cihaz secimi */
    ata_400ns_delay();

    /* --- yuksek yarim --- */
    outb(ATA_REG_SECCOUNT, (uint8_t)((count >> 8) & 0xFF));
    outb(ATA_REG_LBA0,     (uint8_t)((lba >> 24) & 0xFF));
    outb(ATA_REG_LBA1,     (uint8_t)((lba >> 32) & 0xFF));
    outb(ATA_REG_LBA2,     (uint8_t)((lba >> 40) & 0xFF));

    /* --- dusuk yarim --- */
    outb(ATA_REG_SECCOUNT, (uint8_t)(count & 0xFF));
    outb(ATA_REG_LBA0,     (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1,     (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2,     (uint8_t)((lba >> 16) & 0xFF));
}

/* IDENTIFY DEVICE: kapasite ve LBA48 destegini ogrenir. */
static void identify_oku(int idx, uint8_t surucu_sec) {
    uint16_t kimlik[256];
    surucu[idx].var = 0;
    surucu[idx].lba48 = 0;
    surucu[idx].sektor_sayisi = 0;

    outb(ATA_REG_DRIVE, surucu_sec);
    ata_400ns_delay();
    outb(ATA_REG_SECCOUNT, 0);
    outb(ATA_REG_LBA0, 0);
    outb(ATA_REG_LBA1, 0);
    outb(ATA_REG_LBA2, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    if (inb(ATA_REG_STATUS) == 0) return;      /* cihaz yok */
    if (ata_wait_ready() != 0) return;

    for (int i = 0; i < 256; i++) kimlik[i] = inw(ATA_REG_DATA);

    surucu[idx].var = 1;
    /* Kelime 83 bit 10: LBA48 destegi */
    surucu[idx].lba48 = (kimlik[83] & (1 << 10)) ? 1 : 0;

    if (surucu[idx].lba48) {
        /* Kelime 100-103: 48-bit toplam sektor sayisi */
        surucu[idx].sektor_sayisi =
            ((uint64_t)kimlik[100])        |
            ((uint64_t)kimlik[101] << 16)  |
            ((uint64_t)kimlik[102] << 32)  |
            ((uint64_t)kimlik[103] << 48);
    } else {
        /* Kelime 60-61: 28-bit toplam sektor sayisi */
        surucu[idx].sektor_sayisi =
            ((uint64_t)kimlik[60]) | ((uint64_t)kimlik[61] << 16);
    }

    serial_write("disk: IDENTIFY idx=");
    serial_write_hex((uint32_t)idx);
    serial_write(" LBA48=");
    serial_write(surucu[idx].lba48 ? "VAR" : "YOK");
    serial_write(" sektor=");
    serial_write_hex((uint32_t)(surucu[idx].sektor_sayisi >> 32));
    serial_write_hex((uint32_t)surucu[idx].sektor_sayisi);
    serial_write(" (~");
    serial_write_hex((uint32_t)((surucu[idx].sektor_sayisi * 512) >> 30));
    serial_write(" GB)\n");
}

uint64_t disk_sektor_sayisi(int disk_id) {
    if (disk_id < 0 || disk_id > 1) return 0;
    return surucu[disk_id].sektor_sayisi;
}

int disk_lba48_var(int disk_id) {
    if (disk_id < 0 || disk_id > 1) return 0;
    return surucu[disk_id].lba48;
}

int disk_init(void) {
    /* master surucuyu sec, LBA modunu bildir */
    outb(ATA_REG_DRIVE, 0xE0);
    ata_400ns_delay();
    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0xFF) {
        serial_write("disk: ATA portunda yanit yok (0xFF)\n");
        return -1;
    }
    identify_oku(0, 0xE0);
    serial_write("disk: ATA birincil master hazir, status=");
    serial_write_hex(status);
    serial_write("\n");
    return 0;
}

/* 1.4.9: KOK DISK ARKA UCU. ATA (IDE) birincil master yoksa ilk SATA disk
 * (AHCI) kok disk olur: VirtualBox'in varsayilan SATA denetleyicisi, q35
 * ve gercek bilgisayarlar. Eskiden disk yalniz ATA/IDE yoluyla okunuyordu;
 * SATA'ya takili diskte UMAY "program diskte bulunamadi" diyordu. */
static int kok_ahci = -2;                  /* -2 henuz secilmedi, -1 yok, >=0 port */
static int kok_nvme = 0;                   /* 1: kok disk NVMe ad alani 1 */
static int kok_ahci_portu(void) {
    if (surucu[0].var) return -1;          /* ATA varsa o */
    if (kok_ahci == -2 || kok_ahci == -1) {
        int p = ahci_disk_portu();         /* ahci_init oncesi -1: sonra yeniden denenir */
        if (p >= 0) {
            kok_ahci = p;
            surucu[0].sektor_sayisi = ahci_sektor_sayisi(p);
            surucu[0].lba48 = 1;
            serial_write("disk: kok disk AHCI portu ");
            serial_write_hex((uint32_t)p);
            serial_write(" (ATA birincil master yok)\n");
        } else kok_ahci = -1;
    }
    return kok_ahci;
}
int disk_kok_ahci_mi(void) { return kok_ahci_portu() >= 0; }
/* ATA ve SATA disk yoksa NVMe (ad alani 1) kok disk olur */
static int kok_nvme_mi(void) {
    if (surucu[0].var || kok_ahci_portu() >= 0) return 0;
    if (!kok_nvme && nvme_hazir()) {
        kok_nvme = 1;
        surucu[0].sektor_sayisi = nvme_sektor_sayisi();
        surucu[0].lba48 = 1;
        serial_write("disk: kok disk NVMe ad alani 1 (ATA ve SATA disk yok)\n");
    }
    return kok_nvme;
}
int disk_kok_nvme_mi(void) { return kok_nvme_mi(); }

static int disk_read_sectors__ic(uint64_t lba, uint8_t count, void *buf) {
    uint16_t *out = (uint16_t *)buf;
    if (count == 0) return -1;
    {
        int ap = kok_ahci_portu();
        if (ap >= 0) {
            for (uint32_t i = 0; i < count; ) {
                uint32_t n = count - i < 8 ? count - i : 8;
                if (ahci_oku(ap, lba + i, n, (uint8_t *)buf + (uint64_t)i * 512) != 0) return -1;
                i += n;
            }
            return 0;
        }
        if (kok_nvme_mi()) {
            for (uint32_t i = 0; i < count; ) {
                uint32_t n = count - i < 8 ? count - i : 8;
                if (nvme_oku(lba + i, n, (uint8_t *)buf + (uint64_t)i * 512) != 0) return -1;
                i += n;
            }
            return 0;
        }
    }

    /* LBA48 destekleniyorsa ONU kullan: 128 GB tavani kalkar.
     * Desteklenmiyorsa (cok eski surucu) LBA28'e dus ve 128 GB
     * otesindeki istekleri REDDET - sessizce yanlis sektor okumak
     * veri bozulmasi demektir. */
    if (surucu[0].lba48) {
        lba48_yaz(0xE0, lba, count);
        outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        if (lba > 0x0FFFFFFFULL) {
            serial_write("disk: LBA28 siniri asildi (surucu LBA48 desteklemiyor)\n");
            return -1;
        }
        outb(ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        ata_400ns_delay();
        outb(ATA_REG_SECCOUNT, count);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    }

    for (int s = 0; s < count; s++) {
        if (ata_wait_ready() != 0) {
            serial_write("disk: okuma hatasi (sektor bekleme)\n");
            return -1;
        }
        for (int i = 0; i < 256; i++) out[s * 256 + i] = inw(ATA_REG_DATA);
    }
    return 0;
}


int disk2_init(void) {
    outb(ATA_REG_DRIVE, 0xF0);
    ata_400ns_delay();
    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0xFF || status == 0x00) {
        serial_write("disk2: ATA slave bulunamadi\n");
        return -1;
    }
    identify_oku(1, 0xF0);
    serial_write("disk2: ATA birincil slave hazir, status=");
    serial_write_hex(status);
    serial_write("\n");
    return 0;
}

static int disk2_read_sectors__ic(uint64_t lba, uint8_t count, void *buf) {
    uint16_t *out = (uint16_t *)buf;
    if (count == 0) return -1;

    if (surucu[1].lba48) {
        lba48_yaz(0xF0, lba, count);
        outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        if (lba > 0x0FFFFFFFULL) {
            serial_write("disk2: LBA28 siniri asildi\n");
            return -1;
        }
        outb(ATA_REG_DRIVE, (uint8_t)(0xF0 | ((lba >> 24) & 0x0F)));
        ata_400ns_delay();
        outb(ATA_REG_SECCOUNT, count);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    }

    for (int s = 0; s < count; s++) {
        if (ata_wait_ready() != 0) {
            serial_write("disk2: okuma hatasi\n");
            return -1;
        }
        for (int i = 0; i < 256; i++) out[s * 256 + i] = inw(ATA_REG_DATA);
    }
    return 0;
}


/* --- Asama 27: ATA PIO yazma ---
 * Okuma ile simetriktir; farki komutun 0x30 olmasi, verinin outw ile
 * gonderilmesi ve SONUNDA onbellek bosaltma (FLUSH) komutunun
 * verilmesidir. FLUSH atlanirsa surucu veriyi kendi onbelleginde
 * tutabilir ve guc kesintisinde VERI KAYBI olur. */
static int disk_write_sectors__ic(uint64_t lba, uint8_t count, const void *buf) {
    const uint16_t *in = (const uint16_t *)buf;
    if (count == 0) return -1;
    {
        int ap = kok_ahci_portu();
        if (ap >= 0) {
            for (uint32_t i = 0; i < count; ) {
                uint32_t n = count - i < 8 ? count - i : 8;
                if (ahci_yaz(ap, lba + i, n, (const uint8_t *)buf + (uint64_t)i * 512) != 0) return -1;
                i += n;
            }
            return 0;
        }
        if (kok_nvme_mi()) {
            for (uint32_t i = 0; i < count; ) {
                uint32_t n = count - i < 8 ? count - i : 8;
                if (nvme_yaz(lba + i, n, (const uint8_t *)buf + (uint64_t)i * 512) != 0) return -1;
                i += n;
            }
            return 0;
        }
    }

    if (surucu[0].lba48) {
        lba48_yaz(0xE0, lba, count);
        outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    } else {
        if (lba > 0x0FFFFFFFULL) {
            serial_write("disk: LBA28 yazma siniri asildi\n");
            return -1;
        }
        outb(ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        ata_400ns_delay();
        outb(ATA_REG_SECCOUNT, count);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    }

    for (int s = 0; s < count; s++) {
        if (ata_wait_ready() != 0) {
            serial_write("disk: yazma hatasi (sektor bekleme)\n");
            return -1;
        }
        for (int i = 0; i < 256; i++) outw(ATA_REG_DATA, in[s * 256 + i]);
    }

    outb(ATA_REG_COMMAND, ATA_CMD_FLUSH);
    if (ata_bsy_bekle() != 0) {
        serial_write("disk: FLUSH zaman asimi\n");
        return -1;
    }
    return 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int disk_read_sectors(uint64_t lba, uint8_t count, void *buf) {
    ozkilit_al(&depo_kilit);
    int r = disk_read_sectors__ic(lba, count, buf);
    ozkilit_birak(&depo_kilit);
    return r;
}

int disk_write_sectors(uint64_t lba, uint8_t count, const void *buf) {
    ozkilit_al(&depo_kilit);
    int r = disk_write_sectors__ic(lba, count, buf);
    ozkilit_birak(&depo_kilit);
    return r;
}

int disk2_read_sectors(uint64_t lba, uint8_t count, void *buf) {
    ozkilit_al(&depo_kilit);
    int r = disk2_read_sectors__ic(lba, count, buf);
    ozkilit_birak(&depo_kilit);
    return r;
}
