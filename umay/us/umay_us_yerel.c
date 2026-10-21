/* =====================================================================
 * US MOTORU — YEREL ARKA UÇ (US1)
 *
 * Gerçek çıkarım: diskteki US1 model dosyasını okur ve tamsayı motoruyla
 * metin üretir. Model UMAY'a gömülü DEĞİLDİR; /UMAY.US1 değiştirilerek
 * yenilenir, UMAY yeniden derlenmez.
 *
 * DÜRÜSTLÜK: dosya yoksa, bozuksa ya da sözlük eksikse hazırlama
 * BAŞARISIZ döner ve US katmanı sıradaki arka uca (IPC, sonra stub)
 * düşer. Sahte başarı yoktur.
 *
 * KAPSAM: bu model KÜÇÜKTÜR (karakter düzeyi, iki katman, 64 boyut)
 * ve yalnızca doktrin metni üzerinde eğitilmiştir. Motorun doğru
 * çalıştığını kanıtlar; genel bir dil modeli değildir.
 * ===================================================================== */
#include "umay_us.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "us1.h"

#define MODEL_YOLU   "/UMAY.US1"
#define SOZLUK_YOLU  "/SOZLUK.TXT"
#define MODEL_AZAMI  (192 * 1024)
#define URET_AZAMI   160

typedef struct {
    umay_us_t   taban;
    us1_model_t model;
    int         hazir;
    char        sozluk[US1_AZAMI_SOZLUK * 4 + 1];   /* UTF-8: 4 bayta kadar */
    uint32_t    sozluk_boy;                         /* ham bayt sayısı      */
    /* Sözlük UTF-8'dir: 45 karakter 53 bayt tutabilir. İm indeksi
     * KARAKTER sayar, bayt değil. Bayt bayt indeksleyince Türkçe
     * harflerden sonraki her im kayıyor ve çıktıda bozuk karakter
     * oluşuyordu. Bu tablo im -> (başlangıç, uzunluk) eşlemesidir. */
    uint16_t    im_bas[US1_AZAMI_SOZLUK];
    uint8_t     im_boy[US1_AZAMI_SOZLUK];
    uint32_t    im_sayi;
} us_yerel_t;

static us_yerel_t g_yerel;
static uint8_t   *g_model_veri = 0;   /* öbekten: .bss'i şişirmesin */
static int32_t    g_mantik[US1_MANTIK_BOY];

/* ÖTÜKEN'in SYS_READ'i tek çağrıda en çok 4096 bayt döndürür; tüm
 * dosyayı almak için DÖNGÜ şart. Tek çağrıya güvenmek modeli 4 KB'ta
 * kesiyordu ve "bozuk model" gibi görünüyordu. */
static int dosya_oku(const char *yol, void *tampon, uint32_t azami)
{
    up_tut_t t = up_ac(yol, UP_OKU);
    if (t == UP_GECERSIZ || (int64_t)t < 0) return -1;
    uint8_t *h = (uint8_t *)tampon;
    uint32_t toplam = 0;
    while (toplam < azami) {
        int64_t n = up_oku(t, h + toplam, azami - toplam);
        if (n <= 0) break;
        toplam += (uint32_t)n;
    }
    up_kapat(t);
    return (int)toplam;
}

static int utf8_boy(unsigned char b)
{
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Sözlüğü karakterlere böler ve im tablosunu kurar.
 * Dönen değer karakter sayısıdır; modelin sözlük boyuyla eşleşmeli. */
static uint32_t sozluk_coz(us_yerel_t *s)
{
    uint32_t i = 0, n = 0;
    while (i < s->sozluk_boy && n < US1_AZAMI_SOZLUK) {
        int b = utf8_boy((unsigned char)s->sozluk[i]);
        if (i + (uint32_t)b > s->sozluk_boy) break;    /* yarım karakter */
        s->im_bas[n] = (uint16_t)i;
        s->im_boy[n] = (uint8_t)b;
        i += (uint32_t)b;
        n++;
    }
    s->im_sayi = n;
    return n;
}

/* Metindeki bir KARAKTERİ ime çevirir. boy, karakterin bayt uzunluğu. */
static int im_bul(const us_yerel_t *s, const char *p, int boy)
{
    for (uint32_t i = 0; i < s->im_sayi; i++) {
        if (s->im_boy[i] != (uint8_t)boy) continue;
        const char *k = &s->sozluk[s->im_bas[i]];
        int esit = 1;
        for (int j = 0; j < boy; j++)
            if (k[j] != p[j]) { esit = 0; break; }
        if (esit) return (int)i;
    }
    return -1;
}

static int yerel_hazirla(umay_us_t *u, const char *model_yolu)
{
    us_yerel_t *s = (us_yerel_t *)u;
    const char *yol = (model_yolu && *model_yolu) ? model_yolu : MODEL_YOLU;
    s->hazir = 0;

    if (!g_model_veri) g_model_veri = (uint8_t *)up_ayir(MODEL_AZAMI);
    if (!g_model_veri) {
        gun_yaz("US-YEREL: model için bellek ayrılamadı.");
        return UM_YANLIS;
    }
    int n = dosya_oku(yol, g_model_veri, MODEL_AZAMI);
    if (n <= 0) {
        gun_yaz("US-YEREL: model dosyası yok (%s).", yol);
        return UM_YANLIS;
    }
    int r = us1_yukle(&s->model, g_model_veri, (uint32_t)n);
    if (r != US1_OLDU) {
        gun_yaz("US-YEREL: model bozuk ya da desteklenmiyor (kod %d).", r);
        return UM_YANLIS;
    }
    /* Sözlük yolu ayardan gelir. Modelin yanındaki dosyayı tahmin
     * etmek kırılgandı: iki ortamda ad ve büyük/küçük harf farklı. */
    const char *syol = ayar_metin("sozluk", SOZLUK_YOLU);
    int k = dosya_oku(syol, s->sozluk, sizeof s->sozluk - 1);
    if (k <= 0) {
        gun_yaz("US-YEREL: sözlük eksik ya da modelle uyuşmuyor (%s).", syol);
        return UM_YANLIS;
    }
    s->sozluk[k] = 0;
    s->sozluk_boy = (uint32_t)k;

    /* Sondaki satır sonu sözlüğe ait değildir. */
    while (s->sozluk_boy > 0 &&
           (s->sozluk[s->sozluk_boy - 1] == '\n' ||
            s->sozluk[s->sozluk_boy - 1] == '\r')) {
        s->sozluk_boy--;
        s->sozluk[s->sozluk_boy] = 0;
    }

    uint32_t kar = sozluk_coz(s);
    if (kar != s->model.sozluk) {
        gun_yaz("US-YEREL: sözlük modelle uyuşmuyor (%u karakter, "
                "model %u im bekliyor).", kar, s->model.sozluk);
        return UM_YANLIS;      /* sessizce kabul etmek bozuk çıktı üretirdi */
    }
    s->hazir = 1;
    gun_yaz("US-YEREL: model yüklendi — d=%d katman=%d sözlük=%d.",
            (int)s->model.d, (int)s->model.katman, (int)s->model.sozluk);
    return UM_OLDU;
}

/* UTF-8 karakterinin bayt uzunluğu. Bozuk baytta 1 döner: akış
 * kilitlenmesin, en kötü ihtimalle o karakter eşleşmez. */
static int yerel_uret(umay_us_t *u, const us_istem_t *i, char *cikti, uint32_t n)
{
    us_yerel_t *s = (us_yerel_t *)u;
    if (!s->hazir || !i || !cikti || n < 2) return UM_YANLIS;

    static uint32_t imler[US1_AZAMI_BAGLAM];
    uint32_t T = 0;
    const char *tohum_metin = (i->soru && *i->soru) ? i->soru : "Ötüken";
    for (const char *p = tohum_metin; *p && T < s->model.baglam / 2; ) {
        int b = utf8_boy((unsigned char)*p);
        int im = im_bul(s, p, b);
        if (im >= 0) imler[T++] = (uint32_t)im;
        p += b;
    }
    if (T == 0) return UM_YANLIS;      /* sözlükte hiç tanıdık harf yok */

    uint32_t tohum = (uint32_t)up_ms() | 1u;
    uint32_t yazilan = 0;
    for (uint32_t adim = 0; adim < URET_AZAMI && yazilan + 1 < n; adim++) {
        if (T >= s->model.baglam) {     /* bağlamı kaydır */
            for (uint32_t j = 1; j < T; j++) imler[j - 1] = imler[j];
            T--;
        }
        if (us1_ileri(&s->model, imler, T, g_mantik) != US1_OLDU)
            return yazilan ? UM_OLDU : UM_YANLIS;
        uint32_t im = us1_sec(&g_mantik[(T - 1) * s->model.sozluk],
                              s->model.sozluk, &tohum);
        imler[T++] = im;
        if (im >= s->im_sayi) break;
        int cb = s->im_boy[im];
        if (yazilan + (uint32_t)cb >= n) break;  /* tamponu taşırma */
        const char *kaynak = &s->sozluk[s->im_bas[im]];
        for (int j = 0; j < cb; j++) cikti[yazilan++] = kaynak[j];
        if (cb == 1 && kaynak[0] == '.' && yazilan > 24) break;  /* cümle sonu */
    }
    cikti[yazilan] = 0;
    return yazilan ? UM_OLDU : UM_YANLIS;
}

static void yerel_kapat(umay_us_t *u) { ((us_yerel_t *)u)->hazir = 0; }

umay_us_t *us_yerel_motor(void)
{
    g_yerel.taban.ad      = "us1-yerel";
    g_yerel.taban.hazirla = yerel_hazirla;
    g_yerel.taban.uret    = yerel_uret;
    g_yerel.taban.kapat   = yerel_kapat;
    g_yerel.hazir = 0;
    return &g_yerel.taban;
}
