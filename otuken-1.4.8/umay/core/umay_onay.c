/* ONAY — yıkıcı iş için kullanıcı onayı. V9.2 onay_bekleyenler mantığı. */
#include "umay_onay.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"

static onay_t   g_kayit[ONAY_MAX];
static uint32_t g_omur = 600000;
static uint32_t g_sayac;

void onay_kur(uint32_t omur_ms) {
    um_bellek_doldur(g_kayit, 0, sizeof g_kayit);
    g_omur = omur_ms ? omur_ms : 600000;
    g_sayac = 0;
}

const char *onay_iste(const char *tur, const char *baslik, const char *ozet, const char *is) {
    onay_temizle();
    int i;
    for (i = 0; i < ONAY_MAX && g_kayit[i].dolu; i++) {}
    if (i >= ONAY_MAX) return 0;
    onay_t *k = &g_kayit[i];
    um_bicim(k->kimlik, sizeof k->kimlik, "onay%u", ++g_sayac);
    um_kopya(k->tur,    sizeof k->tur,    tur);
    um_kopya(k->baslik, sizeof k->baslik, baslik);
    um_kopya(k->ozet,   sizeof k->ozet,   ozet);
    um_kopya(k->is,     sizeof k->is,     is ? is : "");
    k->olustu = up_ms();
    k->son    = k->olustu + g_omur;
    k->dolu   = 1;
    gun_yaz("Onay istendi: %s (%s)", k->kimlik, k->baslik);
    return k->kimlik;
}

onay_t *onay_bul(const char *kimlik) {
    uint64_t simdi = up_ms();
    for (int i = 0; i < ONAY_MAX; i++)
        if (g_kayit[i].dolu && um_esit(g_kayit[i].kimlik, kimlik) && g_kayit[i].son > simdi)
            return &g_kayit[i];
    return 0;
}

int onay_ver(const char *kimlik) {
    onay_t *k = onay_bul(kimlik);
    if (!k) return 0;
    gun_yaz("Onay verildi: %s", k->kimlik);
    k->dolu = 0;
    return 1;
}

int onay_reddet(const char *kimlik) {
    onay_t *k = onay_bul(kimlik);
    if (!k) return 0;
    gun_yaz("Onay geri çevrildi: %s", k->kimlik);
    k->dolu = 0;
    return 1;
}

void onay_temizle(void) {
    uint64_t simdi = up_ms();
    for (int i = 0; i < ONAY_MAX; i++)
        if (g_kayit[i].dolu && g_kayit[i].son <= simdi) g_kayit[i].dolu = 0;
}

int onay_bekleyen_sayi(void) {
    onay_temizle();
    int n = 0;
    for (int i = 0; i < ONAY_MAX; i++) if (g_kayit[i].dolu) n++;
    return n;
}

onay_t *onay_sirali(int i) {
    int n = 0;
    for (int j = 0; j < ONAY_MAX; j++)
        if (g_kayit[j].dolu && n++ == i) return &g_kayit[j];
    return 0;
}
