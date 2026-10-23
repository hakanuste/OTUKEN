/* =====================================================================
 * UMAY — ÖTÜKEN userland yapay us uygulaması
 * Ana başlık: ortak türler, sürüm, sınırlar
 * Ring3 process. Çekirdek belleğine dokunmaz. Yalnız syscall/IPC/VFS.
 * ===================================================================== */
#ifndef UMAY_H
#define UMAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UMAY_AD        "UMAY"
#define UMAY_SURUM     "0.1.0"
#define UMAY_OZAN_ADI  "Dede Korkut"

#define UMAY_ILETI_MAX     2048
#define UMAY_KUYRUK_MAX    64
#define UMAY_ALP_MAX       24
#define UMAY_MECLIS_MAX    4      /* koşut çalışan meclis (çok uzmanlı görev) */
#define UMAY_MECLIS_UYE    3      /* bir meclise en çok kaç uzman katılır   */
#define UMAY_GOREV_MAX     24     /* görev havuzu boyu */
#define GOREV_ISTEM_MAX    512
#define GOREV_BAGLAM_MAX   768
#define GOREV_SONUC_MAX    2048   /* meclis çıktısı üç uzmanı taşıyabilmeli */
#define GOREV_BAGIM_MAX    4
#define UMAY_BENGI_MAX     128
#define UMAY_AD_MAX        32
#define UMAY_YOL_MAX       128

typedef enum {
    UM_OLDU        =  0,
    UM_YANLIS      = -1,
    UM_YOK         = -2,
    UM_DOLU        = -3,
    UM_YETKISIZ    = -4,
    UM_ZAMANASIMI  = -5,
    UM_BOZUK       = -6,
    UM_DESTEKSIZ   = -7
} um_sonuc_t;

typedef enum {
    KANAL_YOK     = 0,
    KANAL_KLAVYE  = 1,
    KANAL_ULGEN   = 2,
    KANAL_IPC     = 3,
    KANAL_SES     = 4,
    KANAL_IC      = 5
} kanal_tur_t;

typedef struct {
    kanal_tur_t tur;
    uint64_t    damga;
    uint32_t    kaynak;
    uint32_t    uzunluk;
    char        govde[UMAY_ILETI_MAX];
} umay_ileti_t;

typedef enum {
    NIYET_BILINMEZ = 0,
    NIYET_SELAM,
    NIYET_SORU,
    NIYET_BUYRUK,
    NIYET_DENETIM,
    NIYET_ONARIM,
    NIYET_DURUM,
    NIYET_OGUT,
    NIYET_VEDA
} niyet_t;

/* Uzmanlık alanları — OZAN niyeti belirler, alanı da belirler;
 * OTAĞ bu alana göre uzman ajanı seçer. */
typedef enum {
    ALAN_YOK = 0,
    ALAN_AKCA,      /* finans, borsa, kripto, makroekonomi   */
    ALAN_YERSU,     /* permakültür, tarım, toprak, ekosistem */
    ALAN_KUN,       /* güneş enerjisi, solar dizgeler        */
    ALAN_DEMIR,     /* elektronik, PCB, donanım              */
    ALAN_TULGA,     /* otomotiv ve araç mühendisliği         */
    ALAN_OTACI,     /* sağlık ve tıp danışmanlığı            */
    ALAN_YARGU,     /* hukuk ve mevzuat                      */
    ALAN_BALBAL,    /* inşaat, taş ev, yapı, mimari          */
    ALAN_ALPAGU,    /* savunma, güvenlik, hayatta kalma      */
    ALAN_KOPUZ,     /* müzik ve beste                        */
    ALAN_OYUN,      /* dans ve koreografi                    */
    ALAN_YAZIT,     /* bilgi arşivi ve belge yönetimi        */
    ALAN_SAYI
} alan_t;

typedef struct {
    niyet_t niyet;
    int     tur;                       /* gorev_tur_t — umay_alp.h'de tanımlı */
    char    konu[UMAY_AD_MAX];
    int     guven;
    alan_t  alan;                      /* baş uzmanlık alanı            */
    alan_t  yardimci[UMAY_MECLIS_UYE]; /* çoklu uzmanlık gerekiyorsa    */
    int     yardimci_sayi;
} coz_t;

#endif
