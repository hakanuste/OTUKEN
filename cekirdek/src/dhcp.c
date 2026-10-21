#include "dhcp.h"
#include "ulak.h"
#include "log.h"
#include "serial.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

static dhcp_config_t cfg;
static uint32_t xid = 0x4F54554B;   /* "OTUK" - islem kimligi */
static uint8_t  offered_ip[4];
static uint8_t  server_ip[4];
static int      awaiting_ack = 0;

void dhcp_init(void) {
    cfg.configured = 0;
    awaiting_ack = 0;
    log_write(LOG_BILGI, "ULAK-DHCP", "DHCP istemcisi baslatildi");
}

/* Ortak DHCP paketi kurar. type: DISCOVER ya da REQUEST */
static int dhcp_send(uint8_t type, const uint8_t *req_ip, const uint8_t *srv_ip) {
    uint8_t pkt[300];
    for (int i = 0; i < 300; i++) pkt[i] = 0;

    const ulak_state_t *st = ulak_get_state();

    pkt[0] = 1;      /* op: BOOTREQUEST */
    pkt[1] = 1;      /* htype: Ethernet */
    pkt[2] = 6;      /* hlen */
    pkt[3] = 0;      /* hops */
    pkt[4] = (uint8_t)(xid >> 24); pkt[5] = (uint8_t)(xid >> 16);
    pkt[6] = (uint8_t)(xid >> 8);  pkt[7] = (uint8_t)xid;
    pkt[10] = 0x80;  /* bayraklar: yayin (broadcast) iste */

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

    pkt[p++] = 55; pkt[p++] = 4;                       /* parametre istegi */
    pkt[p++] = 1;   /* ag maskesi */
    pkt[p++] = 3;   /* yonlendirici */
    pkt[p++] = 6;   /* DNS */
    pkt[p++] = 51;  /* kira suresi */

    pkt[p++] = 255;                                     /* son */

    uint8_t bcast[4] = {255, 255, 255, 255};
    return ulak_send_udp(bcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, pkt, (uint16_t)p);
}

int dhcp_discover(void) {
    serial_write("ULAK-DHCP: DISCOVER yayinlaniyor...\n");
    return dhcp_send(DHCPDISCOVER, 0, 0);
}

/* Secenek (option) alanindan bir degeri cikarir. */
static const uint8_t *find_option(const uint8_t *p, uint16_t len,
                                   uint8_t code, uint8_t *out_len) {
    uint16_t i = 240;
    while (i < len) {
        uint8_t c = p[i];
        if (c == 255) break;           /* son */
        if (c == 0) { i++; continue; } /* dolgu */
        if (i + 1 >= len) break;
        uint8_t l = p[i + 1];
        if (c == code) { *out_len = l; return &p[i + 2]; }
        i += 2 + l;
    }
    return 0;
}

void dhcp_handle_packet(const uint8_t *payload, uint16_t len) {
    if (len < 240) return;
    if (payload[236] != 99 || payload[237] != 130 ||
        payload[238] != 83 || payload[239] != 99) return;   /* cerez yok */

    uint8_t olen = 0;
    const uint8_t *mt = find_option(payload, len, 53, &olen);
    if (!mt) return;

    if (mt[0] == DHCPOFFER && !awaiting_ack) {
        for (int i = 0; i < 4; i++) offered_ip[i] = payload[16 + i];   /* yiaddr */
        const uint8_t *sid = find_option(payload, len, 54, &olen);
        if (sid && olen == 4) for (int i = 0; i < 4; i++) server_ip[i] = sid[i];

        serial_write("ULAK-DHCP: OFFER alindi, onerilen IP=");
        for (int i = 0; i < 4; i++) { serial_write_hex(offered_ip[i]); if (i<3) serial_write("."); }
        serial_write(" -> REQUEST gonderiliyor\n");

        awaiting_ack = 1;
        dhcp_send(DHCPREQUEST, offered_ip, server_ip);

    } else if (mt[0] == DHCPACK) {
        for (int i = 0; i < 4; i++) cfg.ip[i] = payload[16 + i];

        const uint8_t *nm = find_option(payload, len, 1, &olen);
        if (nm && olen == 4) for (int i = 0; i < 4; i++) cfg.netmask[i] = nm[i];
        const uint8_t *gw = find_option(payload, len, 3, &olen);
        if (gw && olen >= 4) for (int i = 0; i < 4; i++) cfg.gateway[i] = gw[i];
        const uint8_t *dn = find_option(payload, len, 6, &olen);
        if (dn && olen >= 4) for (int i = 0; i < 4; i++) cfg.dns[i] = dn[i];
        const uint8_t *ls = find_option(payload, len, 51, &olen);
        if (ls && olen == 4)
            cfg.lease_seconds = ((uint32_t)ls[0] << 24) | ((uint32_t)ls[1] << 16) |
                                 ((uint32_t)ls[2] << 8) | ls[3];

        cfg.configured = 1;
        awaiting_ack = 0;
        ulak_set_ip(cfg.ip);      /* yigina yeni adresi bildir */

        serial_write("ULAK-DHCP: ACK alindi - YAPILANDIRMA TAMAM\n");
        serial_write("ULAK-DHCP:   IP=");
        for (int i = 0; i < 4; i++) { serial_write_hex(cfg.ip[i]); if (i<3) serial_write("."); }
        serial_write(" maske=");
        for (int i = 0; i < 4; i++) { serial_write_hex(cfg.netmask[i]); if (i<3) serial_write("."); }
        serial_write("\nULAK-DHCP:   gecit=");
        for (int i = 0; i < 4; i++) { serial_write_hex(cfg.gateway[i]); if (i<3) serial_write("."); }
        serial_write(" DNS=");
        for (int i = 0; i < 4; i++) { serial_write_hex(cfg.dns[i]); if (i<3) serial_write("."); }
        serial_write("\n");
        log_write(LOG_BILGI, "ULAK-DHCP", "adres yapilandirmasi alindi");
    }
}

const dhcp_config_t *dhcp_get_config(void) { return &cfg; }
