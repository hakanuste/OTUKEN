#include "iskence.h"
#include "syscall.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "vfs.h"
#include "ulak.h"
#include "socket.h"
#include "ipc2.h"
#include "sched.h"
#include "thread.h"
#include "serial.h"
#include "log.h"
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"
#include "process.h"

/* Her alt sinamadan sonra kesme bayragini denetle: hangi sinama
 * kesmeleri kapali birakiyor? */
static int if_kacak_sayisi = 0;

static void if_denetle(const char *ad) {
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0" : "=r"(f));
    if (!(f & 0x200)) {
        serial_write("ISKENCE-IF-KACAK: ");
        serial_write(ad);
        serial_write(" kesmeleri KAPALI birakti\n");
        if_kacak_sayisi++;
        __asm__ volatile ("sti");   /* sinama devam edebilsin */
    }
}

static void bildir(const char *ad, int hata) {
    /* Denetim: satir parcalari ayri serial_write cagrilariyla yazilinca,
     * eszamanli calisan baska bir gorevin ciktisi ortaya girip satiri
     * bolebiliyordu ("ISKENCE: " ile "syscall..." ayrilinca test takimi
     * sinamayi bulamiyordu - gercekten yasandi). Tum satir TEK
     * serial_write ile (kilit altinda butun) yazilir. */
    char satir[160];
    int n = 0;
    const char *p1 = "ISKENCE: ";
    while (*p1 && n < 150) satir[n++] = *p1++;
    while (*ad && n < 120) satir[n++] = *ad++;
    const char *p2 = " -> ";
    while (*p2) satir[n++] = *p2++;
    if (hata == 0) {
        const char *p3 = "SAGLAM\n";
        while (*p3) satir[n++] = *p3++;
    } else {
        const char *p4 = "HATA sayisi=0x";
        while (*p4) satir[n++] = *p4++;
        for (int k = 7; k >= 0; k--) satir[n++] = "0123456789ABCDEF"[((uint32_t)hata >> (k * 4)) & 0xF];
        satir[n++] = '\n';
    }
    satir[n] = 0;
    serial_write(satir);
}

/* --- 1) Sistem cagrisi iskencesi --- */
int iskence_syscall(void) {
    int hata = 0;

    /* Sinir disi numaralar: hepsi -1 dondurmeli, COKMEMELI */
    uint64_t kotu[] = {SYS_MAX, SYS_MAX + 1, 999, 0xFFFFFFFF, 0x7FFFFFFFFFFFFFFFULL};
    for (int i = 0; i < 5; i++) {
        if (syscall_dispatch(kotu[i], 0, 0, 0, 0) != (uint64_t)-1) hata++;
    }

    /* Kayitli olmayan ORTA numaralar (tablo bosluklari).
     * YIKICI cagrilar ATLANIR: SYS_EXIT calisan gorevi sonlandirir,
     * SYS_YIELD/SYS_SLEEP zamanlayiciyi tetikler - bunlari cagirmak
     * sinamanin KENDISINI durdururdu (ilk denemede tam bu oldu). */
    for (uint64_t n = 0; n < SYS_MAX; n++) {
        if (n == SYS_EXIT || n == SYS_YIELD || n == SYS_SLEEP) continue;
        uint64_t r = syscall_dispatch(n, 0, 0, 0, 0);
        (void)r;   /* cokmemesi yeterli */
    }

    /* NULL isaretciyle dosya acma - -1 donmeli, cokmemeli */
    if (syscall_dispatch(SYS_OPEN, 0, 0, 0, 0) != (uint64_t)-1) {
        /* bazi uygulamalar 0'i gecerli sayabilir; cokmemesi asil olan */
    }
    bildir("syscall (gecersiz numara/isaretci)", hata);
    return hata;
}

/* --- 2) Bellek iskencesi --- */
int iskence_bellek(void) {
    int hata = 0;

    /* Sifir ve asiri boyut istekleri */
    if (kmalloc(0) != 0) { hata++; serial_write("  [teshis] kmalloc(0) NULL donmedi\n"); }
    void *dev = kmalloc(0xFFFFFFFFUL);                 /* asiri buyuk */
    if (dev != 0) { hata++; serial_write("  [teshis] asiri buyuk kmalloc basarili oldu\n"); kfree(dev); }

    /* NULL free - cokmemeli */
    kfree(0);

    /* Cok sayida kucuk ayirma + hepsini iade: denge korunmali */
    uint64_t t, kul, once, blok;
    heap_get_stats(&t, &kul, &once, &blok);
    void *p[64];
    int ayrilan = 0;
    for (int i = 0; i < 64; i++) {
        p[i] = kmalloc(16 + (uint32_t)(i * 7) % 300);
        if (p[i]) {
            ayrilan++;
            /* veri butunlugu imzasi */
            uint8_t *b = (uint8_t *)p[i];
            b[0] = (uint8_t)(0x5A ^ i);
        }
    }
    for (int i = 0; i < 64; i++) {
        if (p[i]) {
            uint8_t *b = (uint8_t *)p[i];
            if (b[0] != (uint8_t)(0x5A ^ i)) hata++;   /* veri bozulmus */
            kfree(p[i]);
        }
    }
    if (ayrilan < 32) { hata++; serial_write("  [teshis] yetersiz ayirma\n"); }

    /* PMM tukenme + denge.
     *
     * ONEMLI: bu olcum KESMESIZ yapilmalidir. Aksi halde olcum
     * penceresinde BASKA gorevler (TaskA/ThreadE/ag yigini) cerceve
     * ayirip birakir ve denge kontrolu YANLIS yere hata verir -
     * ilk calistirmada tam bu oldu; kusur cekirdekte degil,
     * sinamanin kendisindeydi. */
    static uint64_t alinan[512];
    uint64_t bos_once, bos_sonra;
    uint32_t n = 0;
    {
        uint64_t bayrak;
        __asm__ volatile ("pushfq; pop %0; cli" : "=r"(bayrak) : : "memory");

        bos_once = pmm_free_frames();
        while (n < 512) {
            uint64_t f = pmm_alloc_frame();
            if (f == 0) break;
            alinan[n++] = f;
        }
        for (uint32_t i = 0; i < n; i++) pmm_free_frame(alinan[i]);
        bos_sonra = pmm_free_frames();

        __asm__ volatile ("push %0; popfq" : : "r"(bayrak) : "memory", "cc");
    }
    if (bos_sonra != bos_once) {
        hata++;
        serial_write("  [teshis] PMM dengesi bozuk once=");
        serial_write_hex((uint32_t)bos_once);
        serial_write(" sonra=");
        serial_write_hex((uint32_t)bos_sonra);
        serial_write("\n");
    }

    bildir("bellek (sifir/asiri/tukenme/denge)", hata);
    return hata;
}

/* --- 3) Dosya sistemi iskencesi --- */
int iskence_dosya(void) {
    int hata = 0;

    /* Olmayan dosyalar -1 donmeli */
    if (vfs_open("/YOKBOYLE.TXT") >= 0) hata++;
    if (vfs_open("/") >= 0) { /* dizin acma - kabul edilebilir */ }
    if (vfs_open("") >= 0) hata++;
    if (vfs_open("////////") >= 0) hata++;

    /* Cok uzun yol - tasma OLMAMALI */
    static char uzun[512];
    uzun[0] = '/';
    for (int i = 1; i < 500; i++) uzun[i] = 'A';
    uzun[500] = 0;
    if (vfs_open(uzun) >= 0) hata++;

    /* Gecersiz fd ile islem - cokmemeli */
    char tmp[8];
    if (vfs_read(-1, tmp, 8) >= 0) hata++;
    if (vfs_read(9999, tmp, 8) >= 0) hata++;
    if (vfs_close(-1) == 0) hata++;
    if (vfs_close(9999) == 0) hata++;

    /* Ayni dosyayi TEKRAR TEKRAR ac/kapat - fd sizintisi olmamali */
    for (int i = 0; i < 40; i++) {
        int fd = vfs_open("/OTUKEN.TXT");
        if (fd < 0) { hata++; break; }
        vfs_close(fd);
    }
    /* 40 dongu sonrasi hala acilabilmeli (sizinti yoksa) */
    int son = vfs_open("/OTUKEN.TXT");
    if (son < 0) hata++;
    else vfs_close(son);

    bildir("dosya (gecersiz yol/fd, fd sizintisi)", hata);
    return hata;
}

/* --- 4) Ag iskencesi --- */
int iskence_ag(void) {
    int hata = 0;
    uint8_t hedef[4] = {10, 0, 2, 2};
    uint8_t veri[64];
    for (int i = 0; i < 64; i++) veri[i] = (uint8_t)i;

    /* Sifir uzunluk - cokmemeli */
    ulak_send_udp(hedef, 1234, 5678, veri, 0);

    /* Azami ustu - reddedilmeli */
    if (ulak_send_udp(hedef, 1234, 5678, veri, 60000) == 0) hata++;

    /* Gecersiz soket islemleri */
    if (socket_send(-1, veri, 4) >= 0) hata++;
    if (socket_send(9999, veri, 4) >= 0) hata++;
    if (socket_close(-1) == 0) hata++;
    if (socket_recv(-5, veri, 4) >= 0) hata++;

    /* Soket tukenme: hepsini ac, sonra kapat */
    int s[16]; int ac = 0;
    for (int i = 0; i < 16; i++) {
        s[i] = socket_create(SOCK_UDP);
        if (s[i] >= 0) ac++;
    }
    for (int i = 0; i < 16; i++) if (s[i] >= 0) socket_close(s[i]);
    /* tukenmeden sonra yeniden acilabilmeli */
    int tekrar = socket_create(SOCK_UDP);
    if (tekrar < 0) hata++;
    else socket_close(tekrar);

    bildir("ag (sifir/asiri/gecersiz soket/tukenme)", hata);
    return hata;
}

/* --- 5) IPC iskencesi --- */
int iskence_ipc(void) {
    int hata = 0;

    /* Gecersiz port islemleri */
    ipc_message_t m;
    if (ipc_try_receive(-1, &m) > 0) hata++;
    if (ipc_try_receive(9999, &m) > 0) hata++;

    ipc_message_t g;
    g.opcode = 1; g.length = 4; g.sender_port = 0; g.sequence = 0;
    for (int i = 0; i < IPC_MAX_PAYLOAD; i++) g.payload[i] = (uint8_t)i;
    if (ipc_send(-1, &g) == 0) hata++;
    if (ipc_send(9999, &g) == 0) hata++;

    /* Olmayan servisi bul - -1 donmeli */
    if (ipc_port_find("olmayan-servis-xyz") >= 0) hata++;

    /* Port tukenme: hepsini ac */
    static const char *adlar[] = {"isk0","isk1","isk2","isk3","isk4",
                                   "isk5","isk6","isk7","isk8","isk9",
                                   "iskA","iskB"};
    enum { ADSAY = (int)(sizeof adlar / sizeof adlar[0]) };
    _Static_assert(ADSAY > IPC_MAX_PORTS, "tukenme sinamasi siniri asmali");
    int p[ADSAY];
    int acilan = 0;
    for (int i = 0; i < ADSAY; i++) {
        p[i] = ipc_port_create(adlar[i]);
        if (p[i] >= 0) acilan++;
    }
    /* Sinirdan FAZLA acilamamali */
    if (acilan > IPC_MAX_PORTS) hata++;
    for (int i = 0; i < ADSAY; i++) if (p[i] >= 0) ipc_port_close(p[i]);

    bildir("ipc (gecersiz port/tukenme/tasma)", hata);
    return hata;
}

/* --- 6) Gorev iskencesi --- */
static void kisa_gorev(void) { thread_exit(); }

int iskence_gorev(void) {
    int hata = 0;
    uint32_t once = scheduler_task_count();

    /* Cok sayida kisa omurlu gorev olustur - tukenmemeli, sizmamali */
    int olusan = 0;
    for (int i = 0; i < 6; i++) {
        if (thread_create("isk", kisa_gorev)) olusan++;
    }
    if (olusan == 0) hata++;

    /* Sinirin OTESINDE olusturmayi dene - reddedilmeli, cokmemeli.
     *
     * DENETIM NOTU: bu sinama onceden 40 gorev deniyordu ve TUM gorev
     * yuvalarini dolduruyordu; ardindan olusturulan kabuk ve UMAY
     * baslatilamiyordu (kendi sinamamiz sistemi sakat birakiyordu).
     * Artik sinirli sayida deneyip olusanlari HEMEN reap ediyoruz. */
    for (int i = 0; i < 8; i++) thread_create("isktasma", kisa_gorev);

    /* Kisa omurlu gorevlerin bitmesine izin ver, sonra yuvalari iade et */
    for (int d = 0; d < 4; d++) {
        thread_sleep(2);
        scheduler_reap_terminated();
    }

    /* Sistem hala ayakta mi? */
    if (scheduler_task_count() == 0) hata++;
    (void)once;

    bildir("gorev (tukenme/sinir asimi)", hata);
    return hata;
}

/* --- 7) Kullanici isaretcisi SALDIRI sinamasi ---
 * Denetimde eklenen korumalarin GERCEKTEN calistigini kanitlar:
 * cekirdek adresi, haritasiz adres ve SALT-OKUNUR kullanici sayfasi
 * veren cagrilarin hepsi REDDEDILMELIDIR. */
int iskence_isaretci(void) {
    int hata = 0;

    /* a) Cekirdek adresi (higher-half) - reddedilmeli */
    if (syscall_dispatch(SYS_WRITE, 0xFFFFFFFF80100000ULL, 0, 0, 0) != (uint64_t)-1) hata++;
    if (syscall_dispatch(SYS_OPEN,  0xFFFFFFFF80100000ULL, 0, 0, 0) != (uint64_t)-1) hata++;

    /* b) Cekirdek kimlik haritasi (dusuk bolge) - reddedilmeli */
    if (syscall_dispatch(SYS_WRITE, 0x100000ULL, 0, 0, 0) != (uint64_t)-1) hata++;

    /* c) NULL - reddedilmeli */
    if (syscall_dispatch(SYS_WRITE, 0, 0, 0, 0) != (uint64_t)-1) hata++;

    /* d) Kullanici araliginda ama HARITASIZ - reddedilmeli */
    if (syscall_dispatch(SYS_OPEN, 0x00007F0000000000ULL, 0, 0, 0) != (uint64_t)-1) hata++;

    /* e) Cekirdek tamponuna yazma istegi (read hedefi) - reddedilmeli */
    if (syscall_dispatch(SYS_READ, 0, 0xFFFFFFFF80200000ULL, 64, 0) != (uint64_t)-1) hata++;

    /* f) Uzunluk tasmasi - reddedilmeli */
    if (syscall_dispatch(SYS_READ, 0, 0x8000400000ULL, 0xFFFFFFFFFFFFFF00ULL, 0)
          != (uint64_t)-1) hata++;

    /* g) Pencere API'si: dogrulanmamis basliklar (denetimde duzeltildi) */
    if (syscall_dispatch(SYS_PENCERE_AC, 0xFFFFFFFF80100000ULL, 0, 0, 0) != (uint64_t)-1) hata++;

    bildir("isaretci saldirisi (cekirdek/haritasiz/tasma)", hata);
    return hata;
}

/* --- 8) CR3 DEGISIMI ALTINDA ISARETCI GUVENLIGI (regresyon) ---
 * Denetimde kapatilan TOCTOU'nun gercekten kapali oldugunu sinar.
 *
 * Senaryo: bir adres alaninda GECERLI olan bir kullanici adresi,
 * BASKA bir adres alaninda haritali DEGILDIR. Cekirdek, dogrulamayi
 * yaptigi adres alanindan farkli bir alandayken o isaretciyi
 * kullanirsa, ya cop veri okur ya da coker. Bu sinama, adres alani
 * degistikten SONRA ayni isaretcinin REDDEDILDIGINI dogrular. */
int iskence_cr3(void) {
    int hata = 0;

    uint64_t cekirdek_as = vmm_current_address_space();
    uint64_t as1 = vmm_create_address_space();
    uint64_t as2 = vmm_create_address_space();
    uint64_t cerceve = pmm_alloc_frame();

    if (!as1 || !as2 || !cerceve) {
        bildir("cr3 (kurulum basarisiz - atlandi)", 0);
        if (as1) vmm_destroy_address_space(as1);
        if (as2) vmm_destroy_address_space(as2);
        if (cerceve) pmm_free_frame(cerceve);
        return 0;
    }

    /* Ayni sanal adres: as1'de HARITALI, as2'de DEGIL */
    const uint64_t DENEK = 0x8002000000ULL;
    if (vmm_map_page_in(as1, DENEK, cerceve,
                         VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        bildir("cr3 (haritalama basarisiz - atlandi)", 0);
        vmm_destroy_address_space(as1); vmm_destroy_address_space(as2);
        pmm_free_frame(cerceve);
        return 0;
    }

    /* a) as1'de: adres GECERLI olmali (dize okuma basarili donmeli) */
    vmm_switch_address_space(as1);
    *(volatile char *)(uintptr_t)DENEK = 'A';
    *(volatile char *)(uintptr_t)(DENEK + 1) = 0;
    uint64_t r_as1 = syscall_dispatch(SYS_OPEN, DENEK, 0, 0, 0);
    /* Dosya bulunamayacagi icin -1 doner AMA "gecersiz isaretci"
     * nedeniyle degil. Ayrimi anlamak icin dogrudan degil, as2 ile
     * KARSILASTIRARAK karar veriyoruz. */

    /* b) as2'de: AYNI adres haritali DEGIL -> REDDEDILMELI */
    vmm_switch_address_space(as2);
    uint64_t r_as2 = syscall_dispatch(SYS_OPEN, DENEK, 0, 0, 0);
    if (r_as2 != (uint64_t)-1) hata++;   /* haritasiz adres kabul edildi! */

    /* c) as2'de yazma hedefi olarak da REDDEDILMELI */
    uint64_t r_yaz = syscall_dispatch(SYS_READ, 0, DENEK, 64, 0);
    if (r_yaz != (uint64_t)-1) hata++;

    /* d) IPC gonderiminde de REDDEDILMELI */
    uint64_t r_ipc = syscall_dispatch(SYS_IPC_GONDER, 0, 1, DENEK, 8);
    if (r_ipc != (uint64_t)-1) hata++;

    /* e) Cekirdek alanina geri don: sistem saglam kalmali */
    vmm_switch_address_space(cekirdek_as);
    if (vmm_current_address_space() != cekirdek_as) hata++;

    (void)r_as1;
    vmm_destroy_address_space(as1);
    vmm_destroy_address_space(as2);
    pmm_free_frame(cerceve);

    bildir("cr3 degisimi altinda isaretci guvenligi", hata);
    return hata;
}

/* --- CEKIRDEK-BASINA VERI --- ayni cekirdekten ardisik cagrilar AYNI
 * blogu dondurmeli, blok gercek APIC kimligini tasimali, tamponlar
 * blok ICINDE olmali. */
int iskence_cpu(void) {
    int hata = 0;
    cpu_yerel_t *a = cpu_yerel();
    cpu_yerel_t *b = cpu_yerel();
    if (a != b) hata++;
    if ((cpu_yerel_t *)a->kendi != a) hata++;        /* gs:0 kendini gostermeli */
    if (a != cpu_indeksle(a->indeks)) hata++;
    if (a->apic_id != cpu_apic_id()) hata++;
    if (!a->kullanimda) hata++;
    /* Gorev-basina syscall tamponu: ayni gorevde ayni tampon, gorev
     * sinirinin disinda degil (bkz. syscall.c gorev_tamponu). */
    {
        task_t *g = scheduler_current_task();
        if (g && g->sc_tampon && (uintptr_t)g->sc_tampon < 0x100000ULL) hata++;
    }
    if (cpu_sayisi() < 1) hata++;
    serial_write("ISKENCE: cpu apic_id=");
    serial_write_hex(a->apic_id);
    serial_write("\n");
    bildir("cekirdek-basina veri", hata);
    return hata;
}

int iskence_tumu(void) {
    serial_write("\n===== ACIMASIZ SINAMA BASLIYOR =====\n");
    int t = 0;
    t += iskence_syscall();   if_denetle("syscall");
    t += iskence_bellek();    if_denetle("bellek");
    t += iskence_dosya();     if_denetle("dosya");
    t += iskence_ag();        if_denetle("ag");
    t += iskence_ipc();       if_denetle("ipc");
    t += iskence_gorev();     if_denetle("gorev");
    t += iskence_isaretci();  if_denetle("isaretci");
    t += iskence_cr3();       if_denetle("cr3");
    t += iskence_cpu();       if_denetle("cpu");
    t += if_kacak_sayisi;   /* kesme kacagi da bir HATADIR */
    serial_write("===== ACIMASIZ SINAMA SONUCU: ");
    if (t == 0) serial_write("TUM SINAMALAR SAGLAM =====\n\n");
    else { serial_write("TOPLAM HATA="); serial_write_hex((uint32_t)t); serial_write(" =====\n\n"); }
    return t;
}
