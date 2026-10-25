/* =====================================================================
 * ARAYICI — UMAY'ın ağ araştırmacısı (1.5)
 *
 * Ayrı bir Ring3 süreci. Yetkisi: NET_SEND | NET_RECV | IPC | DISK_READ |
 * DISK_WRITE (yalnız /KUT/UMAY/AG altına, TÖRE yazma kökü) | MEM_ALLOC.
 * Ekran ve klavye YOK. UMAY ham web sayfasını hiç işlemez: HTML'i burada
 * okunur metne çeviririz, UMAY yalnız bizim yazdığımız düz belgeyi okur.
 *
 * Akış (IPC kapısı "arayici"):
 *   istek : "<yanit_kapisi>\n<soru terimleri>"   (çok parçalı olabilir)
 *   1) arama sağlayıcısına HTTPS GET (öntanımlı DuckDuckGo lite)
 *   2) sonuç bağlantıları (uddg= çözülür), en çok N sayfa
 *   3) her sayfa: HTTPS ile indir (sertifika denetimi tls.c), HTML -> metin
 *   4) soru terimlerini taşıyan cümleler seçilir (kök eşleme), belge
 *      /KUT/UMAY/AG/Wxxxxxxx.TXT olarak yazılır (#kategori: ag, #kaynak: URL)
 *   5) yanıt: "T <adet> <ad1> <ad2> ..." ya da "H <neden>"
 *
 * Ayar: /ARAYICI.CFG (isteğe bağlı)
 *   saglayici=https://lite.duckduckgo.com/lite/?q=
 *   sayfa=3
 *   (yalnız ARAYICI_SINAMA derlemesinde) esle=<ad> <a.b.c.d>
 * ===================================================================== */
#include "http.h"
#include "html.h"

#define SYS_WRITE_FILE  25
#define SYS_MKDIR       27
#define SYS_DOSYA_EKLE  54
#define SYS_DOSYA_SIL   55

#define GOVDE_AZAMI   (512 * 1024)
#define SONUC_AZAMI   8
#define BELGE_AZAMI   2900          /* UMAY'ın kayıt içeriği 3072 bayt */
#define TERIM_AZAMI   6

#ifdef ARAYICI_SINAMA
#include "bearssl.h"
extern const br_x509_trust_anchor SINAMA_KOKLER[1];
void tls_kokleri_ayarla(const br_x509_trust_anchor *k, unsigned n);
#endif

static uint8_u govde[GOVDE_AZAMI];
static sayfa_t sayfa;
static http_yanit_t yanit;
static char metin[160 * 1024];      /* sayfanın düz metni */
static char belge[BELGE_AZAMI + 600];
static char saglayici[512] = "https://lite.duckduckgo.com/lite/?q=";
static int  sayfa_sayisi = 3;

/* ---------------- küçük yardımcılar ---------------- */
static int uzun(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void kopya(char *h, int n, const char *k) { int i = 0; for (; k[i] && i < n - 1; i++) h[i] = k[i]; h[i] = 0; }
static void ekle(char *h, int n, const char *k) { int u = uzun(h); kopya(h + u, n - u, k); }
static void sayi(char *c, uint64_u v) {
    char t[24]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    int j = 0; while (i) c[j++] = t[--i]; c[j] = 0;
}
static void hex8(char *c, uint32_u v) {
    static const char H[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) { c[i] = H[v & 15]; v >>= 4; }
    c[8] = 0;
}
static uint32_u fnv(const char *s) {
    uint32_u h = 0x811C9DC5u;
    while (*s) { h ^= (uint8_u)*s++; h *= 0x01000193u; }
    return h;
}
/* Günlük /KUT/UMAY/AG/GUNLUK.TXT'ye: ARAYICI'nın ekran (konsol) yetkisi
 * YOK, yazabildiği tek yer kendi rafı. */
static void gunluk(const char *a, const char *b) {
    char s[600]; kopya(s, sizeof s, "ARAYICI: "); ekle(s, sizeof s, a);
    if (b) ekle(s, sizeof s, b);
    ekle(s, sizeof s, "\n");
    syscall(SYS_DOSYA_EKLE, (long)"/KUT/UMAY/AG/GUNLUK.TXT", (long)s, uzun(s), 0);
}

/* Türkçe küçük harf (UTF-8 -> UTF-8). I -> ı, İ -> i. */
static int kucult(const char *k, char *h, int n) {
    int o = 0;
    for (int i = 0; k[i] && o < n - 3; ) {
        uint8_u a = (uint8_u)k[i], b = (uint8_u)k[i + 1];
        if (a == 'I') { h[o++] = (char)0xC4; h[o++] = (char)0xB1; i++; continue; }
        if (a >= 'A' && a <= 'Z') { h[o++] = (char)(a + 32); i++; continue; }
        if (a == 0xC4 && b == 0xB0) { h[o++] = 'i'; i += 2; continue; }               /* İ */
        if (a == 0xC3 && (b == 0x87 || b == 0x96 || b == 0x9C)) { h[o++] = (char)a; h[o++] = (char)(b + 0x20); i += 2; continue; } /* Ç Ö Ü */
        if ((a == 0xC4 && b == 0x9E) || (a == 0xC5 && b == 0x9E)) { h[o++] = (char)a; h[o++] = (char)(b + 1); i += 2; continue; } /* Ğ Ş */
        h[o++] = k[i++];
    }
    h[o] = 0;
    return o;
}
/* sözcük karakteri: harf, rakam ya da UTF-8 devam/başlangıç baytı */
static int harf(uint8_u c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80; }

/* ---------------- terimler ---------------- */
static char terim[TERIM_AZAMI][40];
static int  terim_say;
static const char *DURAK[] = {
    "araştır", "araştırma", "arastir", "hakkında", "nedir", "nasıl", "neden", "için", "ile", "ve",
    "bir", "bu", "şu", "da", "de", "mi", "mı", "ne", "kaynak", "bul", "tara", "derle", "bilgi", "ver", 0
};
static int durak_mi(const char *s) {
    for (int i = 0; DURAK[i]; i++) { const char *a = DURAK[i]; int j = 0; while (a[j] && a[j] == s[j]) j++; if (!a[j] && !s[j]) return 1; }
    return 0;
}
/* kök: ilk 5 kod noktası (sarnıçlar ~ sarnıç) */
static void kok(const char *s, char *c, int n) {
    int o = 0, kn = 0;
    for (int i = 0; s[i] && o < n - 1; i++) {
        if (((uint8_u)s[i] & 0xC0) != 0x80) { if (kn == 5) break; kn++; }
        c[o++] = s[i];
    }
    c[o] = 0;
}
static void terimleri_kur(const char *soru) {
    static char k[512]; kucult(soru, k, sizeof k);
    terim_say = 0;
    for (int i = 0; k[i] && terim_say < TERIM_AZAMI; ) {
        while (k[i] && !harf((uint8_u)k[i])) i++;
        char w[40]; int o = 0;
        while (k[i] && harf((uint8_u)k[i])) { if (o < 39) w[o++] = k[i]; i++; }
        w[o] = 0;
        if (o >= 3 && !durak_mi(w)) { kok(w, terim[terim_say], 40); terim_say++; }
    }
}
/* metin (küçük harfli) içinde terimin sözcük başında geçmesi */
static int geciyor(const char *k, const char *t) {
    int tn = uzun(t);
    for (int i = 0; k[i]; i++) {
        if (i > 0 && harf((uint8_u)k[i - 1])) continue;
        int j = 0; while (j < tn && k[i + j] == t[j]) j++;
        if (j == tn) return 1;
    }
    return 0;
}

/* ---------------- URL ---------------- */
static void url_kodla(const char *s, char *c, int n) {
    static const char H[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; s[i] && o < n - 4; i++) {
        uint8_u b = (uint8_u)s[i];
        if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '-' || b == '_' || b == '.') c[o++] = (char)b;
        else if (b == ' ') c[o++] = '+';
        else { c[o++] = '%'; c[o++] = H[b >> 4]; c[o++] = H[b & 15]; }
    }
    c[o] = 0;
}
static int hexd(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void yuzde_coz(const char *s, int n, char *c, int cn) {
    int o = 0;
    for (int i = 0; i < n && s[i] && o < cn - 1; i++) {
        if (s[i] == '%' && i + 2 < n && hexd(s[i + 1]) >= 0 && hexd(s[i + 2]) >= 0) { c[o++] = (char)(hexd(s[i + 1]) * 16 + hexd(s[i + 2])); i += 2; }
        else if (s[i] == '+') c[o++] = ' ';
        else c[o++] = s[i];
    }
    c[o] = 0;
}
static int ayni_sunucu(const char *a, const char *b) {
    url_t x, y;
    if (url_coz(a, &x) != 0 || url_coz(b, &y) != 0) return 0;
    for (int i = 0; ; i++) { char p = x.host[i], q = y.host[i]; if (p != q) return 0; if (!p) break; }
    return 1;
}

/* Arama sonucu sayfasından hedef bağlantılar. DuckDuckGo: href içinde
 * uddg=<yüzde kodlu hedef>. Diğer sağlayıcılar: sağlayıcının kendisine
 * gitmeyen mutlak http(s) bağlantıları. */
static char sonuc[SONUC_AZAMI][1024];
static int  sonuc_say;
static void sonuclari_bul(const char *g, uint32_u n, const char *sag) {
    sonuc_say = 0;
    for (uint32_u i = 0; i + 6 < n && sonuc_say < sayfa_sayisi && sonuc_say < SONUC_AZAMI; i++) {
        if (!(g[i] == 'h' && g[i + 1] == 'r' && g[i + 2] == 'e' && g[i + 3] == 'f' && g[i + 4] == '=')) continue;
        char tirnak = g[i + 5];
        if (tirnak != '"' && tirnak != '\'') continue;
        uint32_u b = i + 6, s = b;
        while (s < n && g[s] != tirnak && s - b < 2000) s++;
        static char ham[2048], hedef[1024];
        int hn = 0;
        for (uint32_u k = b; k < s && hn < 2047; k++) {           /* &amp; -> & */
            if (g[k] == '&' && k + 4 < s && g[k + 1] == 'a' && g[k + 2] == 'm' && g[k + 3] == 'p' && g[k + 4] == ';') { ham[hn++] = '&'; k += 4; }
            else ham[hn++] = g[k];
        }
        ham[hn] = 0;
        hedef[0] = 0;
        const char *u = 0;
        for (int k = 0; ham[k]; k++) if (ham[k] == 'u' && ham[k + 1] == 'd' && ham[k + 2] == 'd' && ham[k + 3] == 'g' && ham[k + 4] == '=') { u = ham + k + 5; break; }
        if (u) {
            int un = 0; while (u[un] && u[un] != '&') un++;
            yuzde_coz(u, un, hedef, sizeof hedef);
        } else if ((ham[0] == 'h' && ham[1] == 't' && ham[2] == 't' && ham[3] == 'p')) {
            if (ayni_sunucu(ham, sag)) continue;
            kopya(hedef, sizeof hedef, ham);
        }
        if (!hedef[0] || !(hedef[0] == 'h' && hedef[1] == 't' && hedef[2] == 't' && hedef[3] == 'p')) continue;
        if (ayni_sunucu(hedef, sag)) continue;
        int var = 0;
        for (int q = 0; q < sonuc_say; q++) { int a = 0; while (sonuc[q][a] && sonuc[q][a] == hedef[a]) a++; if (!sonuc[q][a] && !hedef[a]) var = 1; }
        if (!var) kopya(sonuc[sonuc_say++], 1024, hedef);
        i = s;
    }
}

/* ---------------- sayfa -> düz metin ---------------- */
static int duz_metin_kur(void) {
    int o = 0;
    for (int r = 0; r < sayfa.satir_adet && o < (int)sizeof metin - 8; r++) {
        uint32_u bas = sayfa.satir[r], son = sayfa.satir[r + 1];
        int bos = 1;
        for (uint32_u k = bas; k < son && o < (int)sizeof metin - 8; k++) {
            uint32_u kn = H_KOD(sayfa.kar[k]);
            if (kn == '\n' || kn == 0) continue;
            if (kn != ' ') bos = 0;
            o += utf8_yaz(kn, metin + o);
        }
        /* boş satır = paragraf sonu; dolu satır sonu = boşluk */
        metin[o++] = bos ? '\n' : ' ';
    }
    metin[o] = 0;
    return o;
}

/* cümleleri puanla, en iyileri sırasıyla belgeye yaz */
#define CUMLE_AZAMI 1500
static int cb[CUMLE_AZAMI], cs[CUMLE_AZAMI], cp[CUMLE_AZAMI];
static int cumleleri_sec(char *c, int cn) {
    int n = 0, i = 0, top = 0;
    static char kk[1200];
    while (metin[i] && n < CUMLE_AZAMI) {
        while (metin[i] == ' ' || metin[i] == '\n') i++;
        int b = i;
        while (metin[i] && metin[i] != '\n' &&
               !((metin[i] == '.' || metin[i] == '!' || metin[i] == '?') && (metin[i + 1] == ' ' || metin[i + 1] == '\n' || !metin[i + 1]))) i++;
        if (metin[i] && metin[i] != '\n') i++;          /* noktalama cümlede kalır */
        int u = i - b;
        if (u < 40 || u > 600) { if (metin[i]) i++; continue; }
        int kn = u < (int)sizeof kk - 1 ? u : (int)sizeof kk - 1;
        static char ham[1200]; for (int q = 0; q < kn; q++) ham[q] = metin[b + q]; ham[kn] = 0;
        kucult(ham, kk, sizeof kk);
        int p = 0;
        for (int t = 0; t < terim_say; t++) if (geciyor(kk, terim[t])) p++;
        if (p > 0) { cb[n] = b; cs[n] = u; cp[n] = p; n++; }
    }
    /* en yüksek puanlıları seç (en çok 10), sonra metindeki sıralarıyla yaz */
    static int sec[CUMLE_AZAMI];
    for (int q = 0; q < n; q++) sec[q] = 0;
    int secilen = 0, boy = 0;
    for (int tur = terim_say; tur >= 1 && secilen < 10; tur--)
        for (int q = 0; q < n && secilen < 10; q++)
            if (!sec[q] && cp[q] == tur && boy + cs[q] + 2 < cn) { sec[q] = 1; secilen++; boy += cs[q] + 2; }
    c[0] = 0;
    for (int q = 0; q < n; q++) if (sec[q]) {
        int o = uzun(c);
        for (int z = 0; z < cs[q] && o < cn - 2; z++) { char ch = metin[cb[q] + z]; c[o++] = (ch == '\n') ? ' ' : ch; }
        c[o++] = '\n'; c[o] = 0; top++;
    }
    return top;
}

/* ---------------- belge yazımı ---------------- */
static void dizinleri_ac(void) {
    syscall(SYS_MKDIR, (long)"/KUT", 0, 0, 0);
    syscall(SYS_MKDIR, (long)"/KUT/UMAY", 0, 0, 0);
    syscall(SYS_MKDIR, (long)"/KUT/UMAY/AG", 0, 0, 0);
}
static int var_mi(const char *yol) { int fd = open(yol); if (fd < 0) return 0; close(fd); return 1; }

/* belge yazar; dosya adı (8.3) ad'a */
static int belge_yaz(const char *url, const char *baslik, const char *cumleler, int kismi, char ad[16]) {
    char h[9]; hex8(h, fnv(url));
    ad[0] = 'W'; for (int i = 0; i < 7; i++) ad[i + 1] = h[i + 1]; ad[8] = 0;
    ekle(ad, 16, ".TXT");
    char yol[64]; kopya(yol, sizeof yol, "/KUT/UMAY/AG/"); ekle(yol, sizeof yol, ad);
    int yeni = !var_mi(yol);
    char et[200]; et[0] = 0;
    for (int t = 0; t < terim_say; t++) { if (t) ekle(et, sizeof et, ","); ekle(et, sizeof et, terim[t]); }
    char bas[90]; kopya(bas, sizeof bas, baslik[0] ? baslik : url);
    for (int i = 0; bas[i]; i++) if (bas[i] == '\n' || bas[i] == '\r') bas[i] = ' ';
    /* tampona sığmayan sayfa: kanıt yalnız sayfanın BAŞINDAN; başlıkta söylenir */
    if (kismi) { if (uzun(bas) > 60) bas[60] = 0; ekle(bas, sizeof bas, " (sayfanin yalniz basi)"); }
    char kay[90]; kopya(kay, sizeof kay, url);
    belge[0] = 0;
    ekle(belge, sizeof belge, "#kategori: ag\n#baslik: "); ekle(belge, sizeof belge, bas);
    ekle(belge, sizeof belge, "\n#etiket: "); ekle(belge, sizeof belge, et);
    ekle(belge, sizeof belge, "\n#kaynak: "); ekle(belge, sizeof belge, kay);
    if (kismi) ekle(belge, sizeof belge, "\n#kismi: evet");
    ekle(belge, sizeof belge, "\n\n"); ekle(belge, sizeof belge, cumleler);
    int n = uzun(belge); if (n > 4000) n = 4000;
    if (syscall(SYS_WRITE_FILE, (long)yol, (long)belge, n, 0) < 0) { gunluk("belge YAZILAMADI: ", yol); return -1; }
    if (yeni) {
        char s[24]; kopya(s, sizeof s, ad); ekle(s, sizeof s, "\n");
        if (syscall(SYS_DOSYA_EKLE, (long)"/KUT/UMAY/AG/DIZIN.TXT", (long)s, uzun(s), 0) != uzun(s)) {
            /* dizine girmeyen belge açılışta yüklenmez; belgeyi geri al ki
             * aynı adresin sonraki getirilişi onu YENİ sayıp dizini onarsın */
            gunluk("DIZIN.TXT'e eklenemedi, belge geri alindi: ", yol);
            syscall(SYS_DOSYA_SIL, (long)yol, 0, 0, 0);
            return -1;
        }
    }
    return 0;
}

/* ---------------- ayar ---------------- */
static void ayar_oku(void) {
    int fd = open("/ARAYICI.CFG");
    if (fd < 0) return;
    static char a[2048]; int n = (int)read(fd, a, sizeof a - 1); close(fd);
    if (n <= 0) return;
    a[n] = 0;
    for (char *p = a; *p; ) {
        char *s = p; while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        int u = uzun(s); while (u > 0 && (s[u - 1] == '\r' || s[u - 1] == ' ')) s[--u] = 0;
        if (s[0] == 's' && s[1] == 'a' && s[2] == 'g' && s[9] == '=' ) kopya(saglayici, sizeof saglayici, s + 10);
        else if (s[0] == 's' && s[1] == 'a' && s[2] == 'y' && s[5] == '=') { int v = 0; for (char *q = s + 6; *q >= '0' && *q <= '9'; q++) v = v * 10 + (*q - '0'); if (v >= 1 && v <= SONUC_AZAMI) sayfa_sayisi = v; }
#ifdef ARAYICI_SINAMA
        else if (s[0] == 'e' && s[1] == 's' && s[2] == 'l' && s[3] == 'e' && s[4] == '=') {
            char *ad = s + 5, *ip = ad; while (*ip && *ip != ' ') ip++;
            if (*ip) { *ip++ = 0; uint8_u b[4] = {0}; int k = 0, v = 0;
                for (; *ip && k < 4; ip++) { if (*ip == '.') { b[k++] = (uint8_u)v; v = 0; } else v = v * 10 + (*ip - '0'); }
                if (k == 3) { b[3] = (uint8_u)v; http_ad_esle(ad, b); gunluk("sinama ad eslemesi: ", ad); } }
        }
#endif
    }
}

/* ---------------- bir araştırma ---------------- */
static char adlar[SONUC_AZAMI][16];
static int ek_adet;            /* son araştırmada yazılan belge sayısı */
static void arastir(const char *soru, char *yanit_c, int yn) {
    terimleri_kur(soru);
    if (terim_say == 0) { kopya(yanit_c, yn, "H aranacak terim yok"); return; }
    char q[400], kod[1200], url[1800];
    q[0] = 0;
    for (int t = 0; t < terim_say; t++) { if (t) ekle(q, sizeof q, " "); ekle(q, sizeof q, terim[t]); }
    /* sorguya terimlerin kökleri değil, sorunun kendisi gider */
    url_kodla(soru, kod, sizeof kod);
    kopya(url, sizeof url, saglayici); ekle(url, sizeof url, kod);
    gunluk("arama: ", url);
    if (http_al(url, &yanit, govde, sizeof govde - 1, 0, 0) != 0 || yanit.durum != 200) {
        gunluk("arama BASARISIZ: ", yanit.hata[0] ? yanit.hata : "HTTP durumu 200 degil");
        kopya(yanit_c, yn, "H arama basarisiz: "); ekle(yanit_c, yn, yanit.hata);
        return;
    }
    govde[yanit.govde_boy] = 0;
    sonuclari_bul((const char *)govde, yanit.govde_boy, saglayici);
    { char s[16]; sayi(s, (uint64_u)sonuc_say); gunluk("sonuc baglantisi: ", s); }
    dizinleri_ac();
    kopya(yanit_c, yn, "T ");
    int adet = 0;
    ek_adet = 0;
    for (int r = 0; r < sonuc_say; r++) {
        gunluk("indiriliyor: ", sonuc[r]);
        if (http_al(sonuc[r], &yanit, govde, sizeof govde - 1, 0, 0) != 0) {
            gunluk("REDDEDILDI/BASARISIZ: ", yanit.hata);
            continue;
        }
        if (yanit.durum != 200) { gunluk("HTTP durumu 200 degil: ", sonuc[r]); continue; }
        int html = 1;
        { const char *t = yanit.tur; for (int i = 0; t[i]; i++) if (t[i] == 'p' && t[i+1] == 'l' && t[i+2] == 'a' && t[i+3] == 'i' && t[i+4] == 'n') html = 0; }
        int kk = html_kodlama_ad(yanit.tur);
        if (kk < 0 && html) kk = html_kodlama_bul(govde, yanit.govde_boy);
        html_isle(govde, yanit.govde_boy, kk < 0 ? KK_UTF8 : kk, !html, 200, &sayfa);
        int mn = duz_metin_kur();
        int kismi = yanit.kesik;
        if (kismi) {
            /* gövde tampona sığmadı: son tam cümleden sonrası (yarım kalan
             * parça) kanıt olamaz -> kesilir */
            while (mn > 0 && (metin[mn - 1] == ' ' || metin[mn - 1] == '\n')) mn--;   /* sondaki boşluk */
            while (mn > 0 && metin[mn - 1] != '.' && metin[mn - 1] != '!' && metin[mn - 1] != '?' &&
                   metin[mn - 1] != '\n') mn--;                            /* yarım son parça */
            metin[mn] = 0;
            gunluk("sayfa tampona sigmadi, yalniz basi kullanildi: ", sonuc[r]);
        }
        static char cumleler[BELGE_AZAMI];
        int c = cumleleri_sec(cumleler, BELGE_AZAMI - 400);
        if (c == 0) { gunluk("ilgili cumle yok: ", sonuc[r]); continue; }
        if (belge_yaz(yanit.son_url[0] ? yanit.son_url : sonuc[r], sayfa.baslik, cumleler, kismi, adlar[adet]) == 0) {
            char s[64]; sayi(s, (uint64_u)c); ekle(s, sizeof s, " cumle -> "); ekle(s, sizeof s, adlar[adet]);
            gunluk("belge: ", s);
            /* son yanıt tek IPC iletisi (96 bayt): en çok 5 ad; 6.-8. adlar
             * önceden "<kimlik> E ad..." ek iletileriyle gider (arastir sonrası) */
            if (adet < 5) { ekle(yanit_c, yn, adlar[adet]); ekle(yanit_c, yn, " "); }
            adet++;
        }
    }
    ek_adet = adet;
    if (adet == 0) { kopya(yanit_c, yn, "H ilgili sayfa bulunamadi"); return; }
    /* "T " + adlar -> "T <adet> ad..." */
    char s[96]; kopya(s, sizeof s, "T "); { char t[8]; sayi(t, (uint64_u)adet); ekle(s, sizeof s, t); }
    ekle(s, sizeof s, " "); ekle(s, sizeof s, yanit_c + 2);
    kopya(yanit_c, yn, s);
}

int main(void) {
    dizinleri_ac();
#ifdef ARAYICI_SINAMA
    tls_kokleri_ayarla(SINAMA_KOKLER, 1);
    gunluk("SINAMA derlemesi: guven koku sinama koku", 0);
#endif
    ayar_oku();
    int kapi = ipc_ac("arayici");
    if (kapi < 0) { gunluk("IPC kapisi acilamadi", 0); exit(); }
    gunluk("hazir, saglayici=", saglayici);
    static char istek[1024];
    int dolu = 0;
    for (;;) {
        ipc_mesaj_u m;
        if (ipc_al(kapi, &m) != 1) { sleep_ticks(5); continue; }
        for (uint32_u i = 0; i < m.length && dolu < (int)sizeof istek - 1; i++) istek[dolu++] = (char)m.payload[i];
        if (m.opcode != 0) continue;                     /* devamı gelecek */
        istek[dolu] = 0; dolu = 0;
        char *nl = istek; while (*nl && *nl != '\n') nl++;
        if (!*nl) { gunluk("bicimsiz istek", 0); continue; }
        *nl = 0;
        /* ilk satır: "<yanit_kapisi> <istek_kimligi>"; yanıt kimlikle başlar */
        char *kime = istek, *kimlik = istek;
        while (*kimlik && *kimlik != ' ') kimlik++;
        if (*kimlik) *kimlik++ = 0;
        const char *soru = nl + 1;
        gunluk("istek: ", soru);
        static char y[96], ty[112];
        arastir(soru, y, sizeof y);
        kopya(ty, sizeof ty, kimlik[0] ? kimlik : "0"); ekle(ty, sizeof ty, " "); ekle(ty, sizeof ty, y);
        if (uzun(ty) > 95) ty[95] = 0;                    /* tek IPC iletisi */
        gunluk("yanit: ", ty);
        int h = ipc_bul(kime);
        if (h >= 0) {
            /* 5'ten fazla belge: kalan adlar ÖNCE ek iletilerle ("<kimlik> E ad...");
             * UMAY bunları canlı rafa yükler, sonra son "T" iletisini işler */
            for (int i = 5; i < ek_adet; ) {
                static char ek[112];
                kopya(ek, sizeof ek, kimlik[0] ? kimlik : "0"); ekle(ek, sizeof ek, " E");
                for (; i < ek_adet && uzun(ek) + 14 < 95; i++) { ekle(ek, sizeof ek, " "); ekle(ek, sizeof ek, adlar[i]); }
                gunluk("ek yanit: ", ek);
                ipc_gonder(h, 0, ek, (uint32_u)uzun(ek));
            }
            ipc_gonder(h, 0, ty, (uint32_u)uzun(ty));
        } else gunluk("yanit kapisi yok: ", kime);
    }
}
