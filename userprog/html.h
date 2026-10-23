/* =====================================================================
 * TULPAR — HTML'den okunur metne (1.4.9)
 *
 * Tarayıcı motoru DEĞİL: JavaScript, CSS yerleşimi ve resim yok. HTML
 * etiketlerinin anlamına göre (başlık, paragraf, liste, tablo, bağlantı,
 * form) sabit genişlikli metin satırları üretir. Ring3'te malloc'suz
 * çalışır: bütün tablolar çağıranın verdiği sayfa_t içindedir.
 * Aynı kod ana makinede de derlenir (tests/tulpar) — sınama oradadır.
 * ===================================================================== */
#ifndef OTUKEN_HTML_H
#define OTUKEN_HTML_H

#ifdef HTML_ANA_MAKINE
#include <stdint.h>
typedef uint8_t  uint8_u;
typedef uint16_t uint16_u;
typedef uint32_t uint32_u;
#else
#include "uyg.h"
typedef unsigned short uint16_u;
#endif

#define H_MAX_KAR    400000       /* hücre (kod noktası) */
#define H_MAX_SATIR  16000
#define H_MAX_OGE    3000         /* bağlantı + form alanı */
#define H_MAX_METIN  (160 * 1024) /* href ve form değerleri */
#define H_MAX_FORM   24

/* hücre = kod noktası (alt 21 bit) | biçem << 24 */
#define H_STIL(h)  (((h) >> 24) & 15)
#define H_KOD(h)   ((h) & 0x1FFFFF)
enum {
    ST_NORMAL = 0, ST_BASLIK, ST_BAG, ST_ONBICIM, ST_SONUK, ST_KALIN, ST_ALAN, ST_RESIM
};

/* öğe türleri (bag[] 1'den başlar: 0 = öğe yok) */
enum { OGE_BAG = 0, OGE_METIN_ALANI, OGE_GONDER, OGE_ISARET, OGE_SECENEK /* radio: ayni ad+formda tek secili */ };

typedef struct {
    uint8_u  tur;
    uint8_u  form;            /* form öğeleri için */
    uint8_u  secili;          /* işaret kutusu */
    uint32_u ad;              /* metin[] içinde: href ya da alan adı */
    uint32_u deger;           /* metin[] içinde: alan değeri (METIN_ALANI) */
} h_oge_t;

typedef struct {
    uint32_u eylem;           /* action (metin[]) */
    uint8_u  post;
} h_form_t;

typedef struct {
    int       genislik;                   /* sütun */
    uint32_u  kar[H_MAX_KAR];
    uint16_u  oge[H_MAX_KAR];             /* 0 ya da öğe no + 1 */
    uint32_u  kar_adet;
    uint32_u  satir[H_MAX_SATIR + 2];     /* satır başları; satir[satir_adet] = kar_adet */
    int       satir_adet;
    h_oge_t   ogeler[H_MAX_OGE];
    int       oge_adet;
    h_form_t  formlar[H_MAX_FORM];
    int       form_adet;
    /* gizli alanlar (hidden): form, ad, değer — metin[]'de */
    uint32_u  gizli[512][3];
    int       gizli_adet;
    char      metin[H_MAX_METIN];
    uint32_u  metin_boy;
    char      baslik[200];
    char      yenile[1024];               /* <meta http-equiv=refresh> hedefi */
    char      taban[1024];                /* <base href> */
    int       kesik;                      /* sınırlar aşıldı: sayfa kısaltıldı */
} sayfa_t;

/* Kod çözümü: 0 UTF-8, 1 ISO-8859-9 / windows-1254, 2 ISO-8859-1 / windows-1252 */
enum { KK_UTF8 = 0, KK_LATIN5, KK_LATIN1 };
/* "text/html; charset=..." ya da <meta charset> için */
int html_kodlama_ad(const char *ad);
/* belgenin ilk 4 KB'ında <meta charset> ara; bulunamazsa -1 */
int html_kodlama_bul(const uint8_u *g, uint32_u n);

/* girdi -> satırlar. duz_metin: text/plain (önbiçimli). 0 / -1 */
int html_isle(const uint8_u *g, uint32_u n, int kodlama, int duz_metin, int genislik, sayfa_t *s);

/* s->metin içindeki sıfır sonlu dize */
static inline const char *h_metin(const sayfa_t *s, uint32_u i) { return s->metin + i; }

/* kod noktasını UTF-8'e yazar; bayt sayısı */
int utf8_yaz(uint32_u k, char *o);
#endif
