; idt_flush.asm - 64-bit: pointer argumani RDI'da gelir (System V ABI)
global idt_flush
idt_flush:
    lidt [rdi]
    ret

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
