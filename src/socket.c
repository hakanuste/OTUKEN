#include "socket.h"
#include "ulak.h"
#include "tcp.h"
#include "log.h"
#include "serial.h"
#include "ag.h"

#define SOCK_RXBUF 512

typedef struct {
    int      used;
    int      type;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t  remote_ip[4];
    int      tcp_conn;              /* SOCK_TCP icin baglanti kimligi */
    uint8_t  rxbuf[SOCK_RXBUF];
    uint16_t rxlen;
    uint8_t  last_src_ip[4];
    uint16_t last_src_port;
} socket_t;

static socket_t socks[SOCKET_MAX];
static uint16_t next_ephemeral = 49152;

void socket_init(void) {
    for (int i = 0; i < SOCKET_MAX; i++) socks[i].used = 0;
    log_write(LOG_BILGI, "ULAK-SOK", "soket katmani baslatildi");
}

static int socket_create__ic(int type) {
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (!socks[i].used) {
            socks[i].used = 1;
            socks[i].type = type;
            socks[i].local_port = next_ephemeral++;
            socks[i].remote_port = 0;
            socks[i].tcp_conn = -1;
            socks[i].rxlen = 0;
            return i;
        }
    }
    log_write(LOG_UYARI, "ULAK-SOK", "bos soket yuvasi yok");
    return -1;
}

static int socket_bind__ic(int sock, uint16_t local_port) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socks[sock].local_port = local_port;
    return 0;
}

static int socket_connect__ic(int sock, const uint8_t ip[4], uint16_t port) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socket_t *s = &socks[sock];
    for (int i = 0; i < 4; i++) s->remote_ip[i] = ip[i];
    s->remote_port = port;

    if (s->type == SOCK_TCP) {
        s->tcp_conn = tcp_connect(ip, port);
        return (s->tcp_conn >= 0) ? 0 : -1;
    }
    return 0;   /* UDP'de connect sadece varsayilan hedefi belirler */
}

static int socket_sendto__ic(int sock, const uint8_t ip[4], uint16_t port,
                   const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socket_t *s = &socks[sock];
    if (s->type != SOCK_UDP) return -1;
    return ulak_send_udp(ip, s->local_port, port, data, len);
}

static int socket_send__ic(int sock, const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socket_t *s = &socks[sock];
    if (s->type == SOCK_UDP) {
        if (s->remote_port == 0) return -1;
        return ulak_send_udp(s->remote_ip, s->local_port, s->remote_port, data, len);
    }
    if (s->type == SOCK_TCP && s->tcp_conn >= 0) {
        return tcp_send(s->tcp_conn, data, len);
    }
    return -1;
}

static int socket_recv__ic(int sock, uint8_t *buf, uint16_t max) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socket_t *s = &socks[sock];
    if (s->rxlen == 0) return 0;

    uint16_t n = (s->rxlen < max) ? s->rxlen : max;
    for (uint16_t i = 0; i < n; i++) buf[i] = s->rxbuf[i];
    s->rxlen = 0;      /* datagram semantigi: kismi okuma kalanı atar */
    return (int)n;
}

static int socket_recvfrom__ic(int sock, uint8_t *buf, uint16_t max,
                     uint8_t out_ip[4], uint16_t *out_port) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    socket_t *s = &socks[sock];
    if (s->rxlen == 0) return 0;

    uint16_t n = (s->rxlen < max) ? s->rxlen : max;
    for (uint16_t i = 0; i < n; i++) buf[i] = s->rxbuf[i];
    if (out_ip)   for (int i = 0; i < 4; i++) out_ip[i] = s->last_src_ip[i];
    if (out_port) *out_port = s->last_src_port;
    s->rxlen = 0;
    return (int)n;
}

static int socket_close__ic(int sock) {
    if (sock < 0 || sock >= SOCKET_MAX || !socks[sock].used) return -1;
    if (socks[sock].type == SOCK_TCP && socks[sock].tcp_conn >= 0)
        tcp_close(socks[sock].tcp_conn);
    socks[sock].used = 0;
    return 0;
}

static void socket_deliver_udp__ic(const uint8_t *src_ip, uint16_t src_port,
                         uint16_t dst_port, const uint8_t *data, uint16_t len) {
    for (int i = 0; i < SOCKET_MAX; i++) {
        socket_t *s = &socks[i];
        if (!s->used || s->type != SOCK_UDP) continue;
        if (s->local_port != dst_port) continue;

        uint16_t n = (len < SOCK_RXBUF) ? len : SOCK_RXBUF;
        for (uint16_t k = 0; k < n; k++) s->rxbuf[k] = data[k];
        s->rxlen = n;
        for (int k = 0; k < 4; k++) s->last_src_ip[k] = src_ip[k];
        s->last_src_port = src_port;

        serial_write("ULAK-SOK: soket ");
        serial_write_hex((uint32_t)i);
        serial_write(" icin ");
        serial_write_hex(n);
        serial_write(" bayt UDP verisi teslim edildi\n");
        return;
    }
}

/* 1.4.9: ag_kilit sarmalayicilari (ozyineli; UDP teslimi zaten kilit altinda) */
int socket_create(int type) {
    ozspin_al(&ag_kilit);
    int r = socket_create__ic(type);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_bind(int sock, uint16_t local_port) {
    ozspin_al(&ag_kilit);
    int r = socket_bind__ic(sock, local_port);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_connect(int sock, const uint8_t ip[4], uint16_t port) {
    ozspin_al(&ag_kilit);
    int r = socket_connect__ic(sock, ip, port);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_sendto(int sock, const uint8_t ip[4], uint16_t port,
                   const uint8_t *data, uint16_t len) {
    ozspin_al(&ag_kilit);
    int r = socket_sendto__ic(sock, ip, port, data, len);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_send(int sock, const uint8_t *data, uint16_t len) {
    ozspin_al(&ag_kilit);
    int r = socket_send__ic(sock, data, len);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_recv(int sock, uint8_t *buf, uint16_t max) {
    ozspin_al(&ag_kilit);
    int r = socket_recv__ic(sock, buf, max);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_recvfrom(int sock, uint8_t *buf, uint16_t max,
                     uint8_t out_ip[4], uint16_t *out_port) {
    ozspin_al(&ag_kilit);
    int r = socket_recvfrom__ic(sock, buf, max, out_ip, out_port);
    ozspin_birak(&ag_kilit);
    return r;
}
int socket_close(int sock) {
    ozspin_al(&ag_kilit);
    int r = socket_close__ic(sock);
    ozspin_birak(&ag_kilit);
    return r;
}
void socket_deliver_udp(const uint8_t *src_ip, uint16_t src_port,
                         uint16_t dst_port, const uint8_t *data, uint16_t len) {
    ozspin_al(&ag_kilit);
    socket_deliver_udp__ic(src_ip, src_port, dst_port, data, len);
    ozspin_birak(&ag_kilit);
}
