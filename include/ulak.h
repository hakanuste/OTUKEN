#ifndef OTUKENOS_ULAK_H
#define OTUKENOS_ULAK_H

#include <stdint.h>

/* ULAK - otukenOS ag altyapisi.
 * Katmanlar: e1000 surucu -> Ethernet -> ARP / IPv4 -> UDP */

#define ULAK_MTU 1518

typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    int     link_up;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_arp;
    uint32_t rx_ipv4;
    uint32_t rx_udp;
    uint32_t rx_icmp;
    uint32_t rx_tcp;
} ulak_state_t;

/* e1000 kartini bulur, MMIO'yu haritalar, MAC adresini okur, RX/TX
 * halkalarini (ring) kurar. Basari:0, hata:-1 */
int ulak_init(void);

/* Ham Ethernet cercevesi gonderir. */
int ulak_send_frame(const uint8_t *data, uint16_t len);

/* Gelen paketleri isler (RX halkasini yoklar). Islenen paket sayisini
 * dondurur. Periyodik cagrilmalidir. */
int ulak_poll(void);

/* UDP paketi gonderir. */
/* Genel IPv4 gonderimi (protokol: 6=TCP, 17=UDP, 1=ICMP). */
int ulak_send_ipv4(const uint8_t dest_ip[4], uint8_t protocol,
                    const uint8_t *payload, uint16_t len);

int ulak_send_udp(const uint8_t dest_ip[4], uint16_t src_port,
                   uint16_t dest_port, const uint8_t *payload, uint16_t len);

/* ARP istegi yayinlar. */
int ulak_send_arp_request(const uint8_t target_ip[4]);

const ulak_state_t *ulak_get_state(void);
void ulak_set_ip(const uint8_t ip[4]);
#ifdef OTUKEN_AG_KAYIP
uint32_t ulak_kayip_sayisi(void);   /* sinama: dusurulen TCP paketi */
#endif

#endif
