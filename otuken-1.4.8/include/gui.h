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

#endif
