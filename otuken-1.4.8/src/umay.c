#include "umay.h"
#include "heap.h"
#include "pmm.h"
#include "vfs.h"
#include "fat16.h"
#include "ulak.h"
#include "socket.h"
#include "ipc2.h"
#include "tore.h"
#include "kut.h"
#include "timer.h"
#include "log.h"
#include "serial.h"

#define UMAY_PID 100          /* UMAY'a ayrilmis surec kimligi */

static umay_runtime_t rt;

const char *umay_durum_adi(umay_durum_t d) {
    switch (d) {
        case UMAY_DURDURULDU: return "DURDURULDU";
        case UMAY_YUKLENIYOR: return "YUKLENIYOR";
        case UMAY_HAZIR:      return "HAZIR";
        case UMAY_MESGUL:     return "MESGUL";
        default:              return "HATA";
    }
}

void umay_init(void) {
    rt.durum = UMAY_DURDURULDU;
    rt.pid = UMAY_PID;
    rt.ipc_port = -1;
    rt.kullanilan_bellek = 0;
    rt.islenen_istek = 0;
    rt.reddedilen_istek = 0;
    rt.model_adi[0] = 0;
    log_write(LOG_BILGI, "UMAY", "calisma zamani baslatildi");
}

int umay_yukle(const char *model_adi, const umay_sozlesme_t *s) {
    rt.durum = UMAY_YUKLENIYOR;
    rt.sozlesme = *s;

    int i = 0;
    while (model_adi[i] && i < 31) { rt.model_adi[i] = model_adi[i]; i++; }
    rt.model_adi[i] = 0;

    /* --- Kaynak sozlesmesini denetle --- */
    uint64_t bos = pmm_free_frames() * PMM_FRAME_SIZE;
    if (s->azami_bellek > bos) {
        serial_write("UMAY: RED - istenen bellek sistemde yok (istenen=");
        serial_write_hex((uint32_t)(s->azami_bellek >> 20));
        serial_write("MB mevcut=");
        serial_write_hex((uint32_t)(bos >> 20));
        serial_write("MB)\n");
        rt.durum = UMAY_HATA;
        log_write(LOG_HATA, "UMAY", "kaynak sozlesmesi karsilanamadi");
        return -1;
    }

    /* --- Yetkileri TORE'ye yaz: UMAY yalnizca sozlesmede yazani yapabilir --- */
    uint32_t yetki = KUT_MEM_ALLOC;
    if (s->dosya_okuma)     yetki |= KUT_DISK_READ;
    if (s->dosya_yazma)     yetki |= KUT_DISK_WRITE;
    if (s->ag_erisimi)      yetki |= KUT_NET_SEND | KUT_NET_RECV;
    if (s->surec_olusturma) yetki |= KUT_PROC_CREATE;
    tore_set_capabilities(rt.pid, yetki);

    /* NOT: IPC portunu ARTIK cekirdek acmiyor. UMAY ayri bir kullanici
     * programi oldugu icin portunu KENDISI acar (bkz. userprog/umay.c).
     * Burada acsaydik isim cakisir ve programin kaydi reddedilirdi -
     * gercekten yasanan hata buydu. */
    rt.ipc_port = -1;

    rt.durum = UMAY_HAZIR;
    serial_write("UMAY: model '");
    serial_write(rt.model_adi);
    serial_write("' yuklendi, durum=HAZIR, yetki=");
    serial_write_hex(yetki);
    serial_write(" ipc_port=");
    serial_write_hex((uint32_t)rt.ipc_port);
    serial_write("\n");
    log_write(LOG_BILGI, "UMAY", "model yuklendi ve hazir");
    return 0;
}

/* ---- Cekirdek yetenek arabirimi: her cagri once SOZLESME sonra
 * TORE yetkisi ile denetlenir. Ikisinden biri izin vermezse islem
 * REDDEDILIR - model, kendisine verilmeyeni yapamaz. ---- */

static int izin_var(int sozlesme_bayragi, kut_capability_t yetki, const char *islem) {
    if (!sozlesme_bayragi) {
        rt.reddedilen_istek++;
        serial_write("UMAY: sozlesme reddi -> ");
        serial_write(islem);
        serial_write("\n");
        return 0;
    }
    if (!tore_check(rt.pid, yetki, islem)) {
        rt.reddedilen_istek++;
        return 0;
    }
    return 1;
}

int umay_cek_dosya_oku(const char *yol, uint8_t *tampon, uint32_t azami) {
    if (!izin_var(rt.sozlesme.dosya_okuma, KUT_DISK_READ, "umay:dosya_oku")) return -1;
    int fd = vfs_open(yol);
    if (fd < 0) return -1;
    int n = vfs_read(fd, tampon, azami);
    vfs_close(fd);
    return n;
}

int umay_cek_dosya_yaz(const char *ad, const uint8_t *veri, uint32_t boyut) {
    if (!izin_var(rt.sozlesme.dosya_yazma, KUT_DISK_WRITE, "umay:dosya_yaz")) return -1;
    return fat16_write_file(ad, veri, boyut);
}

void *umay_cek_bellek_al(uint64_t boyut) {
    if (!izin_var(1, KUT_MEM_ALLOC, "umay:bellek_al")) return 0;
    if (rt.kullanilan_bellek + boyut > rt.sozlesme.azami_bellek) {
        rt.reddedilen_istek++;
        serial_write("UMAY: bellek siniri asildi - RED\n");
        return 0;
    }
    void *p = kmalloc((size_t)boyut);
    if (p) rt.kullanilan_bellek += boyut;
    return p;
}

void umay_cek_bellek_birak(void *p) {
    if (p) kfree(p);
}

int umay_cek_ag_gonder(const uint8_t ip[4], uint16_t port,
                        const uint8_t *veri, uint16_t uzunluk) {
    if (!izin_var(rt.sozlesme.ag_erisimi, KUT_NET_SEND, "umay:ag_gonder")) return -1;
    return ulak_send_udp(ip, 45000, port, veri, uzunluk);
}

int umay_cek_ipc_gonder(const char *servis, uint32_t opkod,
                         const void *veri, uint32_t uzunluk) {
    if (!rt.sozlesme.ipc_erisimi) { rt.reddedilen_istek++; return -1; }
    int hedef = ipc_port_find(servis);
    if (hedef < 0) return -1;
    if (uzunluk > IPC_MAX_PAYLOAD) return -1;

    ipc_message_t m;
    m.opcode      = opkod;
    m.length      = uzunluk;
    m.sender_port = (uint32_t)rt.ipc_port;
    m.sequence    = 0;
    const uint8_t *k = (const uint8_t *)veri;
    for (uint32_t i = 0; i < uzunluk; i++) m.payload[i] = k[i];
    for (uint32_t i = uzunluk; i < IPC_MAX_PAYLOAD; i++) m.payload[i] = 0;
    return ipc_send(hedef, &m);
}

uint64_t umay_cek_zaman(void) { return timer_get_ticks(); }

/* ---- Istek isleme ----
 * GERCEK model burada calisacak. Su an modelin YERINE, arabirimin
 * uctan uca calistigini kanitlayan bir islem yapiyoruz: istegi alir,
 * cekirdek yeteneklerini KULLANIR (dosya + zaman) ve yapilandirilmis
 * bir yanit uretir. Model hazir oldugunda degisecek TEK yer burasidir. */
static int metin_kopyala(char *hedef, int p, int azami, const char *kaynak) {
    while (*kaynak && p < azami - 1) hedef[p++] = *kaynak++;
    return p;
}

static int sayi_yaz(char *hedef, int p, int azami, uint64_t v) {
    char tmp[24]; int n = 0;
    if (v == 0) { if (p < azami-1) hedef[p++] = '0'; return p; }
    while (v && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0 && p < azami - 1) hedef[p++] = tmp[--n];
    return p;
}

int umay_istek(const char *metin, char *out, int out_max) {
    if (rt.durum != UMAY_HAZIR) {
        serial_write("UMAY: istek reddedildi - durum HAZIR degil\n");
        return -1;
    }
    rt.durum = UMAY_MESGUL;

    int p = 0;
    p = metin_kopyala(out, p, out_max, "UMAY[");
    p = metin_kopyala(out, p, out_max, rt.model_adi);
    p = metin_kopyala(out, p, out_max, "] istek alindi: '");
    p = metin_kopyala(out, p, out_max, metin);
    p = metin_kopyala(out, p, out_max, "'\n");

    /* Cekirdek yeteneklerini GERCEKTEN kullan - arabirimin calistiginin
     * kaniti budur. */
    p = metin_kopyala(out, p, out_max, "  cekirdek zamani (tik): ");
    p = sayi_yaz(out, p, out_max, umay_cek_zaman());
    p = metin_kopyala(out, p, out_max, "\n");

    uint8_t tampon[64];
    int okundu = umay_cek_dosya_oku("/OTUKEN.TXT", tampon, sizeof(tampon) - 1);
    p = metin_kopyala(out, p, out_max, "  dosya erisimi: ");
    if (okundu > 0) {
        p = sayi_yaz(out, p, out_max, (uint64_t)okundu);
        p = metin_kopyala(out, p, out_max, " bayt okundu\n");
    } else {
        p = metin_kopyala(out, p, out_max, "REDDEDILDI (sozlesme/yetki)\n");
    }

    void *blok = umay_cek_bellek_al(4096);
    p = metin_kopyala(out, p, out_max, "  bellek ayirma: ");
    p = metin_kopyala(out, p, out_max, blok ? "TAMAM\n" : "REDDEDILDI\n");
    if (blok) umay_cek_bellek_birak(blok);

    out[p] = 0;
    rt.islenen_istek++;
    rt.durum = UMAY_HAZIR;
    return p;
}

void umay_durdur(void) {
    rt.durum = UMAY_DURDURULDU;
    serial_write("UMAY: calisma zamani durduruldu (islenen=");
    serial_write_hex(rt.islenen_istek);
    serial_write(" reddedilen=");
    serial_write_hex(rt.reddedilen_istek);
    serial_write(")\n");
    log_write(LOG_BILGI, "UMAY", "calisma zamani durduruldu");
}

const umay_runtime_t *umay_durum_al(void) { return &rt; }
