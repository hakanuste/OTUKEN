#include "fat32.h"
#include "depo_kilit.h"
#include "disk.h"
#include "disk_cache.h"
#include "gpt.h"
#include "serial.h"

/* FAT32 surucusu: ikinci disk (disk_id 1), okuma + yazma, alt dizinler.
 * Tum LBA'lar 64 bitliktir; birim bir bolumun icinde (bolum_lba) olabilir.
 * Yazmalar disk onbellegi uzerinden (write-through) yapilir ve FAT'in
 * tum kopyalarina islenir. */

#define KUME_SON   0x0FFFFFF8u
#define KUME_BITIS 0x0FFFFFFFu
#define BILINMEZ   0xFFFFFFFFu

static int      fat32_bagli = 0;
static uint64_t bolum_lba;
static uint8_t  spk;                 /* sektor/kume */
static uint16_t ayrilmis;
static uint8_t  fat_adedi;
static uint32_t fat_boyut;           /* bir FAT kopyasinin sektor sayisi */
static uint32_t kok_kume;
static uint64_t fat_baslangic;
static uint64_t veri_baslangic;
static uint32_t toplam_kume;         /* gecerli kumeler: 2 .. toplam_kume+1 */
static uint64_t fsinfo_lba;          /* 0 = yok */
static uint32_t bos_sayisi = BILINMEZ;
static uint32_t bos_ipucu  = 2;

typedef struct { uint64_t lba; uint32_t sira; } yer_t;

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int sek_oku(uint64_t lba, void *b)        { return disk_cache_read_sectors(1, lba, 1, b); }
static int sek_yaz(uint64_t lba, const void *b)  { return disk_cache_write_sectors(1, lba, 1, b); }

static uint64_t kume_lba(uint32_t k) { return veri_baslangic + (uint64_t)(k - 2) * spk; }
static int kume_gecerli(uint32_t k)  { return k >= 2 && k < toplam_kume + 2; }

/* ------------------------------------------------------------------ */
/* Baglama: BPB'yi diskin basinda, GPT bolumlerinde ya da MBR'de ara  */
/* ------------------------------------------------------------------ */
static int bpb_coz(uint64_t lba) {
    static uint8_t s[512];
    if (sek_oku(lba, s) != 0) return -1;
    if (s[510] != 0x55 || s[511] != 0xAA) return -1;
    uint16_t bps = rd_u16(&s[11]);
    uint8_t  spc = s[13];
    uint16_t rsv = rd_u16(&s[14]);
    uint8_t  nf  = s[16];
    uint16_t kok_giris = rd_u16(&s[17]);
    uint32_t fsz = rd_u32(&s[36]);
    uint32_t rc  = rd_u32(&s[44]);
    uint32_t toplam = rd_u16(&s[19]) ? rd_u16(&s[19]) : rd_u32(&s[32]);
    if (bps != 512 || spc == 0 || (spc & (spc - 1)) || rsv == 0 || nf == 0 ||
        kok_giris != 0 || fsz == 0 || rc < 2 || toplam == 0) return -1;
    uint64_t sistem = (uint64_t)rsv + (uint64_t)nf * fsz;
    if (sistem >= toplam) return -1;
    uint32_t nk = (uint32_t)((toplam - sistem) / spc);
    if (rc >= nk + 2) return -1;

    bolum_lba      = lba;
    spk            = spc;
    ayrilmis       = rsv;
    fat_adedi      = nf;
    fat_boyut      = fsz;
    kok_kume       = rc;
    toplam_kume    = nk;
    fat_baslangic  = lba + rsv;
    veri_baslangic = lba + sistem;

    fsinfo_lba = 0; bos_sayisi = BILINMEZ; bos_ipucu = 2;
    uint16_t fi = rd_u16(&s[48]);
    if (fi != 0 && fi != 0xFFFF) {
        static uint8_t f[512];
        if (sek_oku(lba + fi, f) == 0 && rd_u32(&f[0]) == 0x41615252u &&
            rd_u32(&f[484]) == 0x61417272u) {
            fsinfo_lba = lba + fi;
            uint32_t b = rd_u32(&f[488]), n = rd_u32(&f[492]);
            if (b <= nk) bos_sayisi = b;
            if (n >= 2 && n < nk + 2) bos_ipucu = n;
        }
    }
    return 0;
}

int fat32_mount(void) {
    ozkilit_al(&depo_kilit);
    int sonuc = -1;
    fat32_bagli = 0;
    if (bpb_coz(0) == 0) {
        sonuc = 0;
    } else {
        int n = gpt_bolum_sayisi();
        if (n <= 0) n = gpt_tara(1);
        for (int i = 0; sonuc != 0 && i < n; i++) {
            const gpt_bolum_t *b = gpt_bolum_al(i);
            if (b && b->var && bpb_coz(b->ilk_lba) == 0) sonuc = 0;
        }
        if (sonuc != 0) {
            static uint8_t mbr[512];
            if (sek_oku(0, mbr) == 0 && mbr[510] == 0x55 && mbr[511] == 0xAA) {
                for (int p = 0; sonuc != 0 && p < 4; p++) {
                    const uint8_t *g = &mbr[446 + 16 * p];
                    if ((g[4] == 0x0B || g[4] == 0x0C) && bpb_coz(rd_u32(&g[8])) == 0) sonuc = 0;
                }
            }
        }
    }
    if (sonuc != 0) {
        serial_write("BITIG(fat32): gecerli FAT32 birimi bulunamadi\n");
        ozkilit_birak(&depo_kilit);
        return -1;
    }
    fat32_bagli = 1;
    serial_write("BITIG(fat32): baglandi - bolum_lba="); serial_write_hex((uint32_t)(bolum_lba >> 32));
    serial_write_hex((uint32_t)bolum_lba);
    serial_write(" sektor/kume="); serial_write_hex(spk);
    serial_write(" fat_lba="); serial_write_hex((uint32_t)fat_baslangic);
    serial_write(" data_lba="); serial_write_hex((uint32_t)veri_baslangic);
    serial_write(" kok_kume="); serial_write_hex(kok_kume);
    serial_write("\n");
    ozkilit_birak(&depo_kilit);
    return 0;
}

/* ------------------------------------------------------------------ */
/* FAT tablosu                                                         */
/* ------------------------------------------------------------------ */
static uint32_t fat_oku(uint32_t k) {
    static uint8_t b[512];
    uint64_t ofs = (uint64_t)k * 4;
    if (sek_oku(fat_baslangic + ofs / 512, b) != 0) return KUME_BITIS;
    return rd_u32(&b[ofs % 512]) & 0x0FFFFFFFu;
}

/* Tum FAT kopyalarina yazar. */
static int fat_yaz(uint32_t k, uint32_t deger) {
    static uint8_t b[512];
    uint64_t ofs = (uint64_t)k * 4;
    for (uint32_t f = 0; f < fat_adedi; f++) {
        uint64_t lba = fat_baslangic + (uint64_t)f * fat_boyut + ofs / 512;
        if (sek_oku(lba, b) != 0) return -1;
        uint32_t eski = rd_u32(&b[ofs % 512]);
        wr_u32(&b[ofs % 512], (eski & 0xF0000000u) | (deger & 0x0FFFFFFFu));
        if (sek_yaz(lba, b) != 0) return -1;
    }
    return 0;
}

static void fsinfo_yaz(void) {
    static uint8_t f[512];
    if (!fsinfo_lba) return;
    if (sek_oku(fsinfo_lba, f) != 0) return;
    if (rd_u32(&f[0]) != 0x41615252u || rd_u32(&f[484]) != 0x61417272u) return;
    wr_u32(&f[488], bos_sayisi);
    wr_u32(&f[492], bos_ipucu);
    sek_yaz(fsinfo_lba, f);
}

/* Bos bir kume ayirir ve zincir sonu olarak isaretler. 0 = yer yok/hata. */
static uint32_t kume_ayir(void) {
    static uint8_t b[512];
    uint32_t aralik = toplam_kume;           /* 2 .. toplam_kume+1 */
    uint32_t bas = kume_gecerli(bos_ipucu) ? bos_ipucu : 2;
    uint64_t gecerli_lba = ~0ULL;
    for (uint32_t n = 0; n < aralik; n++) {
        uint32_t k = bas + n;
        if (k >= toplam_kume + 2) k -= aralik;
        uint64_t ofs = (uint64_t)k * 4;
        uint64_t lba = fat_baslangic + ofs / 512;
        if (lba != gecerli_lba) {
            if (sek_oku(lba, b) != 0) return 0;
            gecerli_lba = lba;
        }
        if ((rd_u32(&b[ofs % 512]) & 0x0FFFFFFFu) == 0) {
            if (fat_yaz(k, KUME_BITIS) != 0) return 0;
            bos_ipucu = k + 1;
            if (bos_sayisi != BILINMEZ && bos_sayisi) bos_sayisi--;
            return k;
        }
    }
    return 0;
}

static void zincir_birak(uint32_t k) {
    for (uint32_t guvenlik = 0; kume_gecerli(k) && guvenlik <= toplam_kume; guvenlik++) {
        uint32_t sonraki = fat_oku(k);
        if (fat_yaz(k, 0) != 0) return;
        if (bos_sayisi != BILINMEZ) bos_sayisi++;
        if (sonraki >= KUME_SON || sonraki < 2) return;
        k = sonraki;
    }
}

static int kume_sifirla(uint32_t k) {
    static uint8_t sifir[4096];
    for (int i = 0; i < 4096; i++) sifir[i] = 0;
    for (uint32_t s = 0; s < spk; s += 8) {
        uint32_t c = (spk - s) < 8 ? (spk - s) : 8;
        if (disk_cache_write_sectors(1, kume_lba(k) + s, (uint8_t)c, sifir) != 0) return -1;
    }
    return 0;
}

/* Kumenin basina 'uzunluk' bayt yazar; verinin bitisinden sonraki tam
 * sektorler atlanir, kismi sektorun kalani sifirlanir. */
static int kume_veri_yaz(uint32_t k, const uint8_t *veri, uint32_t uzunluk) {
    static uint8_t tmp[4096];
    for (uint32_t s = 0; s < spk; s += 8) {
        uint32_t bas = s * 512;
        if (bas >= uzunluk) break;
        uint32_t c = (spk - s) < 8 ? (spk - s) : 8;
        for (uint32_t i = 0; i < c * 512; i++)
            tmp[i] = (bas + i < uzunluk) ? veri[bas + i] : 0;
        if (disk_cache_write_sectors(1, kume_lba(k) + s, (uint8_t)c, tmp) != 0) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Adlar ve yollar                                                     */
/* ------------------------------------------------------------------ */
static void ad_bicimle(const uint8_t *ham11, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) { if (ham11[i] == ' ') break; out[p++] = (char)ham11[i]; }
    if (ham11[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) { if (ham11[i] == ' ') break; out[p++] = (char)ham11[i]; }
    }
    out[p] = 0;
}

/* 'bas' ten 'uzun' karakterlik bileseni 11 baytlik 8.3 adina cevirir. */
static int ad_83_yap(const char *bas, int uzun, uint8_t *o) {
    for (int i = 0; i < 11; i++) o[i] = ' ';
    if (uzun <= 0) return -1;
    int govde = 0, uzanti = 0, nokta = 0;
    for (int i = 0; i < uzun; i++) {
        char c = bas[i];
        if (c == '.') {
            if (nokta || i == 0) return -1;
            nokta = 1;
            continue;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < 0x21 || c == '"' || c == '*' || c == '+' || c == ',' || c == '/' || c == ':' ||
            c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '\\' || c == '[' ||
            c == ']' || c == '|' || (uint8_t)c > 0x7E) return -1;
        if (!nokta) { if (govde >= 8) return -1; o[govde++] = (uint8_t)c; }
        else        { if (uzanti >= 3) return -1; o[8 + uzanti++] = (uint8_t)c; }
    }
    return govde ? 0 : -1;
}

static int ad_esit(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static uint32_t giris_kumesi(const uint8_t *e) {
    return ((uint32_t)rd_u16(&e[20]) << 16) | rd_u16(&e[26]);
}

static void giris_kur(uint8_t *e, const uint8_t *ad11, uint8_t attr, uint32_t kume, uint32_t boyut) {
    for (int i = 0; i < 32; i++) e[i] = 0;
    for (int i = 0; i < 11; i++) e[i] = ad11[i];
    e[11] = attr;
    wr_u16(&e[16], 0x5C21);                 /* tarih: 2026-01-01 */
    wr_u16(&e[18], 0x5C21);
    wr_u16(&e[24], 0x5C21);
    wr_u16(&e[20], (uint16_t)(kume >> 16));
    wr_u16(&e[26], (uint16_t)kume);
    wr_u32(&e[28], boyut);
}

/* Dizin zincirinde 11 baytlik adi arar. 0 bulundu, -1 yok/hata. */
static int dizinde_ara(uint32_t dizin_kume, const uint8_t *ad11, uint8_t *ent_out, yer_t *yer) {
    static uint8_t s[512];
    uint32_t k = dizin_kume;
    for (uint32_t guvenlik = 0; kume_gecerli(k) && guvenlik <= toplam_kume; guvenlik++) {
        for (uint32_t i = 0; i < spk; i++) {
            uint64_t lba = kume_lba(k) + i;
            if (sek_oku(lba, s) != 0) return -1;
            for (uint32_t e = 0; e < 16; e++) {
                const uint8_t *ent = &s[e * 32];
                if (ent[0] == 0x00) return -1;
                if (ent[0] == 0xE5) continue;
                if (ent[11] == 0x0F || (ent[11] & 0x08)) continue;
                if (ad_esit(ent, ad11)) {
                    if (ent_out) for (int j = 0; j < 32; j++) ent_out[j] = ent[j];
                    if (yer) { yer->lba = lba; yer->sira = e; }
                    return 0;
                }
            }
        }
        uint32_t sonraki = fat_oku(k);
        if (sonraki >= KUME_SON || sonraki < 2) break;
        k = sonraki;
    }
    return -1;
}

/* Dizinde bos bir giris yeri bulur; dolu ise dizin zincirine yeni kume ekler. */
static int dizinde_bos_yer(uint32_t dizin_kume, yer_t *yer) {
    static uint8_t s[512];
    uint32_t k = dizin_kume, son = dizin_kume;
    for (uint32_t guvenlik = 0; kume_gecerli(k) && guvenlik <= toplam_kume; guvenlik++) {
        son = k;
        for (uint32_t i = 0; i < spk; i++) {
            uint64_t lba = kume_lba(k) + i;
            if (sek_oku(lba, s) != 0) return -1;
            for (uint32_t e = 0; e < 16; e++) {
                if (s[e * 32] == 0x00 || s[e * 32] == 0xE5) {
                    yer->lba = lba; yer->sira = e;
                    return 0;
                }
            }
        }
        uint32_t sonraki = fat_oku(k);
        if (sonraki >= KUME_SON || sonraki < 2) break;
        k = sonraki;
    }
    uint32_t yeni = kume_ayir();
    if (!yeni) return -1;
    if (kume_sifirla(yeni) != 0 || fat_yaz(son, yeni) != 0) {
        fat_yaz(yeni, 0);
        if (bos_sayisi != BILINMEZ) bos_sayisi++;
        return -1;
    }
    yer->lba = kume_lba(yeni); yer->sira = 0;
    return 0;
}

static int giris_yaz(const yer_t *y, const uint8_t *ent32) {
    static uint8_t s[512];
    if (sek_oku(y->lba, s) != 0) return -1;
    for (int i = 0; i < 32; i++) s[y->sira * 32 + i] = ent32[i];
    return sek_yaz(y->lba, s);
}

/* Yolun sonuna kadar inen sozcukleri ayirir: bilesen 'sira' icin
 * baslangic ve uzunluk. Bilesen yoksa uzunluk 0. */
static const char *bilesen_al(const char *p, int *uzun) {
    while (*p == '/') p++;
    const char *b = p;
    while (*p && *p != '/') p++;
    *uzun = (int)(p - b);
    return b;
}

/* Yolu cozer: ust dizinin kumesi ve son bilesenin 8.3 adi. */
static int yol_ayir(const char *yol, uint32_t *ust_kume, uint8_t *yaprak11) {
    if (!yol || yol[0] != '/') return -1;
    uint32_t dizin = kok_kume;
    const char *p = yol;
    for (;;) {
        int uzun;
        const char *b = bilesen_al(p, &uzun);
        if (uzun == 0) return -1;                    /* "/" ya da bos bilesen */
        const char *sonra = b + uzun;
        int son_mu;
        { int u2; bilesen_al(sonra, &u2); son_mu = (u2 == 0); }
        uint8_t ad[11];
        if (ad_83_yap(b, uzun, ad) != 0) return -1;
        if (son_mu) {
            *ust_kume = dizin;
            for (int i = 0; i < 11; i++) yaprak11[i] = ad[i];
            return 0;
        }
        uint8_t ent[32];
        if (dizinde_ara(dizin, ad, ent, 0) != 0) return -1;
        if (!(ent[11] & 0x10)) return -1;
        uint32_t k = giris_kumesi(ent);
        dizin = (k == 0) ? kok_kume : k;
        p = sonra;
    }
}

static void dirent_doldur(const uint8_t *ent, fat32_dirent_t *o) {
    ad_bicimle(ent, o->name);
    o->first_cluster = giris_kumesi(ent);
    o->size = rd_u32(&ent[28]);
    o->is_dir = (ent[11] & 0x10) ? 1 : 0;
}

static int yol_bul__ic(const char *yol, fat32_dirent_t *out) {
    if (!fat32_bagli || !yol || yol[0] != '/') return -1;
    int u0;
    bilesen_al(yol, &u0);
    if (u0 == 0) {                                   /* kok dizin */
        out->name[0] = '/'; out->name[1] = 0;
        out->size = 0; out->first_cluster = kok_kume; out->is_dir = 1;
        return 0;
    }
    uint32_t ust; uint8_t ad[11]; uint8_t ent[32];
    if (yol_ayir(yol, &ust, ad) != 0) return -1;
    if (dizinde_ara(ust, ad, ent, 0) != 0) return -1;
    dirent_doldur(ent, out);
    return 0;
}

static int listele_yol__ic(const char *yol, fat32_dirent_t *out, int max) {
    if (!fat32_bagli || max <= 0) return -1;
    fat32_dirent_t d;
    if (yol_bul__ic(yol, &d) != 0 || !d.is_dir) return -1;
    static uint8_t s[512];
    int bulunan = 0;
    uint32_t k = (d.first_cluster == 0) ? kok_kume : d.first_cluster;
    for (uint32_t guvenlik = 0; kume_gecerli(k) && guvenlik <= toplam_kume; guvenlik++) {
        for (uint32_t i = 0; i < spk && bulunan < max; i++) {
            if (sek_oku(kume_lba(k) + i, s) != 0) return bulunan;
            for (uint32_t e = 0; e < 16 && bulunan < max; e++) {
                const uint8_t *ent = &s[e * 32];
                if (ent[0] == 0x00) return bulunan;
                if (ent[0] == 0xE5 || ent[0] == '.') continue;
                if (ent[11] == 0x0F || (ent[11] & 0x08)) continue;
                dirent_doldur(ent, &out[bulunan++]);
            }
        }
        if (bulunan >= max) break;
        uint32_t sonraki = fat_oku(k);
        if (sonraki >= KUME_SON || sonraki < 2) break;
        k = sonraki;
    }
    return bulunan;
}

static int fat32_list_root__ic(fat32_dirent_t *out, int max) {
    if (!fat32_bagli) return 0;
    int n = listele_yol__ic("/", out, max);
    return n < 0 ? 0 : n;
}

static int fat32_read_file_at__ic(const fat32_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset) {
    if (!fat32_bagli || spk == 0) return -1;
    static uint8_t sek[512];
    if (offset >= entry->size || entry->first_cluster == 0) return 0;
    uint32_t kume_bayt = 512u * spk;
    uint32_t atla = offset / kume_bayt;
    uint32_t kume_ofs = offset % kume_bayt;
    uint32_t k = entry->first_cluster;
    for (uint32_t i = 0; i < atla; i++) {
        k = fat_oku(k);
        if (!kume_gecerli(k)) return 0;
    }
    uint32_t kalan = entry->size - offset;
    uint32_t okunan = 0;
    int ilk = 1;
    for (uint32_t guvenlik = 0; kume_gecerli(k) && kalan > 0 && okunan < bufsize && guvenlik <= toplam_kume; guvenlik++) {
        uint32_t bas_sek = ilk ? kume_ofs / 512 : 0;
        uint32_t bas_bayt = ilk ? kume_ofs % 512 : 0;
        for (uint32_t s = bas_sek; s < spk && kalan > 0 && okunan < bufsize; s++) {
            if (sek_oku(kume_lba(k) + s, sek) != 0) return (int)okunan;
            uint32_t sb = (s == bas_sek) ? bas_bayt : 0;
            for (uint32_t i = sb; i < 512 && kalan > 0 && okunan < bufsize; i++) {
                buf[okunan++] = sek[i];
                kalan--;
            }
        }
        ilk = 0;
        uint32_t sonraki = fat_oku(k);
        if (sonraki >= KUME_SON || sonraki < 2) break;
        k = sonraki;
    }
    return (int)okunan;
}

/* ------------------------------------------------------------------ */
/* Yazma                                                               */
/* ------------------------------------------------------------------ */
static int write_file__ic(const char *yol, const uint8_t *veri, uint32_t boyut) {
    if (!fat32_bagli) return -1;
    uint32_t ust; uint8_t ad[11];
    if (yol_ayir(yol, &ust, ad) != 0) return -1;
    uint8_t eski[32]; yer_t yer;
    int var = (dizinde_ara(ust, ad, eski, &yer) == 0);
    if (var && (eski[11] & 0x10)) return -1;

    uint32_t kume_bayt = 512u * spk;
    uint32_t adet = (uint32_t)(((uint64_t)boyut + kume_bayt - 1) / kume_bayt);
    uint32_t ilk = 0, onceki = 0;
    for (uint32_t i = 0; i < adet; i++) {
        uint32_t k = kume_ayir();
        int hata = (k == 0);
        if (!hata) {
            if (onceki) { if (fat_yaz(onceki, k) != 0) hata = 1; } else ilk = k;
            uint32_t ofs = i * kume_bayt;
            uint32_t n = boyut - ofs; if (n > kume_bayt) n = kume_bayt;
            if (!hata && kume_veri_yaz(k, veri + ofs, n) != 0) hata = 1;
        }
        if (hata) {
            if (k && !onceki) ilk = k;
            else if (k) { fat_yaz(k, 0); if (bos_sayisi != BILINMEZ) bos_sayisi++; }
            zincir_birak(ilk);
            fsinfo_yaz();
            return -1;
        }
        onceki = k;
    }

    uint8_t yeni[32];
    int r;
    if (var) {
        for (int i = 0; i < 32; i++) yeni[i] = eski[i];
        yeni[11] = (uint8_t)(eski[11] & ~0x10);
        wr_u16(&yeni[20], (uint16_t)(ilk >> 16));
        wr_u16(&yeni[26], (uint16_t)ilk);
        wr_u32(&yeni[28], boyut);
        wr_u16(&yeni[24], 0x5C21);
        r = giris_yaz(&yer, yeni);
    } else {
        giris_kur(yeni, ad, 0x20, ilk, boyut);
        r = dizinde_bos_yer(ust, &yer);
        if (r == 0) r = giris_yaz(&yer, yeni);
    }
    if (r != 0) {
        zincir_birak(ilk);
        fsinfo_yaz();
        return -1;
    }
    if (var) zincir_birak(giris_kumesi(eski));
    fsinfo_yaz();
    return (int)boyut;
}

static int dizin_yarat__ic(const char *yol) {
    if (!fat32_bagli) return -1;
    uint32_t ust; uint8_t ad[11];
    if (yol_ayir(yol, &ust, ad) != 0) return -1;
    if (dizinde_ara(ust, ad, 0, 0) == 0) return -2;

    uint32_t k = kume_ayir();
    if (!k) return -1;
    static uint8_t s[512];
    int hata = (kume_sifirla(k) != 0);
    if (!hata) {
        for (int i = 0; i < 512; i++) s[i] = 0;
        uint8_t nokta[11], noktanokta[11];
        for (int i = 0; i < 11; i++) { nokta[i] = ' '; noktanokta[i] = ' '; }
        nokta[0] = '.'; noktanokta[0] = '.'; noktanokta[1] = '.';
        giris_kur(&s[0],  nokta,      0x10, k, 0);
        giris_kur(&s[32], noktanokta, 0x10, (ust == kok_kume) ? 0 : ust, 0);
        hata = (sek_yaz(kume_lba(k), s) != 0);
    }
    yer_t yer;
    if (!hata) hata = (dizinde_bos_yer(ust, &yer) != 0);
    if (!hata) {
        uint8_t ent[32];
        giris_kur(ent, ad, 0x10, k, 0);
        hata = (giris_yaz(&yer, ent) != 0);
    }
    if (hata) {
        fat_yaz(k, 0);
        if (bos_sayisi != BILINMEZ) bos_sayisi++;
        fsinfo_yaz();
        return -1;
    }
    fsinfo_yaz();
    return 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int fat32_list_root(fat32_dirent_t *out, int max) {
    ozkilit_al(&depo_kilit);
    int r = fat32_list_root__ic(out, max);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat32_read_file_at(const fat32_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset) {
    ozkilit_al(&depo_kilit);
    int r = fat32_read_file_at__ic(entry, buf, bufsize, offset);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat32_yol_bul(const char *yol, fat32_dirent_t *out) {
    ozkilit_al(&depo_kilit);
    int r = yol_bul__ic(yol, out);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat32_listele_yol(const char *yol, fat32_dirent_t *out, int max) {
    ozkilit_al(&depo_kilit);
    int r = listele_yol__ic(yol, out, max);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat32_write_file(const char *yol, const uint8_t *veri, uint32_t boyut) {
    ozkilit_al(&depo_kilit);
    int r = write_file__ic(yol, veri, boyut);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat32_dizin_yarat(const char *yol) {
    ozkilit_al(&depo_kilit);
    int r = dizin_yarat__ic(yol);
    ozkilit_birak(&depo_kilit);
    return r;
}
