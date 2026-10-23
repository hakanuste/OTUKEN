/* =====================================================================
 * US ÇIKARIM KATMANI
 * Motorun üstünde durur. Dayanağı, kaynağını, uzmanları, uyarıları ve
 * güven durumunu kaybetmeden tek çıktı sözleşmesine dönüştürür.
 * ===================================================================== */
#include "umay_us.h"
#include "umay_str.h"
#include "umay_port.h"
#include "umay_ortak.h"
#include "umay_alp.h"

static umay_us_t *g_motor;

const char *us_guven_adi(us_guven_t g) {
    switch (g) {
    case GUVEN_SAGLAM: return "sağlam";
    case GUVEN_ORTA:   return "orta";
    case GUVEN_ZAYIF:  return "zayıf";
    default:           return "yok";
    }
}

static const char *kaynak_tur_adi(kaynak_tur_t k) {
    switch (k) {
    case KT_DOKTRIN: return "doktrin";
    case KT_BELGE:   return "belge";
    case KT_DIS:     return "dış kayıt";
    default:         return "bilinmez";
    }
}

/* ---------------- motor yönetimi ---------------- */

int us_motor_tak(umay_us_t *m) {
    g_motor = m ? m : us_stub_motor();
    return g_motor == us_stub_motor() ? UM_DESTEKSIZ : UM_OLDU;
}

umay_us_t *umay_us_al(void) {
    if (!g_motor) g_motor = us_stub_motor();
    return g_motor;
}

const char *us_motor_adi(void) { return umay_us_al()->ad; }

int us_kur(const char *model_yolu) {
    /* Arka uç sırası: PORT'un önerdiği (ÖTÜKEN'de yerel US1), sonra IPC,
     * en son stub. Her birinin hazırlanması GERÇEKTEN başarılı olmalı;
     * başarısız olan atlanır ve bu günlüğe yazılır. Sahte başarı yok. */
    umay_us_t *adaylar[3];
    int n = 0;
    umay_us_t *p = (umay_us_t *)up_us_motoru();
    if (p) adaylar[n++] = p;
    umay_us_t *ipc = us_ipc_motor();
    if (ipc != p) adaylar[n++] = ipc;

    for (int i = 0; i < n; i++) {
        umay_us_t *m = adaylar[i];
        gun_yaz("US: %s deneniyor.", m->ad);
        if (!m->hazirla || m->hazirla(m, model_yolu) == UM_OLDU) {
            g_motor = m;
            gun_yaz("US: %s etkin.", m->ad);
            return UM_OLDU;
        }
        gun_yaz("US: %s hazırlanamadı.", m->ad);
    }

    g_motor = us_stub_motor();
    if (g_motor->hazirla) g_motor->hazirla(g_motor, 0);
    gun_yaz("US: gerçek motor yok, stub'a düştüm.");
    return UM_DESTEKSIZ;
}

/* ---------------- güven derecesi ---------------- */

static void uyari_ekle(us_cikti_t *o, const char *s);

static us_guven_t guven_hesapla(const us_derle_t *i, const kanit_t *k) {
    if (i->baglam_sayi <= 0) return GUVEN_YOK;
    if (i->catisma && i->catisma->var) return GUVEN_ZAYIF;

    us_guven_t g = (i->baglam_sayi >= 2) ? GUVEN_SAGLAM : GUVEN_ORTA;
    /* dayanak nereden geldiyse tavanı o belirler */
    if (i->nereden == US_GENEL_DEPO && g > GUVEN_ZAYIF) g = GUVEN_ZAYIF;
    else if (i->nereden == US_ORTAK_HAVUZ && g > GUVEN_ORTA) g = GUVEN_ORTA;
    /* tek kelimelik zayıf eşleşme sağlam sayılmaz */
    if (g == GUVEN_SAGLAM && i->baglam[0].skor <= 1) g = GUVEN_ORTA;
    /* sorunun bir kısmı dayanakta karşılanmıyorsa sağlam sayılmaz */
    if (g == GUVEN_SAGLAM && k && k->kapsama < 100) g = GUVEN_ORTA;
    return g;
}

/* Kanıt yetersiz: motor çağrılmaz; neden yetmediği açıkça söylenir. */
static void ret_yaz(us_cikti_t *o, const us_derle_t *i, const kanit_t *k,
                    int bengi_var, int oncul_var) {
    o->dayanaksiz = 1;
    o->guven = GUVEN_YOK;
    o->dayanak_sayi = 0;                 /* kanıtı karşılamayan kayıt dayanak değildir */
    char s[320];
    if (i->meclis) {
        /* meclis sesi: bütün hakkında hüküm OTAĞ özetinindir */
        um_kopya(o->metin, sizeof o->metin,
                 "Bu sorunun benim alanıma düşen kısmını doğrulayacak kaydım yok.\n");
        return;
    }
    um_kopya(o->metin, sizeof o->metin, "Bunu doğrulayacak yeterli bilgim yok.");
    if (i->tur != (int)GT_ARASTIR) um_ekle(o->metin, sizeof o->metin, " İstersen araştırabilirim.");
    um_ekle(o->metin, sizeof o->metin, "\n");
    if (i->baglam_sayi <= 0 && !bengi_var) {
        if (oncul_var)
            um_kopya(s, sizeof s, "Elimde yalnız önceki görevin çıktısı var; onu dayanak saymam. "
                                  "Doğrulanmış bir kaynak bulamadığım için sözümü kurmuyorum.\n");
        else
            um_kopya(s, sizeof s, "Kütüphanemde de belleğimde de buna değen bir kayıt bulamadım; "
                                  "yeterli dayanak yok, uydurmam.\n");
    } else if (k->terim_sayi == 0) {
        um_kopya(s, sizeof s, "Sorunda aranacak bir terim bulamadım; uydurmam.\n");
    } else if (!k->odak_kapsandi) {
        um_bicim(s, sizeof s, "En yakın kayıt (%s) sorunun odağı olan \"%s\" terimine "
                 "değmiyor; uydurmam.\n",
                 i->baglam_sayi > 0 ? i->baglam[0].baslik : "doğrulanmış bellek",
                 k->terim[k->odak]);
    } else {
        um_bicim(s, sizeof s, "Bulduğum kayıtlar sorunun ancak %%%d'ini karşılıyor; uydurmam.\n",
                 k->kapsama);
    }
    um_ekle(o->metin, sizeof o->metin, s);
}

/* Üretken motorun sözü dayanağa bağlı değilse: söz atılır; dayanaktan
 * (BENGİ dahil) soru terimlerini en ağır karşılayan cümle, sonra ilkinin
 * karşılamadığı terimleri karşılayan ikinci cümle (varsa) AYNEN verilir. */
static int en_iyi_cumle(const us_derle_t *i, const kanit_t *k, const char *bengi,
                        const int *atla, char *c, uint32_t n, int *tutan) {
    static char aday[US_METIN_MAX];
    int en = -1, tut[KANIT_TERIM_MAX];
    c[0] = 0;
    for (int b = 0; b <= i->baglam_sayi; b++) {
        const char *m = b < i->baglam_sayi ? i->baglam[b].alinti : bengi;
        if (!m) continue;
        int p = kanit_cumle_sec(k, atla, m, aday, sizeof aday, tut);
        if (p > en) {
            en = p;
            um_kopya(c, n, aday);
            for (int t = 0; t < KANIT_TERIM_MAX; t++) tutan[t] = tut[t];
        }
    }
    return en;
}

static void baglilik_denetle(us_cikti_t *o, const us_derle_t *i, const kanit_t *k,
                             const char *bengi) {
    o->baglilik = kanit_bagliligi(o->metin, i->baglam, i->baglam_sayi, bengi);
    if (o->baglilik >= KANIT_BAGLILIK_ESIK) return;
    static char bir[US_METIN_MAX], iki[US_METIN_MAX];
    int tut1[KANIT_TERIM_MAX] = { 0 }, tut2[KANIT_TERIM_MAX] = { 0 };
    if (en_iyi_cumle(i, k, bengi, 0, bir, sizeof bir, tut1) < 0) return;  /* cümle yok: bırak */
    um_kopya(o->metin, sizeof o->metin, bir);
    um_ekle(o->metin, sizeof o->metin, "\n");
    if (en_iyi_cumle(i, k, bengi, tut1, iki, sizeof iki, tut2) > 0 && !um_esit(iki, bir) &&
        um_uzun(o->metin) + um_uzun(iki) + 2 < sizeof o->metin) {
        um_ekle(o->metin, sizeof o->metin, iki);
        um_ekle(o->metin, sizeof o->metin, "\n");
    }
    o->ozutlendi = 1;
    char u[160];
    um_bicim(u, sizeof u, "Modelin sözü dayanağa bağlı değildi (%%%d); kaynaktaki cümleyi "
             "aynen verdim.", o->baglilik);
    uyari_ekle(o, u);
}

/* ---------------- çıkarım ---------------- */

static void uyari_ekle(us_cikti_t *o, const char *s) {
    if (!s || !*s || o->uyari_sayi >= US_UYARI_MAX) return;
    for (int i = 0; i < o->uyari_sayi; i++) if (um_esit(o->uyari[i], s)) return;
    um_kopya(o->uyari[o->uyari_sayi++], 160, s);
}

static void uzman_ekle(us_cikti_t *o, const char *ad) {
    if (!ad || !*ad || o->uzman_sayi >= US_UZMAN_MAX) return;
    for (int i = 0; i < o->uzman_sayi; i++) if (um_esit(o->uzman[i], ad)) return;
    um_kopya(o->uzman[o->uzman_sayi++], UMAY_AD_MAX, ad);
}

static void dayanak_ekle(us_cikti_t *o, const baglam_t *b) {
    if (o->dayanak_sayi >= US_DAYANAK_MAX) return;
    us_dayanak_t *d = &o->dayanak[o->dayanak_sayi++];
    um_kopya(d->kategori, sizeof d->kategori, b->kategori);
    um_kopya(d->baslik,   sizeof d->baslik,   b->baslik);
    um_kopya(d->kaynak,   sizeof d->kaynak,   b->kaynak);
    d->kaynak_tur = b->kaynak_tur;
    d->skor = b->skor;
}

int us_cevap_uret(const us_derle_t *i, us_cikti_t *o) {
    if (!i || !o) return UM_YANLIS;
    um_bellek_doldur(o, 0, sizeof *o);
    o->nereden = i->nereden;
    o->motor = us_motor_adi();

    if (i->uzman_ad && i->uzman_is)
        um_bicim(o->basyazi, sizeof o->basyazi, "%s (%s) söyler:", i->uzman_ad, i->uzman_is);
    uzman_ekle(o, i->uzman_ad);
    uyari_ekle(o, i->uyari);

    for (int b = 0; b < i->baglam_sayi && b < US_DAYANAK_MAX; b++)
        dayanak_ekle(o, &i->baglam[b]);

    /* çatışma: birleştirme yok, iki kaynak da adıyla anılır */
    if (i->catisma && i->catisma->var) {
        o->catisma = 1;
        um_bicim(o->catisma_notu, sizeof o->catisma_notu,
                 "%s alanında iki ayrı kaynak: %s ile %s",
                 i->catisma->kategori, i->catisma->kaynak_a, i->catisma->kaynak_b);
    }

    /* Dayanak yoksa ya da KANIT yetmiyorsa motor HİÇ çağrılmaz — uydurma
     * kapısı burada kapanır. Önceki görevin çıktısı DAYANAK SAYILMAZ ve
     * kanıta katılmaz: tek başına bu kapıyı açmaz. */
    int bengi_var = i->bengi_baglam && i->bengi_baglam[0];
    int oncul_var = i->oncul_baglam && i->oncul_baglam[0];
    o->oncul_kullanildi = oncul_var ? 1 : 0;
    o->baglilik = -1;

    kanit_t *k = &o->kanit;
    kanit_degerlendir(i->soru, i->baglam, i->baglam_sayi, bengi_var ? i->bengi_baglam : 0, k);
    /* GEÇİŞ KAPISI (1.4.7):
     *  - Doğrudan FAKTÜEL yanıt (GT_SORU): kanıt TEK bir kaynak bağlamında
     *    tutarlı olmalı (k->yeterli, kayıt düzeyi kapsama). Ayrı kayıtlara
     *    dağılmış sözcükler tek cevaba birleştirilemez.
     *  - SENTEZ görevleri (ARAŞTIR/ANALİZ/TASARLA) ve MECLİS sesi: iş, birden
     *    çok kaynağı bilerek DERLER ve her birini [kaynak:] ile GÖSTERİR; orada
     *    ölçüt "gerçek bir kanıt payı var mı" (kanit_pay_var). Bu, kaynakları
     *    uydurmaya değil, var olan kaynaklardan derlemeye izin verir.
     * Böylece sarnıç(A)+su(B)+temiz(C) FAKTÜEL soruda reddedilir; ama
     * "kiler ve sarnıç harcı araştır" derlemesi kaynaklarını göstererek koşar. */
    int sentez = (i->tur == GT_ARASTIR || i->tur == GT_ANALIZ || i->tur == GT_TASARLA);
    int gecer = (i->meclis || sentez) ? kanit_pay_var(k) : k->yeterli;
    if (!gecer) {
        ret_yaz(o, i, k, bengi_var, oncul_var);
        return UM_OLDU;
    }

    /* motora verilecek bağlam: önce doğrulanmış hafıza, sonra kaynak alıntıları */
    static char baglam_metni[GOREV_BAGLAM_MAX];
    um_kopya(baglam_metni, sizeof baglam_metni, "");
    if (bengi_var) {
        um_ekle(baglam_metni, sizeof baglam_metni, i->bengi_baglam);
        um_ekle(baglam_metni, sizeof baglam_metni, "\n");
    }
    /* 1.4.7: motora YALNIZ kanıt taşıyan kayıtlar gider. Getirilen ama
     * sorunun hiçbir terimini taşımayan kayıt (alakasız kaynak) motor
     * bağlamına girmez: US1'e doğrulanmamış/ilgisiz metin verilmez.
     * (Alıntı zaten kaynağın TAMAMI değil, en ağır terim çevresindeki
     * sınırlı bir kesittir: BILGI_ALINTI_MAX.) */
    for (int b = 0; b < i->baglam_sayi; b++) {
        if (kanit_kayit_kapsama(k, &i->baglam[b]) <= 0) continue;
        um_ekle(baglam_metni, sizeof baglam_metni, i->baglam[b].alinti);
        um_ekle(baglam_metni, sizeof baglam_metni, "\n");
    }
    /* Öncül görev çıktısı motora gider ama AÇIKÇA etiketlenir; dayanak
     * listesine girmez, güven tavanını yükseltmez. */
    if (oncul_var) {
        um_ekle(baglam_metni, sizeof baglam_metni,
                "[ÖNCEKİ GÖREV ÇIKTISI — doğrulanmamış, kaynak değildir]\n");
        um_ekle(baglam_metni, sizeof baglam_metni, i->oncul_baglam);
        um_ekle(baglam_metni, sizeof baglam_metni, "\n");
    }

    us_istem_t motor_istem = {
        .kisilik   = i->kisilik,
        .baglam    = baglam_metni,
        .soru      = i->soru,
        .niyet     = i->niyet,
        .en_cok_im = i->en_cok_im ? i->en_cok_im : 256
    };

    umay_us_t *m = umay_us_al();
    int sonuc = UM_YANLIS;
    if (m && m->uret) sonuc = m->uret(m, &motor_istem, o->metin, sizeof o->metin);

    /* motor düşerse ya da boş dönerse güvenli yedeğe in */
    if (sonuc != UM_OLDU || !o->metin[0]) {
        umay_us_t *yedek = us_stub_motor();
        if (m != yedek) {
            gun_yaz("US: %s yanıt vermedi, stub yedeğine düştüm.", m ? m->ad : "(motor yok)");
            if (yedek->hazirla) yedek->hazirla(yedek, 0);
            sonuc = yedek->uret(yedek, &motor_istem, o->metin, sizeof o->metin);
            o->motor = yedek->ad;
            o->yedege_dustu = 1;
            uyari_ekle(o, "Asıl çıkarım motoru yanıt vermedi; yedek motorla konuştum.");
        }
        if (sonuc != UM_OLDU || !o->metin[0]) {
            o->dayanaksiz = 1;
            o->guven = GUVEN_YOK;
            um_kopya(o->metin, sizeof o->metin,
                     "Dayanağım vardı ama sözü kuramadım; boş konuşmaktansa susarım.\n");
            return UM_OLDU;
        }
    }

    /* üretken motorun sözü dayanağa bağlı mı (yedek/stub alıntı yapar) */
    if (!o->yedege_dustu && m && (m->yetenek & US_YETENEK_URETKEN))
        baglilik_denetle(o, i, k, bengi_var ? i->bengi_baglam : 0);

    o->guven = guven_hesapla(i, k);
    /* öncül çıktı işe karıştıysa güven ORTA'yı geçemez */
    if (o->oncul_kullanildi && o->guven > GUVEN_ORTA) o->guven = GUVEN_ORTA;
    return UM_OLDU;
}

/* ---------------- ortak çıktı yazımı ---------------- */

uint32_t us_cikti_yaz(const us_cikti_t *o, char *c, uint32_t n) {
    um_kopya(c, n, "");
    if (o->basyazi[0]) { um_ekle(c, n, o->basyazi); um_ekle(c, n, "\n"); }
    um_ekle(c, n, o->metin);
    if (um_uzun(o->metin) && o->metin[um_uzun(o->metin) - 1] != '\n') um_ekle(c, n, "\n");

    for (int i = 0; i < o->uyari_sayi; i++) {
        um_ekle(c, n, o->uyari[i]);
        if (um_uzun(o->uyari[i]) && o->uyari[i][um_uzun(o->uyari[i]) - 1] != '\n')
            um_ekle(c, n, "\n");
    }

    if (o->nereden == US_ORTAK_HAVUZ)
        um_ekle(c, n, "Kendi alanımda kayıt yoktu; ortak havuzdan baktım.\n");
    else if (o->nereden == US_GENEL_DEPO)
        um_ekle(c, n, "Bu soru benim alanıma düşmedi; genel depoya indim, dayanağım oradan.\n");

    if (o->catisma) {
        char s[256];
        um_bicim(s, sizeof s,
                 "Dikkat: %s. İkisini birleştirmedim; hangisine uyacağını sen seç.\n",
                 o->catisma_notu);
        um_ekle(c, n, s);
    }

    if (o->oncul_kullanildi)
        um_ekle(c, n, "(Önceki görevin çıktısını da okudum — doğrulanmamış, "
                      "dayanak saymadım.)\n");

    for (int i = 0; i < o->dayanak_sayi; i++) {
        char s[288];
        um_bicim(s, sizeof s, "(Dayanağım: %s / %s — %s [%s])\n",
                 o->dayanak[i].kategori, o->dayanak[i].baslik,
                 o->dayanak[i].kaynak, kaynak_tur_adi(o->dayanak[i].kaynak_tur));
        um_ekle(c, n, s);
    }

    char g[96];
    um_bicim(g, sizeof g, "Güven: %s\n", us_guven_adi(o->guven));
    um_ekle(c, n, g);
    return um_uzun(c);
}

/* ---------------- meclis özeti ---------------- */

void us_ozet_baslat(us_cikti_t *o) {
    um_bellek_doldur(o, 0, sizeof *o);
    o->guven = GUVEN_SAGLAM;      /* en zayıf parçaya göre inecek */
    o->motor = us_motor_adi();
}

void us_ozet_kat(us_cikti_t *o, const us_cikti_t *p) {
    if (!o || !p) return;
    /* payı olmayan (dayanaksız) sesin sessizliği güveni düşürmez; bütünün
     * yeterliliğine birleşik kanıt karar verir (us_ozet_kuyruk) */
    if (!p->dayanaksiz && p->guven < o->guven) o->guven = p->guven;
    if (p->catisma && !o->catisma) {
        o->catisma = 1;
        um_kopya(o->catisma_notu, sizeof o->catisma_notu, p->catisma_notu);
    }
    if (p->yedege_dustu) o->yedege_dustu = 1;
    if (p->oncul_kullanildi) o->oncul_kullanildi = 1;
    kanit_birlestir(&o->kanit, &p->kanit);
    for (int i = 0; i < p->uzman_sayi; i++) uzman_ekle(o, p->uzman[i]);
    for (int i = 0; i < p->uyari_sayi; i++) uyari_ekle(o, p->uyari[i]);
    for (int i = 0; i < p->dayanak_sayi && o->dayanak_sayi < US_DAYANAK_MAX; i++) {
        int var = 0;
        for (int j = 0; j < o->dayanak_sayi; j++)
            if (um_esit(o->dayanak[j].kaynak, p->dayanak[i].kaynak) &&
                um_esit(o->dayanak[j].baslik, p->dayanak[i].baslik)) { var = 1; break; }
        if (!var) o->dayanak[o->dayanak_sayi++] = p->dayanak[i];
    }
    if (p->dayanaksiz && o->uzman_sayi == 1) o->dayanaksiz = 1;
}

uint32_t us_ozet_kuyruk(const us_cikti_t *o, char *c, uint32_t n) {
    char s[256];
    /* bütünün kanıtı: uzmanların karşıladığı terimlerin birleşimi */
    us_guven_t g = o->dayanak_sayi > 0 ? o->guven : GUVEN_YOK;
    if (o->kanit.terim_sayi > 0 && !o->kanit.yeterli) {
        g = GUVEN_YOK;
        if (o->kanit.odak >= 0 && !o->kanit.odak_kapsandi)
            um_bicim(s, sizeof s, "Bunu bütünüyle doğrulayacak yeterli bilgim yok: \"%s\" hiçbir "
                     "uzmanın kaynağında geçmiyor.\n", o->kanit.terim[o->kanit.odak]);
        else
            um_bicim(s, sizeof s, "Bunu bütünüyle doğrulayacak yeterli bilgim yok: uzmanların "
                     "kaynakları sorunun ancak %%%d'ini karşılıyor.\n", o->kanit.kapsama);
        um_ekle(c, n, s);
    }
    if (o->uzman_sayi > 0) {
        um_kopya(s, sizeof s, "Katılan uzmanlar: ");
        for (int i = 0; i < o->uzman_sayi; i++) {
            if (i) um_ekle(s, sizeof s, ", ");
            um_ekle(s, sizeof s, o->uzman[i]);
        }
        um_ekle(s, sizeof s, "\n");
        um_ekle(c, n, s);
    }
    if (o->catisma) {
        um_bicim(s, sizeof s, "Uzmanlar arasında kaynak çatışması var: %s\n", o->catisma_notu);
        um_ekle(c, n, s);
    }
    um_bicim(s, sizeof s, "Otağın ortak güveni: %s\n", us_guven_adi(g));
    um_ekle(c, n, s);
    return um_uzun(c);
}
