#ifndef OTUKENOS_RING3_H
#define OTUKENOS_RING3_H

/* Ring3 test programini (ring3_test.asm) kullanici-erisilebilir bir
 * sayfaya kopyalar, ayri bir kullanici yigini ayirir, ve enter_ring3
 * ile oraya atlar. GERI DONMEZ (ring3 kodu SYS_EXIT ile mevcut gorevi
 * sonlandirir - bkz. task_exit). tss_init() bundan ONCE cagirilmis
 * OLMALIDIR. */
void ring3_run_test(void);

/* Gomulu GERCEK ELF64 programini ayristirip Ring3'te calistirir.
 * GERI DONMEZ. */
void ring3_run_elf(void);

/* OTUKEN Kabugunu (Shell 2.0) Ring3'te baslatir. GERI DONMEZ. */
void ring3_run_shell(void);

/* UMAY'i DISKTEN yukleyip Ring3'te baslatir (gomulu degil!). */
int ring3_umay_calistir(const char *yol);

#endif
