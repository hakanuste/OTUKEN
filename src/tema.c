#include "tema.h"
#include "tamga.h"
#include "serial.h"

/* --- Gömülü temalar. İlki varsayılandır. --- */
static const tema_t TEMALAR[] = {
{   /* Tamga sisteminin kendi paleti: derin okyanus + turkuaz + krem */
    .ad = "otuken",
    .masaustu      = TEMA_OKYANUS,
    .masaustu_alt  = 0x061319u,
    .doku          = 0x123A44u,
    .gorev_cubugu  = 0x07161Bu,
    .pencere_zemin = 0x0E2A35u,
    .baslik        = 0x103A46u,
    .baslik_odakli = TEMA_TURKUAZ,
    .kenar         = 0x1D3F4Bu,
    .gorev_yazi    = TEMA_KREM,
    .pencere_yazi  = TEMA_KREM,
    .baslik_yazi   = TEMA_BEYAZ,
    .dugme         = 0x0E3038u,
    .dugme_etkin   = TEMA_TURKUAZ,
    .kapat         = TEMA_MERCAN,
    .imlec         = TEMA_BEYAZ,
    .uyari         = TEMA_MERCAN,
    .vurgu         = TEMA_TURKUAZ,
    .umay          = TEMA_LAVANTA,
    .metalik       = TEMA_METALIK,
    .altin         = TEMA_ALTIN,
    .krem_vurgu    = TEMA_KREM,
    .beyaz_yerine_krem = TEMA_BEYAZ,
    .baslik_yuksekligi = 20,
    .kenar_kalinligi   = 1
},
{   /* Gece: daha koyu, gözü yormayan; uzun oturumlar için */
    .ad = "gece",
    .masaustu      = 0x050D11u,
    .masaustu_alt  = 0x02070Au,
    .doku          = 0x0C2129u,
    .gorev_cubugu  = 0x02080Bu,
    .pencere_zemin = 0x0A171Cu,
    .baslik        = 0x0C2027u,
    .baslik_odakli = 0x2E7A73u,
    .kenar         = 0x143038u,
    .gorev_yazi    = 0xC9BFA8u,
    .pencere_yazi  = 0xC9BFA8u,
    .baslik_yazi   = TEMA_KREM,
    .dugme         = 0x0A1F26u,
    .dugme_etkin   = 0x2E7A73u,
    .kapat         = 0xB35A48u,
    .imlec         = TEMA_KREM,
    .uyari         = 0xB35A48u,
    .vurgu         = 0x2E7A73u,
    .umay          = 0x7C5FA8u,
    .metalik       = 0xB39B6Fu,
    .altin         = 0xC9A45Au,
    .krem_vurgu    = 0xC9BFA8u,
    .beyaz_yerine_krem = 0xD8CDB4u,
    .baslik_yuksekligi = 20,
    .kenar_kalinligi   = 1
},
{   /* Bozkır: krem zemin, koyu yazı; gündüz ve yüksek karşıtlık */
    .ad = "bozkir",
    .masaustu      = 0x1D3F4Bu,
    .masaustu_alt  = 0x0E2731u,
    .doku          = 0x2A5A66u,
    .gorev_cubugu  = 0x123039u,
    .pencere_zemin = TEMA_KREM,
    .baslik        = 0x2E5B4Fu,
    .baslik_odakli = TEMA_TURKUAZ,
    .kenar         = 0x0D1F27u,
    .gorev_yazi    = TEMA_KREM,
    .pencere_yazi  = 0x14232Au,
    .baslik_yazi   = TEMA_BEYAZ,
    .dugme         = 0x1A3038u,
    .dugme_etkin   = 0x3E8B72u,
    .kapat         = 0xB33A3Au,
    .imlec         = TEMA_BEYAZ,
    .uyari         = 0xB33A3Au,
    .vurgu         = 0x3E8B72u,
    .umay          = 0x6E4FA0u,
    .metalik       = 0xC9A76Bu,
    .altin         = 0xE0B457u,
    .krem_vurgu    = TEMA_KREM,
    .beyaz_yerine_krem = TEMA_BEYAZ,
    .baslik_yuksekligi = 20,
    .kenar_kalinligi   = 1
}
};

#define TEMA_ADET ((int)(sizeof(TEMALAR) / sizeof(TEMALAR[0])))

static const tema_t *g_simdiki = &TEMALAR[0];

static int ad_esit(const char *a, const char *b) {
    while (*a && *b) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}

void tema_init(void) {
    g_simdiki = &TEMALAR[0];
    serial_write("tema: '");
    serial_write(g_simdiki->ad);
    serial_write("' yuklendi (tamga paleti)\n");
}

const tema_t *tema_simdiki(void) { return g_simdiki; }
int tema_sayisi(void) { return TEMA_ADET; }

const tema_t *tema_sirali(int i) {
    if (i < 0 || i >= TEMA_ADET) return 0;
    return &TEMALAR[i];
}

int tema_uygula(const char *ad) {
    for (int i = 0; i < TEMA_ADET; i++) {
        if (ad_esit(TEMALAR[i].ad, ad)) {
            g_simdiki = &TEMALAR[i];
            tamga_onbellek_bosalt();   /* renkler değişti */
            serial_write("tema: '");
            serial_write(ad);
            serial_write("' uygulandi\n");
            return 0;
        }
    }
    return -1;
}
