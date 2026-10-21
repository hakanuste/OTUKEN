/* =====================================================================
 * UMAY — PIPER ses arka ucu (yalnız ana bilgisayar yapımı)
 *
 * NE YAPAR: UMAY'ın yanıtını Piper ile seslendirir. Piper, sinir ağı
 * tabanlı (VITS) bir konuşma sentezleyicisidir; CPU'da gerçek zamanlı
 * koşar, çevrim dışıdır ve Türkçe sesleri vardır.
 *
 * NEDEN AYRI DOSYA: ses, UMAY'ın çekirdeğine gömülmez. Burası PORT
 * katmanının ana bilgisayar tarafıdır; ÖTÜKEN yapımına hiç girmez
 * (ÖTÜKEN yapımına hiç girmez).
 *
 * NEDEN PIPER: ÖTÜKEN'in kendi sentezleyicisi (cekirdek/src/dede.c)
 * formant sentezidir. Ölçülebilir her niteliği hedefe oturtuldu —
 * perde, formantlar, tayf dengesi, ünsüz ayrımı, hece vurgusu — ama
 * sözcükler yine net değil. Formant sentezinin tavanı budur: belirli
 * bir insanın sesini taşıyamaz ve doğal konuşmanın ince yapısını
 * kurallarla yeniden üretmek yılların işidir. Piper o yapıyı binlerce
 * saat konuşmadan öğrenmiştir.
 *
 * SAHTE BAŞARI YOK: Piper ya da ses dosyası ya da çalıcı yoksa bu
 * arka uç AÇILMAZ ve -1 döner. UMAY o zaman yalnız yazıyla yanıtlar.
 * Metni günlüğe basıp "konuştum" demek yankıdan başka bir şey değildir.
 *
 * KURULUM: ses/piper_kur.sh
 * AYARLAR (ortam değişkenleri):
 *   UMAY_PIPER        piper çalıştırılabiliri (varsayılan: yoldaki "piper")
 *   UMAY_PIPER_SES    .onnx ses modeli
 *   UMAY_SES_OYNAT    çalıcı komutu (varsayılan: bulunan ilki)
 * ===================================================================== */

/* popen/pclose POSIX'tir; -std=c99 ile gizlenir. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "umay_piper.h"

#define YOL_AZAMI 512

static int  g_bakildi;          /* arama bir kez yapılır */
static int  g_hazir;
static char g_piper[YOL_AZAMI];
static char g_ses[YOL_AZAMI];
static char g_oynat[YOL_AZAMI];

static int dosya_var(const char *y)
{
    struct stat s;
    return y && *y && stat(y, &s) == 0 && S_ISREG(s.st_mode);
}

/* Çalıştırılabilir PATH içinde var mı? */
static int komut_var(const char *ad)
{
    const char *yol = getenv("PATH");
    if (!yol) return 0;
    char tampon[YOL_AZAMI];
    const char *p = yol;
    while (*p) {
        const char *iki = strchr(p, ':');
        size_t n = iki ? (size_t)(iki - p) : strlen(p);
        if (n > 0 && n + strlen(ad) + 2 < sizeof tampon) {
            memcpy(tampon, p, n);
            tampon[n] = '/';
            strcpy(tampon + n + 1, ad);
            if (access(tampon, X_OK) == 0) return 1;
        }
        if (!iki) break;
        p = iki + 1;
    }
    return 0;
}

static void varsayilan_ses_yolu(char *c, size_t n)
{
    const char *ev = getenv("HOME");
    if (!ev) ev = ".";
    snprintf(c, n, "%s/.local/share/umay/ses/tr_TR-fahrettin-medium.onnx", ev);
}

static void ara(void)
{
    if (g_bakildi) return;
    g_bakildi = 1;
    g_hazir = 0;

    /* 1) piper */
    const char *p = getenv("UMAY_PIPER");
    if (p && *p) {
        if (!dosya_var(p) && !komut_var(p)) return;
        snprintf(g_piper, sizeof g_piper, "%s", p);
    } else {
        if (!komut_var("piper")) return;
        snprintf(g_piper, sizeof g_piper, "piper");
    }

    /* 2) ses modeli */
    const char *m = getenv("UMAY_PIPER_SES");
    if (m && *m) snprintf(g_ses, sizeof g_ses, "%s", m);
    else varsayilan_ses_yolu(g_ses, sizeof g_ses);
    if (!dosya_var(g_ses)) return;

    /* 3) çalıcı */
    const char *o = getenv("UMAY_SES_OYNAT");
    if (o && *o) {
        /* Elle verilen çalıcı DA doğrulanır. Doğrulanmadığında sürücü
         * takılıyor, "dede-korkut takıldı" yazıyor ama hiçbir zaman
         * ses çıkmıyordu: sahte başarının ta kendisi. Sınamada
         * yakalandı. Komutun ilk sözcüğüne bakılır (gerisi bayrak). */
        char ilk[YOL_AZAMI];
        size_t n = 0;
        while (o[n] && o[n] != ' ' && n + 1 < sizeof ilk) { ilk[n] = o[n]; n++; }
        ilk[n] = 0;
        if (!dosya_var(ilk) && !komut_var(ilk)) return;
        snprintf(g_oynat, sizeof g_oynat, "%s", o);
    } else {
        static const char *adaylar[] = { "paplay", "aplay", "afplay",
                                         "play", "ffplay", 0 };
        int i;
        for (i = 0; adaylar[i]; i++)
            if (komut_var(adaylar[i])) break;
        if (!adaylar[i]) return;
        if (strcmp(adaylar[i], "ffplay") == 0)
            snprintf(g_oynat, sizeof g_oynat, "ffplay -autoexit -nodisp -loglevel quiet");
        else
            snprintf(g_oynat, sizeof g_oynat, "%s", adaylar[i]);
    }

    g_hazir = 1;
}

int piper_var(void)
{
    ara();
    return g_hazir;
}

const char *piper_ses_adi(void)
{
    ara();
    return g_hazir ? g_ses : "yok";
}

int piper_konus(const char *metin)
{
    if (!metin || !*metin) return -1;
    ara();
    if (!g_hazir) return -1;

    char wav[YOL_AZAMI];
    snprintf(wav, sizeof wav, "/tmp/umay-dede-%d.wav", (int)getpid());

    char komut[YOL_AZAMI * 4];
    snprintf(komut, sizeof komut,
             "%s --model '%s' --output_file '%s' >/dev/null 2>&1",
             g_piper, g_ses, wav);

    /* Metin BORUDAN gider, komut satırından değil: tırnak, kesme
     * işareti ve kabuk için anlamlı karakterler sorun çıkarmaz. */
    FILE *f = popen(komut, "w");
    if (!f) return -1;
    fputs(metin, f);
    fputc('\n', f);
    int d = pclose(f);
    if (d != 0 || !dosya_var(wav)) { unlink(wav); return -1; }

    snprintf(komut, sizeof komut, "%s '%s' >/dev/null 2>&1", g_oynat, wav);
    d = system(komut);
    unlink(wav);
    return (d == 0) ? 0 : -1;
}

