/* KANAL — klavye ve IPC taşıyıcıları. Hepsi aynı umay_ileti_t üretir. */
#include "umay_kanal.h"
#include "umay_str.h"
#include "umay_ortak.h"

/* ---------------- klavye (konsol / shell borusu) ---------------- */
typedef struct { up_tut_t t; char tampon[UMAY_ILETI_MAX]; uint32_t o; } klavye_ic_t;
static klavye_ic_t g_klavye_ic;

static int klavye_ac(umay_kanal_t *k) {
    g_klavye_ic.t = up_ac("/aygit/konsol", UP_OKU);
    g_klavye_ic.o = 0;
    k->ic = &g_klavye_ic;
    k->acik = 1;
    return UM_OLDU;
}

static int klavye_yokla(umay_kanal_t *k, umay_ileti_t *i) {
    klavye_ic_t *ic = (klavye_ic_t*)k->ic;
    if (ic->t < 0) return 0;
    char c;
    while (up_oku(ic->t, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (ic->o == 0) continue;
            ic->tampon[ic->o] = 0;
            i->tur = KANAL_KLAVYE;
            i->damga = up_ms();
            i->kaynak = 0;
            i->uzunluk = um_kopya(i->govde, sizeof i->govde, ic->tampon);
            ic->o = 0;
            return 1;
        }
        if (ic->o + 1 < sizeof ic->tampon) ic->tampon[ic->o++] = c;
    }
    return 0;
}

static int klavye_yaz(umay_kanal_t *k, const char *metin) {
    (void)k;
    up_gunluk(metin);
    return UM_OLDU;
}

static void klavye_kapat(umay_kanal_t *k) {
    klavye_ic_t *ic = (klavye_ic_t*)k->ic;
    if (ic && ic->t >= 0) up_kapat(ic->t);
    k->acik = 0;
}

static umay_kanal_t g_klavye = {
    .ad = "klavye", .tur = KANAL_KLAVYE,
    .ac = klavye_ac, .yokla = klavye_yokla, .yaz = klavye_yaz, .kapat = klavye_kapat
};
umay_kanal_t *kanal_klavye(void) { return &g_klavye; }

/* ---------------- IPC (başka Ring3 süreçler) ----------------
 * V9.2'deki HTTP rotalarının yerini bu port alır. Bir süreç
 * "/kapi/umay" portuna düz metin buyruk yollar, yanıtı geri alır. */
typedef struct { up_tut_t t; char ad[UMAY_AD_MAX]; } ipc_ic_t;
static ipc_ic_t g_ipc_ic;

static int ipc_ac(umay_kanal_t *k) {
    ipc_ic_t *ic = (ipc_ic_t*)k->ic;
    ic->t = up_kapi_ac(ic->ad);
    k->acik = (ic->t >= 0);
    if (!k->acik) gun_yaz("IPC kapısı açılamadı: %s", ic->ad);
    else gun_yaz("IPC kapısı açık: %s", ic->ad);
    return k->acik ? UM_OLDU : UM_YANLIS;
}

static int ipc_yokla(umay_kanal_t *k, umay_ileti_t *i) {
    ipc_ic_t *ic = (ipc_ic_t*)k->ic;
    if (ic->t < 0) return 0;
    int n = up_al(ic->t, i->govde, sizeof i->govde - 1, 0);
    if (n <= 0) return 0;
    i->govde[n] = 0;
    i->tur = KANAL_IPC;
    i->damga = up_ms();
    i->kaynak = (uint32_t)ic->t;
    i->uzunluk = (uint32_t)n;
    return 1;
}

static int ipc_yaz(umay_kanal_t *k, const char *metin) {
    ipc_ic_t *ic = (ipc_ic_t*)k->ic;
    if (ic->t < 0) return UM_YANLIS;
    return up_gonder(ic->t, metin, um_uzun(metin));
}

static void ipc_kapat(umay_kanal_t *k) {
    ipc_ic_t *ic = (ipc_ic_t*)k->ic;
    if (ic->t >= 0) up_kapat(ic->t);
    k->acik = 0;
}

static umay_kanal_t g_ipc = {
    .ad = "ipc", .tur = KANAL_IPC,
    .ac = ipc_ac, .yokla = ipc_yokla, .yaz = ipc_yaz, .kapat = ipc_kapat,
    .ic = &g_ipc_ic
};

umay_kanal_t *kanal_ipc(const char *port_adi) {
    um_kopya(g_ipc_ic.ad, sizeof g_ipc_ic.ad, port_adi ? port_adi : "/kapi/umay");
    g_ipc_ic.t = UP_GECERSIZ;
    return &g_ipc;
}

/* ---------------- SES (bugün boş, yüzey hazır) ---------------- */
#include "umay_ses.h"
static int ses_kanal_ac(umay_kanal_t *k) { k->acik = ses_var_mi(); return k->acik ? UM_OLDU : UM_DESTEKSIZ; }
static int ses_kanal_yokla(umay_kanal_t *k, umay_ileti_t *i) {
    if (!k->acik) return 0;
    char metin[256];
    if (ses_dinle(metin, sizeof metin) <= 0) return 0;
    i->tur = KANAL_SES; i->damga = up_ms(); i->kaynak = 0;
    i->uzunluk = um_kopya(i->govde, sizeof i->govde, metin);
    return 1;
}
static int ses_kanal_yaz(umay_kanal_t *k, const char *metin) { (void)k; return ses_soyle(metin); }
static void ses_kanal_kapat(umay_kanal_t *k) { k->acik = 0; }
static umay_kanal_t g_ses = {
    .ad = "ses", .tur = KANAL_SES,
    .ac = ses_kanal_ac, .yokla = ses_kanal_yokla, .yaz = ses_kanal_yaz, .kapat = ses_kanal_kapat
};
umay_kanal_t *kanal_ses(void) { return &g_ses; }
