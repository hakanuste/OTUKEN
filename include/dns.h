#ifndef OTUKENOS_DNS_H
#define OTUKENOS_DNS_H
#include <stdint.h>

/* --- Asama 29: DNS istemcisi (A kaydi sorgusu) --- */
void dns_init(void);
int  dns_query(const char *hostname, const uint8_t dns_server[4]);
void dns_handle_packet(const uint8_t *payload, uint16_t len);
/* Cozulmus adresi dondurur; henuz gelmediyse 0. */
int  dns_get_result(uint8_t out_ip[4]);
const char *dns_last_query(void);

#endif
