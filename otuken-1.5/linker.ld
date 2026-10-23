ENTRY(_start)

/* --- Stage 2 tamamlanmasi: Higher Half Kernel ---
 * Cekirdek artik sanal adres uzayinin UST yarisinda (0xFFFFFFFF80000000)
 * calisir; fiziksel olarak yine 1MB'de yuklu kalir. Bunun degeri:
 * kullanici surecleri adres uzayinin ALT yarisini serbestce
 * kullanabilir, cekirdek her surecte AYNI ust adreslerde haritali kalir.
 * Bu, ileride her surece ayri bir adres uzayi (ayri CR3) vermenin
 * on kosuludur. */
KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x100000;

SECTIONS
{
    . = KERNEL_VMA + KERNEL_LMA;
    kernel_start = .;

    /* Multiboot basligi ve 32-bit onyukleme kodu FIZIKSEL adreste
     * calismak ZORUNDA (henuz sayfalama yok, ust-yari haritasi yok) -
     * bu yuzden ayri bir bolume (.boot) alinip DUSUK adreste tutulur. */
    .boot KERNEL_LMA : AT(KERNEL_LMA)
    {
        *(.multiboot)
        *(.boot)
    }

    . = KERNEL_VMA + KERNEL_LMA + SIZEOF(.boot);

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(COMMON)
        *(.bss)
    }

    kernel_end = .;
    kernel_phys_start = KERNEL_LMA;
    kernel_phys_end = . - KERNEL_VMA;
}
