#ifndef OTUKENOS_DNS_H
#define OTUKENOS_DNS_H
#include <stdint.h>

/* --- Asama 29: DNS istemcisi (A kaydi sorgusu) --- */
void dns_init(void);
int  dns_query(const char *hostname, const uint8_t dns_server[4]);
/* src_ip/dport: UDP yanitinin kaynak adresi ve hedef portu; sorunun
 * sunucusu ve rastgele kaynak portuyla eslesmeyen yanit yok sayilir. */
void dns_handle_packet(const uint8_t src_ip[4], uint16_t dport, const uint8_t *payload, uint16_t len);
/* Cozulmus adresi dondurur; henuz gelmediyse 0. */
int  dns_get_result(uint8_t out_ip[4]);
/* 1.4.9: son sorunun yaniti "yok" dediyse (NXDOMAIN / bos yanit) 1 */
int  dns_basarisiz(void);
const char *dns_last_query(void);
/* 1.4.9: esanli sorular (yuvali). dns_sor: tutamak / -1.
 * dns_sonuc: 1 cozuldu (ip yazilir), 0 bekliyor, -1 yok/gecersiz. */
int  dns_sor(const char *hostname, const uint8_t dns_server[4]);
int  dns_sonuc(int h, uint8_t out_ip[4]);
void dns_birak(int h);

#endif
