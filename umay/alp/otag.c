/* =====================================================================
 * OTAĞ — görev motoru ve ajan meclisi.
 *
 * Zamanlama: her turda (1) bağımlılığı biten görevleri HAZIR yap,
 * (2) en yüksek öncelikli hazır görevi uygun ajana ver, (3) koşan her
 * ajana bir kısa adım attır, (4) biten sonucu meclise ya da çağırana
 * ver, (5) kalıcı olması gerekeni BENGİ'ye geçir.
 * ===================================================================== */
#include "umay_alp.h"
#include "umay_port.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_bengi.h"
#include "umay_onay.h"

const char *gorev_tur_adi(gorev_tur_t t) {
    switch (t) {
    case GT_SORU:    return "SORU";
    case GT_ARASTIR: return "ARAŞTIR";
    case GT_ANALIZ:  return "ANALİZ";
    case GT_TASARLA: return "TASARLA";
    case GT_KONTROL: return "KONTROL";
    case GT_ONAR:    return "ONAR";
    case GT_IZLE:    return "İZLE";
    case GT_UYGULA:  return "UYGULA";
    default:         return "BİLİNMEZ";
    }
}
const char *gorev_durum_adi(gorev_durum_t d) {
    switch (d) {
    case GD_YENI:        return "yeni";
    case GD_BEKLIYOR:    return "bağımlılıkta";
    case GD_HAZIR:       return "hazır";
    case GD_KOSUYOR:     return "koşuyor";
    case GD_ONAY_BEKLER: return "onay bekler";
    case GD_BITTI:       return "bitti";
    case GD_YANLIS:      return "yanlış";
    default:             return "iptal";
    }
}

/* Görev türü -> zorunlu TÖRE yeteneği. Ajan içindeki denetimler kalkmadı;
 * bu, dağıtımdan önceki BİRİNCİ kapıdır, onlar ikinci savunmadır. */
uint32_t gorev_yetki_gerek(gorev_tur_t t) {
    switch (t) {
    case GT_UYGULA: return YET_DOSYA_YAZ;      /* yıkıcı olabilen iş */
    case GT_IZLE:   return YET_SUREC_GOR;
    default:        return 0;
    }
}

void gorev_hazirla(gorev_t *g, gorev_tur_t tur, const char *istem) {
    um_bellek_doldur(g, 0, sizeof *g);
    g->tur = tur;
    g->durum = GD_YENI;
    g->oncelik = ONC_ORTA;
    g->alan = ALAN_YOK;
    g->niyet = NIYET_SORU;
    if (istem) um_kopya(g->istem, sizeof g->istem, istem);
    /* UYGULA ve ONAR yıkıcı olabilir; varsayılan olarak onay ister */
    g->onay_gerekli = (tur == GT_UYGULA);
}

void otag_kur(otag_t *o) {
    um_bellek_doldur(o, 0, sizeof *o);
    o->sonraki_kimlik = 1;
}

/* ---------------- yetenek bildirimi ---------------- */

int otag_alp_kat(otag_t *o, alp_t *a) {
    if (o->alp_sayisi >= UMAY_ALP_MAX) return UM_DOLU;
    if (a->yetki_gerek && !up_yetkim_var_mi((up_yetki_t)a->yetki_gerek)) {
        gun_yaz("Ajan katılmadı (yetki yok): %s", a->ad);
        return UM_YETKISIZ;
    }
    if (a->tur_maskesi == 0) a->tur_maskesi = GT_HEPSI;   /* bildirmediyse hepsi */
    a->durum = ALP_BOS;
    a->gorev = 0;
    o->alplar[o->alp_sayisi++] = a;
    gun_yaz("Ajan katıldı: %s — %s", a->ad, a->is);
    return UM_OLDU;
}

int otag_alan_var_mi(const otag_t *o, alan_t alan) {
    for (int i = 0; i < o->alp_sayisi; i++)
        if (o->alplar[i]->alan == alan) return 1;
    return 0;
}

void otag_yetenek_dok(const otag_t *o, char *c, uint32_t n) {
    um_kopya(c, n, "Otağdaki ajanlar ve yetenekleri:\n");
    for (int i = 0; i < o->alp_sayisi; i++) {
        const alp_t *a = o->alplar[i];
        char s[192];
        um_bicim(s, sizeof s, "  %s — %s [", a->ad, a->is);
        um_ekle(c, n, s);
        int ilk = 1;
        for (int t = 0; t < GT_SAYI; t++)
            if (a->tur_maskesi & GT_BIT(t)) {
                if (!ilk) um_ekle(c, n, ",");
                um_ekle(c, n, gorev_tur_adi((gorev_tur_t)t));
                ilk = 0;
            }
        um_ekle(c, n, "]\n");
    }
}

/* ---------------- görev havuzu ---------------- */

static gorev_t *bos_yuva(otag_t *o) {
    for (int i = 0; i < UMAY_GOREV_MAX; i++) if (!o->havuz[i].dolu) return &o->havuz[i];
    return 0;
}

gorev_t *otag_gorev_bul(otag_t *o, uint32_t kimlik) {
    if (!kimlik) return 0;
    for (int i = 0; i < UMAY_GOREV_MAX; i++)
        if (o->havuz[i].dolu && o->havuz[i].kimlik == kimlik) return &o->havuz[i];
    return 0;
}

static void kuyruga_koy(otag_t *o, uint32_t indeks) {
    for (int i = 0; i < o->kuyruk_sayi; i++) if (o->kuyruk[i] == indeks) return;
    if (o->kuyruk_sayi >= UMAY_GOREV_MAX) return;
    o->kuyruk[o->kuyruk_sayi++] = indeks;
}

static void kuyruktan_al(otag_t *o, int yer) {
    for (int i = yer; i + 1 < o->kuyruk_sayi; i++) o->kuyruk[i] = o->kuyruk[i + 1];
    o->kuyruk_sayi--;
}

int otag_gorev_ver(otag_t *o, const gorev_t *tanim) {
    gorev_t *g = bos_yuva(o);
    if (!g) return UM_DOLU;
    um_bellek_kopya(g, tanim, sizeof *g);
    g->kimlik = o->sonraki_kimlik++;
    g->dolu = 1;
    g->olustu = up_ms();
    g->ilerleme = 0;
    g->sonuc[0] = 0;
    g->durum = g->bagimlilik_sayi > 0 ? GD_BEKLIYOR : GD_HAZIR;
    kuyruga_koy(o, (uint32_t)(g - o->havuz));
    return (int)g->kimlik;
}

int otag_alt_gorev(otag_t *o, uint32_t ebeveyn, const gorev_t *tanim) {
    gorev_t *ust = otag_gorev_bul(o, ebeveyn);
    if (!ust) return UM_YOK;
    gorev_t yeni = *tanim;
    yeni.ebeveyn = ebeveyn;
    if (yeni.oncelik < ust->oncelik) yeni.oncelik = ust->oncelik;
    int k = otag_gorev_ver(o, &yeni);
    if (k > 0) ust->alt_sayi++;
    return k;
}

int otag_bagimlilik_ekle(otag_t *o, uint32_t gorev, uint32_t onceki) {
    gorev_t *g = otag_gorev_bul(o, gorev);
    if (!g || !otag_gorev_bul(o, onceki)) return UM_YOK;
    if (g->bagimlilik_sayi >= GOREV_BAGIM_MAX) return UM_DOLU;
    g->bagimlilik[g->bagimlilik_sayi++] = onceki;
    if (g->durum == GD_HAZIR || g->durum == GD_YENI) g->durum = GD_BEKLIYOR;
    return UM_OLDU;
}

int otag_ilerleme(otag_t *o, uint32_t kimlik, int yuzde) {
    gorev_t *g = otag_gorev_bul(o, kimlik);
    if (!g) return UM_YOK;
    g->ilerleme = yuzde < 0 ? 0 : (yuzde > 100 ? 100 : yuzde);
    return UM_OLDU;
}

/* Bir ajan çatışma gördüğünde OTAĞ'a bildirir; görev kaydında kalır ve
 * sonuçla birlikte kullanıcıya çıkar. Bilgiler birleştirilmez. */
int otag_catisma_bildir(otag_t *o, uint32_t gorev, const char *not_) {
    gorev_t *g = otag_gorev_bul(o, gorev);
    if (!g) return UM_YOK;
    g->catisma = 1;
    um_kopya(g->catisma_notu, sizeof g->catisma_notu, not_ ? not_ : "");
    gun_yaz("Çatışma bildirildi (görev %u): %s", gorev, g->catisma_notu);
    return UM_OLDU;
}

int otag_catisma_sayi(const otag_t *o) {
    int n = 0;
    for (int i = 0; i < UMAY_GOREV_MAX; i++)
        if (o->havuz[i].dolu && o->havuz[i].catisma) n++;
    return n;
}

int otag_bekleyen_sayi(const otag_t *o) {
    int n = 0;
    for (int i = 0; i < UMAY_GOREV_MAX; i++)
        if (o->havuz[i].dolu &&
            (o->havuz[i].durum == GD_HAZIR || o->havuz[i].durum == GD_BEKLIYOR)) n++;
    return n;
}

/* bağımlılıkları biten görevleri HAZIR yap */
static void bagimliliklari_coz(otag_t *o) {
    for (int i = 0; i < UMAY_GOREV_MAX; i++) {
        gorev_t *g = &o->havuz[i];
        if (!g->dolu || g->durum != GD_BEKLIYOR) continue;
        int hepsi = 1;
        for (int b = 0; b < g->bagimlilik_sayi; b++) {
            gorev_t *on = otag_gorev_bul(o, g->bagimlilik[b]);
            if (!on) continue;                       /* düşmüş görev engel değil */
            if (on->durum != GD_BITTI) { hepsi = 0; break; }
            /* Biten öncülün ÇIKTISI ardıla aktarılır — ama doğrulanmış
             * hafıza bağlamına değil, ayrı bir alana. US bunu dayanak
             * saymaz ve güveni yükseltmez. */
            if (on->sonuc[0] && !um_icinde(g->oncul_baglam, on->sonuc))
                um_ekle(g->oncul_baglam, sizeof g->oncul_baglam, on->sonuc);
        }
        if (hepsi) g->durum = GD_HAZIR;
    }
}

/* ---------------- meclis ---------------- */

int otag_meclis_kur(otag_t *o, const gorev_t *tanim, const alan_t *alanlar, int sayi) {
    alan_t gecerli[UMAY_MECLIS_UYE];
    int n = 0;
    for (int i = 0; i < sayi && n < UMAY_MECLIS_UYE; i++)
        if (otag_alan_var_mi(o, alanlar[i])) gecerli[n++] = alanlar[i];

    if (n == 0) { gorev_t t = *tanim; t.alan = ALAN_YOK; t.kok_kimlik = 0; return otag_gorev_ver(o, &t); }
    if (n == 1) { gorev_t t = *tanim; t.alan = gecerli[0]; t.kok_kimlik = 0; return otag_gorev_ver(o, &t); }

    int m;
    for (m = 0; m < UMAY_MECLIS_MAX && o->meclis[m].dolu; m++) {}
    if (m >= UMAY_MECLIS_MAX) { gorev_t t = *tanim; t.alan = gecerli[0]; t.kok_kimlik = 0; return otag_gorev_ver(o, &t); }

    meclis_t *me = &o->meclis[m];
    um_bellek_doldur(me, 0, sizeof *me);
    me->kok_kimlik = o->sonraki_kimlik;
    me->beklenen = n;
    me->dolu = 1;
    um_kopya(me->soru, sizeof me->soru, tanim->konu);
    me->kanal  = tanim->kanal;
    me->kaynak = tanim->kaynak;

    for (int i = 0; i < n; i++) {
        gorev_t t = *tanim;
        t.alan = gecerli[i];
        t.kok_kimlik = me->kok_kimlik;
        if (otag_gorev_ver(o, &t) < 0) me->beklenen--;
    }
    if (me->beklenen <= 0) { me->dolu = 0; return UM_DOLU; }
    gun_yaz("Meclis kuruldu: %d uzman, kök %u", me->beklenen, me->kok_kimlik);
    return (int)me->kok_kimlik;
}

static meclis_t *meclis_bul(otag_t *o, uint32_t kok) {
    for (int i = 0; i < UMAY_MECLIS_MAX; i++)
        if (o->meclis[i].dolu && o->meclis[i].kok_kimlik == kok) return &o->meclis[i];
    return 0;
}

/* ---------------- ajan seçimi ---------------- */

/* Yetenek denetimi — ajanın meşgul olup olmadığına BAKMAZ.
 * "Bu işi görecek ajan var mı?" ile "şu an boş mu?" ayrı sorulardır:
 * birincisi kalıcı bir eksikliktir, ikincisi geçici. */
static int ajan_yetenekli_mi(const alp_t *a, const gorev_t *g) {
    if (g->hedef_ajan[0]) return um_esit(a->ad, g->hedef_ajan);
    if (!(a->tur_maskesi & GT_BIT(g->tur))) return 0;
    if (a->alan != g->alan) return 0;
    if (a->uygun_mu && !a->uygun_mu(g)) return 0;
    return 1;
}

static int ajan_uyar_mi(const alp_t *a, const gorev_t *g) {
    if (a->durum != ALP_BOS && a->durum != ALP_BITTI) return 0;
    return ajan_yetenekli_mi(a, g);
}

static int yetenekli_ajan_var_mi(const otag_t *o, const gorev_t *g) {
    for (int i = 0; i < o->alp_sayisi; i++)
        if (ajan_yetenekli_mi(o->alplar[i], g)) return 1;
    return 0;
}

/* Görevi kalıcı olarak düşür ve çağırana sebebini bildir. */
static void gorev_dusur(otag_t *o, gorev_t *g, int kuyruk_yeri,
                        const char *sebep, sonuc_t *cikan, int *c, int en_cok) {
    g->durum = GD_YANLIS;
    g->ilerleme = 100;
    g->bitti = up_ms();
    um_kopya(g->sonuc, sizeof g->sonuc, sebep);
    if (kuyruk_yeri >= 0) kuyruktan_al(o, kuyruk_yeri);
    gun_yaz("Görev düştü (%u): %s", g->kimlik, sebep);
    if (*c < en_cok) {
        sonuc_t *s = &cikan[(*c)++];
        um_bellek_doldur(s, 0, sizeof *s);
        s->kimlik = g->kimlik;
        s->basarili = 0;
        s->tur = g->tur;
        s->kanal = g->kanal;
        s->kaynak = g->kaynak;
        um_kopya(s->ad, sizeof s->ad, "OTAĞ");
        um_kopya(s->oz, sizeof s->oz, sebep);
    }
}

/* ---------------- sonucun BENGİ'ye geçmesi ---------------- */

static void bengiye_gecir(const gorev_t *g, const sonuc_t *s) {
    if (!g->bengiye_yaz || !s->basarili || !s->oz[0]) return;
    char anahtar[32];
    um_bicim(anahtar, sizeof anahtar, "gorev%u", g->kimlik);
    char ham[GOREV_SONUC_MAX], deger[192];
    um_bicim(ham, sizeof ham, "[%s/%s] %s", gorev_tur_adi(g->tur), s->ad, s->oz);
    bengi_duzle(ham, deger, sizeof deger);          /* çok satırlı çıktı kaydı bölmesin */
    /* Görev çıktısı KENDİLİĞİNDEN doğrulanmış sayılmaz: ADAY girer; kullanıcı
     * "doğrula <anahtar>" (onaylı YAZMAN işi) ile doğrular. */
    bengi_kaydet(BENGI_SONUC, anahtar, deger, 0);
}

/* ---------------- ana tur ---------------- */

int otag_dondur(otag_t *o, sonuc_t *cikan, int en_cok) {
    int c = 0;

    bagimliliklari_coz(o);

    /* Tur başında "bu turda denendi" işaretlerini temizle */
    for (int i = 0; i < UMAY_GOREV_MAX; i++) o->havuz[i].denendi = 0;

    /* 1) hazır görevleri önceliğe göre uygun ajana dağıt.
     * Atanamayan görev kuyruğu KİLİTLEMEZ: işaretlenip atlanır, sıradaki
     * atanabilir görev çalışmayı sürdürür. */
    for (;;) {
        int en_iyi = -1;
        oncelik_t en_onc = ONC_DUSUK;
        uint64_t en_eski = 0;
        for (int q = 0; q < o->kuyruk_sayi; q++) {
            gorev_t *g = &o->havuz[o->kuyruk[q]];
            if (!g->dolu || g->durum != GD_HAZIR || g->denendi) continue;
            if (en_iyi < 0 || g->oncelik > en_onc ||
                (g->oncelik == en_onc && g->olustu < en_eski)) {
                en_iyi = q; en_onc = g->oncelik; en_eski = g->olustu;
            }
        }
        if (en_iyi < 0) break;

        gorev_t *g = &o->havuz[o->kuyruk[en_iyi]];
        g->denendi = 1;

        /* --- BİRİNCİ KAPI: TÖRE yetkisi. Yetki yoksa görev ajana hiç
         * verilmez; onay istemenin de anlamı kalmaz. --- */
        uint32_t yetki = gorev_yetki_gerek(g->tur);
        if (yetki && !up_yetkim_var_mi((up_yetki_t)yetki)) {
            char sebep[256];
            um_bicim(sebep, sizeof sebep,
                     "%s türü iş için TÖRE yetkim yok; elimi bu işe atamam.\n",
                     gorev_tur_adi(g->tur));
            gorev_dusur(o, g, en_iyi, sebep, cikan, &c, en_cok);
            continue;
        }

        /* --- Bu işi görebilecek ajan hiç yok mu? Kalıcı eksiklik: düşür.
         * Yalnız meşgulse bekletiriz, düşürmeyiz. --- */
        if (!yetenekli_ajan_var_mi(o, g)) {
            char sebep[256];
            um_bicim(sebep, sizeof sebep,
                     "Bu işi görecek ajan otağımda yok (%s / %s). Görevi bıraktım.\n",
                     gorev_tur_adi(g->tur), alan_adi(g->alan));
            gorev_dusur(o, g, en_iyi, sebep, cikan, &c, en_cok);
            continue;
        }

        /* --- İKİNCİ KAPI: kullanıcı onayı --- */
        if (g->onay_gerekli && !g->onay_kimlik[0]) {
            const char *k = onay_iste(gorev_tur_adi(g->tur), "Görev onayı",
                                      g->istem, g->konu);
            if (k) um_kopya(g->onay_kimlik, sizeof g->onay_kimlik, k);
            g->durum = GD_ONAY_BEKLER;
            kuyruktan_al(o, en_iyi);
            continue;
        }

        alp_t *secilen = 0;
        for (int i = 0; i < o->alp_sayisi; i++)
            if (ajan_uyar_mi(o->alplar[i], g)) { secilen = o->alplar[i]; break; }
        if (!secilen) continue;      /* yetenekli ajan var ama meşgul: sonraki tura */

        kuyruktan_al(o, en_iyi);
        g->durum = GD_KOSUYOR;
        g->basladi = up_ms();
        secilen->gorev_kimlik = g->kimlik;
        secilen->kok_kimlik   = g->kok_kimlik;
        secilen->gorev        = g;
        secilen->basla_ms     = up_ms();
        secilen->durum = (secilen->basla(secilen, g) == UM_OLDU) ? ALP_KOSUYOR : ALP_YANLIS;
        if (secilen->durum == ALP_YANLIS) g->durum = GD_YANLIS;
    }

    /* 2) koşan her ajana bir adım */
    for (int i = 0; i < o->alp_sayisi; i++) {
        alp_t *a = o->alplar[i];
        if (a->durum != ALP_KOSUYOR && a->durum != ALP_BEKLIYOR) continue;
        a->durum = a->adim(a);
        if (a->durum != ALP_BITTI && a->durum != ALP_YANLIS) continue;

        gorev_t *g = a->gorev;
        sonuc_t ara;
        um_bellek_doldur(&ara, 0, sizeof ara);
        ara.kimlik   = a->gorev_kimlik;
        ara.basarili = (a->durum == ALP_BITTI);
        ara.alan     = a->alan;
        ara.tur      = g ? g->tur : GT_SORU;
        ara.kanal    = g ? g->kanal : KANAL_YOK;
        ara.kaynak   = g ? g->kaynak : 0;
        um_kopya(ara.ad, sizeof ara.ad, a->ad);
        if (a->topla) a->topla(a, &ara);

        if (g) {
            um_kopya(g->sonuc, sizeof g->sonuc, ara.oz);
            g->durum = ara.basarili ? GD_BITTI : GD_YANLIS;
            g->ilerleme = 100;
            g->bitti = up_ms();
            bengiye_gecir(g, &ara);
            gorev_t *ust = otag_gorev_bul(o, g->ebeveyn);
            if (ust && ust->alt_sayi > 0) ust->alt_sayi--;
        }

        a->durum = ALP_BOS;
        a->gorev = 0;
        o->bitmis++;

        meclis_t *me = a->kok_kimlik ? meclis_bul(o, a->kok_kimlik) : 0;
        a->kok_kimlik = 0;

        if (!me) {
            if (c < en_cok) um_bellek_kopya(&cikan[c++], &ara, sizeof ara);
            continue;
        }
        if (me->gelen == 0) {
            um_bicim(me->birlesik, sizeof me->birlesik,
                     "Bu iş birden çok uzmanı ilgilendirir; otağı topladım.\n\n");
            us_ozet_baslat(&me->ozet);
        }
        /* kuyruk satırlarına (katılan uzmanlar + ortak güven) yer bırak */
        if (um_uzun(me->birlesik) + um_uzun(ara.oz) + 320 < sizeof me->birlesik) {
            um_ekle(me->birlesik, sizeof me->birlesik, ara.oz);
            um_ekle(me->birlesik, sizeof me->birlesik, "\n");
        } else {
            um_ekle(me->birlesik, sizeof me->birlesik,
                    "(Kalan uzman sözleri yere sığmadı; özetini aşağıda veriyorum.)\n");
        }
        /* yapılandırılmış çıktı veren ajanların güven/dayanak/uyarıları
         * ortak özete katılır — düz metin birleştirmesi tek başına yetmez */
        if (a->cikti_al) {
            const us_cikti_t *p = a->cikti_al(a);
            if (p) { us_ozet_kat(&me->ozet, p); me->yapili = 1; }
        }
        me->gelen++;

        if (me->gelen >= me->beklenen && c < en_cok) {
            if (me->yapili) us_ozet_kuyruk(&me->ozet, me->birlesik, sizeof me->birlesik);
            sonuc_t *s = &cikan[c++];
            um_bellek_doldur(s, 0, sizeof *s);
            s->kimlik   = me->kok_kimlik;
            s->kanal    = me->kanal;
            s->kaynak   = me->kaynak;
            s->basarili = 1;
            s->alan     = ALAN_YOK;
            s->tur      = ara.tur;
            um_kopya(s->ad, sizeof s->ad, "OTAĞ");
            um_kopya(s->oz, sizeof s->oz, me->birlesik);
            me->dolu = 0;
        }
    }

    /* 3) bitmiş görevleri havuzdan düşür (ebeveyni bekleyen kalmasın) */
    for (int i = 0; i < UMAY_GOREV_MAX; i++) {
        gorev_t *g = &o->havuz[i];
        if (!g->dolu) continue;
        if (g->durum != GD_BITTI && g->durum != GD_YANLIS && g->durum != GD_IPTAL) continue;
        if (g->alt_sayi > 0) continue;
        int bagli = 0;
        for (int j = 0; j < UMAY_GOREV_MAX && !bagli; j++) {
            gorev_t *d = &o->havuz[j];
            if (!d->dolu || d == g) continue;
            if (d->ebeveyn == g->kimlik) bagli = 1;
            for (int b = 0; b < d->bagimlilik_sayi; b++)
                if (d->bagimlilik[b] == g->kimlik && d->durum != GD_BITTI) bagli = 1;
        }
        if (!bagli) g->dolu = 0;
    }
    return c;
}

int otag_mesgul(const otag_t *o) {
    for (int i = 0; i < UMAY_GOREV_MAX; i++) {
        if (!o->havuz[i].dolu) continue;
        gorev_durum_t d = o->havuz[i].durum;
        if (d == GD_HAZIR || d == GD_BEKLIYOR || d == GD_KOSUYOR || d == GD_YENI) return 1;
    }
    for (int i = 0; i < o->alp_sayisi; i++)
        if (o->alplar[i]->durum == ALP_KOSUYOR || o->alplar[i]->durum == ALP_BEKLIYOR) return 1;
    for (int i = 0; i < UMAY_MECLIS_MAX; i++) if (o->meclis[i].dolu) return 1;
    return 0;
}
