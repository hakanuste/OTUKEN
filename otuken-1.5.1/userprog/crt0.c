/* ÖTÜKEN userland baslangic kodu (C runtime zero).
 * Cekirdek ELF'in giris noktasina atlar; burasi main()'i cagirip
 * donusunde SYS_EXIT ile duzgun sonlanmayi garanti eder. */
#include "ulib.h"

extern int main(void);

void _start(void) {
    main();
    exit();     /* main donse bile surec DUZGUN sonlanir */
}
