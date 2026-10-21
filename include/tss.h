#ifndef OTUKENOS_TSS_H
#define OTUKENOS_TSS_H

#include <stdint.h>

/* TSS'i olusturur, GDT'deki (boot.asm/gdt64) ayrilmis 16 baytlik
 * yer tutucuyu GERCEK TSS adresiyle "patch" eder, ve LTR ile yukler.
 * Ring3'ten bir kesme/istisna geldiginde CPU'nun hangi cekirdek
 * yiginina gececegini (RSP0) bu belirler - ring3 test edilmeden
 * ONCE cagirilmasi SARTTIR, aksi halde ilk kesmede (timer, IRQ0)
 * cokme olur. */
void tss_init(void);

/* --- KRITIK (denetimde bulundu) ---
 * Ring3'teki bir gorev kesmeye ugradiginda CPU otomatik olarak
 * TSS.RSP0'daki yigina gecer. Bu TEK bir kuresel yigin olursa, iki
 * farkli gorev sirayla kesintiye ugradiginda AYNI yigini kullanir ve
 * birbirlerinin kayitli baglamini EZERLER (register bozulmasi, cop
 * syscall numaralari, cokme). Bu yuzden zamanlayici, her gorev
 * gecisinde RSP0'i O GOREVIN kendi cekirdek yiginina ayarlamalidir. */
void tss_set_rsp0(uint64_t rsp0);

#endif
