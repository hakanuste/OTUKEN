/* PIPER ses arka ucu — yalniz ana bilgisayar yapiminda.
 * Bkz. ses/umay_piper.c */
#ifndef UMAY_PIPER_H
#define UMAY_PIPER_H

/* Piper, ses modeli ve bir calici; ucu birden var mi? */
int         piper_var(void);
/* Kullanilan ses modelinin yolu (yoksa "yok"). */
const char *piper_ses_adi(void);
/* Metni seslendirir ve calar. 0 = oldu, -1 = olmadi.
 * SAHTE BASARI YOK: bir asama tutmazsa -1 doner. */
int         piper_konus(const char *metin);

#endif
