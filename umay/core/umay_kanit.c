/* KANIT — UMAY karar katmanı. Tasarım: include/umay_kanit.h */
#include "umay_kanit.h"
#include "umay_str.h"

/* DİLBİLGİSEL soru ve buyruk sözcükleri. Konu listesi DEĞİLDİR: bir
 * sorunun neyi sorduğunu değil, nasıl sorulduğunu taşırlar.
 *   - soru adılları ve TÜM durum ekli biçimleri (ne/kim/nere/hangi/kaç)
 *   - soru eki (mı/mi/mu/mü + -dır, kişi ekleri)
 *   - istek/buyruk kalıpları (anlat, açıkla, söyle, araştır, tara...)
 *   - kip yardımcıları (olur, olmalı, gerekir, lazım, yapılır, edilir...)
 * Bir sözcük burada yoksa terim sayılır; bilinmeyen terim kanıtı
 * ZORLAŞTIRIR (güvenli yöne düşer), kolaylaştırmaz. */
static const char *SORU_SOZ[] = {
    /* ne */
    "nedir", "neydi", "neler", "neyi", "neye", "neyle", "nede", "neden", "neyin",
    "nesi", "nesidir", "nasıl", "nasıldır", "niçin", "niye", "neresi", "neresidir",
    "nerede", "nereye", "nereden", "nereyi", "nerelerde", "nereli",
    /* kim / hangi / kaç */
    "kim", "kimi", "kime", "kimin", "kimle", "kimden", "kimdir", "kimler",
    "hangi", "hangisi", "hangisini", "hangisine", "hangileri", "hangisidir",
    "kaç", "kaçta", "kaçı", "kaçıncı", "kaçtır",
    /* soru eki */
    "mı", "mi", "mu", "mü", "mıdır", "midir", "mudur", "müdür",
    "mısın", "misin", "musun", "müsün", "mıyım", "miyim", "muyum", "müyüm",
    /* istek / buyruk */
    "anlat", "anlatır", "açıkla", "hakkında", "bilgi", "ver", "bana", "bize",
    "lütfen", "söyle", "sen", "senin", "benim", "şunu", "bunu", "şu",
    "araştır", "araştırma", "araştırması", "arastir", "arastirma", "tara",
    "derle", "bul", "kaynak", "kaynakları", "konusunda", "üzerine", "ilgili",
    /* kip yardımcıları */
    "yapılır", "yapılmalı", "olur", "olmalı", "olabilir", "yarar", "işe",
    "gerekir", "gerek", "lazım", "var", "yok", "eder", "edilir", "edilmeli",
    "durumda", 0
};

static int soru_sozu(const char *k) {
    for (int i = 0; SORU_SOZ[i]; i++) if (um_esit(k, SORU_SOZ[i])) return 1;
    return 0;
}

static uint32_t harf(const char *s) {
    uint32_t n = 0, b;
    while (*s) { um_utf8_oku(s, &b); s += b; n++; }
    return n;
}

/* ünsüz yumuşaması: ç~c, k~ğ, p~b, t~d (harç/harcı, kireç/kireci) */
static int yumusak_esit(uint32_t x, uint32_t y) {
    if (x == y) return 1;
    if ((x == 0xE7 && y == 'c') || (x == 'c' && y == 0xE7)) return 1;
    if ((x == 'k' && y == 0x11F) || (x == 0x11F && y == 'k')) return 1;
    if ((x == 'p' && y == 'b') || (x == 'b' && y == 'p')) return 1;
    if ((x == 't' && y == 'd') || (x == 'd' && y == 't')) return 1;
    return 0;
}

static int unlu(uint32_t c) {
    return c == 'a' || c == 'e' || c == 0x131 || c == 'i' || c == 'o' || c == 0xF6 ||
           c == 'u' || c == 0xFC || c == 0xE2 || c == 0xEE || c == 0xFB;
}

static uint32_t harfler(const char *s, uint32_t *h, uint32_t en_cok) {
    uint32_t n = 0, b;
    while (*s && n < en_cok) { h[n++] = um_utf8_oku(s, &b); s += b; }
    return n;
}

/* Aynı kök mü. Türkçe ekler sözcüğün SONUNA gelir; kök başta kalır:
 *   - ikisi de >= 5 harf: ilk 5 harf aynı (5. harfte yumuşama serbest:
 *     kireç ~ kireci)
 *   - kısa olan 3-4 harf: uzun olan kısa olanla başlar (son harfte yumuşama
 *     serbest) ve ardından ÜNLÜYLE başlayan en çok 2 harflik ek gelir
 *     (harç ~ harcı, taş ~ taşın; taş !~ taşıma, kum !~ kumanda) */
int kanit_esles(const char *a, const char *b) {
    if (um_esit(a, b)) return 1;
    uint32_t ha[24], hb[24];
    uint32_t na = harfler(a, ha, 24), nb = harfler(b, hb, 24);
    if (na >= 5 && nb >= 5) {
        for (uint32_t i = 0; i < 4; i++) if (ha[i] != hb[i]) return 0;
        return yumusak_esit(ha[4], hb[4]);
    }
    const uint32_t *k = na <= nb ? ha : hb, *u = na <= nb ? hb : ha;
    uint32_t kn = na <= nb ? na : nb, un = na <= nb ? nb : na;
    if (kn < 3 || kn > 4 || un <= kn || un - kn > 2) return 0;
    for (uint32_t i = 0; i + 1 < kn; i++) if (k[i] != u[i]) return 0;
    return yumusak_esit(k[kn - 1], u[kn - 1]) && unlu(u[kn]);
}

int kanit_terimleri(const char *soru, char cikan[][24], int en_cok) {
    char k[24][24];
    int n = bilgi_kelimeler(soru ? soru : "", k, 24), m = 0;
    for (int i = 0; i < n && m < en_cok; i++)
        if (!soru_sozu(k[i])) um_kopya(cikan[m++], 24, k[i]);
    return m;
}

/* soru eki (ve kişi/ek-eylem biçimleri): yüklemden SONRA gelebilen tek şey */
static const char *SORU_EKI[] = {
    "mı", "mi", "mu", "mü", "mıdır", "midir", "mudur", "müdür",
    "mısın", "misin", "musun", "müsün", "mıyım", "miyim", "muyum", "müyüm",
    "mıyız", "miyiz", "muyuz", "müyüz", 0
};

/* Sorunun yüklem yerindeki sözcük: son sözcük; sondaki soru ekleri atlanır. */
static int yuklem_yeri(const char *soru, char c[24]) {
    char k[24];
    const char *p = soru;
    c[0] = 0;
    while (p && (p = bilgi_sonraki_kelime(p, k)) != 0) {
        int ek = 0;
        for (int i = 0; SORU_EKI[i]; i++) if (um_esit(k, SORU_EKI[i])) { ek = 1; break; }
        if (!ek) um_kopya(c, 24, k);
    }
    return c[0] != 0;
}

/* Çekimli eylem sonekleri — dilbilgisi çizelgesinden, ünlü uyumu kalıbıyla:
 *   A = {a, e}   I = {ı, i, u, ü}   D = {d, t}
 * Kip/zaman + kişi: geniş zaman (-Ar/-Ir; ünlüyle biten gövdede -r de
 * bunlarla biter) ve olumsuzu (-mAz), şimdiki (-yor), gelecek (-AcAk),
 * görülen geçmiş (-DI), öğrenilen geçmiş (-mIş), gereklilik (-mAlI),
 * istek 1. kişi (-AyIm, -AlIm: "ne zaman yapalım"). Ek-eylem -DIr de -Ir
 * ile biter. Adlarla çakışan biçimler bilerek alınmadı: -DIn (altın, odun),
 * -mAm (tamam). Biçim tek başına karar vermez: yalnız yüklem YERİNDEKİ,
 * kütüphanede HİÇ geçmeyen son terime uygulanır (kanit_degerlendir). */
static const char *EYLEM_EK[] = {
    "Ar", "Ir", "rIm", "rsIn", "rIz", "rsInIz", "rlAr",
    "mAz", "mAzsIn", "mAyIz", "mAzsInIz", "mAzlAr",
    "yor", "yorum", "yorsun", "yoruz", "yorsunuz", "yorlar",
    "AcAk", "AcAğIm", "AcAksIn", "AcAğIz", "AcAksInIz", "AcAklAr",
    "DI", "DIm", "DIk", "DInIz", "DIlAr",
    "mIş", "mIşIm", "mIşsIn", "mIşIz", "mIşsInIz", "mIşlAr",
    "mAlI", "mAlIyIm", "mAlIsIn", "mAlIyIz", "mAlIsInIz", "mAlIlAr",
    "AyIm", "AlIm", 0
};

/* kalıp harfi ile metin kod noktası uyuşuyor mu */
static int ek_harf(char k, uint32_t kn) {
    switch (k) {
    case 'A': return kn == 'a' || kn == 'e';
    case 'I': return kn == 0x131 || kn == 'i' || kn == 'u' || kn == 0xFC;
    case 'D': return kn == 'd' || kn == 't';
    default:  return kn == (uint32_t)(uint8_t)k;
    }
}

/* s kalıpla bitiyor mu (kalıp ASCII + ğ/ş; ğ ve ş iki bayt) */
static int ek_bitis(const char *s, const char *kalip) {
    uint32_t ks[16], kn = 0, b;
    for (const char *q = kalip; *q && kn < 16; ) {
        if ((uint8_t)*q < 0x80) { ks[kn++] = (uint32_t)(uint8_t)*q; q++; }
        else { ks[kn++] = um_utf8_oku(q, &b) | 0x80000000u; q += b; }  /* düz harf */
    }
    uint32_t sn[48], n = 0;
    for (const char *p = s; *p && n < 48; ) { sn[n++] = um_utf8_oku(p, &b); p += b; }
    if (kn >= n) return 0;
    for (uint32_t i = 0; i < kn; i++) {
        uint32_t m = ks[kn - 1 - i], c = sn[n - 1 - i];
        if (m & 0x80000000u) { if ((m & 0x7FFFFFFFu) != c) return 0; }
        else if (!ek_harf((char)m, c)) return 0;
    }
    return 1;
}

int kanit_eylem_ekli(const char *s) {
    if (harf(s) < 4) return 0;
    for (int i = 0; EYLEM_EK[i]; i++) if (ek_bitis(s, EYLEM_EK[i])) return 1;
    return 0;
}

int kanit_belge_sikligi(const char *terim) {
    return bilgi_siklik(terim, kanit_esles);
}

/* IDF benzeri basamak: seyrek terim ağır; kütüphanede hiç yoksa en ağır */
static int agirlik(int df, int N) {
    if (df == 0) return 400;
    if (df == 1) return 300;
    if (df <= 3) return 200;
    if (df * 4 <= N) return 120;
    return 60;
}

int kanit_agirligi(const char *terim) {
    return agirlik(kanit_belge_sikligi(terim), bilgi_sayi());
}

int kanit_dayanak_bul(const char *soru, const char *const *kategoriler, int kat_sayi,
                      baglam_t *cikan, int en_cok) {
    static char t[KANIT_TERIM_MAX][24];
    static char sorgu[KANIT_TERIM_MAX * 25];
    int tn = kanit_terimleri(soru, t, KANIT_TERIM_MAX);
    if (tn == 0) return 0;
    sorgu[0] = 0;
    for (int i = 0; i < tn; i++) { um_ekle(sorgu, sizeof sorgu, t[i]); um_ekle(sorgu, sizeof sorgu, " "); }
    bilgi_suzgec_t sz = { kategoriler, kat_sayi, 0, 0, 0, kanit_esles, kanit_agirligi };
    return bilgi_baglam_bul_suzgec(sorgu, &sz, cikan, en_cok);
}

/* dayanak metinlerinin sözcükleri (durağan: Ring3 yığını küçük) */
#define KANIT_SOZ_MAX 512
static char g_soz[KANIT_SOZ_MAX][24];
static int  g_soz_kaynak[KANIT_SOZ_MAX];
static int  g_soz_n;

static void soz_ekle(const char *metin, int kaynak) {
    static char k[160][24];
    int n = bilgi_kelimeler(metin ? metin : "", k, 160);
    for (int i = 0; i < n && g_soz_n < KANIT_SOZ_MAX; i++) {
        um_kopya(g_soz[g_soz_n], 24, k[i]);
        g_soz_kaynak[g_soz_n++] = kaynak;
    }
}

static void dayanak_sozleri(const baglam_t *d, int n, const char *ek) {
    g_soz_n = 0;
    for (int i = 0; i < n; i++) {
        soz_ekle(d[i].baslik, i);
        soz_ekle(d[i].alinti, i);
        for (int e = 0; e < d[i].etiket_sayi; e++) soz_ekle(d[i].etiket[e], i);
    }
    if (ek && *ek) soz_ekle(ek, n);            /* BENGİ ayrı kaynak sayılır */
}

/* Eylemsi (ulaç) sonekleri: -ArkAn/-IrkAn/-rkAn ("örerken", "yaparken"),
 * -ArAk ("kullanarak"). Sorunun ORTASINDA da olsa konu değil eylemdir.
 * Adlarla çakışması düşük olanlar seçildi (-IncA: "düşünce", -mAdAn:
 * "maden" gibi çakışanlar alınmadı). */
static const char *EYLEMSI_EK[] = { "rkAn", "ArAk", 0 };

int kanit_eylemsi_ekli(const char *s) {
    if (harf(s) < 5) return 0;
    for (int i = 0; EYLEMSI_EK[i]; i++) if (ek_bitis(s, EYLEMSI_EK[i])) return 1;
    return 0;
}

/* Terim t, verilen sözcük kümesinde (kanit_esles ile) geçiyor mu. */
static int terim_gecer(const kanit_t *k, int t, char kelime[][24], int kn) {
    for (int i = 0; i < kn; i++) if (kanit_esles(k->terim[t], kelime[i])) return 1;
    return 0;
}

/* --- 1.4.7: TEK kaydın (bağlamın) kapsaması ---
 * Bir kayıt = TEK bir tutarlı kaynak bağlamıdır: başlık + etiket + alıntının
 * TAMAMI (çok cümleli olabilir) birlikte sayılır. Terim, bu kaydın herhangi
 * bir yerinde geçiyorsa o kayıtça kapsanır; ağırlık toplamı döner.
 * Farklı KAYITLAR burada BİRLEŞTİRİLMEZ: sarnıç(A)+su(B)+temiz(C) gibi ayrı
 * kaynaklara dağılmış sahte kanıt tek kayıtta toplanamaz, yeterli sayılmaz.
 * (Eski davranış: TÜM kayıtların sözcükleri tek havuzdaydı; kaynak sınırı
 * yoktu.) Cümle düzeyine inmek yerine kayıt düzeyi seçildi: aynı kaydın
 * birden çok cümlesine yayılan MEŞRU yanıtlar korunur, yalnız kaynaklar
 * arası birleştirme engellenir (ölçüm: docs/UMAY-KANIT.md). */
static int kayit_kapsama(const kanit_t *k, const char *baslik,
                         const char etiket[][24], int etiket_sayi,
                         const char *alinti, int kaps[]) {
    static char w[160][24];
    for (int t = 0; t < k->terim_sayi; t++) kaps[t] = 0;
    int wn = bilgi_kelimeler(baslik ? baslik : "", w, 160);
    for (int t = 0; t < k->terim_sayi; t++) if (terim_gecer(k, t, w, wn)) kaps[t] = 1;
    for (int e = 0; e < etiket_sayi; e++) {
        int en = bilgi_kelimeler(etiket[e], w, 160);
        for (int t = 0; t < k->terim_sayi; t++)
            if (!kaps[t] && terim_gecer(k, t, w, en)) kaps[t] = 1;
    }
    wn = bilgi_kelimeler(alinti ? alinti : "", w, 160);
    for (int t = 0; t < k->terim_sayi; t++)
        if (!kaps[t] && terim_gecer(k, t, w, wn)) kaps[t] = 1;
    int puan = 0;
    for (int t = 0; t < k->terim_sayi; t++) if (kaps[t]) puan += k->agirlik[t];
    return puan;
}

int kanit_kayit_kapsama(const kanit_t *k, const baglam_t *d) {
    if (!k || !d || k->terim_sayi == 0) return 0;
    int kaps[KANIT_TERIM_MAX];
    return kayit_kapsama(k, d->baslik, d->etiket, d->etiket_sayi, d->alinti, kaps);
}

/* kapsandi[] ve ağırlıklardan kapsama / odak / yeterli */
static void kanit_hukum(kanit_t *k) {
    int top = 0, kap = 0;
    for (int t = 0; t < k->terim_sayi; t++) {
        top += k->agirlik[t];
        if (k->kapsandi[t]) kap += k->agirlik[t];
    }
    k->kapsama = top ? (100 * kap) / top : 0;
    k->odak_kapsandi = (k->odak >= 0) ? k->kapsandi[k->odak] : 0;
    k->yeterli = k->dayanak_var && (k->kapsama >= KANIT_KAPSAMA_ESIK) && k->odak_kapsandi;
}

void kanit_degerlendir(const char *soru, const baglam_t *d, int n,
                       const char *ek, kanit_t *k) {
    um_bellek_doldur(k, 0, sizeof *k);
    k->odak = -1;
    k->yuklem = -1;
    k->terim_sayi = kanit_terimleri(soru, k->terim, KANIT_TERIM_MAX);
    if (k->terim_sayi == 0) return;

    /* terimler, sıklık, ağırlık, odak: dayanaktan BAĞIMSIZ (meclis birleşimi
     * için dayanaksız uzmanda da kurulur) */
    int N = bilgi_sayi();
    for (int t = 0; t < k->terim_sayi; t++) k->df[t] = kanit_belge_sikligi(k->terim[t]);
    /* yüklem adayı: SON terim, sorunun yüklem yerinde (ardından yalnız soru
     * eki gelebilir: "... hakkında bilgi ver" ya da "... nedir" ile biten
     * soruda son terim ad durumundadır), kütüphanede yok ve eylem ekli */
    int son = k->terim_sayi - 1;
    char yer[24];
    if (k->terim_sayi >= 2 && k->df[son] == 0 && kanit_eylem_ekli(k->terim[son]) &&
        yuklem_yeri(soru, yer) && um_esit(yer, k->terim[son])) k->yuklem = son;
    int enb = -1;
    for (int t = 0; t < k->terim_sayi; t++) {
        k->eylem[t] = (t == k->yuklem) ||
                      (k->terim_sayi >= 2 && k->df[t] == 0 && kanit_eylemsi_ekli(k->terim[t]));
        k->agirlik[t] = k->eylem[t] ? KANIT_YUKLEM_AGIRLIK : agirlik(k->df[t], N);
        if (!k->eylem[t] && k->agirlik[t] > enb) { enb = k->agirlik[t]; k->odak = t; }
    }

    k->dayanak_var = (n > 0) || (ek && *ek);
    if (!k->dayanak_var) return;

    /* --- 1.4.7: kapsama KAYIT/CÜMLE düzeyinde ölçülür ---
     * En iyi TEK kaydın en iyi cümlesi (+ o kaydın başlık/etiket bağlamı)
     * kapsamayı belirler. Farklı kaynaklar birleştirilmez; böylece odak ve
     * yan terimler AYNI kayıt-cümle bağlamında geçmedikçe yeterli sayılmaz. */
    int en_puan = -1, en_kaps[KANIT_TERIM_MAX] = { 0 };
    int kaynak_isaret = 0;      /* en az bir terim taşıyan AYRI kaynak sayısı */
    for (int i = 0; i < n; i++) {
        int kaps[KANIT_TERIM_MAX] = { 0 };
        int puan = kayit_kapsama(k, d[i].baslik, d[i].etiket, d[i].etiket_sayi,
                                 d[i].alinti, kaps);
        int tasiyor = 0;
        for (int t = 0; t < k->terim_sayi; t++) if (kaps[t]) tasiyor = 1;
        if (tasiyor) kaynak_isaret++;
        if (puan > en_puan) { en_puan = puan; for (int t = 0; t < k->terim_sayi; t++) en_kaps[t] = kaps[t]; }
    }
    if (ek && *ek) {           /* BENGİ bağlamı ayrı kaynak, başlık/etiketsiz */
        int kaps[KANIT_TERIM_MAX] = { 0 };
        int puan = kayit_kapsama(k, 0, 0, 0, ek, kaps);
        int tasiyor = 0;
        for (int t = 0; t < k->terim_sayi; t++) if (kaps[t]) tasiyor = 1;
        if (tasiyor) kaynak_isaret++;
        if (puan > en_puan) { en_puan = puan; for (int t = 0; t < k->terim_sayi; t++) en_kaps[t] = kaps[t]; }
    }
    for (int t = 0; t < k->terim_sayi; t++) k->kapsandi[t] = en_kaps[t];
    k->kaynak_sayi = kaynak_isaret;
    kanit_hukum(k);
}

int kanit_pay_var(const kanit_t *k) {
    for (int t = 0; t < k->terim_sayi; t++)
        if (k->kapsandi[t] && !k->eylem[t] && k->agirlik[t] > KANIT_GENEL_AGIRLIK) return 1;
    return 0;
}

void kanit_birlestir(kanit_t *hedef, const kanit_t *k) {
    if (!k || k->terim_sayi == 0) return;
    if (hedef->terim_sayi == 0) { um_bellek_kopya(hedef, k, sizeof *hedef); return; }
    /* aynı soru: aynı terimler aynı sırada; değilse terim adıyla eşlenir */
    for (int t = 0; t < k->terim_sayi; t++) {
        if (!k->kapsandi[t]) continue;
        for (int h = 0; h < hedef->terim_sayi; h++)
            if (um_esit(hedef->terim[h], k->terim[t])) { hedef->kapsandi[h] = 1; break; }
    }
    hedef->kaynak_sayi += k->kaynak_sayi;
    if (k->dayanak_var) hedef->dayanak_var = 1;
    kanit_hukum(hedef);
}

int kanit_bagliligi(const char *metin, const baglam_t *d, int n, const char *ek) {
    static char t[32][24];
    int tn = kanit_terimleri(metin, t, 32), say = 0, tut = 0;
    dayanak_sozleri(d, n, ek);
    for (int i = 0; i < tn; i++) {
        if (harf(t[i]) < 4) continue;          /* kısa bağlaçlar ölçüye girmez */
        say++;
        for (int w = 0; w < g_soz_n; w++)
            if (kanit_esles(t[i], g_soz[w])) { tut++; break; }
    }
    return say ? (100 * tut) / say : 100;
}

int kanit_cumle_sec(const kanit_t *k, const int *atla, const char *s, char *c, uint32_t n,
                    int *tutan) {
    if (!c || n < 24) return -1;
    c[0] = 0;
    if (!s || !k) return -1;
    int32_t L = (int32_t)um_uzun(s), bas = 0, en_bas = 0, en_son = 0, en_puan = -1;
    int en_tut[KANIT_TERIM_MAX] = { 0 };
    static char cumle[1024];
    while (bas < L) {
        int32_t son = bas;
        while (son < L && s[son] != '\n' &&
               !(s[son] == '.' && (s[son + 1] == ' ' || s[son + 1] == 0 || s[son + 1] == '\n'))) son++;
        if (son < L && s[son] == '.') son++;
        if (son - bas >= 15) {
            int32_t cb = son - bas < (int32_t)sizeof cumle - 1 ? son - bas : (int32_t)sizeof cumle - 1;
            um_bellek_kopya(cumle, s + bas, (uint64_t)cb);
            cumle[cb] = 0;
            static char kc[64][24];
            int kn = bilgi_kelimeler(cumle, kc, 64), puan = 0;
            int tut[KANIT_TERIM_MAX] = { 0 };
            for (int t = 0; t < k->terim_sayi; t++)
                for (int w = 0; w < kn; w++)
                    if (kanit_esles(k->terim[t], kc[w])) {
                        tut[t] = 1;
                        if (!(atla && atla[t])) puan += k->agirlik[t] > 0 ? k->agirlik[t] : 1;
                        break;
                    }
            if (puan > en_puan) {
                en_puan = puan; en_bas = bas; en_son = son;
                for (int t = 0; t < KANIT_TERIM_MAX; t++) en_tut[t] = tut[t];
            }
        }
        bas = son;
        while (bas < L && (s[bas] == ' ' || s[bas] == '\n')) bas++;
    }
    if (en_puan < 0) return -1;
    if (tutan) for (int t = 0; t < KANIT_TERIM_MAX; t++) tutan[t] = en_tut[t];
    uint32_t boy = (uint32_t)(en_son - en_bas);
    if (boy + 1 > n) {                          /* sözcük sınırında kes */
        boy = n - 4;
        while (boy > 20 && s[en_bas + boy] != ' ') boy--;
        um_bellek_kopya(c, s + en_bas, boy);
        c[boy] = 0;
        um_ekle(c, n, "…");
    } else {
        um_bellek_kopya(c, s + en_bas, boy);
        c[boy] = 0;
    }
    return en_puan;
}
