/* otukenOS kullanici programi - GERCEK bir ELF64 ikilisi olarak
 * derlenir, cekirdek tarafindan ayristirilip Ring3'te calistirilir.
 * Cekirdek kutuphanesi YOK - sadece syscall'lar. */

#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_RING3_PROOF 4
#define SYS_EXIT        5

static long syscall1(long num, long a1) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(num), "D"(a1)
                      : "rcx", "r11", "memory");
    return ret;
}

static long syscall0(long num) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(num)
                      : "rcx", "r11", "memory");
    return ret;
}

/* Veri segmentinde duran bir dize - ELF yukleyicinin PT_LOAD
 * segmentlerini DOGRU adreslere yerlestirdigini kanitlar (yanlis
 * yuklenirse bu dize okunamaz ya da cop cikar). */
static const char message[] =
    "MERHABA! Bu yazi GERCEK bir ELF64 programindan geliyor.\n";

void _start(void) {
    syscall1(SYS_WRITE, (long)message);
    syscall0(SYS_RING3_PROOF);
    syscall0(SYS_EXIT);
    for (;;) { }
}
