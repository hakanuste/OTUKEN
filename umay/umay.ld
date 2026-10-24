ENTRY(_start)

/* PML4[1] bolgesi (512GB+): her surece OZEL adres alani.
 * 0x800000000 gibi dusuk adresler PML4[0]'da kalir ve cekirdegin
 * PAYLASILAN kimlik haritasiyla cakisir - izolasyon saglanamaz. */

/* AYRI program basliklari (segmentler): kod salt-okunur+calistirilabilir,
 * veri yazilabilir+calistirilamaz. Hepsi tek segmentte olsaydi ELF
 * yukleyici tumunu ayni izinle haritalar, W^X politikasi UYGULANAMAZDI
 * (ve veri salt-okunur olup program cokerdi). */
PHDRS
{
    kod   PT_LOAD FLAGS(5);   /* R + X */
    veri  PT_LOAD FLAGS(6);   /* R + W */
    /* PT_GNU_STACK: yigin YAZILABILIR ama CALISTIRILAMAZ olmali.
     * Bu baslik hic yoksa yukleyiciler yigini calistirilabilir
     * VARSAYABILIR - guvenlik acisindan istenmez. */
    yigin 0x6474e551 FLAGS(6);
}

SECTIONS
{
    /* 1.5: UMAY (ses motoruyla ~2,3 MB) 0x8000A00000'dan baslayinca
     * kabugun yiginina (0x8000C00000) tasiyordu. Heap pencerelerinin
     * (1.5: 512 MB x 32 -> 0x8001000000 .. 0x8401000000) otesine tasindi. Yigin yerinde
     * kaldi (ring3.c UMAY_YIGIN_VIRT). */
    . = 0x9000000000;

    .text : { *(.text*) } :kod
    .rodata : { *(.rodata*) } :kod

    /* Veri segmenti AYRI bir sayfada baslamali - ayni 4KB sayfayi
     * kodla paylasirsa, sayfa izinleri catisir. */
    . = ALIGN(0x1000);

    .data : { *(.data*) } :veri
    .bss  : { *(.bss*) *(COMMON) } :veri
}
