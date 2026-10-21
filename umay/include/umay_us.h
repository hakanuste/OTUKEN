/* =====================================================================
 * US — çıkarım katmanı.
 *
 * İKİ AYRI ŞEY VARDIR:
 *
 *  1) MOTOR SÖZLEŞMESİ (umay_us_t) — değişmedi. Bugün us_stub_motor();
 *     yarın gerçek model PORT üzerinden up_us_motoru() ile takılır.
 *
 *  2) ÇIKARIM KATMANI (us_cevap_uret / us_ozet_*) — motorun üstünde durur.
 *     Görevi: dayanağı, kaynağını, uzmanları, uyarıları ve güven durumunu
 *     kaybetmeden tek bir çıktı sözleşmesine (us_cikti_t) dönüştürmek.
 *     Kurallar:
 *       - Dayanak yoksa motor hiç çağrılmaz; "yeterli dayanak yok" üretilir.
 *       - Çatışma varsa kaynaklar birleştirilmez; ikisi de adıyla anılır.
 *       - BENGİ'den yalnız doğrulanmış KULLANICI/PROJE/KARAR bağlamı gelir;
 *         SONUC kayıtları buraya hiç girmez (bkz. bengi_baglam()).
 *       - Motor boş ya da yanlış dönerse stub'a düşülür ve bu söylenir.
 * ===================================================================== */
#ifndef UMAY_US_H
#define UMAY_US_H
#include "umay.h"
#include "umay_bilgi.h"

#define US_DAYANAK_MAX 4
#define US_UZMAN_MAX   4
#define US_UYARI_MAX   4
#define US_METIN_MAX   768

/* ---------------- motor sözleşmesi (değişmedi) ---------------- */
typedef struct {
    const char *kisilik;
    const char *baglam;
    const char *soru;
    niyet_t     niyet;
    uint32_t    en_cok_im;
} us_istem_t;

typedef struct umay_us {
    const char *ad;
    uint32_t    yetenek;
    int  (*hazirla)(struct umay_us *u, const char *model_yolu);
    int  (*uret)(struct umay_us *u, const us_istem_t *i, char *cikti, uint32_t n);
    void (*kapat)(struct umay_us *u);
    void *ic;
} umay_us_t;

umay_us_t *us_stub_motor(void);      /* us/us_stub.c — güvenli yedek */
/* us/umay_us_ipc.c — ÖTÜKEN IPC üzerinden gerçek motor arka ucu.
 * Servis yoksa ya da yanıt bozuksa BAŞARISIZ döner; sahte başarı yok. */
umay_us_t *us_ipc_motor(void);

/* us/umay_us_ollama.c — ana bilgisayarda yerel Ollama uzerinden
 * cikarim. Ollama kapali, model kurulu degil ya da yanit bozuksa
 * BASARISIZ doner; sahte basari yok. ÖTÜKEN yapiminda yoktur. */
umay_us_t *us_ollama_motor(void);
/* us/umay_us_yerel.c — diskteki US1 modelini tamsayı motoruyla koşturur.
 * Model yoksa ya da bozuksa BAŞARISIZ döner; sahte başarı yok. */
umay_us_t *us_yerel_motor(void);

/* ---------------- çıktı sözleşmesi ---------------- */
typedef enum {
    GUVEN_YOK = 0,     /* dayanak yok            */
    GUVEN_ZAYIF,       /* çatışma ya da alan dışı */
    GUVEN_ORTA,        /* tek dayanak            */
    GUVEN_SAGLAM       /* birden çok uyumlu dayanak */
} us_guven_t;

typedef enum {
    US_KENDI_ALANI = 0, US_ORTAK_HAVUZ = 1, US_GENEL_DEPO = 2
} us_nereden_t;

typedef struct {
    char kategori[32];
    char baslik[96];
    char kaynak[96];
    kaynak_tur_t kaynak_tur;
    int  skor;
} us_dayanak_t;

typedef struct {
    char         basyazi[128];                  /* "TULGA (…) söyler:"        */
    char         metin[US_METIN_MAX];
    us_guven_t   guven;
    us_nereden_t nereden;
    int          dayanaksiz;
    int          catisma;
    char         catisma_notu[192];
    us_dayanak_t dayanak[US_DAYANAK_MAX];
    int          dayanak_sayi;
    char         uzman[US_UZMAN_MAX][UMAY_AD_MAX];
    int          uzman_sayi;
    char         uyari[US_UYARI_MAX][160];
    int          uyari_sayi;
    const char  *motor;                         /* çıktıyı üreten motorun adı */
    int          yedege_dustu;
    int          oncul_kullanildi;              /* önceki görev çıktısı işe girdi */
} us_cikti_t;

/* Çıkarım katmanına verilen tam istem */
typedef struct {
    const char  *kisilik;
    const char  *soru;
    niyet_t      niyet;
    int          tur;                           /* gorev_tur_t */
    const char  *bengi_baglam;                  /* yalnız doğrulanmış kayıtlar */
    /* Önceki görevlerin ÇIKTISI. Doğrulanmış hafıza değildir:
     * dayanak sayılmaz, güveni yükseltmez, ayrıca işaretlenir. */
    const char  *oncul_baglam;
    const baglam_t *baglam;                     /* BİLGİ'den, provenance ile   */
    int          baglam_sayi;
    us_nereden_t nereden;
    const catisma_t *catisma;
    const char  *uzman_ad;
    const char  *uzman_is;
    const char  *uyari;                         /* uzmana özel uyarı, olabilir */
    uint32_t     en_cok_im;
} us_derle_t;

/* motor yönetimi */
int         us_kur(const char *model_yolu);     /* PORT'tan motor ister, yoksa stub */
int         us_motor_tak(umay_us_t *m);         /* elle takma (sınama/ileride)      */
umay_us_t  *umay_us_al(void);
const char *us_motor_adi(void);

/* çıkarım */
int  us_cevap_uret(const us_derle_t *istem, us_cikti_t *cikti);

/* tek uzman ve meclis için ORTAK çıktı sözleşmesi */
void     us_ozet_baslat(us_cikti_t *o);
void     us_ozet_kat(us_cikti_t *o, const us_cikti_t *parca);
uint32_t us_cikti_yaz(const us_cikti_t *o, char *c, uint32_t n);
uint32_t us_ozet_kuyruk(const us_cikti_t *o, char *c, uint32_t n);
const char *us_guven_adi(us_guven_t g);
#endif
