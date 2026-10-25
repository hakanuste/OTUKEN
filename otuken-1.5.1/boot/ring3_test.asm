; ring3_test.asm - Ring3'te (kullanici modu) calisacak KUCUK, elle
; yazilmis test programi. Henuz gercek bir ELF yukleyici yok (bu,
; "Ring3 hazirligi/altyapi" asamasi) - bu yuzden bu kod, kernel imaji
; icinde derlenip runtime'da kullanici-erisilebilir bir sayfaya
; KOPYALANACAK. Bu yuzden hicbir bellek referansi (string/degisken)
; ICERMEMELI - sadece anlik degerler ve syscall kullanir, boylece
; nereye kopyalanirsa kopyalansin dogru calisir (relocation gerekmez).

section .text
bits 64

global ring3_test_program
global ring3_test_program_end

ring3_test_program:
    ; SYS_RING3_PROOF (4) cagir - cekirdek tarafinda "ring3 kodu
    ; GERCEKTEN calisti" bayragini set eder.
    mov rax, 4
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    syscall

    ; SYS_EXIT (5) cagir - kernel tarafinda task_exit() tetikler,
    ; bu gorev bir daha ASLA calismaz.
    mov rax, 5
    xor rdi, rdi
    syscall

.hang:
    ; Buraya ASLA ulasilmamali (SYS_EXIT donmemeli) - guvenlik agi.
    jmp .hang

ring3_test_program_end:

; --- Calistirilamaz yigin bildirimi ---
; Bu bolum OLMAZSA baglayici, yigini CALISTIRILABILIR (RWE) varsayar
; ve program basliginda GNU_STACK RWE olur. Calistirilabilir yigin,
; yigin tabanli saldirilarda kod calistirmayi mumkun kilar; guvenlik
; acisindan istenmez. Bos bir .note.GNU-stack bolumu, yigina
; calistirma izni GEREKMEDIGINI bildirir.
section .note.GNU-stack noalloc noexec nowrite progbits
