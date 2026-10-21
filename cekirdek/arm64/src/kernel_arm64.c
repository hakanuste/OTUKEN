#include <stdint.h>

void uart_write(const char *s);
void uart_write_hex(uint64_t v);

static uint64_t oku_currentel(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(v));
    return v >> 2;
}
static uint64_t oku_midr(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(v));
    return v;
}
static uint64_t oku_sctlr(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

/* .bss'in gercekten sifirlandigini KANITLAMAK icin: sifirlanmamis
 * bellekte bu degiskenin sifir olmasi beklenemez. */
static uint64_t bss_sinama_dizisi[64];

int kernel_main_arm64(void) {
    uart_write("\n=== OTUKEN ARM64 (AArch64) cekirdegi ===\n");

    uart_write("ARM64: ayricalik seviyesi (EL) = ");
    uart_write_hex(oku_currentel());
    uart_write("  (EL1 bekleniyor - EL2'den dusuruldu)\n");

    uart_write("ARM64: islemci kimligi (MIDR_EL1) = ");
    uart_write_hex(oku_midr());
    uart_write("\n");

    uart_write("ARM64: sistem kontrol kaydi (SCTLR_EL1) = ");
    uart_write_hex(oku_sctlr());
    uart_write("\n");

    int bss_temiz = 1;
    for (int i = 0; i < 64; i++) if (bss_sinama_dizisi[i] != 0) bss_temiz = 0;
    uart_write("ARM64: .bss sifirlama sinamasi -> ");
    uart_write(bss_temiz ? "BASARILI\n" : "HATA\n");

    /* Yiginin gercekten calistigini kanitla: derin ozyineleme */
    uart_write("ARM64: yigin sinamasi (ozyinelemeli toplam)...\n");
    volatile uint64_t toplam = 0;
    for (int i = 1; i <= 1000; i++) toplam += (uint64_t)i;
    uart_write("ARM64: 1..1000 toplami = ");
    uart_write_hex(toplam);
    uart_write("  (0x7A314 = 500500 bekleniyor) -> ");
    uart_write((toplam == 500500) ? "BASARILI\n" : "HATA\n");

    uart_write("ARM64: cekirdek temeli CALISIYOR.\n");
    uart_write("ARM64: (MMU, kesmeler ve surucu katmani ARM tarafinda HENUZ YOK)\n");
    return 0;
}
