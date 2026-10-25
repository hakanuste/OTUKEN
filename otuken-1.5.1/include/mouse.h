#ifndef OTUKENOS_MOUSE_H
#define OTUKENOS_MOUSE_H

void mouse_install(void);
void mouse_get_state(int *x, int *y, int *buttons);
/* Son okumadan beri fare hareket etti/tiklandi mi? Okunca bayragi sifirlar. */
int mouse_poll_dirty(void);

#endif
