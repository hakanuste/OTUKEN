/* 1.4.9 — TULPAR'ın HTTP/1.1 istemcisi (Ring3, malloc kullanmaz).
 * Akış katmanı düz TCP ya da TLS (guvenli_* işlevleri) üzerinden çalışır. */
#ifndef OTUKEN_HTTP_H
#define OTUKEN_HTTP_H
#include "uyg.h"

typedef struct {
    int  guvenli;             /* https */
    char host[256];
    int  port;
    char yol[2048];           /* /yol?sorgu (# parçası atılır) */
} url_t;

int url_coz(const char *s, url_t *u);                     /* 0 / -1 */
void url_yaz(const url_t *u, char *cikti, int boy);
/* taban sayfaya göre göreli bağlantıyı mutlak URL'ye çevirir. 0 / -1 */
int url_birlestir(const url_t *taban, const char *bag, char *cikti, int boy);

typedef struct akis {
    int   t;                  /* ağ tutamağı */
    void *tls;                /* 0: düz TCP */
    uint8_u tam[4096];
    int   bas, son;
    int   bitti;
    int   tls_kesik;          /* TLS: close_notify gelmeden TCP kapandı */
} akis_t;

/* TLS katmanı (tls.c ya da tls_yok.c) */
int  guvenli_ac(akis_t *a, const char *host, char *hata, int hboy);
int  guvenli_yaz(akis_t *a, const void *v, int n);        /* 0 / -1 */
int  guvenli_oku(akis_t *a, void *v, int n);              /* n / 0 yok / AG_SON / -1 */
void guvenli_kapat(akis_t *a);

typedef struct {
    int      durum;           /* HTTP durum kodu (200...) ; 0 = istek başarısız */
    char     tur[96];         /* Content-Type */
    char     konum[2048];     /* Location */
    char     son_url[2400];   /* yönlendirmeler sonrası gerçek adres */
    uint8_u *govde;
    uint32_u govde_boy;
    int      kesik;           /* gövde tampona sığmadı */
    char     hata[160];       /* insan için açıklama (Türkçe) */
    uint32_u sure_tik;
} http_yanit_t;

/* Yerel ad eşlemesi (hosts dosyası gibi): ad bu IP'ye çözülür, DNS'e
 * sorulmaz. Sınama (AGSINA) kullanır; en çok 8 kayıt. */
int http_ad_esle(const char *ad, const uint8_u ip[4]);

/* url'yi indirir (en çok 5 yönlendirme). govde: çağıranın tamponu.
 * ilerleme: her okunan parçada (alınan bayt) — 0 dönerse iptal. */
int http_al(const char *url, http_yanit_t *y, uint8_u *govde, uint32_u azami,
            int (*ilerleme)(uint32_u alinan, void *v), void *v);
#endif
