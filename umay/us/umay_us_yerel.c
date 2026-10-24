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
 * KAPSAM: bu model KÜÇÜKTÜR (bayt düzeyi, 4 katman, 96 boyut, ~460 bin
 * parametre) ve UMAY'ın kendi bilgi derlemiyle (doktrin, V15, kişilik,
 * soru-yanıt) eğitilmiştir: us1/arac/egit.py. Genel bir dil modeli
 * DEĞİLDİR; öğrendiği konuları kendi sözüyle söyler, dışını bilmez.
 * ===================================================================== */
#include "umay_us.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "us1.h"

#define MODEL_YOLU   "/UMAY.US1"
#define SOZLUK_YOLU  "/SOZLUK.TXT"
#define MODEL_AZAMI  (2048 * 1024)  /* 96x4 model ~470 KB; büyümüş 96x8/ffn512 ~1,5 MB (Q8) */
#define URET_AZAMI   160

typedef struct {
    umay_us_t   taban;
    us1_model_t model;
    int         hazir;
    char        sozluk[US1_AZAMI_SOZLUK + 1];
    uint32_t    sozluk_boy;
} us_yerel_t;

static us_yerel_t g_yerel;
static uint8_t   *g_model_veri = 0;   /* öbekten: .bss'i şişirmesin */

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

static int yerel_hazirla(umay_us_t *u, const char *model_yolu)
{
    us_yerel_t *s = (us_yerel_t *)u;
    /* 1.5: UMAY'ın kendi eğittiği model (KABUL edildiyse) /KUT/UMAY/UMAY.US1;
     * yoksa ya da açılamazsa diskteki ana model. */
    const char *yol = (model_yolu && *model_yolu) ? model_yolu
                    : (up_var_mi("/KUT/UMAY/UMAY.US1") ? "/KUT/UMAY/UMAY.US1" : MODEL_YOLU);
    s->hazir = 0;

    if (!g_model_veri) g_model_veri = (uint8_t *)up_ayir(MODEL_AZAMI);
    if (!g_model_veri) {
        gun_yaz("US-YEREL: model için bellek ayrılamadı.");
        return UM_YANLIS;
    }
    /* Tampondan büyük dosya KESİLİP "bozuk" diye raporlanmasın: açıkça reddet */
    int64_t dosya_boy = up_boy(yol);
    if (dosya_boy > (int64_t)MODEL_AZAMI) {
        gun_yaz("US-YEREL: model dosyası çok büyük (%l bayt, sınır %d).", dosya_boy, (int)MODEL_AZAMI);
        return UM_YANLIS;
    }
    int n = dosya_oku(yol, g_model_veri, MODEL_AZAMI);
    if (n <= 0) {
        gun_yaz("US-YEREL: model dosyası yok (%s).", yol);
        return UM_YANLIS;
    }
    /* 1.5: büyümüş model (4'ten çok katman) KV tamponunu öbekten alır */
    if (n >= 16) {
        uint32_t katman = (uint32_t)g_model_veri[12] | ((uint32_t)g_model_veri[13] << 8);
        if (katman > us1_kv_katman() && katman <= US1_AZAMI_KATMAN) {
            static int32_t *kv = 0;
            if (!kv) kv = (int32_t *)up_ayir(sizeof(int32_t) * 2u * US1_AZAMI_KATMAN * US1_AZAMI_BAGLAM * US1_AZAMI_D);
            if (kv) us1_kv_kur(kv, US1_AZAMI_KATMAN);
        }
    }
    int r = us1_yukle(&s->model, g_model_veri, (uint32_t)n);
    if (r != US1_OLDU) {
        gun_yaz("US-YEREL: model bozuk ya da desteklenmiyor (kod %d).", r);
        return UM_YANLIS;
    }
    int k = dosya_oku(SOZLUK_YOLU, s->sozluk, sizeof s->sozluk - 1);
    if (k <= 0 || (uint32_t)k < s->model.sozluk) {
        gun_yaz("US-YEREL: sözlük eksik ya da modelle uyuşmuyor.");
        return UM_YANLIS;
    }
    s->sozluk[k] = 0;
    s->sozluk_boy = (uint32_t)k;
    s->hazir = 1;
    gun_yaz("US-YEREL: model yüklendi — d=%d katman=%d sözlük=%d.",
            (int)s->model.d, (int)s->model.katman, (int)s->model.sozluk);
    return UM_OLDU;
}


/* Eğitimdeki kalıp: "S: <soru>\nU: " -> yanıt satır sonuna dek.
 * KV önbellekli us1_adim ile her yeni im yalnız bir kez hesaplanır
 * (eskiden her harf için bütün dizi baştan hesaplanıyordu). */
static int32_t g_satir[US1_AZAMI_SOZLUK];

static int istem_doldur(us_yerel_t *s, const uint32_t *imler, uint32_t n) {
    us1_sifirla(&s->model);
    for (uint32_t j = 0; j < n; j++)
        if (us1_adim(&s->model, imler[j], g_satir) != US1_OLDU) return 0;
    return 1;
}

static int yerel_uret(umay_us_t *u, const us_istem_t *i, char *cikti, uint32_t n)
{
    us_yerel_t *s = (us_yerel_t *)u;
    if (!s->hazir || !i || !cikti || n < 2) return UM_YANLIS;

    static uint32_t imler[US1_AZAMI_BAGLAM];
    static uint32_t gecmis[URET_AZAMI + US1_AZAMI_BAGLAM];
    uint32_t T = 0, G = 0;
    const char *soru = (i->soru && *i->soru) ? i->soru : "Ötüken";

    /* istem: "S: " + sorunun SON kısmı (bağlamın yarısına sığacak) + "\nU: " */
    uint32_t sb = um_uzun(soru), sigar = s->model.baglam / 2;
    const char *bas = sb > sigar ? soru + (sb - sigar) : soru;
    /* kodlama: us1_kodla (tests/us1_conformance'ta Python'la eşliği sınanan
     * işlevin AYNISI) */
    const char *parca[3] = { "S: ", bas, "\nU: " };
    for (int p = 0; p < 3 && T < s->model.baglam - 1; p++)
        T += us1_kodla(s->sozluk, s->sozluk_boy, s->model.sozluk, parca[p],
                       imler + T, s->model.baglam - 1 - T);
    if (T < 4) return UM_YANLIS;
    for (uint32_t j = 0; j < T; j++) gecmis[G++] = imler[j];
    if (!istem_doldur(s, imler, T)) return UM_YANLIS;

    uint32_t yazilan = 0;
    for (uint32_t adim = 0; adim < URET_AZAMI && yazilan + 1 < n; adim++) {
        /* açgözlü seçim: küçük, ezberli model için en sadık yol */
        uint32_t im = us1_sec(g_satir, s->model.sozluk, 0);
        char c = s->sozluk[im];
        if (c == '\n') break;
        /* cümle sonu: küçük model doğru cümleyi söyledikten sonra belgenin
         * SONRAKİ cümlesine kayabiliyor; '.' + boşlukta durulur. */
        if (c == ' ' && yazilan >= 8 && cikti[yazilan - 1] == '.') break;
        cikti[yazilan++] = c;
        gecmis[G++] = im;
        int r = us1_adim(&s->model, im, g_satir);
        if (r == US1_DOLU) {
            /* bağlam doldu: son yarısıyla yeniden doldur */
            uint32_t k = s->model.baglam / 2;
            if (!istem_doldur(s, &gecmis[G - k], k)) break;
        } else if (r != US1_OLDU) break;
    }
    /* Bayt düzeyindeki model GEÇERSİZ UTF-8 de üretebilir (yalnız sonda
     * değil, ortada da). Ekrana ve IPC'ye bozuk metin gitmez. */
    cikti[yazilan] = 0;
    yazilan = um_utf8_temizle(cikti, yazilan);
    return yazilan ? UM_OLDU : UM_YANLIS;
}

static void yerel_kapat(umay_us_t *u) { ((us_yerel_t *)u)->hazir = 0; }

umay_us_t *us_yerel_motor(void)
{
    g_yerel.taban.ad      = "us1-yerel";
    g_yerel.taban.yetenek = US_YETENEK_URETKEN;
    g_yerel.taban.hazirla = yerel_hazirla;
    g_yerel.taban.uret    = yerel_uret;
    g_yerel.taban.kapat   = yerel_kapat;
    g_yerel.hazir = 0;
    return &g_yerel.taban;
}
