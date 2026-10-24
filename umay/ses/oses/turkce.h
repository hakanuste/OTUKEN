#ifndef TURKCE_H
#define TURKCE_H
#include <stdint.h>
/* Metnin bir sonraki cümlesini modelin fonem kimliklerine çevirir (^ ... $,
 * her işaretten sonra dolgu 0). *metin ilerler. Döner: kimlik sayısı,
 * 0 metin bitti, -1 tampon yetmedi. ilk_sesli: cümle ünlüyle mi başlıyor. */
int tr_cumle(const char **metin, int64_t *ids, int azami, int *ilk_sesli);
#endif
