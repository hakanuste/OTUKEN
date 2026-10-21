/* =====================================================================
 * DEDE — Dede Korkut'un sesi: Türkçe formant sentezleyici.
 *
 * NE YAPAR: metni Türkçe seslerine ayırır ve KAYNAK-SÜZGEÇ modeliyle
 * PCM üretir. Çıkış 48 kHz, 16 bit, iki kanal — doğrudan AC'97
 * sürücüsüne (ses_cal) verilir.
 *
 * ---------------------------------------------------------------------
 * BU SÜRÜMDE NE DEĞİŞTİ VE NEDEN
 *
 * Önceki sürüm ÖLÇÜTLERİ tutturuyor ama KONUŞMUYORDU. Sebebi iki
 * yapısal yanlıştı; ikisi de ölçümle yakalandı:
 *
 *  1. "Gırtlak alçak geçireni" aslında 85 Hz'e kurulmuş DAR bir
 *     çınlayıcıydı. Ölçüldü: 85 Hz'i 2 kHz'in 308 KATINA çıkarıyordu.
 *     Kaynak böylece neredeyse saf bir sinüse dönüşüyor, formantları
 *     uyaracak harmonik kalmıyordu. Ses uğulduyordu.
 *  2. Üç formant PARALEL bağlıydı ve elle ağırlıklandırılmıştı
 *     (1, 1/3, 1/8). Ölçüldü: F3 dalı F1 dalının 27 dB altında
 *     kalıyordu. Oysa anlaşılırlığın çoğu 1-5 kHz'dedir.
 *
 * Sonuç ölçümü (hedef ses / eski DEDE): 1-2 kHz bandı 11 dB, 3-5 kHz
 * bandı 19 dB eksikti; F2 1853 Hz yerine 951 Hz okunuyordu. Yani
 * ünlüler birbirinden ayrılmıyordu.
 *
 * Yerine konan:
 *  - ROSENBERG gırtlak darbesi. Gerçek bir gırtlak akışı biçimi:
 *    açılışta üçüncü dereceden yükseliş, kapanışta ikinci dereceden
 *    iniş. Doğal olarak harmonik zengindir ve -12 dB/sekizli iner.
 *  - KADEMELİ (cascade) ses yolu: beş çınlayıcı ARDIŞIK bağlı.
 *    Formantlar arası göreli genlik böylece kendiliğinden doğru
 *    çıkar; elle ağırlık verilmez. F4 ve F5 sabittir, üst bandı
 *    doldurur.
 *  - Her çınlayıcı DC kazancı 1'e normalize edildi; zincir patlamıyor.
 *  - Formant GEÇİŞLERİ: ünsüzden ünlüye formantlar kayarak gider.
 *    Anlaşılırlığın büyük kısmı bu geçişlerdedir.
 *  - Tayf eğimi tek kutuplu bir süzgeçle ayrı denetleniyor; artık
 *    kaynağın perdesini bozarak eğim ayarlanmıyor.
 *
 * ---------------------------------------------------------------------
 * ÖLÇÜLMÜŞ HEDEF SES (TRT kaydı, 52 sn temiz konuşma bölümü)
 *   F0 ortanca      74.6 Hz   (%10-%90: 60 - 91)     — gerçekten davudi
 *   tayf eğimi     -13.9 dB/onkat
 *   bantlar (dB)    60-200: -1.2   200-500: -9.9   0.5-1k: -14.9
 *                   1-2k: -14.9    2-3k: -17.6     3-5k: -16.5
 *   formantlar      F1 560  F2 1853  F3 2765
 *   hece hızı       2.75 /sn  (ağır, anlatan bir söyleyiş)
 *   sessizlik       %38       (tane tane, duraklı)
 *
 * NE DEĞİLDİR: bu bir sinir ağı sentezleyicisi değildir; belirli bir
 * insanın ses KİMLİĞİNİ taşıyamaz. Ölçülebilir nitelikleri — perde,
 * formant yapısı, tayf dengesi, tempo — eşler.
 *
 * TAMSAYI: kayan nokta birimi kapalıdır. Çınlayıcılar 16.16 sabit
 * noktada iki kutuplu özyinelemeli süzgeçtir.
 * ===================================================================== */
#include "dede.h"
#include "ses.h"
#include "serial.h"

#define HZ        48000
#define KESIR     16
#define BIR       (1 << KESIR)
#define PARCA     1024          /* tek seferde üretilen çerçeve */

/* İç çözünürlük payı. Normalize çınlayıcının giriş kazancı küçüktür
 * (F1 için 734/65536); ham genlikle çarpılırsa alt basamaklar yiter.
 * Kaynak bu kadar sola kaydırılıp zincire girer, çıkışta geri alınır.
 * Ölçülen en yüksek zincir kazancı 15.4; 1<<29 sınırına yer var. */
#define IC_KAYMA  10
#define IC_SINIR  (1 << 29)


/* --- Yuvarlayarak kaydırma -------------------------------------------
 * C'de aritmetik sağ kaydırma NEGATİF sayıları AŞAĞI yuvarlar:
 * (-1) >> 16 eşittir -1, sıfır değil. Geri beslemeli bir süzgeçte bu
 * küçücük yanlılık her örnekte birikir: süzgeç bir daha sıfıra inemez,
 * bir DC basamağında KİLİTLENİR.
 *
 * ÖLÇÜLDÜ: cümleler arası duraklarda çıkış hiç susmuyordu — sabit
 * -2953'te duruyordu, yani tepe düzeyinin yalnız 3.5 dB altında. Söz,
 * hiç kesilmeyen bir basamağın üstünden geliyordu: duraklar duyulmuyor,
 * sözcükler birbirinden ayrılmıyordu. Üstelik DC kaçığı çıkış payını
 * yiyor ve her geçişte gümbürtü üretiyor.
 *
 * En yakına yuvarlamak hatayı sıfır ortalamalı yapar; kilitlenme kalkar.
 * ------------------------------------------------------------------- */
static inline int32_t kaydir(int64_t x)
{
    return (int32_t)((x + (1 << (KESIR - 1))) >> KESIR);
}

/* --- Ses birimi tanımı --- */
typedef enum {
    T_SESLI,        /* ötümlü, formantlı             */
    T_SURTUNME,     /* gürültü kaynaklı (s, ş, f, h) */
    T_PATLAMA,      /* kapanma + patlama (p, t, k)   */
    T_OTUMLU_PAT,   /* ötümlü patlama (b, d, g)      */
    T_GENIZ,        /* m, n                          */
    T_AKICI,        /* l, r, y, v, z, j              */
    T_SESSIZLIK
} ses_turu_t;

typedef struct {
    ses_turu_t tur;
    uint16_t   f1, f2, f3;   /* SES YOLU hedefi (kademeli zincir), Hz */
    uint16_t   gf1, gf2, gf3;/* GÜRÜLTÜ bandı (paralel dal), Hz       */
    uint16_t   sure_ms;
    uint16_t   genlik;       /* binde; 1000 = ölçünlü düzey */
    int16_t    perde_ek;     /* Hz; vurgulu hecede perde yükselir */
} birim_t;

/* ÖLÇÜLEN KUSUR: HECE BAŞINA İKİ DARBE  (ÇÖZÜLMEDİ)
 * Olgun bir formant sentezleyicisiyle (espeak-ng, Türkçe) aynı cümle
 * karşılaştırıldı:
 *     metinde 27 hece, 62 harf
 *     espeak : 31 enerji tepesi -> hece başına 1.15
 *     DEDE   : 56 enerji tepesi -> hece başına 2.07
 * DEDE hece başına değil, HARF başına bir darbe üretiyor. Kulak
 * konuşmayı hece yaylarından çözer; 27 hecenin yerinde 56 ayrı darbe
 * görünce sözcük oluşmaz. Bütün tayf ölçütleri geçerken anlaşılırlığın
 * neden gelmediği buradadır.
 *
 * ELENEN NEDENLER (hepsi ölçümle, varsayımla değil):
 *   1. Birim başına genlik zarfı. Yalnız gerçek sınırlarda kesen
 *      "kesiklik bayrakları" eklendi: tepe 56'dan 60'a ÇIKTI, üstelik
 *      çıkış taşmaya başladı (kaynak sözcük boyunca tam düzeyde
 *      kalıyor, çıkış kazancı ise rampalar varmış gibi ayarlı).
 *      Değişiklik tümüyle geri alındı.
 *   2. Vurgu. VURGU_GENLIK=ZAYIF_GENLIK=1000, süreler eşit, perde eki
 *      sıfır: çukurlar yerinde kaldı.
 *   3. Süzgeç durumunun sıfırlanması. birim_sesle içindeki her yerel
 *      değişken sayıldı; süzgeç durumu ses_yolu_t'de ve birimler
 *      arasında taşınıyor. Sıfırlanma yok.
 *   4. Formant kayması. GECIS_MS 40 ile 1 karşılaştırıldı: "aaa"
 *      üzerinde zarflar BİRE BİR AYNI çıktı.
 *
 * Üç özdeş ünlüde ("aaa") her birim sınırında 5-6 dB çukur duruyor.
 * Nedeni henüz bulunamadı. Sıradaki adım varsayım kurmak değil,
 * kaynak sinyali ile zincir çıkışını bir sınırın iki yanında AYRI AYRI
 * dökmek: çukuru hangi katın doğurduğunu ölçüyle görmek. */

/* NEDEN İKİ AYRI FORMANT TAKIMI
 * Önce tek takım vardı ve ünsüzlerde gürültü bandı (örneğin "s" için
 * 4000/6500/8200 Hz) oraya yazılıyordu. Ama o sayılar SES YOLU değil,
 * dişlerin arasındaki dar aralığın tınısıdır. Sonuç: ünsüzün ses yolu
 * hedefi hiç tanımlanmamış oluyordu.
 *
 * ÖLÇÜLDÜ: "b", "d" ve "g" bit bit AYNI ses çıkıyordu (tayf uzaklığı
 * 0.00 dB). p-k 1.99 dB, t-ç 1.65 dB, m-n 2.09 dB — hepsi ayırt etme
 * sınırının altında. Türkçeyi taşıyan ünsüzler; ünlüler kusursuz olsa
 * bile sözcük çözülmüyordu.
 *
 * Bir ünsüzün kimliği BOĞUMLANMA YERİNDEN okunur ve bu yer, komşu
 * ünlünün formantlarını kendine doğru BÜKER. İkinci formantın gittiği
 * yere "locus" denir:
 *     dudak    (b p m)      F2 ~  750 Hz
 *     diş      (d t n s z)  F2 ~ 1750 Hz
 *     artdamak (g k)        F2 ~ 2300 Hz (ön ünlüde) / ~1250 (arka)
 * Kulak bu bükülmeyi okur. Bükülme yoksa ünsüz yoktur. */

/* NEDEN AYRI GENLİK: tek bir "sürtünme kazancı" yetmiyor. Çıkıştaki
 * varlık yükseltmesi 4 kHz'den sonra etkili olduğu için "s" (4000/
 * 6500/8200 Hz) ile "ş" (2400/3600/5200 Hz) aynı kaynak düzeyinde
 * 10 dB farkla çıkıyordu. Doğal konuşmada aradaki fark 3-5 dB'dir.
 * Her sesin düzeyi ölçülüp buradan düzeltiliyor. */

/* Formant ölçekleri (yüzde).
 * Önceki sürümde F2 %86'ya çekilmişti; bu, 6 saniyelik kayıptan
 * ölçülmüş yanlış bir "F2 ~1100-1300 Hz" değerine dayanıyordu. Uzun
 * kayıtta F2 ortancası 1853 Hz çıktı. F2'yi sıkıştırmak ünlüleri
 * birbirine yaklaştırıyor, yani anlaşılırlığı doğrudan bozuyordu.
 * Şimdi yalnız hafif bir uzun-ses-yolu kayması var: iri gövde
 * izlenimini verir, ünlü kimliğini bozmaz. */
#define DEDE_F1_OLCEK   98
#define DEDE_F2_OLCEK   97
#define DEDE_F3_OLCEK   97

/* NEDEN SÜRE ÖLÇEĞİ
 * ÖLÇÜLDÜ (kiplenim tayfı — zarfın kendi tayfı; konuşmada hece hızı
 * 3-8 Hz'de tepe yapar):
 *       espeak-ng (Türkçe) : hece hızı 3.6 Hz, 3-8 Hz payı %20.8
 *       DEDE               : hece hızı 2.1 Hz, 3-8 Hz payı %13.2
 * Aynı metin espeak'te 8.13 sn, DEDE'de 9.51 sn sürüyor; 27 hece için
 * saniyede 2.8 hece demektir. Türkçe konuşmada bu sayı 5-6'dır. DEDE
 * iki kat yavaş konuşuyor ve hece yayları olağan aralığın ALTINDA
 * kalıyor: kulak hece sınırını bulamaz.
 *
 * Bütün birim süreleri tek bir ölçekten geçer ki hız deneyle
 * ayarlanabilsin ve ölçüme bağlanabilsin. */
#ifndef SURE_OLCEK
#define SURE_OLCEK  65          /* yüzde; 100 = eski, iki kat yavaş hal */
#endif
#ifndef DURAK_SOZ
#define DURAK_SOZ     50        /* ms; sözcük arası */
#endif
#ifndef DURAK_VIRGUL
#define DURAK_VIRGUL 240
#endif
#ifndef DURAK_NOKTA
#define DURAK_NOKTA  400
#endif
#ifndef DURAK_IKI
#define DURAK_IKI    300
#endif

/* Formant kayma süresi. Deneyle değiştirilebilsin diye dışarıdan
 * tanımlanabilir; birim_sesle içinde kullanıldığı için TANIM ORADAN
 * ÖNCE olmak zorunda. Önce sözcük vurgusu bölümüne konmuştu, yani
 * kullanımdan 190 satır SONRA: derleme kırılıyordu. */
#ifndef GECIS_MS
#define GECIS_MS 40
#endif

/* Üst formantlar sabittir: ünlü kimliğini taşımazlar ama tayfın
 * 3-5 kHz bandını doldururlar. Eski sürümde yoktular; o bant 19 dB
 * eksik kalıyordu ve ünsüzler duyulmuyordu. */
#ifndef DEDE_F4
#define DEDE_F4       3300
#endif
#ifndef DEDE_F5
#define DEDE_F5       4300
#endif
/* Altıncı formant ve geniş bantlar.
 * Ölçüldü: F5'ten sonra zincir dikçe iniyor ve 3.5-5 kHz'de bir
 * ÇUKUR kalıyordu (hedefe göre 15 dB). Gerçek bir ses yolunda sonsuz
 * sayıda kutup vardır; beşte kesmek üst bölgeyi boşaltıyor. Altıncı
 * kutup ve geniş bantlar o boşluğu ses yolunun kendisinden doldurur —
 * "s" sesini bağırtmadan. */
#ifndef DEDE_F6
#define DEDE_F6       5500
#endif
#ifndef UST_BANT4
#define UST_BANT4     400
#endif
#ifndef UST_BANT5
#define UST_BANT5     500
#endif
#ifndef UST_BANT6
#define UST_BANT6     700
#endif

/* Türkçe seslilerin formantları (erkek sesi, Hz).
 * Önceki tabloda 'ı' ile 'ö' birbirine fazla yakındı (ölçülen ayrım
 * 83 Hz). Türkçe 'ı' ARKA ve DÜZ bir ünlüdür ([ɯ]): F2'si düşüktür.
 * 'ö' ise ÖN ve YUVARLAK ([ø]): F2'si daha yukarıdadır. İkisini aynı
 * F2 bölgesine koymak onları duyulamaz biçimde birbirine yaklaştırıyordu. */
static const struct { uint32_t kod; uint16_t f1, f2, f3; } SESLI[] = {
    { 'a',    730, 1250, 2500 },   /* [ɑ] arka, açık      */
    { 'e',    500, 1900, 2550 },   /* [e̞] ön, yarı açık   */
    { 0x0131, 350, 1300, 2400 },   /* [ɯ] arka, düz, dar  */
    { 'i',    290, 2250, 2900 },   /* [i] ön, düz, dar    */
    { 'o',    480,  850, 2450 },   /* [o] arka, yuvarlak  */
    { 0x00F6, 440, 1550, 2300 },   /* [ø] ön, yuvarlak    */
    { 'u',    320,  750, 2350 },   /* [u] arka, yuvarlak  */
    { 0x00FC, 300, 1800, 2250 },   /* [y] ön, yuvarlak    */
};
#define SESLI_SAYI ((int)(sizeof(SESLI) / sizeof(SESLI[0])))

static int sesli_ara(uint32_t kod)
{
    for (int i = 0; i < SESLI_SAYI; i++) if (SESLI[i].kod == kod) return i;
    return -1;
}

/* --- Rastgelelik: gürültü ve titreşim için --- */
static uint32_t g_tohum = 0x5EED1234u;

static int32_t gurultu(void)
{
    g_tohum = g_tohum * 1103515245u + 12345u;
    return (int32_t)((g_tohum >> 16) & 0xFFFF) - 32768;
}

/* ÖLÇÜM KANCASI — yalnız ana bilgisayar render'ında tanımlı.
 * Çınlayıcı iç sınıra dayanırsa sinyal kırpılır ve formant yapısı
 * bozulur. Bu sayaç o durumu görünür kılar; çekirdek derlemesinde
 * hiç kod üretmez. */
#ifdef DEDE_OLCUM
extern unsigned long dede_kirpma;   /* çınlayıcı iç sınırı */
extern unsigned long dede_tasma;    /* 16 bit çıkış sınırı */
extern unsigned long dede_ornek;    /* üretilen toplam örnek */
#define KIRP_SAY() (dede_kirpma++)
#define TAS_SAY()  (dede_tasma++)
#define ORN_SAY()  (dede_ornek++)
#else
#define KIRP_SAY() ((void)0)
#define TAS_SAY()  ((void)0)
#define ORN_SAY()  ((void)0)
#endif

/* İZ DÖKÜMÜ: zincirin her katı ayrı ayrı yazılır. Hece başına iki
 * darbe kusurunun hangi katta doğduğunu VARSAYIMLA değil ölçümle
 * bulmak için. Yalnız DEDE_IZ tanımlıyken kod üretir. */
#ifdef DEDE_IZ
extern void dede_iz_dok(int32_t kaynak, int32_t egim, int32_t yol,
                        int32_t turev, int32_t cikis);
#define IZ_DOK(a, b, c, d, e) dede_iz_dok((a), (int32_t)(b), (c), (d), (e))
#else
#define IZ_DOK(a, b, c, d, e) ((void)0)
#endif

/* --- İki kutuplu çınlayıcı (formant) ---
 * y[n] = g·x[n] + a1·y[n-1] + a2·y[n-2]
 * g = 1 − a1 − a2  seçilir: DC kazancı tam 1 olur. Böylece zincire
 * beş çınlayıcı ardışık bağlansa bile kazanç patlamaz (ölçülen en
 * yüksek zincir kazancı 15.4). Eski paralel yapıda her dalın tepe
 * kazancı 128 ile 446 arasındaydı ve elle ağırlıklandırılıyordu. */
typedef struct { int32_t y1, y2, a1, a2, g; } cinlayici_t;

/* --- Kosinüs ---
 * BURADA ESKİ BİR HATA VARDI, BULUNMASI UZUN SÜRDÜ.
 *
 * Eskiden "cos(pi·t) ≈ 1 − 8t² + 8t⁴" parabol yaklaşımı kullanılıyor
 * ve yorumda "hata %1'in altında" deniyordu. Kosinüsün DEĞERİ için bu
 * doğru. Ama bize lazım olan şey çınlayıcının FREKANSI ve o frekans
 * acos(a1/2r) ile belirleniyor: kosinüs 1'e yakınken acos aşırı
 * duyarlıdır. 700 Hz için gerçek cos 0.995806 iken parabol 0.993194
 * veriyordu — küçücük bir fark, ama çınlayıcı 891 Hz'e kuruluyordu.
 *
 * Ölçüldü: SEKİZ ÜNLÜNÜN TAMAMI F1 ve F2'de yaklaşık %25 YUKARIDA
 * çınlıyordu. Yani sentezleyici en baştan beri yanlış ses yolunu
 * kuruyormuş; ünlüler ne amaçlanan yerdeydi ne de birbirinden yeterince
 * ayrılıyordu (en yakın çift 99 Hz).
 *
 * Yerine Taylor açılımı: cos(x) = 1 − x²/2 + x⁴/24 − x⁶/720.
 * x burada RADYANDIR (16.16). Formantlar 9 kHz'in altında, yani
 * x < pi/2; bu aralıkta hata 5e-4'ün altında (8 kHz'de 4 Hz sapma).
 * pi/2 üstü için cos(pi−x) = −cos(x) bağıntısı kullanılır. */
#define PI_16     205887        /* pi  << 16 */
#define PI_YARIM  102944        /* pi/2 << 16 */

static int32_t kosinus(int32_t w)
{
    int isaret = 1;
    if (w < 0) w = -w;
    if (w > PI_16) w = PI_16;
    if (w > PI_YARIM) { w = PI_16 - w; isaret = -1; }
    int64_t x2 = kaydir((int64_t)w * w);
    int64_t x4 = kaydir(x2 * x2);
    int64_t x6 = kaydir(x4 * x2);
    int32_t c = BIR - (int32_t)(x2 / 2) + (int32_t)(x4 / 24)
                    - (int32_t)(x6 / 720);
    return isaret * c;
}

/* Katsayıları hesaplar, DURUMU BOZMAZ. Formant geçişleri sırasında
 * saniyede onlarca kez çağrılır; y1/y2 sıfırlansaydı süzgeç her
 * seferinde yeniden çınlar, ses çıtırdardı. */
static void cinlayici_ayarla(cinlayici_t *c, uint32_t frekans, uint32_t bant)
{
    int32_t r = BIR - (int32_t)(((uint64_t)bant * 205887u) / HZ);  /* pi<<16 */
    if (r < 0) r = 0;
    if (r > BIR - 200) r = BIR - 200;
    /* w = 2·pi·f / HZ, 16.16 radyan */
    int32_t w = (int32_t)(((uint64_t)frekans * 2u * PI_16) / HZ);
    if (w > PI_16) w = PI_16;
    int32_t cosw = kosinus(w);
    c->a1 = kaydir(((int64_t)2 * r) * cosw);
    c->a2 = -kaydir((int64_t)r * r);
    c->g  = BIR - c->a1 - c->a2;
    if (c->g < 1) c->g = 1;
}

static void cinlayici_kur(cinlayici_t *c, uint32_t frekans, uint32_t bant)
{
    cinlayici_ayarla(c, frekans, bant);
    c->y1 = c->y2 = 0;
}

static int32_t cinlayici_isle(cinlayici_t *c, int32_t x)
{
    int64_t y = (int64_t)kaydir((int64_t)x * c->g)
              + (int64_t)kaydir((int64_t)c->a1 * c->y1)
              + (int64_t)kaydir((int64_t)c->a2 * c->y2);
    if (y >  IC_SINIR) { y =  IC_SINIR; KIRP_SAY(); }
    if (y < -IC_SINIR) { y = -IC_SINIR; KIRP_SAY(); }
    c->y2 = c->y1;
    c->y1 = (int32_t)y;
    return (int32_t)y;
}

/* Ses başına düzey (binde; 1000 = ölçünlü).
 * Bu değerler TAHMİN DEĞİL, ÇÖZÜM: her ses tek başına seslendirilip
 * ünlü ortalamasına göre düzeyi ölçüldü, doğal aralığın ortasına
 * oturtuldu (olcum/kalibre.py, üç turda yakınsıyor). Gürültü genliği
 * ile çıkış düzeyi doğrusal olduğu için tek adımda çözülür.
 * Düzeltmenin büyüklüğü sorunun büyüklüğünü gösteriyor: "s" sesi
 * 16 kat yüksek çıkıyordu. */
#ifndef GEN_S
#define GEN_S   63
#endif
#ifndef GEN_SH
#define GEN_SH  196
#endif
#ifndef GEN_F
#define GEN_F   63
#endif
#ifndef GEN_H
#define GEN_H  445
#endif
#ifndef GEN_Z
#define GEN_Z   70
#endif
#ifndef GEN_J
#define GEN_J  195
#endif
#ifndef GEN_P
#define GEN_P 1036
#endif
#ifndef GEN_T
#define GEN_T  128
#endif
#ifndef GEN_K
#define GEN_K  340
#endif
#ifndef GEN_C
#define GEN_C  185
#endif
#ifndef GEN_MN
#define GEN_MN  750
#endif

/* --- Harf -> ses birimi eşlemesi --- */
static void birim_coz_ham(uint32_t kod, birim_t *b, const birim_t *onceki_sesli);

static void birim_coz(uint32_t kod, birim_t *b, const birim_t *onceki_sesli)
{
    birim_coz_ham(kod, b, onceki_sesli);
#if SURE_OLCEK != 100
    uint32_t s = (uint32_t)b->sure_ms * SURE_OLCEK / 100;
    b->sure_ms = (uint16_t)(s < 12 ? 12 : s);   /* 12 ms'nin altı duyulmaz */
#endif
}

static void birim_coz_ham(uint32_t kod, birim_t *b, const birim_t *onceki_sesli)
{
    b->genlik = 1000;
    b->perde_ek = 0;
    b->gf1 = b->gf2 = b->gf3 = 0;

    int s = sesli_ara(kod);
    if (s >= 0) {
        b->tur = T_SESLI;
        b->f1 = SESLI[s].f1; b->f2 = SESLI[s].f2; b->f3 = SESLI[s].f3;
        b->sure_ms = 160;
        return;
    }

    /* Komşu ünlü ÖN mü ARKA mı? Artdamak ünsüzlerinin (g, k) locus'u
     * buna göre değişir: "ki" ile "kı"da dilin değdiği yer aynı
     * değildir. Türkçede ünlü uyumu olduğu için önceki ünlü iyi bir
     * göstergedir. Ölçüt F2: ön ünlülerde 1400 Hz'in üstündedir. */
    int on = (onceki_sesli && onceki_sesli->f2 > 1400);

    /* Varsayılan: komşu ünlünün ses yolunu koru. */
    b->f1 = onceki_sesli ? onceki_sesli->f1 : 450;
    b->f2 = onceki_sesli ? onceki_sesli->f2 : 1500;
    b->f3 = onceki_sesli ? onceki_sesli->f3 : 2500;

    switch (kod) {
    /* ---- dudak ---- */
    case 'p':
        b->tur = T_PATLAMA;  b->sure_ms = 85; b->genlik = GEN_P;
        b->f1 = 250; b->f2 =  750; b->f3 = 2100;
        b->gf1 = 700; b->gf2 = 1200; b->gf3 = 2000;   /* dağınık, pes */
        break;
    case 'b':
        b->tur = T_OTUMLU_PAT; b->sure_ms = 80; b->genlik = GEN_P;
        b->f1 = 250; b->f2 =  750; b->f3 = 2100;
        b->gf1 = 700; b->gf2 = 1200; b->gf3 = 2000;
        break;
    case 'm':
        b->tur = T_GENIZ; b->sure_ms = 85; b->genlik = GEN_MN;
        b->f1 = 250; b->f2 =  900; b->f3 = 2200;
        break;
    case 'f':
        b->tur = T_SURTUNME; b->sure_ms = 95; b->genlik = GEN_F;
        b->f1 = 300; b->f2 = 1100; b->f3 = 2400;
        b->gf1 = 1500; b->gf2 = 4500; b->gf3 = 7000;
        break;
    case 'v':
        /* Türkçe "v" çoğu konumda sürtünmelidir. Ölçüldü: salt akıcı
         * yapılınca "l" ile arası 1.64 dB'ye düşüyordu — ikisi de
         * alçak F2'li akıcı sesler olduğu için başka ayırıcı
         * kalmıyordu. Hafif bir dudak-diş hışırtısı ayrımı verir. */
        b->tur = T_AKICI; b->sure_ms = 70;
        b->f1 = 300; b->f2 = 1150; b->f3 = 2400;
        b->gf1 = 1600; b->gf2 = 3800; b->gf3 = 6000;
        b->genlik = 1000;
        break;

    /* ---- diş / diş eti ---- */
    case 't':
        b->tur = T_PATLAMA; b->sure_ms = 85; b->genlik = GEN_T;
        b->f1 = 250; b->f2 = 1750; b->f3 = 2600;
        b->gf1 = 3600; b->gf2 = 4800; b->gf3 = 6000;  /* tiz, keskin */
        break;
    case 'd':
        b->tur = T_OTUMLU_PAT; b->sure_ms = 80; b->genlik = GEN_T;
        b->f1 = 250; b->f2 = 1750; b->f3 = 2600;
        b->gf1 = 3600; b->gf2 = 4800; b->gf3 = 6000;
        break;
    case 'n':
        b->tur = T_GENIZ; b->sure_ms = 85; b->genlik = GEN_MN;
        b->f1 = 250; b->f2 = 1700; b->f3 = 2600;
        break;
    case 's':
        b->tur = T_SURTUNME; b->sure_ms = 95; b->genlik = GEN_S;
        b->f1 = 300; b->f2 = 1800; b->f3 = 2700;
        b->gf1 = 4000; b->gf2 = 6500; b->gf3 = 8200;
        break;
    case 'z':
        b->tur = T_SURTUNME; b->sure_ms = 85; b->genlik = GEN_Z;
        b->f1 = 300; b->f2 = 1800; b->f3 = 2700;
        b->gf1 = 3800; b->gf2 = 6000; b->gf3 = 8000;
        break;
    case 'l':
        b->tur = T_AKICI; b->sure_ms = 70;
        /* "l" ön ünlüde ince, arka ünlüde kalındır. */
        b->f1 = 330; b->f2 = on ? 1500 : 900; b->f3 = 2700;
        break;
    case 'r':
        b->tur = T_AKICI; b->sure_ms = 60;
        b->f1 = 400; b->f2 = 1350; b->f3 = 1700;      /* alçak F3 */
        break;

    /* ---- damak ---- */
    case 0x015F:                                       /* ş */
        b->tur = T_SURTUNME; b->sure_ms = 95; b->genlik = GEN_SH;
        b->f1 = 300; b->f2 = 2000; b->f3 = 2600;
        b->gf1 = 2400; b->gf2 = 3600; b->gf3 = 5200;
        break;
    case 'j':
        b->tur = T_SURTUNME; b->sure_ms = 85; b->genlik = GEN_J;
        b->f1 = 300; b->f2 = 2000; b->f3 = 2600;
        b->gf1 = 2400; b->gf2 = 3600; b->gf3 = 5200;
        break;
    case 0x00E7:                                       /* ç */
        b->tur = T_PATLAMA; b->sure_ms = 95; b->genlik = GEN_C;
        b->f1 = 280; b->f2 = 2100; b->f3 = 2800;
        b->gf1 = 2200; b->gf2 = 3400; b->gf3 = 5000;
        break;
    case 'c':
        b->tur = T_OTUMLU_PAT; b->sure_ms = 90; b->genlik = GEN_C;
        b->f1 = 280; b->f2 = 2100; b->f3 = 2800;
        b->gf1 = 2200; b->gf2 = 3400; b->gf3 = 5000;
        break;
    case 'y':
        b->tur = T_AKICI; b->sure_ms = 60;
        b->f1 = 270; b->f2 = 2250; b->f3 = 3000;
        break;

    /* ---- artdamak ----
     * g ve k'nin locus'u komşu ünlüye bağlıdır. Ön ünlüde F2 ile F3
     * birbirine yaklaşır ("artdamak sıkışması"); bu, kulağın k/g'yi
     * p/t'den ayırdığı en güçlü ipucudur. */
    case 'k':
        b->tur = T_PATLAMA; b->sure_ms = 85; b->genlik = GEN_K;
        /* ARTDAMAK SIKIŞMASI: k/g'de F2 ile F3 birbirine yaklaşır.
         * Ölçüldü: F3 2300'de bırakılınca "k", "p"den ayrılmıyordu
         * (1.61 dB) — çünkü arka ünlüde k'nin F2'si (1250) tam da
         * "a"nın kendi F2'sine (1212) denk geliyor, yani hiç bükülme
         * olmuyor. Ayırt eden şey F2'nin yeri değil, F3'ün inip F2'ye
         * yaklaşmasıdır. */
        b->f1 = 250;
        b->f2 = on ? 2150 : 1150;
        b->f3 = on ? 2450 : 1850;
        b->gf1 = on ? 2400 : 1400; b->gf2 = on ? 3200 : 2000;
        b->gf3 = on ? 4000 : 2800;
        break;
    case 'g':
        b->tur = T_OTUMLU_PAT; b->sure_ms = 80; b->genlik = GEN_K;
        b->f1 = 250;
        b->f2 = on ? 2150 : 1150;
        b->f3 = on ? 2450 : 1850;
        b->gf1 = on ? 2400 : 1400; b->gf2 = on ? 3200 : 2000;
        b->gf3 = on ? 4000 : 2800;
        break;
    case 0x011F:                                       /* ğ: ünlüyü uzatır */
        b->tur = T_AKICI; b->sure_ms = 55;
        break;

    /* ---- gırtlak ---- */
    case 'h':
        b->tur = T_SURTUNME; b->sure_ms = 85; b->genlik = GEN_H;
        /* "h" komşu ünlünün ses yolundan geçen soluktur: locus yok. */
        b->gf1 = 900; b->gf2 = 1800; b->gf3 = 2800;
        break;

    default:
        b->tur = T_SESSIZLIK; b->sure_ms = 70;
        break;
    }
}

/* UTF-8 çözücü: Türkçe harfler iki bayttır. */
static uint32_t utf8_al(const char **p)
{
    const unsigned char *u = (const unsigned char *)*p;
    if (u[0] < 0x80) { (*p)++; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        uint32_t k = (uint32_t)((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        *p += 2;
        /* Büyük harfleri küçüğe indir (Türkçe dahil). */
        if (k == 0x00C7) k = 0x00E7;   /* Ç */
        if (k == 0x011E) k = 0x011F;   /* Ğ */
        if (k == 0x0130) k = 'i';      /* İ */
        if (k == 0x00D6) k = 0x00F6;   /* Ö */
        if (k == 0x015E) k = 0x015F;   /* Ş */
        if (k == 0x00DC) k = 0x00FC;   /* Ü */
        return k;
    }
    (*p)++;
    return u[0];
}

/* =====================================================================
 * GIRTLAK KAYNAĞI — Rosenberg darbesi
 *
 * Tek örneklik darbe (eski sürüm) DÜZ tayf verir: doğal değildir ve
 * tiz bölgeyi gereğinden parlak bırakır. Gerçek gırtlak akışı bir
 * DARBE BİÇİMİdir:
 *   açılış  (süre t1): g(u) = 3u² − 2u³      u = t/t1
 *   kapanış (süre t2): g(v) = 1 − v²         v = (t−t1)/t2
 *   geri kalan dönem : 0  (kapalı evre)
 * Bu biçim doğal olarak -12 dB/sekizli iner ve harmoniklerin TAMAMI
 * vardır; formantları uyaracak enerji kaynakta hazırdır.
 *
 * Açıklık oranı (t1+t2)/T ≈ %56: ne nefesli ne gergin, yaşlı ve tok
 * bir gırtlağa yakışır.
 * ===================================================================== */
#ifndef GIRTLAK_ACILIS
#define GIRTLAK_ACILIS  40      /* dönemin yüzdesi */
#endif
#ifndef GIRTLAK_KAPANIS
#define GIRTLAK_KAPANIS 16
#endif

static int32_t girtlak_darbe(uint32_t faz, uint32_t donem)
{
    uint32_t t1 = donem * GIRTLAK_ACILIS / 100;
    uint32_t t2 = donem * GIRTLAK_KAPANIS / 100;
    if (t1 == 0) t1 = 1;
    if (t2 == 0) t2 = 1;

    if (faz < t1) {
        int32_t u  = (int32_t)(((uint64_t)faz << KESIR) / t1);
        int32_t u2 = kaydir((int64_t)u * u);
        int32_t u3 = kaydir((int64_t)u2 * u);
        return 3 * u2 - 2 * u3;                       /* 0 .. BIR */
    }
    if (faz < t1 + t2) {
        int32_t v  = (int32_t)(((uint64_t)(faz - t1) << KESIR) / t2);
        int32_t v2 = kaydir((int64_t)v * v);
        return BIR - v2;                              /* BIR .. 0 */
    }
    return 0;                                         /* kapalı evre */
}

/* --- Tayf eğimi denetimi ---
 * Hedef kayıtta eğim -13.9 dB/onkat. Rosenberg darbesi + yayılım
 * bundan daha parlak çıkıyor. Tek kutuplu alçak geçiren süzgeç farkı
 * kapatır. ÖNEMLİ: bu süzgeç DAR DEĞİLDİR, çınlamaz — eski sürümdeki
 * hata buydu. Katsayı taranarak seçildi. */
/* Katsayilar -D ile taranabilir olsun diye korumali tanimlandi:
 * deger secimi TAHMIN degil OLCUM isidir. */
#ifndef EGIM_KATSAYI
#define EGIM_KATSAYI  26000     /* 16.16; buyudukce ses koyulasir */
#endif

/* --- Ağızdan yayılım ---
 * Ses dudaktan havaya çıkarken türevlenir: tayfa +6 dB/sekizli ekler.
 * Kaynağın -12'si ile birlikte net eğim ≈ -6 dB/sekizli olur. */

#ifndef KAYNAK_TEPE
#define KAYNAK_TEPE   11000
#endif
#ifndef SOLUK_BOLME
#define SOLUK_BOLME   6         /* soluk gürültüsü: yaşlı sesin pürüzü */
#endif

/* Çıkış kazancı.
 * Yayılım türevi, alçak frekansları 40 dB'ye varan oranda zayıflatır
 * (78 Hz'de çarpan 2pi*78/48000 = 0.0102). İç çözünürlük payı geri
 * alınırken bu kayıp da telafi edilmeli; yoksa ünlüler duyulmaz.
 * Ölçüldü: ham >>IC_KAYMA ile ünlü tepesi 636 kalıyordu (gereken
 * ~22000, yani 30 dB eksik). */
#ifndef CIKIS_KAYMA
#define CIKIS_KAYMA   6
#endif

/* --- ÇIKIŞ TINI KATI --------------------------------------------
 * Hedef kayıtta enerjinin yaklaşık dörtte üçü 200 Hz'in ALTINDADIR
 * (60-200 bandı -1.2 dB). Bu, mikrofona yakın konuşan bir radyo
 * sesinin göğüs ağırlığıdır. Aynı etkiyi KAYNAĞI bozarak elde etmeye
 * çalışmak önceki sürümü uğultuya çevirmişti: temel perdeyi
 * yükseltmek formantları besleyen harmonikleri yok ediyor.
 *
 * Doğrusu, ses yolu modelini olduğu gibi bırakıp çıkışa ayrı bir
 * TON denetimi koymak. Radyo stüdyosunda yapılan da budur.
 *   PES_KAZANC : 180 Hz altı raf yükseltmesi (16.16)
 *   TIZ_KAZANC : 2 kHz üstü varlık (presence) yükseltmesi (16.16)
 * Her ikisi de 0 ise kat devre dışıdır. */
#ifndef PES_KESIM
#define PES_KESIM 110
#endif
#ifndef PES_KAZANC
#define PES_KAZANC 393216    /* 6.0 — taranarak secildi */
#endif
#ifndef TIZ_KESIM
#define TIZ_KESIM 3400
#endif
#ifndef TIZ_KAZANC
#define TIZ_KAZANC 300000    /* 4.58 — taranarak secildi */
#endif
/* Son düzey. Tını katı kazanç ekler; sınıra dayanmamak için
 * geri alınır. Taranarak seçildi: sınırlayıcıya hiç değmeyen
 * en yüksek değer. */
#ifndef CIKIS_KAZANC
#define CIKIS_KAZANC  60000     /* 16.16 — taranarak: sınırlayıcıya değmeyen en yüksek düzey */
#endif
/* Sürtünmeli seslerin kaynak düzeyi (8.8 sabit nokta çarpan).
 * Doğal konuşmada ünlülerin 15-25 dB ALTINDADIR. Ölçüldü: eşit
 * kaynakla "ssss", "aaaa"dan 7 dB DAHA YÜKSEK çıkıyor ve çıkışı
 * taşırıyordu (örneklerin %2.2'si sınıra dayanıyordu). "s" sesi
 * cümleyi bastırınca geri kalan anlaşılmaz oluyor. */
#ifndef SURTUNME_KAZANC
#define SURTUNME_KAZANC 300     /* 16.16 */
#endif
/* Patlamalı sesler (p t k ç) ayrı ölçeklenir. Ölçüldü: sürtünmeyle
 * aynı kazançta "k" sesi ünlülerin 37 dB altında kalıyordu — yani
 * hiç duyulmuyordu. Patlama kısa ve keskindir; enerjisi zamana
 * yayılmadığı için daha yüksek bir kazanç gerekir. */
#ifndef PATLAMA_KAZANC
#define PATLAMA_KAZANC 400      /* 16.16 */
#endif

static int16_t g_tampon[PARCA * 2];

/* Birim seslendirme bağlamı: formant geçişleri için önceki birimin
 * bıraktığı ses yolu burada taşınır. */
typedef struct {
    int32_t     faz;
    uint32_t    f0;
    uint32_t    sf1, sf2, sf3;    /* o anki (kayan) formantlar */
    cinlayici_t c1, c2, c3, c4, c5, c6;
    cinlayici_t p1, p2, p3;       /* gürültü için paralel dal */
    int32_t     egim_y;           /* tayf eğimi süzgeci durumu */
    /* Çıkış tonu süzgeç durumları — 16.16 KESİRLİ tutulur.
     * Tamsayı tutulurken ÖLÜ BÖLGEye takılıyorlardı: pes rafının
     * katsayısı 600/65536 = 0.0092 kadar küçük olduğu için, giriş ile
     * durum arasındaki fark 54'ten azsa güncelleme sıfıra yuvarlanıyor
     * ve durum ORADA KALIYORDU. Yuvarlamayı en yakına çevirmek ölü
     * bölgeyi yarıya indirdi ama kaldırmadı; duraklarda çıkış hâlâ
     * sabit bir basamakta kilitleniyordu. Durumu 16 bit kesirle tutmak
     * ölü bölgeyi 65536 kat küçültür: süzgeç artık sıfıra iner. */
    int64_t     pes_y, pes_y2, pes_y3, tiz_y;
    int32_t     onceki_y;         /* yayılım türevi */
    int         kuruldu;
} ses_yolu_t;

static void ses_yolu_kur(ses_yolu_t *v)
{
    v->faz = 0;
    v->f0  = DEDE_TEMEL_PERDE;
    v->sf1 = 500; v->sf2 = 1500; v->sf3 = 2500;
    cinlayici_kur(&v->c1, v->sf1,  90);
    cinlayici_kur(&v->c2, v->sf2, 110);
    cinlayici_kur(&v->c3, v->sf3, 170);
    cinlayici_kur(&v->c4, DEDE_F4, UST_BANT4);
    cinlayici_kur(&v->c5, DEDE_F5, UST_BANT5);
    cinlayici_kur(&v->c6, DEDE_F6, UST_BANT6);
    cinlayici_kur(&v->p1, 1500, 500);
    cinlayici_kur(&v->p2, 3000, 700);
    cinlayici_kur(&v->p3, 5000, 900);
    v->egim_y = 0;
    v->pes_y = v->pes_y2 = v->pes_y3 = v->tiz_y = 0;
    v->onceki_y = 0;
    v->kuruldu = 1;
}

/* Tek birimi seslendirir ve sürücüye yollar. */
static void birim_sesle(const birim_t *b, ses_yolu_t *v)
{
    uint32_t toplam = (uint32_t)b->sure_ms * HZ / 1000;
    uint32_t yazilan = 0;

    /* Ses yolu hedefi. Artık ÜNSÜZLERİN DE kendi hedefi var (locus),
     * ve o da ses yoludur: aynı ölçekten geçer. Gürültü bandı ayrı
     * alanda (gf1..gf3) durduğu için ölçekten etkilenmez — "s" ile
     * "ş"nin tiz kimliği korunur. */
    uint32_t hf1 = (uint32_t)((uint64_t)b->f1 * DEDE_F1_OLCEK / 100);
    uint32_t hf2 = (uint32_t)((uint64_t)b->f2 * DEDE_F2_OLCEK / 100);
    uint32_t hf3 = (uint32_t)((uint64_t)b->f3 * DEDE_F3_OLCEK / 100);
    int otumlu = (b->tur == T_SESLI || b->tur == T_GENIZ ||
                  b->tur == T_AKICI || b->tur == T_OTUMLU_PAT);

    /* Geçiş süresi: formantlar önceki birimin bıraktığı yerden
     * hedefe KAYAR. Anlaşılırlığın büyük kısmı bu geçiştedir;
     * ünsüzün kimliği, bitişik ünlünün formantını nasıl büktüğünden
     * okunur. Eski sürümde geçiş yoktu, her birim keskin başlıyordu. */
    uint32_t gecis = HZ * GECIS_MS / 1000;
    if (gecis > toplam) gecis = toplam;

    uint32_t bas_f1 = v->sf1, bas_f2 = v->sf2, bas_f3 = v->sf3;
    uint32_t bant1 = 90, bant2 = 110, bant3 = 170;
    if (b->tur == T_GENIZ) { bant1 = 150; bant2 = 200; bant3 = 250; }

    while (yazilan < toplam) {
        uint32_t n = (toplam - yazilan < PARCA) ? (toplam - yazilan) : PARCA;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t k = yazilan + i;

            /* --- ses yolu kayması (32 örnekte bir yeter) --- */
            if ((k & 31) == 0) {
                /* Ses yolu hedefe KAYAR — her birimde, ünsüzlerde de.
                 * Ünsüzün kimliği işte bu bükülmededir: "aba", "ada",
                 * "aga" hecelerinde ünlünün F2'si sırasıyla aşağı,
                 * ortaya ve yukarı çekilir. Kulak bunu okur.
                 *
                 * Eskiden geçiş yalnız ötümlü birimlerde vardı, çünkü
                 * ünsüzlerin "formantı" diye gürültü bandı (4000-8200
                 * Hz) tutuluyordu ve yüksek Q'lu bir çınlayıcıyı içinde
                 * enerji varken oraya çekmek çıkışı taşırıyordu. Artık
                 * gürültü bandı ayrı alanda; buradaki hedefler ses yolu
                 * aralığında (250-2800 Hz), kayma güvenli. */
                if (b->tur != T_SESSIZLIK) {
                    if (k < gecis) {
                        v->sf1 = bas_f1 + (int32_t)(hf1 - bas_f1) * (int32_t)k / (int32_t)gecis;
                        v->sf2 = bas_f2 + (int32_t)(hf2 - bas_f2) * (int32_t)k / (int32_t)gecis;
                        v->sf3 = bas_f3 + (int32_t)(hf3 - bas_f3) * (int32_t)k / (int32_t)gecis;
                    } else {
                        v->sf1 = hf1; v->sf2 = hf2; v->sf3 = hf3;
                    }
                    cinlayici_ayarla(&v->c1, v->sf1, bant1);
                    cinlayici_ayarla(&v->c2, v->sf2, bant2);
                    cinlayici_ayarla(&v->c3, v->sf3, bant3);
                }
                if (b->gf1) {
                    cinlayici_ayarla(&v->p1, b->gf1, 500);
                    cinlayici_ayarla(&v->p2, b->gf2, 700);
                    cinlayici_ayarla(&v->p3, b->gf3, 900);
                }
            }

            /* ===========================================
             * İKİ KAYNAK HER ZAMAN AKAR.
             *
             * Önceki sürümde ötümsüz birimlerde çıkış doğrudan 0
             * yapılıyordu. Yayılım türevi (y - önceki_y) bunu her
             * duraklamada DEV BİR ÇITLAMAYA çeviriyordu: ölçüldü,
             * sekiz saniyelik bir cümlede 11111 örnek sınıra
             * dayanıyordu ve tayf eğimi +3.6'ya çıkıyordu (çıtlama
             * geniş bantlıdır). Çözüm: süzgeçleri hiç durdurmamak.
             * Ötümsüz birimde gırtlak kaynağı sıfırlanır ama zincir
             * beslenmeye devam eder; kendiliğinden söner.
             *
             * Genlik zarfı da artık ÇIKIŞA değil KAYNAĞA uygulanır;
             * süreksizlik baştan doğmaz.
             * =========================================== */
            uint32_t kenar = HZ / 250;
            int32_t zarf = BIR;
            if (k < kenar)
                zarf = (int32_t)(((int64_t)BIR * k) / kenar);
            else if (k + kenar > toplam)
                zarf = (int32_t)(((int64_t)BIR * (toplam - k)) / kenar);

            /* --- ötümlü kaynak --- */
            int32_t kaynak = 0;
            if (otumlu) {
                int32_t fp = (int32_t)v->f0 + b->perde_ek;
                if (fp < 50) fp = 50;
                if (fp > 300) fp = 300;
                uint32_t donem = HZ / (uint32_t)fp;
                uint32_t titre = (uint32_t)((gurultu() >> 13) & 3);
                v->faz += 1;
                if ((uint32_t)v->faz >= donem + titre) v->faz = 0;

                kaynak = girtlak_darbe((uint32_t)v->faz, donem);
                kaynak = kaydir((int64_t)kaynak * KAYNAK_TEPE);
                kaynak += gurultu() >> SOLUK_BOLME;     /* soluk */
                if (b->tur == T_OTUMLU_PAT) {
                    /* ÖTÜM ÇUBUĞU: b, d, g'de dudak/dil kapalıyken
                     * gırtlak çalışmaya devam eder; dışarı yalnız pes
                     * bir uğultu sızar. p, t, k'de bu yoktur — ikisini
                     * ayıran birincil ipucu budur. Kapanma boyunca
                     * kaynak kısılır, bırakışta açılır. */
                    uint32_t birak = toplam * 55 / 100;
                    if (k < birak) kaynak = kaynak / 7;
                }
                kaynak = kaynak * (int32_t)b->genlik / 1000;
                kaynak = kaydir((int64_t)kaynak * zarf);
            }

            /* --- tayf eğimi (tek kutuplu, çınlamaz) --- */
            v->egim_y += kaydir((int64_t)(kaynak - v->egim_y)
                                * (BIR - EGIM_KATSAYI));

            /* --- kademeli ses yolu (hep çalışır) --- */
            int32_t s = v->egim_y << IC_KAYMA;
            s = cinlayici_isle(&v->c1, s);
            s = cinlayici_isle(&v->c2, s);
            s = cinlayici_isle(&v->c3, s);
            s = cinlayici_isle(&v->c4, s);
            s = cinlayici_isle(&v->c5, s);
            s = cinlayici_isle(&v->c6, s);
            int32_t y = s;

            /* --- gürültü kaynağı ve paralel bant (hep çalışır) --- */
            int32_t gkaynak = 0, gkazanc = 0;
            if (b->tur == T_SURTUNME) {
                gkaynak = gurultu();  gkazanc = SURTUNME_KAZANC;
            } else if (b->tur == T_PATLAMA) {
                uint32_t patla = toplam * 2 / 3;        /* önce kapanma */
                if (k > patla) { gkaynak = gurultu(); gkazanc = PATLAMA_KAZANC; }
            } else if (b->tur == T_AKICI && b->gf1) {
                /* "v" gibi hafif sürtünmeli akıcılar */
                gkaynak = gurultu(); gkazanc = SURTUNME_KAZANC / 3;
            } else if (b->tur == T_OTUMLU_PAT) {
                /* Ötümlü patlamanın da bırakış gürültüsü vardır, ama
                 * daha kısa ve kısıktır (ötüm hemen başlar). */
                uint32_t birak = toplam * 55 / 100;
                if (k > birak && k < birak + HZ / 125) {
                    gkaynak = gurultu(); gkazanc = PATLAMA_KAZANC / 2;
                }
            }
            gkazanc = gkazanc * (int32_t)b->genlik / 1000;
            gkaynak = kaydir((int64_t)gkaynak * zarf);
            /* Kazanç 16.16; iç çözünürlük payı burada veriliyor. */
            int32_t gs = kaydir(((int64_t)gkaynak << IC_KAYMA) * gkazanc);
            int32_t gy  = cinlayici_isle(&v->p1, gs);
            gy += cinlayici_isle(&v->p2, gs);
            gy += cinlayici_isle(&v->p3, gs);
            y += gy / 2;

            /* Yayılım: birinci fark türevi (+6 dB/sekizli). */
            int32_t turev = y - v->onceki_y;
            v->onceki_y = y;
            int32_t cik = turev >> CIKIS_KAYMA;

            /* --- tını katı: pes rafı + varlık --- */
            {
                /* tek kutuplu alçak geçiren; a = 2*pi*fc/HZ */
                /* ÜÇ kutuplu raf.
                 * İki kutupluyken etek fazla genişti: 130 Hz'e kurulan
                 * raf 250-400 Hz'i de yükseltiyor, yani F1'in TAM
                 * ÜSTÜNE biniyordu. Ölçüldü: sekiz ünlünün yedisinde
                 * LPC gerçek F1 yerine rafın tepesini okuyordu (sapma
                 * %36-63) ve en yakın ünlü çifti 52 mel'e düşüyordu —
                 * yani ünlüler yeniden birbirine karışıyordu. Üçüncü
                 * kutup eteği -18 dB/sekizliye diklestiriyor; 250
                 * Hz'de yükseltme pratikte kalmıyor. */
                int32_t ap = (int32_t)(((uint64_t)PES_KESIM * 2u * PI_16) / HZ);
                int64_t ck = (int64_t)cik << KESIR;      /* 16.16 */
                v->pes_y  += ((ck - v->pes_y) * ap) >> KESIR;
                v->pes_y2 += ((v->pes_y - v->pes_y2) * ap) >> KESIR;
                v->pes_y3 += ((v->pes_y2 - v->pes_y3) * ap) >> KESIR;
                cik += (int32_t)((v->pes_y3 * PES_KAZANC) >> (KESIR * 2));

                int32_t at = (int32_t)(((uint64_t)TIZ_KESIM * 2u * PI_16) / HZ);
                int64_t ck2 = (int64_t)cik << KESIR;
                v->tiz_y += ((ck2 - v->tiz_y) * at) >> KESIR;
                int64_t tiz = ck2 - v->tiz_y;         /* yüksek geçiren */
                cik += (int32_t)((tiz * TIZ_KAZANC) >> (KESIR * 2));
                cik  = kaydir((int64_t)cik * CIKIS_KAZANC);
            }

            IZ_DOK(kaynak, v->egim_y, y, turev, cik);
            ORN_SAY();
            if (cik >  30000) { cik =  30000; TAS_SAY(); }
            if (cik < -30000) { cik = -30000; TAS_SAY(); }
            g_tampon[i * 2]     = (int16_t)cik;
            g_tampon[i * 2 + 1] = (int16_t)cik;
        }
        ses_cal(g_tampon, n);
        yazilan += n;
    }
}

/* =====================================================================
 * VURGU — sözcük bir bütün olarak seslendirilir
 *
 * NEDEN: ölçüldü, DEDE'nin bütün heceleri aynı şiddette çıkıyordu.
 * Gerçek konuşmada en güçlü hece ile en zayıfı arasında 8.3 kat
 * (Söylev kaydı), yapay seste 6.5 kat fark var; DEDE'de yalnız 3.5.
 * Ritim ve süre değişkenliği zaten yerindeydi (0.74'e karşı 0.68) —
 * eksik olan ŞİDDET KARŞITLIĞIydı. Bütün heceler eşit vurgulanınca
 * dinleyici sözcüğün nerede başlayıp bittiğini bulamaz; ses akar
 * gider ama çözülmez.
 *
 * Türkçede vurgu kural olarak SON HECEDEDİR ("kitáp", "geliyorúm").
 * Bu yüzden bir sözcüğün tamamı toplanmadan seslendirilemez: son
 * ünlünün hangisi olduğunu bilmek gerekir.
 * ===================================================================== */
#define SOZ_AZAMI 64            /* bir sözcükteki en çok birim */

#ifndef VURGU_GENLIK
#define VURGU_GENLIK  1500      /* binde */
#endif
#ifndef VURGU_SURE
#define VURGU_SURE     130      /* yüzde */
#endif
#ifndef VURGU_PERDE
#define VURGU_PERDE      7      /* Hz */
#endif
#ifndef ZAYIF_GENLIK
#define ZAYIF_GENLIK   650      /* binde */
#endif
#ifndef ZAYIF_SURE
#define ZAYIF_SURE      82      /* yüzde */
#endif

static void soz_vurgula(birim_t *soz, int n)
{
    int son_sesli = -1;
    for (int i = 0; i < n; i++)
        if (soz[i].tur == T_SESLI) son_sesli = i;
    if (son_sesli < 0) return;              /* ünlüsüz: dokunma */

    for (int i = 0; i < n; i++) {
        if (soz[i].tur != T_SESLI) continue;
        if (i == son_sesli) {
            soz[i].genlik   = (uint16_t)((uint32_t)soz[i].genlik * VURGU_GENLIK / 1000);
            soz[i].sure_ms  = (uint16_t)((uint32_t)soz[i].sure_ms * VURGU_SURE / 100);
            soz[i].perde_ek = VURGU_PERDE;
        } else {
            soz[i].genlik  = (uint16_t)((uint32_t)soz[i].genlik * ZAYIF_GENLIK / 1000);
            soz[i].sure_ms = (uint16_t)((uint32_t)soz[i].sure_ms * ZAYIF_SURE / 100);
        }
    }
}

int dede_konus(const char *metin)
{
    if (!metin || !ses_var()) return -1;

    ses_yolu_t yol;
    ses_yolu_kur(&yol);

    birim_t soz[SOZ_AZAMI];
    int     soz_n = 0;
    birim_t onceki_sesli;
    int onceki_var = 0;
    int birim_sayisi = 0;

    const char *p = metin;
    for (;;) {
        uint32_t kod = *p ? utf8_al(&p) : 0;
        if (kod >= 'A' && kod <= 'Z') kod += 32;

        int sinir = (kod == 0 || kod == ' ' || kod == ',' || kod == '.' ||
                     kod == '!' || kod == '?' || kod == ';' || kod == ':');

        if (!sinir) {
            if (soz_n < SOZ_AZAMI) {
                birim_coz(kod, &soz[soz_n], onceki_var ? &onceki_sesli : 0);
                if (soz[soz_n].tur == T_SESLI) {
                    onceki_sesli = soz[soz_n]; onceki_var = 1;
                }
                soz_n++;
            }
            continue;
        }

        /* --- sözcük bitti: vurgula ve seslendir --- */
        if (soz_n > 0) {
            soz_vurgula(soz, soz_n);
            for (int i = 0; i < soz_n; i++) {
                birim_sesle(&soz[i], &yol);
                birim_sayisi++;
                /* Ezgi düşüşü: cümle boyunca perde doğal olarak alçalır. */
                if ((birim_sayisi % 5) == 0 && yol.f0 > DEDE_TEMEL_PERDE - 24)
                    yol.f0--;
            }
            soz_n = 0;
        }

        if (kod == 0) break;

        /* --- duraklama --- */
        /* DURAK SÜRELERİ
         * Ölçüldü: susma payı DEDE'de %35.8, espeak'te %30.7; süre
         * ölçeği 60'a çekildiğinde %44'e çıkıyordu. Sözcük arası 120
         * ms, özenli okumanın bile üstünde — bağlı konuşmada sözcükler
         * birbirine girer, arada 0-50 ms kalır. Durak süreleri birim
         * sürelerinden AYRI tutulur: SURE_OLCEK ikisini birlikte
         * kısaltsa duraklar duyulmaz olurdu. */
        uint16_t bekle = DURAK_SOZ;                       /* boşluk */
        if (kod == ',' || kod == ';') bekle = DURAK_VIRGUL;
        else if (kod == '.' || kod == '!' || kod == '?') bekle = DURAK_NOKTA;
        else if (kod == ':') bekle = DURAK_IKI;
        /* ADLANDIRILMIŞ ilklendirme: birim_t'ye alan eklendiğinde
         * konumsal liste sessizce kayar. Bir kez kaydı ve bütün
         * duraklar yok oldu; derleyici uyarmamıştı. */
        birim_t d = { .tur = T_SESSIZLIK, .sure_ms = bekle, .genlik = 1000 };
        birim_sesle(&d, &yol);
        if (kod == '.' || kod == '!') yol.f0 = DEDE_TEMEL_PERDE;
    }
    ses_dur();
    return 0;
}

int dede_birim_sayisi(const char *metin)
{
    if (!metin) return 0;
    int n = 0;
    const char *p = metin;
    while (*p) {
        uint32_t kod = utf8_al(&p);
        if (kod != ' ') n++;
    }
    return n;
}

int dede_sesli_mi(uint32_t kod)
{
    if (kod >= 'A' && kod <= 'Z') kod += 32;
    return sesli_ara(kod) >= 0;
}
