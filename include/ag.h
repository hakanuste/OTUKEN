#ifndef OTUKENOS_AG_H
#define OTUKENOS_AG_H
#include <stdint.h>
#include "spinlock.h"

/* 1.4.9 — ULAK ag yigininin kilidi ve Ring3 ag tutamaklari.
 *
 * Eskiden yigin kilitsizdi ve yalniz BSP'deki ana dongu dokunuyordu.
 * Ring3 ag cagrilari herhangi bir cekirdekten gelir: artik ULAK/TCP/
 * soket/DNS giris noktalarinin HEPSI ag_kilit'i alir (ozyineli, kesmeler
 * kapali). */
extern ozspin_t ag_kilit;

/* Ana dongu ve ag cagrilari: gelen paketleri isle + TCP zamanlayicisi. */
void ag_yokla(void);

/* Ring3 tutamaklari (sahip gorev kimligiyle) */
#define AG_TUTAMAK_MAX   32
#define AG_GOREV_AZAMI    8      /* gorev basina acik baglanti */

/* ad (ya da "a.b.c.d") -> ip. Bloklar (en cok ~4 sn). 0 / -1 */
int ag_coz(const char *ad, uint8_t ip[4]);
/* Baglanir (bloklar, en cok ~8 sn). Tutamak / -1 */
int ag_baglan(uint32_t sahip, const uint8_t ip[4], uint16_t port);
int ag_gonder(uint32_t sahip, int t, const uint8_t *veri, uint32_t n);   /* kabul / -1 */
int ag_al(uint32_t sahip, int t, uint8_t *tampon, uint32_t azami);       /* n/0/-2/-1 */
int ag_kapat(uint32_t sahip, int t);
/* Reap: olen gorevin baglantilari birakilir. Birakilan sayi. */
int ag_gorev_bitti(uint32_t sahip);
int ag_tutamak_sayisi(void);
uint32_t ag_baglanti_toplam(void);   /* acilistan beri kurulan Ring3 baglantisi */

/* SYS_AG_BILGI cikti yapisi (Ring3 ile ayni duzen) */
typedef struct {
    uint8_t  ip[4], maske[4], gecit[4], dns[4];
    uint8_t  mac[6];
    uint8_t  bagli, yapilandi;
    uint32_t rx_paket, tx_paket, rx_tcp, acik_tcp;
} ag_bilgi_t;
void ag_bilgi(ag_bilgi_t *b);
#endif
