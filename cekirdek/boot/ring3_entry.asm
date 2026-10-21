; ring3_entry.asm - sahte bir iretq cercevesi kurup ring3'e "geri donerek"
; atlar (context switch trampolinimizle ayni felsefe: iretq'nun kendisi
; hicbir zaman GERCEKTEN bir yerden "donmuyor", sadece CPU'yu kandirip
; istedigimiz baglamda calismaya baslatiyoruz).
;
; void enter_ring3(uint64_t entry_rip, uint64_t user_rsp)
; RDI = ring3 kodunun baslayacagi adres
; RSI = ring3 yig ininin (stack) baslangic (en yuksek adres) degeri

global enter_ring3
enter_ring3:
    mov ax, (0x20 | 3)   ; kullanici veri segmenti secici, RPL=3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword (0x20 | 3)  ; SS (kullanici verisi, RPL=3)
    push rsi                ; RSP (kullanici yigini)
    pushfq                  ; RFLAGS (mevcut degerler - IF=1 korunur)
    push qword (0x28 | 3)  ; CS (kullanici kodu, RPL=3)
    push rdi                ; RIP (giris noktasi)
    iretq

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
