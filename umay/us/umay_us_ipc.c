/* =====================================================================
 * US MOTORU — IPC ARKA UCU
 *
 * UMAY'ın çıkarım motoru artık DEĞİŞTİRİLEBİLİR bir arka uçtur.
 * Bu dosya, motorun ÖTÜKEN'in IPC katmanı üzerinden konuşan
 * gerçekleştirimidir: UMAY istemi bir servise yollar, yanıtı bekler.
 *
 * NEDEN IPC: model çekirdek içinde koşamaz. Ring3'te ayrı bir süreç
 * (ya da ileride ağ üzerinden bir uç) modeli taşır; UMAY yalnızca
 * istem gönderip yanıt alır. Böylece model değiştirmek için UMAY'ı
 * yeniden derlemek gerekmez.
 *
 * DÜRÜSTLÜK KURALI: servis yoksa, yanıt vermezse ya da yanıt bozuksa
 * bu motor BAŞARISIZ döner. Sahte başarı üretmez, uydurma cümle
 * kurmaz. US katmanı o zaman stub'a düşer ve kullanıcıya yedeğe
 * düşüldüğü açıkça bildirilir.
 *
 * İLETİŞİM DÜZENİ (metin, satır tabanlı):
 *   istek : "SOR\n<kişilik>\n<bağlam>\n<soru>\n"
 *   yanıt : "YANIT\n<metin>\n"  ya da  "YOK\n"
 * IPC yükü 96 bayt olduğundan uzun iletiler parçalanır; port katmanı
 * (up_gonder/up_al) bunu kendi halleder.
 * ===================================================================== */
#include "umay_us.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"

#define IPC_KAPI      "us"       /* servisin açtığı port adı */
#define BEKLE_MS      4000       /* yanıt için üst sınır     */
#define ISTEK_AZAMI   3072
#define YANIT_AZAMI   2048

typedef struct {
    umay_us_t taban;
    up_tut_t  kapi;
    int       bagli;
} us_ipc_t;

static us_ipc_t g_ipc;

static int ipc_hazirla(umay_us_t *u, const char *model_yolu)
{
    us_ipc_t *s = (us_ipc_t *)u;
    (void)model_yolu;

    s->kapi = up_kapi_bagla(IPC_KAPI);
    if (s->kapi == UP_GECERSIZ || (int64_t)s->kapi < 0) {
        s->bagli = 0;
        gun_yaz("US-IPC: '%s' kapısı yok — motor bağlanamadı.", IPC_KAPI);
        return UM_YANLIS;          /* sahte başarı YOK */
    }
    s->bagli = 1;
    gun_yaz("US-IPC: '%s' kapısına bağlanıldı.", IPC_KAPI);
    return UM_OLDU;
}

static int ipc_uret(umay_us_t *u, const us_istem_t *i, char *cikti, uint32_t n)
{
    us_ipc_t *s = (us_ipc_t *)u;
    if (!s->bagli || !i || !cikti || n == 0) return UM_YANLIS;

    static char istek[ISTEK_AZAMI];
    um_bicim(istek, sizeof istek, "SOR\n%s\n%s\n%s\n",
             i->kisilik ? i->kisilik : "",
             i->baglam  ? i->baglam  : "",
             i->soru    ? i->soru    : "");

    if (up_gonder(s->kapi, istek, (uint32_t)um_uzun(istek)) < 0) {
        gun_yaz("US-IPC: istem gönderilemedi.");
        return UM_YANLIS;
    }

    static char yanit[YANIT_AZAMI];
    int alinan = up_al(s->kapi, yanit, sizeof yanit - 1, BEKLE_MS);
    if (alinan <= 0) {
        gun_yaz("US-IPC: yanıt gelmedi (%d ms bekledim).", BEKLE_MS);
        return UM_YANLIS;
    }
    yanit[alinan] = 0;

    /* Yanıt biçimi denetlenir; bozuksa BAŞARISIZ sayılır. */
    if (yanit[0]=='Y' && yanit[1]=='O' && yanit[2]=='K') return UM_YANLIS;
    if (!(yanit[0]=='Y' && yanit[1]=='A' && yanit[2]=='N' && yanit[3]=='I' &&
          yanit[4]=='T' && yanit[5]=='\n')) {
        gun_yaz("US-IPC: yanıt biçimi tanınmadı, yok sayıldı.");
        return UM_YANLIS;
    }
    const char *govde = yanit + 6;
    if (*govde == 0) return UM_YANLIS;

    um_kopya(cikti, n, govde);
    return UM_OLDU;
}

static void ipc_kapat(umay_us_t *u)
{
    us_ipc_t *s = (us_ipc_t *)u;
    if (s->bagli) {
        up_kapat(s->kapi);
        s->bagli = 0;
    }
}

umay_us_t *us_ipc_motor(void)
{
    g_ipc.taban.ad       = "ipc-us";
    g_ipc.taban.yetenek  = US_YETENEK_URETKEN;
    g_ipc.taban.hazirla  = ipc_hazirla;
    g_ipc.taban.uret     = ipc_uret;
    g_ipc.taban.kapat    = ipc_kapat;
    g_ipc.bagli          = 0;
    return &g_ipc.taban;
}
