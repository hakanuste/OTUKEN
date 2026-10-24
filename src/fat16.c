#include "fat16.h"
#include "fat16_ic.h"   /* 1.4.7: ic arayuz bildirimleri tanimlarla derleyicide eslessin */
#include "depo_kilit.h"
#include "disk.h"
#include "disk_cache.h"
#include "serial.h"

/* 1.5: FAT16 zincir konum onbellegi (bkz. fat16_read_file_at__ic) */
static struct { int gecerli; uint64_t kusak; uint32_t baglama; uint16_t ilk; uint32_t sira; uint16_t kume; } zincir_ok;
static uint32_t baglama_sayaci = 0;


static int fat16_bagli = 0;   /* mount BASARILI mi */
static uint16_t bytes_per_sector;
static uint8_t  sectors_per_cluster;
static uint16_t reserved_sectors;
static uint8_t  num_fats;
static uint16_t root_entry_count;
static uint16_t fat_size_sectors;
static uint32_t total_sectors;

static uint32_t fat_start_lba;
static uint32_t root_dir_start_lba;
static uint32_t root_dir_sectors;
static uint32_t data_start_lba;

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fat16_mount(void) {
    static uint8_t sector[512];

    if (disk_cache_read_sectors(0, 0, 1, sector) != 0) {
        serial_write("fat16: boot sector okunamadi\n");
        return -1;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        serial_write("BITIG(fat16): gecerli bir onyukleme imzasi yok (0x55AA degil)\n");
        return -1;
    }
    serial_write("BITIG dosya sistemi (fat16) baglaniyor...\n");
    fat16_bagli = 1;
    baglama_sayaci++;               /* 1.5: zincir onbellegi eski birime bakmasin */

    bytes_per_sector    = rd_u16(&sector[11]);
    sectors_per_cluster = sector[13];
    reserved_sectors    = rd_u16(&sector[14]);
    num_fats            = sector[16];
    root_entry_count    = rd_u16(&sector[17]);
    uint16_t total16    = rd_u16(&sector[19]);
    fat_size_sectors    = rd_u16(&sector[22]);
    uint32_t total32    = rd_u32(&sector[32]);

    total_sectors = total16 ? total16 : total32;

    fat_start_lba      = reserved_sectors;
    root_dir_start_lba = fat_start_lba + (uint32_t)num_fats * fat_size_sectors;
    root_dir_sectors   = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1)
                          / bytes_per_sector;
    data_start_lba     = root_dir_start_lba + root_dir_sectors;

    serial_write("fat16: bytes/sektor="); serial_write_hex(bytes_per_sector);
    serial_write(" sektor/kume="); serial_write_hex(sectors_per_cluster);
    serial_write(" FAT sayisi="); serial_write_hex(num_fats);
    serial_write(" kok giris="); serial_write_hex(root_entry_count);
    serial_write("\n");
    serial_write("fat16: fat_lba="); serial_write_hex(fat_start_lba);
    serial_write(" root_lba="); serial_write_hex(root_dir_start_lba);
    serial_write(" data_lba="); serial_write_hex(data_start_lba);
    serial_write(" toplam_sektor="); serial_write_hex(total_sectors);
    serial_write("\n");

    if (bytes_per_sector != 512) {
        serial_write("fat16: UYARI - 512 disinda sektor boyutu desteklenmiyor\n");
        fat16_bagli = 0;
        return -1;
    }
    /* bozuk BPB: sıfır küme boyu / FAT yok / veri bölgesi yok -> bağlanmaz
     * (aksi hâlde küme hesapları sıfıra bölerdi) */
    if (sectors_per_cluster == 0 || num_fats == 0 || fat_size_sectors == 0 ||
        total_sectors <= data_start_lba) {
        serial_write("fat16: UYARI - gecersiz BPB (kume boyu/FAT/veri bolgesi), baglanmadi\n");
        fat16_bagli = 0;
        return -1;
    }

    return 0;
}

static void format_83_name(const uint8_t *raw11, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (raw11[i] == ' ') break;
        out[p++] = (char)raw11[i];
    }
    if (raw11[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw11[i] == ' ') break;
            out[p++] = (char)raw11[i];
        }
    }
    out[p] = 0;
}

static int fat16_list_root__ic(fat16_dirent_t *out, int max) {
    /* DAYANIKLILIK: birim bagli degilken bolen degerler sifirdir
     * (sifira bolme -> #DE panigi). */
    if (!fat16_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) return 0;

    static uint8_t sector[512];
    int found = 0;

    for (uint32_t s = 0; s < root_dir_sectors && found < max; s++) {
        if (disk_cache_read_sectors(0, root_dir_start_lba + s, 1, sector) != 0) {
            serial_write("fat16: kok dizin sektoru okunamadi\n");
            break;
        }
        for (int e = 0; e < 16 && found < max; e++) {
            const uint8_t *ent = &sector[e * 32];
            uint8_t first = ent[0];

            if (first == 0x00) goto done; /* dizinin sonu */
            if (first == 0xE5) continue;  /* silinmis giris */

            uint8_t attr = ent[11];
            if (attr == 0x0F) continue;         /* uzun dosya adi parcasi */
            if (attr & 0x08) continue;           /* birim etiketi */

            format_83_name(ent, out[found].name);
            out[found].first_cluster = rd_u16(&ent[26]);
            out[found].size          = rd_u32(&ent[28]);
            out[found].is_dir        = (attr & 0x10) ? 1 : 0;
            found++;
        }
    }
done:
    return found;
}

static uint32_t veri_kume_sayisi(void);
/* küme bu birimin veri bölgesinde mi: FAT erişimleri BUNA bağlı (bozuk
 * zincir FAT alanının dışına okuma/yazma yaptıramaz) */
static int kume_icinde(uint16_t k) { return k >= 2 && (uint32_t)k < veri_kume_sayisi() + 2; }

static uint16_t fat16_next_cluster(uint16_t cluster) {
    static uint8_t fat_sector[512];
    if (!kume_icinde(cluster)) return 0xFFFF;          /* birim dışı: zincir burada biter */
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sec = fat_start_lba + (fat_offset / bytes_per_sector);
    uint32_t ent_off = fat_offset % bytes_per_sector;

    if (disk_cache_read_sectors(0, fat_sec, 1, fat_sector) != 0)
        return 0xFFFF;

    return rd_u16(&fat_sector[ent_off]);
}

static int fat16_read_file__ic(const fat16_dirent_t *entry, uint8_t *buf, uint32_t bufsize) {
    /* DAYANIKLILIK: birim bagli degilken bolen degerler sifirdir
     * (sifira bolme -> #DE panigi). */
    if (!fat16_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) return -1;

    static uint8_t cluster_buf[512];
    uint32_t total_read = 0;
    uint16_t cluster = entry->first_cluster;
    uint32_t remaining = entry->size;

    if (cluster == 0)
        return 0;

    while (cluster < 0xFFF8 && cluster != 0 && remaining > 0) {
        uint32_t lba = data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;

        for (int s = 0; s < sectors_per_cluster && remaining > 0; s++) {
            if (disk_cache_read_sectors(0, lba + s, 1, cluster_buf) != 0) {
                serial_write("fat16: dosya sektoru okunamadi\n");
                return (int)total_read;
            }
            uint32_t chunk = bytes_per_sector;
            if (chunk > remaining) chunk = remaining;
            if (total_read + chunk > bufsize) chunk = bufsize - total_read;

            for (uint32_t i = 0; i < chunk; i++)
                buf[total_read + i] = cluster_buf[i];

            total_read += chunk;
            remaining  -= chunk;

            if (total_read >= bufsize) return (int)total_read;
        }

        cluster = fat16_next_cluster(cluster);
    }

    return (int)total_read;
}

/* Stage 12 iyilestirmesi: dosyanin BASINDAN degil, 'offset' baytindan
 * itibaren okur - hedef offset'e kadar olan kumeleri (cluster) atlar. */
static int fat16_read_file_at__ic(const fat16_dirent_t *entry, uint8_t *buf,
                        uint32_t bufsize, uint32_t offset) {
    static uint8_t cluster_buf[512];
    /* DAYANIKLILIK: birim bagli degilken bolen degerler sifirdir
     * (sifira bolme -> #DE panigi). */
    if (!fat16_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) return -1;

    if (offset >= entry->size || entry->first_cluster == 0) return 0;

    uint32_t cluster_size_bytes = (uint32_t)bytes_per_sector * sectors_per_cluster;
    uint32_t clusters_to_skip = offset / cluster_size_bytes;
    uint32_t byte_offset_in_cluster = offset % cluster_size_bytes;

    /* 1.5: kume zinciri her cagride bastan yuruyordu: 4 KB'lik okumalarla
     * 64 MB'lik bir dosya (UMAY ses modeli) karesel surede (~5 dk)
     * okunuyordu. Son konum saklanir; ayni dosyada ileri okuma oradan
     * surer. Herhangi bir disk yazimi ya da yeniden baglama onbellegi
     * gecersiz kilar (zincir degismis olabilir). */
    uint16_t cluster = entry->first_cluster;
    uint32_t i0 = 0;
    uint64_t kusak = disk_cache_yazim_kusagi();
    if (zincir_ok.gecerli && zincir_ok.kusak == kusak && zincir_ok.baglama == baglama_sayaci &&
        zincir_ok.ilk == entry->first_cluster && zincir_ok.sira <= clusters_to_skip) {
        cluster = zincir_ok.kume; i0 = zincir_ok.sira;
    }
    for (uint32_t i = i0; i < clusters_to_skip; i++) {
        cluster = fat16_next_cluster(cluster);
        if (cluster == 0 || cluster >= 0xFFF8) return 0;
    }
    zincir_ok.gecerli = 1; zincir_ok.kusak = kusak; zincir_ok.baglama = baglama_sayaci;
    zincir_ok.ilk = entry->first_cluster; zincir_ok.sira = clusters_to_skip; zincir_ok.kume = cluster;

    uint32_t remaining = entry->size - offset;
    uint32_t total_read = 0;
    int first_iter = 1;

    while (cluster < 0xFFF8 && cluster != 0 && remaining > 0 && total_read < bufsize) {
        uint32_t lba = data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;
        uint32_t start_sector = first_iter ? (byte_offset_in_cluster / bytes_per_sector) : 0;
        uint32_t start_byte = first_iter ? (byte_offset_in_cluster % bytes_per_sector) : 0;

        for (uint32_t s = start_sector; s < (uint32_t)sectors_per_cluster &&
             remaining > 0 && total_read < bufsize; s++) {
            if (disk_cache_read_sectors(0, lba + s, 1, cluster_buf) != 0) return (int)total_read;
            uint32_t sb = (s == start_sector) ? start_byte : 0;
            for (uint32_t i = sb; i < bytes_per_sector && remaining > 0 &&
                 total_read < bufsize; i++) {
                buf[total_read++] = cluster_buf[i];
                remaining--;
            }
        }
        first_iter = 0;
        cluster = fat16_next_cluster(cluster);
    }
    return (int)total_read;
}

/* ================= Asama 27: FAT16 YAZMA ================= */

/* FAT tablosundaki bir girdiyi gunceller. FAT KOPYALARININ HEPSINE
 * yazar (num_fats) - sadece birine yazmak, dosya sistemini tutarsiz
 * birakir ve baska bir isletim sistemi ikinci kopyayi okursa
 * BOZUK gorur. */
static int fat16_set_cluster(uint16_t cluster, uint16_t value) {
    static uint8_t fat_sector[512];
    if (!kume_icinde(cluster)) return -1;              /* birim dışı: FAT alanı dışına yazılmaz */
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t sec_in_fat = fat_offset / bytes_per_sector;
    uint32_t ent_off    = fat_offset % bytes_per_sector;
    if (sec_in_fat >= fat_size_sectors) return -1;

    for (uint8_t f = 0; f < num_fats; f++) {
        uint32_t lba = fat_start_lba + (uint32_t)f * fat_size_sectors + sec_in_fat;
        if (disk_cache_read_sectors(0, lba, 1, fat_sector) != 0) return -1;
        fat_sector[ent_off]     = (uint8_t)(value & 0xFF);
        fat_sector[ent_off + 1] = (uint8_t)((value >> 8) & 0xFF);
        if (disk_cache_write_sectors(0, lba, 1, fat_sector) != 0) return -1;
    }
    return 0;
}

/* Bos (0x0000) bir kume bulur. Bulamazsa 0 doner.
 * 1.4.9: arama son bulunan sektorden baslar (buyuk dosyada her kume icin
 * FAT'i bastan taramak karesel suruyordu); bulunamazsa bastan bir tur. */
static uint32_t bos_ipucu_sektor = 0;
/* veri bolgesindeki kume sayisi: FAT tablosu bundan UZUN olabilir; sondaki
 * girdiler diskte olmayan kumelerdir (1.4.9: eskiden onlar da verilebilirdi). */
static uint32_t veri_kume_sayisi(void) {
    if (!sectors_per_cluster || total_sectors <= data_start_lba) return 0;
    return (total_sectors - data_start_lba) / sectors_per_cluster;
}
static uint16_t fat16_find_free_cluster(void) {
    static uint8_t fat_sector[512];
    uint32_t entries_per_sector = bytes_per_sector / 2;

    for (uint32_t k = 0; k < fat_size_sectors; k++) {
        uint32_t sec = (bos_ipucu_sektor + k) % fat_size_sectors;
        if (disk_cache_read_sectors(0, fat_start_lba + sec, 1, fat_sector) != 0)
            return 0;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            uint32_t cluster = sec * entries_per_sector + i;
            if (cluster < 2) continue;            /* 0 ve 1 ayrilmistir */
            uint16_t val = rd_u16(&fat_sector[i * 2]);
            if (val == 0x0000 && cluster < veri_kume_sayisi() + 2) { bos_ipucu_sektor = sec; return (uint16_t)cluster; }
        }
    }
    return 0;
}

/* 8.3 bicimli ham ismi (11 bayt) uretir: "DENEME  TXT" */
static void make_83_raw(const char *name, uint8_t *out11) {
    for (int i = 0; i < 11; i++) out11[i] = ' ';
    int i = 0, p = 0;
    while (name[i] && name[i] != '.' && p < 8) {
        char ch = name[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        out11[p++] = (uint8_t)ch;
        i++;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++;
        p = 8;
        while (name[i] && p < 11) {
            char ch = name[i];
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            out11[p++] = (uint8_t)ch;
            i++;
        }
    }
}

static int raw83_equal(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Ayrilmis kumeleri FAT'ta serbest birakir (yazma basarisiz olursa). */
static void fat16_geri_al(const uint16_t *kumeler, uint32_t adet) {
    for (uint32_t i = 0; i < adet; i++) {
        fat16_set_cluster(kumeler[i], 0x0000);
    }
    if (adet > 0) {
        serial_write("BITIG(fat16): ");
        serial_write_hex(adet);
        serial_write(" kume geri alindi (rollback)\n");
    }
}

/* sınama: >0 ise N. yazım (kök dizin dosyası) başarısız olur. Yalnız
 * çekirdek içi sınamalar ayarlar; syscall yolu yok. */
volatile int fat16_sinama_yazim_hatasi;

static int fat16_write_file__ic(const char *name83, const uint8_t *data, uint32_t size) {
    static uint8_t sector[512];
    if (fat16_sinama_yazim_hatasi > 0 && --fat16_sinama_yazim_hatasi == 0) {
        serial_write("BITIG(fat16): SINAMA yazim hatasi uretildi\n");
        return -1;
    }

    /* DAYANIKLILIK: dosya sistemi BAGLI DEGILSE bytes_per_sector ve
     * sectors_per_cluster sifirdir; asagidaki kume boyutu hesabi
     * SIFIRA BOLME ile cekirdegi panikletirdi. FAT16 diski olmayan
     * bir makinede bu gercekten yasandi (#DE panigi). */
    if (!fat16_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) {
        serial_write("BITIG(fat16): birim bagli degil - yazma reddedildi\n");
        return -1;
    }
    uint8_t want[11];
    make_83_raw(name83, want);

    uint32_t cluster_bytes = (uint32_t)bytes_per_sector * sectors_per_cluster;
    uint32_t needed_clusters = (size + cluster_bytes - 1) / cluster_bytes;
    if (needed_clusters == 0) needed_clusters = 1;

    /* --- 1) Kok dizinde girdiyi bul ya da bos yer ayir --- */
    int found_sec = -1, found_idx = -1;
    int free_sec = -1, free_idx = -1;

    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (disk_cache_read_sectors(0, root_dir_start_lba + s, 1, sector) != 0)
            return -1;
        for (int e = 0; e < 16; e++) {
            uint8_t *ent = &sector[e * 32];
            if (ent[0] == 0x00 || ent[0] == 0xE5) {
                if (free_sec < 0) { free_sec = (int)s; free_idx = e; }
                if (ent[0] == 0x00) goto scan_done;   /* dizin sonu */
                continue;
            }
            if (ent[11] == 0x0F || (ent[11] & 0x08)) continue;  /* LFN / etiket */
            if (raw83_equal(ent, want)) {
                if (ent[11] & 0x10) {                /* aynı adlı DİZİN: dosyayla değiştirilmez */
                    serial_write("BITIG(fat16): ayni adli dizin var - yazma reddedildi\n");
                    return -1;
                }
                found_sec = (int)s; found_idx = e;
                goto scan_done;
            }
        }
    }
scan_done:;

    int target_sec = (found_sec >= 0) ? found_sec : free_sec;
    int target_idx = (found_sec >= 0) ? found_idx : free_idx;
    if (target_sec < 0) {
        serial_write("BITIG(fat16): kok dizinde bos yer yok\n");
        return -1;
    }

    /* --- 2) Kume zinciri kur --- */
    uint16_t eski_zincir = 0;
    if (found_sec >= 0) {
        if (disk_cache_read_sectors(0, root_dir_start_lba + target_sec, 1, sector) != 0)
            return -1;
        /* 1.4.9: eski zincir YENİ veri ve dizin girdisi yazıldıktan SONRA
         * bırakılır; yazma yarıda kalırsa eski dosya yerinde durur. */
        eski_zincir = rd_u16(&sector[target_idx * 32 + 26]);
    }

    /* --- Denetim duzeltmesi (MEDIUM): yazma islemi GERI ALINABILIR ---
     * Onceden herhangi bir adimda basarisizlik, ayrilmis kumeleri
     * OKSUZ birakiyor ve dizin girdisini tutarsiz hale getiriyordu
     * (disk sizintisi + bozuk dosya sistemi). Artik ayrilan tum
     * kumeler kaydediliyor ve hata durumunda serbest birakiliyor. */
    /* 1.4.9: 64 kume (512 baytlik kumede 32 KB) KERVAN paketleri icin
     * cok kucuktu. 4096 kume = 2 MB; geri alma listesi yigin yerine
     * duraganda (depo_kilit altinda tek yazar). */
    #define FAT16_AZAMI_KUME 4096
    static uint16_t ayrilan[FAT16_AZAMI_KUME];
    uint32_t ayrilan_adet = 0;

    if (needed_clusters > FAT16_AZAMI_KUME) {
        serial_write("BITIG(fat16): dosya cok buyuk (kume siniri)\n");
        return -1;
    }

    uint16_t prev = 0;
    uint16_t chain_start = 0;
    for (uint32_t i = 0; i < needed_clusters; i++) {
        uint16_t nc = fat16_find_free_cluster();
        if (nc == 0) {
            serial_write("BITIG(fat16): diskte bos kume kalmadi - geri aliniyor\n");
            fat16_geri_al(ayrilan, ayrilan_adet);
            return -1;
        }
        if (fat16_set_cluster(nc, 0xFFFF) != 0) {
            fat16_geri_al(ayrilan, ayrilan_adet);
            return -1;
        }
        ayrilan[ayrilan_adet++] = nc;
        if (prev != 0) {
            if (fat16_set_cluster(prev, nc) != 0) {
                fat16_geri_al(ayrilan, ayrilan_adet);
                return -1;
            }
        } else {
            chain_start = nc;
        }
        prev = nc;
    }

    /* --- 3) Veriyi kumelere yaz --- */
    uint32_t written = 0;
    uint16_t cl = chain_start;
    while (cl >= 2 && cl < 0xFFF8 && written < size) {
        uint32_t lba = data_start_lba + (uint32_t)(cl - 2) * sectors_per_cluster;
        for (uint32_t s = 0; s < sectors_per_cluster && written < size; s++) {
            for (int i = 0; i < 512; i++) {
                sector[i] = (written + (uint32_t)i < size)
                            ? data[written + i] : 0;
            }
            if (disk_cache_write_sectors(0, lba + s, 1, sector) != 0) {
                serial_write("BITIG(fat16): veri yazma hatasi - geri aliniyor\n");
                fat16_geri_al(ayrilan, ayrilan_adet);
                return -1;
            }
            uint32_t chunk = (size - written < 512) ? (size - written) : 512;
            written += chunk;
        }
        cl = fat16_next_cluster(cl);
    }

    /* --- 4) Dizin girdisini guncelle/olustur --- */
    if (disk_cache_read_sectors(0, root_dir_start_lba + target_sec, 1, sector) != 0) {
        fat16_geri_al(ayrilan, ayrilan_adet);
        return -1;
    }
    uint8_t *ent = &sector[target_idx * 32];
    for (int i = 0; i < 11; i++) ent[i] = want[i];
    ent[11] = 0x20;                 /* oznitelik: normal dosya (arsiv) */
    for (int i = 12; i < 26; i++) ent[i] = 0;
    ent[26] = (uint8_t)(chain_start & 0xFF);
    ent[27] = (uint8_t)((chain_start >> 8) & 0xFF);
    ent[28] = (uint8_t)(size & 0xFF);
    ent[29] = (uint8_t)((size >> 8) & 0xFF);
    ent[30] = (uint8_t)((size >> 16) & 0xFF);
    ent[31] = (uint8_t)((size >> 24) & 0xFF);
    if (disk_cache_write_sectors(0, root_dir_start_lba + target_sec, 1, sector) != 0) {
        serial_write("BITIG(fat16): dizin girdisi yazilamadi - geri aliniyor\n");
        fat16_geri_al(ayrilan, ayrilan_adet);
        return -1;
    }

    {   /* eski zinciri şimdi bırak (bozuk zincire karşı sınırlı) */
        uint16_t ek = eski_zincir; uint32_t say = 0;
        while (ek >= 2 && ek < 0xFFF8 && say++ < 65536) {
            uint16_t next = fat16_next_cluster(ek);
            if (fat16_set_cluster(ek, 0x0000) != 0) break;
            ek = next;
        }
    }
    serial_write("BITIG(fat16): dosya yazildi '");
    serial_write(name83);
    serial_write("' boyut=");
    serial_write_hex(size);
    serial_write(" ilk_kume=");
    serial_write_hex(chain_start);
    serial_write("\n");
    return (int)written;
}

/* 1.4.9: kok dizinden dosya siler (KERVAN kaldirma). Dizin girdisi 0xE5
 * olur, kume zinciri serbest kalir. Sira: ONCE girdi silinir (diske), sonra
 * kumeler: yarida kesilirse sonuc sahipsiz kume (sizinti) olur, ASLA iki
 * dosyanin paylastigi kume olmaz. 0 / -1 (yok) */
static int fat16_sil__ic(const char *name83) {
    static uint8_t sector[512];
    if (!fat16_bagli) return -1;
    uint8_t want[11];
    make_83_raw(name83, want);
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        if (disk_cache_read_sectors(0, root_dir_start_lba + s, 1, sector) != 0) return -1;
        for (int e = 0; e < 16; e++) {
            uint8_t *ent = &sector[e * 32];
            if (ent[0] == 0x00) return -1;
            if (ent[0] == 0xE5 || ent[11] == 0x0F || (ent[11] & 0x18)) continue;   /* LFN, etiket, dizin */
            if (!raw83_equal(ent, want)) continue;
            uint16_t cl = rd_u16(&ent[26]);
            ent[0] = 0xE5;
            if (disk_cache_write_sectors(0, root_dir_start_lba + s, 1, sector) != 0) return -1;
            uint32_t say = 0;
            while (cl >= 2 && cl < 0xFFF8 && say++ < 65536) {
                uint16_t next = fat16_next_cluster(cl);
                if (fat16_set_cluster(cl, 0x0000) != 0) return -1;
                cl = next;
            }
            serial_write("BITIG(fat16): dosya silindi '");
            serial_write(name83);
            serial_write("'\n");
            return 0;
        }
    }
    return -1;
}

int fat16_sil(const char *name83) {
    ozkilit_al(&depo_kilit);
    int r = fat16_sil__ic(name83);
    ozkilit_birak(&depo_kilit);
    return r;
}

/* bos kume sayisi (sinama: silme/kurma sizinti denetimi) */
uint32_t fat16_bos_kume_sayisi(void) {
    static uint8_t fs[512];
    uint32_t n = 0, veri = veri_kume_sayisi();
    ozkilit_al(&depo_kilit);
    uint32_t eps = bytes_per_sector / 2;
    for (uint32_t sec = 0; fat16_bagli && sec < fat_size_sectors; sec++) {
        if (disk_cache_read_sectors(0, fat_start_lba + sec, 1, fs) != 0) break;
        for (uint32_t i = 0; i < eps; i++) {
            uint32_t k = sec * eps + i;
            if (k >= 2 && k < veri + 2 && rd_u16(&fs[i * 2]) == 0) n++;
        }
    }
    ozkilit_birak(&depo_kilit);
    return n;
}

/* =====================================================================
 * Asama 42: alt dizin katmani icin IC arayuz.
 * fat16_dizin.c bu islevlerle surucunun ozel durumuna erisir; global
 * degiskenleri disariya acmak yerine daraltilmis bir yuzey verilir.
 * ===================================================================== */
int      fat16_bagli_mi(void)             { return fat16_bagli; }
/* 1.4.9: ASENA/ERGENEKON için birim geometrisi */
int fat16_geometri(fat16_geo_t *g) {
    if (!fat16_bagli || !g) return -1;
    g->bayt_sektor = bytes_per_sector; g->sektor_kume = sectors_per_cluster;
    g->fat_sayisi = num_fats; g->fat_lba = fat_start_lba; g->fat_sektor = fat_size_sectors;
    g->kok_lba = root_dir_start_lba; g->kok_sektor = root_dir_sectors; g->kok_giris = root_entry_count;
    g->veri_lba = data_start_lba; g->toplam_sektor = total_sectors; g->kume_sayisi = veri_kume_sayisi();
    return 0;
}
/* bos kume arama ipucunu sifirla (biçimleme/onarım sonrası) */
void fat16_ipucu_sifirla(void) { bos_ipucu_sektor = 0; }
uint32_t fat16_root_lba(void)             { return root_dir_start_lba; }
uint32_t fat16_root_dir_sectors(void)     { return root_dir_sectors; }
uint32_t fat16_sectors_per_cluster(void)  { return sectors_per_cluster; }

uint32_t fat16_kume_lba(uint16_t kume) {
    return data_start_lba + (uint32_t)(kume - 2) * sectors_per_cluster;
}
/* küme numarası bu birimin veri bölgesinde mi */
int fat16_kume_gecerli(uint16_t kume) { return kume_icinde(kume); }

uint16_t fat16_sonraki_kume(uint16_t kume) { return fat16_next_cluster(kume); }
uint16_t fat16_bos_kume_bul(void)          { return fat16_find_free_cluster(); }
int      fat16_kume_yaz(uint16_t k, uint16_t v) { return fat16_set_cluster(k, v); }
void     fat16_ad_bicimle(const uint8_t *ham11, char *cikti) { format_83_name(ham11, cikti); }
void     fat16_ad_83_yap(const char *ad, uint8_t *cikti11)   { make_83_raw(ad, cikti11); }
uint16_t fat16_oku16(const uint8_t *p) { return rd_u16(p); }
uint32_t fat16_oku32(const uint8_t *p) { return rd_u32(p); }

void fat16_yaz16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void fat16_yaz32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int fat16_list_root(fat16_dirent_t *out, int max) {
    ozkilit_al(&depo_kilit);
    int r = fat16_list_root__ic(out, max);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat16_read_file(const fat16_dirent_t *entry, uint8_t *buf, uint32_t bufsize) {
    ozkilit_al(&depo_kilit);
    int r = fat16_read_file__ic(entry, buf, bufsize);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat16_read_file_at(const fat16_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset) {
    ozkilit_al(&depo_kilit);
    int r = fat16_read_file_at__ic(entry, buf, bufsize, offset);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat16_write_file(const char *name83, const uint8_t *data, uint32_t size) {
    ozkilit_al(&depo_kilit);
    int r = fat16_write_file__ic(name83, data, size);
    ozkilit_birak(&depo_kilit);
    return r;
}
