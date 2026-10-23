/* =====================================================================
 * 1.4.9 — ag_kilit ve Ring3 ag tutamaklari (bkz. include/ag.h)
 *
 * Ring3 programi TCP yuvasina dogrudan erismez: gorev kimligine bagli bir
 * TUTAMAK alir. Baska gorevin tutamagi kullanilamaz; gorev olunce reap
 * tutamaklari birakir (baglanti duzgun kapanir, yuva geri doner).
 * Bloklayan cagrilar (coz, baglan) kilidi TUTMADAN uyur; bekledikleri
 * sirada paketleri kendileri de yoklar (ana donguye bagimli kalmasinlar).
 * ===================================================================== */
#include "ag.h"
#include "ulak.h"
#include "tcp.h"
#include "dns.h"
#include "dhcp.h"
#include "timer.h"
#include "thread.h"
#include "serial.h"

ozspin_t ag_kilit = OZSPIN_BASLANGIC;

typedef struct {
    int      kullanimda;
    uint32_t sahip;
    int      tcp;
} tutamak_t;

static tutamak_t tut[AG_TUTAMAK_MAX];
static uint32_t baglanti_toplam;     /* basarili Ring3 baglantilari (sinama) */          /* DNS tek soru tasir: sorular sirayla */

static uint32_t hz(void) { uint32_t h = timer_hz(); return h ? h : 100; }

void ag_yokla(void) {
    ozspin_al(&ag_kilit);
    ulak_poll();
    tcp_zamanlayici();
    ozspin_birak(&ag_kilit);
}

static void bekle(void) {
    ag_yokla();
    thread_sleep(1);
}

/* "a.b.c.d" mi? */
static int noktali_ip(const char *s, uint8_t ip[4]) {
    int parca = 0;
    uint32_t deger = 0;
    int basamak = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            deger = deger * 10 + (uint32_t)(*p - '0');
            if (++basamak > 3 || deger > 255) return 0;
        } else if (*p == '.' || *p == 0) {
            if (!basamak || parca > 3) return 0;
            ip[parca++] = (uint8_t)deger;
            deger = 0; basamak = 0;
            if (*p == 0) break;
        } else return 0;
    }
    return parca == 4;
}

int ag_coz(const char *ad, uint8_t ip[4]) {
    if (noktali_ip(ad, ip)) return 0;
    const dhcp_config_t *dc = dhcp_get_config();
    if (!dc || !dc->configured) return -1;
    int sonuc = -1;
    for (int deneme = 0; deneme < 3 && sonuc != 0; deneme++) {
        int h = dns_sor(ad, dc->dns);
        if (h < 0) { thread_sleep(5); continue; }       /* yuvalar dolu: az bekle */
        uint64_t bitis = timer_get_ticks() + (uint64_t)(deneme + 1) * hz();
        int d = 0;
        while (timer_get_ticks() < bitis) {
            bekle();
            d = dns_sonuc(h, ip);
            if (d != 0) break;
        }
        dns_birak(h);
        if (d == 1) sonuc = 0;
        if (d == -1) break;                               /* "yok" yanitı kesindir */
    }
    return sonuc;
}

static tutamak_t *tutamak(uint32_t sahip, int t) {
    if (t < 0 || t >= AG_TUTAMAK_MAX) return 0;
    tutamak_t *x = &tut[t];
    return (x->kullanimda && x->sahip == sahip) ? x : 0;
}

int ag_baglan(uint32_t sahip, const uint8_t ip[4], uint16_t port) {
    if (port == 0) return -1;
    int t = -1, sayi = 0;
    ozspin_al(&ag_kilit);
    for (int i = 0; i < AG_TUTAMAK_MAX; i++) {
        if (tut[i].kullanimda && tut[i].sahip == sahip) sayi++;
        else if (!tut[i].kullanimda && t < 0) t = i;
    }
    if (t >= 0 && sayi < AG_GOREV_AZAMI) {
        tut[t].kullanimda = 1;
        tut[t].sahip = sahip;
        tut[t].tcp = -1;
    } else t = -1;
    ozspin_birak(&ag_kilit);
    if (t < 0) return -1;

    int c = tcp_ac(ip, port);
    if (c < 0) {
        ozspin_al(&ag_kilit);
        tut[t].kullanimda = 0;
        ozspin_birak(&ag_kilit);
        return -1;
    }
    tut[t].tcp = c;
    uint64_t bitis = timer_get_ticks() + 8 * hz();
    for (;;) {
        tcp_state_t d = tcp_durum(c);
        if (d == TCP_KURULDU || d == TCP_KARSI_KAPADI) {
            __atomic_add_fetch(&baglanti_toplam, 1, __ATOMIC_RELAXED);
            return t;
        }
        if (d != TCP_SYN_GONDERDI || timer_get_ticks() >= bitis) break;
        bekle();
    }
    ozspin_al(&ag_kilit);
    tcp_birak(c);
    tut[t].kullanimda = 0;
    ozspin_birak(&ag_kilit);
    return -1;
}

int ag_gonder(uint32_t sahip, int t, const uint8_t *veri, uint32_t n) {
    ozspin_al(&ag_kilit);
    tutamak_t *x = tutamak(sahip, t);
    int r = x ? tcp_yaz(x->tcp, veri, n) : -1;
    ozspin_birak(&ag_kilit);
    return r;
}

int ag_al(uint32_t sahip, int t, uint8_t *tampon, uint32_t azami) {
    ag_yokla();
    ozspin_al(&ag_kilit);
    tutamak_t *x = tutamak(sahip, t);
    int r = x ? tcp_oku(x->tcp, tampon, azami) : -1;
    ozspin_birak(&ag_kilit);
    return r;
}

int ag_kapat(uint32_t sahip, int t) {
    ozspin_al(&ag_kilit);
    tutamak_t *x = tutamak(sahip, t);
    int r = -1;
    if (x) {
        tcp_birak(x->tcp);
        x->kullanimda = 0;
        r = 0;
    }
    ozspin_birak(&ag_kilit);
    return r;
}

int ag_gorev_bitti(uint32_t sahip) {
    int n = 0;
    ozspin_al(&ag_kilit);
    for (int i = 0; i < AG_TUTAMAK_MAX; i++) {
        if (tut[i].kullanimda && tut[i].sahip == sahip) {
            if (tut[i].tcp >= 0) tcp_birak(tut[i].tcp);
            tut[i].kullanimda = 0;
            n++;
        }
    }
    ozspin_birak(&ag_kilit);
    if (n) {
        serial_write("AG: olen gorevin baglantilari birakildi, adet=");
        serial_write_hex((uint32_t)n);
        serial_write("\n");
    }
    return n;
}

int ag_tutamak_sayisi(void) {
    int n = 0;
    ozspin_al(&ag_kilit);
    for (int i = 0; i < AG_TUTAMAK_MAX; i++) if (tut[i].kullanimda) n++;
    ozspin_birak(&ag_kilit);
    return n;
}

void ag_bilgi(ag_bilgi_t *b) {
    uint8_t *p = (uint8_t *)b;
    for (uint32_t i = 0; i < sizeof(*b); i++) p[i] = 0;
    ozspin_al(&ag_kilit);
    const ulak_state_t *st = ulak_get_state();
    const dhcp_config_t *dc = dhcp_get_config();
    for (int i = 0; i < 4; i++) b->ip[i] = st->ip[i];
    for (int i = 0; i < 6; i++) b->mac[i] = st->mac[i];
    if (dc && dc->configured) {
        for (int i = 0; i < 4; i++) {
            b->maske[i] = dc->netmask[i];
            b->gecit[i] = dc->gateway[i];
            b->dns[i] = dc->dns[i];
        }
        b->yapilandi = 1;
    }
    b->bagli = (uint8_t)st->link_up;
    b->rx_paket = st->rx_packets;
    b->tx_paket = st->tx_packets;
    b->rx_tcp = st->rx_tcp;
    ozspin_birak(&ag_kilit);
    b->acik_tcp = (uint32_t)tcp_acik_sayisi();
}

uint32_t ag_baglanti_toplam(void) { return __atomic_load_n(&baglanti_toplam, __ATOMIC_RELAXED); }
