/* ARAŞTIRMA — ajanların araştırma yordamı. Tasarım: include/umay_arastirma.h */
#include "umay_arastirma.h"
#include "umay_kanit.h"
#include "umay_str.h"
#include "umay_port.h"
#include "umay_ortak.h"

static uint32_t harf_sayisi(const char *s) {
    uint32_t n = 0, b;
    while (*s) { um_utf8_oku(s, &b); s += b; n++; }
    return n;
}

/* ilk 'harf' harfi (UTF-8 güvenli) */
static void on_kopya(char *h, uint32_t n, const char *s, uint32_t harf) {
    uint32_t o = 0, b;
    while (*s && harf > 0) {
        um_utf8_oku(s, &b);
        if (o + b + 1 > n) break;
        um_bellek_kopya(h + o, s, b);
        o += b; s += b; harf--;
    }
    h[o] = 0;
}

void ars_basla(arastirma_t *a, const char *soru,
               const char *const *kategoriler, int kategori_sayi, int kendi_sayi) {
    um_bellek_doldur(a, 0, sizeof *a);
    um_kopya(a->soru, sizeof a->soru, soru ? soru : "");
    for (int i = 0; i < kategori_sayi && i < 4; i++)
        if (kategoriler[i]) a->kategori[a->kategori_sayi++] = kategoriler[i];
    a->kendi_sayi = kendi_sayi < a->kategori_sayi ? kendi_sayi : a->kategori_sayi;

    /* Araştırma buyruğunun kendisi terim değildir: "sarnıç hakkında kaynak
     * tara" -> yalnız "sarnıç" aranır. Soru/buyruk sözcükleri kanıt
     * katmanıyla ORTAK tek listeden atılır (umay_kanit.c). */
    a->terim_sayi = kanit_terimleri(a->soru, a->terim, ARS_TERIM_MAX);
}

int ars_ilerleme(const arastirma_t *a) {
    if (a->bitti) return 100;
    return 10 + (80 * a->sorgu_no) / (a->terim_sayi + 1);
}

/* havuza kat: aynı kayıt (başlık+kaynak) bir kez; yer yoksa en zayıfın
 * yerine ancak daha güçlüyse girer */
static void havuza_kat(arastirma_t *a, const baglam_t *b, ars_kademe_t kd) {
    for (int i = 0; i < a->havuz_sayi; i++)
        if (um_esit(a->havuz[i].baslik, b->baslik) && um_esit(a->havuz[i].kaynak, b->kaynak)) {
            if (b->skor > a->havuz[i].skor) a->havuz[i].skor = b->skor;
            if (kd < a->havuz_kademe[i]) a->havuz_kademe[i] = kd;
            return;
        }
    int yer = a->havuz_sayi;
    if (yer >= ARS_HAVUZ_MAX) {
        int en = 0;
        for (int i = 1; i < a->havuz_sayi; i++) if (a->havuz[i].skor < a->havuz[en].skor) en = i;
        if (a->havuz[en].skor >= b->skor) return;
        yer = en;
    } else a->havuz_sayi++;
    um_bellek_kopya(&a->havuz[yer], b, sizeof *b);
    a->havuz_kademe[yer] = kd;
}

/* kademeli arama: kendi -> ortak -> genel. Bulunan kayıt sayısını döndürür. */
static int kademeli_ara(arastirma_t *a, const char *sorgu, int onek) {
    static baglam_t ara[3];              /* durağan: Ring3 yığını küçük */
    int n = 0; ars_kademe_t kd = ARS_KENDI;
    bilgi_suzgec_t sz = { 0, 0, 0, 0, onek, 0, 0 };
    if (a->kendi_sayi > 0) {
        sz.kategoriler = a->kategori; sz.kategori_sayi = a->kendi_sayi;
        n = bilgi_baglam_bul_suzgec(sorgu, &sz, ara, 3);
    }
    if (n == 0 && a->kategori_sayi > a->kendi_sayi) {
        sz.kategoriler = a->kategori + a->kendi_sayi;
        sz.kategori_sayi = a->kategori_sayi - a->kendi_sayi;
        n = bilgi_baglam_bul_suzgec(sorgu, &sz, ara, 3);
        kd = ARS_ORTAK;
    }
    if (n == 0) {
        sz.kategoriler = 0; sz.kategori_sayi = 0;
        n = bilgi_baglam_bul_suzgec(sorgu, &sz, ara, 3);
        kd = ARS_GENEL;
    }
    for (int i = 0; i < n; i++) havuza_kat(a, &ara[i], kd);
    a->ham_bulgu += n;
    return n;
}

/* ---- 1.5: ağ evresi (ARAYICI) ----
 * Yerel tarama bitince soru terimleri IPC ile ARAYICI'ya gider; o, ağda
 * arar, sayfaları okunur metne çevirip /KUT/UMAY/AG altına belge yazar.
 * Biz yalnız o düz belgeleri okuruz (ham HTML UMAY'a girmez). Aynı anda
 * tek istek: başka bir araştırma bekliyorsa sıra beklenir. */
#define AG_ZAMAN_ASIMI_MS 300000u      /* QEMU (TCG) TLS'te yavaş */
static up_tut_t g_ag_kapi = UP_GECERSIZ, g_ag_yanit = UP_GECERSIZ;
static const arastirma_t *g_ag_sahip = 0;

static int ag_gonder(arastirma_t *a) {
    if (g_ag_sahip) return 0;                         /* sıra bekle */
    if (g_ag_kapi == UP_GECERSIZ) g_ag_kapi = up_kapi_bagla("arayici");
    if (g_ag_kapi == UP_GECERSIZ) return -1;          /* ARAYICI yok */
    if (g_ag_yanit == UP_GECERSIZ) g_ag_yanit = up_kapi_ac("umay-ag");
    if (g_ag_yanit == UP_GECERSIZ) return -1;
    char istek[400];
    um_kopya(istek, sizeof istek, "umay-ag\n");
    for (int t = 0; t < a->terim_sayi; t++) {
        if (t) um_ekle(istek, sizeof istek, " ");
        um_ekle(istek, sizeof istek, a->terim[t]);
    }
    if (up_gonder(g_ag_kapi, istek, um_uzun(istek)) < 0) return -1;
    g_ag_sahip = a;
    a->ag_bas_ms = up_ms();
    gun_yaz("ARAŞTIRMA: ağa soruldu: %s", istek + 8);
    return 1;
}

/* ARAYICI'nın getirdiği belgeleri yalnız "ag" rafında arar */
static int ag_rafinda_ara(arastirma_t *a, const char *sorgu, int onek) {
    static baglam_t ara[3];
    static const char *RAF[1] = { "ag" };
    bilgi_suzgec_t sz = { 0, 0, 0, 0, onek, 0, 0 };
    sz.kategoriler = RAF; sz.kategori_sayi = 1;
    int n = bilgi_baglam_bul_suzgec(sorgu, &sz, ara, 3);
    for (int i = 0; i < n; i++) havuza_kat(a, &ara[i], ARS_AG);
    return n;
}

static void ag_yanitini_isle(arastirma_t *a, const char *y) {
    um_kopya(a->ag_not, sizeof a->ag_not, y);
    if (y[0] != 'T') return;
    /* "T <adet> ad1 ad2 ..." */
    const char *p = y + 1;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') p++;
    int yuklenen = 0;
    while (*p) {
        while (*p == ' ') p++;
        char ad[20]; int o = 0;
        while (*p && *p != ' ' && o < 19) ad[o++] = *p++;
        ad[o] = 0;
        if (!o) break;
        char yol[48];
        um_bicim(yol, sizeof yol, "/KUT/UMAY/AG/%s", ad);
        if (bilgi_belge_ekle(yol) == UM_OLDU) yuklenen++;
    }
    a->ag_belge = yuklenen;
}

/* Ağ rafı kalıcıdır: ARAYICI bu kez ulaşamasa da önceki bulgular aranır. */
static void ag_rafini_tara(arastirma_t *a) {
    /* ağ rafında: bütün terimler, sonra tek tek (gerekirse kökle) */
    char birlesik[ARS_TERIM_MAX * 25]; birlesik[0] = 0;
    for (int t = 0; t < a->terim_sayi; t++) { um_ekle(birlesik, sizeof birlesik, a->terim[t]); um_ekle(birlesik, sizeof birlesik, " "); }
    if (birlesik[0]) ag_rafinda_ara(a, birlesik, 0);
    for (int t = 0; t < a->terim_sayi; t++) {
        int n = ag_rafinda_ara(a, a->terim[t], 0);
        if (n == 0 && harf_sayisi(a->terim[t]) >= 5) {
            char kok[24]; uint32_t h = harf_sayisi(a->terim[t]);
            on_kopya(kok, sizeof kok, a->terim[t], h - 2 < 4 ? 4 : h - 2);
            n = ag_rafinda_ara(a, kok, 1);
        }
        if (n > 0) a->terim_bulgu[t] += n;
        a->ag_sorgu++;                                 /* yerel sorgu sayısına katılmaz */
    }
}

static void ars_bitir(arastirma_t *a) {
    for (int i = 1; i < a->havuz_sayi; i++)
        for (int j = i; j > 0 && a->havuz[j].skor > a->havuz[j - 1].skor; j--) {
            baglam_t tb = a->havuz[j]; a->havuz[j] = a->havuz[j - 1]; a->havuz[j - 1] = tb;
            ars_kademe_t tk = a->havuz_kademe[j];
            a->havuz_kademe[j] = a->havuz_kademe[j - 1]; a->havuz_kademe[j - 1] = tk;
        }
    bilgi_catisma_bul(a->havuz, a->havuz_sayi, &a->catisma);
    a->bitti = 1;
}

/* ağ evresi adımı: 1 sürüyor, 0 bitti */
static int ag_adim(arastirma_t *a) {
    if (a->ag_durum == 0) {
        if (a->terim_sayi == 0) { a->ag_durum = -1; return 0; }
        int r = ag_gonder(a);
        if (r == 0) return 1;                          /* sırada */
        if (r < 0) { a->ag_durum = -1; um_kopya(a->ag_not, sizeof a->ag_not, "ARAYICI yok"); return 0; }
        a->ag_durum = 1;
        return 1;
    }
    char y[100];
    int n = up_al(g_ag_yanit, y, sizeof y - 1, 0);
    if (n > 0) {
        y[n] = 0;
        g_ag_sahip = 0;
        gun_yaz("ARAŞTIRMA: ağdan yanıt: %s", y);
        ag_yanitini_isle(a, y);
        a->ag_durum = 2;
        return 0;
    }
    if (up_ms() - a->ag_bas_ms > AG_ZAMAN_ASIMI_MS) {
        g_ag_sahip = 0;
        um_kopya(a->ag_not, sizeof a->ag_not, "H zaman asimi");
        a->ag_durum = 2;
        return 0;
    }
    return 1;
}

int ars_adim(arastirma_t *a) {
    if (a->bitti) return 0;
    if (a->sorgu_no > a->terim_sayi) {                 /* yerel bitti: ağ evresi */
        if (ag_adim(a)) return 1;
        ag_rafini_tara(a);
        ars_bitir(a);
        return 0;
    }
    if (a->sorgu_no == 0) {
        /* bütün soru — ama YALNIZ terimlerle: buyruk sözcükleri ("kaynak
         * tara") kütüphanede kendi başına eşleşip ilgisiz kayıt getiriyordu */
        char birlesik[ARS_TERIM_MAX * 25];
        birlesik[0] = 0;
        for (int t = 0; t < a->terim_sayi; t++) {
            um_ekle(birlesik, sizeof birlesik, a->terim[t]);
            um_ekle(birlesik, sizeof birlesik, " ");
        }
        if (birlesik[0]) kademeli_ara(a, birlesik, 0);
    } else {
        int t = a->sorgu_no - 1;
        int n = kademeli_ara(a, a->terim[t], 0);
        if (n == 0 && harf_sayisi(a->terim[t]) >= 5) {
            /* Türkçe çekim: "sarnıcı", "harcı" tam sözcükle tutmaz; kökle ara */
            char kok[24];
            uint32_t h = harf_sayisi(a->terim[t]);
            on_kopya(kok, sizeof kok, a->terim[t], h - 2 < 4 ? 4 : h - 2);
            n = kademeli_ara(a, kok, 1);
            if (n > 0) a->terim_kokle[t] = 1;
        }
        a->terim_bulgu[t] = n;
    }
    a->sorgu_sayisi++;
    a->sorgu_no++;
    return 1;                                          /* sonraki adım: sorgu ya da ağ evresi */
}

/* Kanıt cümleleri: her bulgunun alıntısından soru terimlerini en ağır
 * taşıyan cümle; ilkinin karşılamadığı terimleri taşıyan ikinci cümle
 * (varsa, yer kalırsa) — AYNEN. */
static void kanit_cumleleri(const kanit_t *k, const baglam_t *b, char *c, uint32_t n) {
    static char bir[ARS_BULGU_BOY + 8], iki[ARS_BULGU_BOY + 8];
    int tut[KANIT_TERIM_MAX] = { 0 }, tut2[KANIT_TERIM_MAX];
    c[0] = 0;
    if (kanit_cumle_sec(k, 0, b->alinti, bir, sizeof bir, tut) < 0) return;
    um_kopya(c, n, bir);
    if (kanit_cumle_sec(k, tut, b->alinti, iki, sizeof iki, tut2) > 0 && !um_esit(iki, bir) &&
        um_uzun(c) + um_uzun(iki) + 2 < n) {
        um_ekle(c, n, " ");
        um_ekle(c, n, iki);
    }
}

void ars_sonuc_cikar(const arastirma_t *a, ars_sonuc_t *s) {
    um_bellek_doldur(s, 0, sizeof *s);
    um_kopya(s->soru, sizeof s->soru, a->soru);
    s->sorgu_sayisi = a->sorgu_sayisi;
    s->terim_sayi = a->terim_sayi;
    um_bellek_kopya(&s->catisma, &a->catisma, sizeof s->catisma);

    kanit_t k;
    kanit_degerlendir(a->soru, a->havuz, a->havuz_sayi, 0, &k);   /* terim ağırlıkları */
    static baglam_t kb[ARS_HAVUZ_MAX];
    for (int i = 0; i < a->havuz_sayi; i++) {
        const baglam_t *h = &a->havuz[i];
        ars_kaynak_t *q = &s->kaynak[s->kaynak_sayi];
        um_kopya(q->kategori, sizeof q->kategori, h->kategori);
        um_kopya(q->baslik, sizeof q->baslik, h->baslik);
        um_kopya(q->kaynak, sizeof q->kaynak, h->kaynak);
        q->tur = h->kaynak_tur;
        q->kademe = a->havuz_kademe[i];
        q->skor = h->skor;
        kanit_cumleleri(&k, h, s->kanit[s->kaynak_sayi], ARS_KANIT_BOY);
        int yeni = 1;
        for (int j = 0; j < i; j++) if (um_esit(a->havuz[i].kaynak, a->havuz[j].kaynak)) yeni = 0;
        s->ayri_kaynak += yeni;
        /* güven, YALNIZ kanıt cümleleri üzerinden ölçülür */
        um_bellek_kopya(&kb[s->kaynak_sayi], h, sizeof *h);
        um_kopya(kb[s->kaynak_sayi].alinti, sizeof kb[s->kaynak_sayi].alinti, s->kanit[s->kaynak_sayi]);
        s->kaynak_sayi++;
    }
    for (int t = 0; t < a->terim_sayi; t++) {
        if (a->terim_bulgu[t] == 0) um_kopya(s->eksik[s->eksik_sayi++], 24, a->terim[t]);
        else if (a->terim_kokle[t]) um_kopya(s->kokle[s->kokle_sayi++], 24, a->terim[t]);
        else um_kopya(s->bulunan[s->bulunan_sayi++], 24, a->terim[t]);
    }
    kanit_t kk;
    kanit_degerlendir(a->soru, kb, s->kaynak_sayi, 0, &kk);
    s->guven = kk.kapsama;
    s->ag_durum = a->ag_durum; s->ag_belge = a->ag_belge;
    um_kopya(s->ag_not, sizeof s->ag_not, a->ag_not);
    s->yeterli = kk.yeterli;
}

static void liste(char *c, uint32_t n, const char (*l)[24], int say) {
    c[0] = 0;
    for (int i = 0; i < say; i++) { if (i) um_ekle(c, n, ", "); um_ekle(c, n, l[i]); }
}

uint32_t ars_sonuc_yaz(const ars_sonuc_t *r, const char *ajan_ad, char *c, uint32_t n) {
    char s[400];
    um_kopya(c, n, "");
    um_bicim(s, sizeof s, "%s araştırdı: %s\n", ajan_ad ? ajan_ad : "UMAY", r->soru);
    um_ekle(c, n, s);
    um_bicim(s, sizeof s, "Tarama: %d sorgu, %d terim; %d ayrı kayıt, %d ayrı kaynaktan.\n",
             r->sorgu_sayisi, r->terim_sayi, r->kaynak_sayi, r->ayri_kaynak);
    um_ekle(c, n, s);

    if (r->kaynak_sayi == 0) {
        um_ekle(c, n, "Bulgu yok: kütüphanemde bu konuya değen kayıt bulamadım; uydurmam.\n");
    } else {
        um_ekle(c, n, "Bulgular:\n");
        for (int i = 0; i < r->kaynak_sayi; i++) {
            const char *kd = r->kaynak[i].kademe == ARS_KENDI ? "" :
                             r->kaynak[i].kademe == ARS_ORTAK ? ", ortak havuzdan" : ", genel depodan";
            if (r->kaynak[i].kademe == ARS_AG || um_esit(r->kaynak[i].kategori, "ag"))
                um_bicim(s, sizeof s, "%d. %s\n   [kaynak: AĞ, doğrulanmadı / %s — %s]\n",
                         i + 1, r->kanit[i], r->kaynak[i].baslik, r->kaynak[i].kaynak);
            else
                um_bicim(s, sizeof s, "%d. %s\n   [kaynak: %s / %s%s]\n",
                         i + 1, r->kanit[i], r->kaynak[i].kategori, r->kaynak[i].baslik, kd);
            if (um_uzun(c) + um_uzun(s) + 200 >= n) {
                um_ekle(c, n, "(Kalan bulgular yere sığmadı.)\n");
                break;
            }
            um_ekle(c, n, s);
        }
    }

    /* 1.5: ağ araştırmasının sonucu da sessizce geçilmez */
    if (r->ag_durum == 2) {
        if (r->ag_not[0] == 'T')
            um_bicim(s, sizeof s, "Ağ araştırması: %d sayfa okundu, belge olarak rafa kondu.\n", r->ag_belge);
        else
            um_bicim(s, sizeof s, "Ağ araştırması sonuç vermedi: %s\n", r->ag_not[0] == 'H' ? r->ag_not + 2 : r->ag_not);
        um_ekle(c, n, s);
    }
    /* bulunamayan ve kökle bulunan terimler — sessizce geçilmez */
    char l[200];
    if (r->eksik_sayi) {
        liste(l, sizeof l, (const char (*)[24])r->eksik, r->eksik_sayi);
        um_bicim(s, sizeof s, "Kayıt bulunamayan: %s.\n", l); um_ekle(c, n, s);
    }
    if (r->kokle_sayi) {
        liste(l, sizeof l, (const char (*)[24])r->kokle, r->kokle_sayi);
        um_bicim(s, sizeof s, "Kökle bulunan (tam sözcük tutmadı): %s.\n", l); um_ekle(c, n, s);
    }
    if (r->catisma.var) {
        um_bicim(s, sizeof s, "Dikkat: %s alanında iki ayrı kaynak (%s / %s); birleştirmedim.\n",
                 r->catisma.kategori, r->catisma.baslik_a, r->catisma.baslik_b);
        um_ekle(c, n, s);
    }
    if (r->kaynak_sayi > 0) {
        um_bicim(s, sizeof s, "Kanıt gücü: %%%d — %s\n", r->guven,
                 r->yeterli ? "bulgular soruyu karşılıyor."
                            : "bulgular soruyu bütünüyle karşılamıyor; kesin hüküm kurmam.");
        um_ekle(c, n, s);
    }
    return um_uzun(c);
}

uint32_t ars_rapor(const arastirma_t *a, const char *ajan_ad, char *c, uint32_t n) {
    static ars_sonuc_t r;
    ars_sonuc_cikar(a, &r);
    return ars_sonuc_yaz(&r, ajan_ad, c, n);
}

int ars_dayanak(const arastirma_t *a, baglam_t *cikan, int en_cok) {
    static ars_sonuc_t r;
    ars_sonuc_cikar(a, &r);
    int k = a->havuz_sayi < en_cok ? a->havuz_sayi : en_cok;
    for (int i = 0; i < k; i++) {
        um_bellek_kopya(&cikan[i], &a->havuz[i], sizeof *cikan);
        um_kopya(cikan[i].alinti, sizeof cikan[i].alinti, r.kanit[i]);   /* ham pencere değil */
    }
    return k;
}
