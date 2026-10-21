#include "gpt.h"
#include "disk.h"
#include "log.h"
#include "serial.h"

#define MBR_IMZA_OFS   510
#define GPT_KORUYUCU   0xEE    /* MBR'de GPT'yi belirten bolum turu */

/* GPT basligi (LBA 1) */
typedef struct __attribute__((packed)) {
    char     imza[8];            /* "EFI PART" */
    uint32_t surum;
    uint32_t baslik_boyut;
    uint32_t baslik_crc;
    uint32_t yedek0;
    uint64_t bu_lba;
    uint64_t yedek_lba;
    uint64_t ilk_kullanilabilir;
    uint64_t son_kullanilabilir;
    uint8_t  disk_guid[16];
    uint64_t giris_dizisi_lba;
    uint32_t giris_sayisi;
    uint32_t giris_boyutu;
    uint32_t giris_crc;
} gpt_baslik_t;

/* GPT bolum girisi */
typedef struct __attribute__((packed)) {
    uint8_t  tur_guid[16];
    uint8_t  bolum_guid[16];
    uint64_t ilk_lba;
    uint64_t son_lba;
    uint64_t oznitelik;
    uint16_t ad[36];             /* UTF-16LE */
} gpt_giris_t;

static gpt_bolum_t bolumler[GPT_AZAMI_BOLUM];
static int bolum_sayisi = 0;

/* Bos (tumu sifir) GUID = kullanilmayan giris */
static int guid_bos(const uint8_t *g) {
    for (int i = 0; i < 16; i++) if (g[i]) return 0;
    return 1;
}

/* Bilinen bolum turleri */
static const uint8_t GUID_MS_TEMEL[16] = {   /* Microsoft Basic Data (FAT/NTFS) */
    0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44,
    0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7 };
static const uint8_t GUID_EFI_SISTEM[16] = { /* EFI System Partition */
    0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11,
    0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B };

static int guid_ayni(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int gpt_veri_bolumu_mu(const gpt_bolum_t *b) {
    if (!b || !b->var) return 0;
    return guid_ayni(b->tur_guid, GUID_MS_TEMEL) ||
           guid_ayni(b->tur_guid, GUID_EFI_SISTEM);
}

static int sektor_oku(int disk_id, uint64_t lba, void *tampon) {
    if (disk_id == 0) return disk_read_sectors(lba, 1, tampon);
    return disk2_read_sectors(lba, 1, tampon);
}

int gpt_tara(int disk_id) {
    static uint8_t sek[512];
    bolum_sayisi = 0;
    for (int i = 0; i < GPT_AZAMI_BOLUM; i++) bolumler[i].var = 0;

    /* --- 1) Koruyucu MBR (LBA 0) --- */
    /* Surucu hic yoksa bu bir HATA degildir - tek diskli makinelerde
     * normaldir. Uretim gunlugunu bosuna kirletmemek icin sessizce
     * ciikiyoruz (onceden UYARI basiyordu). */
    if (disk_sektor_sayisi(disk_id) == 0) {
        serial_write("GPT: surucu yok, tarama atlandi\n");
        return 0;
    }
    if (sektor_oku(disk_id, 0, sek) != 0) {
        serial_write("GPT: sektor 0 okunamadi, tarama atlandi\n");
        return 0;
    }
    if (sek[MBR_IMZA_OFS] != 0x55 || sek[MBR_IMZA_OFS + 1] != 0xAA) {
        serial_write("GPT: MBR imzasi yok - bolumsuz disk (tum disk tek birim)\n");
        return 0;
    }
    /* MBR'de 0xEE turunde bolum varsa disk GPT'dir */
    int koruyucu = 0;
    for (int b = 0; b < 4; b++) {
        if (sek[446 + b * 16 + 4] == GPT_KORUYUCU) koruyucu = 1;
    }
    if (!koruyucu) {
        serial_write("GPT: koruyucu MBR yok - klasik MBR bolumleme\n");
        return 0;
    }

    /* --- 2) GPT basligi (LBA 1) --- */
    if (sektor_oku(disk_id, 1, sek) != 0) return -1;
    const gpt_baslik_t *h = (const gpt_baslik_t *)sek;

    static const char BEK[8] = {'E','F','I',' ','P','A','R','T'};
    for (int i = 0; i < 8; i++) {
        if (h->imza[i] != BEK[i]) {
            serial_write("GPT: 'EFI PART' imzasi bulunamadi\n");
            return 0;
        }
    }

    uint64_t giris_lba   = h->giris_dizisi_lba;
    uint32_t giris_sayisi = h->giris_sayisi;
    uint32_t giris_boyutu = h->giris_boyutu;

    serial_write("GPT: tablo bulundu - giris sayisi=");
    serial_write_hex(giris_sayisi);
    serial_write(" giris boyutu=");
    serial_write_hex(giris_boyutu);
    serial_write("\n");

    if (giris_boyutu < sizeof(gpt_giris_t) || giris_boyutu > 512) {
        log_write(LOG_HATA, "GPT", "gecersiz giris boyutu");
        return -1;
    }
    if (giris_sayisi > 512) giris_sayisi = 512;   /* makul ust sinir */

    /* --- 3) Bolum girislerini oku --- */
    uint32_t giris_per_sektor = 512 / giris_boyutu;
    uint32_t okunan = 0;

    for (uint32_t s = 0; okunan < giris_sayisi && bolum_sayisi < GPT_AZAMI_BOLUM; s++) {
        if (sektor_oku(disk_id, giris_lba + s, sek) != 0) break;

        for (uint32_t i = 0; i < giris_per_sektor && okunan < giris_sayisi; i++, okunan++) {
            const gpt_giris_t *g = (const gpt_giris_t *)(sek + i * giris_boyutu);
            if (guid_bos(g->tur_guid)) continue;       /* kullanilmayan */
            if (bolum_sayisi >= GPT_AZAMI_BOLUM) break;

            gpt_bolum_t *b = &bolumler[bolum_sayisi];
            b->var = 1;
            b->ilk_lba = g->ilk_lba;
            b->son_lba = g->son_lba;
            b->sektor_sayisi = (g->son_lba >= g->ilk_lba)
                                ? (g->son_lba - g->ilk_lba + 1) : 0;
            for (int k = 0; k < 16; k++) b->tur_guid[k] = g->tur_guid[k];

            /* UTF-16 adi kaba sekilde ASCII'ye indir */
            int j = 0;
            for (int k = 0; k < GPT_AD_UZUNLUK; k++) {
                uint16_t ch = g->ad[k];
                if (ch == 0) break;
                b->ad[j++] = (ch < 128) ? (char)ch : '?';
            }
            b->ad[j] = 0;

            serial_write("GPT: bolum ");
            serial_write_hex((uint32_t)bolum_sayisi);
            serial_write(" LBA ");
            serial_write_hex((uint32_t)(b->ilk_lba >> 32));
            serial_write_hex((uint32_t)b->ilk_lba);
            serial_write("..");
            serial_write_hex((uint32_t)(b->son_lba >> 32));
            serial_write_hex((uint32_t)b->son_lba);
            serial_write(" (");
            serial_write_hex((uint32_t)((b->sektor_sayisi * 512) >> 20));
            serial_write(" MB) ad='");
            serial_write(b->ad);
            serial_write("' ");
            serial_write(gpt_veri_bolumu_mu(b) ? "[veri]" : "[diger]");
            serial_write("\n");

            bolum_sayisi++;
        }
    }

    log_write_num(LOG_BILGI, "GPT", "bulunan bolum sayisi", (uint64_t)bolum_sayisi);
    return bolum_sayisi;
}

int gpt_bolum_sayisi(void) { return bolum_sayisi; }

const gpt_bolum_t *gpt_bolum_al(int i) {
    if (i < 0 || i >= bolum_sayisi) return 0;
    return &bolumler[i];
}
