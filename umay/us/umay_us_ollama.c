/* =====================================================================
 * UMAY — OLLAMA arka ucu (yalnız ana bilgisayar yapımı)
 *
 * NE YAPAR: US katmanının çıkarımını yerelde koşan Ollama'ya verir.
 * Ollama çevrim dışıdır; makineden dışarı bir şey gitmez.
 *
 * NEDEN: UMAY'ın kendi US1 motoru, ÖTÜKEN'in tamsayı aritmetiğiyle
 * koşabilsin diye küçük tutuldu — motorun izin verdiği en büyük model
 * 467 bin değişken. O boyut Türkçenin harf dizilimini öğrenir, anlamlı
 * cümle kurmaz. Tutarlı metin üreten en küçük modeller yüz milyon
 * değişken dolayındadır. US1 yerinde kalır; bu arka uç onun yerine
 * değil, ÖNÜNE geçer ve yoksa US1'e düşülür.
 *
 * SAHTE BAŞARI YOK: Ollama kapalıysa, model yüklü değilse ya da yanıt
 * bozuksa hazırlama BAŞARISIZ döner. Boş bir yanıtı "cevap" diye
 * geçirmek, hiç cevap vermemekten kötüdür.
 *
 * AYARLAR:
 *   UMAY_OLLAMA        adres, varsayılan 127.0.0.1:11434
 *   UMAY_OLLAMA_MODEL  model adı; verilmezse kurulu ilk model seçilir
 * ===================================================================== */
#ifndef UMAY_OTUKEN

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "umay_us.h"
#include "umay_str.h"
#include "umay_ortak.h"

#define YANIT_AZAMI  (64 * 1024)
#define ISTEK_AZAMI  (16 * 1024)

static char g_model[128];
static char g_adres[128] = "127.0.0.1:11434";

/* --- en küçük HTTP istemcisi --- */
static int baglan(void)
{
    char ev[128];
    int kapi = 11434;
    snprintf(ev, sizeof ev, "%s", g_adres);
    char *iki = strrchr(ev, ':');
    if (iki) { *iki = 0; kapi = atoi(iki + 1); }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)kapi);
    if (inet_pton(AF_INET, ev[0] ? ev : "127.0.0.1", &a.sin_addr) != 1) {
        close(s); return -1;
    }
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
    int bir = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &bir, sizeof bir);
    return s;
}

/* İstek yollar, YANIT GÖVDESİNİ döndürür. 0 = oldu. */
static int http(const char *yontem, const char *yol, const char *govde,
                char *cikti, uint32_t n)
{
    int s = baglan();
    if (s < 0) return -1;

    char baslik[512];
    int bl;
    if (govde)
        bl = snprintf(baslik, sizeof baslik,
                      "%s %s HTTP/1.1\r\nHost: %s\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                      yontem, yol, g_adres, strlen(govde));
    else
        bl = snprintf(baslik, sizeof baslik,
                      "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                      yontem, yol, g_adres);
    if (bl <= 0 || write(s, baslik, (size_t)bl) != bl) { close(s); return -1; }
    if (govde) {
        size_t gl = strlen(govde);
        if (write(s, govde, gl) != (ssize_t)gl) { close(s); return -1; }
    }

    static char tampon[YANIT_AZAMI];
    size_t o = 0;
    ssize_t k;
    while (o + 1 < sizeof tampon &&
           (k = read(s, tampon + o, sizeof tampon - o - 1)) > 0)
        o += (size_t)k;
    close(s);
    tampon[o] = 0;
    if (o == 0) return -1;

    /* durum satırı */
    if (strncmp(tampon, "HTTP/1.", 7) != 0) return -1;
    if (strstr(tampon, " 200 ") == 0 || strstr(tampon, " 200 ") > strchr(tampon, '\r'))
        if (strncmp(tampon + 9, "200", 3) != 0) return -1;

    char *bos = strstr(tampon, "\r\n\r\n");
    if (!bos) return -1;
    snprintf(cikti, n, "%s", bos + 4);
    return 0;
}

/* JSON dizgesinden bir alanın değerini çeker (kaçışlar çözülür). */
static int json_dize(const char *json, const char *alan, char *c, uint32_t n)
{
    char ara[64];
    snprintf(ara, sizeof ara, "\"%s\"", alan);
    const char *p = strstr(json, ara);
    if (!p) return -1;
    p = strchr(p + strlen(ara), ':');
    if (!p) return -1;
    while (*p && *p != '"') p++;
    if (*p != '"') return -1;
    p++;
    uint32_t o = 0;
    while (*p && *p != '"' && o + 1 < n) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': c[o++] = '\n'; break;
            case 't': c[o++] = '\t'; break;
            case 'r': break;
            case 'u': {
                /* \uXXXX: yalnız Latin-1 aralığı; gerisi atlanır */
                unsigned v = 0;
                for (int i = 1; i <= 4 && p[i]; i++) {
                    char h = p[i];
                    v = v * 16 + (unsigned)((h >= '0' && h <= '9') ? h - '0' :
                                 (h | 32) - 'a' + 10);
                }
                p += 4;
                if (v < 0x80) c[o++] = (char)v;
                else if (v < 0x800 && o + 2 < n) {
                    c[o++] = (char)(0xC0 | (v >> 6));
                    c[o++] = (char)(0x80 | (v & 0x3F));
                } else if (o + 3 < n) {
                    c[o++] = (char)(0xE0 | (v >> 12));
                    c[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                    c[o++] = (char)(0x80 | (v & 0x3F));
                }
                break;
            }
            default: c[o++] = *p; break;
            }
            p++;
        } else {
            c[o++] = *p++;
        }
    }
    c[o] = 0;
    return 0;
}

/* JSON dizge kaçışı (istem gövdesi için). */
static void json_kacir(const char *s, char *c, uint32_t n)
{
    uint32_t o = 0;
    for (; s && *s && o + 7 < n; s++) {
        unsigned char u = (unsigned char)*s;
        if (u == '"' || u == '\\') { c[o++] = '\\'; c[o++] = (char)u; }
        else if (u == '\n') { c[o++] = '\\'; c[o++] = 'n'; }
        else if (u == '\t') { c[o++] = '\\'; c[o++] = 't'; }
        else if (u < 0x20)  { o += (uint32_t)snprintf(c + o, n - o, "\\u%04x", u); }
        else c[o++] = (char)u;
    }
    c[o] = 0;
}

static int model_sec(void)
{
    static char yanit[YANIT_AZAMI];
    if (http("GET", "/api/tags", 0, yanit, sizeof yanit) != 0) return UM_YOK;

    const char *istek = getenv("UMAY_OLLAMA_MODEL");
    if (istek && *istek) {
        char ara[160];
        snprintf(ara, sizeof ara, "\"%s", istek);
        if (!strstr(yanit, ara)) {
            gun_yaz("US-OLLAMA: '%s' kurulu değil.", istek);
            return UM_YOK;
        }
        snprintf(g_model, sizeof g_model, "%s", istek);
        return UM_OLDU;
    }
    /* model verilmediyse: listedeki ilk modeli al */
    if (json_dize(yanit, "name", g_model, sizeof g_model) != 0 || !g_model[0]) {
        gun_yaz("US-OLLAMA: hiç model kurulu değil "
                "(ollama pull <model> ile kurun).");
        return UM_YOK;
    }
    return UM_OLDU;
}

static int ollama_hazirla(umay_us_t *u, const char *model_yolu)
{
    (void)u; (void)model_yolu;
    const char *a = getenv("UMAY_OLLAMA");
    if (a && *a) snprintf(g_adres, sizeof g_adres, "%s", a);

    if (model_sec() != UM_OLDU) return UM_YOK;
    gun_yaz("US-OLLAMA: %s üzerinde '%s' hazır.", g_adres, g_model);
    return UM_OLDU;
}

static int ollama_uret(umay_us_t *u, const us_istem_t *i, char *cikti, uint32_t n)
{
    (void)u;
    if (!i || !cikti || n == 0) return UM_YANLIS;

    static char istem[ISTEK_AZAMI];
    static char kacirilmis[ISTEK_AZAMI];
    static char govde[ISTEK_AZAMI + 512];
    static char yanit[YANIT_AZAMI];

    snprintf(istem, sizeof istem,
             "%s\n\nDAYANAK:\n%s\n\nSORU: %s\n\n"
             "Yalnız yukarıdaki dayanağa göre, Öz Türkçe ve kısa yanıtla. "
             "Dayanak yetmiyorsa bunu açıkça söyle.",
             i->kisilik ? i->kisilik : "",
             i->baglam  ? i->baglam  : "(yok)",
             i->soru    ? i->soru    : "");
    json_kacir(istem, kacirilmis, sizeof kacirilmis);

    uint32_t im = i->en_cok_im ? i->en_cok_im : 256;
    snprintf(govde, sizeof govde,
             "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false,"
             "\"options\":{\"num_predict\":%u,\"temperature\":0.3}}",
             g_model, kacirilmis, im);

    if (http("POST", "/api/generate", govde, yanit, sizeof yanit) != 0) {
        gun_yaz("US-OLLAMA: istek başarısız.");
        return UM_YOK;
    }
    char metin[US_METIN_MAX];
    if (json_dize(yanit, "response", metin, sizeof metin) != 0) {
        gun_yaz("US-OLLAMA: yanıt çözümlenemedi.");
        return UM_YOK;
    }
    /* Boş yanıt BAŞARI DEĞİLDİR: üst katman stub'a düşsün. */
    const char *p = metin;
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;
    if (!*p) {
        gun_yaz("US-OLLAMA: boş yanıt.");
        return UM_YOK;
    }
    um_kopya(cikti, n, p);
    return UM_OLDU;
}

static umay_us_t g_ollama = {
    .ad = "ollama",
    .yetenek = 0,
    .hazirla = ollama_hazirla,
    .uret = ollama_uret,
    .kapat = 0,
    .ic = 0
};

umay_us_t *us_ollama_motor(void) { return &g_ollama; }

#endif /* !UMAY_OTUKEN */
