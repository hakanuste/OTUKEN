#ifndef OTUKENOS_IPC2_H
#define OTUKENOS_IPC2_H

#include <stdint.h>
#include "msgqueue.h"

/* --- Asama 31: IPC 2.0 - servis iletisimi ---
 * Mevcut mesaj kuyrugu ilkelinin uzerine ADLANDIRILMIS baglanti
 * noktalari (port) ve istek/yanit (RPC benzeri) katmani ekler.
 * Boylece surecler birbirini PID ile degil, ISIMLE bulur. */

#define IPC_MAX_PORTS      8
#define IPC_PORT_NAME_LEN 20
#define IPC_MAX_PAYLOAD   96

typedef struct {
    uint32_t opcode;                 /* istegin turu */
    uint32_t length;                 /* payload uzunlugu */
    uint32_t sender_port;            /* yanitin donecegi port */
    uint32_t sequence;               /* istek/yanit eslestirme */
    uint8_t  payload[IPC_MAX_PAYLOAD];
} ipc_message_t;

void ipc2_init(void);

/* Bir isim altinda port acar; port kimligi (>=0) doner. */
int  ipc_port_create(const char *name);
int  ipc_port_find(const char *name);
int  ipc_port_close(int port);

/* Hedef porta mesaj gonderir (bloklamaz). 0 tamam, -1 gecersiz/kapali
 * port, IPC_DOLU hedef kuyrugu dolu (gonderen sonra yeniden deneyebilir). */
#define IPC_DOLU (-2)
int  ipc_send(int dest_port, const ipc_message_t *msg);

/* Gonderen portu olmayan ileti (sender_port). 0 GECERLI bir port
 * kimligidir; 'yok' anlami icin bu deger kullanilir. */
#define IPC_GONDEREN_YOK 0xFFFFFFFFu

/* Gorevin sahip oldugu ilk port (yoksa -1) ve portun sahibi (yoksa -1). */
int  ipc_gorevin_portu(uint32_t gorev_id);
int  ipc_port_sahibi(int port, uint32_t *sahip);
/* Sonlanan gorevin portlarini kapatir (reap cagirir); kapanan sayisi. */
int  ipc_gorev_portlarini_kapat(uint32_t gorev_id);

/* Kendi portundan mesaj alir. Yoksa 0, aldiysa 1 doner (bloklamaz). */
int  ipc_try_receive(int port, ipc_message_t *out);

/* Istek gonderip yaniti BEKLER (RPC). Zaman asiminda -1. */
int  ipc_call(const char *service_name, int reply_port,
               ipc_message_t *msg, ipc_message_t *reply, uint64_t timeout_ticks);

const char *ipc_port_name(int port);
uint32_t ipc_stats_sent(void);
uint32_t ipc_stats_received(void);

#endif
