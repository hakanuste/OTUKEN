/* US stub motoru — gerçek model gelene dek anlamlı yanıt üretir.
 * Bağlam alıntılarını özetleyip OZAN kalıbına döker. Sözleşme
 * (umay_us_t) gerçek motorla birebir aynıdır; takas tek satırdır. */
#include "umay_us.h"
#include "umay_str.h"
#include "umay_port.h"

static int stub_hazirla(umay_us_t *u, const char *model_yolu) {
    (void)u;
    if (model_yolu && up_var_mi(model_yolu)) return UM_OLDU;   /* ileride yüklenir */
    return UM_OLDU;                                            /* model şart değil */
}

/* Alıntıdan ilk iki tam cümleyi çekip kendi çerçevemize koyar. */
static uint32_t cumle_al(const char *kaynak, int adet, char *c, uint32_t n) {
    uint32_t o = 0; int bulunan = 0;
    const char *p = kaynak;
    while (*p == ' ' || *p == '\n') p++;
    for (; *p && o + 1 < n; p++) {
        c[o++] = *p;
        if (*p == '.' || *p == '!' || *p == '?') {
            /* "3.2" gibi ondalıkta ya da kısaltmada cümle bitmez */
            if (*p == '.' && p[1] >= '0' && p[1] <= '9') continue;
            if (*p == '.' && p > kaynak && p[-1] >= '0' && p[-1] <= '9' && p[1] == ' '
                && p[2] >= '0' && p[2] <= '9') continue;
            if (++bulunan >= adet) break;
        }
    }
    c[o] = 0;
    return o;
}

static int stub_uret(umay_us_t *u, const us_istem_t *i, char *cikti, uint32_t n) {
    (void)u;
    um_kopya(cikti, n, "");
    if (!i) return UM_YANLIS;

    if (i->baglam && *i->baglam) {
        char oz[512];
        cumle_al(i->baglam, 2, oz, sizeof oz);
        um_ekle(cikti, n, oz);
        um_ekle(cikti, n, "\n");
    } else {
        um_ekle(cikti, n,
            "Bu konuda kütüphanemde yeterli bilgi bulamadım. "
            "Bildiğimi söylerim, bilmediğimi uydurmam.\n");
    }
    return UM_OLDU;
}

static void stub_kapat(umay_us_t *u) { (void)u; }

static umay_us_t g_stub = {
    .ad = "stub-ozan",
    .yetenek = 0,
    .hazirla = stub_hazirla,
    .uret    = stub_uret,
    .kapat   = stub_kapat,
    .ic      = 0
};

umay_us_t *us_stub_motor(void) { return &g_stub; }
