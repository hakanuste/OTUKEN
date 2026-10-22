; boot.asm - Multiboot1 header + 32-bit -> 64-bit (long mode) gecis.
;
; CPU, GRUB tarafindan HER ZAMAN 32-bit korumali modda baslatilir
; (multiboot spesifikasyonu geregi) - 64-bit bir cekirdek bile bu asamadan
; gecmek ZORUNDADIR. Bu dosya once 32-bit calisir: sayfa tablolarini kurar,
; PAE + long mode'u acar, kendi 64-bit GDT'mizi yukler, sonra 64-bit kod
; segmentine uzak atlama (far jump) yaparak GERCEK 64-bit yurutmeye gecer.

KERNEL_VMA  equ 0xFFFFFFFF80000000
%define PHYS(x) ((x) - KERNEL_VMA)

; ===================== ONYUKLEME BASLIKLARI =====================
; IKI baslik birden koyuyoruz:
;
;  1) Multiboot1 - eski BIOS makineleri icin (geriye donuk uyum)
;  2) Multiboot2 - MODERN makineler icin. GRUB'in UEFI surumu
;     YALNIZCA multiboot2 anlar. AM5 gibi CSM'siz anakartlarda
;     multiboot1 ISO HIC ACILMAZ; bu yuzden UEFI destegi
;     multiboot2'den gecer.
;
; GRUB hangisini bulursa onu kullanir; cekirdek her iki durumda da
; ayni giris noktasindan (_start) baslar ve sihirli sayiya bakarak
; hangi protokolle acildigini anlar.

MBALIGN     equ  1<<0
MEMINFO     equ  1<<1
VIDEOINFO   equ  1<<2
FLAGS       equ  MBALIGN | MEMINFO | VIDEOINFO
MAGIC       equ  0x1BADB002
CHECKSUM    equ -(MAGIC + FLAGS)

MB2_MAGIC   equ  0xE85250D6
MB2_ARCH    equ  0                    ; i386 (32-bit korumali mod)

section .multiboot
align 8
; --- Multiboot1 basligi ---
mb1_bas:
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0, 0, 0, 0, 0
    dd 0            ; linear framebuffer
    dd 1024
    dd 768
    dd 32

; --- Multiboot2 basligi (UEFI icin SART) ---
align 8
mb2_bas:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd mb2_son - mb2_bas
    dd -(MB2_MAGIC + MB2_ARCH + (mb2_son - mb2_bas))

    ; -- framebuffer etiketi: UEFI'de GOP, BIOS'ta VBE saglar --
align 8
    dw 5            ; tur: framebuffer
    dw 0            ; bayraklar
    dd 20           ; boyut
    dd 1024         ; genislik
    dd 768          ; yukseklik
    dd 32           ; bit derinligi

    ; -- bitis etiketi --
align 8
    dw 0
    dw 0
    dd 8
mb2_son:

section .bss
align 4096
p4_table:    resb 4096
p3_table:    resb 4096
p2_table_0:  resb 4096   ; 0-1GB
p2_table_1:  resb 4096   ; 1-2GB
p2_table_2:  resb 4096   ; 2-3GB
p2_table_3:  resb 4096   ; 3-4GB
align 16
stack_bottom:
    resb 65536
stack_top:

; 1.4.9: GDT .data'da: tss.c TSS tanimlayicisini calisirken yazar, CPU
; erisim bitlerini isaretler. .rodata'da durmasi TAMGA'nin cekirdek ozetini
; (kod + salt okunur veri) her acilista degistiriyordu.
section .data
align 16
global gdt64
gdt64:
    dq 0x0000000000000000
.code: equ $ - gdt64
    dq 0x00AF9A000000FFFF   ; 64-bit kod segmenti (L=1, D=0, P=1, E=1, S=1)
.data: equ $ - gdt64
    dq 0x00CF92000000FFFF   ; duz veri segmenti
.user_placeholder: equ $ - gdt64
    ; SYSRET'in STAR MSR offset aritmetigi geregi burada bir 32-bit
    ; uyumluluk kod segmenti "yer tutucusu" olmasi GEREKIYOR (Intel'in
    ; tarihsel tasarim kisitlamasi) - gercekte hicbir zaman yuklenmez/
    ; calistirilmaz, ama STAR[63:48] hesaplamasinin doğru cikmasi icin
    ; burada bulunmasi sart.
    dq 0x00CFFA000000FFFF   ; 32-bit kod, DPL=3 (yer tutucu, kullanilmaz)
.user_data: equ $ - gdt64
    dq 0x00CFF2000000FFFF   ; kullanici verisi, DPL=3
.user_code: equ $ - gdt64
    dq 0x00AFFA000000FFFF   ; kullanici 64-bit kodu, DPL=3
.tss: equ $ - gdt64
    ; TSS descriptor'i (16 bayt, sistem segmenti) - GERCEK deger
    ; runtime'da C tarafindan (tss.c) tss'nin gercek adresi bilindiginde
    ; buraya YAZILACAK (patch edilecek). Simdilik sifir yer tutucu.
    dq 0x0000000000000000
    dq 0x0000000000000000
.pointer:
    dw $ - gdt64 - 1
    ; GDT tabanini FIZIKSEL adres olarak veriyoruz: bu lgdt komutu
    ; 32-bit uyumluluk modunda calisiyor ve orada taban 32-bit'e
    ; KIRPILIR - higher-half adresi (0xFFFFFFFF80...) verilirse
    ; gecersiz bir GDT'ye isaret eder ve CPU uc-kat hata (triple fault)
    ; ile resetlenir. Fiziksel adres hem 32-bit'te hem 64-bit'te
    ; gecerlidir (alt yari kimlik haritali).
    dq PHYS(gdt64)

section .boot
bits 32
global _start
_start:
    mov esp, PHYS(stack_top)
    mov [PHYS(saved_magic)], eax
    mov [PHYS(saved_mbi)], ebx

    ; --- CPUID var mi? (EFLAGS.ID bitini degistirebiliyor muyuz) ---
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1<<21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid

    ; --- Uzatilmis CPUID fonksiyonlari var mi? ---
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    ; --- Long mode (64-bit) destegi var mi? (CPUID.80000001h:EDX[29]) ---
    mov eax, 0x80000001
    cpuid
    test edx, 1<<29
    jz .no_long_mode

    call print_ok_32   ; seri porta 'A' yaz: buraya kadar geldik

    call setup_page_tables
    call print_ok_32b  ; 'B': sayfa tablolari hazir

    ; PAE ac (CR4 bit5)
    mov eax, cr4
    or eax, 1<<5
    mov cr4, eax

    ; CR3 = PML4
    mov eax, PHYS(p4_table)
    mov cr3, eax

    ; EFER.LME ac
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1<<8
    wrmsr

    ; Sayfalamayi ac (CR0 bit31) -> artik "compatibility mode"
    mov eax, cr0
    or eax, 1<<31
    mov cr0, eax

    call print_ok_32c  ; 'C': paging+LME acik

    lgdt [PHYS(gdt64.pointer)]

    call print_ok_32d  ; 'D': 64-bit GDT yuklendi, atlamaya hazir

    jmp gdt64.code:PHYS(long_mode_start)

.no_cpuid:
    mov al, 'x'
    jmp .fail
.no_long_mode:
    mov al, 'y'
.fail:
    mov dx, 0x3F8
    out dx, al
.hang32:
    hlt
    jmp .hang32

; --- Basit seri port yazma yardimcilari (henuz C calisamiyor) ---
print_ok_32:
    mov dx, 0x3F8
    mov al, 'A'
    out dx, al
    ret
print_ok_32b:
    mov dx, 0x3F8
    mov al, 'B'
    out dx, al
    ret
print_ok_32c:
    mov dx, 0x3F8
    mov al, 'C'
    out dx, al
    ret
print_ok_32d:
    mov dx, 0x3F8
    mov al, 'D'
    out dx, al
    ret

; --- Sayfa tablolarini kur ---
;
; MODERN DONANIM DESTEGI: onceden yalnizca 0-4GB kimlik haritasi
; kuruluyordu (4 adet PD tablosu, 2MB sayfa). 4GB'dan fazla RAM'i olan
; makinelerde (ornegin 256GB) cekirdek bellegin ancak %1.5'ini
; gorebiliyordu.
;
; Artik CPU 1GB sayfa (PDPTE.PS) destekliyorsa TEK bir PDPT ile
; 512GB kimlik haritasi kuruyoruz - hem cok daha genis hem de PD
; tablolarina hic gerek kalmiyor. Desteklemeyen eski islemcilerde
; onceki 4GB / 2MB yoluna geri duşuyoruz.
setup_page_tables:
    ; --- 1GB sayfa destegini sina: CPUID.80000001h:EDX bit 26 ---
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .iki_mb_yolu              ; genisletilmis yaprak yok -> eski yol
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 26            ; PDPE1GB
    jz .iki_mb_yolu

; ================= 1GB SAYFA YOLU (modern islemciler) =================
.bir_gb_yolu:
    mov eax, PHYS(p3_table)
    or eax, 0x03
    mov [PHYS(p4_table)], eax          ; PML4[0]   -> kimlik haritasi
    mov [PHYS(p4_table) + 511*8], eax  ; PML4[511] -> ust yari (ayni PDPT)

    ; PDPT[0..511] = 1GB kimlik sayfalari (toplam 512GB).
    ; Bayraklar: P|RW|PS  (PS = bit7 -> 1GB sayfa)
    ; ECX = giris indeksi. Fiziksel adres = ECX * 1GB.
    ; DUZELTME: onceden EAX'e 0x40000000 ekleyip ADC ile tasima
    ; yapiliyordu; ancak bayraklari bozan ara komutlar yuzunden tasima
    ; guvenilir degildi ve sistem burada donuyordu. Artik her giris
    ; icin adresi indeksten DOGRUDAN hesapliyoruz:
    ;   dusuk 32 bit = (ECX & 3) << 30      (1GB'lik kaymalar)
    ;   ust   32 bit = ECX >> 2             (4GB sinirlari)
    mov ecx, 0
.gb_dongu:
    mov eax, ecx
    and eax, 3
    shl eax, 30                         ; (ecx % 4) * 1GB
    or  eax, 0x83                       ; P | RW | PS(1GB sayfa)
    mov edx, ecx
    shr edx, 2                          ; ecx / 4  -> ust 32 bit
    mov [PHYS(p3_table) + ecx*8], eax
    mov [PHYS(p3_table) + ecx*8 + 4], edx
    inc ecx
    cmp ecx, 512
    jb .gb_dongu

    ; Higher-half: PDPT[510] fiziksel 0'i gostermeli ki
    ; 0xFFFFFFFF80000000 -> fiziksel 0 olsun (linker.ld boyle bekliyor).
    mov dword [PHYS(p3_table) + 510*8], 0x83
    mov dword [PHYS(p3_table) + 510*8 + 4], 0
    ret

; ================= 2MB SAYFA YOLU (eski islemciler) =================
.iki_mb_yolu:
    mov eax, PHYS(p3_table)
    or eax, 0x03
    mov [PHYS(p4_table)], eax
    mov [PHYS(p4_table) + 511*8], eax

    mov eax, PHYS(p2_table_0)
    or eax, 0x03
    mov [PHYS(p3_table) + 510*8], eax  ; higher-half -> ilk 1GB

    mov eax, PHYS(p2_table_0)
    or eax, 0x03
    mov [PHYS(p3_table)], eax
    mov eax, PHYS(p2_table_1)
    or eax, 0x03
    mov [PHYS(p3_table) + 8], eax
    mov eax, PHYS(p2_table_2)
    or eax, 0x03
    mov [PHYS(p3_table) + 16], eax
    mov eax, PHYS(p2_table_3)
    or eax, 0x03
    mov [PHYS(p3_table) + 24], eax

    ; --- PD tablolarini 2MB kimlik sayfalariyla doldur ---
    ; Her PD 512 giris = 1GB; dort tablo = 4GB.
    ; Giris degeri: fiziksel adres | P|RW|PS(2MB)
    ; ECX = kuresel 2MB sayfa indeksi (0..2047)
    mov ecx, 0
.mb_dongu:
    mov eax, ecx
    shl eax, 21                 ; ecx * 2MB (dusuk 32 bit)
    or  eax, 0x83               ; P | RW | PS
    mov edx, ecx
    shr edx, 11                 ; ust 32 bit (2048 sayfa = 4GB sinirinda 1)

    ; Hedef tablo: ecx / 512  -> p2_table_0..3
    mov ebx, ecx
    shr ebx, 9                  ; tablo indeksi 0..3
    mov esi, ecx
    and esi, 511                ; tablo ici giris indeksi

    cmp ebx, 0
    jne .t1
    mov [PHYS(p2_table_0) + esi*8], eax
    mov [PHYS(p2_table_0) + esi*8 + 4], edx
    jmp .sonraki
.t1:
    cmp ebx, 1
    jne .t2
    mov [PHYS(p2_table_1) + esi*8], eax
    mov [PHYS(p2_table_1) + esi*8 + 4], edx
    jmp .sonraki
.t2:
    cmp ebx, 2
    jne .t3
    mov [PHYS(p2_table_2) + esi*8], eax
    mov [PHYS(p2_table_2) + esi*8 + 4], edx
    jmp .sonraki
.t3:
    mov [PHYS(p2_table_3) + esi*8], eax
    mov [PHYS(p2_table_3) + esi*8 + 4], edx
.sonraki:
    inc ecx
    cmp ecx, 2048               ; 2048 * 2MB = 4GB
    jb .mb_dongu
    ret

section .bss
align 8
saved_magic: resd 1
saved_mbi:   resd 1

section .text
bits 64
extern kernel_main
long_mode_start:
    ; Su an DUSUK (fiziksel) adreste calisiyoruz. Ust-yari haritasi
    ; hazir oldugu icin, mutlak bir uzak atlama ile YUKSEK adrese
    ; geciyoruz - bundan sonra tum kod/veri erisimleri higher-half
    ; adreslerinden yapilir.
    mov rax, higher_half_entry
    jmp rax

higher_half_entry:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top

    ; seri porta 'E' yaz: 64-bit moddayiz
    mov dx, 0x3F8
    mov al, 'E'
    out dx, al

    xor rdi, rdi
    mov edi, [saved_magic]
    xor rsi, rsi
    mov esi, [saved_mbi]

    call kernel_main

.hang64:
    cli
    hlt
    jmp .hang64

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
