#include "dns.h"
#include "ulak.h"
#include "log.h"
#include "serial.h"

#define DNS_PORT 53

static uint16_t dns_id = 0x4F54;
static char     last_name[64];
static uint8_t  result_ip[4];
static int      have_result = 0;

void dns_init(void) {
    have_result = 0;
    last_name[0] = 0;
    log_write(LOG_BILGI, "ULAK-DNS", "DNS istemcisi baslatildi");
}

/* "ornek.com" -> "\5ornek\3com\0" (DNS etiket bicimi) */
static int encode_name(const char *host, uint8_t *out) {
    int op = 0;
    int i = 0;
    while (host[i]) {
        int start = i;
        while (host[i] && host[i] != '.') i++;
        int seglen = i - start;
        if (seglen <= 0 || seglen > 63) return -1;
        out[op++] = (uint8_t)seglen;
        for (int k = 0; k < seglen; k++) out[op++] = (uint8_t)host[start + k];
        if (host[i] == '.') i++;
    }
    out[op++] = 0;
    return op;
}

int dns_query(const char *hostname, const uint8_t dns_server[4]) {
    uint8_t pkt[256];
    int i = 0;
    dns_id++;
    have_result = 0;

    /* Denetim duzeltmesi (HIGH): encode_name TUM ad'i pkt[256] yigin
     * tamponuna yaziyordu; uzun bir ad cekirdek yiginini tasirirdi.
     * DNS ad siniri 253 bayttir; baslik+soru ek payi ile 200'e kisitla. */
    int hn = 0;
    while (hostname[hn]) { if (++hn > 200) return -1; }
    if (hn == 0) return -1;

    int n = 0;
    while (hostname[n] && n < 63) { last_name[n] = hostname[n]; n++; }
    last_name[n] = 0;

    pkt[i++] = (uint8_t)(dns_id >> 8); pkt[i++] = (uint8_t)(dns_id & 0xFF);
    pkt[i++] = 0x01; pkt[i++] = 0x00;   /* bayraklar: ozyineleme istenir */
    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* soru sayisi = 1 */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* yanit */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* yetkili */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* ek */

    int nlen = encode_name(hostname, &pkt[i]);
    if (nlen < 0) return -1;
    i += nlen;

    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* tur: A */
    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* sinif: IN */

    serial_write("ULAK-DNS: '");
    serial_write(hostname);
    serial_write("' icin A kaydi soruluyor\n");

    return ulak_send_udp(dns_server, 40000 + (dns_id & 0xFF), DNS_PORT, pkt, (uint16_t)i);
}

/* Ad alanini atlar (sikistirma isaretcisi 0xC0 dahil) */
static int skip_name(const uint8_t *p, int off, uint16_t len) {
    while (off < len) {
        uint8_t l = p[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2;   /* sikistirma isaretcisi */
        off += 1 + l;
    }
    return off;
}

void dns_handle_packet(const uint8_t *payload, uint16_t len) {
    if (len < 12) return;
    /* Denetim: islem kimligi ve YANIT bayragi (QR) dogrulanmiyordu; 53
     * numarali kaynak porttan gelen her paket sonucu zehirleyebilirdi. */
    if ((((uint16_t)payload[0] << 8) | payload[1]) != dns_id) return;
    if (!(payload[2] & 0x80)) return;

    uint16_t qd = ((uint16_t)payload[4] << 8) | payload[5];
    uint16_t an = ((uint16_t)payload[6] << 8) | payload[7];
    if (an == 0) {
        serial_write("ULAK-DNS: yanit geldi ama A kaydi YOK\n");
        return;
    }

    int off = 12;
    for (uint16_t q = 0; q < qd; q++) {
        off = skip_name(payload, off, len);
        off += 4;   /* tur + sinif */
    }

    for (uint16_t a = 0; a < an && off + 10 <= len; a++) {
        off = skip_name(payload, off, len);
        if (off + 10 > len) break;
        uint16_t type = ((uint16_t)payload[off] << 8) | payload[off + 1];
        uint16_t rdlen = ((uint16_t)payload[off + 8] << 8) | payload[off + 9];
        off += 10;
        if (type == 1 && rdlen == 4 && off + 4 <= len) {
            for (int k = 0; k < 4; k++) result_ip[k] = payload[off + k];
            have_result = 1;
            serial_write("ULAK-DNS: '");
            serial_write(last_name);
            serial_write("' -> ");
            for (int k = 0; k < 4; k++) { serial_write_hex(result_ip[k]); if (k<3) serial_write("."); }
            serial_write("  COZULDU\n");
            log_write(LOG_BILGI, "ULAK-DNS", "ad cozumlemesi basarili");
            return;
        }
        off += rdlen;
    }
}

int dns_get_result(uint8_t out_ip[4]) {
    if (!have_result) return 0;
    for (int i = 0; i < 4; i++) out_ip[i] = result_ip[i];
    return 1;
}

const char *dns_last_query(void) { return last_name; }
