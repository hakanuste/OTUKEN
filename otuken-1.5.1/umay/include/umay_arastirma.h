/* =====================================================================
 * ARAŞTIRMA — ajanların araştırma yordamı.
 *
 * Bir ARAŞTIR görevi eskiden sıradan bir SORU gibi işleniyordu: tek
 * kütüphane araması + tek yanıt. Araştırma artık AJANIN KENDİSİ
 * tarafından, OTAĞ turlarına yayılmış adımlarla yapılır:
 *
 *   1) PLAN     soru terimlere ayrılır (durak ve buyruk sözcükleri atılır)
 *   2) TARAMA   her adımda TEK sorgu: önce bütün soru, sonra her terim.
 *               Kademe: ajanın kendi kategorileri -> ortak kategorileri
 *               -> genel depo. Tam sözcük tutmazsa KÖKLE (önek) yeniden.
 *   3) DERLEME  bulgular tekilleştirilir (aynı kayıt iki kez sayılmaz),
 *               skorla sıralanır; her bulgu kaynağıyla birlikte tutulur.
 *   4) RAPOR    bulgu cümleleri + kaynakları, hangi kademede bulunduğu,
 *               BULUNAMAYAN terimler, çatışan kaynaklar. Uydurma yok:
 *               raporda kaynağı olmayan tek cümle yoktur.
 *
 * SONUÇ YAPISI (ars_sonuc_t) — rapor metni bundan YAZILIR; ayrı tutulur:
 *   kaynak[]   : bulgunun nereden geldiği (kategori, başlık, dosya, tür, kademe)
 *   kanit[]    : kaynaktaki cümle(ler), AYNEN (soru terimlerini en ağır taşıyan)
 *   bulunan / kokle / eksik : terimlerin durumu
 *   catisma    : çelişen (ayrı kaynaklı, aynı kategorili) kayıtlar
 *   guven      : kanıt cümlelerinin soru terimlerini ağırlıklı kapsaması
 *                (umay_kanit.h) ve yeterli mi
 *
 * Özet (US motoru) raporun ALTINA eklenir; bulguların yerine geçmez. US'e
 * HAM araştırma metni (alıntı pencereleri) VERİLMEZ: ars_dayanak yalnız
 * kanıt cümlelerini dayanak olarak geçirir.
 * Ağ araştırması YOKTUR: kaynak yalnız yerel kütüphanedir.
 * ===================================================================== */
#ifndef UMAY_ARASTIRMA_H
#define UMAY_ARASTIRMA_H
#include "umay_bilgi.h"

#define ARS_TERIM_MAX   6
#define ARS_HAVUZ_MAX   6
#define ARS_BULGU_BOY   220

typedef enum { ARS_KENDI = 0, ARS_ORTAK = 1, ARS_GENEL = 2 } ars_kademe_t;

typedef struct {
    char        soru[256];
    const char *kategori[4];         /* ajanın kendi + ortak kategorileri */
    int         kendi_sayi;          /* ilk kaç kategori KENDİ alanı      */
    int         kategori_sayi;

    char        terim[ARS_TERIM_MAX][24];
    int         terim_sayi;
    int         terim_bulgu[ARS_TERIM_MAX];   /* terim başına bulunan kayıt */
    int         terim_kokle[ARS_TERIM_MAX];   /* 1: kökle (önek) bulundu    */

    int         sorgu_no;            /* sıradaki sorgu (0 = bütün soru)    */
    int         sorgu_sayisi;        /* yapılan sorgu sayısı               */
    int         bitti;

    baglam_t    havuz[ARS_HAVUZ_MAX];
    ars_kademe_t havuz_kademe[ARS_HAVUZ_MAX];
    int         havuz_sayi;
    int         ham_bulgu;           /* tekilleştirmeden önceki toplam     */
    catisma_t   catisma;
} arastirma_t;

/* kategoriler: ajanın kategorileri (NULL = yalnız genel depo);
 * kendi_sayi: bunlardan ilk kaçı ajanın KENDİ alanı. */
void ars_basla(arastirma_t *a, const char *soru,
               const char *const *kategoriler, int kategori_sayi, int kendi_sayi);

/* Bir tarama adımı (tek sorgu). 1: sürüyor, 0: tarama bitti. */
int  ars_adim(arastirma_t *a);

/* 0..100 */
int  ars_ilerleme(const arastirma_t *a);

#define ARS_KANIT_BOY   (2 * ARS_BULGU_BOY + 8)

typedef struct {
    char         kategori[32];
    char         baslik[96];
    char         kaynak[96];
    kaynak_tur_t tur;
    ars_kademe_t kademe;
    int          skor;
} ars_kaynak_t;

typedef struct {
    char         soru[256];
    ars_kaynak_t kaynak[ARS_HAVUZ_MAX];
    char         kanit[ARS_HAVUZ_MAX][ARS_KANIT_BOY];
    int          kaynak_sayi;                 /* bulgu (kayıt) sayısı          */
    int          ayri_kaynak;                 /* ayrı kaynak (dosya) sayısı     */
    char         bulunan[ARS_TERIM_MAX][24];  int bulunan_sayi;
    char         kokle[ARS_TERIM_MAX][24];    int kokle_sayi;
    char         eksik[ARS_TERIM_MAX][24];    int eksik_sayi;
    catisma_t    catisma;
    int          guven;                       /* 0..100 */
    int          yeterli;
    int          sorgu_sayisi, terim_sayi;
} ars_sonuc_t;

/* Taramadan yapılandırılmış sonucu çıkarır (tarama bitmiş olmalı). */
void     ars_sonuc_cikar(const arastirma_t *a, ars_sonuc_t *s);
/* Sonuçtan rapor metni (bulgular, kaynaklar, eksikler, çatışma, kanıt gücü). */
uint32_t ars_sonuc_yaz(const ars_sonuc_t *s, const char *ajan_ad, char *c, uint32_t n);
/* ars_sonuc_cikar + ars_sonuc_yaz */
uint32_t ars_rapor(const arastirma_t *a, const char *ajan_ad, char *c, uint32_t n);

/* Özet için en iyi en_cok bulgu US dayanağı olur: kaynak bilgisi korunur,
 * alıntı yerine YALNIZ kanıt cümleleri geçer (ham metin geçmez). */
int  ars_dayanak(const arastirma_t *a, baglam_t *cikan, int en_cok);
#endif
