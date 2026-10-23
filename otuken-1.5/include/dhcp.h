#ifndef OTUKENOS_DHCP_H
#define OTUKENOS_DHCP_H
#include <stdint.h>

/* --- Asama 29: DHCP istemcisi ---
 * DISCOVER gonderir, OFFER'i isler, REQUEST gonderir, ACK ile
 * yapilandirmayi (IP, ag gecidi, ag maskesi, DNS) alir. */

typedef struct {
    int      configured;
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  netmask[4];
    uint8_t  dns[4];
    uint32_t lease_seconds;
} dhcp_config_t;

void dhcp_init(void);
int  dhcp_discover(void);                 /* DISCOVER yayinlar */
void dhcp_handle_packet(const uint8_t *payload, uint16_t len);
const dhcp_config_t *dhcp_get_config(void);

#endif
