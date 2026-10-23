/* =====================================================================
 * ULAK-TCP — istemci TCP'si (bkz. include/tcp.h)
 *
 * Sira no uzayi (gonderen taraf):
 *   snd_una ............ snd_una+gonder_dolu  [+1 FIN]
 *   |-- gonderilmis --|-- bekleyen --|
 *   send_next = snd_una + gonderilmis (+1 FIN gonderildiyse)
 * SYN_GONDERDI'de snd_una = ISN, send_next = ISN+1.
 * ===================================================================== */
#include "tcp.h"
#include "ulak.h"
#include "dhcp.h"
#include "ag.h"
#include "heap.h"
#include "timer.h"
#include "log.h"
#include "serial.h"

#define TCP_MAX_CONNECTIONS 16
#define TCP_ILAN_AZAMI      32768     /* RX halkasini asmasin */
#define TCP_DENEME_AZAMI    8
#define TCP_RTO_AZAMI_SN    8

static tcp_connection_t conns[TCP_MAX_CONNECTIONS];
static uint32_t initial_seq_counter = 0x1000;
static uint16_t yerel_port_sirasi = 0;
static uint32_t saglama_reddi = 0;

#define KILIT   ozspin_al(&ag_kilit)
#define BIRAK   ozspin_birak(&ag_kilit)

const char *tcp_state_name(tcp_state_t s) {
    switch (s) {
        case TCP_KAPALI:       return "KAPALI";
        case TCP_SYN_GONDERDI: return "SYN_GONDERDI";
        case TCP_KURULDU:      return "KURULDU";
        case TCP_FIN_BEKLIYOR: return "FIN_BEKLIYOR";
        case TCP_KARSI_KAPADI: return "KARSI_KAPADI";
        case TCP_SON_ACK:      return "SON_ACK";
        default:               return "ZAMAN_ASIMI";
    }
}

void tcp_init(void) {
    KILIT;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        conns[i].in_use = 0;
        conns[i].state = TCP_KAPALI;
    }
    /* ISN: tahmin edilebilir olmasin (RFC 6528'in ruhu: zaman + sayac) */
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    initial_seq_counter ^= lo * 2654435761u;
    yerel_port_sirasi = (uint16_t)(hi ^ lo) & 0x3FFF;
    BIRAK;
    log_write(LOG_BILGI, "ULAK-TCP", "TCP katmani baslatildi");
}

static uint32_t hz(void) { uint32_t h = timer_hz(); return h ? h : 100; }

uint16_t tcp_saglama(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                     const uint8_t *tcp_seg, uint16_t tcp_len) {
    uint32_t sum = 0;
    /* IPv4 sozde basligi: kaynak IP, hedef IP, sifir, protokol(6), TCP boyu */
    for (int i = 0; i < 4; i += 2) sum += ((uint32_t)src_ip[i] << 8) | src_ip[i+1];
    for (int i = 0; i < 4; i += 2) sum += ((uint32_t)dst_ip[i] << 8) | dst_ip[i+1];
    sum += 6;
    sum += tcp_len;
    for (int i = 0; i + 1 < tcp_len; i += 2)
        sum += ((uint32_t)tcp_seg[i] << 8) | tcp_seg[i+1];
    if (tcp_len & 1) sum += (uint32_t)tcp_seg[tcp_len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t serbest_pencere(const tcp_connection_t *c) {
    uint32_t bos = c->alma ? TCP_ALMA_BOY - c->alma_dolu : 0;
    return bos > TCP_ILAN_AZAMI ? TCP_ILAN_AZAMI : bos;
}

/* Sira disi veri beklenirken (bosluk varken) ilan edilen pencere SABIT
 * tutulur: gonderen, pencereleri ayni olan yinelenen ACK'leri sayar (RFC
 * 5681) ve 3.'de kayip parcayi HEMEN yeniden yollar. Pencere her ACK'te
 * degisirse (uygulama bu arada okuyor) hizli yeniden gonderim hic
 * tetiklenmez, her kayip bir RTO (slirp'te ~1.5 sn) bekletir: %5 kayipta
 * 1 MB 28 KB/sn ile iniyordu. Sabit deger, o anki serbest alani asamaz
 * cunku bosluk surerken halka yalniz okunarak bosalir. */
static uint32_t ilan_penceresi(const tcp_connection_t *c) {
    if (c->sd_adet > 0 && c->sabit_pencere) return c->sabit_pencere;
    return serbest_pencere(c);
}

/* TCP segmentini olusturup IPv4 uzerinden gonderir. seq acikca verilir. */
static int segment_yolla(tcp_connection_t *c, uint32_t seq, uint8_t flags,
                         const uint8_t *payload, uint16_t payload_len) {
    uint8_t seg[20 + 4 + TCP_MSS];
    const ulak_state_t *st = ulak_get_state();
    int bas = (flags & TCP_SYN) ? 24 : 20;
    if (payload_len > TCP_MSS) return -1;

    seg[0] = (uint8_t)(c->local_port >> 8);  seg[1] = (uint8_t)c->local_port;
    seg[2] = (uint8_t)(c->remote_port >> 8); seg[3] = (uint8_t)c->remote_port;
    seg[4] = (uint8_t)(seq >> 24); seg[5] = (uint8_t)(seq >> 16);
    seg[6] = (uint8_t)(seq >> 8);  seg[7] = (uint8_t)seq;
    uint32_t ack = (flags & TCP_ACK) ? c->recv_next : 0;
    seg[8] = (uint8_t)(ack >> 24); seg[9] = (uint8_t)(ack >> 16);
    seg[10] = (uint8_t)(ack >> 8); seg[11] = (uint8_t)ack;
    seg[12] = (uint8_t)((bas / 4) << 4);
    seg[13] = flags;
    uint32_t w = ilan_penceresi(c);
    if ((flags & TCP_SYN) && w == 0) w = 8192;
    seg[14] = (uint8_t)(w >> 8); seg[15] = (uint8_t)w;
    seg[16] = 0; seg[17] = 0; seg[18] = 0; seg[19] = 0;
    if (bas == 24) {                      /* MSS secenegi */
        seg[20] = 2; seg[21] = 4;
        seg[22] = (uint8_t)(TCP_MSS >> 8); seg[23] = (uint8_t)TCP_MSS;
    }
    for (uint16_t i = 0; i < payload_len; i++) seg[bas + i] = payload[i];
    uint16_t seg_len = (uint16_t)(bas + payload_len);
    uint16_t ck = tcp_saglama(st->ip, c->remote_ip, seg, seg_len);
    seg[16] = (uint8_t)(ck >> 8); seg[17] = (uint8_t)ck;
    return ulak_send_ipv4(c->remote_ip, 6, seg, seg_len);
}

static void ack_yolla(tcp_connection_t *c) {
    segment_yolla(c, c->send_next, TCP_ACK, 0, 0);
}

static void rto_kur(tcp_connection_t *c) {
    c->rto_bitis = timer_get_ticks() + c->rto;
}

static void yuva_bosalt(tcp_connection_t *c) {
    if (c->alma) { kfree(c->alma); c->alma = 0; }
    if (c->gonder) { kfree(c->gonder); c->gonder = 0; }
    c->in_use = 0;
}

/* Baglanti oldu/kapandi. Sahipsizse (eski API ya da birakilmis) yuva bosalir. */
static void kapandi(tcp_connection_t *c, tcp_state_t son) {
    c->state = son;
    c->rto_bitis = 0;
    if (son == TCP_ZAMAN_ASIMI || c->hata) c->hata = c->hata ? c->hata : 2;
    if (c->birakildi) yuva_bosalt(c);
}

/* Bekleyen veriyi (ve istenmisse FIN'i) pencere elverdikce yollar. */
static void gonder_bosalt(tcp_connection_t *c) {
    if (c->state != TCP_KURULDU && c->state != TCP_KARSI_KAPADI &&
        c->state != TCP_FIN_BEKLIYOR && c->state != TCP_SON_ACK) return;
    while (c->gonderilmis < c->gonder_dolu) {
        uint32_t ucusta = c->gonderilmis;
        uint32_t pencere = c->snd_wnd;
        if (ucusta >= pencere) break;
        uint32_t n = c->gonder_dolu - c->gonderilmis;
        if (n > c->mss) n = c->mss;
        if (n > pencere - ucusta) n = pencere - ucusta;
        uint32_t seq = c->snd_una + c->gonderilmis;
        if (segment_yolla(c, seq, TCP_ACK | TCP_PSH, c->gonder + c->gonderilmis, (uint16_t)n) != 0)
            break;
        c->gonderilmis += n;
        c->send_next = c->snd_una + c->gonderilmis;
        c->bytes_sent += n;
        if (!c->rto_bitis) rto_kur(c);
    }
    if (c->fin_istendi && !c->fin_gonderildi && c->gonderilmis == c->gonder_dolu) {
        uint32_t seq = c->snd_una + c->gonder_dolu;
        segment_yolla(c, seq, TCP_FIN | TCP_ACK, 0, 0);
        c->fin_gonderildi = 1;
        c->send_next = seq + 1;
        if (c->state == TCP_KURULDU) c->state = TCP_FIN_BEKLIYOR;
        else if (c->state == TCP_KARSI_KAPADI) c->state = TCP_SON_ACK;
        c->kapanis_bitis = timer_get_ticks() + 10 * hz();
        if (!c->rto_bitis) rto_kur(c);
    }
}

static int yuva_ac(const uint8_t remote_ip[4], uint16_t remote_port, int sahipsiz) {
    int slot = -1;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
        if (!conns[i].in_use) { slot = i; break; }
    if (slot < 0) {
        log_write(LOG_HATA, "ULAK-TCP", "bos baglanti yuvasi yok");
        return -1;
    }
    tcp_connection_t *c = &conns[slot];
    uint8_t *alma = (uint8_t *)kmalloc(TCP_ALMA_BOY);
    uint8_t *gon = (uint8_t *)kmalloc(TCP_GONDER_BOY);
    if (!alma || !gon) {
        if (alma) kfree(alma);
        if (gon) kfree(gon);
        log_write(LOG_HATA, "ULAK-TCP", "baglanti tamponu ayrilamadi");
        return -1;
    }
    uint8_t *p = (uint8_t *)c;
    for (uint32_t i = 0; i < sizeof(*c); i++) p[i] = 0;
    c->alma = alma;
    c->gonder = gon;
    c->in_use = 1;
    c->birakildi = sahipsiz;
    for (int i = 0; i < 4; i++) c->remote_ip[i] = remote_ip[i];
    c->remote_port = remote_port;
    c->local_port = (uint16_t)(49152 + (yerel_port_sirasi++ & 0x3FFF));
    c->snd_una = initial_seq_counter;
    initial_seq_counter += 0x10000 + (uint32_t)timer_get_ticks() * 64;
    c->send_next = c->snd_una + 1;          /* SYN bir sira numarasi tuketir */
    c->mss = 536;                           /* karsi MSS bildirene dek RFC varsayilani */
    c->rto = hz();                          /* 1 sn */
    c->state = TCP_SYN_GONDERDI;
    c->sentetik = 0;

    serial_write("ULAK-TCP: baglanti baslatiliyor -> ");
    for (int i = 0; i < 4; i++) { serial_write_hex(remote_ip[i]); if (i<3) serial_write("."); }
    serial_write(":");
    serial_write_hex(remote_port);
    serial_write(" (SYN gonderiliyor, ISN=");
    serial_write_hex(c->snd_una);
    serial_write(")\n");

    if (segment_yolla(c, c->snd_una, TCP_SYN, 0, 0) != 0) {
        yuva_bosalt(c);
        return -1;
    }
    rto_kur(c);
    return slot;
}

int tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port) {
    KILIT;
    int r = yuva_ac(remote_ip, remote_port, 1);
    BIRAK;
    return r;
}

int tcp_ac(const uint8_t remote_ip[4], uint16_t remote_port) {
    KILIT;
    int r = yuva_ac(remote_ip, remote_port, 0);
    BIRAK;
    return r;
}

static tcp_connection_t *gecerli(int id) {
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !conns[id].in_use) return 0;
    return &conns[id];
}

static int yaz__ic(int conn_id, const uint8_t *data, uint32_t len) {
    tcp_connection_t *c = gecerli(conn_id);
    if (!c || (c->state != TCP_KURULDU && c->state != TCP_KARSI_KAPADI) || c->fin_istendi) {
        log_write(LOG_UYARI, "ULAK-TCP", "baglanti KURULDU durumunda degil, gonderim iptal");
        return -1;
    }
    uint32_t yer = TCP_GONDER_BOY - c->gonder_dolu;
    uint32_t n = len < yer ? len : yer;
    for (uint32_t i = 0; i < n; i++) c->gonder[c->gonder_dolu + i] = data[i];
    c->gonder_dolu += n;
    gonder_bosalt(c);
    return (int)n;
}

int tcp_yaz(int conn_id, const uint8_t *data, uint32_t len) {
    KILIT;
    int r = yaz__ic(conn_id, data, len);
    BIRAK;
    return r;
}

int tcp_send(int conn_id, const uint8_t *data, uint16_t len) {
    KILIT;
    int r = yaz__ic(conn_id, data, len);
    BIRAK;
    return (r == (int)len) ? 0 : -1;
}

int tcp_oku(int conn_id, uint8_t *buf, uint32_t max) {
    KILIT;
    tcp_connection_t *c = gecerli(conn_id);
    int r;
    if (!c) r = -1;
    else if (c->alma_dolu > 0) {
        uint32_t onceki_pencere = ilan_penceresi(c);
        uint32_t n = c->alma_dolu < max ? c->alma_dolu : max;
        for (uint32_t i = 0; i < n; i++)
            buf[i] = c->alma[(c->alma_bas + i) % TCP_ALMA_BOY];
        c->alma_bas = (c->alma_bas + n) % TCP_ALMA_BOY;
        c->alma_dolu -= n;
        /* pencere kapanmaya yakinken acildiysa karsiya bildir */
        if (onceki_pencere < 2u * c->mss && ilan_penceresi(c) >= 2u * c->mss &&
            c->state != TCP_KAPALI && c->state != TCP_ZAMAN_ASIMI)
            ack_yolla(c);
        r = (int)n;
    } else if (c->karsi_fin) r = -2;
    else if (c->hata || c->state == TCP_KAPALI || c->state == TCP_ZAMAN_ASIMI) r = -1;
    else r = 0;
    BIRAK;
    return r;
}

int tcp_close(int conn_id) {
    KILIT;
    tcp_connection_t *c = gecerli(conn_id);
    int r = -1;
    if (c) {
        r = 0;
        if (c->state == TCP_SYN_GONDERDI) {
            kapandi(c, TCP_KAPALI);
        } else if ((c->state == TCP_KURULDU || c->state == TCP_KARSI_KAPADI) && !c->fin_istendi) {
            c->fin_istendi = 1;
            gonder_bosalt(c);
            serial_write("ULAK-TCP: baglanti kapatiliyor (FIN gonderildi)\n");
        }
    }
    BIRAK;
    return r;
}

void tcp_birak(int conn_id) {
    KILIT;
    tcp_connection_t *c = gecerli(conn_id);
    if (c) {
        c->birakildi = 1;
        if (c->state == TCP_KAPALI || c->state == TCP_ZAMAN_ASIMI) yuva_bosalt(c);
        else if (c->state == TCP_SYN_GONDERDI) kapandi(c, TCP_KAPALI);
        else if (!c->fin_istendi) {
            c->fin_istendi = 1;
            gonder_bosalt(c);
        }
        /* okunmamis veri artik kimsenin degil: karsi hala yolluyorsa kapanis
         * zaman asimi (10 sn) yuvayi yine de bosaltir */
    }
    BIRAK;
}

tcp_state_t tcp_durum(int conn_id) {
    KILIT;
    tcp_connection_t *c = gecerli(conn_id);
    tcp_state_t s = c ? c->state : TCP_KAPALI;
    BIRAK;
    return s;
}

int tcp_hata(int conn_id) {
    KILIT;
    tcp_connection_t *c = gecerli(conn_id);
    int h = c ? c->hata : -1;
    BIRAK;
    return h;
}

void tcp_zamanlayici(void) {
    KILIT;
    uint64_t simdi = timer_get_ticks();
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *c = &conns[i];
        if (!c->in_use) continue;
        if (c->state == TCP_KAPALI || c->state == TCP_ZAMAN_ASIMI) continue;
        if ((c->state == TCP_FIN_BEKLIYOR || c->state == TCP_SON_ACK) &&
            c->kapanis_bitis && simdi >= c->kapanis_bitis) {
            kapandi(c, TCP_KAPALI);
            continue;
        }
        if (c->birakildi && c->kapanis_bitis == 0 && c->fin_istendi)
            c->kapanis_bitis = simdi + 10 * hz();
        if (c->rto_bitis && simdi >= c->rto_bitis) {
            if (++c->deneme > TCP_DENEME_AZAMI) {
                serial_write("ULAK-TCP: yeniden gonderme tukendi - baglanti zaman asimina ugradi\n");
                kapandi(c, TCP_ZAMAN_ASIMI);
                continue;
            }
            c->yeniden_gonderim++;
            c->rto *= 2;
            if (c->rto > TCP_RTO_AZAMI_SN * hz()) c->rto = TCP_RTO_AZAMI_SN * hz();
            c->rto_bitis = 0;
            if (c->state == TCP_SYN_GONDERDI) {
                segment_yolla(c, c->snd_una, TCP_SYN, 0, 0);
                rto_kur(c);
            } else {
                /* geri-N: onaylanmamis en eskiden yeniden. Pencere 0 ise
                 * bu, pencere yoklamasi da olur (en az bir parca). */
                c->gonderilmis = 0;
                c->send_next = c->snd_una;
                if (c->fin_gonderildi) c->fin_gonderildi = 0;
                uint32_t pencere = c->snd_wnd;
                if (c->snd_wnd == 0) c->snd_wnd = 1;
                gonder_bosalt(c);
                c->snd_wnd = pencere;
                if (!c->rto_bitis) rto_kur(c);
            }
        } else {
            gonder_bosalt(c);        /* pencere acildiysa bekleyeni yolla */
        }
    }
    BIRAK;
}

static tcp_connection_t *find_conn(const uint8_t *src_ip, uint16_t src_port,
                                    uint16_t dst_port) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *c = &conns[i];
        if (!c->in_use) continue;
        if (c->state == TCP_KAPALI || c->state == TCP_ZAMAN_ASIMI) continue;
        if (c->local_port != dst_port) continue;
        if (c->remote_port != src_port) continue;
        int ip_ok = 1;
        for (int k = 0; k < 4; k++) if (c->remote_ip[k] != src_ip[k]) ip_ok = 0;
        if (ip_ok) return c;
    }
    return 0;
}

/* a < b (sira no, 2^32 sarmali) */
static int once(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

static void ack_isle(tcp_connection_t *c, uint32_t ack, uint32_t pencere) {
    uint32_t onaylanan = ack - c->snd_una;
    uint32_t ucusta = c->send_next - c->snd_una;
    if (onaylanan == 0 || onaylanan > ucusta) {
        c->snd_wnd = pencere;                 /* yinelenen/eski ACK: pencere guncelle */
        return;
    }
    uint32_t veri = onaylanan < c->gonder_dolu ? onaylanan : c->gonder_dolu;
    for (uint32_t i = veri; i < c->gonder_dolu; i++) c->gonder[i - veri] = c->gonder[i];
    c->gonder_dolu -= veri;
    c->gonderilmis -= veri < c->gonderilmis ? veri : c->gonderilmis;
    c->snd_una = ack;
    c->snd_wnd = pencere;
    if (onaylanan > veri && c->fin_gonderildi) c->fin_onaylandi = 1;
    c->deneme = 0;
    c->rto = hz();
    c->rto_bitis = (c->send_next != c->snd_una) ? timer_get_ticks() + c->rto : 0;
}

static void segment_isle(const uint8_t *src_ip, const uint8_t *seg, uint16_t len) {
    if (len < 20) return;
    const ulak_state_t *st = ulak_get_state();
    if (tcp_saglama(src_ip, st->ip, seg, len) != 0) {
        saglama_reddi++;
        return;                               /* bozuk (ya da sahte) segment */
    }
    uint16_t src_port = ((uint16_t)seg[0] << 8) | seg[1];
    uint16_t dst_port = ((uint16_t)seg[2] << 8) | seg[3];
    uint32_t their_seq = ((uint32_t)seg[4] << 24) | ((uint32_t)seg[5] << 16) |
                         ((uint32_t)seg[6] << 8) | seg[7];
    uint32_t their_ack = ((uint32_t)seg[8] << 24) | ((uint32_t)seg[9] << 16) |
                         ((uint32_t)seg[10] << 8) | seg[11];
    uint8_t  flags = seg[13];
    uint32_t pencere = ((uint32_t)seg[14] << 8) | seg[15];
    uint8_t  data_offset = (uint8_t)((seg[12] >> 4) * 4);
    if (data_offset < 20 || data_offset > len) return;
    uint16_t payload_len = (uint16_t)(len - data_offset);
    const uint8_t *payload = seg + data_offset;

    tcp_connection_t *c = find_conn(src_ip, src_port, dst_port);
    if (!c) return;

    if (c->state == TCP_SYN_GONDERDI) {
        if ((flags & TCP_RST) && (flags & TCP_ACK) && their_ack == c->send_next) {
            serial_write("ULAK-TCP: RST alindi - baglanti reddedildi\n");
            log_write(LOG_UYARI, "ULAK-TCP", "baglanti reddedildi (RST)");
            c->hata = 1;
            kapandi(c, TCP_KAPALI);
        } else if ((flags & TCP_SYN) && (flags & TCP_ACK) && their_ack == c->send_next) {
            /* MSS secenegi */
            for (int o = 20; o + 1 < data_offset; ) {
                uint8_t tur = seg[o];
                if (tur == 0) break;
                if (tur == 1) { o++; continue; }
                uint8_t ob = seg[o + 1];
                if (ob < 2 || o + ob > data_offset) break;
                if (tur == 2 && ob == 4) {
                    uint16_t m = (uint16_t)((seg[o + 2] << 8) | seg[o + 3]);
                    c->mss = m < TCP_MSS ? m : TCP_MSS;
                    if (c->mss < 64) c->mss = 64;
                }
                o += ob;
            }
            c->recv_next = their_seq + 1;
            c->snd_una = their_ack;
            c->snd_wnd = pencere;
            c->state = TCP_KURULDU;
            c->deneme = 0;
            c->rto = hz();
            c->rto_bitis = 0;
            ack_yolla(c);
            serial_write("ULAK-TCP: SYN-ACK alindi -> ACK gonderildi -> "
                         "BAGLANTI KURULDU (uc-yollu el sikismasi TAMAM)\n");
            /* Kaynak acikca belirtilir: sentetik enjeksiyon GERCEK ag kaniti degildir. */
            if (c->sentetik)
                log_write(LOG_BILGI, "ULAK-TCP",
                          "SENTETIK handshake tamam (ag DOGRULAMASI DEGIL)");
            else
                log_write(LOG_BILGI, "ULAK-TCP",
                          "GERCEK AG handshake tamam (SYN/SYN-ACK/ACK)");
            gonder_bosalt(c);
        }
        return;
    }

    /* Kabul edilebilirlik: RST yalniz pencere icindeyse (kor RST'ye karsi) */
    if (flags & TCP_RST) {
        if (their_seq == c->recv_next ||
            (!once(their_seq, c->recv_next) && once(their_seq, c->recv_next + ilan_penceresi(c) + 1))) {
            serial_write("ULAK-TCP: RST alindi - baglanti koptu\n");
            c->hata = 1;
            kapandi(c, TCP_KAPALI);
        }
        return;
    }
    if (flags & TCP_SYN) { ack_yolla(c); return; }    /* yinelenen SYN-ACK */

    if (flags & TCP_ACK) ack_isle(c, their_ack, pencere);

    /* Veri. Beklenen siradaysa halkaya eklenir; ilerideyse (pencere icinde)
     * halkadaki YERINE yazilir ve araligi tutulur. */
    int ack_gerek = 0;
    if (payload_len > 0) {
        ack_gerek = 1;
        uint32_t atla = 0;
        int sira_disi = 0;
        if (once(their_seq, c->recv_next)) {
            atla = c->recv_next - their_seq;
            if (atla >= payload_len) atla = payload_len;     /* tamami eski */
        } else if (their_seq != c->recv_next) {
            sira_disi = 1;
        }
        if (sira_disi && !c->karsi_fin) {
            uint32_t ofs = their_seq - c->recv_next;
            uint32_t bos = TCP_ALMA_BOY - c->alma_dolu;
            if (ofs < bos && c->sd_adet < 8) {
                uint32_t n = payload_len;
                if (ofs + n > bos) n = bos - ofs;
                for (uint32_t i = 0; i < n; i++)
                    c->alma[(c->alma_bas + c->alma_dolu + ofs + i) % TCP_ALMA_BOY] = payload[i];
                uint32_t b = their_seq, e = their_seq + n;
                /* araliklari birlestir */
                int k = 0;
                for (int i = 0; i < c->sd_adet; i++) {
                    if (!once(c->sd_son[i], b) && !once(e, c->sd_bas[i])) {   /* ortusuyor/bitisik */
                        if (once(c->sd_bas[i], b)) b = c->sd_bas[i];
                        if (once(e, c->sd_son[i])) e = c->sd_son[i];
                    } else { c->sd_bas[k] = c->sd_bas[i]; c->sd_son[k] = c->sd_son[i]; k++; }
                }
                if (c->sd_adet == 0) c->sabit_pencere = serbest_pencere(c);
                c->sd_bas[k] = b; c->sd_son[k] = e; c->sd_adet = k + 1;
                c->sira_disi_saklanan += n;
            }
            flags &= (uint8_t)~TCP_FIN;                      /* FIN sirasi gelince */
        } else if (atla < payload_len && !c->karsi_fin) {
            uint32_t yer = TCP_ALMA_BOY - c->alma_dolu;
            uint32_t n = payload_len - atla;
            if (n > yer) n = yer;
            for (uint32_t i = 0; i < n; i++)
                c->alma[(c->alma_bas + c->alma_dolu + i) % TCP_ALMA_BOY] = payload[atla + i];
            c->alma_dolu += n;
            c->recv_next += n;
            c->bytes_received += n;
            if (n != payload_len - atla) flags &= (uint8_t)~TCP_FIN;   /* FIN'e ulasilmadi */
            /* bosluk kapandiysa sira disi araliklar da teslim edilir */
            int ilerledi = 1;
            while (ilerledi) {
                ilerledi = 0;
                for (int i = 0; i < c->sd_adet; i++) {
                    if (!once(c->recv_next, c->sd_bas[i])) {         /* bas <= recv_next */
                        if (once(c->recv_next, c->sd_son[i])) {
                            uint32_t ek = c->sd_son[i] - c->recv_next;
                            if (ek > TCP_ALMA_BOY - c->alma_dolu) ek = TCP_ALMA_BOY - c->alma_dolu;
                            c->alma_dolu += ek;
                            c->recv_next += ek;
                            c->bytes_received += ek;
                            flags &= (uint8_t)~TCP_FIN;      /* FIN bu segmentin sonundaydi */
                        }
                        c->sd_bas[i] = c->sd_bas[c->sd_adet - 1];
                        c->sd_son[i] = c->sd_son[c->sd_adet - 1];
                        c->sd_adet--;
                        ilerledi = 1;
                        break;
                    }
                }
            }
        } else if (flags & TCP_FIN) {
            if (their_seq + payload_len != c->recv_next) flags &= (uint8_t)~TCP_FIN;
        }
    }

    if ((flags & TCP_FIN) && !c->karsi_fin &&
        (payload_len > 0 || their_seq == c->recv_next)) {
        c->recv_next++;
        c->karsi_fin = 1;
        ack_gerek = 1;
        serial_write("ULAK-TCP: karsi taraf FIN gonderdi\n");
        if (c->state == TCP_KURULDU) c->state = TCP_KARSI_KAPADI;
    } else if ((flags & TCP_FIN) && c->karsi_fin) {
        ack_gerek = 1;                                       /* yinelenen FIN */
    }

    if (ack_gerek) ack_yolla(c);

    /* kapanis gecisleri */
    if (c->state == TCP_FIN_BEKLIYOR && c->fin_onaylandi && c->karsi_fin) {
        serial_write("ULAK-TCP: baglanti iki yonde kapandi\n");
        kapandi(c, TCP_KAPALI);
    } else if (c->state == TCP_SON_ACK && c->fin_onaylandi) {
        serial_write("ULAK-TCP: FIN onaylandi - baglanti tamamen kapandi\n");
        kapandi(c, TCP_KAPALI);
    } else if (c->karsi_fin && c->birakildi && !c->fin_istendi) {
        c->fin_istendi = 1;
        gonder_bosalt(c);
    } else {
        gonder_bosalt(c);
    }
}

void tcp_handle_segment(const uint8_t *src_ip, const uint8_t *seg, uint16_t len) {
    KILIT;
    segment_isle(src_ip, seg, len);
    BIRAK;
}

const tcp_connection_t *tcp_get_connection(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return 0;
    return &conns[conn_id];
}

void tcp_sentetik_isaretle(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return;
    conns[conn_id].sentetik = 1;
}

uint32_t tcp_saglama_reddi(void) { return saglama_reddi; }

int tcp_acik_sayisi(void) {
    int n = 0;
    KILIT;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) if (conns[i].in_use) n++;
    BIRAK;
    return n;
}

/* 1.4.9: ULAK uygulamasi icin i. acik baglantinin ozeti. 0 / -1 (yok) */
int tcp_ozet(int i, uint8_t ip[4], uint16_t *yerel, uint16_t *uzak, int *durum, int *sahipli,
             uint32_t *alinan, uint32_t *gonderilen, uint32_t *yeniden) {
    int n = 0, r = -1;
    KILIT;
    for (int k = 0; k < TCP_MAX_CONNECTIONS; k++) {
        tcp_connection_t *c = &conns[k];
        if (!c->in_use) continue;
        if (n++ != i) continue;
        for (int b = 0; b < 4; b++) ip[b] = c->remote_ip[b];
        *yerel = c->local_port; *uzak = c->remote_port; *durum = (int)c->state;
        *sahipli = !c->birakildi; *alinan = c->bytes_received; *gonderilen = c->bytes_sent;
        *yeniden = c->yeniden_gonderim;
        r = 0;
        break;
    }
    BIRAK;
    return r;
}
