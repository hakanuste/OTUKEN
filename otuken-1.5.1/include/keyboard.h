#ifndef OTUKENOS_KEYBOARD_H
#define OTUKENOS_KEYBOARD_H

void keyboard_install(void);
/* Kuyruktan bir sonraki karakteri dondurur, yoksa 0. Ozel tuslar:
 * 8  = backspace, 13 = enter. */
char keyboard_pop_char(void);

/* Harici bir klavye kaynagindan (USB) karakter enjekte eder. */
void keyboard_push_char(char c);

/* Set-1 tarama kodu -> Turkce Q karakteri (USB klavye ile paylasilir). */
char keyboard_scancode_to_char(unsigned char sc, int shift);

/* KLAVYE ODAGI (1.4.4): kuyrugun iki tuketicisi var - kabuk (SYS_READKEY)
 * ve ULGEN penceresi (SYS_PEN_OLAY). Eskiden ikisi de ayni kuyruktan
 * YARISARAK cekiyordu: UMAY penceresini her turda yokladigi icin kabuga
 * yazilan tuslar rastgele kayboluyordu. Artik tuslar yalniz ODAK
 * sahibine verilir. Tab odagi kabuk <-> pencere arasinda degistirir
 * (Tab kuyruga girmez). Varsayilan: kabuk. */
#define KLAVYE_ODAK_KABUK   0
#define KLAVYE_ODAK_PENCERE 1
int  klavye_odak(void);
void klavye_odak_ata(int odak);
/* Cagiran odak sahibi degilse 0 doner ve kuyruga DOKUNMAZ. */
char keyboard_pop_for(int tuketici);

#endif
