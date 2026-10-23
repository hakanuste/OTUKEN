#ifndef OTUKENOS_TCP_H
#define OTUKENOS_TCP_H

#include <stdint.h>

/* ULAK-TCP: baglanti yonelimli tasima katmani.
 * Bu asamada: baglanti durum makinesi (state machine), uc-yollu el
 * sikismasi (three-way handshake) baslatma, siralama numarasi (sequence)
 * takibi, ve baglanti kapatma. */

typedef enum {
    TCP_KAPALI       = 0,
    TCP_SYN_GONDERDI = 1,
    TCP_KURULDU      = 2,
    TCP_FIN_BEKLIYOR = 3,
    TCP_ZAMAN_ASIMI  = 4
} tcp_state_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct {
    tcp_state_t state;
    uint8_t  remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_next;   /* SND.NXT - gonderecegimiz siradaki sira no */
    uint32_t recv_next;   /* RCV.NXT - bekledigimiz siradaki sira no */
    uint32_t bytes_sent;
    uint32_t bytes_received;
    int      in_use;
    /* Test butunlugu: bu baglanti SENTETIK olarak (enjeksiyonla) mi
     * kuruldu? Gercek ag dogrulamasiyla karistirilmamalidir. */
    int      sentetik;
} tcp_connection_t;

void tcp_init(void);

/* Uzak sunucuya baglanti baslatir (SYN gonderir). Baglanti kimligini
 * (>=0) dondurur, hata:-1 */
int tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port);

/* Kurulu baglantidan veri gonderir. */
int tcp_send(int conn_id, const uint8_t *data, uint16_t len);

/* Baglantiyi duzgun kapatir (FIN gonderir). */
int tcp_close(int conn_id);

/* ULAK IPv4 katmanindan cagrilir - gelen TCP segmentini isler. */
void tcp_handle_segment(const uint8_t *src_ip, const uint8_t *segment, uint16_t len);

/* Baglantiyi SENTETIK olarak isaretler - yalnizca durum makinesi
 * sinamalarinda kullanilir. */
void tcp_sentetik_isaretle(int conn_id);

const tcp_connection_t *tcp_get_connection(int conn_id);
const char *tcp_state_name(tcp_state_t s);

#endif
