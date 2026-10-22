/* =====================================================================
 * 1.4.7 CEKIRDEK REGRESYON SINAMALARI (yalniz TANI kipi)
 *
 *   ASYOK-TEST     P0: adres alani olusturulamazsa surec OLUSMAZ (cekirdek
 *                  alanina geri dusus YOK); kaynak sizmaz. Bellek yetersizligi
 *                  GERCEKTEN uretilir (bos cercevelerin hepsi alinir) - sahte
 *                  (mock) ayirici YOKTUR.
 *   FD-TEST        tanitici sahipligi, kusak (bayat tanitici), cift kapatma,
 *                  gecersiz tanitici, capraz gorev, olumde temizlik.
 *   POLITIKA-TEST  syscall TORE politika tablosu: eksiksiz; yetkisiz cagri
 *                  reddedilir, yetkili gecer, SERBEST herkese acik.
 *   YUZEY-TEST     pencere nesne sahipligi, boyut siniri, olumde temizlik.
 *   TOREREAP-TEST  olen gorevin TORE kaydi silinir.
 *   SUREC-SMP-TEST yasam dongusu: baska cekirdekte CALISAN surec yok
 *                  edilmez (istek reddedilir, yeniden denenir); hepsi sonunda
 *                  yok edilir, adres alanlari GERCEKTEN geri alinir.
 *
 * "Ring3 cagirani" iki sinamada (POLITIKA, YUZEY) cekirdek sinama gorevinde
 * task->kullanici=1 ile temsil edilir: GERCEK politika kapisi ve GERCEK TORE
 * kaydi kosar (Ring3'ten gelen syscall da ayni syscall_dispatch'e girer).
 * Gercek Ring3 programlari (kabuk, UMAY, sinama/ELF) profilleriyle zaten
 * calisir; yanlis profil onlari bozardi (kabuk ve UMAY sinamalari bunu olcer).
 *
 * Her sonuc TEK serial_write satiridir (bolunen satir baska gorevin
 * ciktisiyla karisabiliyordu). Her bekleme zaman asimlidir: basarisizlik
 * acilisi ASLA kilitlemez.
 * ===================================================================== */
#include "sinama147.h"
#include "process.h"
#include "sched.h"
#include "thread.h"
#include "vfs.h"
#include "tore.h"
#include "syscall.h"
#include "ulgen.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "timer.h"
#include "serial.h"
#include "spinlock.h"
#include "cpu.h"
#include "smp.h"

/* ---- tek satir kurucu ---- */
typedef struct { char b[320]; int n; } sat_t;
static void s_ek(sat_t *s, const char *t) {
    while (*t && s->n < (int)sizeof s->b - 2) s->b[s->n++] = *t++;
    s->b[s->n] = 0;
}
static void s_sayi(sat_t *s, uint64_t v) {
    char t[24]; int i = 0;
    if (v == 0) t[i++] = '0';
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i && s->n < (int)sizeof s->b - 2) s->b[s->n++] = t[--i];
    s->b[s->n] = 0;
}
static void s_isaretli(sat_t *s, int64_t v) {
    if (v < 0) { s_ek(s, "-"); s_sayi(s, (uint64_t)(-v)); } else s_sayi(s, (uint64_t)v);
}
static int sonuc_yaz(sat_t *s, int hata) {
    s_ek(s, hata ? " -> HATA\n" : " -> BASARILI\n");
    serial_write(s->b);
    return hata ? 1 : 0;
}

/* TORE kaydi acan sinama yardimcilarinin kimlikleri (TOREREAP-TEST icin):
 * olculen, KUSURSUZ bu gorevlerin kaydidir - kuresel sayi degil (sinama
 * sirasinda baska gorevler, orn. kabuk, kendi kaydini acar). */
static volatile uint32_t tore_yardimci[8];
static volatile int      tore_yardimci_n = 0;
static void tore_yardimci_kaydet(uint32_t id) {
    int i = __atomic_fetch_add(&tore_yardimci_n, 1, __ATOMIC_ACQ_REL);
    if (i < 8) tore_yardimci[i] = id;
}

/* Zaman asimli bekleme: kosul saglanana ya da 'tik' dolana dek uyu. */
#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

/* =============================== ASYOK =============================== */
static volatile int asyok_kostu = 0;
static void asyok_giris(void) { asyok_kostu = 1; for (;;) thread_sleep(100); }
static void asvar_giris(void) { process_exit_current(); }

static int sinama_asyok(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "ASYOK-TEST: adres alani yokken surec");
    /* Dizi ONCE ayrilir (heap buyumesi cerceve isteyebilir); sonra kesmeler
     * kapali bolgede butun bos cerceveler alinir: zamanlayici ve AP'ler bu
     * aralikta calismaz (sinama AP dagitimindan ONCE kosar). */
    uint64_t kap = pmm_free_frames() + 64;
    uint64_t *liste = (uint64_t *)kmalloc((size_t)kap * sizeof(uint64_t));
    if (!liste) { s_ek(&s, ": sinama dizisi ayrilamadi"); return sonuc_yaz(&s, 1); }

    uint64_t f = irq_kaydet_kapat();
    uint64_t bos0 = pmm_free_frames();
    uint32_t gorev0 = scheduler_task_count();
    int surec0 = process_count_active();
    int64_t alan0 = vmm_canli_alan_sayisi();
    uint64_t n = 0;
    while (n < kap) { uint64_t fr = pmm_alloc_frame(); if (!fr) break; liste[n++] = fr; }
    uint64_t kalan = pmm_free_frames();
    process_t *p = process_create("AsYokSinama", asyok_giris);   /* GERCEK yol */
    uint32_t gorev1 = scheduler_task_count();
    int surec1 = process_count_active();
    int64_t alan1 = vmm_canli_alan_sayisi();
    for (uint64_t i = 0; i < n; i++) pmm_free_frame(liste[i]);
    uint64_t bos1 = pmm_free_frames();
    /* KONTROL: bellek varken ayni cagri basarili ve surec KENDI alaninda */
    process_t *pk = process_create("AsVarSinama", asvar_giris);
    int k_ok = pk && pk->address_space != 0 && pk->task &&
               pk->task->cr3 == pk->address_space && pk->task->adres_sahibi == 1;
    irq_geri_yukle(f);
    kfree(liste);
    thread_sleep(5);                     /* yanlislikla olusmus gorev kossa bayrak kalkardi */

    int hata = 0;
    if (kalan != 0)          hata |= 1;   /* bellek gercekten tukenmedi: sinama gecersiz */
    if (p != 0)              hata |= 2;   /* surec olustu: GERI DUSUS VAR */
    if (gorev1 != gorev0)    hata |= 4;   /* gorev eklendi */
    if (surec1 != surec0)    hata |= 8;   /* surec yuvasi tuketildi */
    if (alan1 != alan0)      hata |= 16;  /* adres alani sayaci degisti */
    if (bos1 != bos0)        hata |= 32;  /* cerceve sizdi */
    if (asyok_kostu)         hata |= 64;  /* giris kosu: surec calisti */
    if (!k_ok)               hata |= 128; /* kontrol: bellek varken olusmadi */
    s_ek(&s, " (alinan cerceve "); s_sayi(&s, n);
    s_ek(&s, ", kalan "); s_sayi(&s, kalan);
    s_ek(&s, ") olusmadi="); s_sayi(&s, p == 0);
    s_ek(&s, " gorev_farki="); s_isaretli(&s, (int64_t)gorev1 - (int64_t)gorev0);
    s_ek(&s, " cerceve_farki="); s_isaretli(&s, (int64_t)bos1 - (int64_t)bos0);
    s_ek(&s, " kontrol_kendi_alani="); s_sayi(&s, (uint64_t)k_ok);
    s_ek(&s, " hata="); s_sayi(&s, (uint64_t)hata);
    return sonuc_yaz(&s, hata);
}

/* ================================ FD ================================ */
static volatile int fdy_tut = -2, fdy_cik = 0, fdy_bitti = 0;
static void fd_yardimci(void) {
    fdy_tut = vfs_open("/OTUKEN.TXT");
    while (!fdy_cik) thread_sleep(1);
    fdy_bitti = 1;
    thread_exit();                       /* dosyayi KAPATMADAN olur */
}

static int sinama_fd(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "FD-TEST: sahiplik/kusak/cift kapatma/capraz gorev/olumde temizlik");
    int hata = 0; char b[16];
    int once = vfs_acik_sayisi();
    int h1 = vfs_open("/OTUKEN.TXT");
    if (h1 < 16) hata |= 1;                              /* tanitici >= 16 */
    if (vfs_close(h1) != 0) hata |= 2;
    if (vfs_close(h1) != -1) hata |= 4;                  /* cift kapatma guvenli */
    int h2 = vfs_open("/OTUKEN.TXT");
    int ayni_yuva = ((h1 & 0xF) == (h2 & 0xF));
    if (h2 == h1) hata |= 8;                             /* kusak artmali */
    if (vfs_read(h1, b, 8) != -1) hata |= 16;            /* BAYAT tanitici eski nesneye erisemez */
    if (vfs_read(h2, b, 8) <= 0) hata |= 32;             /* guncel tanitici calisir */
    if (vfs_read(-1, b, 8) != -1 || vfs_read(0x7FFFFFFF, b, 8) != -1 ||
        vfs_close(12345) != -1 || vfs_lseek(-5, 0) != (uint64_t)-1) hata |= 64;
    vfs_close(h2);

    fdy_tut = -2; fdy_cik = 0; fdy_bitti = 0;
    thread_create("FdYardimci", fd_yardimci);
    BEKLE(fdy_tut != -2, 250);
    int yt = fdy_tut;
    if (yt < 16) hata |= 128;
    if (vfs_read(yt, b, 8) != -1) hata |= 256;           /* A'nin taniticisini B kullanamaz */
    if (vfs_close(yt) != -1) hata |= 512;                /* ...kapatamaz da */
    int yasarken = vfs_acik_sayisi();
    fdy_cik = 1;
    BEKLE(fdy_bitti, 250);
    int sonra = -1;
    for (int d = 0; d < 60; d++) {
        thread_sleep(2);
        scheduler_reap_terminated();
        sonra = vfs_acik_sayisi();
        if (sonra == once) break;
    }
    if (yasarken != once + 1) hata |= 1024;
    if (sonra != once) hata |= 2048;                     /* olen gorevin dosyasi kapandi */
    s_ek(&s, " (ayni_yuva_yeni_kusak="); s_sayi(&s, (uint64_t)ayni_yuva);
    s_ek(&s, " acik_once="); s_isaretli(&s, once);
    s_ek(&s, " yardimci_yasarken="); s_isaretli(&s, yasarken);
    s_ek(&s, " reap_sonrasi="); s_isaretli(&s, sonra);
    s_ek(&s, ") hata="); s_sayi(&s, (uint64_t)hata);
    return sonuc_yaz(&s, hata);
}

/* ============================== POLITIKA ============================== */
#define GECERSIZ_KULLANICI_ADRESI 0x8000123000ULL   /* haritasiz kullanici adresi */
static volatile int pol_sonuc = -1;
static void politika_gorev(void) {
    task_t *ben = scheduler_current_task();
    int hata = 0;
    ben->kullanici = 1;          /* Ring3 cagirani: GERCEK kapi + GERCEK TORE kaydi */
    tore_yardimci_kaydet(ben->id);
    uint32_t d0; uint64_t r;
    /* 1) yetkisi olmayan cagri REDDEDILIR (TORE ret sayaci +1) */
    tore_set_capabilities(ben->id, KUT_EKRAN);
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_OPEN, GECERSIZ_KULLANICI_ADRESI, 0, 0, 0);
    if (r != (uint64_t)-1 || tore_denied_count() != d0 + 1) hata |= 1;
    /* 2) yetkisi olan cagri kapidan GECER (ret sayaci degismez; isaretci
     *    haritasiz oldugu icin cagri kendisi -1 doner) */
    tore_set_capabilities(ben->id, KUT_DISK_READ);
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_OPEN, GECERSIZ_KULLANICI_ADRESI, 0, 0, 0);
    if (tore_denied_count() != d0) hata |= 2;
    /* 3) SERBEST cagri BOS profilde bile gecer */
    tore_set_capabilities(ben->id, 0);
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_GETPID, 0, 0, 0, 0);
    if (r != ben->id || tore_denied_count() != d0) hata |= 4;
    /* 4) KABUK profili KURESEL ayari (tema) degistiremez */
    tore_set_capabilities(ben->id, TORE_PROFILE_KABUK);
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_TEMA, GECERSIZ_KULLANICI_ADRESI, 0, 0, 0);
    if (r != (uint64_t)-1 || tore_denied_count() != d0 + 1) hata |= 8;
    /* 5) 1.4.9: UMAY profili DISK_YAZ ALIR (BENGI kalici olsun diye) ama
     *    yalniz yazma kokunun altina (tore_yazma_koku; DIZIN-YAZ-TEST kokun
     *    disini dener). Kapi MKDIR'i gecirir; SUREC olusturma hala YOK. */
    tore_set_capabilities(ben->id, TORE_PROFILE_UMAY);
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_MKDIR, GECERSIZ_KULLANICI_ADRESI, 0, 0, 0);
    if (r != (uint64_t)-1 || tore_denied_count() != d0) hata |= 16;
    d0 = tore_denied_count();
    r = syscall_dispatch(SYS_PROC_LISTE, GECERSIZ_KULLANICI_ADRESI, 0, 0, 0);
    if (r != (uint64_t)-1 || tore_denied_count() != d0 + 1) hata |= 32;
    pol_sonuc = hata;
    thread_exit();
}

static int sinama_politika(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "POLITIKA-TEST: syscall TORE tablosu");
    int hata = 0;
    int politikasiz = syscall_politikasiz_sayisi();
    int serbest = 0, yetkili = 0;
    for (uint64_t i = 0; i < SYS_MAX; i++) {
        int64_t y = syscall_gereken_yetki(i);
        if (y == 0) serbest++;
        else if (y > 0) yetkili++;
    }
    if (politikasiz != 0) hata |= 1;
    pol_sonuc = -1;
    thread_create("PolitikaSinama", politika_gorev);
    BEKLE(pol_sonuc != -1, 250);
    if (pol_sonuc != 0) hata |= 2 | (pol_sonuc << 8);
    s_ek(&s, " (yetkili "); s_sayi(&s, (uint64_t)yetkili);
    s_ek(&s, ", serbest "); s_sayi(&s, (uint64_t)serbest);
    s_ek(&s, ", politikasiz "); s_sayi(&s, (uint64_t)politikasiz);
    s_ek(&s, "; kapi_hatasi="); s_isaretli(&s, pol_sonuc);
    s_ek(&s, ") hata="); s_sayi(&s, (uint64_t)hata);
    return sonuc_yaz(&s, hata);
}

/* =============================== YUZEY =============================== */
static volatile int yz_id = -2, yz_a = -1, yz_b = -1, yz_cik = 0, yz_bitti = 0;
static void yuzey_sahip(void) {           /* A: yuzeyi olusturur */
    task_t *ben = scheduler_current_task();
    tore_set_capabilities(ben->id, KUT_EKRAN);
    tore_yardimci_kaydet(ben->id);
    ben->kullanici = 1;
    int id = ulgen_yuzey_olustur("S147", 10, 10, 40, 30);
    yz_a = (id >= 0 && syscall_dispatch(SYS_PEN_DOLDUR, (uint64_t)id, 0x00FF00, 0, 0) == 0) ? 0 : 1;
    yz_id = id;
    while (!yz_cik) thread_sleep(1);
    yz_bitti = 1;
    thread_exit();                         /* yuzeyi YOK ETMEDEN olur -> reap temizler */
}
static void yuzey_yabanci(void) {         /* B: A'nin yuzeyine dokunmaya calisir */
    task_t *ben = scheduler_current_task();
    tore_set_capabilities(ben->id, KUT_EKRAN);
    tore_yardimci_kaydet(ben->id);
    ben->kullanici = 1;
    int h = 0;
    if (syscall_dispatch(SYS_PEN_DOLDUR, (uint64_t)yz_id, 0xFF0000, 0, 0) != (uint64_t)-1) h |= 1;
    if (syscall_dispatch(SYS_PEN_KAPAT, (uint64_t)yz_id, 0, 0, 0) != (uint64_t)-1) h |= 2;
    yz_b = h;
    thread_exit();
}

static int sinama_yuzey(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "YUZEY-TEST: pencere sahipligi/boyut siniri/olumde temizlik");
    int hata = 0;
    yz_id = -2; yz_a = -1; yz_b = -1; yz_cik = 0; yz_bitti = 0;
    thread_create("YuzeySahip", yuzey_sahip);
    BEKLE(yz_id != -2, 250);
    int id = yz_id;
    if (id < 0) hata |= 1;
    if (yz_a != 0) hata |= 2;                             /* sahibi cizebilir */
    thread_create("YuzeyYabanci", yuzey_yabanci);
    BEKLE(yz_b != -1, 250);
    if (yz_b != 0) hata |= 4 | (yz_b << 8);               /* yabanci cizemez, kapatamaz */
    uint32_t sahip = 0;
    if (ulgen_yuzey_sahibi(id, &sahip) != 0) hata |= 8;   /* yabanci kapatamadi: hala var */
    if (ulgen_yuzey_olustur("x", 0, 0, 65535, 65535) != -1) hata |= 16;   /* dev pencere */
    if (ulgen_yuzey_olustur("x", 0, 0, 0, 10) != -1) hata |= 32;          /* sifir boyut */
    yz_cik = 1;
    BEKLE(yz_bitti, 250);
    int temiz = 0;
    for (int d = 0; d < 60 && !temiz; d++) {
        thread_sleep(2);
        scheduler_reap_terminated();
        temiz = (ulgen_yuzey_sahibi(id, &sahip) != 0);
    }
    if (!temiz) hata |= 64;                               /* olen gorevin penceresi silindi */
    s_ek(&s, " (sahip_cizer="); s_sayi(&s, (uint64_t)(yz_a == 0));
    s_ek(&s, " yabanci_red="); s_sayi(&s, (uint64_t)(yz_b == 0));
    s_ek(&s, " olumde_silindi="); s_sayi(&s, (uint64_t)temiz);
    s_ek(&s, ") hata="); s_sayi(&s, (uint64_t)hata);
    return sonuc_yaz(&s, hata);
}

/* ============================ TORE REAP ============================ */
static int sinama_tore_reap(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "TOREREAP-TEST: olen sinama gorevlerinin TORE kaydi silindi");
    int n = tore_yardimci_n < 8 ? tore_yardimci_n : 8;
    int kalan = n;
    for (int d = 0; d < 60 && kalan; d++) {
        scheduler_reap_terminated();
        kalan = 0;
        for (int i = 0; i < n; i++) if (tore_kayit_var_mi(tore_yardimci[i])) kalan++;
        if (kalan) thread_sleep(2);
    }
    s_ek(&s, " (yardimci "); s_sayi(&s, (uint64_t)n);
    s_ek(&s, ", kaydi kalan "); s_sayi(&s, (uint64_t)kalan); s_ek(&s, ")");
    return sonuc_yaz(&s, n == 0 || kalan != 0);   /* hic yardimci yoksa sinama gecersiz */
}

int sinama147_tek_cekirdek(void) {
    serial_write("\n===== 1.4.7 SINAMALARI (tek cekirdek evresi) =====\n");
    scheduler_reap_terminated();
    tore_yardimci_n = 0;
    int t = 0;
    t += sinama_asyok();
    t += sinama_fd();
    t += sinama_politika();
    t += sinama_yuzey();
    t += sinama_tore_reap();
    return t;
}

/* ============================ SUREC-SMP ============================ */
#define SMP_SUREC 6
static volatile uint64_t smp_is = 0;
/* Surec cogu zaman CALISIR (uzun dongu), arada kisa uyur. Boylece birden
 * cok cekirdekte, yok etme istegi hedefi cogunlukla BASKA cekirdekte
 * calisirken yakalar (reddedilmeli), uyurken yakalayinca da yok eder. */
static void smp_surec_giris(void) {
    for (uint64_t k = 0;; k++) {
        for (volatile int i = 0; i < 300000; i++) { }
        __atomic_add_fetch(&smp_is, 1, __ATOMIC_RELAXED);
        if ((k & 7) == 7) thread_sleep(1);
    }
}

int sinama147_surec_smp(void) {
    sat_t s = { {0}, 0 };
    s_ek(&s, "SUREC-SMP-TEST: calisan surec yok edilmez, istek bekler, alan geri alinir");
    for (int d = 0; d < 5; d++) { scheduler_reap_terminated(); thread_sleep(2); }
    int64_t alan0 = vmm_canli_alan_sayisi();
    int surec0 = process_count_active();
    uint32_t pid[SMP_SUREC];
    int olustu = 0;
    for (int i = 0; i < SMP_SUREC; i++) {
        process_t *p = process_create("SmpSurec", smp_surec_giris);
        pid[i] = p ? p->pid : 0;
        if (p) olustu++;
    }
    thread_sleep(30);                         /* diger cekirdeklere yayilsin */
    int64_t alan_orta = vmm_canli_alan_sayisi();
    uint64_t is_orta = smp_is;
    int yok = 0; uint64_t red = 0;
    uint64_t t0 = timer_get_ticks();
    while (yok < olustu && timer_get_ticks() - t0 < 500) {
        for (int i = 0; i < SMP_SUREC; i++) {
            if (!pid[i]) continue;
            if (process_destroy(pid[i]) == 0) { pid[i] = 0; yok++; }
            else red++;                       /* calisiyor/bloke: istek reddedildi, sonra yeniden */
        }
        thread_sleep(1);
    }
    int64_t alan_son = -1;
    for (int d = 0; d < 60; d++) {
        scheduler_reap_terminated();
        alan_son = vmm_canli_alan_sayisi();
        if (alan_son == alan0) break;
        thread_sleep(2);
    }
    int hata = 0;
    if (olustu != SMP_SUREC) hata |= 1;
    if (alan_orta != alan0 + SMP_SUREC) hata |= 2;     /* her surec kendi alaninda */
    if (is_orta == 0) hata |= 4;                        /* surecler gercekten calisti */
    if (yok != olustu) hata |= 8;                       /* hepsi sonunda yok edildi */
    if (alan_son != alan0) hata |= 16;                  /* adres alanlari geri alindi */
    if (process_count_active() != surec0) hata |= 32;   /* surec tablosu temiz */
    /* Birden cok cekirdekte, baska cekirdekte CALISAN hedefe gelen istek en
     * az bir kez REDDEDILMIS olmali: korumanin gercekten devreye girdiginin
     * kaniti (tek cekirdekte hedef, yok eden kosarken calisamaz: 0 olagan). */
    int ncpu = cpu_sayisi();
    int dagitim = smp_dagitim_acik();       /* AP'ler gorev aliyor mu */
    if (ncpu > 1 && dagitim && red == 0) hata |= 64;
    s_ek(&s, " (cekirdek "); s_sayi(&s, (uint64_t)ncpu);
    s_ek(&s, ", surec "); s_sayi(&s, (uint64_t)olustu);
    s_ek(&s, ", yok_edilen "); s_sayi(&s, (uint64_t)yok);
    s_ek(&s, ", reddedilen_istek "); s_sayi(&s, red);
    s_ek(&s, ", canli_alan "); s_isaretli(&s, alan0);
    s_ek(&s, "->"); s_isaretli(&s, alan_orta);
    s_ek(&s, "->"); s_isaretli(&s, alan_son);
    s_ek(&s, ") hata="); s_sayi(&s, (uint64_t)hata);
    return sonuc_yaz(&s, hata);
}
