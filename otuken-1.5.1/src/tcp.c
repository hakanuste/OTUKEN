#include "tcp.h"
#include "ulak.h"
#include "log.h"
#include "serial.h"

#define TCP_MAX_CONNECTIONS 4
#define TCP_AZAMI_YUK (576 - 20)   /* tcp_send_segment seg[] tamponundan kalan */

static tcp_connection_t conns[TCP_MAX_CONNECTIONS];
static uint32_t initial_seq_counter = 0x1000;

const char *tcp_state_name(tcp_state_t s) {
    switch (s) {
        case TCP_KAPALI:       return "KAPALI";
        case TCP_SYN_GONDERDI: return "SYN_GONDERDI";
        case TCP_KURULDU:      return "KURULDU";
        case TCP_FIN_BEKLIYOR: return "FIN_BEKLIYOR";
        default:               return "ZAMAN_ASIMI";
    }
}

void tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        conns[i].in_use = 0;
        conns[i].state = TCP_KAPALI;
    }
    log_write(LOG_BILGI, "ULAK-TCP", "TCP katmani baslatildi");
}

static uint16_t checksum16_pseudo(const uint8_t *src_ip, const uint8_t *dst_ip,
                                   const uint8_t *tcp_seg, uint16_t tcp_len) {
    uint32_t sum = 0;
    /* IPv4 sozde basligi (pseudo header): kaynak IP, hedef IP,
     * sifir, protokol(6), TCP uzunlugu */
    for (int i = 0; i < 4; i += 2) sum += ((uint32_t)src_ip[i] << 8) | src_ip[i+1];
    for (int i = 0; i < 4; i += 2) sum += ((uint32_t)dst_ip[i] << 8) | dst_ip[i+1];
    sum += 6;                 /* protokol = TCP */
    sum += tcp_len;

    for (int i = 0; i + 1 < tcp_len; i += 2)
        sum += ((uint32_t)tcp_seg[i] << 8) | tcp_seg[i+1];
    if (tcp_len & 1) sum += (uint32_t)tcp_seg[tcp_len - 1] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* TCP segmentini olusturup IPv4 uzerinden gonderir. */
static int tcp_send_segment(tcp_connection_t *c, uint8_t flags,
                             const uint8_t *payload, uint16_t payload_len) {
    uint8_t seg[576];
    const ulak_state_t *st = ulak_get_state();

    seg[0] = (uint8_t)(c->local_port >> 8);
    seg[1] = (uint8_t)(c->local_port & 0xFF);
    seg[2] = (uint8_t)(c->remote_port >> 8);
    seg[3] = (uint8_t)(c->remote_port & 0xFF);

    seg[4] = (uint8_t)(c->send_next >> 24);
    seg[5] = (uint8_t)(c->send_next >> 16);
    seg[6] = (uint8_t)(c->send_next >> 8);
    seg[7] = (uint8_t)(c->send_next);

    uint32_t ack = (flags & TCP_ACK) ? c->recv_next : 0;
    seg[8]  = (uint8_t)(ack >> 24);
    seg[9]  = (uint8_t)(ack >> 16);
    seg[10] = (uint8_t)(ack >> 8);
    seg[11] = (uint8_t)(ack);

    seg[12] = 0x50;   /* veri ofseti = 5 (20 bayt baslik), rezerve=0 */
    seg[13] = flags;
    seg[14] = 0x20; seg[15] = 0x00;   /* pencere boyutu = 8192 */
    seg[16] = 0x00; seg[17] = 0x00;   /* checksum (asagida) */
    seg[18] = 0x00; seg[19] = 0x00;   /* acil isaretcisi */

    for (uint16_t i = 0; i < payload_len; i++) seg[20 + i] = payload[i];
    uint16_t seg_len = (uint16_t)(20 + payload_len);

    uint16_t ck = checksum16_pseudo(st->ip, c->remote_ip, seg, seg_len);
    seg[16] = (uint8_t)(ck >> 8);
    seg[17] = (uint8_t)(ck & 0xFF);

    /* ULAK'in ham IPv4 gonderim yolunu kullan (protokol=6=TCP) */
    return ulak_send_ipv4(c->remote_ip, 6, seg, seg_len);
}

int tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port) {
    int slot = -1;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
        if (!conns[i].in_use) { slot = i; break; }
    if (slot < 0) {
        log_write(LOG_HATA, "ULAK-TCP", "bos baglanti yuvasi yok");
        return -1;
    }

    tcp_connection_t *c = &conns[slot];
    c->in_use = 1;
    for (int i = 0; i < 4; i++) c->remote_ip[i] = remote_ip[i];
    c->remote_port = remote_port;
    c->local_port = (uint16_t)(40000 + slot);
    c->send_next = initial_seq_counter;
    initial_seq_counter += 0x10000;
    c->recv_next = 0;
    c->bytes_sent = 0;
    c->bytes_received = 0;
    c->state = TCP_SYN_GONDERDI;
    c->sentetik = 0;    /* gercek ag baglantisi olarak baslar */

    serial_write("ULAK-TCP: baglanti baslatiliyor -> ");
    for (int i = 0; i < 4; i++) { serial_write_hex(remote_ip[i]); if (i<3) serial_write("."); }
    serial_write(":");
    serial_write_hex(remote_port);
    serial_write(" (SYN gonderiliyor, ISN=");
    serial_write_hex(c->send_next);
    serial_write(")\n");

    if (tcp_send_segment(c, TCP_SYN, 0, 0) != 0) {
        c->in_use = 0;
        c->state = TCP_KAPALI;
        return -1;
    }
    c->send_next++;   /* SYN bir sira numarasi tuketir */
    return slot;
}

int tcp_send(int conn_id, const uint8_t *data, uint16_t len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
    tcp_connection_t *c = &conns[conn_id];
    if (!c->in_use || c->state != TCP_KURULDU) {
        log_write(LOG_UYARI, "ULAK-TCP", "baglanti KURULDU durumunda degil, gonderim iptal");
        return -1;
    }
    /* Denetim duzeltmesi (HIGH): seg[576] yigin tamponu 20 baytlik baslikla
     * birlikte tasmadan en fazla 556 bayt yuk tasir; sinirsiz 'len' cekirdek
     * yiginini ezerdi. */
    if (len > TCP_AZAMI_YUK) {
        log_write(LOG_UYARI, "ULAK-TCP", "gonderim yuku cok buyuk, iptal");
        return -1;
    }
    if (tcp_send_segment(c, TCP_ACK | TCP_PSH, data, len) != 0) return -1;
    c->send_next += len;
    c->bytes_sent += len;
    return 0;
}

int tcp_close(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
    tcp_connection_t *c = &conns[conn_id];
    if (!c->in_use) return -1;

    tcp_send_segment(c, TCP_FIN | TCP_ACK, 0, 0);
    c->send_next++;
    c->state = TCP_FIN_BEKLIYOR;
    serial_write("ULAK-TCP: baglanti kapatiliyor (FIN gonderildi)\n");
    return 0;
}

static tcp_connection_t *find_conn(const uint8_t *src_ip, uint16_t src_port,
                                    uint16_t dst_port) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *c = &conns[i];
        if (!c->in_use) continue;
        if (c->local_port != dst_port) continue;
        if (c->remote_port != src_port) continue;
        int ip_ok = 1;
        for (int k = 0; k < 4; k++) if (c->remote_ip[k] != src_ip[k]) ip_ok = 0;
        if (ip_ok) return c;
    }
    return 0;
}

void tcp_handle_segment(const uint8_t *src_ip, const uint8_t *seg, uint16_t len) {
    if (len < 20) return;

    uint16_t src_port = ((uint16_t)seg[0] << 8) | seg[1];
    uint16_t dst_port = ((uint16_t)seg[2] << 8) | seg[3];
    uint32_t their_seq = ((uint32_t)seg[4] << 24) | ((uint32_t)seg[5] << 16) |
                          ((uint32_t)seg[6] << 8) | seg[7];
    uint8_t  flags = seg[13];
    uint8_t  data_offset = (uint8_t)((seg[12] >> 4) * 4);
    /* Denetim: bozuk veri ofseti (<20 ya da segmentten buyuk) yuk uzunlugunu
     * yanlis hesaplatirdi. */
    if (data_offset < 20 || data_offset > len) return;
    uint16_t payload_len = (uint16_t)(len - data_offset);
    uint32_t their_ack = ((uint32_t)seg[8] << 24) | ((uint32_t)seg[9] << 16) |
                         ((uint32_t)seg[10] << 8) | seg[11];

    serial_write("ULAK-TCP: segment alindi port ");
    serial_write_hex(src_port);
    serial_write("->");
    serial_write_hex(dst_port);
    serial_write(" bayraklar=");
    if (flags & TCP_SYN) serial_write("SYN ");
    if (flags & TCP_ACK) serial_write("ACK ");
    if (flags & TCP_FIN) serial_write("FIN ");
    if (flags & TCP_RST) serial_write("RST ");
    if (flags & TCP_PSH) serial_write("PSH ");
    serial_write("\n");

    tcp_connection_t *c = find_conn(src_ip, src_port, dst_port);
    if (!c) return;

    /* --- Durum makinesi --- */
    if (c->state == TCP_SYN_GONDERDI) {
        if ((flags & TCP_SYN) && (flags & TCP_ACK) && their_ack != c->send_next) {
            return;   /* SYN'imizi onaylamiyor: sahte/eski segment */
        }
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            /* Uc-yollu el sikismasinin 2. adimi geldi: SYN-ACK.
             * 3. adimi (ACK) gonderip baglantiyi KUR. */
            c->recv_next = their_seq + 1;
            c->state = TCP_KURULDU;
            tcp_send_segment(c, TCP_ACK, 0, 0);
            serial_write("ULAK-TCP: SYN-ACK alindi -> ACK gonderildi -> "
                         "BAGLANTI KURULDU (uc-yollu el sikismasi TAMAM)\n");
            /* --- Denetim duzeltmesi (MEDIUM/test butunlugu) ---
             * Bu satiri hem GERCEK ag hem de SENTETIK enjeksiyon testi
             * tetikliyordu; test takimi ikisini AYIRT EDEMIYOR ve
             * sentetik testi "gercek TCP calisiyor" sayiyordu.
             * Artik kaynak acikca belirtiliyor. */
            if (c->sentetik)
                log_write(LOG_BILGI, "ULAK-TCP",
                          "SENTETIK handshake tamam (ag DOGRULAMASI DEGIL)");
            else
                log_write(LOG_BILGI, "ULAK-TCP",
                          "GERCEK AG handshake tamam (SYN/SYN-ACK/ACK)");
        } else if (flags & TCP_RST) {
            c->state = TCP_KAPALI;
            c->in_use = 0;
            serial_write("ULAK-TCP: RST alindi - baglanti reddedildi\n");
            log_write(LOG_UYARI, "ULAK-TCP", "baglanti reddedildi (RST)");
        }
        return;
    }

    if (c->state == TCP_KURULDU) {
        if (flags & TCP_RST) {
            if (their_seq == c->recv_next) {   /* yalniz beklenen sira no'lu RST gecerli */
                c->state = TCP_KAPALI;
                c->in_use = 0;
                serial_write("ULAK-TCP: RST alindi - baglanti sifirlandi\n");
            }
            return;
        }
        /* Denetim: sira numarasi dogrulanmiyordu; yinelenen/sahte segment
         * recv_next'i ileri-geri oynatabiliyordu. Beklenmeyen sira: yalniz
         * mevcut beklentiyi yeniden onayla (standart davranis). */
        if ((payload_len > 0 || (flags & TCP_FIN)) && their_seq != c->recv_next) {
            tcp_send_segment(c, TCP_ACK, 0, 0);
            return;
        }
        if (payload_len > 0) {
            c->recv_next = their_seq + payload_len;
            c->bytes_received += payload_len;
            tcp_send_segment(c, TCP_ACK, 0, 0);   /* veriyi onayla */
            serial_write("ULAK-TCP: ");
            serial_write_hex(payload_len);
            serial_write(" bayt veri alindi ve ACK'landi\n");
        }
        if (flags & TCP_FIN) {
            c->recv_next = their_seq + payload_len + 1;   /* varsa yuk + FIN */
            tcp_send_segment(c, TCP_ACK | TCP_FIN, 0, 0);
            c->state = TCP_KAPALI;
            c->in_use = 0;
            serial_write("ULAK-TCP: karsi taraf FIN gonderdi - baglanti kapatildi\n");
        }
        return;
    }

    if (c->state == TCP_FIN_BEKLIYOR) {
        if (flags & TCP_ACK) {
            c->state = TCP_KAPALI;
            c->in_use = 0;
            serial_write("ULAK-TCP: FIN onaylandi - baglanti tamamen kapandi\n");
        }
    }
}

const tcp_connection_t *tcp_get_connection(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return 0;
    return &conns[conn_id];
}

void tcp_sentetik_isaretle(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return;
    conns[conn_id].sentetik = 1;
}
