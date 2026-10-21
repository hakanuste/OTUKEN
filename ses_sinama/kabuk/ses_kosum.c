/* =====================================================================
 * SES KOŞUMU — AC'97 sürücüsünü ANA BİLGİSAYARDA sınar.
 *
 * NEDEN BU KOŞUM VAR
 * ses.c donanıma I/O kapılarıyla konuşur; bu ortamda ses kartı yok,
 * dolayısıyla sürücü hiç sınanmıyordu. Sonuç: içinde aynı anda DÖRT
 * hata duruyordu ve hiçbiri görünmüyordu.
 *
 *   1. TAMPON TAŞMASI. dma_sayfa() TEK bir 4096 baytlık sayfa ayırır
 *      (1024 kare), ama SES_TAMPON_KARE 4096 yazılıydı: 16384 bayt
 *      4096 baytlık sayfaya yazılıyordu. ozan_ses.c 160-420 ms nota
 *      çalar (ses_ton), ezgi.c 2048 karelik parçalar verir — ikisi de
 *      canlı olarak çekirdek belleğini bozuyordu.
 *   2. MOTOR HER ÇAĞRIDA BAŞTAN BAŞLIYORDU. ses_cal her seferinde
 *      BDBAR'ı yeniden yazıp CR'ye RPBM basıyordu. DEDE 21.3 ms'lik
 *      parçalar üretir; her parça öncekini çalarken kesiyordu.
 *   3. DÖRT TAMPONA OTUZ İKİ GİRİŞ. Giriş indeksi mod 4 dolduğu için
 *      beşinci girişten sonra kart hâlâ okurken aynı bellek yeniden
 *      yazılıyordu.
 *   4. ses_ton(0, ms) -1 ile çıkıyordu: sözcük arası soluk hiç
 *      çalınmıyordu.
 *
 * NASIL SINANIR
 * Donanım taklit edilir: I/O kapıları bir AC'97 PCM OUT kutusu gibi
 * davranır (CIV ilerler, CIV==LVI olunca DCH ile durur). Her DMA
 * tamponunun ARDINA BEKÇİ BAYTLARI konur; sürücü sayfa sınırını
 * aşarsa bekçi bozulur ve yakalanır.
 * ===================================================================== */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "io.h"

/* ---------------- taklit donanım durumu ---------------- */
#include <sys/mman.h>

#define NAM   0x1000
#define NABM  0x2000

#define SAYFA       4096
#define SAYFA_AZAMI 24            /* BDL + 16 tampon + pay */
#define BEKCI_IM    0x5A

/* ses.c sanal adresi DOĞRUDAN kullanır ve sayfa başına 4096 artırır:
 * v = SES_VIRT + i*4096. Bu yüzden bellek BİTİŞİK olmak zorunda.
 * Bölgenin ARDINA erişilemez (PROT_NONE) bir bekçi sayfası konur:
 * son tampondan taşan yazma anında SIGSEGV ile düşer, sessizce
 * komşuyu ezmez. Komşuya taşma ise içerik sayımıyla yakalanır. */
static uint8_t *g_taban;
static uint64_t g_fiz[SAYFA_AZAMI];
static int      g_sayfa_n;

/* ses.c'nin kendi SES_VIRT adresine MAP_FIXED ile eslenir.
 * Boylece ses.c SINAMA ICIN HIC DEGISTIRILMEZ: sanal adres
 * hesabi (SES_VIRT + i*4096) oldugu gibi calisir. */
#define KOSUM_SES_VIRT 0xA40000000ULL

static void bellek_kur(void)
{
    size_t boy = (size_t)(SAYFA_AZAMI + 1) * SAYFA;
    g_taban = mmap((void *)(uintptr_t)KOSUM_SES_VIRT, boy,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (g_taban == MAP_FAILED) { perror("mmap"); exit(2); }
    if ((uint64_t)(uintptr_t)g_taban != KOSUM_SES_VIRT) {
        fprintf(stderr, "kosum: SES_VIRT adresine eslenemedi\n"); exit(2);
    }
    /* son sayfa: erişilemez bekçi */
    if (mprotect(g_taban + (size_t)SAYFA_AZAMI * SAYFA, SAYFA, PROT_NONE) != 0) {
        perror("mprotect"); exit(2);
    }
}

static uint8_t *sayfa_adr(int i) { return g_taban + (size_t)i * SAYFA; }

/* Bütün sayfaları bekçi imiyle doldur. */
static void bekci_doldur(void)
{
    for (int i = 0; i < SAYFA_AZAMI; i++)
        memset(sayfa_adr(i), BEKCI_IM, SAYFA);
}

/* Bekçi imi BOZULMAMIŞ sayfa sayısı (yani dokunulmamış olanlar). */
static int dokunulan_sayfa(int bas, int adet)
{
    int n = 0;
    for (int i = bas; i < bas + adet && i < SAYFA_AZAMI; i++) {
        uint8_t *m = sayfa_adr(i);
        for (int j = 0; j < SAYFA; j++)
            if (m[j] != BEKCI_IM) { n++; break; }
    }
    return n;
}

/* PCM OUT kutusu */
static uint32_t h_bdbar;
static uint8_t  h_civ, h_lvi, h_cr;
static uint16_t h_sr = 1;        /* DCH kurulu: durmuş */
static int      h_bdbar_yazim;   /* BDBAR kaç kez yazıldı */
static int      h_baslatma;      /* RPBM kaç kez kuruldu */
static int      h_calinan_ornek; /* kartın "çaldığı" toplam örnek */
static int      h_okuma;
static int      h_adim = 3;   /* kac yoklamada bir ilerler */
/* VERI BUTUNLUGU
 * Akis denetiminin ASIL isi, kart hala okurken bir tamponun uzerine
 * yazilmasini onlemektir. Bunu "kac sayfa kirlendi" ile goremezsin:
 * bellek gecerli, yalnizca ICERIK yanlis olur ve kulaga bozuk ses
 * gider. Bu yuzden LVI yazildigi ANDA o tamponun ozeti alinir; kart
 * o girisi CALARKEN ozet yeniden hesaplanir. Ikisi tutmuyorsa surucu,
 * kartin okumasini beklemeden uzerine yazmistir. */
static uint32_t h_ozet[32];
static int      h_ozet_var[32];
static int      h_bozulma;        /* kart BOZULMUS veri okudu */
/* ASIL DEGISMEZ, OZETTEN DAHA YALIN:
 * Kartin HENUZ CALMADIGI bir BDL girisi yeniden bildirilemez. Ozet
 * karsilastirmasi tek basina yetmiyordu, cunku surucu ayni girisi
 * ikinci kez bildirdiginde ozet de YENILENIYOR ve bozulma
 * maskeleniyordu. Giris "calinmamis" isaretliyken yeniden
 * bildirilmesi, kart hala o bellegi okurken uzerine yazilmis
 * olmasinin ta kendisidir. */
static int      h_ezme;

typedef struct __attribute__((packed)) {
    uint32_t adres; uint16_t uzunluk; uint16_t kontrol;
} bd_t;

static int fiz_sira(uint32_t fiz);
static uint32_t bd_ozet(const bd_t *b, int i);
static bd_t *h_bdl(void)
{
    int i = fiz_sira(h_bdbar);
    return (i < 0) ? 0 : (bd_t *)sayfa_adr(i);
}

/* Bir BDL girisinin gosterdigi tamponun ozeti. */
static uint32_t bd_ozet(const bd_t *b, int i)
{
    int sira = fiz_sira(b[i].adres);
    if (sira < 0) return 0;
    const uint8_t *m = sayfa_adr(sira);
    uint32_t bayt = (uint32_t)b[i].uzunluk * 2u;
    if (bayt > SAYFA) bayt = SAYFA;           /* bozuk surumde asabilir */
    uint32_t h = 2166136261u;                 /* FNV-1a */
    for (uint32_t j = 0; j < bayt; j++) { h ^= m[j]; h *= 16777619u; }
    return h;
}

/* GERCEK AC'97 DAVRANISI
 * CIV, CALINACAK siradaki giristir. Kart girisi calar, CIV'i ilerletir
 * ve caldigi giris LVI ise durur (DCH).
 *
 * Ilk modelde "CIV == LVI ise CALMADAN dur" yaziyordu. O zaman son
 * giris hic tuketilmiyor, dolayisiyla surucu onu sonradan yeniden
 * kullandiginda koşum haksiz yere "calinmamis giris ezildi" diyordu —
 * duzgun surucu de kaliyordu. Donanimi yanlis taklit eden bir koşum,
 * dogru kodu suclar. */
static void kart_ilerle(void)
{
    if (!(h_cr & 1)) return;                  /* RPBM yok: durur */
    if (h_sr & 1) return;                     /* zaten durmus */
    bd_t *b = h_bdl();
    int son = (h_civ == h_lvi);
    if (b && b[h_civ].uzunluk) {
        /* Kart bu girisi SIMDI okuyor: icerik, LVI yazildigi andaki
         * icerikle ayni olmak zorunda. */
        if (h_ozet_var[h_civ] && bd_ozet(b, h_civ) != h_ozet[h_civ])
            h_bozulma++;
        h_calinan_ornek += b[h_civ].uzunluk;
        h_ozet_var[h_civ] = 0;
    }
    h_civ = (uint8_t)((h_civ + 1) % 32);
    if (son) h_sr |= 1;                       /* LVI calindi: DCH */
}

/* ---------------- ses.c'nin beklediği arayüzler ---------------- */
void outb(uint16_t p, uint8_t v)
{
    if (p == NABM + 0x1B) {
        h_cr = v;
        if (v & 1) { h_baslatma++; h_sr &= (uint16_t)~1u; }
        if (v & 2) { h_civ = 0; h_lvi = 0; h_sr = 1; }   /* RR */
    } else if (p == NABM + 0x15) {
        h_lvi = v;
        /* Girisin ozeti, karta BILDIRILDIGI anda alinir. */
        bd_t *b = h_bdl();
        if (b && v < 32 && b[v].uzunluk) {
            if (h_ozet_var[v]) {
                h_ezme++;
                if (getenv("KOSUM_IZ"))
                    fprintf(stderr, "EZME giris=%u CIV=%u LVI=%u SR=%#x CR=%#x\n",
                            v, h_civ, h_lvi, h_sr, h_cr);
            }
            h_ozet[v] = bd_ozet(b, v);
            h_ozet_var[v] = 1;
        }
    } else if (p == NABM + 0x16) {
        h_sr &= (uint16_t)~(uint16_t)v;
    }
}
uint8_t inb(uint16_t p)
{
    if (p == NABM + 0x14) {
        /* Üç okumada bir kartı ilerlet: akış denetimi döngüsü
         * gerçekten dönsün, sonsuza gitmesin. */
        if (++h_okuma % h_adim == 0) kart_ilerle();
        return h_civ;
    }
    return 0;
}
void outw(uint16_t p, uint16_t v)
{
    if (p == NABM + 0x16) h_sr &= (uint16_t)~v;
    (void)p; (void)v;
}
uint16_t inw(uint16_t p)
{
    if (p == NABM + 0x16) {
        /* GERCEK kartta zaman, hangi kaydi yoklasan gecer. Once yalniz
         * CIV okumasinda ilerletiliyordu; ses_bekle() ise SR'ye bakar,
         * dolayisiyla taklit kart hic ilerlemiyor ve bekleme tavana
         * dayaniyordu. Bu koşumun kusuruydu, surucunun degil. */
        if (++h_okuma % h_adim == 0) kart_ilerle();
        return h_sr;
    }
    if (p == NAM + 0x28) return 1;          /* VRA destekli */
    if (p == NAM + 0x2A) return 0;
    return 0;
}
void outl(uint16_t p, uint32_t v)
{
    if (p == NABM + 0x10) { h_bdbar = v; h_bdbar_yazim++; }
}
uint32_t inl(uint16_t p) { (void)p; return 0; }
void io_wait(void) { }

/* --- pmm/vmm: bitişik sayfalar --- */
uint64_t pmm_alloc_frame(void)
{
    if (g_sayfa_n >= SAYFA_AZAMI) return 0;
    /* 4 GB altında, sayfa sırasıyla aynı düzende sahte fiziksel adres */
    g_fiz[g_sayfa_n] = 0x1000000ULL + (uint64_t)g_sayfa_n * SAYFA;
    return g_fiz[g_sayfa_n++];
}
void pmm_free_frame(uint64_t f) { (void)f; }

int vmm_map_page(uint64_t v, uint64_t f, uint64_t bayrak)
{
    (void)v; (void)bayrak;
    for (int i = 0; i < g_sayfa_n; i++)
        if (g_fiz[i] == f) return 0;
    return -1;
}

/* Fiziksel adresten sayfa sırasını bul (taklit kart BDL'yi okurken). */
static int fiz_sira(uint32_t fiz)
{
    for (int i = 0; i < g_sayfa_n; i++)
        if ((uint32_t)g_fiz[i] == fiz) return i;
    return -1;
}

/* --- log/serial: sessiz ---
 * İmzalar GERÇEK başlıklardan alınır; elle yazmak, başlık değişince
 * koşumun derlenmemesi yerine YANLIŞ imzayla bağlanmasına yol açardı. */
#include "log.h"
#include "serial.h"
void log_write(log_level_t d, const char *k, const char *m)
{ (void)d; (void)k; (void)m; }
void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int v) { (void)v; }

/* --- pci: tek AC'97 kartı ---
 * Yapı GERÇEK include/pci.h'den alınır; burada yeniden tanımlamak,
 * alan düzeni değişince koşumun sessizce yanlış yeri okumasına yol
 * açardı. */
#include "pci.h"

static pci_device_t g_kart = {
    .bus = 0, .slot = 4, .func = 0,
    .vendor_id = 0x8086, .device_id = 0x2415,
    .class_code = 0x04, .subclass = 0x01,
    .bar = { NAM, NABM, 0, 0, 0, 0 }
};
const pci_device_t *pci_find_by_class(uint8_t c, uint8_t a)
{ return (c == 0x04 && a == 0x01) ? &g_kart : 0; }
uint32_t pci_config_read32(uint8_t b, uint8_t s, uint8_t f, uint8_t o)
{ (void)b;(void)s;(void)f;(void)o; return 0; }
void pci_config_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v)
{ (void)b;(void)s;(void)f;(void)o;(void)v; }

/* ---------------- sınanan sürücü ---------------- */
#include SES_KAYNAK

/* ---------------- sınamalar ---------------- */
static int g_gecti, g_kaldi;
static void dene(const char *ad, int kosul)
{
    printf("  [%s] %s\n", kosul ? "GECTI" : "KALDI", ad);
    if (kosul) g_gecti++; else g_kaldi++;
}

/* ASIL DEGISMEZ: BIR TANIMLAYICI KENDI SAYFASINDAN BUYUK OLAMAZ.
 *
 * Yalnizca "kac sayfa kirlendi" sayilarak iki durum AYIRT EDILEMEZ:
 *   duzgun : 2048 kare -> IKI tanimlayici, her biri bir sayfa
 *   bozuk  : 2048 kare -> BIR tanimlayici, 8192 bayt, komsuya tasar
 * Ikisinde de iki sayfa kirlenir. Ilk surumde sinama bu yuzden bozuk
 * surumu de GECIRIYORDU — yani hicbir sey sinamiyordu.
 *
 * Ayirt edici olan tanimlayicinin KENDISI: uzunluk ORNEK adedidir,
 * ornek 2 bayttir, tampon bir sayfadir. Demek ki uzunluk hicbir zaman
 * 2048 ornegi (4096 bayt) gecemez. Bozuk surumde 4096 ornek yaziliyor
 * ve bu, imkansiz bir tanimlayicidir. */
#define SAYFA_ORNEK (SAYFA / 2)          /* 2048 ornek = 4096 bayt */

static int bd_gecerli(const char **neden)
{
    bd_t *b = h_bdl();
    if (!b) { *neden = "BDL bulunamadi"; return 0; }
    for (int i = 0; i < 32; i++) {
        if (b[i].uzunluk == 0) continue;
        if (b[i].uzunluk > SAYFA_ORNEK) {
            static char ileti[128];
            snprintf(ileti, sizeof ileti,
                     "tanimlayici %d: %u ornek (%u bayt) > bir sayfa (4096 bayt)",
                     i, b[i].uzunluk, b[i].uzunluk * 2u);
            *neden = ileti;
            return 0;
        }
        /* Tanimlayicinin gosterdigi adres gercek bir tampon olmali. */
        if (fiz_sira(b[i].adres) < 0) {
            *neden = "tanimlayici bilinmeyen adres gosteriyor"; return 0;
        }
    }
    *neden = "";
    return 1;
}

/* Sifirdan farkli uzunluktaki tanimlayici sayisi. n kare,
 * ceil(n/1024) tanimlayici ile anlatilmak zorundadir. */
static int bd_sayisi(void)
{
    bd_t *b = h_bdl();
    int n = 0;
    if (b) for (int i = 0; i < 32; i++) if (b[i].uzunluk) n++;
    return n;
}

static void bd_sifirla(void)
{
    bd_t *b = h_bdl();
    if (b) memset(b, 0, 32 * sizeof(bd_t));
}

static int tanimlayici_dogru(int kare)
{
    const char *neden = "";
    int beklenen = (kare + 1023) / 1024;
    if (!bd_gecerli(&neden)) { printf("      (%s)\n", neden); return 0; }
    if (bd_sayisi() != beklenen) {
        printf("      (beklenen %d tanimlayici, bulunan %d)\n",
               beklenen, bd_sayisi());
        return 0;
    }
    return 1;
}

/* Komsuya tasma: n kare ceil(n/1024) sayfaya sigmak zorunda. */
static int sayfa_sayisi_dogru(int bas, int kare)
{
    int beklenen = (kare + 1023) / 1024;
    int gercek = dokunulan_sayfa(bas, SAYFA_AZAMI - bas);
    if (gercek == beklenen) return 1;
    printf("      (beklenen %d sayfa, dokunulan %d)\n", beklenen, gercek);
    return 0;
}

int main(void)
{
    printf("SES SURUCUSU KOSUMU (taklit AC'97, bekci sayfali)\n\n");
    bellek_kur();

    printf("1) Baslatma\n");
    dene("ses_init() basarili", ses_init() == 0);
    dene("ses_var() 1 doner", ses_var() == 1);
    dene("BDBAR baslatmada BIR KEZ yazildi", h_bdbar_yazim == 1);
    /* BDL sayfa 0, tamponlar 1..16 */
    int T0 = 1;

    printf("\n2) Sayfa sinirlari: n kare -> ceil(n/1024) sayfa\n");
    {
        static int16_t p[4096 * 2];
        for (int i = 0; i < 4096 * 2; i++) p[i] = (int16_t)(i * 7 + 1);

        struct { int kare; const char *kim; } d[] = {
            { 1024, "DEDE (PARCA 1024)" },
            { 2048, "ezgi.c (PARCA 2048)" },
            { 4096, "eski SES_TAMPON_KARE tavani" },
        };
        for (unsigned t = 0; t < sizeof d / sizeof d[0]; t++) {
            char ad[128];
            /* g_yaz'a ELLE dokunmak, surucunun ic durumunu donanimin
             * durumundan ayirir ve koşum kendi kurdugu tutarsizligi
             * surucuye yukler. Bunun yerine kuyruk BOSALTILIR. */
            ses_bekle();
            bekci_doldur();
            bd_sifirla();
            for (int z = 0; z < 32; z++) h_ozet_var[z] = 0;
            snprintf(ad, sizeof ad, "%d kare kabul -- %s", d[t].kare, d[t].kim);
            dene(ad, ses_cal(p, d[t].kare) == 0);
            snprintf(ad, sizeof ad,
                     "%d kare: her tanimlayici bir sayfaya siğiyor", d[t].kare);
            dene(ad, tanimlayici_dogru(d[t].kare));
            snprintf(ad, sizeof ad, "%d kare -> %d sayfa (komsuya tasma yok)",
                     d[t].kare, (d[t].kare + 1023) / 1024);
            dene(ad, sayfa_sayisi_dogru(T0, d[t].kare));
        }
    }

    printf("\n3) Motor BASTAN BASLATILMIYOR (akis surekliligi)\n");
    {
        /* Kart YAVAS olmali. Hizli kartta cagrilar arasinda kuyruk
         * gercekten bosalir ve o durumda motoru yeniden baslatmak
         * DOGRU davranistir; sinama da anlamsiz olur. (Ilk surumde
         * bu yuzden haksiz yere kaliyordu.) */
        ses_bekle();
        h_adim = 400;
        int once_bdbar = h_bdbar_yazim;
        int once_basl  = h_baslatma;
        static int16_t p[1024 * 2];
        for (int c = 0; c < 8; c++) ses_cal(p, 1024);
        dene("BDBAR bir daha yazilmadi", h_bdbar_yazim == once_bdbar);
        /* Kart geride iken motor CALISIYOR; yeniden baslatilmamali.
         * Eskiden her cagri bir baslatmaydi (8 cagri -> 8 baslatma) ve
         * her baslatma onceki parcayi kesiyordu. */
        printf("      (8 cagri -> %d baslatma)\n", h_baslatma - once_basl);
        dene("kart geride iken motor yeniden baslatilmadi",
             (h_baslatma - once_basl) <= 1);
        h_adim = 3;
    }

    printf("\n4) Akis denetimi: halkadan cok veri\n");
    {
        /* Her parcaya KENDI damgasi: akis denetimi calismazsa kart
         * hala okurken uzerine yazilir ve ozet tutmaz. */
        static int16_t p[1024 * 2];
        h_adim = 7;                    /* kart biraz geride kalsin */
        ses_bekle();
        h_bozulma = 0; h_ezme = 0;
        for (int i = 0; i < 32; i++) h_ozet_var[i] = 0;
        bd_sifirla();
        int oldu = 1;
        for (int c = 0; c < 64; c++) {
            for (int i = 0; i < 1024 * 2; i++)
                p[i] = (int16_t)((c * 7919) + i);
            if (ses_cal(p, 1024) != 0) { oldu = 0; break; }
        }
        dene("64 parca (halkanin 4 kati) kuyruga girdi", oldu);
        dene("kart gercekten caldi", h_calinan_ornek > 0);
        if (h_bozulma) printf("      (%d giriste icerik bozulmasi)\n", h_bozulma);
        dene("kart HIC bozulmus veri okumadi", h_bozulma == 0);
        if (h_ezme) printf("      (%d kez calinmamis giris ezildi)\n", h_ezme);
        dene("calinmamis giris HIC ezilmedi (akis denetimi)", h_ezme == 0);
        h_adim = 3;
    }

    printf("\n5) ses_ton: ozan_ses.c'nin kullandigi sureler\n");
    {
        /* 420 ms = 20160 kare = 20 sayfa; eskiden 64 KB'lik statik dizi
         * doldurulup 4096 karelik parcalar halinde 4096 BAYTLIK
         * sayfalara yaziliyordu. */
        dene("420 ms nota (en uzun) kabul", ses_ton(196, 420) == 0);
        dene("210 ms nota kabul", ses_ton(262, 210) == 0);
        dene("SESSIZLIK (frekans 0) artik calinir -- eskiden -1",
             ses_ton(0, 60) == 0);
        dene("sure 0 hatasiz gecer", ses_ton(440, 0) == 0);
    }

    printf("\n6) Bosalmayi bekleme\n");
    {
        /* Karti YAVASLAT: yoksa ses_cal icindeki akis denetimi
         * yoklamalari sirasinda kuyrugu bitirir ve bekleme sinanmamis
         * olur. (Ilk surumde tam bu oldu.) */
        h_adim = 400;
        static int16_t p[1024 * 2];
        ses_bekle();
        bd_sifirla();
        for (int z = 0; z < 32; z++) h_ozet_var[z] = 0;
        for (int c = 0; c < 4; c++) ses_cal(p, 1024);

        bd_t *b = h_bdl();
        dene("beklemeden ONCE kuyruk dolu (kart geride)",
             b && h_civ != h_lvi);
        int once = h_calinan_ornek;

        ses_bekle();
        dene("bekleme sirasinda kart ilerledi", h_calinan_ornek > once);
        /* Yeni (dogru) modelde kart LVI'yi CALAR ve CIV'i ilerletir,
         * yani bosalinca CIV = LVI + 1 olur. */
        dene("bekleme sonunda kuyruk bosaldi (CIV = LVI+1)",
             h_civ == (uint8_t)((h_lvi + 1) % 32));
        dene("bekleme sonunda motor durmus (DCH)", (inw(NABM + 0x16) & 1) != 0);
        h_adim = 3;
    }

    printf("\n7) Hatali girdiler\n");
    {
        static int16_t p[64 * 2];
        dene("kare_sayisi 0 -> -1", ses_cal(p, 0) == -1);
        dene("bos isaretci -> -1", ses_cal(0, 64) == -1);
        ses_dur();
        dene("ses_dur() sonrasi ses_var() yine 1", ses_var() == 1);
    }

    printf("\n=== SONUC: %d gecti, %d kaldi ===\n", g_gecti, g_kaldi);
    return g_kaldi == 0 ? 0 : 1;
}
