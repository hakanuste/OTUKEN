; interrupt.asm - 64-bit ISR/IRQ giris noktalari.
;
; PUSHA/POPA 64-bit modda GECERSIZ opcode'dur; genel amacli 15 registeri
; tek tek push/pop ediyoruz. Argument, System V AMD64 ABI'ye gore RDI'da
; gecirilir (32-bit'teki gibi stack'e push edilmez).

%macro PUSH_ALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common_noerr
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common_err
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern isr_handler
extern irq_handler

; NOERR yolu: int_no + sahte err_code(0) bizim tarafimizdan push edildi.
; Toplam (donanim+bizim push'larimiz) = 160 bayt -> call'dan once RSP 16
; hizali kalir (on kosul: kesme oncesi RSP zaten 16 hizaliydi).
isr_common_noerr:
    PUSH_ALL
    mov rdi, rsp
    call isr_handler
    POP_ALL
    add rsp, 16
    iretq

; ERR yolu: donanim GERCEK err_code'u NOERR'deki sahte err_code ile
; AYNI konuma (int_no'nun hemen ustune) koyar - cunku donanimin kendi
; push sirasi [rflags,cs,rip,error_code] seklindedir (error_code EN SON
; push edilir, yani bizim sonraki push'umuza -int_no'ya- en yakin olan
; odur). Bu yuzden ERR yolu da NOERR ile AYNI temizligi (16 bayt) yapar.
isr_common_err:
    PUSH_ALL
    mov rdi, rsp
    call isr_handler
    POP_ALL
    add rsp, 16
    iretq

; IRQ yolu: CPU istisnasi degil, donanim kesmesi - irq_handler cagirir
; (PIC'e EOI gonderme mantigi orada). Hic hardware err_code olmadigi
; icin duzeni ISR_NOERR ile ayni (160 bayt, cleanup=16).
irq_common:
    PUSH_ALL
    mov rdi, rsp
    call irq_handler
    POP_ALL
    add rsp, 16
    iretq

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
