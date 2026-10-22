#ifndef OTUKENOS_KEYBOARD_H
#define OTUKENOS_KEYBOARD_H
#include <stdint.h>

void keyboard_install(void);
/* Kuyruktan bir sonraki karakteri dondurur, yoksa 0. Ozel tuslar:
 * 8  = backspace, 13 = enter. */
char keyboard_pop_char(void);

/* Harici bir klavye kaynagindan (USB) karakter enjekte eder. */
void keyboard_push_char(char c);

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
/* 1.4.9: genisletilmis tuslar. Kuyrukta 0x80-0x8F; pencere olayinda
 * (klavye_kod_noktasi) U+E000 + (bayt - 0x80). Kabuga verilmez. */
#define KLAVYE_YUKARI        0x80
#define KLAVYE_ASAGI         0x81
#define KLAVYE_SOL           0x82
#define KLAVYE_SAG           0x83
#define KLAVYE_SAYFA_YUKARI  0x84
#define KLAVYE_SAYFA_ASAGI   0x85
#define KLAVYE_BAS           0x86
#define KLAVYE_SON           0x87
#define KLAVYE_SIL           0x88
#define KLAVYE_F5            0x89
#define KLAVYE_F6            0x8A

/* ISO-8859-9 klavye baytini Unicode kod noktasina cevirir (pencere olaylari). */
uint32_t klavye_kod_noktasi(unsigned char c);

#endif
