ENTRY(_start)
SECTIONS
{
    /* QEMU 'virt' makinesi cekirdegi buraya yukler */
    . = 0x40080000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;

    . = ALIGN(16);
    . = . + 0x8000;          /* 32KB cekirdek yigini */
    _stack_top = .;
}
