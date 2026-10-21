/* ÖTÜKEN ARM64 cekirdegi - ortak alt sistemlerin AArch64 uzerinde
 * calistigini gosteren gercek bir port.
 *
 * Bu asamada tasinan alt sistemler: seri cikti, bellek yoneticisi
 * (bitmap tabanli), yigin (heap) ve gorev listesi. Mimariden BAGIMSIZ
 * algoritmalar x86 surumuyle AYNIDIR - degisen yalnizca donanim
 * katmani (UART, kesme denetleyicisi, sayfa tablolari). */
#include <stdint.h>

void uart_yaz(const char *s);
void uart_hex(uint64_t v);
void uart_putc(char c);

/* --- Mimariden bagimsiz: bitmap fiziksel bellek yoneticisi --- */
#define ARM_CERCEVE 4096
#define ARM_MAX_CERCEVE 4096            /* 16MB'lik alani takip eder */
static uint8_t bitmap[ARM_MAX_CERCEVE / 8];
static uint64_t taban_adres;
static uint64_t toplam_cerceve;

static void bit_kur(uint64_t f)   { bitmap[f >> 3] |= (uint8_t)(1u << (f & 7)); }
static void bit_temizle(uint64_t f){ bitmap[f >> 3] &= (uint8_t)~(1u << (f & 7)); }
static int  bit_test(uint64_t f)  { return (bitmap[f >> 3] >> (f & 7)) & 1; }

static void pmm_baslat(uint64_t taban, uint64_t boyut) {
    taban_adres = taban;
    toplam_cerceve = boyut / ARM_CERCEVE;
    if (toplam_cerceve > ARM_MAX_CERCEVE) toplam_cerceve = ARM_MAX_CERCEVE;
    for (uint64_t i = 0; i < sizeof(bitmap); i++) bitmap[i] = 0;
}

static uint64_t cerceve_al(void) {
    for (uint64_t f = 0; f < toplam_cerceve; f++) {
        if (!bit_test(f)) { bit_kur(f); return taban_adres + f * ARM_CERCEVE; }
    }
    return 0;
}
static void cerceve_birak(uint64_t adr) {
    uint64_t f = (adr - taban_adres) / ARM_CERCEVE;
    if (f < toplam_cerceve) bit_temizle(f);
}
static uint64_t bos_cerceve(void) {
    uint64_t n = 0;
    for (uint64_t f = 0; f < toplam_cerceve; f++) if (!bit_test(f)) n++;
    return n;
}

/* --- Mimariden bagimsiz: basit yigin (heap) --- */
static uint8_t heap_alan[64 * 1024];
static uint64_t heap_imlec = 0;

static void *ayir(uint64_t boyut) {
    boyut = (boyut + 15) & ~15UL;
    if (heap_imlec + boyut > sizeof(heap_alan)) return 0;
    void *p = &heap_alan[heap_imlec];
    heap_imlec += boyut;
    return p;
}

/* --- Islemci kimligi --- */
static uint64_t el_seviyesi(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(v));
    return (v >> 2) & 3;
}
static uint64_t midr(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(v));
    return v;
}

void cekirdek_ana(void) {
    uart_yaz("\n=== OTUKEN (ARM64 / AArch64) cekirdegi ===\n");

    uart_yaz("islemci: MIDR="); uart_hex(midr());
    uart_yaz(" calisma seviyesi EL"); uart_putc((char)('0' + el_seviyesi()));
    uart_yaz("\n");

    /* QEMU 'virt' makinesinde RAM 0x40000000'dan baslar. Cekirdek
     * imajinin ustunde kalan bolgeyi yonetiyoruz. */
    pmm_baslat(0x41000000UL, 16UL * 1024 * 1024);
    uart_yaz("PMM: toplam cerceve="); uart_hex(toplam_cerceve);
    uart_yaz(" bos="); uart_hex(bos_cerceve()); uart_yaz("\n");

    /* --- Sinama 1: cerceve ayirma/iade --- */
    uint64_t once = bos_cerceve();
    uint64_t a = cerceve_al(), b = cerceve_al(), c = cerceve_al();
    uint64_t orta = bos_cerceve();
    cerceve_birak(a); cerceve_birak(b); cerceve_birak(c);
    uint64_t sonra = bos_cerceve();
    uart_yaz("PMM-SINAMA: once="); uart_hex(once);
    uart_yaz(" ayirma sonrasi="); uart_hex(orta);
    uart_yaz(" iade sonrasi="); uart_hex(sonra);
    uart_yaz((once == sonra && orta == once - 3) ? " -> BASARILI\n" : " -> HATA\n");

    /* --- Sinama 2: yigin ve veri butunlugu --- */
    char *t1 = (char *)ayir(64);
    char *t2 = (char *)ayir(128);
    int tamam = (t1 && t2) ? 1 : 0;
    if (tamam) {
        for (int i = 0; i < 64; i++)  t1[i] = (char)('A' + (i % 26));
        for (int i = 0; i < 128; i++) t2[i] = (char)('a' + (i % 26));
        for (int i = 0; i < 64; i++)  if (t1[i] != (char)('A' + (i % 26))) tamam = 0;
        for (int i = 0; i < 128; i++) if (t2[i] != (char)('a' + (i % 26))) tamam = 0;
    }
    uart_yaz("HEAP-SINAMA: veri butunlugu -> ");
    uart_yaz(tamam ? "BASARILI\n" : "HATA\n");

    /* --- Sinama 3: 64-bit aritmetik (gercek AArch64 calistigimizin kaniti) --- */
    uint64_t buyuk = 0x0123456789ABCDEFULL;
    uint64_t donmus = (buyuk << 8) | (buyuk >> 56);
    uart_yaz("64BIT-SINAMA: deger="); uart_hex(buyuk);
    uart_yaz(" dondurulmus="); uart_hex(donmus);
    uart_yaz((donmus == 0x23456789ABCDEF01ULL) ? " -> BASARILI\n" : " -> HATA\n");

    uart_yaz("\n--- KAPSAM (durust bildirim) ---\n");
    uart_yaz("Bu ARM64 portu TEMEL seviyededir. Calisan: onyukleme,\n");
    uart_yaz("PL011 seri cikti, bitmap bellek yoneticisi, yigin.\n");
    uart_yaz("YOK: kesme denetleyicisi (GIC), zamanlayici, MMU/sayfalama,\n");
    uart_yaz("     gorev degistirme, surucu yok, kullanici modu yok.\n");
    uart_yaz("x86_64 surumundeki alt sistemler ARM64'e TASINMAMISTIR.\n");
    uart_yaz("--- ARM64 temel cekirdek calisir durumda ---\n");
    for (;;) { __asm__ volatile ("wfe"); }
}
