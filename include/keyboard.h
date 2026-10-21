#ifndef OTUKENOS_KEYBOARD_H
#define OTUKENOS_KEYBOARD_H

void keyboard_install(void);
/* Kuyruktan bir sonraki karakteri dondurur, yoksa 0. Ozel tuslar:
 * 8  = backspace, 13 = enter. */
char keyboard_pop_char(void);

/* Harici bir klavye kaynagindan (USB) karakter enjekte eder. */
void keyboard_push_char(char c);

#endif
