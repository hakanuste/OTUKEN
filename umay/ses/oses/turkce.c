/* =====================================================================
 * Türkçe yazı -> ses işaretleri (modelin fonem kimlikleri).
 *
 * Model espeak-ng'nin Türkçe çıktısıyla eğitilmiş. Bu dosya espeak'i
 * içermez: kurallar espeak'in 62 bin sözcükteki çıktısından öğrenilip
 * C tablosuna dönüştürüldü (arac/turkce_uret.py -> turkce_tablo.c):
 *   - harf kuralları: harfin çevresine bakan bağlam pencereleri, özelden
 *     genele; tutmayan sözcükler için tam ses dizisi istisnası
 *   - vurgu: sözcük sonu kuralları + istisna listesi; vurgu işareti (ˈ)
 *     vurgulu ünlünün önüne konur
 * Ayrılmış sınama kümesinde (görülmemiş 9309 sözcük) ses dizisi %95,7,
 * vurgu %95,8 espeak ile aynı.
 * Sayılar sözcüğe çevrilir; ünlüsüz kısaltmalar harf harf okunur.
 * ===================================================================== */
#include "turkce.h"
#include "turkce_tablo.h"

/* --- küçük dize yardımcıları --- */
static int tt_karsilastir(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static const char *tt_bul(const tt_kayit_t *t, int n, const char *anahtar) {
    int a = 0, b = n - 1;
    while (a <= b) {
        int o = (a + b) / 2, k = tt_karsilastir(t[o].a, anahtar);
        if (k == 0) return t[o].d;
        if (k < 0) a = o + 1; else b = o - 1;
    }
    return 0;
}

/* UTF-8 çöz: bir kod noktası; *s ilerler */
static uint32_t u8_al(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = p[0];
    int n = 1;
    if (c < 0x80) n = 1;
    else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (p[1] & 0x3F); n = 2; }
    else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); n = 3; }
    else { c = 0xFFFD; n = 1; }
    *s += n;
    return c;
}
static int u8_yaz(uint32_t c, char *o) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3;
}

/* Türkçe küçük harf; Türkçe harf değilse 0 */
static uint32_t harf_kucuk(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c;
    if (c >= 'A' && c <= 'Z') return c == 'I' ? 0x131 : c + 32;
    switch (c) {
    case 0xE7: case 0xC7: return 0xE7;            /* ç */
    case 0x11F: case 0x11E: return 0x11F;         /* ğ */
    case 0x131: return 0x131;                     /* ı */
    case 0x130: return 'i';                       /* İ */
    case 0xF6: case 0xD6: return 0xF6;            /* ö */
    case 0x15F: case 0x15E: return 0x15F;         /* ş */
    case 0xFC: case 0xDC: return 0xFC;            /* ü */
    case 0xE2: case 0xC2: return 'a';             /* â */
    case 0xEE: case 0xCE: return 'i';             /* î */
    case 0xFB: case 0xDB: return 'u';             /* û */
    }
    return 0;
}
static int sesli(uint32_t c) { return c == 'a' || c == 'e' || c == 0x131 || c == 'i' || c == 'o' || c == 0xF6 || c == 'u' || c == 0xFC; }

/* --- çıktı: fonem kimlikleri --- */
typedef struct { int64_t *ids; int n, azami; int tasma; } cikti_t;
static void id_ekle(cikti_t *c, int64_t id) { if (c->n < c->azami) c->ids[c->n++] = id; else c->tasma = 1; }
/* bir ses işareti (tek kod noktası) -> kimlik + dolgu 0 */
static void isaret(cikti_t *c, uint32_t kp) {
    char b[5]; int n = u8_yaz(kp, b); b[n] = 0;
    const char *d = tt_bul(tt_kimlik, tt_kimlik_sayi, b);
    if (!d) return;                                /* modelde olmayan işaret: atla */
    int64_t v = 0; while (*d) v = v * 10 + (*d++ - '0');
    id_ekle(c, v); id_ekle(c, 0);
}
static void isaretler(cikti_t *c, const char *s) { while (*s) isaret(c, u8_al(&s)); }

/* --- sözcük -> ses --- */
#define AZ_HARF 48
static const int PENC[8][2] = { {-2,3}, {-1,3}, {-2,2}, {-1,2}, {0,3}, {-1,1}, {0,2}, {0,1} };

static void sozcuk_seslendir(cikti_t *c, const uint32_t *h, int n, int vurgusuz) {
    char anahtar[AZ_HARF * 3 + 8];
    /* sözcük dizesi (istisna aramaları için) */
    char soz[AZ_HARF * 3 + 4]; int k = 0;
    for (int i = 0; i < n; i++) k += u8_yaz(h[i], soz + k);
    soz[k] = 0;
    /* her harfin sesi */
    const char *ses[AZ_HARF]; char yedek[AZ_HARF][8];
    const char *ist = tt_bul(tt_ses_istisna, tt_ses_istisna_sayi, soz);
    if (ist) {
        /* "s1\x01s2\x01..." : harf başına bir ses (boş olabilir) */
        static char tampon[AZ_HARF * 8];
        int t = 0, i = 0; ses[0] = tampon;
        for (const char *p = ist; ; p++) {
            if (*p == 1 || *p == 0) { tampon[t++] = 0; if (++i >= n || !*p) break; ses[i] = tampon + t; }
            else tampon[t++] = *p;
        }
        for (; i < n; i++) ses[i] = "";
    } else {
        for (int i = 0; i < n; i++) {
            ses[i] = 0;
            for (int w = 0; w < 8 && !ses[i]; w++) {
                int a = PENC[w][0], b = PENC[w][1], q = 0;
                for (int j = i + a; j < i + b; j++) q += u8_yaz((j < 0 || j >= n) ? '#' : h[j], anahtar + q);
                anahtar[q++] = '|';
                if (a < 0) { anahtar[q++] = '-'; anahtar[q++] = (char)('0' - a); } else anahtar[q++] = (char)('0' + a);
                anahtar[q] = 0;
                ses[i] = tt_bul(tt_harf, tt_harf_sayi, anahtar);
            }
            if (!ses[i]) {                         /* tabloda olmayan harf (q, w, x...) */
                uint32_t x = h[i];
                int q = 0;
                if (x == 'q') x = 'k'; else if (x == 'w') x = 'v';
                if (x == 'x') { yedek[i][q++] = 'k'; yedek[i][q++] = 's'; }
                else q = u8_yaz(x, yedek[i]);
                yedek[i][q] = 0; ses[i] = yedek[i];
            }
        }
    }
    /* vurgu: sondan kaçıncı ünlü (-1 yok) */
    const char *vd = tt_bul(tt_vurgu_istisna, tt_vurgu_istisna_sayi, soz);
    int vurgu = 0;
    if (vd) { vurgu = vd[0] == '-' ? -1 : vd[0] - '0'; }
    else {
        vurgu = 0;
        for (int L = 6; L >= 1; L--) {
            int q = 0;
            for (int j = n - L; j < n; j++) q += u8_yaz(j < 0 ? '#' : h[j], anahtar + q);
            anahtar[q] = 0;
            const char *r = tt_bul(tt_vurgu_son, tt_vurgu_son_sayi, anahtar);
            if (r) { vurgu = r[0] == '-' ? -1 : r[0] - '0'; break; }
        }
    }
    int vurgu_harf = -1;
    if (vurgusuz) vurgu = -1;
    if (vurgu >= 0) {
        int say = 0;
        for (int i = n - 1; i >= 0; i--) if (sesli(h[i])) { if (say == vurgu) { vurgu_harf = i; break; } say++; }
    }
    for (int i = 0; i < n; i++) {
        if (i == vurgu_harf && ses[i][0]) isaret(c, 0x2C8);    /* ˈ */
        isaretler(c, ses[i]);
    }
}

/* --- sayılar: espeak'in sayı okunuşu tablosu (turkce_sayi.c) --- */
extern const char *const tt_sayi[1000];
extern const char tt_s_bin[], tt_s_milyon[], tt_s_milyar[], tt_s_nokta[], tt_s_virgul[], tt_s_yuzde[];
static void bosluk_yaz(cikti_t *c, int *bosluk) { if (*bosluk) isaret(c, ' '); *bosluk = 1; }
static void sayi_ses(cikti_t *c, uint64_t v, int *bosluk) {
    if (v >= 1000000000000ull) {                  /* çok büyük: üçerli gruplar ayrı ayrı */
        sayi_ses(c, v / 1000000000ull, bosluk); v %= 1000000000ull;
        if (!v) return;
    }
    if (v < 1000) { bosluk_yaz(c, bosluk); isaretler(c, tt_sayi[v]); return; }
    static const char *const BAS[] = { 0, tt_s_bin, tt_s_milyon, tt_s_milyar };
    int parca[4], np = 0;
    while (v && np < 4) { parca[np++] = (int)(v % 1000); v /= 1000; }
    for (int i = np - 1; i >= 0; i--) {
        if (!parca[i]) continue;
        if (!(i == 1 && parca[i] == 1)) { bosluk_yaz(c, bosluk); isaretler(c, tt_sayi[parca[i]]); }   /* "bin", "bir bin" değil */
        if (i) { bosluk_yaz(c, bosluk); isaretler(c, BAS[i]); }
    }
}

/* --- harf adları (ünlüsüz kısaltmalar) --- */
static const char *harf_adi(uint32_t c) {
    switch (c) {
    case 'b': return "be"; case 'c': return "ce"; case 0xE7: return "çe"; case 'd': return "de"; case 'f': return "fe";
    case 'g': return "ge"; case 0x11F: return "yumuşak ge"; case 'h': return "he"; case 'j': return "je"; case 'k': return "ke";
    case 'l': return "le"; case 'm': return "me"; case 'n': return "ne"; case 'p': return "pe"; case 'r': return "re";
    case 's': return "se"; case 0x15F: return "şe"; case 't': return "te"; case 'v': return "ve"; case 'y': return "ye";
    case 'z': return "ze"; case 'q': return "kü"; case 'w': return "dabılyu"; case 'x': return "iks";
    case 'a': return "a"; case 'e': return "e"; case 0x131: return "ı"; case 'i': return "i";
    case 'o': return "o"; case 0xF6: return "ö"; case 'u': return "u"; case 0xFC: return "ü";
    }
    return 0;
}

/* cümle içinde (ardından başka sözcük gelirken) vurgusu düşen sözcükler (espeak) */
static int vurgusuz_mu(const uint32_t *h, int n) {
    static const char *const L[] = { "o", "bu", "\xc5\x9fu", "bir", "ben", "sen", "biz", "siz" };
    char b[16]; int k = 0;
    if (n > 4) return 0;
    for (int i = 0; i < n; i++) k += u8_yaz(h[i], b + k);
    b[k] = 0;
    for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) if (!tt_karsilastir(L[i], b)) return 1;
    return 0;
}

static void sozcuk(cikti_t *c, const uint32_t *h, int n, int *bosluk, int sonraki_var, int hep_buyuk) {
    if (n <= 0) return;
    int unlu = 0;
    for (int i = 0; i < n; i++) if (sesli(h[i])) unlu = 1;
    if (*bosluk) isaret(c, ' ');
    *bosluk = 1;
    if (n > AZ_HARF) n = AZ_HARF;
    if (!unlu || (hep_buyuk && n <= 3)) {         /* kısaltma (ünlüsüz ya da en çok 3 büyük harf): harf adları tek sözcük ("HF" -> "hefe") */
        uint32_t h2[AZ_HARF]; int n2 = 0;
        for (int i = 0; i < n; i++) {
            const char *a = harf_adi(h[i]);
            while (a && *a && n2 < AZ_HARF) { uint32_t x = u8_al(&a); if (x != ' ') h2[n2++] = x; }
        }
        sozcuk_seslendir(c, h2, n2, 0);
        return;
    }
    sozcuk_seslendir(c, h, n, sonraki_var && vurgusuz_mu(h, n));
}

/* s: ayırıcıdan sonraki konum; ardından (boşlukları geçerek) sözcük/sayı geliyor mu */
static int sonraki_sozcuk(uint32_t ayirici, const char *s) {
    if (ayirici != ' ' && ayirici != '\t' && ayirici != '-') return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;
    const char *p = s; uint32_t k = u8_al(&p);
    return harf_kucuk(k) || (k >= '0' && k <= '9');
}

static void metin_seslendir(cikti_t *c, const char *s, int bosluk_once) {
    uint32_t h[AZ_HARF + 1]; int n = 0, bosluk = bosluk_once, buyuk = 0;
    while (*s) {
        uint32_t kp = u8_al(&s), k = harf_kucuk(kp);
        if (k) { if (n < AZ_HARF) h[n++] = k; buyuk += (k != kp); continue; }
        sozcuk(c, h, n, &bosluk, sonraki_sozcuk(kp, s), n > 0 && buyuk == n); n = 0; buyuk = 0;
        if (kp >= '0' && kp <= '9') {               /* sayı */
            uint64_t v = kp - '0';
            while (*s >= '0' && *s <= '9' && v < 100000000000000ull) { v = v * 10 + (uint64_t)(*s - '0'); s++; }
            sayi_ses(c, v, &bosluk);
            if ((*s == '.' || *s == ',') && s[1] >= '0' && s[1] <= '9') {          /* ondalık */
                bosluk_yaz(c, &bosluk); isaretler(c, *s == '.' ? tt_s_nokta : tt_s_virgul); s++;
            } else if (*s == ':' && s[1] >= '0' && s[1] <= '9') s++;             /* 14:30 */
            continue;
        }
        if (kp == '%') { bosluk_yaz(c, &bosluk); isaretler(c, tt_s_yuzde); continue; }
        if (kp == ',' || kp == ';' || kp == ':' || kp == '!' || kp == '?' || kp == '.')
            isaret(c, kp);                          /* noktalama işareti modelde var */
    }
    sozcuk(c, h, n, &bosluk, 0, n > 0 && buyuk == n);
}

#ifndef TR_PARCA_AZAMI
#define TR_PARCA_AZAMI 80   /* bayt */
#endif
int tr_cumle(const char **metin, int64_t *ids, int azami, int *ilk_sesli) {
    const char *s = *metin;
    while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    if (!*s) { *metin = s; return 0; }
    /* cümle sonu: . ? ! (sayı içindeki nokta değil) ya da satır sonu */
    const char *e = s;
    while (*e) {
        if ((*e == '.' || *e == '?' || *e == '!') && !(e[1] >= '0' && e[1] <= '9' && e > s && e[-1] >= '0' && e[-1] <= '9')) { e++; while (*e == '.' || *e == '?' || *e == '!') e++; break; }
        if (*e == '\n') break;
        e++;
    }
    /* Uzun cümle parçalanır: kod çözücünün tepe belleği ses süresiyle
     * doğru orantılı (~17 MB/sn). Önce son virgül/noktalı virgül/iki
     * nokta, yoksa son boşluk; parça noktalamasını korur. */
    if (e - s > TR_PARCA_AZAMI) {
        const char *k = 0, *bos = 0;
        for (const char *q = s + 20; q < s + TR_PARCA_AZAMI; q++) {
            if (*q == ',' || *q == ';' || *q == ':') k = q + 1;
            if (*q == ' ') bos = q;
        }
        if (k) e = k; else if (bos) e = bos;
    }
    static char cumle[2048];
    int n = (int)(e - s); if (n > (int)sizeof cumle - 1) n = sizeof cumle - 1;
    for (int i = 0; i < n; i++) cumle[i] = s[i];
    cumle[n] = 0;
    *metin = e;
    cikti_t c = { ids, 0, azami, 0 };
    id_ekle(&c, 1); id_ekle(&c, 0);                /* ^ */
    metin_seslendir(&c, cumle, 0);
    id_ekle(&c, 2);                                /* $ */
    /* Cümle başı ünlü: model kısa tutup yutuyor ("Ötüken" -> "tüken").
     * Süreyi zorla uzatmak işe yaramadı (model eklenen süreyi sessizlik
     * olarak çalıyor). Ünlünün ardına uzunluk imi (ː) konur; model onu
     * eğitimde gördüğü uzun ünlü olarak sesli çalar. Dinleyerek seçildi. */
    int sesli = 0;
    for (int i = 2; i < c.n; i += 2) {
        int64_t v = ids[i];
        if (v == 120 || v == 121) continue;    /* ˈ ˌ */
        static const int64_t UNLU[] = { 14, 18, 21, 27, 33, 37, 39, 42, 45, 54, 61, 74, 79, 100, 105, 50, 51, 59 };
        for (unsigned j = 0; j < sizeof UNLU / sizeof UNLU[0]; j++) if (UNLU[j] == v) sesli = 1;
        if (sesli && !(i + 2 < c.n && ids[i + 2] == 122)) {
            if (c.n + 2 > azami) { c.tasma = 1; break; }
            for (int k = c.n - 1; k >= i + 2; k--) ids[k + 2] = ids[k];
            ids[i + 2] = 122; ids[i + 3] = 0;   /* ː */
            c.n += 2;
        }
        break;
    }
    if (ilk_sesli) *ilk_sesli = sesli;
    return c.tasma ? -1 : c.n;
}
