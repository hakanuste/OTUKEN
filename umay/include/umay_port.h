/* =====================================================================
 * UMAY taşıma (port) katmanı — ÖTÜKEN'e bakan TEK yüzey.
 * Çekirdek mantık (core/, alp/, us/) bu başlıktan başka dış API görmez.
 * ===================================================================== */
#ifndef UMAY_PORT_H
#define UMAY_PORT_H

#include "umay.h"

typedef int32_t up_tut_t;
#define UP_GECERSIZ ((up_tut_t)-1)
/* ÖTÜKEN'de yazma YOL tabanlıdır (SYS_WRITE_FILE), tutamak tabanlı
 * değil. Yazma kipinde açılan dosya için sözde tutamak. */
#define UP_YAZ_TUTAMAK ((up_tut_t)0x7FFF0001)

uint64_t up_ms(void);
void     up_uyu(uint32_t ms);
void     up_birak(void);
void     up_gunluk(const char *satir);
void    *up_ayir(uint64_t n);
void     up_biraktir(void *p);

#define UP_OKU   0x1
#define UP_YAZ   0x2
#define UP_YARAT 0x4
#define UP_EKLE  0x8
up_tut_t up_ac(const char *yol, uint32_t kip);
int64_t  up_oku(up_tut_t t, void *tampon, uint64_t n);
int64_t  up_yaz(up_tut_t t, const void *tampon, uint64_t n);
int      up_kapat(up_tut_t t);
int      up_var_mi(const char *yol);
int      up_dizin_yarat(const char *yol);
/* Dosyayı BAŞTAN yazar (yoksa oluşturur). ÖTÜKEN'de yazma yol
 * tabanlıdır; tutamakla yazma desteklenmez. */
int      up_yaz_yola(const char *yol, const void *tampon, uint64_t n);
int64_t  up_boy(const char *yol);

up_tut_t up_kapi_ac(const char *ad);
up_tut_t up_kapi_bagla(const char *ad);
int      up_gonder(up_tut_t t, const void *v, uint32_t n);
int      up_al(up_tut_t t, void *v, uint32_t n, uint32_t bekle_ms);
/* Yanit hedefi: t kapisindan en son alinan iletinin GONDEREN kapisi.
 * Gonderen bilinmiyorsa 'kendi' dondurulur. (Eskiden yanit UMAY'in
 * KENDI kapisina yaziliyordu: soran surece hic ulasmiyor, ustelik
 * UMAY kendi yanitini yeni buyruk diye yeniden okuyabiliyordu.) */
up_tut_t up_yanit_kapisi(up_tut_t kendi);

typedef struct up_pencere up_pencere_t;

typedef enum {
    UP_OLAY_YOK = 0, UP_OLAY_TUS, UP_OLAY_TIK, UP_OLAY_KAPAT, UP_OLAY_BOYUT
} up_olay_tur_t;

typedef struct {
    up_olay_tur_t tur;
    uint32_t      tus;
    int32_t       x, y, en, boy;
} up_olay_t;

up_pencere_t *up_pencere_yarat(const char *baslik, int en, int boy);
void up_pencere_yok(up_pencere_t *p);
void up_dikdortgen(up_pencere_t *p, int x, int y, int en, int boy, uint32_t renk);
void up_yazi(up_pencere_t *p, int x, int y, const char *s, uint32_t renk);
void up_sun(up_pencere_t *p);
int  up_olay_al(up_pencere_t *p, up_olay_t *o);
int  up_metin_en(const char *s);

typedef struct {
    int32_t  pid;
    char     ad[UMAY_AD_MAX];
    uint64_t bellek_kb;
    uint32_t durum;
} up_surec_t;
int up_surec_listele(up_surec_t *dizi, int en_cok);

/* ---- gerçek çıkarım motoru buradan takılır ----
 * Bağlanmamışsa 0 döner ve US katmanı stub motoruna düşer.
 * Gerçek model (GGUF vb.) geldiğinde YALNIZ bu işlev doldurulur. */
struct umay_us;
struct umay_us *up_us_motoru(void);

typedef enum {
    YET_DOSYA_OKU = 1u<<0, YET_DOSYA_YAZ = 1u<<1, YET_AG = 1u<<2,
    YET_SUREC_GOR = 1u<<3, YET_SUREC_DUR = 1u<<4, YET_SES = 1u<<5
} up_yetki_t;
int up_yetkim_var_mi(up_yetki_t y);


/* --- Ses ---
 * up_ozan_soyle metni heceleyip kopuz düzeninde ezgiye çevirir.
 * KONUŞMA DEĞİLDİR: PC hoparlörü tek kanal kare dalga üretir. */
void     up_ozan_soyle(const char *metin, int hiz);
void     up_ses_nota(uint32_t hz, uint32_t ms);
/* Dede Korkut'un GERÇEK sesi: Türkçe formant sentezi. */
int      up_dede_konus(const char *metin);

/* Yalniz ana makine portu (umay_port_host.c) ve sinamalar: yetki maskesini ayarlar. */
void host_yetki_ayarla(uint32_t y);

#endif
