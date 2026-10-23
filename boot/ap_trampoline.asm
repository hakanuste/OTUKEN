; ap_trampoline.asm - SMP adim 3: uygulama islemcisi (AP) giris kodu.
;
; INIT-SIPI-SIPI ile uyandirilan AP, 16-bit GERCEK MODDA, CS:IP =
; (vektor<<8):0000 adresinden baslar. Vektor 0x08 -> fiziksel 0x8000.
; Bu kod cekirdek imajinda .rodata icinde durur; smp.c onu fiziksel
; 0x8000'e KOPYALAR ve veri alanini (0x8F00) doldurur.
;
; Konumdan bagimsiz degildir: tum adresler T(x) = x - bas + 0x8000
; olarak mutlak hesaplanir (ayni bolumdeki etiket farki sabittir,
; yeniden konumlandirma gerektirmez).
;
; Akis: gercek mod -> gecici GDT -> 32-bit korumali mod -> BSP'nin
; CR4/CR3/EFER/CR0 degerleri -> 64-bit -> cekirdek yigini -> C (ap_giris).

TRAMP_TABAN equ 0x8000
%define T(x) ((x) - ap_tramp_bas + TRAMP_TABAN)

section .rodata
align 16
global ap_tramp_bas
global ap_tramp_son

bits 16
ap_tramp_bas:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    lgdt [T(tr_gdtr)]
    mov eax, cr0
    or  eax, 1                  ; PE
    mov cr0, eax
    jmp dword 0x08:T(tr_pm32)

bits 32
tr_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, [T(v_cr4)]         ; BSP CR4 (PAE dahil, PCIDE/LA57 maskeli)
    mov cr4, eax
    mov eax, [T(v_cr3)]         ; cekirdek PML4 (4GB alti zorunlu)
    mov cr3, eax
    mov ecx, 0xC0000080         ; EFER: LME (+NXE, SCE) - BSP ile ayni
    mov eax, [T(v_efer)]
    xor edx, edx
    wrmsr
    mov eax, [T(v_cr0)]         ; PG|PE|WP|... - BSP ile ayni
    mov cr0, eax
    jmp 0x18:T(tr_lm64)

bits 64
tr_lm64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [T(v_yigin)]
    mov rdi, [T(v_arg)]
    mov rax, [T(v_giris)]
    call rax
.dur:
    cli
    hlt
    jmp .dur

align 8
tr_gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit kod
    dq 0x00CF92000000FFFF       ; 0x10: veri
    dq 0x00AF9A000000FFFF       ; 0x18: 64-bit kod
tr_gdt_son:
tr_gdtr:
    dw tr_gdt_son - tr_gdt - 1
    dd T(tr_gdt)

; --- Veri alani: sabit ofset 0xF00 (fiziksel 0x8F00). smp.c yazar. ---
; Kod bu sinira tasarsa asagidaki 'times' negatif olur ve NASM hata verir.
    times 0xF00 - ($ - ap_tramp_bas) db 0
v_cr3:   dq 0
v_efer:  dq 0
v_cr0:   dq 0
v_cr4:   dq 0
v_yigin: dq 0
v_giris: dq 0
v_arg:   dq 0
ap_tramp_son:

section .note.GNU-stack noalloc noexec nowrite progbits
