// ÖTÜKEN ARM64 (AArch64) onyukleme kodu
//
// x86-64 surumunden TEMEL farklar:
//  - BIOS/GRUB yok: QEMU virt makinesi cekirdegi dogrudan 0x40080000'a
//    yukleyip EL1'de (ya da EL2'de) calistirir.
//  - Sayfalama (MMU) baslangicta KAPALIDIR; fiziksel adresleme gecerlidir.
//  - Cok cekirdekli baslatma: tum CPU'lar ayni noktadan baslar, biz
//    yalnizca 0 numarali cekirdegi calistirip digerlerini uyutuyoruz.

.section ".text.boot"
.global _start

_start:
    // Hangi CPU cekirdegindeyiz? (MPIDR_EL1'in alt 2 biti)
    mrs     x0, mpidr_el1
    and     x0, x0, #3
    cbz     x0, cekirdek0
    // Diger cekirdekler: sonsuz uykuya yat (WFE = olay bekle)
bekle:
    wfe
    b       bekle

cekirdek0:
    // Yigin isaretcisini kur (cekirdek imajinin hemen altina)
    ldr     x0, =_stack_top
    mov     sp, x0

    // .bss bolumunu sifirla - C kodu sifirlanmis genel degiskenler
    // bekler; bunu yapmazsak cop degerlerle calisiriz.
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
sifirla:
    cmp     x0, x1
    b.ge    sifir_bitti
    str     xzr, [x0], #8
    b       sifirla
sifir_bitti:

    bl      cekirdek_ana

// cekirdek_ana donmemeli; donerse guvenli sekilde dur
dur:
    wfe
    b       dur
