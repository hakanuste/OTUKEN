/* =====================================================================
 * ULAK-DHCP — istemci (RFC 2131)
 *
 * 1.4.9 (Codex incelemesi):
 *  - Yanit ETKIN ALISVERISLE eslesmeli: op BOOTREPLY, islem kimligi
 *    (her alisveriste yeni, tahmin edilemez xid), istemci donanim adresi
 *    (chaddr = bizim MAC) ve DURUM: OFFER yalniz DISCOVER sonrasi, ACK/NAK
 *    yalniz REQUEST sonrasi ve secilen sunucudan. Paylasilan agda baska
 *    makinenin yayin ACK'i yapilandirmayi degistiremez.
 *  - Kira suresi IZLENIR: T1'de (kira/2 ya da secenek 58) sunucuya tekil
 *    REQUEST (YENILIYOR), T2'de (7/8 ya da secenek 59) yayin REQUEST
 *    (YENIDEN BAGLIYOR); kira biterse adres BIRAKILIR (configured=0, IP
 *    0.0.0.0) ve bastan DISCOVER. NAK -> bastan DISCOVER. Yanitsiz
 *    DISCOVER/REQUEST 4, 8, 16, 32 sn arayla yinelenir.
 *  - dhcp_zamanlayici() ag_yokla'dan (ag_kilit altinda) cagrilir.
 * ===================================================================== */
#include "dhcp.h"
#include "ulak.h"
#include "log.h"
#include "serial.h"
#include "timer.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5
#define DHCPNAK      6

#define KIRA_SONSUZ  0xFFFFFFFFu

enum { D_BOS = 0, D_SECIYOR, D_ISTIYOR, D_BAGLI, D_YENILIYOR, D_YENIDEN_BAGLIYOR };

static dhcp_config_t cfg;
static int      durum = D_BOS;
static uint32_t xid = 0x4F54554B;
static uint8_t  offered_ip[4];
static uint8_t  server_ip[4];
static uint64_t gonderim_sn;       /* son DISCOVER/REQUEST */
static uint32_t yineleme_aralik;   /* sn */
static uint64_t kira_bas_sn;       /* ACK alindiginda */
static uint32_t t1_sn, t2_sn, kira_sn;

static uint64_t simdi_sn(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    return timer_get_ticks() / hz;
}

static void yeni_xid(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    xid = xid * 1103515245u + (lo ^ (hi << 13) ^ (lo >> 7)) + 12345u;
}

void dhcp_init(void) {
    cfg.configured = 0;
    durum = D_BOS;
    log_write(LOG_BILGI, "ULAK-DHCP", "DHCP istemcisi baslatildi");
}

static void ip_yaz(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) { serial_write_hex(ip[i]); if (i < 3) serial_write("."); }
}

/* Ortak DHCP paketi. ciaddr: YENILE/YENIDEN BAGLA'da bizim adres (0 = yok).
 * hedef: 0 = yayin. req_ip/srv_ip: SECME durumundaki REQUEST icin. */
static int dhcp_send(uint8_t type, const uint8_t *ciaddr, const uint8_t *req_ip,
                     const uint8_t *srv_ip, const uint8_t *hedef) {
    uint8_t pkt[300];
    for (int i = 0; i < 300; i++) pkt[i] = 0;

    const ulak_state_t *st = ulak_get_state();

    pkt[0] = 1;      /* op: BOOTREQUEST */
    pkt[1] = 1;      /* htype: Ethernet */
    pkt[2] = 6;      /* hlen */
    pkt[3] = 0;      /* hops */
    pkt[4] = (uint8_t)(xid >> 24); pkt[5] = (uint8_t)(xid >> 16);
    pkt[6] = (uint8_t)(xid >> 8);  pkt[7] = (uint8_t)xid;
    if (!ciaddr) pkt[10] = 0x80;  /* adres yokken yanit yayinla gelsin */
    else for (int i = 0; i < 4; i++) pkt[12 + i] = ciaddr[i];

    for (int i = 0; i < 6; i++) pkt[28 + i] = st->mac[i];   /* chaddr */

    /* Sihirli cerez (magic cookie): 99.130.83.99 */
    pkt[236] = 99; pkt[237] = 130; pkt[238] = 83; pkt[239] = 99;

    int p = 240;
    pkt[p++] = 53; pkt[p++] = 1; pkt[p++] = type;     /* mesaj turu */

    if (type == DHCPREQUEST && req_ip) {
        pkt[p++] = 50; pkt[p++] = 4;                   /* istenen IP */
        for (int i = 0; i < 4; i++) pkt[p++] = req_ip[i];
        if (srv_ip) {
            pkt[p++] = 54; pkt[p++] = 4;               /* sunucu kimligi */
            for (int i = 0; i < 4; i++) pkt[p++] = srv_ip[i];
        }
    }

    pkt[p++] = 55; pkt[p++] = 6;                       /* parametre istegi */
    pkt[p++] = 1;   /* ag maskesi */
    pkt[p++] = 3;   /* yonlendirici */
    pkt[p++] = 6;   /* DNS */
    pkt[p++] = 51;  /* kira suresi */
    pkt[p++] = 58;  /* T1 */
    pkt[p++] = 59;  /* T2 */

    pkt[p++] = 255;                                     /* son */

    uint8_t bcast[4] = {255, 255, 255, 255};
    gonderim_sn = simdi_sn();
    return ulak_send_udp(hedef ? hedef : bcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, pkt, (uint16_t)p);
}

static int kesif_baslat(void) {
    yeni_xid();
    durum = D_SECIYOR;
    yineleme_aralik = 4;
    serial_write("ULAK-DHCP: DISCOVER yayinlaniyor...\n");
    return dhcp_send(DHCPDISCOVER, 0, 0, 0, 0);
}

int dhcp_discover(void) { return kesif_baslat(); }

/* Secenek (option) alanindan bir degeri cikarir. */
static const uint8_t *find_option(const uint8_t *p, uint16_t len,
                                   uint8_t code, uint8_t *out_len) {
    /* Denetim (1.4.6): uint16_t sayac 65535 yakininda sarmalayabiliyordu;
     * secenegin degeri paket disina tasiyorsa da donduruluyordu. */
    uint32_t i = 240;
    while (i < len) {
        uint8_t c = p[i];
        if (c == 255) break;           /* son */
        if (c == 0) { i++; continue; } /* dolgu */
        if (i + 1 >= len) break;
        uint8_t l = p[i + 1];
        if (i + 2 + (uint32_t)l > len) break;   /* deger paketin disinda */
        if (c == code) { *out_len = l; return &p[i + 2]; }
        i += 2u + l;
    }
    return 0;
}

static uint32_t oku32(const uint8_t *v) {
    return ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
}

static int ayni4(const uint8_t *a, const uint8_t *b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static void adresi_birak(const char *neden) {
    static const uint8_t sifir[4] = { 0, 0, 0, 0 };
    cfg.configured = 0;
    for (int i = 0; i < 4; i++) cfg.ip[i] = 0;
    ulak_set_ip(sifir);
    serial_write("ULAK-DHCP: adres birakildi (");
    serial_write(neden);
    serial_write(") -> yeniden DISCOVER\n");
    log_write(LOG_UYARI, "ULAK-DHCP", "adres birakildi, yeniden kesif");
}

void dhcp_handle_packet(const uint8_t *payload, uint16_t len) {
    if (len < 240) return;
    if (payload[0] != 2) return;                               /* BOOTREPLY degil */
    if (payload[236] != 99 || payload[237] != 130 ||
        payload[238] != 83 || payload[239] != 99) return;   /* cerez yok */
    uint32_t px = oku32(&payload[4]);
    if (px != xid) return;                                     /* baska alisveris */
    const ulak_state_t *st = ulak_get_state();
    if (payload[2] != 6) return;
    for (int i = 0; i < 6; i++) if (payload[28 + i] != st->mac[i]) return;   /* baska istemci */

    uint8_t olen = 0;
    const uint8_t *mt = find_option(payload, len, 53, &olen);
    if (!mt || olen != 1) return;
    const uint8_t *sid = find_option(payload, len, 54, &olen);
    int sid_var = sid && olen == 4;

    if (mt[0] == DHCPOFFER) {
        if (durum != D_SECIYOR || !sid_var) return;           /* OFFER beklenmiyor */
        for (int i = 0; i < 4; i++) offered_ip[i] = payload[16 + i];   /* yiaddr */
        for (int i = 0; i < 4; i++) server_ip[i] = sid[i];

        serial_write("ULAK-DHCP: OFFER alindi, onerilen IP=");
        ip_yaz(offered_ip);
        serial_write(" -> REQUEST gonderiliyor\n");

        durum = D_ISTIYOR;
        yineleme_aralik = 4;
        dhcp_send(DHCPREQUEST, 0, offered_ip, server_ip, 0);
        return;
    }

    if (mt[0] != DHCPACK && mt[0] != DHCPNAK) return;
    if (durum != D_ISTIYOR && durum != D_YENILIYOR && durum != D_YENIDEN_BAGLIYOR) return;   /* ACK beklenmiyor */
    /* SECME/YENILEME: yanit secilen sunucudan; YENIDEN BAGLAMA: herhangi bir sunucu */
    if (durum != D_YENIDEN_BAGLIYOR && (!sid_var || !ayni4(sid, server_ip))) return;

    if (mt[0] == DHCPNAK) {
        serial_write("ULAK-DHCP: NAK alindi\n");
        if (cfg.configured) adresi_birak("NAK");
        kesif_baslat();
        return;
    }

    uint8_t yeni_ip[4];
    for (int i = 0; i < 4; i++) yeni_ip[i] = payload[16 + i];
    if (durum == D_ISTIYOR && !ayni4(yeni_ip, offered_ip)) return;   /* istedigimiz adres degil */
    if (durum != D_ISTIYOR && cfg.configured && !ayni4(yeni_ip, cfg.ip)) {
        /* yenilemede baska adres verildi: eskisini birak, yenisini al */
        adresi_birak("yenilemede farkli adres");
    }
    if (sid_var) for (int i = 0; i < 4; i++) server_ip[i] = sid[i];
    for (int i = 0; i < 4; i++) cfg.ip[i] = yeni_ip[i];

    const uint8_t *nm = find_option(payload, len, 1, &olen);
    if (nm && olen == 4) for (int i = 0; i < 4; i++) cfg.netmask[i] = nm[i];
    const uint8_t *gw = find_option(payload, len, 3, &olen);
    if (gw && olen >= 4) for (int i = 0; i < 4; i++) cfg.gateway[i] = gw[i];
    const uint8_t *dn = find_option(payload, len, 6, &olen);
    if (dn && olen >= 4) for (int i = 0; i < 4; i++) cfg.dns[i] = dn[i];
    const uint8_t *ls = find_option(payload, len, 51, &olen);
    cfg.lease_seconds = (ls && olen == 4) ? oku32(ls) : KIRA_SONSUZ;
    kira_sn = cfg.lease_seconds;
    const uint8_t *o58 = find_option(payload, len, 58, &olen);
    t1_sn = (o58 && olen == 4) ? oku32(o58) : kira_sn / 2;
    const uint8_t *o59 = find_option(payload, len, 59, &olen);
    t2_sn = (o59 && olen == 4) ? oku32(o59) : (uint32_t)((uint64_t)kira_sn * 7 / 8);
    if (t2_sn > kira_sn) t2_sn = kira_sn;
    if (t1_sn > t2_sn) t1_sn = t2_sn;
    kira_bas_sn = simdi_sn();

    int ilk = !cfg.configured;
    cfg.configured = 1;
    durum = D_BAGLI;
    ulak_set_ip(cfg.ip);      /* yigina yeni adresi bildir */

    if (!ilk) {
        serial_write("ULAK-DHCP: kira yenilendi, IP=");
        ip_yaz(cfg.ip);
        serial_write("\n");
        return;
    }
    serial_write("ULAK-DHCP: ACK alindi - YAPILANDIRMA TAMAM\n");
    serial_write("ULAK-DHCP:   IP=");
    ip_yaz(cfg.ip);
    serial_write(" maske=");
    ip_yaz(cfg.netmask);
    serial_write("\nULAK-DHCP:   gecit=");
    ip_yaz(cfg.gateway);
    serial_write(" DNS=");
    ip_yaz(cfg.dns);
    serial_write("\n");
    log_write(LOG_BILGI, "ULAK-DHCP", "adres yapilandirmasi alindi");
}

/* Kira ve yineleme zamanlayicisi: ag_yokla'dan (ag_kilit altinda). */
void dhcp_zamanlayici(void) {
    if (durum == D_BOS) return;
    uint64_t t = simdi_sn();
    if (durum == D_SECIYOR || durum == D_ISTIYOR) {
        if (t - gonderim_sn < yineleme_aralik) return;
        if (yineleme_aralik < 32) yineleme_aralik *= 2;
        if (durum == D_SECIYOR) { serial_write("ULAK-DHCP: DISCOVER yineleniyor\n"); dhcp_send(DHCPDISCOVER, 0, 0, 0, 0); }
        else {
            /* REQUEST'e yanit yok: bir kez daha, sonra bastan kesif */
            if (yineleme_aralik > 8) { kesif_baslat(); return; }
            dhcp_send(DHCPREQUEST, 0, offered_ip, server_ip, 0);
        }
        return;
    }
    if (kira_sn == KIRA_SONSUZ) return;
    uint64_t gecen = t - kira_bas_sn;
    if (gecen >= kira_sn) {
        adresi_birak("kira doldu");
        kesif_baslat();
        return;
    }
    if (durum == D_BAGLI && gecen >= t1_sn) {
        yeni_xid();
        durum = D_YENILIYOR;
        serial_write("ULAK-DHCP: T1 - kira sunucudan yenileniyor\n");
        dhcp_send(DHCPREQUEST, cfg.ip, 0, 0, server_ip);
        return;
    }
    if ((durum == D_YENILIYOR || durum == D_BAGLI) && gecen >= t2_sn) {
        yeni_xid();
        durum = D_YENIDEN_BAGLIYOR;
        serial_write("ULAK-DHCP: T2 - kira yayinla yeniden baglaniyor\n");
        dhcp_send(DHCPREQUEST, cfg.ip, 0, 0, 0);
        return;
    }
    /* YENILIYOR / YENIDEN BAGLIYOR: kalan surenin yarisinda (en az 60 sn) yinele */
    if (durum == D_YENILIYOR || durum == D_YENIDEN_BAGLIYOR) {
        uint32_t sinir = durum == D_YENILIYOR ? t2_sn : kira_sn;
        uint64_t kalan = sinir > gecen ? sinir - gecen : 0;
        uint64_t ara = kalan / 2 < 60 ? 60 : kalan / 2;
        if (t - gonderim_sn >= ara && kalan > 0)
            dhcp_send(DHCPREQUEST, cfg.ip, 0, 0, durum == D_YENILIYOR ? server_ip : 0);
    }
}

const dhcp_config_t *dhcp_get_config(void) { return &cfg; }
