#ifndef TURKCE_TABLO_H
#define TURKCE_TABLO_H
typedef struct { const char *a; const char *d; } tt_kayit_t;   /* anahtar (UTF-8, bayt sıralı) -> değer */
extern const tt_kayit_t tt_harf[], tt_vurgu_son[], tt_vurgu_istisna[], tt_ses_istisna[], tt_kimlik[];
extern const int tt_harf_sayi, tt_vurgu_son_sayi, tt_vurgu_istisna_sayi, tt_ses_istisna_sayi, tt_kimlik_sayi;
#endif
