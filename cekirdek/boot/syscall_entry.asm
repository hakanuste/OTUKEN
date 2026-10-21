; syscall_entry.asm - SYSCALL komutuyla buraya dusulur.
;
; --- ABI KURALI (denetimde bulunan kritik hata) ---
; Donanimin SYSCALL komutu SADECE RCX (donus RIP) ve R11 (RFLAGS)
; registerlarini bozar. Kullanici kodu -ve derleyici- diger TUM
; registerlarin korunacagini VARSAYAR. Nitekim GCC, syscall numarasini
; bir dongu boyunca R8'de tutup "mov %r8,%rax" ile yerlestiriyordu.
;
; Bizim cekirdek tarafimiz ise araya bir C fonksiyonu (syscall_dispatch)
; soktugu icin System V ABI geregi TUM caller-saved registerlari
; (rdi,rsi,rdx,rcx,r8,r9,r10,r11) bozabilir. Bunlari kaydetmezsek
; kullanicinin R8'i cop olur ve bir sonraki cagri COP bir syscall
; numarasiyla yapilir - gercekten yasanan hata buydu (no=0x00FF0000).
;
; Cozum: gercek cekirdeklerin yaptigi gibi giriste TUM genel amacli
; registerlari kaydedip cikista geri yukluyoruz. Yalnizca RAX donus
; degeriyle degistirilir.
;
; FMASK ile IF giriste temizlenir; yigin degisimi bu sayede atomiktir.

global syscall_entry
extern syscall_dispatch
extern current_kernel_rsp

section .bss
align 8
syscall_scratch_rsp: resq 1
global last_syscall_rip
last_syscall_rip: resq 1

section .text
syscall_entry:
    mov [rel last_syscall_rip], rcx
    mov [rel syscall_scratch_rsp], rsp      ; kullanici RSP'sini gecici sakla
    mov rsp, [rel current_kernel_rsp]        ; GOREVIN kendi cekirdek yiginina gec

    push qword [rel syscall_scratch_rsp]     ; [15] kullanici RSP
    push rcx                                  ; [14] donus RIP
    push r11                                  ; [13] RFLAGS
    ; --- callee-saved ---
    push rbp                                  ; [12]
    push rbx                                  ; [11]
    push r12                                  ; [10]
    push r13                                  ; [9]
    push r14                                  ; [8]
    push r15                                  ; [7]
    ; --- caller-saved: kullanici bunlarin KORUNACAGINI varsayar ---
    push rdi                                  ; [6]
    push rsi                                  ; [5]
    push rdx                                  ; [4]
    push r8                                   ; [3]
    push r9                                   ; [2]
    push r10                                  ; [1]
    sub  rsp, 8                               ; [0] 16-bayt hizalama dolgusu
                                              ; (16 slot = 128 bayt -> call
                                              ;  oncesi RSP 16'nin kati)

    sti                                       ; guvenli yigindayiz, on-alim acilabilir

    ; Register duzenlemesi: (RAX,RDI,RSI,RDX,R10) -> (RDI,RSI,RDX,RCX,R8)
    mov rbx, rdi
    mov rdi, rax
    mov rax, rsi
    mov rsi, rbx
    mov rbx, rdx
    mov rdx, rax
    mov rcx, rbx
    mov r8,  r10

    call syscall_dispatch                     ; donus degeri RAX'te

    cli                                       ; donus dizisi atomik olmali

    add  rsp, 8                               ; hizalama dolgusunu at
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rsi
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11                                   ; RFLAGS
    pop rcx                                   ; donus RIP
    pop rsp                                   ; kullanici RSP
    o64 sysret

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
