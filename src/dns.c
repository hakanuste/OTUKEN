/* =====================================================================
 * ULAK-DNS — A kaydi istemcisi
 *
 * 1.4.9: soru YUVALARI. Eskiden tek bir kuresel "son soru/son yanit"
 * vardi: ayni anda iki soru (cekirdek acilis sinamasi + Ring3 ag_coz)
 * sorulunca yanitlar karisiyordu (sinamada '*.invalid' sorusuna
 * example.com'un adresi dondu). Artik her soru kendi kimligi (rastgele
 * 16 bit) ve yuvasiyla eslesir; yanit, sorulan adla da denetlenir.
 * ===================================================================== */
#include "dns.h"
#include "ulak.h"
#include "log.h"
#include "serial.h"
#include "ag.h"

#define DNS_PORT 53
#define DNS_YUVA 6

typedef struct {
    int      kullanimda;
    uint16_t id;
    uint16_t kaynak_port;
    char     ad[64];
    int      durum;           /* 0 bekliyor, 1 cozuldu, -1 yok */
    uint8_t  ip[4];
} dns_yuva_t;

static dns_yuva_t yuva[DNS_YUVA];
static int eski_yuva = -1;    /* dns_query/dns_get_result (eski API) */
static uint16_t dns_id = 0x4F54;
static char last_name[64];

#define KILIT   ozspin_al(&ag_kilit)
#define BIRAK   ozspin_birak(&ag_kilit)

void dns_init(void) {
    KILIT;
    for (int i = 0; i < DNS_YUVA; i++) yuva[i].kullanimda = 0;
    eski_yuva = -1;
    last_name[0] = 0;
    BIRAK;
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

static int sor__ic(const char *hostname, const uint8_t dns_server[4]) {
    int n = 0;
    while (hostname[n]) { if (++n > 253) return -1; }
    if (n == 0) return -1;
    int y = -1;
    for (int i = 0; i < DNS_YUVA; i++) if (!yuva[i].kullanimda) { y = i; break; }
    if (y < 0) return -1;

    /* kimlik tahmin edilemesin (sahte yanita karsi): TSC karisimi */
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    dns_id = (uint16_t)(dns_id * 31u + (lo ^ (lo >> 16) ^ hi) + 1u);

    dns_yuva_t *s = &yuva[y];
    s->kullanimda = 1;
    s->id = dns_id;
    s->kaynak_port = (uint16_t)(40000 + ((lo >> 3) & 0x3FF) * DNS_YUVA + y);
    s->durum = 0;
    int k = 0;
    while (hostname[k] && k < 63) { s->ad[k] = hostname[k]; last_name[k] = hostname[k]; k++; }
    s->ad[k] = 0; last_name[k] = 0;

    uint8_t pkt[300];
    int i = 0;
    pkt[i++] = (uint8_t)(s->id >> 8); pkt[i++] = (uint8_t)(s->id & 0xFF);
    pkt[i++] = 0x01; pkt[i++] = 0x00;   /* bayraklar: ozyineleme istenir */
    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* soru sayisi = 1 */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* yanit */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* yetkili */
    pkt[i++] = 0x00; pkt[i++] = 0x00;   /* ek */
    int nlen = encode_name(hostname, &pkt[i]);
    if (nlen < 0) { s->kullanimda = 0; return -1; }
    i += nlen;
    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* tur: A */
    pkt[i++] = 0x00; pkt[i++] = 0x01;   /* sinif: IN */

    serial_write("ULAK-DNS: '");
    serial_write(s->ad);
    serial_write("' icin A kaydi soruluyor\n");
    if (ulak_send_udp(dns_server, s->kaynak_port, DNS_PORT, pkt, (uint16_t)i) != 0) {
        s->kullanimda = 0;
        return -1;
    }
    return y;
}

int dns_sor(const char *hostname, const uint8_t dns_server[4]) {
    KILIT;
    int r = sor__ic(hostname, dns_server);
    BIRAK;
    return r;
}

int dns_sonuc(int h, uint8_t out_ip[4]) {
    KILIT;
    int r = -1;
    if (h >= 0 && h < DNS_YUVA && yuva[h].kullanimda) {
        r = yuva[h].durum;
        if (r == 1) for (int i = 0; i < 4; i++) out_ip[i] = yuva[h].ip[i];
    }
    BIRAK;
    return r;
}

void dns_birak(int h) {
    KILIT;
    if (h >= 0 && h < DNS_YUVA) yuva[h].kullanimda = 0;
    BIRAK;
}

/* --- eski API (tek soru): cekirdek acilis sinamasi --- */
int dns_query(const char *hostname, const uint8_t dns_server[4]) {
    KILIT;
    if (eski_yuva >= 0) yuva[eski_yuva].kullanimda = 0;
    eski_yuva = sor__ic(hostname, dns_server);
    int r = eski_yuva >= 0 ? 0 : -1;
    BIRAK;
    return r;
}

int dns_get_result(uint8_t out_ip[4]) {
    return eski_yuva >= 0 && dns_sonuc(eski_yuva, out_ip) == 1;
}

int dns_basarisiz(void) {
    uint8_t ip[4];
    return eski_yuva >= 0 && dns_sonuc(eski_yuva, ip) == -1;
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

/* Soru bolumundeki ad, yuvadaki adla ayni mi (buyuk/kucuk duyarsiz) */
static int ad_ayni(const uint8_t *p, int off, uint16_t len, const char *ad) {
    int k = 0;
    while (off < len) {
        uint8_t l = p[off++];
        if (l == 0) return ad[k] == 0;
        if (l & 0xC0) return 0;
        if (k && ad[k++] != '.') return 0;
        for (int i = 0; i < l; i++, off++) {
            if (off >= len || !ad[k]) return 0;
            uint8_t a = p[off], b = (uint8_t)ad[k++];
            if (a >= 'A' && a <= 'Z') a = (uint8_t)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (uint8_t)(b + 32);
            if (a != b) return 0;
        }
    }
    return 0;
}

static void yanit__ic(const uint8_t *payload, uint16_t len) {
    if (len < 12) return;
    uint16_t id = ((uint16_t)payload[0] << 8) | payload[1];
    if (!(payload[2] & 0x80)) return;                    /* soru, yanit degil */
    dns_yuva_t *s = 0;
    for (int i = 0; i < DNS_YUVA; i++)
        if (yuva[i].kullanimda && yuva[i].durum == 0 && yuva[i].id == id) s = &yuva[i];
    if (!s) return;
    uint16_t qd = ((uint16_t)payload[4] << 8) | payload[5];
    uint16_t an = ((uint16_t)payload[6] << 8) | payload[7];
    if (qd != 1 || !ad_ayni(payload, 12, len, s->ad)) return;   /* baska sorunun yaniti */
    if ((payload[3] & 0x0F) != 0) { s->durum = -1; return; }  /* RCODE: NXDOMAIN vb. */
    if (an == 0) {
        s->durum = -1;
        serial_write("ULAK-DNS: yanit geldi ama A kaydi YOK\n");
        return;
    }
    int off = 12;
    off = skip_name(payload, off, len);
    off += 4;   /* tur + sinif */
    for (uint16_t a = 0; a < an && off + 10 <= len; a++) {
        off = skip_name(payload, off, len);
        if (off + 10 > len) break;
        uint16_t type = ((uint16_t)payload[off] << 8) | payload[off + 1];
        uint16_t rdlen = ((uint16_t)payload[off + 8] << 8) | payload[off + 9];
        off += 10;
        if (type == 1 && rdlen == 4 && off + 4 <= len) {
            for (int k = 0; k < 4; k++) s->ip[k] = payload[off + k];
            s->durum = 1;
            serial_write("ULAK-DNS: '");
            serial_write(s->ad);
            serial_write("' -> ");
            for (int k = 0; k < 4; k++) { serial_write_hex(s->ip[k]); if (k<3) serial_write("."); }
            serial_write("  COZULDU\n");
            log_write(LOG_BILGI, "ULAK-DNS", "ad cozumlemesi basarili");
            return;
        }
        off += rdlen;
    }
    s->durum = -1;          /* yanit var ama A kaydi yok (yalniz CNAME/AAAA) */
}

void dns_handle_packet(const uint8_t *payload, uint16_t len) {
    KILIT;
    yanit__ic(payload, len);
    BIRAK;
}

const char *dns_last_query(void) { return last_name; }
