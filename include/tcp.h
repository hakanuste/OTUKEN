#ifndef OTUKENOS_TCP_H
#define OTUKENOS_TCP_H

#include <stdint.h>

/* ULAK-TCP: baglanti yonelimli tasima katmani.
 *
 * 1.4.9: iskeletten gercek istemci TCP'sine: alma halkasi (64 KB, sirali
 * teslim), gonderme tamponu (16 KB, onaylanana dek saklanir), pencere
 * (alinan ve ilan edilen), yeniden gonderme (RTO, geri cekilmeli), MSS
 * secenegi, saglama toplami DOGRULAMASI, duzgun kapanis (FIN her iki
 * yonde), RST. Yalniz istemci (dinleme yok). Sira disi gelen segment
 * halkada yerine yazilir ve araligi tutulur; bosluk dolunca hepsi birden
 * onaylanir (kayipta karsi taraf yalniz kayip parcayi yeniden yollar).
 *
 * Kilit: butun giris noktalari ag_kilit'i alir (bkz. ag.h). */

typedef enum {
    TCP_KAPALI       = 0,
    TCP_SYN_GONDERDI = 1,
    TCP_KURULDU      = 2,
    TCP_FIN_BEKLIYOR = 3,   /* biz FIN gonderdik (FIN_WAIT_1/2)          */
    TCP_ZAMAN_ASIMI  = 4,   /* yeniden gonderme tukendi: baglanti oldu   */
    TCP_KARSI_KAPADI = 5,   /* karsi FIN gonderdi, biz hala gonderebiliriz */
    TCP_SON_ACK      = 6    /* karsi kapadi, biz de FIN gonderdik         */
} tcp_state_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_ALMA_BOY    (64 * 1024)
#define TCP_GONDER_BOY  (16 * 1024)
#define TCP_MSS         1360

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
    /* --- 1.4.9 --- */
    uint32_t snd_una;       /* onaylanmamis en eski sira no */
    uint32_t snd_wnd;       /* karsinin ilan ettigi pencere */
    uint16_t mss;           /* gonderim parca boyu */
    uint8_t *alma;          /* halka */
    uint32_t alma_bas, alma_dolu;
    uint8_t *gonder;        /* [0] = snd_una'daki bayt */
    uint32_t gonder_dolu;   /* tampondaki veri (gonderilmis+gonderilmemis) */
    uint32_t gonderilmis;   /* bunun kaci yola cikti */
    int      fin_istendi, fin_gonderildi, fin_onaylandi, karsi_fin;
    int      hata;          /* RST ya da zaman asimi */
    int      birakildi;     /* sahibi birakti: kapaninca yuva bosalir */
    uint64_t rto_bitis;     /* tik; 0 = zamanlayici kapali */
    uint32_t rto;           /* tik */
    int      deneme;
    uint64_t kapanis_bitis; /* FIN_BEKLIYOR/SON_ACK icin ust sinir */
    uint32_t yeniden_gonderim;
    /* sira disi alinan araliklar [bas, son) (halkada yerlerine yazildi) */
    uint32_t sd_bas[8], sd_son[8];
    int      sd_adet;
    uint32_t sira_disi_saklanan;
    uint32_t sabit_pencere;   /* bosluk varken ilan edilen (degismez) pencere */
} tcp_connection_t;

void tcp_init(void);

/* Eski (cekirdek ici) API: uzak sunucuya baglanti baslatir. Yuva sahipsizdir:
 * baglanti kapaninca kendiliginden bosalir. Kimlik (>=0) ya da -1. */
int tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port);

/* Kurulu baglantidan veri gonderir: TAMAMI kabul edilirse 0, yoksa -1. */
int tcp_send(int conn_id, const uint8_t *data, uint16_t len);

/* Baglantiyi duzgun kapatir (bekleyen veriden sonra FIN). */
int tcp_close(int conn_id);

/* --- 1.4.9 sahipli API (Ring3 baglantilari, ag.c) --- */
int  tcp_ac(const uint8_t remote_ip[4], uint16_t remote_port);    /* kimlik / -1 */
int  tcp_yaz(int conn_id, const uint8_t *data, uint32_t len);     /* kabul edilen / -1 */
/* >0 okunan, 0 simdilik veri yok, -2 karsi kapadi (akis sonu), -1 hata */
int  tcp_oku(int conn_id, uint8_t *buf, uint32_t max);
void tcp_birak(int conn_id);          /* sahip birakir (gerekirse kapatir) */
tcp_state_t tcp_durum(int conn_id);
int  tcp_hata(int conn_id);

/* Yeniden gonderme ve bekleyen veri; ana dongu her tikte cagirir. */
void tcp_zamanlayici(void);

/* ULAK IPv4 katmanindan cagrilir - gelen TCP segmentini isler. */
void tcp_handle_segment(const uint8_t *src_ip, const uint8_t *segment, uint16_t len);

/* IPv4 sozde baslikli TCP saglama toplami (sinama da kullanir). */
uint16_t tcp_saglama(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                     const uint8_t *seg, uint16_t len);

/* Baglantiyi SENTETIK olarak isaretler - yalnizca durum makinesi
 * sinamalarinda kullanilir. */
void tcp_sentetik_isaretle(int conn_id);

const tcp_connection_t *tcp_get_connection(int conn_id);
const char *tcp_state_name(tcp_state_t s);

/* Sayaclar (sinama/tani) */
uint32_t tcp_saglama_reddi(void);
int      tcp_acik_sayisi(void);
int      tcp_ozet(int i, uint8_t ip[4], uint16_t *yerel, uint16_t *uzak, int *durum, int *sahipli,
                  uint32_t *alinan, uint32_t *gonderilen, uint32_t *yeniden);

#endif
