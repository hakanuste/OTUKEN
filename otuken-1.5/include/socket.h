#ifndef OTUKENOS_SOCKET_H
#define OTUKENOS_SOCKET_H

#include <stdint.h>

/* --- Asama 29: ULAK soket API'si ---
 * UDP ve TCP'yi ortak, BSD benzeri bir arayuz arkasina alir. */

#define SOCK_UDP 1
#define SOCK_TCP 2

#define SOCKET_MAX 8

void socket_init(void);

/* Yeni soket olusturur (SOCK_UDP / SOCK_TCP). Soket kimligi (>=0)
 * ya da -1 doner. */
int socket_create(int type);

/* Yerel porta baglar (UDP alimi icin gerekli). */
int socket_bind(int sock, uint16_t local_port);

/* TCP icin: uzak uca baglanir (uc-yollu el sikismasi baslatir). */
int socket_connect(int sock, const uint8_t ip[4], uint16_t port);

/* Veri gonderir. UDP'de hedef sendto ile, TCP'de connect ile belirlenir. */
int socket_send(int sock, const uint8_t *data, uint16_t len);
int socket_sendto(int sock, const uint8_t ip[4], uint16_t port,
                   const uint8_t *data, uint16_t len);

/* Alinan veriyi kopyalar. Veri yoksa 0 doner (BLOKLAMAZ). */
int socket_recv(int sock, uint8_t *buf, uint16_t max);

/* socket_recv gibi, ama GONDERENIN adresini de dondurur. Bir hizmetin
 * istegi YANITLAYABILMESI icin gonderenin IP'si ve PORTU gereklidir -
 * sabit bir adrese yanit gondermek yanlistir (istemci farkli bir
 * porttan/adresten gelebilir ve genellikle gelir). */
int socket_recvfrom(int sock, uint8_t *buf, uint16_t max,
                     uint8_t out_ip[4], uint16_t *out_port);

int socket_close(int sock);

/* ULAK/UDP katmanindan cagirilir: gelen datagrami ilgili sokete teslim eder. */
void socket_deliver_udp(const uint8_t *src_ip, uint16_t src_port,
                         uint16_t dst_port, const uint8_t *data, uint16_t len);

#endif
