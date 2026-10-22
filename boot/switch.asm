; switch.asm - gorevler arasi baglami degistirme (context switch).
;
; System V AMD64 ABI'ye gore SADECE callee-saved registerlari (rbx,
; rbp, r12-r15) korumak yeterli - caller-saved olanlar zaten kesme
; stub'u tarafindan (PUSH_ALL/POP_ALL) veya normal fonksiyon cagirma
; kuraliyla halledilir.

global context_switch
; void context_switch(uint64_t *old_rsp_ptr, uint64_t new_rsp)
; RDI = eski gorevin rsp'sinin KAYDEDILECEGI yer (bir uint64_t*)
; RSI = yeni gorevin rsp DEGERI (bir uint64_t)
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp   ; eski gorevin rsp'sini kaydet
    mov rsp, rsi      ; yeni gorevin rsp'sine gec

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret               ; yeni gorevin "kaldigi yere" (ya da ilk kez
                       ; calisiyorsa task_trampoline'a) atlar

; Yeni olusturulan bir gorevin ILK kez calistirilmasi icin kucuk bir
; sicrama tahtasi. context_switch'in pop sirasina gore RBX'e gorev
; giris fonksiyonunun adresi yerlestirilmis olur (bkz. sched.c
; task_create). Burada 'sti' calistirmak SART: kesmeler, bir kesme
; kapisindan girildigi icin (ve context_switch bunu degistirmedigi
; icin) hala KAPALI durumda olur - acmazsak bu gorev hicbir zaman
; zamanlayici tarafindan yeniden bolunemez (preempt edilemez).
extern sched_gecis_sonrasi
extern task_exit
global task_trampoline
task_trampoline:
    ; SMP adim 5: yeni gorev ILK kez calisiyor - gecisin ikinci yarisini
    ; (eski gorevi birak, rq kilidini ac) tamamla, sonra kesmeleri ac.
    ; rbx ABI geregi korunur; RSP burada 16 hizali (call oncesi dogru).
    call sched_gecis_sonrasi
    sti
    call rbx
    ; Giris islevi GERI DONDU: eskiden asagidaki cli;hlt ile TUM CEKIRDEK
    ; olduruluyordu (tek cekirdekte sistem donardi). Gorevi duzgunce
    ; sonlandir; task_exit geri donmez.
    call task_exit
.hang:
    cli
    hlt
    jmp .hang

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
