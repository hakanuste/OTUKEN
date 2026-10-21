/* ÜLGEN arayüzü: konuşma ekranı, durum çubuğu, ajan/görev görünümü. */
#ifndef UMAY_GUI_H
#define UMAY_GUI_H
#include "umay.h"
#include "umay_alp.h"

#define GUI_SATIR_MAX 64
#define GUI_SUTUN_MAX 96

int  gui_ac(int en, int boy);
int  gui_acik_mi(void);
/* Ön yüz uçbirimi kendisi okuyup yazıyor mu? Konsol ön yüzünde 1'dir:
 * o durumda klavye kanalı KAYDEDİLMEZ, yoksa ikisi aynı stdin'den
 * okur ve girdi ikiye bölünür; yanıt da iki kez basılır. */
int  gui_konsolu_sahiplenir(void);
void gui_kapat(void);
/* Kullanıcı pencere kapatma düğmesine bastı mı? */
int  gui_kapat_istendi(void);
void gui_satir_ekle(const char *kim, const char *metin);   /* konuşma akışı */
void gui_durum(const char *metin);
void gui_ciz(const otag_t *o);
int  gui_giris_yokla(umay_ileti_t *i);                     /* 1 = yeni söz */
/* Otağ görünümünü istek üzerine basar. Konsol ön yüzünde anlamlıdır;
 * ÜLGEN ön yüzü otağı zaten sürekli çizer, orada boştur. */
void gui_otag_yaz(const otag_t *o);
#endif
