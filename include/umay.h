#ifndef OTUKENOS_UMAY_H
#define OTUKENOS_UMAY_H

#include <stdint.h>

/* ============ Asama 39-40: UMAY Calisma Zamani ============
 *
 * KAPSAM DURUSU (dürüst olmak gerekirse):
 * UMAY bir dil modelidir. Bu cekirdekte GERCEK bir model calistirmak
 * su an mumkun degildir - kayan nokta birimi (SSE/FPU) bilincli olarak
 * kapali derlenmistir, bellek 128MB'dir ve tensor hesabi icin altyapi
 * yoktur. Burada yapilan sey MODELIN KENDISI DEGIL, modelin cekirdekle
 * konusacagi ARABIRIM ve BARINDIRMA katmanidir:
 *
 *   - Kaynak sozlesmesi (bellek/CPU siniri, yetki profili)
 *   - Cekirdek yetenek arayuzu (bellek, dosya, ag, surec, IPC)
 *   - Yasam dongusu (yukle -> baslat -> istek isle -> durdur)
 *   - Yalitim: UMAY kendi surec baglaminda ve TORE yetkileriyle calisir
 *
 * Boylece gercek model hazir oldugunda TEK yapilmasi gereken
 * umay_model_isle() fonksiyonunun icini doldurmaktir; cevresindeki
 * tum altyapi hazir ve sinanmis olur. */

#define UMAY_MAX_ISTEK   64
#define UMAY_MAX_METIN  192

typedef enum {
    UMAY_DURDURULDU = 0,
    UMAY_YUKLENIYOR = 1,
    UMAY_HAZIR      = 2,
    UMAY_MESGUL     = 3,
    UMAY_HATA       = 4
} umay_durum_t;

/* UMAY'a verilen cekirdek yetenekleri - her biri TORE tarafindan
 * denetlenir. Model, kendisine verilmeyen bir yetenegi kullanamaz. */
typedef struct {
    uint64_t azami_bellek;      /* bayt */
    uint32_t azami_cerceve;     /* PMM cerceve siniri */
    int      dosya_okuma;
    int      dosya_yazma;
    int      ag_erisimi;
    int      surec_olusturma;
    int      ipc_erisimi;
} umay_sozlesme_t;

typedef struct {
    umay_durum_t durum;
    umay_sozlesme_t sozlesme;
    uint32_t pid;               /* TORE/KUT icin surec kimligi */
    int      ipc_port;
    uint64_t kullanilan_bellek;
    uint32_t islenen_istek;
    uint32_t reddedilen_istek;
    char     model_adi[32];
} umay_runtime_t;

void umay_init(void);

/* Modeli belirtilen sozlesmeyle yukler. Basari:0 */
int  umay_yukle(const char *model_adi, const umay_sozlesme_t *s);

/* Bir istegi isler; yaniti out'a yazar. Donus: yanit uzunlugu, hata:-1 */
int  umay_istek(const char *metin, char *out, int out_max);

void umay_durdur(void);
const umay_runtime_t *umay_durum_al(void);
const char *umay_durum_adi(umay_durum_t d);

/* --- Cekirdek yetenek arabirimi (Asama 40) ---
 * UMAY bu fonksiyonlar disinda cekirdege ERISEMEZ. Her biri once
 * sozlesmeyi, sonra TORE yetkisini denetler. */
int      umay_cek_dosya_oku(const char *yol, uint8_t *tampon, uint32_t azami);
int      umay_cek_dosya_yaz(const char *ad, const uint8_t *veri, uint32_t boyut);
void    *umay_cek_bellek_al(uint64_t boyut);
void     umay_cek_bellek_birak(void *p);
int      umay_cek_ag_gonder(const uint8_t ip[4], uint16_t port,
                             const uint8_t *veri, uint16_t uzunluk);
int      umay_cek_ipc_gonder(const char *servis, uint32_t opkod,
                              const void *veri, uint32_t uzunluk);
uint64_t umay_cek_zaman(void);

#endif
