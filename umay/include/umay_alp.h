/* =====================================================================
 * GÖREV MOTORU + OTAĞ (ajan meclisi)
 *
 * Omurga:  Kullanıcı → OZAN → OTAĞ → uzman ajanlar → BİLGİ/BENGİ → US
 *          → sonuç → BENGİ
 *
 * Görevler durağan bir havuzda (gorev_t havuz[]) yaşar; kuyruk yalnız
 * havuz indeksi taşır. Böylece alt görev, bağımlılık, ilerleme ve durum
 * güncellemesi görev yer değiştirmeden yapılabilir.
 * Ajanlar işbirlikçi adımlarla koşar; iş parçacığı yoktur.
 * ===================================================================== */
#ifndef UMAY_ALP_H
#define UMAY_ALP_H
#include "umay.h"
#include "umay_us.h"

/* ---------------- görev türleri ---------------- */
typedef enum {
    GT_SORU = 0,    /* bilgi isteği                        */
    GT_ARASTIR,     /* kaynak tarama, derleme              */
    GT_ANALIZ,      /* karşılaştırma, değerlendirme, öğüt  */
    GT_TASARLA,     /* kurgu, plan, ölçülendirme           */
    GT_KONTROL,     /* yoklama, denetim                    */
    GT_ONAR,        /* aksaklık giderme                    */
    GT_IZLE,        /* sürekli gözetim                     */
    GT_UYGULA,      /* yıkıcı olabilen iş — onay ister     */
    GT_SAYI
} gorev_tur_t;

#define GT_BIT(t) (1u << (t))
#define GT_HEPSI  0xFFu

typedef enum {
    GD_YENI = 0,
    GD_BEKLIYOR,       /* bağımlılığı bitmedi        */
    GD_HAZIR,          /* kuyrukta, ajan bekliyor    */
    GD_KOSUYOR,
    GD_ONAY_BEKLER,
    GD_BITTI,
    GD_YANLIS,
    GD_IPTAL
} gorev_durum_t;

typedef enum {
    ONC_DUSUK = 0, ONC_ORTA = 1, ONC_YUKSEK = 2, ONC_ACIL = 3
} oncelik_t;

typedef enum {
    ALP_BOS = 0, ALP_KOSUYOR, ALP_BEKLIYOR, ALP_BITTI, ALP_YANLIS
} alp_durum_t;

/* ---------------- görev kaydı ---------------- */
typedef struct {
    uint32_t      kimlik;
    gorev_tur_t   tur;
    gorev_durum_t durum;
    oncelik_t     oncelik;
    alan_t        alan;
    niyet_t       niyet;

    char     hedef_ajan[UMAY_AD_MAX];      /* boşsa OTAĞ seçer            */
    char     istem[GOREV_ISTEM_MAX];       /* kullanıcının sözü / iş metni */
    char     baglam[GOREV_BAGLAM_MAX];     /* BİLGİ'den gelen dayanak      */
    char     sonuc[GOREV_SONUC_MAX];       /* ajanın çıktısı               */
    char     konu[UMAY_AD_MAX];

    int      ilerleme;                     /* 0..100                       */
    uint32_t ebeveyn;                      /* 0 = kök görev                */
    uint32_t bagimlilik[GOREV_BAGIM_MAX];  /* bitmesi beklenen görevler    */
    int      bagimlilik_sayi;
    int      alt_sayi;                     /* açılmış alt görev sayısı     */

    int      onay_gerekli;
    char     onay_kimlik[16];

    uint32_t kok_kimlik;                   /* meclis kökü (0 = tekil)      */
    kanal_tur_t kanal;
    uint32_t kaynak;
    int      bengiye_yaz;                  /* sonuç kalıcı belleğe geçsin  */
    int      catisma;                      /* ayrı kaynaklar aynı konuda    */
    char     catisma_notu[192];
    char     oncul_baglam[GOREV_BAGLAM_MAX]; /* biten öncül görevlerin ÇIKTISI —
                                              * doğrulanmış hafıza DEĞİLDİR    */
    int      denendi;                      /* bu turda dağıtım denendi        */

    uint64_t olustu, basladi, bitti;
    int      dolu;
} gorev_t;

typedef struct {
    uint32_t kimlik;
    int      basarili;
    char     ad[UMAY_AD_MAX];
    alan_t   alan;
    gorev_tur_t tur;
    char     oz[GOREV_SONUC_MAX];
} sonuc_t;

/* MECLİS — çok alanlı görevde en çok üç ajanın sözünü tek sonuçta toplar. */
typedef struct {
    uint32_t   kok_kimlik;
    int        beklenen, gelen;
    char       soru[UMAY_AD_MAX * 4];
    char       birlesik[GOREV_SONUC_MAX];
    us_cikti_t ozet;              /* uzmanların ortak güven/dayanak özeti */
    int        yapili;            /* en az bir ajan yapılandırılmış çıktı verdi */
    int        dolu;
} meclis_t;

/* ---------------- ajan ---------------- */
typedef struct alp {
    char     ad[UMAY_AD_MAX];
    char     is[64];
    /* --- yetenek bildirimi --- */
    alan_t   alan;                         /* ALAN_YOK = genel görevli   */
    uint32_t tur_maskesi;                  /* GT_BIT(...) birleşimi      */
    const char *kategori[4];               /* baktığı BİLGİ kategorileri */
    uint32_t yetki_gerek;                  /* up_yetki_t maskesi         */
    /* --- davranış --- */
    int      (*uygun_mu)(const gorev_t *g);
    int      (*basla)(struct alp *a, const gorev_t *g);
    alp_durum_t (*adim)(struct alp *a);
    void     (*topla)(struct alp *a, sonuc_t *s);
    /* US çıktı sözleşmesini veren ajanlar bunu doldurur; meclis
     * birleştirmesi bu yolla yapılandırılmış çalışır. 0 ise düz metin. */
    const us_cikti_t *(*cikti_al)(struct alp *a);
    /* --- çalışma durumu --- */
    alp_durum_t durum;
    uint32_t gorev_kimlik;
    uint32_t kok_kimlik;
    gorev_t *gorev;                        /* havuzdaki canlı görev      */
    uint64_t basla_ms;
    void    *ic;
} alp_t;

typedef struct {
    alp_t   *alplar[UMAY_ALP_MAX];
    int      alp_sayisi;
    gorev_t  havuz[UMAY_GOREV_MAX];
    uint32_t kuyruk[UMAY_GOREV_MAX];       /* havuz indeksleri           */
    int      kuyruk_sayi;
    uint32_t sonraki_kimlik;
    uint32_t bitmis;
    meclis_t meclis[UMAY_MECLIS_MAX];
} otag_t;

void otag_kur(otag_t *o);
int  otag_alp_kat(otag_t *o, alp_t *a);
int  otag_alan_var_mi(const otag_t *o, alan_t alan);
void otag_yetenek_dok(const otag_t *o, char *c, uint32_t n);

/* --- görev motoru --- */
void     gorev_hazirla(gorev_t *g, gorev_tur_t tur, const char *istem);
int      otag_gorev_ver(otag_t *o, const gorev_t *tanim);   /* kimlik döner */
int      otag_alt_gorev(otag_t *o, uint32_t ebeveyn, const gorev_t *tanim);
int      otag_bagimlilik_ekle(otag_t *o, uint32_t gorev, uint32_t onceki);
gorev_t *otag_gorev_bul(otag_t *o, uint32_t kimlik);
int      otag_ilerleme(otag_t *o, uint32_t kimlik, int yuzde);
int      otag_meclis_kur(otag_t *o, const gorev_t *tanim, const alan_t *alanlar, int sayi);
int      otag_dondur(otag_t *o, sonuc_t *cikan, int en_cok);
int      otag_mesgul(const otag_t *o);
int      otag_bekleyen_sayi(const otag_t *o);
int      otag_catisma_bildir(otag_t *o, uint32_t gorev, const char *not_);
int      otag_catisma_sayi(const otag_t *o);
const char *gorev_tur_adi(gorev_tur_t t);
/* Görev türünün gerektirdiği TÖRE yeteneği (up_yetki_t maskesi, 0 = gerekmez).
 * Yıkıcı işler ajana verilmeden ÖNCE OTAĞ burada denetler. */
uint32_t    gorev_yetki_gerek(gorev_tur_t t);
const char *gorev_durum_adi(gorev_durum_t d);

/* --- çekirdek görevliler --- */
alp_t *alp_gozcu(void);
alp_t *alp_onarici(void);
alp_t *alp_bilge(void);
alp_t *alp_yazman(void);

/* --- uzman kadrosu (12) --- */
alp_t      *alp_uzman(alan_t alan);
alp_t      *alp_uzman_sirali(int i);
int         alp_uzman_sayisi(void);
const char *alan_adi(alan_t a);
const char *alan_isi(alan_t a);
#endif
