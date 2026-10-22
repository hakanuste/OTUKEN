#ifndef OTUKENOS_GUI_H
#define OTUKENOS_GUI_H

void gui_init(void);
void gui_type_char(char c);
void gui_on_mouse_event(void);
/* Stage 18: gunluk penceresini canli tutmak icin periyodik yeniden cizim */
void gui_refresh(void);

/* Bir dizenin TAMAMINI terminal tamponuna isleyip TEK bir yeniden
 * cizim yapar (karakter basina tam ekran cizimden cok daha hizli). */
void gui_write_string(const char *s);

/* Pencere kimlikleri (gui.c ile ayni) */
#define GUI_TERMINAL 0
#define GUI_DOSYA    1
#define GUI_GORUNTU  2
#define GUI_GUNLUK   3
#define GUI_PENCERE_SAYISI 4
#define GUI_GOREV_CUBUGU_H 28   /* alt gorev cubugu yuksekligi */

int  gui_pencere_bilgi(int idx, int *x, int *y, int *w, int *h, int *gorunur);
void gui_pencere_yerlestir(int idx, int x, int y, int w, int h, int gorunur);
void gui_fare_isle(int mx, int my, int buttons);
void gui_terminal_temizle(void);
/* Ana dongunun ekran adimi (periyodik: tam yenileme zamani geldi). */
void masaustu_tur(int periyodik);
/* Birlestirici icin (gorsel_kilit tutulurken) */
void gui_pencere_ciz_ic(int idx);
int  gui_pencere_z(int idx);
void gui_ust_katman_ciz_ic(void);

#endif
