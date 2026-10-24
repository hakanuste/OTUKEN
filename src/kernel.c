#include <stdint.h>
#include "multiboot.h"
#include "multiboot2.h"
#include "ekran_sinama.h"
#include "serial.h"
#include "smp.h"
#include "dede.h"
#include "modul.h"
#include "smap.h"
#include "fpu.h"
#include "ergenekon.h"
#include "fat16_ic.h"
#include "cpu.h"
#include "idt.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "disk.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "sched.h"
#include "process.h"
#include "thread.h"
#include "mutex.h"
#include "msgqueue.h"
#include "pipe.h"
#include "shm.h"
#include "syscall.h"
#include "tss.h"
#include "ring3.h"
#include "vfs.h"
#include "vfs_fat16.h"
#include "fat16.h"
#include "fat32.h"
#include "vfs_fat32.h"
#include "disk_cache.h"
#include "driver.h"
#include "pci.h"
#include "usb.h"
#include "gpt.h"
#include "uhci.h"
#include "ahci.h"
#include "nvme.h"
#include "ulak.h"
#include "tcp.h"
#include "socket.h"
#include "dhcp.h"
#include "dns.h"
#include "aghizmet.h"
#include "timur.h"
#include "tore.h"
#include "uygulama.h"
#include "kut.h"
#include "umay.h"
#include "kararlilik.h"
#include "iskence.h"
#include "sinama147.h"
#include "sinama148.h"
#include "sinama149.h"
#include "sinama_kervan.h"
#include "sinama_uyg.h"
#include "sinama_asena.h"
#include "sinama_dizin.h"
#include "sinama_sse.h"
#include "ses.h"
#include "ag.h"
#include "rtc.h"
#include "kervan2.h"
#include "surum.h"
#include "ipc2.h"
#include "servis.h"
#include "ulgen.h"
#include "kervan.h"
#include "log.h"
#include "otukenconf.h"
#include "spinlock.h"
#include "elf.h"
#include "timer.h"
#include "gui.h"

/* Denetim duzeltmesi: oz-sinama ciktilari sadece TANI MODU'nda
 * yazilir. Uretim modunda acilis sessizdir - gercek bir sistem her
 * acilista onlarca test raporu basmaz. */
/* Tani ciktisi seri katmanin ONEK tamponundan gecer (serial.h:
 * seri_onek_ekle): "X -> " gibi parcalar gorev basina bekletilir ve
 * ayni gorevin sonraki serial_write'i ile AYNI kilit tutusunda basilir.
 * Boylece "onek + sonuc" satiri baska gorevin ciktisiyla bolunmez
 * (GUVENLIK-5 bu yuzden rastgele KALDI veriyordu; bkz. ders 5). */
static void tani_yaz(const char *s) {
    if (OTUKEN_TANI_MODU) seri_onek_ekle(s);
}
static void tani_hex(uint32_t v) {
    if (!OTUKEN_TANI_MODU) return;
    char t[11]; const char *h = "0123456789ABCDEF";
    t[0] = '0'; t[1] = 'x';
    for (int i = 0; i < 8; i++) t[2 + i] = h[(v >> (28 - 4 * i)) & 0xF];
    t[10] = 0;
    seri_onek_ekle(t);
}


static volatile uint64_t task_a_counter = 0;
static volatile uint64_t task_b_counter = 0;

/* Birden fazla gorev ayni anda serial_write cagirinca ciktinin karakter
 * duzeyinde karismasi (daha once "kozmetik sinirlama" olarak belgelendi)
 * - artik gercek bir mutex'le cozuluyor. ONEMLI: coklu parcali bir
 * mesaji (metin+sayi+satirsonu) TEK bir kilit altinda yazdirmak gerekir
 * - her parcayi AYRI AYRI kilitlemek arada baskasinin araya girmesini
 * engellemez, bu yuzden burada kasitli olarak boyle bir "guvenli ama
 * yanlis" yardimci fonksiyon YOK; her gorev kendi mesaj bloğunu
 * mutex_lock/unlock ile sarmalar (asagidaki entry fonksiyonlarina bakin). */
static mutex_t print_mutex;

static void task_a_entry(void) {
    for (;;) {
        /* DUZELTME: eskiden bu dongu hic uyumuyordu ve CPU'yu surekli
         * %100 mesgul ediyordu. Artik her turda uyuyarak islemciyi
         * gercek islere birakiyor. */
        thread_sleep(OTUKEN_DEMO_UYKU);
        task_a_counter++;
        if (task_a_counter % 20 == 0) {
            mutex_lock(&print_mutex);
            serial_write("[TaskA] calisiyor, sayac=");
            serial_write_hex((uint32_t)task_a_counter);
            serial_write("\n");
            mutex_unlock(&print_mutex);
        }
    }
}

static void task_b_entry(void) {
    for (;;) {
        /* DUZELTME: eskiden bu dongu hic uyumuyordu ve CPU'yu surekli
         * %100 mesgul ediyordu. Artik her turda uyuyarak islemciyi
         * gercek islere birakiyor. */
        thread_sleep(OTUKEN_DEMO_UYKU);
        task_b_counter++;
        if (task_b_counter % 20 == 0) {
            mutex_lock(&print_mutex);
            serial_write("[TaskB] calisiyor, sayac=");
            serial_write_hex((uint32_t)task_b_counter);
            serial_write("\n");
            mutex_unlock(&print_mutex);
        }
    }
}

static volatile uint64_t task_c_counter = 0;
static volatile int task_c_finished = 0;

static void task_c_entry(void) {
    /* SINIRLI is yapip kendi kendine sonlanan surec ornegi. */
    for (uint64_t i = 0; i < 1000000; i++) {
        task_c_counter++;
    }
    serial_write("[TaskC] islerini bitirdi (sayac=");
    serial_write_hex((uint32_t)task_c_counter);
    serial_write("), kendi kendine sonlaniyor\n");
    task_c_finished = 1;
    process_exit_current(); /* ASLA GERI DONMEZ */
}

static volatile uint64_t task_d_counter = 0;

static void task_d_entry(void) {
    /* Sonsuz calisan, DISARIDAN oldurulecek surec ornegi.
     * DUZELTME: artik uyuyor - bos dongu CPU israfiydi. */
    for (;;) {
        thread_sleep(OTUKEN_DEMO_UYKU);
        task_d_counter++;
        if (task_d_counter % 20 == 0) {
            mutex_lock(&print_mutex);
            serial_write("[TaskD] calisiyor, sayac=");
            serial_write_hex((uint32_t)task_d_counter);
            serial_write("\n");
            mutex_unlock(&print_mutex);
        }
    }
}

static volatile uint32_t thread_e_wake_count = 0;
static volatile uint32_t thread_f_wake_count = 0;

static void thread_e_entry(void) {
    /* Her 30 tick'te (50Hz'de ~0.6sn) bir uyanip iz birakan thread. */
    for (;;) {
        thread_sleep(30);
        thread_e_wake_count++;
        mutex_lock(&print_mutex);
        serial_write("[ThreadE] uyandim (Sleep/Wake calisiyor), sayac=");
        serial_write_hex(thread_e_wake_count);
        serial_write("\n");
        mutex_unlock(&print_mutex);
    }
}

static void thread_f_entry(void) {
    /* Her 75 tick'te (50Hz'de ~1.5sn) bir uyanan, DAHA SEYREK calisan
     * ikinci thread - E'den farkli periyotta olmasi, meşgul-bekleme
     * (busy-wait) DEGIL, GERCEK uyku oldugunu kanitlar. */
    for (;;) {
        thread_sleep(75);
        thread_f_wake_count++;
        mutex_lock(&print_mutex);
        serial_write("[ThreadF] uyandim (daha seyrek periyot), sayac=");
        serial_write_hex(thread_f_wake_count);
        serial_write("\n");
        mutex_unlock(&print_mutex);
    }
}

/* --- Senkronizasyon kaniti: mutex olmadan KAYIP GUNCELLEME (lost
 * update) riski olan, kasitli GENIS bir kritik bolgeye sahip paylasimli
 * sayac. Iki thread ayni sayaci "oku-degistir-yaz" ile artiriyor;
 * mutex dogru calisiyorsa sonuc TAM OLARAK 2*SYNC_ITERS olmali. */
#define SYNC_ITERS 150
static mutex_t counter_mutex;
static volatile uint64_t shared_counter = 0;
static volatile int thread_g_done = 0;
static volatile int thread_h_done = 0;

static void thread_g_entry(void) {
    for (int i = 0; i < SYNC_ITERS; i++) {
        mutex_lock(&counter_mutex);
        uint64_t tmp = shared_counter;
        for (volatile int j = 0; j < 3000; j++) { /* kasitli genis pencere */ }
        shared_counter = tmp + 1;
        mutex_unlock(&counter_mutex);
    }
    serial_write("[ThreadG] bitti\n");
    thread_g_done = 1;
    thread_exit();
}

static void thread_h_entry(void) {
    for (int i = 0; i < SYNC_ITERS; i++) {
        mutex_lock(&counter_mutex);
        uint64_t tmp = shared_counter;
        for (volatile int j = 0; j < 3000; j++) { }
        shared_counter = tmp + 1;
        mutex_unlock(&counter_mutex);
    }
    serial_write("[ThreadH] bitti\n");
    thread_h_done = 1;
    thread_exit();
}

/* --- Stage 10 kaniti: gercek Ring3 (kullanici modu) yurutmesi.
 * Bu gorev ring3_run_test() cagirir - bu fonksiyon ASLA GERI DONMEZ
 * (ring3 kodu SYS_EXIT ile bu gorevi sonlandirir). */
extern volatile int g_ring3_proof_received;

static volatile int g_elf_ran = 0;
static int g_tcp_conn = -1;

/* --- Asama 31/32: gercek servisler ve IPC ile haberlesme --- */
#define OP_TOPLA   1
#define OP_YANKILA 2

static volatile int g_zaman_servis_id = -1;
static volatile int g_hesap_servis_id = -1;
static volatile int g_ipc_test_ok = -1;
static volatile uint32_t g_zaman_nabiz = 0;

/* "zamand" servisi: duzenli nabiz atar (izleme kanıtı) */
static void servis_zamand(void) {
    for (;;) {
        servis_nabiz(g_zaman_servis_id);
        g_zaman_nabiz++;
        thread_sleep(10);
    }
}

/* "hesapd" servisi: IPC uzerinden istek alir, YANIT dondurur.
 * Gercek bir istek/yanit (RPC) sunucusudur. */
static void servis_hesapd(void) {
    int port = ipc_port_create("hesapd");
    if (port < 0) { thread_exit(); }

    for (;;) {
        servis_nabiz(g_hesap_servis_id);
        ipc_message_t msg;
        while (ipc_try_receive(port, &msg) == 1) {
            ipc_message_t yanit;
            /* Denetim (1.4.6): yanit yiginda ilklenmemisti; IPC_AL tum yapiyi
             * kullaniciya kopyaladigindan eski cekirdek yigini baytlari Ring3
             * cagirana sizabiliyordu. Once tamamen sifirla. */
            for (uint32_t z = 0; z < sizeof yanit; z++) ((volatile uint8_t *)&yanit)[z] = 0;
            yanit.opcode = msg.opcode;
            yanit.sequence = msg.sequence;
            yanit.sender_port = (uint32_t)port;
            yanit.length = 0;

            if (msg.opcode == OP_TOPLA) {
                /* payload: iki uint32 -> toplamini dondur */
                uint32_t a = (uint32_t)msg.payload[0] | ((uint32_t)msg.payload[1] << 8) |
                             ((uint32_t)msg.payload[2] << 16) | ((uint32_t)msg.payload[3] << 24);
                uint32_t b = (uint32_t)msg.payload[4] | ((uint32_t)msg.payload[5] << 8) |
                             ((uint32_t)msg.payload[6] << 16) | ((uint32_t)msg.payload[7] << 24);
                uint32_t s = a + b;
                yanit.payload[0] = (uint8_t)(s & 0xFF);
                yanit.payload[1] = (uint8_t)((s >> 8) & 0xFF);
                yanit.payload[2] = (uint8_t)((s >> 16) & 0xFF);
                yanit.payload[3] = (uint8_t)((s >> 24) & 0xFF);
                yanit.length = 4;
            } else if (msg.opcode == OP_YANKILA) {
                for (uint32_t i = 0; i < msg.length && i < IPC_MAX_PAYLOAD; i++)
                    yanit.payload[i] = msg.payload[i];
                yanit.length = msg.length;
            }
            ipc_send((int)msg.sender_port, &yanit);
        }
        thread_sleep(2);
    }
}

/* IPC istemcisi: hesapd'ye RPC cagrisi yapip sonucu DOGRULAR */
static void ipc_istemci_gorev(void) {
    thread_sleep(30);   /* servislerin ayaga kalkmasini bekle */

    int myport = ipc_port_create("istemci");
    if (myport < 0) { g_ipc_test_ok = 0; thread_exit(); }

    ipc_message_t istek, yanit;
    istek.opcode = OP_TOPLA;
    istek.length = 8;
    uint32_t a = 40000, b = 2345;
    istek.payload[0] = (uint8_t)(a & 0xFF);       istek.payload[1] = (uint8_t)((a >> 8) & 0xFF);
    istek.payload[2] = (uint8_t)((a >> 16) & 0xFF); istek.payload[3] = (uint8_t)((a >> 24) & 0xFF);
    istek.payload[4] = (uint8_t)(b & 0xFF);       istek.payload[5] = (uint8_t)((b >> 8) & 0xFF);
    istek.payload[6] = (uint8_t)((b >> 16) & 0xFF); istek.payload[7] = (uint8_t)((b >> 24) & 0xFF);

    int r = ipc_call("hesapd", myport, &istek, &yanit, 100);
    if (r == 0) {
        uint32_t sonuc = (uint32_t)yanit.payload[0] | ((uint32_t)yanit.payload[1] << 8) |
                          ((uint32_t)yanit.payload[2] << 16) | ((uint32_t)yanit.payload[3] << 24);
        tani_yaz("IPC2-TEST: hesapd RPC cagrisi 40000+2345 -> ");
        tani_hex(sonuc);
        serial_write(" (beklenen 0x");
        tani_hex(a + b);
        serial_write(") -> ");
        serial_write((sonuc == a + b) ? "BASARILI\n" : "HATA\n");
        g_ipc_test_ok = (sonuc == a + b) ? 1 : 0;
    } else {
        tani_yaz("IPC2-TEST: RPC cagrisi BASARISIZ (zaman asimi)\n");
        g_ipc_test_ok = 0;
    }
    thread_exit();
}

static volatile int g_shell_active = 0;

/* 1.5: ARAYICI — UMAY'in ag arastirmacisi; KENDI adres alaninda, dar
 * profille. Yazma koku yetki kaydi acildiktan SONRA verilir. */
static void arayici_giris(void) {
    task_t *ben = scheduler_current_task();
    if (ben) {
        tore_set_capabilities((uint32_t)ben->id, TORE_PROFILE_ARAYICI);
        tore_yazma_koku((uint32_t)ben->id, ARAYICI_YAZMA_KOKU);
    }
    uygulama_elf_calistir("/ARAYICI.ELF", TORE_PROFILE_ARAYICI);
}
static void arayici_baslat(void) {
    int fd = vfs_open("/ARAYICI.ELF");
    if (fd < 0) { serial_write("ARAYICI: diskte yok, ag arastirmasi kapali\n"); return; }
    vfs_close(fd);
    if (!process_create("Arayici", arayici_giris)) serial_write("ARAYICI: surec olusturulamadi\n");
}

/* UMAY gorevi: diskteki programi yukleyip Ring3'e gecer. */
static void umay_gorev_entry(void) {
    if (ring3_umay_calistir("/UMAY.ELF") != 0) {
        serial_write("UMAY-HOST: baslatilamadi\n");
    }
    thread_exit();
}

/* Kabuk 'cik' ile sonlanirsa sistem kullanilamaz hale gelirdi.
 * Gercek bir sistemde birincil arayuz her zaman ayakta olmalidir -
 * bu yuzden kabuk cikarsa yeniden baslatiliyor. */
static void shell_task_entry(void);

static void shell_yeniden_baslat_gorev(void) {
    thread_sleep(25);
    thread_create("OtukenKabuk", shell_task_entry);
    thread_exit();
}

static void shell_task_entry(void) {
    g_shell_active = 1;   /* ana dongu artik tuslari TUKETMESIN */
    ring3_run_shell();
    g_shell_active = 0;
    serial_write("kabuk: sonlandi - yeniden baslatiliyor\n");
    thread_create("KabukYeniden", shell_yeniden_baslat_gorev);
    thread_exit();
}

static void ring3_elf_task_entry(void) {
    ring3_run_elf();
    serial_write("ring3-elf: HATA - ring3_run_elf geri dondu!\n");
    thread_exit();
}

static void ring3_test_task_entry(void) {
    ring3_run_test();
    /* BURAYA ASLA ULASILMAMALI */
    serial_write("ring3-test: HATA - ring3_run_test geri dondu!\n");
    thread_exit();
}

/* --- Stage 19 kaniti: heap yaris durumu stres testi.
 * Iki gorev AYNI ANDA, zamanlayici kesmesi surekli araya girerken
 * kmalloc/kfree yapar. Kesme korumasi OLMASAYDI serbest liste
 * bozulur, heap magic dogrulamasi paniklerdi. */
#define HEAP_STRESS_ITERS 300
static volatile int heap_stress_i_done = 0;
static volatile int heap_stress_j_done = 0;
static volatile int heap_stress_errors = 0;

static void heap_stress_worker(volatile int *done_flag, uint8_t fill) {
    for (int i = 0; i < HEAP_STRESS_ITERS; i++) {
        uint32_t sz = 16 + (uint32_t)((i * 37) % 200);  /* degisken boyut */
        uint8_t *p = (uint8_t *)kmalloc(sz);
        if (!p) { heap_stress_errors++; continue; }
        for (uint32_t k = 0; k < sz; k++) p[k] = fill;
        /* veri butunlugu: baskasi bizim blogumuza yazmis mi? */
        for (uint32_t k = 0; k < sz; k++)
            if (p[k] != fill) { heap_stress_errors++; break; }
        kfree(p);
    }
    *done_flag = 1;
}

static void heap_stress_i_entry(void) { heap_stress_worker(&heap_stress_i_done, 0xA1); thread_exit(); }
static void heap_stress_j_entry(void) { heap_stress_worker(&heap_stress_j_done, 0xB2); thread_exit(); }

/* --- IPC kaniti 1: Mesaj Kuyrugu (Message Queue) - uretici/tuketici.
 * Tuketici, kuyruk bosken CAGIRANI BLOKE EDER (meşgul-bekleme degil);
 * bu, tuketicinin ürün gelene kadar "uyuyup" CPU'yu baskalarina
 * biraktigini kanitlar. */
static msgqueue_t demo_queue;
static volatile int mq_producer_done = 0;
static volatile int mq_consumer_done = 0;
static volatile uint32_t mq_last_received = 0;
static volatile int mq_order_ok = 1;

static void mq_producer_entry(void) {
    for (uint32_t i = 1; i <= 10; i++) {
        msgqueue_send(&demo_queue, &i, sizeof(i));
        thread_sleep(15); /* mesajlar arasi bekleme - tuketicinin
                            * gercekten BLOKE oldugunu (busy-spin
                            * yapmadigini) kanitlamak icin */
    }
    mq_producer_done = 1;
    thread_exit();
}

static void mq_consumer_entry(void) {
    for (int i = 0; i < 10; i++) {
        uint32_t val;
        uint32_t sz;
        msgqueue_receive(&demo_queue, &val, &sz); /* bos ise burada BLOKE olur */
        if (val != mq_last_received + 1) mq_order_ok = 0;
        mq_last_received = val;
        mutex_lock(&print_mutex);
        serial_write("[MQ-Consumer] alindi: ");
        serial_write_hex(val);
        serial_write("\n");
        mutex_unlock(&print_mutex);
    }
    mq_consumer_done = 1;
    thread_exit();
}

/* --- IPC kaniti 2: Pipe - bayt akisi, yazan/okuyan farkli hizlarda. */
static pipe_t demo_pipe;
static volatile int pipe_writer_done = 0;
static volatile int pipe_reader_done = 0;
static char pipe_result[64];
static volatile int pipe_result_ok = 0;

static void pipe_writer_entry(void) {
    static const char msg[] = "OTUKEN ISLETIM SISTEMI PIPE TESTI";
    for (uint32_t i = 0; i < sizeof(msg) - 1; i++) {
        pipe_write(&demo_pipe, &msg[i], 1); /* tek tek bayt - kasitli yavas */
        thread_sleep(2);
    }
    pipe_writer_done = 1;
    thread_exit();
}

static void pipe_reader_entry(void) {
    uint32_t total = 0;
    static const char expected[] = "OTUKEN ISLETIM SISTEMI PIPE TESTI";
    while (total < sizeof(expected) - 1) {
        uint32_t n = pipe_read(&demo_pipe, &pipe_result[total], 8); /* pipe bossa BLOKE olur */
        total += n;
    }
    pipe_result[total] = 0;

    pipe_result_ok = 1;
    for (uint32_t i = 0; i < total; i++) {
        if (pipe_result[i] != expected[i]) { pipe_result_ok = 0; break; }
    }

    mutex_lock(&print_mutex);
    serial_write("[Pipe-Reader] tamamlandi, okunan: ");
    serial_write(pipe_result);
    serial_write("\n");
    mutex_unlock(&print_mutex);

    pipe_reader_done = 1;
    thread_exit();
}


/* --- Stage 13: Driver Framework sarmalayicilari ---
 * Mevcut surucu kodlarina HIC DOKUNMADAN, onlari driver_manager'in
 * bekledigi ortak imzaya (int fn(void)) uyduruyoruz. */
static int drv_keyboard_init(void) { keyboard_install(); return 0; }
static int drv_mouse_init(void)    { mouse_install();    return 0; }
static int drv_timer_init(void)    { timer_init(50);     return 0; }
static int drv_disk1_init(void)    { return disk_init(); }
static int drv_disk2_init(void)    { return disk2_init(); }

/* probe ornegi: ikinci disk gercekten var mi? (disk2_init zaten
 * kontrol ediyor ama probe/init ayrimini GERCEKTEN gostermek icin
 * ayri bir sinama olarak da veriyoruz) */
static int drv_disk2_probe(void) {
    uint8_t tmp[512];
    return (disk2_read_sectors(0, 1, tmp) == 0) ? 1 : 0;
}


/* =====================================================================
 * SMP-YARIS SINAMASI (SMP adim 6) - yalniz TANI MODU
 * YARIS_GOREV gorev AYNI ANDA: ortak dosyayi okur ve karsilastirir, kendi
 * dosyasina gorev+tura OZGU desen yazip geri okur (FAT tablosu ve kok
 * dizin eszamanli degisir), 64 KB ayirip birakir (heap -> VMM), gunluge
 * yazar ve yetki sorar. Her gorev kostugu cekirdekleri isaretler.
 * Kontrol grubu: -DDEPO_KILIT_KAPALI ile ayni sinama hata BULMALIDIR.
 * ===================================================================== */
#define YARIS_GOREV 6
#define YARIS_TUR   12
#define YARIS_BOY   1536
static volatile int      yaris_sira = 0, yaris_biten = 0, yaris_hata = 0;
static volatile int      yaris_neden[8];     /* ilk hatanin kodu (gorev basina) */
static volatile uint64_t yaris_cekirdek = 0;
static uint8_t  yaris_ref[1024];
static int      yaris_ref_n = 0;

static int yaris_dosya_oku(const char *yol, uint8_t *buf, int azami) {
    int fd = vfs_open(yol);
    if (fd < 0) return -1;
    int top = 0;
    for (;;) {
        int n = vfs_read(fd, buf + top, (uint64_t)((azami - top) > 512 ? 512 : (azami - top)));
        if (n <= 0) break;
        top += n;
        if (top >= azami) break;
    }
    vfs_close(fd);
    return top;
}

static void yaris_isaretle(void) {
    __atomic_or_fetch(&yaris_cekirdek, 1ULL << (cpu_bu()->indeks & 63), __ATOMIC_RELAXED);
}

static void yaris_gorev(void) {
    int n = __atomic_fetch_add(&yaris_sira, 1, __ATOMIC_SEQ_CST);
    char ad[16] = "/YARIS0.TXT";
    ad[6] = (char)('0' + n);
    uint8_t *yaz = (uint8_t *)kmalloc(YARIS_BOY);
    uint8_t *oku = (uint8_t *)kmalloc(YARIS_BOY + 16);
    int hata = 0;
    if (!yaz || !oku) hata++;
    for (int tur = 0; tur < YARIS_TUR && !hata; tur++) {
        yaris_isaretle();
        /* 1) ortak dosya */
        int r = yaris_dosya_oku("/OTUKEN.TXT", oku, (int)sizeof(yaris_ref));
        if (r != yaris_ref_n) { hata++; yaris_neden[n & 7] = 10 + (r < 0 ? 0 : 1); }
        else for (int i = 0; i < r; i++) if (oku[i] != yaris_ref[i]) { hata++; yaris_neden[n & 7] = 12; break; }
        yaris_isaretle();
        /* 2) kendi dosyasi: gorev+tur desenli yaz, geri oku */
        for (int i = 0; i < YARIS_BOY; i++) yaz[i] = (uint8_t)(n * 37 + tur * 11 + i);
        if (fat16_write_file(ad + 1, yaz, YARIS_BOY) != YARIS_BOY) { hata++; yaris_neden[n & 7] = 20; }   /* basari: yazilan bayt */
        r = yaris_dosya_oku(ad, oku, YARIS_BOY + 16);
        if (!hata && r != YARIS_BOY) { hata++; yaris_neden[n & 7] = 0x2100 + (r & 0xFF); }
        else if (!hata) for (int i = 0; i < YARIS_BOY; i++) if (oku[i] != yaz[i]) { hata++; yaris_neden[n & 7] = 0x2200 + (i & 0xFF); break; }
        yaris_isaretle();
        /* 3) heap buyumesi (VMM), gunluk, yetki */
        uint8_t *b = (uint8_t *)kmalloc(65536);
        if (!b) hata++; else { b[0] = 1; b[65535] = 2; kfree(b); }
        log_write(LOG_AYIKLA, "YARIS", "tur tamam");
        (void)tore_get_capabilities(1);
        thread_sleep(1);
    }
    if (yaz) kfree(yaz);
    if (oku) kfree(oku);
    __atomic_add_fetch(&yaris_hata, hata, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&yaris_biten, 1, __ATOMIC_SEQ_CST);
    thread_exit();
}

static void smp_yaris_sinamasi(void) {
    yaris_ref_n = yaris_dosya_oku("/OTUKEN.TXT", yaris_ref, (int)sizeof(yaris_ref));
    if (yaris_ref_n <= 0) { tani_yaz("SMP-YARIS: [HATA] referans dosya okunamadi\n"); return; }
    for (int i = 0; i < YARIS_GOREV; i++) thread_create("YarisGorevi", yaris_gorev);
    uint64_t bas = timer_get_ticks();
    while (__atomic_load_n(&yaris_biten, __ATOMIC_ACQUIRE) < YARIS_GOREV &&
           timer_get_ticks() - bas < 50 * 40)          /* <= 40 sn */
        thread_sleep(5);
    int biten = __atomic_load_n(&yaris_biten, __ATOMIC_ACQUIRE);
    int hata = __atomic_load_n(&yaris_hata, __ATOMIC_ACQUIRE);
    uint64_t maske = __atomic_load_n(&yaris_cekirdek, __ATOMIC_ACQUIRE);
    int cek = 0; for (int i = 0; i < 64; i++) if (maske & (1ULL << i)) cek++;
    tani_yaz((biten == YARIS_GOREV && hata == 0) ? "SMP-YARIS: GECTI" : "SMP-YARIS: KALDI");
    tani_yaz(" gorev="); tani_hex((uint32_t)biten);
    tani_yaz(" hata="); tani_hex((uint32_t)hata);
    tani_yaz(" cekirdek_sayisi="); tani_hex((uint32_t)cek);
    tani_yaz(" maske="); tani_hex((uint32_t)maske);
    tani_yaz("\n");
    if (hata) {
        tani_yaz("SMP-YARIS: ilk hata kodlari");
        for (int i = 0; i < YARIS_GOREV; i++) { tani_yaz(" "); tani_hex((uint32_t)yaris_neden[i]); }
        tani_yaz("\n");
    }
    tani_yaz("SMP-DAGITIM: is gecisleri");
    for (int i = 0; i < smp_uyanan() && i < 8; i++) {
        cpu_yerel_t *c = cpu_indeksle((uint32_t)i);
        tani_yaz(" c"); tani_hex((uint32_t)i); tani_yaz("="); tani_hex((uint32_t)c->is_gecis);
    }
    tani_yaz("\n");
}

/* =====================================================================
 * AGIR SINAMA (stres + soak + sizinti) - yalniz TANI + OTUKEN_AGIR_SINAMA
 *
 * SMP-YARIS "dogruluk" sinamasidir (kilit calisiyor mu). Bu ise DAYANIKLILIK
 * sinamasidir: cok gorevli, uzun sureli, degisken boyutlu yuk altinda
 * cekirdek COKUYOR MU, SIZDIRIYOR MU, kaybediyor mu.
 *
 *   - heap: 16..8192 bayt degisken ayirma/birakma, desen dogrulama
 *   - VFS/FAT: ortak dosya oku + goreve ozel dosya yaz/oku (depo_kilit)
 *   - boru: yaz/oku gidis-donus (pipe_t)
 *   - mesaj kuyrugu: gonder/al gidis-donus
 *   - mutex: ORTAK sayaci kilit altinda artir -> KAYIP GUNCELLEME denetimi
 *
 * Her turdan ONCE ve SONRA bos PMM cercevesi + heap kullanimi olculur.
 * Gorevler bittikten ve reap edildikten sonra bunlar BASA DONMELI; tur
 * boyunca DUSUYORSA sizinti vardir. Cikti makine-okunur (AGIR-*). */
#define AGIR_GOREV     8
#define AGIR_IC_TUR    40      /* her gorev, her dalgada bu kadar dongu doner */
#define AGIR_MUTEX_ART 200     /* her gorev ortak sayaci bu kadar artirir */

static volatile int      agir_biten = 0;
static volatile int      agir_hata = 0;
static volatile uint64_t agir_sayac = 0;       /* mutex korumali ortak sayac */
static mutex_t           agir_mutex;
static volatile uint64_t agir_cekirdek = 0;
static uint8_t           agir_ref[1024];
static int               agir_ref_n = 0;
static volatile int      agir_neden[AGIR_GOREV];

static void agir_gorev(void) {
    /* Kararli bir kimlik: kendi gorevimizin pid'inden turet. */
    task_t *self = scheduler_current_task();
    uint32_t kimlik = self ? self->id : 0;
    int hata = 0, neden = 0;
    char ad[16] = "/AGIRX.TXT";
    ad[5] = (char)('A' + (kimlik % 20));

    uint8_t *oku = (uint8_t *)kmalloc(1040);
    pipe_t  *br  = (pipe_t *)kmalloc(sizeof(pipe_t));
    msgqueue_t *mq = (msgqueue_t *)kmalloc(sizeof(msgqueue_t));
    if (!oku || !br || !mq) { hata++; neden = 1; }
    if (!hata) { pipe_init(br); msgqueue_init(mq); }

    for (int t = 0; t < AGIR_IC_TUR && !hata; t++) {
        __atomic_or_fetch(&agir_cekirdek, 1ULL << (cpu_bu()->indeks & 63), __ATOMIC_RELAXED);

        /* 1) degisken boyutlu heap: 8 blok, farkli boyut, desen, dogrula, birak */
        void *blok[8]; int boy[8];
        for (int i = 0; i < 8; i++) {
            boy[i] = 16 + ((kimlik * 7 + t * 13 + i * 131) % 8177);
            blok[i] = kmalloc((uint32_t)boy[i]);
            if (blok[i]) { uint8_t *p = blok[i];
                for (int j = 0; j < boy[i]; j++) p[j] = (uint8_t)(i * 37 + j); }
        }
        for (int i = 0; i < 8; i++) if (blok[i]) {
            uint8_t *p = blok[i];
            for (int j = 0; j < boy[i]; j++) if (p[j] != (uint8_t)(i * 37 + j)) { hata++; neden = 2; break; }
#ifdef AGIR_KONTROL_SIZINTI
            /* KONTROL GRUBU: olcutun gercek sizintiyi yakaladigini kanitlar.
             * Her ic turda bir blok BILEREK birakilmaz. */
            if (i == 0) continue;
#endif
            kfree(blok[i]);
        }

        /* 2) ortak dosya oku + karsilastir (depo_kilit) */
        int fd = vfs_open("/OTUKEN.TXT");
        if (fd >= 0) {
            int r = vfs_read(fd, oku, 1024);
            vfs_close(fd);
            if (r != agir_ref_n) { hata++; neden = 3; }
            else for (int i = 0; i < r; i++) if (oku[i] != agir_ref[i]) { hata++; neden = 4; break; }
        } else { hata++; neden = 5; }

        /* 3) goreve ozel dosya yaz + geri oku (FAT tablosu eszamanli degisir) */
        uint8_t desen[300];
        for (int i = 0; i < 300; i++) desen[i] = (uint8_t)(kimlik + t + i);
        if (fat16_write_file(ad + 1, desen, 300) != 300) { hata++; neden = 6; }
        else {
            int fd2 = vfs_open(ad);
            if (fd2 < 0) { hata++; neden = 7; }
            else { int r = vfs_read(fd2, oku, 1024); vfs_close(fd2);
                if (r != 300) { hata++; neden = 0x800 | (r & 0xFF); }
                else for (int i = 0; i < 300; i++) if (oku[i] != desen[i]) { hata++; neden = 9; break; } }
        }

        /* 4) boru yaz/oku gidis-donus */
        if (!hata) {
            uint8_t bd[64]; for (int i = 0; i < 64; i++) bd[i] = (uint8_t)(t * 3 + i);
            pipe_write(br, bd, 64);
            uint8_t bo[64];
            uint32_t rr = pipe_read(br, bo, 64);
            if (rr != 64) { hata++; neden = 10; }
            else for (int i = 0; i < 64; i++) if (bo[i] != bd[i]) { hata++; neden = 11; break; }
        }

        /* 5) mesaj kuyrugu gonder/al */
        if (!hata) {
            uint8_t md[32]; for (int i = 0; i < 32; i++) md[i] = (uint8_t)(kimlik ^ (t + i));
            msgqueue_send(mq, md, 32);
            uint8_t mo[MSG_MAX_SIZE]; uint32_t mn = 0;
            msgqueue_receive(mq, mo, &mn);
            if (mn != 32) { hata++; neden = 12; }
            else for (int i = 0; i < 32; i++) if (mo[i] != md[i]) { hata++; neden = 13; break; }
        }

        /* 6) mutex: ORTAK sayaci artir (kayip guncelleme denetimi) */
        for (int k = 0; k < AGIR_MUTEX_ART / AGIR_IC_TUR + 1; k++) {
            mutex_lock(&agir_mutex);
            uint64_t v = agir_sayac;
            v = v + 1;
            agir_sayac = v;          /* okuma-degistirme-yazma: kilitsizse kaybolur */
            mutex_unlock(&agir_mutex);
        }

        if ((t & 3) == 0) thread_sleep(1);
    }
    if (oku) kfree(oku);
    if (br) kfree(br);
    if (mq) kfree(mq);
    if (hata) { agir_neden[kimlik % AGIR_GOREV] = neden; __atomic_add_fetch(&agir_hata, hata, __ATOMIC_SEQ_CST); }
    __atomic_add_fetch(&agir_biten, 1, __ATOMIC_SEQ_CST);
    thread_exit();
}

static void agir_sinama(void) {
    agir_ref_n = 0;
    { int fd = vfs_open("/OTUKEN.TXT");
      if (fd >= 0) { agir_ref_n = vfs_read(fd, agir_ref, (int)sizeof(agir_ref)); vfs_close(fd); } }
    if (agir_ref_n <= 0) { tani_yaz("AGIR-SINAMA: [HATA] referans dosya okunamadi\n"); return; }
    mutex_init(&agir_mutex);

    uint64_t ilk_pmm = pmm_free_frames();
    uint64_t ilk_heap = 0, bosb = 0, blk = 0;
    heap_get_stats(0, &ilk_heap, &bosb, &blk);

    int turlar = OTUKEN_AGIR_TUR;
    int sizinti = 0, toplam_hata = 0, kaldi_tur = 0;
    uint64_t onceki_pmm = ilk_pmm;
    /* SIZINTI OLCUTU: heap bellegi PMM'e GERI VERMEZ (tasarim): ilk turlarda
     * en yuksek kullanim duzeyine cikip orada KALIR. Arka planda UMAY ve
     * kabuk da ayirir. Bu yuzden toplam fark sizinti DEGILDIR; sizinti,
     * duzey oturduktan SONRA da buyumeye devam etmektir. Olcut: turlarin
     * YARISINDAN son tura kadar pmm dususu ve heap artisi. (Ilk surum
     * toplam farka bakiyor ve duzlesen bir egriye yanlislikla SIZINTI
     * diyordu.) */
    uint64_t yari_pmm = 0, yari_heap = 0;
    /* Ikinci duzeltme (matris kosusunda gorulen yanlis alarm): arka planda
     * UMAY penceresi (ULGEN yuzeyi, ~0xBA cerceve) zamanlamaya bagli olarak
     * herhangi bir turda BIR KEZ ayrilir. Tek basamak sizinti degildir;
     * AGIR gorevlerinden gelen gercek bir sizinti HER turda tekrarlar.
     * Olcut: tur>=2 turlarinin YARISINDAN fazlasinda buyume. */
    int buyuyen_tur = 0, sayilan_tur = 0;
    uint64_t onceki_heap = 0;

    for (int tur = 0; tur < turlar; tur++) {
        agir_biten = 0; agir_hata = 0;
        uint64_t beklenen_bas = agir_sayac;
        for (int i = 0; i < AGIR_GOREV; i++) thread_create("AgirGorev", agir_gorev);
        uint64_t bas = timer_get_ticks();
        while (__atomic_load_n(&agir_biten, __ATOMIC_ACQUIRE) < AGIR_GOREV &&
               timer_get_ticks() - bas < 50 * 60)         /* tur basina <= 60 sn */
            thread_sleep(3);
        scheduler_reap_terminated();
        thread_sleep(10);
        scheduler_reap_terminated();     /* iki tur reap: son gorevler de birakilsin */

        int biten = agir_biten, hata = agir_hata;
        uint64_t su_pmm = pmm_free_frames(), su_heap = 0;
        heap_get_stats(0, &su_heap, &bosb, &blk);
        /* mutex kayip guncelleme: bu turda tam AGIR_GOREV*(art) beklenir */
        uint64_t art = agir_sayac - beklenen_bas;
        uint64_t beklenen_art = (uint64_t)AGIR_GOREV * (AGIR_MUTEX_ART / AGIR_IC_TUR + 1) * AGIR_IC_TUR;

        int tur_iyi = (biten == AGIR_GOREV && hata == 0 && art == beklenen_art);
        toplam_hata += hata;
        if (!tur_iyi) kaldi_tur++;
        tani_yaz(tur_iyi ? "AGIR-TUR GECTI" : "AGIR-TUR KALDI");
        tani_yaz(" tur="); tani_hex((uint32_t)tur);
        tani_yaz(" biten="); tani_hex((uint32_t)biten);
        tani_yaz(" hata="); tani_hex((uint32_t)hata);
        tani_yaz(" mutex_art="); tani_hex((uint32_t)art);
        tani_yaz("/"); tani_hex((uint32_t)beklenen_art);
        tani_yaz(" pmm_bos="); tani_hex((uint32_t)su_pmm);
        tani_yaz(" heap_kul="); tani_hex((uint32_t)su_heap);
        tani_yaz(" d_pmm="); tani_hex((uint32_t)(onceki_pmm - su_pmm));
        tani_yaz("\n");
        if (hata) {
            tani_yaz("  ilk hata kodlari");
            for (int i = 0; i < AGIR_GOREV; i++) { tani_yaz(" "); tani_hex((uint32_t)agir_neden[i]); }
            tani_yaz("\n");
        }
        if (tur >= 2) {
            sayilan_tur++;
            if (su_pmm + 4 < onceki_pmm || su_heap > onceki_heap + 4096) buyuyen_tur++;
        }
        onceki_pmm = su_pmm;
        onceki_heap = su_heap;
        if (tur == turlar / 2) { yari_pmm = su_pmm; yari_heap = su_heap; }
    }

    uint64_t son_pmm = pmm_free_frames(), son_heap = 0;
    heap_get_stats(0, &son_heap, &bosb, &blk);
    uint64_t pmm_kayip = (ilk_pmm > son_pmm) ? (ilk_pmm - son_pmm) : 0;      /* bilgi */
    uint64_t heap_artis = (son_heap > ilk_heap) ? (son_heap - ilk_heap) : 0;  /* bilgi */
    uint64_t egim_pmm  = (yari_pmm > son_pmm) ? (yari_pmm - son_pmm) : 0;
    uint64_t egim_heap = (son_heap > yari_heap) ? (son_heap - yari_heap) : 0;
    sizinti = (sayilan_tur >= 2) && (buyuyen_tur * 2 > sayilan_tur);
    uint64_t maske = agir_cekirdek; int cek = 0;
    for (int i = 0; i < 64; i++) if (maske & (1ULL << i)) cek++;

    tani_yaz(sizinti ? "AGIR-SINAMA: SIZINTI VAR" : "AGIR-SINAMA: SIZINTI YOK");
    tani_yaz(" turlar="); tani_hex((uint32_t)turlar);
    tani_yaz(" pmm_kayip_cerceve="); tani_hex((uint32_t)pmm_kayip);
    tani_yaz(" heap_artis_bayt="); tani_hex((uint32_t)heap_artis);
    tani_yaz(" ikinci_yari_pmm_dusus="); tani_hex((uint32_t)egim_pmm);
    tani_yaz(" ikinci_yari_heap_artis="); tani_hex((uint32_t)egim_heap);
    tani_yaz(" buyuyen_tur="); tani_hex((uint32_t)buyuyen_tur);
    tani_yaz("/"); tani_hex((uint32_t)sayilan_tur);
    tani_yaz(" cekirdek_sayisi="); tani_hex((uint32_t)cek);
    tani_yaz(" kaldi_tur="); tani_hex((uint32_t)kaldi_tur);
    tani_yaz(" toplam_hata="); tani_hex((uint32_t)toplam_hata);
    tani_yaz("\n");
    tani_yaz(toplam_hata == 0 && kaldi_tur == 0 && !sizinti ? "AGIR-SINAMA: GECTI\n" : "AGIR-SINAMA: KALDI\n");
}

/* =====================================================================
 * UMAY UYUM SINAMASI - yalniz TANI + OTUKEN_UMAY_UYUM
 *
 * Cekirdek ile Ring3 UMAY arasindaki sozlesmeyi YUK ALTINDA dener:
 * UYUM_ISTEMCI gorev ayni anda, her biri kendi IPC portundan, UMAY'a
 * sirayla soru sorar ve her sorunun TAM yanitini bekler (UMAY uzun
 * yaniti 96 baytlik parcalara boler: opkod 1 devami var, 0 son).
 * Her istemcinin ayni anda en fazla BIR acik sorusu vardir; bu yuzden:
 *   - yanit baska istemciye giderse: soran zaman asimina duser, oteki
 *     fazladan ileti alir (sondaki bosaltmada 'fazla' sayilir);
 *   - parca kaybolursa ya da yarim kalirsa: 'eksik' sayilir.
 * Sorular yalniz OKUYAN turdendir (onay gerektiren yazma/onarim yok).
 * Cikti makine-okunur: UMAY-UYUM-ISTEMCI ... / UMAY-UYUM: GECTI|KALDI
 * ===================================================================== */
#define UYUM_ISTEMCI 3
#define UYUM_ZAMAN_ASIMI_TIK (50 * 30)     /* soru basina <= 30 sn */

static const char *const uyum_sorular[] = {
    "durum",
    "merhaba",
    "zeytin budama atığından ne yapılır",
    "kerpiç duvar nasıl sıvanır",
    "otağ ne durumda",
    "sarnıç suyu nasıl temiz kalır",
    "yer altı kileri kaç derece",
    "güneş panelleri nereye konur",
    "kiler ve sarnıç harcı araştır",
    "zeytin budama biyokömür kaynak tara",
};
#define UYUM_SORU_CESIDI ((int)(sizeof(uyum_sorular) / sizeof(uyum_sorular[0])))

static volatile int      uyum_biten = 0;
static volatile uint32_t uyum_tamam[UYUM_ISTEMCI], uyum_asim[UYUM_ISTEMCI],
                         uyum_eksik[UYUM_ISTEMCI], uyum_fazla[UYUM_ISTEMCI],
                         uyum_parca[UYUM_ISTEMCI], uyum_bos[UYUM_ISTEMCI];
static volatile uint64_t uyum_gecikme_top[UYUM_ISTEMCI], uyum_gecikme_en[UYUM_ISTEMCI];
static volatile uint64_t uyum_cekirdek = 0;
static volatile int      uyum_sira = 0;

static void uyum_istemci(void) {
    int no = __atomic_fetch_add(&uyum_sira, 1, __ATOMIC_ACQ_REL);
    if (no >= UYUM_ISTEMCI) { task_exit(); return; }
    char ad[8] = "uyum0";
    ad[4] = (char)('0' + no);
    int kendi = ipc_port_create(ad);
    int hedef = ipc_port_find("umay");
    if (kendi < 0 || hedef < 0) { uyum_asim[no] = OTUKEN_UYUM_SORU; goto bitti; }

    for (int q = 0; q < OTUKEN_UYUM_SORU; q++) {
        __atomic_or_fetch(&uyum_cekirdek, 1ULL << (cpu_bu()->indeks & 63), __ATOMIC_RELAXED);
        const char *soru = uyum_sorular[(no * 2 + q) % UYUM_SORU_CESIDI];
        ipc_message_t m;
        for (uint32_t i = 0; i < IPC_MAX_PAYLOAD; i++) m.payload[i] = 0;
        uint32_t n = 0;
        while (soru[n] && n < IPC_MAX_PAYLOAD) { m.payload[n] = (uint8_t)soru[n]; n++; }
        m.opcode = 0; m.length = n; m.sender_port = (uint32_t)kendi; m.sequence = (uint32_t)q;

        uint64_t bas = timer_get_ticks();
        int r;
        while ((r = ipc_send(hedef, &m)) == IPC_DOLU && timer_get_ticks() - bas < UYUM_ZAMAN_ASIMI_TIK)
            thread_sleep(1);
        if (r != 0) { uyum_asim[no]++; continue; }

        /* tam yaniti topla */
        int son = 0; uint32_t boy = 0, parca = 0;
        ipc_message_t y;
        static char metin[UYUM_ISTEMCI][2400];   /* arastirma raporu uzun */
        uint32_t mb = 0;
        while (!son && timer_get_ticks() - bas < UYUM_ZAMAN_ASIMI_TIK) {
            if (ipc_try_receive(kendi, &y) == 1) {
                parca++; boy += y.length;
                for (uint32_t i = 0; i < y.length && mb + 1 < sizeof metin[0]; i++) {
                    char c = (char)y.payload[i];
                    metin[no][mb++] = (c == '\n') ? '|' : c;   /* tek satir kalsin */
                }
                if (y.opcode == 0) son = 1;
                continue;
            }
            thread_sleep(1);
        }
        metin[no][mb] = 0;
        /* kanit: ilk istemci her soru cesidinin yanitini bir kez yazar */
        if (no == 0 && son && q < UYUM_SORU_CESIDI) {
            tani_yaz("UMAY-YANIT soru=\""); tani_yaz(soru);
            tani_yaz("\" yanit=\""); tani_yaz(metin[no]); tani_yaz("\"\n");
        }
        uyum_parca[no] += parca;
        if (!son) { if (parca) uyum_eksik[no]++; else uyum_asim[no]++; continue; }
        if (boy == 0) uyum_bos[no]++;
        uint64_t g = timer_get_ticks() - bas;
        uyum_gecikme_top[no] += g;
        if (g > uyum_gecikme_en[no]) uyum_gecikme_en[no] = g;
        uyum_tamam[no]++;
    }
    /* Sondaki bosaltma: buraya ait olmayan (baskasinin) yanit var mi?
     * Diger istemciler hala soruyor olabilir; 3 sn bekle. */
    {
        ipc_message_t y;
        uint64_t bas = timer_get_ticks();
        while (timer_get_ticks() - bas < 150) {
            if (ipc_try_receive(kendi, &y) == 1) uyum_fazla[no]++;
            else thread_sleep(2);
        }
    }
bitti:
    __atomic_add_fetch(&uyum_biten, 1, __ATOMIC_ACQ_REL);
    task_exit();
}

static void umay_uyum_sinamasi(void) {
    /* Ring3 UMAY'in 'umay' portunu acmasini bekle (<= 60 sn). */
    uint64_t bas = timer_get_ticks();
    while (ipc_port_find("umay") < 0 && timer_get_ticks() - bas < 50 * 60) thread_sleep(5);
    if (ipc_port_find("umay") < 0) {
        tani_yaz("UMAY-UYUM: KALDI (UMAY 'umay' portunu acmadi)\n");
        task_exit(); return;
    }
    thread_sleep(50);    /* acilis selamini bitirsin */
    uint64_t t0 = timer_get_ticks();
    for (int i = 0; i < UYUM_ISTEMCI; i++) thread_create("UyumIstemci", uyum_istemci);
    while (__atomic_load_n(&uyum_biten, __ATOMIC_ACQUIRE) < UYUM_ISTEMCI &&
           timer_get_ticks() - t0 < (uint64_t)UYUM_ZAMAN_ASIMI_TIK * (OTUKEN_UYUM_SORU + 2))
        thread_sleep(10);

    uint32_t tamam = 0, hata = 0;
    for (int i = 0; i < UYUM_ISTEMCI; i++) {
        tani_yaz("UMAY-UYUM-ISTEMCI no="); tani_hex((uint32_t)i);
        tani_yaz(" tamam="); tani_hex(uyum_tamam[i]);
        tani_yaz("/"); tani_hex(OTUKEN_UYUM_SORU);
        tani_yaz(" zaman_asimi="); tani_hex(uyum_asim[i]);
        tani_yaz(" eksik="); tani_hex(uyum_eksik[i]);
        tani_yaz(" fazla="); tani_hex(uyum_fazla[i]);
        tani_yaz(" bos="); tani_hex(uyum_bos[i]);
        tani_yaz(" parca="); tani_hex(uyum_parca[i]);
        tani_yaz(" ort_gecikme_tik=");
        tani_hex(uyum_tamam[i] ? (uint32_t)(uyum_gecikme_top[i] / uyum_tamam[i]) : 0);
        tani_yaz(" en_gecikme_tik="); tani_hex((uint32_t)uyum_gecikme_en[i]);
        tani_yaz("\n");
        tamam += uyum_tamam[i];
        hata += uyum_asim[i] + uyum_eksik[i] + uyum_fazla[i] + uyum_bos[i];
    }
    int biten = __atomic_load_n(&uyum_biten, __ATOMIC_ACQUIRE);
    int gecti = (biten == UYUM_ISTEMCI) && (hata == 0) &&
                (tamam == (uint32_t)(UYUM_ISTEMCI * OTUKEN_UYUM_SORU));
    tani_yaz("UMAY-UYUM: "); tani_yaz(gecti ? "GECTI" : "KALDI");
    tani_yaz(" istemci="); tani_hex(UYUM_ISTEMCI);
    tani_yaz(" soru="); tani_hex((uint32_t)(UYUM_ISTEMCI * OTUKEN_UYUM_SORU));
    tani_yaz(" tam_yanit="); tani_hex(tamam);
    tani_yaz(" hata="); tani_hex(hata);
    tani_yaz(" cekirdek_maske="); tani_hex((uint32_t)uyum_cekirdek);
    tani_yaz("\n");
    task_exit();
}

/* boot.asm cagirir (tek giris noktasi; baska dosyadan cagrilmaz) */
void kernel_main(uint32_t magic, multiboot_info_t *mbi);
void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
    /* SMP adim 4: GS_BASE -> BSP blogu. cpu_bu()/cpu_yerel() ve
     * zamanlayici (cekirdek_rsp) bundan sonra gecerlidir; EN BASTA. */
    cpu_bsp_hazirla();
    serial_init();
    serial_write("\n===============================================\n");
    serial_write("  " OTUKEN_SURUM_ADI "  (x86_64 / AArch64)\n");
    serial_write("===============================================\n");
    serial_write("otukenOS-mini: kernel_main basladi\n");

    /* --- Onyukleme protokolu: multiboot1 (BIOS) veya multiboot2 (UEFI) ---
     * UEFI makinelerde GRUB YALNIZCA multiboot2 kullanir. Bilgi yapisi
     * tamamen farkli oldugu icin once multiboot1 bicimine ceviriyoruz;
     * cekirdegin geri kalani degismeden calisiyor. */
    static multiboot_info_t mb2_cevrilen;
    int protokol = onyukleme_protokolu(magic);

    if (protokol == 2) {
        serial_write("onyukleme: MULTIBOOT2 (UEFI uyumlu)\n");
        if (multiboot2_cevir((uint64_t)(uintptr_t)mbi, &mb2_cevrilen) != 0) {
            serial_write("HATA: multiboot2 bilgisi cevrilemedi\n");
            for (;;) { __asm__ volatile ("cli; hlt"); }
        }
        mbi = &mb2_cevrilen;
    } else if (protokol == 1) {
        serial_write("onyukleme: MULTIBOOT1 (eski BIOS)\n");
        modul_mb1_oku(mbi);            /* 1.4.9: ERGENEKON imaji (GRUB modulu) */
    } else {
        serial_write("HATA: bilinmeyen onyukleme protokolu, magic=");
        serial_write_hex(magic);
        serial_write("\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    serial_write("onyukleme bilgisi dogrulandi\n");

    /* --- CR0.WP (Write Protect) ACIKCA ayarlanir ---
     * Bu bit TEMIZ iken cekirdek, SALT-OKUNUR isaretli sayfalara bile
     * yazabilir; yani W^X korumasi cekirdek icin GECERSIZ olur.
     * GRUB-BIOS onu temiz birakiyor, GRUB-UEFI set ediyordu - yani
     * davranis FIRMWARE'E gore degisiyordu ve hatalar yalnizca gercek
     * donanimda ortaya cikiyordu. Artik her iki yolda da SET. */
    {
        uint64_t cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1ULL << 16);            /* WP */
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
        serial_write("guvenlik: CR0.WP etkin (cekirdek salt-okunur sayfalara yazamaz)\n");
    }
    /* 1.4.9: SMEP + SMAP (islemci destekliyorsa). AP'ler CR4'u BSP'den alir. */
    {
        if (fpu_ac() == 0) serial_write("guvenlik: x87/SSE etkin (gorev basina FXSAVE)\n");
        else serial_write("[UYARI] islemci FXSR/SSE2 desteklemiyor: Ring3 SSE kullanamaz\n");
        int s = smep_smap_ac();
        serial_write((s & 1) ? "guvenlik: SMEP etkin" : "guvenlik: SMEP yok (islemci desteklemiyor)");
        serial_write((s & 2) ? ", SMAP etkin\n" : ", SMAP yok (islemci desteklemiyor)\n");
    }

    if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        serial_write("HATA: bootloader framebuffer bilgisi vermedi\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("framebuffer adres: ");
    serial_write_hex((uint32_t)mbi->framebuffer_addr);
    serial_write(" genislik: ");
    serial_write_hex(mbi->framebuffer_width);
    serial_write(" yukseklik: ");
    serial_write_hex(mbi->framebuffer_height);
    serial_write(" bpp: ");
    serial_write_hex(mbi->framebuffer_bpp);
    serial_write(" tur: ");
    serial_write_hex(mbi->framebuffer_type);
    serial_write(" R/G/B konum: ");
    serial_write_hex(mbi->color_info[0]); serial_write("/");
    serial_write_hex(mbi->color_info[2]); serial_write("/");
    serial_write_hex(mbi->color_info[4]);
    serial_write("\n");

    serial_write("64-bit long mode'dayiz (boot.asm GDT'yi zaten kurdu)\n");

    log_init();

    idt_install();
    serial_write("IDT + PIC kuruldu\n");

    fb_init(mbi);
    serial_write("framebuffer baslatildi\n");

    pmm_init(mbi);

    {
        uint64_t free_before = pmm_free_frames();
        uint64_t f1 = pmm_alloc_frame();
        uint64_t f2 = pmm_alloc_frame();
        uint64_t f3 = pmm_alloc_frame();
        serial_write("pmm-test: ayrilan cerceveler: ");
        serial_write_hex((uint32_t)f1); serial_write(" ");
        serial_write_hex((uint32_t)f2); serial_write(" ");
        serial_write_hex((uint32_t)f3); serial_write("\n");

        uint64_t free_after_alloc = pmm_free_frames();
        serial_write("pmm-test: bos cerceve (alloc oncesi/sonrasi): ");
        serial_write_hex((uint32_t)free_before); serial_write(" / ");
        serial_write_hex((uint32_t)free_after_alloc); serial_write("\n");

        pmm_free_frame(f1);
        pmm_free_frame(f2);
        pmm_free_frame(f3);
        uint64_t free_after_free = pmm_free_frames();
        serial_write("pmm-test: bos cerceve (free sonrasi, basa esit olmali): ");
        serial_write_hex((uint32_t)free_after_free); serial_write("\n");
        if (free_after_free == free_before) {
            serial_write("pmm-test: BASARILI (free_after_free == free_before)\n");
        } else {
            serial_write("pmm-test: HATA - sayaç tutmuyor!\n");
        }
    }

    vmm_init();
    {
        /* Mevcut 0-4GB kimlik haritasinin tamamen disinda, daha once
         * hic dokunulmamis bir sanal adres seciyoruz - boylece
         * gercekten YENI bir haritalama yapildigini kanitlariz. */
        uint64_t test_virt = 0x500000000ULL; /* 20GB civari - kesinlikle bos */
        uint64_t test_phys = pmm_alloc_frame();

        if (test_phys == 0) {
            serial_write("vmm-test: PMM'den cerceve alinamadi, atlaniyor\n");
        } else if (vmm_map_page(test_virt, test_phys,
                                 VMM_PRESENT | VMM_WRITABLE) != 0) {
            serial_write("vmm-test: haritalama basarisiz\n");
        } else {
            volatile uint32_t *p = (volatile uint32_t *)test_virt;
            *p = 0xDEADBEEF;
            uint32_t readback = *p;

            serial_write("vmm-test: sanal="); serial_write_hex((uint32_t)(test_virt >> 32));
            serial_write_hex((uint32_t)test_virt);
            serial_write(" fiziksel="); serial_write_hex((uint32_t)test_phys);
            serial_write(" yazilan=0xDEADBEEF okunan=");
            serial_write_hex(readback);
            serial_write("\n");

            uint64_t resolved = vmm_get_physical(test_virt);
            serial_write("vmm-test: vmm_get_physical dogrulamasi=");
            serial_write_hex((uint32_t)resolved);
            serial_write((resolved == test_phys) ? " (DOGRU)\n" : " (YANLIS!)\n");

            if (readback == 0xDEADBEEF && resolved == test_phys) {
                serial_write("vmm-test: BASARILI\n");
            } else {
                serial_write("vmm-test: HATA\n");
            }

            vmm_unmap_page(test_virt);
            pmm_free_frame(test_phys);
            serial_write("vmm-test: unmap edildi, cerceve PMM'e iade edildi\n");
        }

        /* --- 2. test: mevcut 2MB kimlik haritasi icindeki bir adresi
         * FARKLI bir fiziksel cerceveye yeniden haritalamak - bu,
         * split_2mb_if_needed yolunu (riskli kisim) calistirir. */
        uint64_t scratch_virt = 0x700000ULL; /* 7MB - kernelin disinda, bos */
        uint64_t new_phys = pmm_alloc_frame();
        if (new_phys == 0) {
            serial_write("vmm-split-test: PMM'den cerceve alinamadi, atlaniyor\n");
        } else if (new_phys == scratch_virt) {
            /* cok dusuk ihtimal ama ayni cikarsa test anlamsizlasir */
            serial_write("vmm-split-test: sansa - ayni cerceve cikti, atlaniyor\n");
            pmm_free_frame(new_phys);
        } else {
            volatile uint32_t *before = (volatile uint32_t *)scratch_virt;
            uint32_t original_identity_value = *before; /* bolmeden once oku */

            if (vmm_map_page(scratch_virt, new_phys,
                              VMM_PRESENT | VMM_WRITABLE) != 0) {
                serial_write("vmm-split-test: haritalama basarisiz\n");
            } else {
                volatile uint32_t *v = (volatile uint32_t *)scratch_virt;
                *v = 0xCAFEF00D;

                volatile uint32_t *direct_new_phys = (volatile uint32_t *)new_phys;
                uint32_t via_new_phys = *direct_new_phys;

                serial_write("vmm-split-test: scratch_virt=0x00700000 (eskiden 2MB kimlik "
                             "haritaliydi) simdi fiziksel="); serial_write_hex((uint32_t)new_phys);
                serial_write(" degerine yonlendirildi\n");
                serial_write("vmm-split-test: virt uzerinden yazilan=0xCAFEF00D; "
                             "yeni fiziksel adresten DOGRUDAN okunan=");
                serial_write_hex(via_new_phys);
                serial_write((via_new_phys == 0xCAFEF00D) ? " (DOGRU - bolme calisti)\n"
                                                            : " (YANLIS!)\n");

                /* KIMLIGE GERI DON: eskiden unmap ediliyordu ve kimlik
                 * haritasinda 0x700000'da KALICI DELIK kaliyordu. PMM
                 * sonradan fiziksel 0x700000'i dagitinca cekirdek ona
                 * kimlik adresiyle yazip PAGE FAULT veriyordu (UMAY
                 * paketiyle ikili buyuyunce ortaya cikti; zamanlamaya
                 * bagli oldugu icin rastgeleydi). */
                vmm_map_page(scratch_virt, scratch_virt, VMM_PRESENT | VMM_WRITABLE);
                pmm_free_frame(new_phys);
                if (*before != original_identity_value)
                    serial_write("vmm-split-test: [HATA] kimlik haritasi geri donmedi\n");
            }
        }
    }

    heap_init();
    /* 1.4.8: cift tampon. Bundan sonra cizim bellege yapilir, fb_sun()
     * ekrana aktarir (titreme ve yarim kare yok). */
    if (fb_arka_tampon_kur() == 0)
        serial_write("framebuffer: cift tampon kuruldu\n");
    else
        serial_write("framebuffer: cift tampon KURULAMADI - dogrudan cizim\n");
    {
        /* 1) Farkli boyutlarda ayirma + veri butunlugu */
        char *a = (char *)kmalloc(64);
        char *b = (char *)kmalloc(128);
        char *c = (char *)kmalloc(256);
        if (a && b && c) {
            for (int i = 0; i < 64;  i++) a[i] = (char)('A' + (i % 26));
            for (int i = 0; i < 128; i++) b[i] = (char)('a' + (i % 26));
            for (int i = 0; i < 256; i++) c[i] = (char)(i & 0xFF);

            int ok = 1;
            for (int i = 0; i < 64;  i++) if (a[i] != (char)('A' + (i % 26))) ok = 0;
            for (int i = 0; i < 128; i++) if (b[i] != (char)('a' + (i % 26))) ok = 0;
            for (int i = 0; i < 256; i++) if (c[i] != (char)(i & 0xFF)) ok = 0;

            serial_write(ok ? "heap-test: veri butunlugu BASARILI\n"
                             : "heap-test: veri butunlugu HATA\n");
        } else {
            serial_write("heap-test: kmalloc basarisiz (a/b/c)\n");
        }

        uint64_t tb, ub, fb_, cnt;
        heap_get_stats(&tb, &ub, &fb_, &cnt);
        serial_write("heap-test: [orta] kullanilan="); serial_write_hex((uint32_t)ub);
        serial_write(" blok_sayisi="); serial_write_hex((uint32_t)cnt);
        serial_write("\n");

        /* 2) Ortadaki bloğu (b) free edip coalesce'i test et:
         * a(kullanimda) - b(FREE EDILECEK) - c(kullanimda)
         * b'yi free ettikten sonra hemen yeniden aynı boyutta bir
         * blok istersek, coalesce+split dogru calisiyorsa ayni
         * bolgeyi geri almaliyiz (parcalanma yonetimi kanit). */
        void *b_old_addr = (void *)b;
        kfree(b);
        void *d = kmalloc(100); /* 128'den biraz kucuk, b'nin yerine oturmali */
        serial_write("heap-test: b free edildi, yeniden 100 bayt istendi -> ");
        serial_write((d == b_old_addr) ? "AYNI ADRES (parcalanma yonetimi calisiyor)\n"
                                        : "farkli adres (yine de gecerli olabilir)\n");

        /* 3) krealloc: c'yi buyut, eski veri korunmus mu kontrol et */
        char *c_grown = (char *)krealloc(c, 512);
        int grow_ok = 1;
        if (c_grown) {
            for (int i = 0; i < 256; i++)
                if (c_grown[i] != (char)(i & 0xFF)) grow_ok = 0;
        } else {
            grow_ok = 0;
        }
        serial_write(grow_ok ? "heap-test: krealloc (256->512) veri korundu BASARILI\n"
                              : "heap-test: krealloc HATA\n");

        /* 4) Hizali ayirma */
        void *aligned = kmalloc_aligned(200, 64);
        int align_ok = aligned && (((uint64_t)(uintptr_t)aligned) % 64 == 0);
        serial_write(align_ok ? "heap-test: kmalloc_aligned(64) hizalama BASARILI\n"
                               : "heap-test: kmalloc_aligned HATA\n");

        /* 5) Cift free tespiti (guvenli - sadece uyari basar, cokme yok) */
        kfree(a);
        if (OTUKEN_TANI_MODU) {
            kfree(a); /* kasitli cift free - sadece tani modunda sinanir */
        }

        kfree(d);
        kfree(c_grown);
        kfree_aligned(aligned);

        heap_get_stats(&tb, &ub, &fb_, &cnt);
        serial_write("heap-test: [tumu free sonrasi] kullanilan=");
        serial_write_hex((uint32_t)ub);
        serial_write(" (0 olmali) blok_sayisi="); serial_write_hex((uint32_t)cnt);
        serial_write(" (dusuk olmali - coalesce calisiyorsa)\n");
    }

    /* --- Stage 13: artik surucular DOGRUDAN degil, Driver Manager
     * uzerinden kaydedilip yukleniyor (probe -> init -> durum takibi). */
    driver_manager_init();
    driver_register("ps2-klavye", DRIVER_CLASS_INPUT,   drv_keyboard_init, 0);
    driver_register("ps2-fare",   DRIVER_CLASS_INPUT,   drv_mouse_init,    0);
    driver_register("pit-timer",  DRIVER_CLASS_TIMER,   drv_timer_init,    0);
    driver_register("ata-disk1",  DRIVER_CLASS_STORAGE, drv_disk1_init,    0);
    driver_register("ata-disk2",  DRIVER_CLASS_STORAGE, drv_disk2_init,    drv_disk2_probe);

    int active_drivers = driver_load_all();
    serial_write("driver: toplam AKTIF surucu sayisi=");
    serial_write_hex((uint32_t)active_drivers);
    serial_write(" / kayitli=");
    serial_write_hex((uint32_t)driver_count());
    serial_write("\n");

    for (int i = 0; i < driver_count(); i++) {
        const driver_t *d = driver_get(i);
        serial_write("driver:   - ");
        serial_write(d->name);
        serial_write(" [");
        serial_write(driver_class_name(d->klass));
        serial_write("] ");
        serial_write(driver_state_name(d->state));
        serial_write("\n");
    }

    /* --- Stage 14: PCI veri yolu taramasi --- */
    {
        int pci_n = pci_scan();
        serial_write("pci: tarama tamam, bulunan cihaz sayisi=");
        serial_write_hex((uint32_t)pci_n);
        serial_write("\n");
        for (int i = 0; i < pci_n; i++) {
            const pci_device_t *d = pci_get_device(i);
            serial_write("pci:   ");
            serial_write_hex(d->bus); serial_write(":");
            serial_write_hex(d->slot); serial_write(".");
            serial_write_hex(d->func);
            serial_write(" vendor="); serial_write_hex(d->vendor_id);
            serial_write(" device="); serial_write_hex(d->device_id);
            serial_write(" [");
            serial_write(pci_class_name(d->class_code, d->subclass));
            serial_write("]\n");
        }

        /* Depolama denetleyicisini SINIFA gore bul (Stage 15 icin
         * gercek bir surucu-eslestirme ornegi) */
        const pci_device_t *ide = pci_find_by_class(0x01, 0x01);
        if (ide) {
            serial_write("pci: IDE denetleyici bulundu -> ata-disk surucumuz bunu kullaniyor\n");
        }
        const pci_device_t *ahci = pci_find_by_class(0x01, 0x06);
        serial_write(ahci ? "pci: AHCI denetleyici VAR (Stage 15 icin hazir)\n"
                           : "pci: AHCI denetleyici yok (bu makinede IDE kullaniliyor)\n");
        const pci_device_t *nvme = pci_find_by_class(0x01, 0x08);
        serial_write(nvme ? "pci: NVMe denetleyici VAR (Stage 15 icin hazir)\n"
                           : "pci: NVMe denetleyici yok\n");
        /* 1.4.9: AC'97 ses. ses_init HIC cagrilmiyordu (docs/KAYNAK-ANALIZI.md
         * [1]): DEDE/OZAN/ses tonu her cagrida -1 donuyordu. Kart yoksa
         * sessizce devre disi (ses_var()=0). */
        if (ses_init() != 0) serial_write("ses: AC97 yok, ses yolu devre disi\n");
    }

    /* --- Stage 15: Storage (AHCI / NVMe hazirligi) --- */
    ahci_init();
    nvme_init();
    usb_init();     /* Asama 28: USB denetleyicileri */
    uhci_init();    /* USB aktarim katmani + klavye numaralandirma */

    /* --- GPT BOLUM TABLOSU TARAMASI ---
     * 2 TB ustu diskler MBR ile bolumlenemez; buyuk SSD'ler GPT
     * kullanir. Diskin tamamini tek birim saymak yerine bolum
     * tablosunu okuyup dosya sistemlerinin dogru ofsette
     * aranmasini sagliyoruz. */
    {
        int bn = gpt_tara(1);          /* ikinci surucu */
        if (bn > 0) {
            tani_yaz("GPT-TEST: bolum sayisi=");
            tani_hex((uint32_t)bn);
            /* 2 TB OTESINDE baslayan bir bolum var mi? Varsa 64-bit
             * bolum adreslemesi gercekten calisiyor demektir. */
            int uzak = 0;
            for (int i = 0; i < bn; i++) {
                const gpt_bolum_t *b = gpt_bolum_al(i);
                if (b && b->ilk_lba > 0xFFFFFFFFULL) uzak = 1;
            }
            tani_yaz(uzak ? " (2TB otesinde bolum VAR) -> BASARILI\n"
                           : " -> BASARILI\n");
        } else if (bn == 0) {
            tani_yaz("GPT-TEST: bolum tablosu yok (bolumsuz disk)\n");
        }
    }

    /* --- BUYUK DISK (LBA48) SINAMASI ---
     * 2/4/8 TB SSD destegini kanitlar. LBA 12884901888 (6 TB konumu)
     * hem LBA28 (2^28) hem de uint32 (2^32) tavanini ASAR; oradan
     * dogru veri okuyabilmek LBA48 ve 64-bit adreslemenin gercekten
     * calistigi anlamina gelir. */
    {
        uint64_t sek_sayisi = disk_sektor_sayisi(1);   /* ikinci surucu */
        if (sek_sayisi > 0) {
            serial_write("BUYUKDISK: surucu kapasitesi=");
            serial_write_hex((uint32_t)(sek_sayisi >> 32));
            serial_write_hex((uint32_t)sek_sayisi);
            serial_write(" sektor (~");
            serial_write_hex((uint32_t)((sek_sayisi * 512) >> 40));
            serial_write(" TB) LBA48=");
            serial_write(disk_lba48_var(1) ? "VAR\n" : "YOK\n");

            const uint64_t IMZA_LBA = 12884901888ULL;   /* 6 TB konumu */
            if (sek_sayisi > IMZA_LBA) {
                static uint8_t sek[512];
                if (disk2_read_sectors(IMZA_LBA, 1, sek) == 0) {
                    static const char bek[] = "OTUKEN-6TB-IMZA-";
                    int ayni = 1;
                    for (int i = 0; i < 16; i++)
                        if (sek[i] != (uint8_t)bek[i]) ayni = 0;
                    serial_write("BUYUKDISK: 6TB konumundan okuma -> ");
                    serial_write(ayni ? "IMZA DOGRU (LBA48 CALISIYOR)\n"
                                       : "imza YANLIS\n");
                } else {
                    serial_write("BUYUKDISK: 6TB okuma BASARISIZ\n");
                }
            }
        }
    }

    /* --- AHCI GERCEK VERI OKUMA (DMA) sinamasi ---
     * Denetleyici tespiti yeterli degil; diskten GERCEKTEN veri
     * okuyabildigimizi kanitliyoruz. Ilk sektorun sonundaki 0x55AA
     * onyukleme imzasi, dogru veriyi okudugumuzun kanitidir. */
    {
        const ahci_controller_t *ac = ahci_get_controller();
        if (ac && ac->active_port_count > 0) {
            int p = -1;
            for (int i = 0; i < AHCI_MAX_PORTS; i++)
                if (ac->active_ports[i]) { p = i; break; }
            if (p >= 0) {
                static uint8_t sek[512];
                if (ahci_oku(p, 0, 1, sek) == 0) {
                    serial_write("AHCI-OKUMA: port ");
                    serial_write_hex((uint32_t)p);
                    serial_write(" sektor 0 okundu, imza=");
                    serial_write_hex(((uint32_t)sek[510] << 8) | sek[511]);
                    serial_write((sek[510] == 0x55 && sek[511] == 0xAA)
                                  ? " -> BASARILI (gecerli onyukleme imzasi)\n"
                                  : " -> imza farkli\n");
                } else {
                    serial_write("AHCI-OKUMA: okuma BASARISIZ\n");
                }
            }
        } else {
            tani_yaz("AHCI-OKUMA: bagli AHCI diski yok (atlandi)\n");
        }
    }

    /* --- Stage 17: TIMUR (guvenlik) + TORE (politikalar) --- */
    timur_enable_nx();
    tore_init();
    {
        /* W^X politikasini GERCEK bir sayfa uzerinde uygula ve dogrula */
        uint64_t test_phys = pmm_alloc_frame();
        /* Kimlik haritasinin (0-512GB, 1GB sayfa yolu) DISINDA olmali:
         * eskiden 0xC00000000 (48 GB) idi; sinama sonunda unmap edilince
         * kimlik haritasinda delik kaliyordu. 256 GB'lik hedef makinede
         * PMM 48 GB'daki cerceveyi dagittiginda cekirdek PAGE FAULT
         * verirdi (sandbox en cok 3 GB ile sinandigi icin gorulmedi). */
        uint64_t data_virt = 0x00007F0000100000ULL;
        uint64_t code_virt = 0x00007F0000101000ULL;
        uint64_t code_phys = pmm_alloc_frame();

        if (test_phys && code_phys &&
            vmm_map_page(data_virt, test_phys, VMM_PRESENT | VMM_WRITABLE) == 0 &&
            vmm_map_page(code_virt, code_phys, VMM_PRESENT | VMM_WRITABLE) == 0) {

            timur_protect_data(data_virt);
            timur_protect_code(code_virt);

            timur_report_protection("veri sayfasi (W^X: W tarafi)", data_virt);
            timur_report_protection("kod sayfasi (W^X: X tarafi)", code_virt);
            /* Stage 19: test sayfalari kullanimdan sonra iade edilmeli
             * (kod taramasinda bulunan gercek sizinti) - asagida
             * yazma testinden SONRA serbest birakiliyor. */

            /* Veri sayfasina yazabilmeliyiz (izinli islem) */
            *(volatile uint32_t *)data_virt = 0x5AFE5AFE;
            serial_write("TIMUR: veri sayfasina yazma BASARILI (beklenen)\n");
            serial_write("TIMUR: okunan deger=");
            serial_write_hex(*(volatile uint32_t *)data_virt);
            serial_write("\n");

            /* test bitti - haritalari kaldir ve cerceveleri PMM'e iade et */
            vmm_unmap_page(data_virt);
            vmm_unmap_page(code_virt);
            pmm_free_frame(test_phys);
            pmm_free_frame(code_phys);

            /* NOT: NX'in donanim tarafindan GERCEKTEN zorlandigi,
             * bu sayfada kod calistirma denenerek dogrulandi - CPU
             * hata kodu 0x11 (present + instruction-fetch) ile sayfa
             * hatasi uretti. Kasitli cokme oldugu icin test kodu
             * kaldirildi, kanit gelistirme kayitlarinda. */
        }

        /* --- Asama 30: KUT kimlik katmani ---
         * Yetkiler artik dogrudan degil, KULLANICI ROLUNDEN turetiliyor. */
        kut_init();
        uint32_t uid_yonetici = kut_add_user("otuken", KUT_ROL_YONETICI);
        uint32_t uid_hizmet   = kut_add_user("ulakd",  KUT_ROL_HIZMET);
        uint32_t uid_konuk    = kut_add_user("konuk",  KUT_ROL_KONUK);

        kut_bind_process(10, uid_yonetici);
        kut_bind_process(11, uid_hizmet);
        kut_bind_process(12, uid_konuk);

        tani_yaz("KUT-TEST: yonetici(pid=10) donanim erisimi -> ");
        serial_write(tore_check(10, KUT_HW_DIRECT, "donanim") ? "IZINLI\n" : "RED\n");
        tani_yaz("KUT-TEST: hizmet(pid=11) ag gonderimi -> ");
        serial_write(tore_check(11, KUT_NET_SEND, "ag") ? "IZINLI\n" : "RED\n");
        tani_yaz("KUT-TEST: konuk(pid=12) ag gonderimi -> ");
        serial_write(tore_check(12, KUT_NET_SEND, "ag") ? "IZINLI\n" : "RED (beklenen)\n");
        tani_yaz("KUT-TEST: konuk(pid=12) disk YAZMA -> ");
        serial_write(tore_check(12, KUT_DISK_WRITE, "disk") ? "IZINLI\n" : "RED (beklenen)\n");

        const kut_user_t *sahip = kut_owner_of(11);
        tani_yaz("KUT-TEST: pid=11 sahibi -> ");
        serial_write(sahip ? sahip->name : "(yok)");
        serial_write(" rol=");
        serial_write(sahip ? kut_rol_name(sahip->rol) : "-");
        serial_write((sahip && sahip->rol == KUT_ROL_HIZMET)
                      ? " -> BASARILI\n" : " -> HATA\n");

        /* TORE yetki denetimi: farkli profillerle GERCEK kararlar */
        tore_set_capabilities(1, TORE_PROFILE_CEKIRDEK);   /* cekirdek: tam yetki */
        tore_set_capabilities(2, TORE_PROFILE_HIZMET);     /* hizmet */
        tore_set_capabilities(3, TORE_PROFILE_KISITLI);    /* kisitli */

        tani_yaz("TORE-TEST: cekirdek(pid=1) donanim erisimi -> ");
        serial_write(tore_check(1, KUT_HW_DIRECT, "dogrudan donanim") ? "IZINLI\n" : "RED\n");

        tani_yaz("TORE-TEST: hizmet(pid=2) ag gonderimi -> ");
        serial_write(tore_check(2, KUT_NET_SEND, "paket gonder") ? "IZINLI\n" : "RED\n");

        tani_yaz("TORE-TEST: hizmet(pid=2) disk YAZMA -> ");
        serial_write(tore_check(2, KUT_DISK_WRITE, "diske yaz") ? "IZINLI\n" : "RED (beklenen)\n");

        tani_yaz("TORE-TEST: kisitli(pid=3) ag gonderimi -> ");
        serial_write(tore_check(3, KUT_NET_SEND, "paket gonder") ? "IZINLI\n" : "RED (beklenen)\n");

        tani_yaz("TORE-TEST: kisitli(pid=3) surec sonlandirma -> ");
        serial_write(tore_check(3, KUT_PROC_KILL, "sureci oldur") ? "IZINLI\n" : "RED (beklenen)\n");

        tani_yaz("TORE-TEST: toplam reddedilen erisim sayisi=");
        tani_hex(tore_denied_count());
        serial_write("\n");
    }

    /* --- Asama 23 kaniti: surec basina sanal adres alani ---
     * AYNI sanal adresi (0x300000000) iki FARKLI adres alaninda
     * FARKLI fiziksel cerceveye haritalayip, aralarinda gecis yaparak
     * her birinin KENDI verisini gordugunu kanitliyoruz. Bu, gercek
     * surec izolasyonunun ta kendisidir. */
    {
        uint64_t kernel_as = vmm_current_address_space();
        uint64_t as1 = vmm_create_address_space();
        uint64_t as2 = vmm_create_address_space();
        uint64_t f1 = pmm_alloc_frame();
        uint64_t f2 = pmm_alloc_frame();
        /* ONEMLI: test adresi PML4[1..510] araliginda OLMALI.
         * 0x300000000 (12GB) gibi adresler PML4[0]'a duser - orasi
         * cekirdegin PAYLASILAN kimlik haritasidir, dolayisiyla iki
         * adres alani ayni tabloyu kullanir ve izolasyon OLMAZ
         * (bu hata gercekten yasandi ve testle yakalandi).
         * 0x8000000000 = 512GB -> PML4[1] -> her surece OZEL. */
        const uint64_t SHARED_VA = 0x8000000000ULL;

        if (as1 && as2 && f1 && f2 &&
            vmm_map_page_in(as1, SHARED_VA, f1, VMM_PRESENT | VMM_WRITABLE) == 0 &&
            vmm_map_page_in(as2, SHARED_VA, f2, VMM_PRESENT | VMM_WRITABLE) == 0) {

            serial_write("AS-TEST: iki adres alani, AYNI sanal adres 0x8000000000, "
                         "FARKLI fiziksel cerceveler (");
            serial_write_hex((uint32_t)f1); serial_write(" ve ");
            serial_write_hex((uint32_t)f2); serial_write(")\n");

            /* 1. adres alanina gec ve kendi degerini yaz */
            vmm_switch_address_space(as1);
            *(volatile uint32_t *)SHARED_VA = 0x11111111;
            uint32_t read1 = *(volatile uint32_t *)SHARED_VA;

            /* 2. adres alanina gec ve FARKLI bir deger yaz */
            vmm_switch_address_space(as2);
            *(volatile uint32_t *)SHARED_VA = 0x22222222;
            uint32_t read2 = *(volatile uint32_t *)SHARED_VA;

            /* 1'e geri don - HALA kendi degerini gormeli */
            vmm_switch_address_space(as1);
            uint32_t read1_again = *(volatile uint32_t *)SHARED_VA;

            /* 2'ye tekrar gec - o da kendi degerini korumali */
            vmm_switch_address_space(as2);
            uint32_t read2_again = *(volatile uint32_t *)SHARED_VA;

            /* cekirdek adres alanina geri don */
            vmm_switch_address_space(kernel_as);

            tani_yaz("AS-TEST: alan1 yazdi=0x11111111 okudu=");
            tani_hex(read1);
            serial_write(" | alan2 yazdi=0x22222222 okudu=");
            tani_hex(read2);
            serial_write("\n");
            tani_yaz("AS-TEST: gecis sonrasi alan1=");
            tani_hex(read1_again);
            serial_write(" alan2=");
            tani_hex(read2_again);
            serial_write("\n");
            tani_yaz("AS-TEST: SUREC IZOLASYONU -> ");
            serial_write((read1 == 0x11111111 && read2 == 0x22222222 &&
                          read1_again == 0x11111111 && read2_again == 0x22222222)
                          ? "BASARILI (her alan KENDI bellegini goruyor)\n"
                          : "HATA (adres alanlari birbirine karisiyor!)\n");

            /* Fiziksel cerceveleri dogrudan okuyarak capraz dogrulama */
            uint32_t direct1 = *(volatile uint32_t *)(uintptr_t)f1;
            uint32_t direct2 = *(volatile uint32_t *)(uintptr_t)f2;
            tani_yaz("AS-TEST: fiziksel cerceveler dogrudan okundu f1=");
            tani_hex(direct1);
            serial_write(" f2=");
            tani_hex(direct2);
            serial_write((direct1 == 0x11111111 && direct2 == 0x22222222)
                          ? " -> DOGRULANDI\n" : " -> TUTARSIZ\n");

            vmm_destroy_address_space(as1);
            vmm_destroy_address_space(as2);
        } else {
            tani_yaz("AS-TEST: adres alani kurulumu basarisiz\n");
        }
    }

    /* --- Stage 18: Gunlukleme (Logging) testi --- */
    {
        log_write(LOG_BILGI,  "CEKIRDEK", "otukenOS baslatma tamamlandi");
        log_write_num(LOG_BILGI, "PMM", "toplam fiziksel bellek", pmm_total_bytes());
        log_write_num(LOG_BILGI, "PMM", "bos cerceve sayisi", pmm_free_frames());
        log_write(LOG_BILGI,  "BITIG", "FAT16 ve FAT32 birimleri bagli");
        log_write(LOG_BILGI,  "ULAK", "TCP/UDP/DHCP/DNS katmanlari hazir");
        if (OTUKEN_TANI_MODU)
            log_write(LOG_HATA, "TEST", "ornek HATA kaydi (sadece tani modu)");

        /* Seviye filtresi GERCEKTEN calisiyor mu? AYIKLA seviyesindeki
         * kayit, mevcut seviye BILGI iken YOK SAYILMALI. */
        int before = log_entry_count();
        log_write(LOG_AYIKLA, "TEST", "bu AYIKLA kaydi gorunmemeli");
        int after_filtered = log_entry_count();

        tani_yaz("LOG-TEST: seviye filtresi (AYIKLA kaydi BILGI seviyesinde) -> ");
        serial_write(before == after_filtered ? "DOGRU FILTRELENDI\n" : "HATA - filtrelenmedi!\n");

        /* Seviyeyi AYIKLA yapinca AYNI kayit artik GECMELI */
        log_set_level(LOG_AYIKLA);
        log_write(LOG_AYIKLA, "TEST", "bu AYIKLA kaydi artik GORUNMELI");
        int after_allowed = log_entry_count();
        tani_yaz("LOG-TEST: seviye AYIKLA'ya cekildi -> ");
        serial_write(after_allowed > after_filtered ? "KAYIT GECTI (dogru)\n" : "HATA!\n");
        log_set_level(LOG_BILGI);

        tani_yaz("LOG-TEST: halka tamponda kayit sayisi=");
        tani_hex((uint32_t)log_entry_count());
        serial_write(" hata=");
        tani_hex(log_count_by_level(LOG_HATA));
        serial_write(" uyari=");
        tani_hex(log_count_by_level(LOG_UYARI));
        serial_write(" bilgi=");
        tani_hex(log_count_by_level(LOG_BILGI));
        serial_write("\n");

        /* En yeni kaydi geri okuyup dogrula */
        const log_entry_t *newest = log_get_entry(0);
        if (newest) {
            tani_yaz("LOG-TEST: en yeni kayit [");
            serial_write(log_level_name(newest->level));
            serial_write("][");
            serial_write(newest->subsystem);
            serial_write("] ");
            serial_write(newest->message);
            serial_write("\n");
        }
    }

    /* --- Stage 16: ULAK (ag) --- */
    if (ulak_init() == 0) {
        uint8_t my_ip[4] = {10, 0, 2, 15};   /* QEMU kullanici agi varsayilani */
        uint8_t gw_ip[4] = {10, 0, 2, 2};    /* QEMU sanal ag gecidi */
        ulak_set_ip(my_ip);

        ulak_send_arp_request(gw_ip);
        serial_write("ULAK: ag gecidine ARP istegi gonderildi\n");

        const char *msg = "OTUKEN ULAK TEST PAKETI";
        uint8_t payload[32];
        int mlen = 0;
        while (msg[mlen] && mlen < 31) { payload[mlen] = (uint8_t)msg[mlen]; mlen++; }
        ulak_send_udp(gw_ip, 12345, 9999, payload, (uint16_t)mlen);
        serial_write("ULAK: UDP test paketi gonderildi\n");

        /* --- Stage 16 tamamlanmasi: TCP uc-yollu el sikismasi ---
         * QEMU'nun kullanici agi (user-mode networking) 10.0.2.2
         * uzerinden TCP baglantilarini karsilar; buraya baglanip
         * gercek bir handshake yapiyoruz. */
        /* --- Asama 29: DHCP + DNS + soket API --- */
        socket_init();
        dhcp_init();
        dns_init();
        dhcp_discover();     /* QEMU slirp gercek bir DHCP sunucusu calistirir */

        /* --- Asama 38: ÖTÜKEN Ag Hizmetleri --- */
        aghizmet_init();

        tcp_init();
        {
            uint8_t tcp_srv[4] = {10, 0, 2, 100};
            /* Once hedefin MAC adresini ogren (ARP) - TCP el sikismasi
             * icin paketlerin GERCEK hedefe gitmesi sart. */
            ulak_send_arp_request(tcp_srv);
            g_tcp_conn = tcp_connect(tcp_srv, 8080);
        }
        if (g_tcp_conn >= 0) {
            serial_write("ULAK-TCP: baglanti istegi gonderildi, "
                         "SYN-ACK bekleniyor...\n");
        }
    }

    disk_cache_init();
    int disk2_ok = 0;
    for (int i = 0; i < driver_count(); i++) {
        const driver_t *d = driver_get(i);
        /* isim karsilastirmasi (indeks tabanli kirilgan kontrol yerine) */
        const char *target = "ata-disk2";
        int same = 1, k = 0;
        while (target[k] || d->name[k]) {
            if (d->name[k] != target[k]) { same = 0; break; }
            k++;
        }
        if (same && d->state == DRIVER_STATE_ACTIVE) disk2_ok = 1;
    }

    /* SMP adim 3: AP'leri uyandir. sti ONCESI (BSP beklerken araya
     * gorev girmesin, seri cikti karismasin) ve TIMUR/CR0.WP SONRASI
     * (AP bu kayitlari BSP'den kopyalar). */
    smp_baslat();

    __asm__ volatile ("sti");
    serial_write("kesmeler acildi (sti)\n");

    zaman_baslat();          /* 1.4.9: gercek saat (RTC) - ust cubuk ondan okur */
    gui_init();
    serial_write("ULGEN (grafik masaustu ortami) baslatildi - otukenOS-mini calisir durumda\n");

    /* --- Stage 11 kaniti: Virtual File System. gui_init() icinde
     * fat16_mount() zaten calisti (Dosya Yoneticisi penceresi icin) -
     * simdi bunun UZERINE genel VFS katmanini kuruyoruz. */
    vfs_init();
    if (vfs_fat16_mount_root() == 0) {
        vfs_dirent_t entries[8];
        int n = vfs_list_dir("/", entries, 8);
        serial_write("vfs-test: '/' listelendi, dosya sayisi=");
        serial_write_hex((uint32_t)n);
        serial_write("\n");
        for (int i = 0; i < n; i++) {
            serial_write("vfs-test:   - ");
            serial_write(entries[i].name);
            serial_write(" (");
            serial_write_hex((uint32_t)entries[i].size);
            serial_write(" bayt)\n");
        }

        /* Dosya tanitici (fd) ile ACIP, KUCUK PARCALAR halinde OKUYUP
         * (offset takibini kanitlamak icin), butun icerigi dogru
         * sirayla birlestirdigimizi dogruluyoruz. */
        int fd = vfs_open("/NOTLAR.TXT");
        if (fd >= 0) {
            char reconstructed[256];
            uint64_t total = 0;
            for (;;) {
                char chunk[8];
                int got = vfs_read(fd, chunk, sizeof(chunk));
                if (got <= 0) break;
                for (int i = 0; i < got && total < sizeof(reconstructed) - 1; i++)
                    reconstructed[total++] = chunk[i];
            }
            reconstructed[total] = 0;
            vfs_close(fd);

            serial_write("vfs-test: NOTLAR.TXT ");
            serial_write_hex((uint32_t)total);
            serial_write(" baytta, 8'er baytlik parcalarla (fd offset "
                         "takibiyle) okundu\n");
            serial_write("vfs-test: ilk satir='");
            /* sadece ilk satiri goster (okunabilirlik icin) */
            for (uint64_t i = 0; i < total && reconstructed[i] != '\n'; i++) {
                char one[2] = {reconstructed[i], 0};
                serial_write(one);
            }
            serial_write("'\n");
        } else {
            serial_write("vfs-test: vfs_open basarisiz\n");
        }
    }

    /* --- Asama 27 kaniti: FAT16 YAZMA ---
     * Diske gercekten bir dosya yazip, sonra AYNI dosyayi VFS
     * uzerinden geri okuyoruz. Nihai dogrulama disk imajinin ana
     * makinede (mtools ile) incelenmesiyle yapilacak. */
    {
        static const char yazi[] =
            "OTUKEN YAZMA SINAMASI\n"
            "Bu dosya cekirdek tarafindan FAT16 uzerine YAZILDI.\n"
            "Asama 27 - Depolama 2.0\n";
        uint32_t ylen = 0;
        while (yazi[ylen]) ylen++;

        int wr = fat16_write_file("YAZDIM.TXT", (const uint8_t *)yazi, ylen);
        tani_yaz("YAZMA-TEST: fat16_write_file sonuc=");
        tani_hex((uint32_t)wr);
        serial_write(" beklenen=");
        tani_hex(ylen);
        serial_write("\n");

        if (wr > 0) {
            /* Geri oku - VFS/FAT16 okuma yolu uzerinden */
            int fd = vfs_open("/YAZDIM.TXT");
            if (fd >= 0) {
                char rb[128];
                int got = vfs_read(fd, rb, sizeof(rb) - 1);
                vfs_close(fd);
                if (got > 0) {
                    rb[got] = 0;
                    int ayni = 1;
                    for (int i = 0; i < got && i < (int)ylen; i++)
                        if (rb[i] != yazi[i]) ayni = 0;
                    tani_yaz("YAZMA-TEST: geri okuma ");
                    tani_hex((uint32_t)got);
                    serial_write(" bayt, icerik ");
                    serial_write((ayni && got == (int)ylen)
                                  ? "AYNI -> BASARILI\n" : "FARKLI -> HATA\n");
                } else {
                    tani_yaz("YAZMA-TEST: geri okuma basarisiz\n");
                }
            } else {
                tani_yaz("YAZMA-TEST: yazilan dosya ACILAMADI -> HATA\n");
            }
        }
    }

    /* --- Stage 12 kaniti: FAT32 destegi, ikinci bir fiziksel diskte
     * (QEMU -hdb), VFS'e AYRI bir noktada ("/fat32") baglanarak - bu
     * ayrica VFS'in coklu-mount cozumlemesini de gercekten test eder. */
    if (disk2_ok && fat32_mount() == 0 && vfs_fat32_mount("/fat32") == 0) {
        vfs_dirent_t entries32[8];
        int n32 = vfs_list_dir("/fat32", entries32, 8);
        serial_write("vfs-fat32-test: '/fat32' listelendi, dosya sayisi=");
        serial_write_hex((uint32_t)n32);
        serial_write("\n");
        for (int i = 0; i < n32; i++) {
            serial_write("vfs-fat32-test:   - ");
            serial_write(entries32[i].name);
            serial_write(" (");
            serial_write_hex((uint32_t)entries32[i].size);
            serial_write(" bayt)\n");
        }

        int fd32 = vfs_open("/fat32/FAT32F.TXT");
        if (fd32 >= 0) {
            char buf32[64];
            int got32 = vfs_read(fd32, buf32, sizeof(buf32) - 1);
            vfs_close(fd32);
            if (got32 > 0) {
                buf32[got32] = 0;
                serial_write("vfs-fat32-test: okunan icerik='");
                for (int i = 0; i < got32 && buf32[i] != '\n'; i++) {
                    char one[2] = {buf32[i], 0};
                    serial_write(one);
                }
                serial_write("'\n");
            }
        } else {
            serial_write("vfs-fat32-test: vfs_open basarisiz\n");
        }

        /* vfs_stat ikinci bag noktasindan cozulur (SYS_STAT bunu kullanir) */
        {
            uint64_t sb = 0, lb = (uint64_t)-1; int sd = -1, kd = -1;
            for (int i = 0; i < n32; i++) {
                const char *a = entries32[i].name, *b = "FAT32F.TXT";
                while (*a && *a == *b) { a++; b++; }
                if (!*a && !*b) lb = entries32[i].size;
            }
            int r1 = vfs_stat("/fat32/fat32f.txt", &sb, &sd);
            int r2 = vfs_stat("/fat32", 0, &kd);
            int r3 = vfs_stat("/fat32/YOK.TXT", 0, 0);
            int r4 = vfs_stat("/fat32x/fat32f.txt", 0, 0);
            int iyi = r1 == 0 && sb == lb && sd == 0 && r2 == 0 && kd == 1 && r3 != 0 && r4 != 0;
            serial_write("STAT-TEST: /fat32 dosya boyu=");
            serial_write_hex((uint32_t)sb);
            serial_write(iyi ? " (listeyle ayni), bag noktasi dizin, yok/sinir disi reddedildi -> BASARILI\n"
                             : " -> HATA (FAT32 bag noktasinda stat)\n");
        }

        uint32_t cache_hits = 0, cache_misses = 0;
        disk_cache_stats(&cache_hits, &cache_misses);
        serial_write("disk-cache-test: hits="); serial_write_hex(cache_hits);
        serial_write(" misses="); serial_write_hex(cache_misses);
        serial_write("\n");
    } else {
        serial_write("vfs-fat32-test: ikinci disk/FAT32 bulunamadi (atlandi)\n");
    }

    scheduler_init("gui_main");
    process_manager_init();
    mutex_init(&print_mutex);
    task_create("TaskA", task_a_entry);
    task_create("TaskB", task_b_entry);
    process_create("TaskC-bounded", task_c_entry);
    process_t *task_d_proc = process_create("TaskD-forever", task_d_entry);
    thread_manager_init();
    mutex_init(&counter_mutex);
    thread_create("ThreadE-sleep30", thread_e_entry);
    thread_create("ThreadF-sleep75", thread_f_entry);
    dede_sira_baslat();   /* 1.4.9: eszamansiz konusma (ses karti varsa) */
    thread_create("HeapStresI", heap_stress_i_entry);
    thread_create("HeapStresJ", heap_stress_j_entry);
    thread_create("ThreadG-sync", thread_g_entry);
    thread_create("ThreadH-sync", thread_h_entry);

    msgqueue_init(&demo_queue);
    thread_create("MQ-Producer", mq_producer_entry);
    thread_create("MQ-Consumer", mq_consumer_entry);

    pipe_init(&demo_pipe);
    thread_create("Pipe-Writer", pipe_writer_entry);
    thread_create("Pipe-Reader", pipe_reader_entry);

    /* NOT: timer_init artik Driver Manager tarafindan ("pit-timer"
     * surucusu olarak) daha ONCE cagrildi. scheduler_tick, scheduler
     * hazir olmadan gelen tick'leri zaten guvenle yok sayar
     * (scheduler_started bayragi), bu yuzden erken baslamasi sorun
     * degil - burada TEKRAR cagirmiyoruz. */
    serial_write("scheduler+process+thread+sync+ipc: hazir\n");

    /* --- IPC kaniti 3: Shared Memory altyapisi. Ayni fiziksel
     * cerceveleri IKI FARKLI sanal adrese haritalayip, birinden
     * yazip digerinden okuyarak GERCEKTEN ayni fiziksel bellegi
     * gosterdiklerini kanitliyoruz. */
    {
        uint32_t shm_id = shm_create(4096);
        if (shm_id != 0) {
            void *va1 = shm_attach(shm_id, 0x600000000ULL);
            void *va2 = shm_attach(shm_id, 0x610000000ULL);
            if (va1 && va2) {
                *(volatile uint32_t *)va1 = 0xC0FFEE42;
                uint32_t via_va2 = *(volatile uint32_t *)va2;
                serial_write("shm-test: va1'e yazilan=0xC0FFEE42, va2'den okunan=");
                serial_write_hex(via_va2);
                serial_write((via_va2 == 0xC0FFEE42) ? " (DOGRU - ayni fiziksel bellek)\n"
                                                       : " (YANLIS!)\n");
            } else {
                serial_write("shm-test: attach basarisiz\n");
            }
            shm_detach(shm_id, va1);
            shm_detach(shm_id, va2);
            shm_destroy(shm_id);
        }
    }

    /* --- Stage 9 kaniti: gercek SYSCALL/SYSRET donanim mekanizmasi.
     * Ring3 henuz yok, bu yuzden ring0'dan test ediyoruz - ama
     * mekanizmanin KENDISI (MSR'lar, register shuffle, dispatcher,
     * sysretq ile geri donus) Ring3 geldiginde AYNEN kullanilacak. */
    syscall_init();
    {
        /* ONEMLI MIMARI NOT: SYSRET komutu, donuste CS/SS'i HER ZAMAN
         * RPL=3 (ring3) yapar - bu, Intel'in SYSCALL/SYSRET ciftini
         * TASARIM GEREGI ring3<->ring0 gidis-donusu icin tanimlamasindan
         * kaynaklanir, kosula bagli degildir. Ring3 (Stage 10) henuz
         * yokken ring0'dan gercek SYSCALL komutunu cagirip SYSRET ile
         * "ring0'a geri donmeyi" beklemek, komutun kendi sozlesmesiyle
         * celisir: sayfalarimiz henuz kullanici-erisilebilir (US biti)
         * isaretli olmadigi icin, sysret sonrasi ilk komut getirmede
         * cokme olusur (bunu gercekten deneyip gozlemledik).
         *
         * Bu yuzden: MSR kurulumu (EFER.SCE/STAR/LSTAR/FMASK) ve giris
         * stub'unun (register shuffle) dogrulugu bu asamada TAMAMLANDI
         * (yukaridaki kurulum mesaji + syscall_entry.asm'in disassembly
         * ile dogrulanmis register duzeni). Gercek donanim SYSCALL/SYSRET
         * gidis-donusunu CANLI test etmek, Ring3 gercek bir kullanici
         * baglami sagladiginda (Stage 10) yapilacak - o zamana kadar
         * dispatcher/tablo mantigini GUVENLI sekilde dogrudan C
         * cagrisiyla test ediyoruz. */
        /* DENETIM NOTU: SYS_WRITE artik kullanici isaretcisi DOGRULAR.
         * Buradaki dize CEKIRDEK bellegindedir, dolayisiyla dogru
         * davranis REDDETMEKTIR. Bu cagri artik bir "basari" sinamasi
         * degil, dogrulamanin CALISTIGININ kanitidir - ve yalnizca
         * tani modunda yapilir (uretimde yaniltici UYARI uretiyordu). */
        if (OTUKEN_TANI_MODU) {
            uint64_t r1 = syscall_dispatch(SYS_WRITE,
                (uint64_t)(uintptr_t)"[SYSCALL] cekirdek isaretcisi (reddedilmeli)\n",
                0, 0, 0);
            tani_yaz("SYSCALL-TEST: cekirdek isaretcisiyle SYS_WRITE -> ");
            tani_yaz((r1 == (uint64_t)-1) ? "REDDEDILDI (dogru)\n"
                                           : "KABUL EDILDI (ACIK!)\n");
        }

        uint64_t pid = syscall_dispatch(SYS_GETPID, 0, 0, 0, 0);
        tani_yaz("SYSCALL-TEST: SYS_GETPID sonucu=");
        tani_hex((uint32_t)pid);
        serial_write("\n");

        /* Gecersiz syscall reddi sadece TANI MODU'nda sinanir - uretim
         * modunda bosuna bir UYARI kaydi olusturmasin. */
        uint64_t bad = OTUKEN_TANI_MODU ? syscall_dispatch(99, 0, 0, 0, 0)
                                         : (uint64_t)-1;
        tani_yaz("SYSCALL-TEST: gecersiz syscall(99) sonucu=");
        tani_hex((uint32_t)bad);
        serial_write(" (0xFFFFFFFF beklenir)\n");
    }

    /* --- Stage 10: Ring3. TSS, ring3'ten gelecek herhangi bir
     * kesme/istisna icin GEREKLI - test gorevi olusturulmadan ONCE
     * kurulmasi SART. */
    tss_init();
    thread_create("Ring3Test", ring3_test_task_entry);
    thread_create("Ring3ELF", ring3_elf_task_entry);
    /* --- Asama 39-40 (yeniden mimari): UMAY AYRI BIR PROGRAM ---
     * UMAY artik cekirdegin parcasi DEGIL. Diskteki UMAY.ELF
     * dosyasindan yuklenip Ring3'te, kendi surec baglaminda calisir.
     * Cekirdek tarafinda kalan tek sey: kaynak sozlesmesi denetimi ve
     * yukleme. Model guncellemesi = diskteki dosyayi degistirmek. */
    umay_init();
    {
        umay_sozlesme_t s;
        s.azami_bellek     = 8ULL * 1024 * 1024;
        s.azami_cerceve    = 2048;
        s.dosya_okuma      = 1;
        s.dosya_yazma      = 0;    /* YASAK */
        s.ag_erisimi       = 0;    /* YASAK */
        s.surec_olusturma  = 0;
        s.ipc_erisimi      = 1;
        umay_yukle("umay-userland-v1", &s);
    }

    /* Buyuk/kucuk harf duyarsiz dosya acma (FAT tasarim geregi boyle
     * olmalidir - kullanici testinde bulunan hata). */
    {
        int fdk = vfs_open("/notlar.txt");   /* KUCUK harfle */
        if (fdk >= 0) vfs_close(fdk);
        /* Sonucu TEK serial_write ile yaz: birden cok cagri, arada
         * calisan gorevlerin ciktisiyla BOLUNEBILIYOR ve testler
         * yanlis negatif veriyordu. */
        tani_yaz(fdk >= 0 ? "HARF-TEST: kucuk harfle dosya acma -> BASARILI\n"
                           : "HARF-TEST: kucuk harfle dosya acma -> HATA\n");
    }

    /* ===== DENETIM DOGRULAMA SINAMALARI =====
     * Duzeltmelerin GERCEKTEN koruma sagladigini kanitlar.
     *
     * SADECE TANI MODUNDA calisir: bu sinamalar KASITLI olarak
     * gecersiz ELF'ler ve cekirdek isaretcileri besler; korumalar
     * dogru calistigi icin [HATA]/[UYARI] kayitlari uretirler.
     * Uretim modunda bunlar, gercek bir sorun varmis gibi gorunup
     * gunlugu kirletiyordu (denetimde bulundu). */
    if (OTUKEN_TANI_MODU) {
        int gecti = 0, kaldi = 0;

        /* 1) ELF: cekirdek adresine yuklenmeye calisan sahte ELF
         *    REDDEDILMELI (aksi halde cekirdek ezilirdi). */
        {
            static uint8_t sahte[256];
            for (int i = 0; i < 256; i++) sahte[i] = 0;
            sahte[0]=0x7F; sahte[1]='E'; sahte[2]='L'; sahte[3]='F';
            sahte[4]=2; sahte[5]=1;                 /* 64-bit, LSB */
            *(uint16_t*)&sahte[16] = 2;             /* ET_EXEC */
            *(uint16_t*)&sahte[18] = 62;            /* x86-64 */
            *(uint64_t*)&sahte[24] = 0xFFFFFFFF80101000ULL;  /* CEKIRDEK giris! */
            *(uint64_t*)&sahte[32] = 64;            /* e_phoff */
            *(uint16_t*)&sahte[54] = 56;            /* e_phentsize */
            *(uint16_t*)&sahte[56] = 1;             /* e_phnum */
            uint8_t *ph = &sahte[64];
            *(uint32_t*)&ph[0]  = 1;                /* PT_LOAD */
            *(uint32_t*)&ph[4]  = 7;                /* RWX */
            *(uint64_t*)&ph[8]  = 0;                /* p_offset */
            *(uint64_t*)&ph[16] = 0xFFFFFFFF80101000ULL; /* CEKIRDEGE yaz! */
            *(uint64_t*)&ph[32] = 16;               /* p_filesz */
            *(uint64_t*)&ph[40] = 4096;             /* p_memsz */
            elf_image_t sahte_img;
            int r = elf_load(sahte, sizeof(sahte), &sahte_img);
            tani_yaz("GUVENLIK-1: cekirdek adresine yuklenen ELF -> ");
            if (r != 0) { tani_yaz("REDDEDILDI (dogru)\n"); gecti++; }
            else { tani_yaz("KABUL EDILDI (ACIK!)\n"); kaldi++; }
        }

        /* 2) ELF: tamsayi tasmasi denemesi REDDEDILMELI */
        {
            static uint8_t sahte2[256];
            for (int i = 0; i < 256; i++) sahte2[i] = 0;
            sahte2[0]=0x7F; sahte2[1]='E'; sahte2[2]='L'; sahte2[3]='F';
            sahte2[4]=2; sahte2[5]=1;
            *(uint16_t*)&sahte2[16] = 2;
            *(uint16_t*)&sahte2[18] = 62;
            *(uint64_t*)&sahte2[24] = 0x8000400000ULL;
            *(uint64_t*)&sahte2[32] = 64;
            *(uint16_t*)&sahte2[54] = 56;
            *(uint16_t*)&sahte2[56] = 1;
            uint8_t *ph2 = &sahte2[64];
            *(uint32_t*)&ph2[0]  = 1;
            *(uint64_t*)&ph2[8]  = 0xFFFFFFFFFFFFFF00ULL;  /* TASMA */
            *(uint64_t*)&ph2[16] = 0x8000400000ULL;
            *(uint64_t*)&ph2[32] = 0x200;
            *(uint64_t*)&ph2[40] = 0x200;
            elf_image_t img2;
            int r2 = elf_load(sahte2, sizeof(sahte2), &img2);
            tani_yaz("GUVENLIK-2: tamsayi tasmali ELF -> ");
            if (r2 != 0) { tani_yaz("REDDEDILDI (dogru)\n"); gecti++; }
            else { tani_yaz("KABUL EDILDI (ACIK!)\n"); kaldi++; }
        }

        /* 3) Syscall: cekirdek isaretcisi gecirme REDDEDILMELI */
        {
            uint64_t kernel_adres = (uint64_t)(uintptr_t)"cekirdek";
            uint64_t r3 = syscall_dispatch(SYS_WRITE, kernel_adres, 0, 0, 0);
            tani_yaz("GUVENLIK-3: syscall'a CEKIRDEK isaretcisi -> ");
            if (r3 == (uint64_t)-1) { tani_yaz("REDDEDILDI (dogru)\n"); gecti++; }
            else { tani_yaz("KABUL EDILDI (ACIK!)\n"); kaldi++; }
        }

        /* 4) Syscall: haritali OLMAYAN kullanici adresi REDDEDILMELI */
        {
            uint64_t r4 = syscall_dispatch(SYS_OPEN, 0x00007F0000000000ULL, 0, 0, 0);
            tani_yaz("GUVENLIK-4: haritasiz kullanici adresi -> ");
            if (r4 == (uint64_t)-1) { tani_yaz("REDDEDILDI (dogru)\n"); gecti++; }
            else { tani_yaz("KABUL EDILDI (ACIK!)\n"); kaldi++; }
        }

        /* 5) VMM: cekirdek higher-half dalina USER izni VERILMEMELI */
        {
            uint64_t cr3v;
            __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3v));
            uint64_t *pml4v = (uint64_t *)(uintptr_t)(cr3v & 0x000FFFFFFFFFF000ULL);
            int us511 = (pml4v[511] & VMM_USER) ? 1 : 0;
            /* Tek cagri: parcali yazimda diger gorevlerin ciktisi araya
             * girip "KAPALI" baska satira dusuyordu (test yanlis KALDI). */
            if (!us511) { tani_yaz("GUVENLIK-5: PML4[511] (cekirdek) USER biti -> KAPALI (dogru)\n"); gecti++; }
            else { tani_yaz("GUVENLIK-5: PML4[511] (cekirdek) USER biti -> ACIK (ACIK!)\n"); kaldi++; }
        }

        tani_yaz("GUVENLIK-OZET: gecen=");
        tani_hex((uint32_t)gecti);
        tani_yaz(" kalan=");
        tani_hex((uint32_t)kaldi);
        tani_yaz(kaldi == 0 ? " -> TUM KORUMALAR CALISIYOR\n"
                            : " -> ACIK VAR\n");
    }

    /* --- Denetim sonrasi ACIMASIZ dayaniklilik sinamasi --- */
    if (OTUKEN_TANI_MODU) {
        uint64_t if_once, if_sonra;
        __asm__ volatile ("pushfq; pop %0" : "=r"(if_once));
        int ih = iskence_tumu();
        __asm__ volatile ("pushfq; pop %0" : "=r"(if_sonra));
        serial_write("[IZ] iskence oncesi IF=");
        serial_write((if_once & 0x200) ? "1" : "0");
        serial_write(" sonrasi IF=");
        serial_write((if_sonra & 0x200) ? "1\n" : "0  <-- KESMELER KAPALI KALDI!\n");
        tani_yaz("ISKENCE-TOPLAM: hata=");
        tani_hex((uint32_t)ih);
        tani_yaz(ih == 0 ? " -> SAGLAM\n" : " -> HATA VAR\n");
    }

    /* --- Asama 41: baski sinamasi (silinen blok geri eklendi) --- */
    kararlilik_baslat();
    {
        int bh = kararlilik_baski_sinamasi(40);
        tani_yaz("BASKI-TEST: 40 tur yogun ayirma/iade -> hata=");
        tani_hex((uint32_t)bh);
        tani_yaz(bh == 0 ? " -> BASARILI (kaynak dengesi korundu)\n"
                          : " -> HATA\n");
    }

    /* --- UMAY sozlesme sinirlarinin GERCEKTEN uygulandigini dogrula.
     * UMAY artik ayri bir programdir; bu sinama, cekirdek tarafindaki
     * yetenek arabiriminin (umay_cek_*) sozlesmeyi zorladigini
     * gosterir. --- */
    {
        const uint8_t deneme[8] = {'U','M','A','Y',0,0,0,0};
        int yz = umay_cek_dosya_yaz("UMAYYAZ.TXT", deneme, 4);
        tani_yaz("UMAY-TEST: yasakli dosya YAZMA -> ");
        tani_yaz((yz < 0) ? "REDDEDILDI (dogru)\n" : "IZIN VERILDI (HATA!)\n");

        uint8_t hedefip[4] = {10, 0, 2, 2};
        int ag = umay_cek_ag_gonder(hedefip, 9999, deneme, 4);
        tani_yaz("UMAY-TEST: yasakli AG erisimi -> ");
        tani_yaz((ag < 0) ? "REDDEDILDI (dogru)\n" : "IZIN VERILDI (HATA!)\n");

        void *cok = umay_cek_bellek_al(16ULL * 1024 * 1024);
        tani_yaz("UMAY-TEST: sinir ustu bellek istegi -> ");
        tani_yaz((cok == 0) ? "REDDEDILDI (dogru)\n" : "IZIN VERILDI (HATA!)\n");

        const umay_runtime_t *ud = umay_durum_al();
        tani_yaz("UMAY-TEST: reddedilen istek sayisi=");
        tani_hex(ud->reddedilen_istek);
        tani_yaz((ud->reddedilen_istek >= 3) ? " -> BASARILI\n" : " -> HATA\n");
    }

    /* --- Asama 42: sistem hazir ozeti (silinen blok geri eklendi) --- */
    {
        /* KIMLIK HARITASI BUTUNLUGU: acilis sinamalari kimlik adreslerini
         * yeniden haritalayip unmap ederek DELIK birakiyordu (0x700000,
         * TIMUR 48 GB, TLB 504 GB). Delik, PMM o cerceveyi dagitinca
         * rastgele PAGE FAULT demektir. Hepsi kendini gostermeli. */
        uint32_t a_, b_, c_, d_;
        __asm__ volatile ("cpuid" : "=a"(a_), "=b"(b_), "=c"(c_), "=d"(d_) : "a"(0x80000001u), "c"(0));
        int gb1 = (d_ >> 26) & 1;
        const uint64_t adr[3] = { 0x700000ULL, 0xC00000000ULL, 0x7E00000000ULL };
        int n_adr = gb1 ? 3 : 1;          /* 2MB yolunda kimlik yalniz 4 GB */
        int delik = 0;
        for (int i = 0; i < n_adr; i++)
            if (vmm_get_physical(adr[i]) != adr[i]) delik++;
        tani_yaz(delik == 0 ? "KIMLIK-TEST: kimlik haritasinda delik yok\n"
                            : "KIMLIK-TEST: kimlik haritasinda DELIK VAR\n");
    }
    {
        /* DEDE: Turkce sesli tanima ve birim sayaci dogru mu. Sesin
         * DUYULUP duyulmadigi burada olculemez (QEMU'da ses arka ucu
         * yok); olculebilen sey cozumlemenin dogrulugudur. */
        int n = dede_birim_sayisi("Ötüken'in yurdunda ocak tütüyor");
        int s1 = dede_sesli_mi('a') && dede_sesli_mi(0x00FC);
        int s2 = !dede_sesli_mi('k') && !dede_sesli_mi('s');
        serial_write((n == 28 && s1 && s2)
                     ? "DEDE-TEST: cozumleme dogru\n"
                     : "DEDE-TEST: cozumleme YANLIS\n");
    }
    {
        /* Alt dizin: GERCEK davranis olcumu. Olusturulur, listelenir,
         * icine yol cozulur, ikinci kez olusturma REDDEDILIR. */
        int r1 = vfs_mkdir("/KUT");
        int r2 = vfs_mkdir("/KUT");           /* zaten var -> -2 */
        fat16_dirent_t d;
        int bulundu = fat16_yol_bul("/KUT", &d);
        int dizin_mi = (bulundu == 0) && d.is_dir;
        fat16_dirent_t liste[8];
        int n = fat16_listele_yol("/KUT", liste, 8);   /* "." ve ".." */
        int r3 = vfs_mkdir("/KUT/UMAY");
        fat16_dirent_t d2;
        int ic = fat16_yol_bul("/KUT/UMAY", &d2);
        /* VFS katmani da alt dizini gormeli: stat ve listele. */
        uint64_t boyut = 0; int dm = 0;
        int st = vfs_stat("/KUT", &boyut, &dm);
        vfs_dirent_t vl[8];
        int vn = vfs_list_dir("/KUT", vl, 8);
        serial_write((st == 0 && dm == 1 && vn >= 2)
                     ? "VFS-DIZIN-TEST: stat ve listele alt dizini goruyor\n"
                     : "VFS-DIZIN-TEST: VFS alt dizini GORMUYOR\n");
        serial_write(((r1 == 0 || r1 == -2) && r2 == -2 && dizin_mi &&
                      n >= 2 && (r3 == 0 || r3 == -2) && ic == 0 && d2.is_dir)
                     ? "DIZIN-TEST: alt dizin olusuyor ve yol cozuluyor\n"
                     : "DIZIN-TEST: alt dizin YANLIS\n");
    }
    serial_write("\n--- " OTUKEN_SURUM_ADI " HAZIR ---\n");
    serial_write("bilesenler: " OTUKEN_BILESENLER "\n");
    {
        const kararlilik_t *kk = kararlilik_al();
        serial_write("bellek: ");
        serial_write_hex((uint32_t)(pmm_total_bytes() >> 20));
        serial_write(" MB | bos cerceve: ");
        serial_write_hex((uint32_t)kk->ilk_bos_cerceve);
        serial_write(" | aktif surucu: ");
        serial_write_hex((uint32_t)driver_count());
        serial_write("\n");
        serial_write("kabuk baslatiliyor...\n\n");
    }

    thread_create("OtukenKabuk", shell_task_entry);

    /* --- Asama 31/32: IPC 2.0 + Servis Sistemi --- */
    /* --- Asama 33: ULGEN compositor ---
     * Eski GUI (gui.c) ekranin UST kismini kullanmaya devam ediyor;
     * ULGEN yuzeyleri ALT bolgeye yerlesir. Boylece iki sistem
     * cakismadan yan yana calisir ve compositor GERCEK ekranda
     * dogrulanabilir. */
    ulgen_init();
#ifdef OTUKEN_EKRAN_SINAMA
    /* Ekran regresyon sinamasi (tests/ulgen_render ile AYNI sahne ve
     * denetimler), gercek VBE cerceve tamponunda. Kesmeler kapali: baska
     * gorev sahneye yazamaz. Sonra cekirdek DONAR: ekran goruntusu
     * (QEMU screendump) ana makine ciktisiyla piksel piksel karsilastirilir. */
    __asm__ volatile ("cli");
    ekran_sinamasi(serial_write);
    serial_write("EKRAN-SINAMA: cekirdek donduruldu (ekran goruntusu icin)\n");
    for (;;) __asm__ volatile ("cli; hlt");
#endif
    {
        int y1 = ulgen_yuzey_olustur("ULGEN YUZEY 1", 560, 470, 200, 110);
        int y2 = ulgen_yuzey_olustur("ULGEN YUZEY 2 (ustte)", 640, 520, 200, 110);
        if (y1 >= 0 && y2 >= 0) {
            ulgen_doldur(y1, 0xEDE6D6);
            ulgen_dikdortgen(y1, 0, 0, 200, 22, 0x2E5B4F);
            ulgen_yazi(y1, 6, 3, "ARKA YUZEY", 0xFFFFFF, 0x2E5B4F);
            ulgen_yazi(y1, 8, 40, "KENDI TAMPONUNA", 0x1A1A1A, 0xEDE6D6);
            ulgen_yazi(y1, 8, 56, "CIZILDI", 0x1A1A1A, 0xEDE6D6);

            ulgen_doldur(y2, 0xDDE8F0);
            ulgen_dikdortgen(y2, 0, 0, 200, 22, 0xB33A3A);
            ulgen_yazi(y2, 6, 3, "ON YUZEY", 0xFFFFFF, 0xB33A3A);
            ulgen_yazi(y2, 8, 40, "Z-SIRASI: USTTE", 0x1A1A1A, 0xDDE8F0);
            ulgen_yazi(y2, 8, 56, "ORTUSME DOGRU", 0x1A1A1A, 0xDDE8F0);
            ulgen_one_getir(y2);   /* y2 y1'in UZERINDE olmali */

            int is1 = ulgen_birlestir();
            int is2 = ulgen_birlestir();   /* hicbir sey degismedi -> 0 olmali */
            tani_yaz("ULGEN-TEST: ilk birlestirme is=");
            tani_hex((uint32_t)is1);
            serial_write(" ikinci (degisiklik yok) is=");
            tani_hex((uint32_t)is2);
            serial_write(" -> ");
            serial_write((is1 > 0 && is2 == 0)
                ? "BASARILI (hasar takibi calisiyor, bosa cizim yok)\n"
                : "HATA\n");
            tani_yaz("ULGEN-TEST: yuzey sayisi=");
            tani_hex((uint32_t)ulgen_yuzey_sayisi());
            serial_write("\n");
            /* 1.4.8: sinama yuzeyleri uretim kipinde masaustunde kalmaz
             * (yuzeyler durur, yalniz gizlenir: kimlik numaralari degismez). */
            if (!OTUKEN_TANI_MODU) {
                ulgen_gorunurluk(y1, 0);
                ulgen_gorunurluk(y2, 0);
            }
        }
    }

    /* --- Asama 37: KERVAN paket sistemi --- */
    kervan_init();
    kervan2_init();          /* 1.4.9: imzali paketler; /KERVAN.LST */
    {   /* 1.4.9: ERGENEKON dizge imaji (GRUB modulu) acilista dogrulanir */
        static ergenekon_durum_t ed;
        ergenekon_durum(&ed);
        serial_write(ed.var ? (ed.gecerli ? "ERGENEKON: dizge imaji dogrulandi, dosya=" : "ERGENEKON: dizge imaji GECERSIZ: ")
                            : "ERGENEKON: dizge imaji yok\n");
        if (ed.var && ed.gecerli) { serial_write_hex(ed.dosya); serial_write(" anahtar="); serial_write_hex(ed.anahtar_no); serial_write("\n"); }
        else if (ed.var) { serial_write(ed.neden); serial_write("\n"); }
    }
    /* v1 ornek paketi YALNIZ tani kipinde kurulur: uretim acilisi her
     * seferinde diske OKUBENI.TXT/VERI.DAT yaziyordu (1.4.9'da duzeltildi). */
    if (OTUKEN_TANI_MODU) {
        extern const unsigned char ornek_paket[];
        extern const unsigned long ornek_paket_boyut;

        int r = kervan_kur(ornek_paket, (uint32_t)ornek_paket_boyut);
        tani_yaz("KERVAN-TEST: kurulum sonucu=");
        tani_hex((uint32_t)r);
        serial_write(" -> ");
        serial_write((r == 2) ? "BASARILI\n" : "HATA\n");

        /* Kurulan dosya GERCEKTEN diskte mi? VFS uzerinden oku. */
        int fd = vfs_open("/OKUBENI.TXT");
        if (fd >= 0) {
            char rb[96];
            int got = vfs_read(fd, rb, sizeof(rb) - 1);
            vfs_close(fd);
            if (got > 0) {
                rb[got] = 0;
                tani_yaz("KERVAN-TEST: kurulan dosya okundu (");
                tani_hex((uint32_t)got);
                serial_write(" bayt) -> BASARILI\n");
            }
        } else {
            tani_yaz("KERVAN-TEST: kurulan dosya ACILAMADI -> HATA\n");
        }

        /* GUVENLIK: bozulmus paket REDDEDILMELI */
        static uint8_t bozuk[512];
        for (uint32_t i = 0; i < 512 && i < ornek_paket_boyut; i++)
            bozuk[i] = ornek_paket[i];
        bozuk[300] ^= 0xFF;   /* veri blogunu boz */
        int r2 = kervan_dogrula(bozuk, 512);
        tani_yaz("KERVAN-TEST: BOZUK paket dogrulamasi=");
        tani_hex((uint32_t)r2);
        serial_write(" -> ");
        serial_write((r2 != 0) ? "REDDEDILDI (dogru)\n" : "KABUL EDILDI (HATA!)\n");

        tani_yaz("KERVAN-TEST: kurulu paket sayisi=");
        tani_hex((uint32_t)kervan_kurulu_sayisi());
        serial_write("\n");
    }

    /* 1.4.7 regresyon sinamalari, TEK CEKIRDEK evresi: ULGEN ve VFS hazir,
     * AP'ler henuz gorev almiyor, UMAY baslamadi (ASYOK sinamasi kesmeler
     * kapaliyken butun bos cerceveleri alir: bu evre sart). */
    if (OTUKEN_TANI_MODU) sinama147_tek_cekirdek();

    ipc2_init();
    servis_init();
    g_zaman_servis_id = servis_tanimla("zamand", 0, servis_zamand, 3);
    g_hesap_servis_id = servis_tanimla("hesapd", "zamand", servis_hesapd, 3);
    servis_hepsini_baslat();
    thread_create("IpcIstemci", ipc_istemci_gorev);

    /* UMAY EN SON baslar: IPC, servisler ve dosya sistemi hazir
     * olmadan bir calisma zamani baslatmak yanlistir (gercekten
     * yasandi: UMAY, IPC katmanindan ONCE calisip portunu acamadi). */
    thread_create("UMAY", umay_gorev_entry);
    arayici_baslat();          /* 1.5: UMAY'in ag arastirmacisi */

    /* SMP adim 5: gorevler ve zamanlayici hazir - AP'ler katilsin. */
    if (OTUKEN_SMP_DAGITIM) smp_dagitimi_ac();
    if (OTUKEN_TANI_MODU) smp_yaris_sinamasi();
    /* 1.4.7: surec yasam dongusu, AP'ler gorev ALIRKEN (calisan surecin
     * baska cekirdekten yok edilmesi). */
    if (OTUKEN_TANI_MODU) sinama147_surec_smp();
    /* UMAY uyum sinamasi AYRI gorevde: AGIR sinama aciksa onunla ayni
     * anda kosar ve UMAY'i yuk altinda dener. */
    if (OTUKEN_TANI_MODU && OTUKEN_UMAY_UYUM) thread_create("UmayUyum", umay_uyum_sinamasi);
    if (OTUKEN_TANI_MODU && OTUKEN_AGIR_SINAMA) agir_sinama();
    /* 1.4.8: masaustu uygulamalari uctan uca (gercek tiklama yolu) */
    if (OTUKEN_TANI_MODU) sinama148_uygulama_baslat();
    /* 1.4.9: Ring3 ag sinamasi (yalniz sinama diskinde AGSINA.ELF varsa) */
    if (OTUKEN_TANI_MODU) sinama149_ag_baslat();
    /* 1.4.9: KERVAN v2 (yalniz sinama diskinde K2*.KRV varsa) */
    if (OTUKEN_TANI_MODU) sinama_kervan_baslat();
    /* 1.4.9: Uygulamalar menusu ve ULAK/DEDE/TORE/TIMUR/KUT */
    if (OTUKEN_TANI_MODU) sinama_dizin_baslat();
    if (OTUKEN_TANI_MODU) sinama_sse_baslat();      /* 1.4.9: x87/SSE durumu korunuyor mu */    /* 1.4.9: alt dizine yazma + yazma koku */
    if (OTUKEN_TANI_MODU) sinama_asena_baslat();   /* 1.4.9: ASENA/ERGENEKON (ASENA.SNM varsa) */
    if (OTUKEN_TANI_MODU) sinama_uyg_baslat();

    uint32_t task_d_pid = task_d_proc ? task_d_proc->pid : 0;
    int task_d_killed = 0;

    for (;;) {
        /* Asama 25: kabuk calisirken klavye ONUN'dur. Ana dongu
         * tuslari tuketirse kabuk hicbir sey okuyamaz (iki tuketici
         * ayni kuyruktan cekemez). */
        /* USB klavye: periyodik yokla ve tuslari PS/2 ile AYNI kuyruga
         * besle - boylece kabuk hangi klavyenin kullanildigini
         * BILMEK ZORUNDA DEGILDIR. */
        if (uhci_klavye_var()) {
            static uint64_t son_usb = 0;
            uint64_t simdi_u = timer_get_ticks();
            if (simdi_u != son_usb) {
                uhci_yokla();
                uint8_t ut = uhci_klavye_oku();
                if (ut) keyboard_push_char((char)ut);
                son_usb = simdi_u;
            }
        }

        if (!g_shell_active) {
            char c = keyboard_pop_char();
            if (c) {
                gui_type_char(c);
            }
        }
        if (mouse_poll_dirty()) {
            gui_on_mouse_event();
        }

        /* Demo: yaklasik 3 saniye (150 tick @ 50Hz) sonra TaskD'yi
         * DISARIDAN sonlandir - process_destroy()'un baska bir sureci
         * gercekten oldurup kaynaklarini geri aldigini kanitlar. */
        if (!task_d_killed && task_d_pid != 0 && timer_get_ticks() > 150) {
            /* process_destroy guvenli olmayan anda (TaskD baska cekirdekte
             * calisiyor / kilit tutuyor / bloke) reddeder: sonra yeniden. */
            if (process_destroy(task_d_pid) == 0) task_d_killed = 1;
        }

        /* Kendi kendine sonlanmis (TaskC gibi) gorevlerin bellegini
         * periyodik olarak geri al. */
        static uint64_t last_reap_tick = 0;
        uint64_t now = timer_get_ticks();
        if (now - last_reap_tick > 25) { /* ~yarim saniyede bir */
            scheduler_reap_terminated();
            last_reap_tick = now;
        }

        static int sync_verified = 0;
        if (!sync_verified && thread_g_done && thread_h_done) {
            sync_verified = 1;
            tani_yaz("SYNC-TEST: shared_counter=");
            tani_hex((uint32_t)shared_counter);
            serial_write(" beklenen=");
            tani_hex((uint32_t)(SYNC_ITERS * 2));
            serial_write(" -> ");
            serial_write((shared_counter == (uint64_t)(SYNC_ITERS * 2))
                          ? "BASARILI (kayip guncelleme yok, mutex calisiyor)\n"
                          : "HATA (kayip guncelleme VAR)\n");
        }

        static int mq_verified = 0;
        if (!mq_verified && mq_producer_done && mq_consumer_done) {
            mq_verified = 1;
            tani_yaz("MQ-TEST: son alinan=");
            tani_hex(mq_last_received);
            serial_write(" sira=");
            serial_write(mq_order_ok ? "DOGRU" : "YANLIS");
            serial_write(" -> ");
            serial_write((mq_last_received == 10 && mq_order_ok)
                          ? "BASARILI\n" : "HATA\n");
        }

        static int pipe_verified = 0;
        if (!pipe_verified && pipe_writer_done && pipe_reader_done) {
            pipe_verified = 1;
            tani_yaz("PIPE-TEST: veri butunlugu -> ");
            serial_write(pipe_result_ok ? "BASARILI\n" : "HATA\n");
        }

        static int heap_stress_verified = 0;
        if (!heap_stress_verified && heap_stress_i_done && heap_stress_j_done) {
            heap_stress_verified = 1;
            uint64_t tb2, ub2, fb2, cnt2;
            heap_get_stats(&tb2, &ub2, &fb2, &cnt2);
            serial_write("HEAP-YARIS-TESTI: 2 gorev x ");
            serial_write_hex(HEAP_STRESS_ITERS);
            serial_write(" es zamanli kmalloc/kfree -> hata sayisi=");
            serial_write_hex((uint32_t)heap_stress_errors);
            serial_write(" -> ");
            serial_write(heap_stress_errors == 0
                ? "BASARILI (heap bozulmadi, kesme korumasi calisiyor)\n"
                : "HATA - heap bozulmasi tespit edildi!\n");
        }

        /* ARP yaniti geldikten sonra (ilk SYN yayina gitmis olabilir)
         * baglantiyi bir kez yeniden dene - artik dogru MAC biliniyor. */
        static int tcp_retried = 0;
        if (!tcp_retried && timer_get_ticks() > 60) {
            tcp_retried = 1;
            uint8_t tcp_srv2[4] = {10, 0, 2, 100};
            int c2 = tcp_connect(tcp_srv2, 8080);
            if (c2 >= 0) {
                g_tcp_conn = c2;
                serial_write("ULAK-TCP: ARP cozuldukten sonra baglanti yeniden denendi\n");
            }
        }

        /* --- TCP DURUM MAKINESI DOGRULAMASI ---
         * Gercek aga baglanmak yerine (bunun icin dinleyen bir sunucu
         * ve kararli bir test duzenegi gerekiyor), TCP durum makinesini
         * DOGRUDAN sinamak icin elle hazirlanmis gercek bir SYN-ACK
         * segmentini alma yoluna enjekte ediyoruz. Bu, KENDI kodumuzu
         * (durum gecisleri, sira numarasi takibi, ACK uretimi) sinar.
         * Paketlerin fiziksel olarak agda dogru dolastigi ZATEN ayrica
         * kanitlandi: gonderdigimiz SYN'in checksum'i pcap dokumunden
         * bagimsiz hesaplanip dogrulandi ve gercek slirp'ten RST alindi. */
        /* DHCP yapilandirmasi geldiginde DNS sorgusu baslat */
        static int dhcp_checked = 0, dns_sent = 0, dns_checked = 0;
        if (!dhcp_checked && timer_get_ticks() > 40) {
            dhcp_checked = 1;
            const dhcp_config_t *dc = dhcp_get_config();
            tani_yaz("DHCP-TEST: yapilandirma durumu -> ");
            if (dc->configured) {
                serial_write("BASARILI (IP/gecit/DNS alindi)\n");
            } else {
                serial_write("henuz gelmedi\n");
            }
        }
        if (!dns_sent && timer_get_ticks() > 60) {
            const dhcp_config_t *dc = dhcp_get_config();
            if (dc->configured) {
                dns_sent = 1;
                dns_query("example.com", dc->dns);
            }
        }
        if (dns_sent && !dns_checked && timer_get_ticks() > 130) {
            dns_checked = 1;
            uint8_t rip[4];
            tani_yaz("DNS-TEST: '");
            serial_write(dns_last_query());
            serial_write("' cozumlemesi -> ");
            if (dns_get_result(rip)) {
                serial_write("BASARILI ");
                for (int i = 0; i < 4; i++) { serial_write_hex(rip[i]); if (i<3) serial_write("."); }
                serial_write("\n");
            } else {
                serial_write("yanit gelmedi\n");
            }
        }

        /* ULGEN: kirli yuzeyleri birlestir (hasar takibi sayesinde
         * degisiklik yoksa hicbir is yapmaz).
         * ONEMLI: eski GUI (gui.c) her yenilemede TUM ekrani siliyor
         * ve compositor'un ciktisini eziyor. Compositor bunu KENDILIGINDEN
         * bilemez, bu yuzden GUI yenilemesinden sonra yuzeyleri kirli
         * isaretliyoruz. Iki cizim sistemi ayni framebuffer'i paylastigi
         * surece bu koordinasyon SART. */
        /* ekran adimi: asagida masaustu_tur() (ULGEN birlestirme dahil) */

        /* init: servisleri periyodik izle (cokerse yeniden baslat) */
        static uint64_t son_izleme = 0;
        uint64_t simdi_t = timer_get_ticks();
        if (simdi_t - son_izleme > 60) {
            son_izleme = simdi_t;
            servis_izle();
        }

        static int servis_raporlandi = 0;
        if (!servis_raporlandi && timer_get_ticks() > 90) {
            servis_raporlandi = 1;
            tani_yaz("SERVIS-TEST: durum tablosu\n");
            int calisan = 0;
            for (int i = 0; i < servis_sayisi(); i++) {
                const servis_t *s = servis_al(i);
                serial_write("  - ");
                serial_write(s->name);
                serial_write(" : ");
                serial_write(servis_durum_adi(s->durum));
                serial_write(" nabiz=");
                serial_write_hex(s->heartbeat);
                serial_write("\n");
                if (s->durum == SERVIS_CALISIYOR) calisan++;
            }
            tani_yaz("SERVIS-TEST: calisan=");
            tani_hex((uint32_t)calisan);
            serial_write("/");
            tani_hex((uint32_t)servis_sayisi());
            serial_write(" -> ");
            serial_write((calisan == servis_sayisi()) ? "BASARILI\n" : "EKSIK\n");
            tani_yaz("IPC2-TEST: gonderilen=");
            tani_hex(ipc_stats_sent());
            serial_write(" alinan=");
            tani_hex(ipc_stats_received());
            serial_write("\n");
        }

        static int tcp_sm_tested = 0;
        if (!tcp_sm_tested && g_tcp_conn >= 0 && timer_get_ticks() > 80) {
            tcp_sm_tested = 1;
            const tcp_connection_t *before = tcp_get_connection(g_tcp_conn);
            tcp_sentetik_isaretle(g_tcp_conn);   /* GERCEK ag testi DEGIL */
            tani_yaz("TCP-SENTETIK: enjeksiyon oncesi durum=");
            serial_write(tcp_state_name(before->state));
            serial_write("\n");

            /* Gercek bir SYN-ACK segmenti kur (20 baytlik TCP basligi) */
            uint8_t synack[20];
            uint16_t our_port = before->local_port;
            uint16_t their_port = before->remote_port;
            uint32_t their_isn = 0xABCD0000;

            synack[0] = (uint8_t)(their_port >> 8);
            synack[1] = (uint8_t)(their_port & 0xFF);
            synack[2] = (uint8_t)(our_port >> 8);
            synack[3] = (uint8_t)(our_port & 0xFF);
            synack[4] = (uint8_t)(their_isn >> 24);
            synack[5] = (uint8_t)(their_isn >> 16);
            synack[6] = (uint8_t)(their_isn >> 8);
            synack[7] = (uint8_t)(their_isn);
            /* ACK numarasi: bizim SYN'imizi onayliyor */
            uint32_t ack_us = before->send_next;
            synack[8]  = (uint8_t)(ack_us >> 24);
            synack[9]  = (uint8_t)(ack_us >> 16);
            synack[10] = (uint8_t)(ack_us >> 8);
            synack[11] = (uint8_t)(ack_us);
            synack[12] = 0x50;               /* veri ofseti=5 */
            synack[13] = TCP_SYN | TCP_ACK;  /* SYN-ACK */
            synack[14] = 0x20; synack[15] = 0x00;
            synack[16] = 0x00; synack[17] = 0x00;
            synack[18] = 0x00; synack[19] = 0x00;

            uint8_t srv_ip[4] = {10, 0, 2, 100};
            /* 1.4.9: TCP artik saglama toplamini DOGRULAR (bozuk/sahte segment
             * dusurulur). Enjekte edilen segment gecerli bir toplam tasir;
             * eskiden 0 idi ve kabul ediliyordu (bu bir yigin acigiydi). */
            {
                uint16_t ck = tcp_saglama(srv_ip, ulak_get_state()->ip, synack, 20);
                synack[16] = (uint8_t)(ck >> 8);
                synack[17] = (uint8_t)ck;
            }
            tcp_handle_segment(srv_ip, synack, 20);

            const tcp_connection_t *after = tcp_get_connection(g_tcp_conn);
            tani_yaz("TCP-SENTETIK: SYN-ACK enjekte edildi -> durum=");
            serial_write(tcp_state_name(after->state));
            serial_write(" recv_next=");
            tani_hex(after->recv_next);
            serial_write(" (0xABCD0001 olmali)\n");
            tani_yaz("TCP-SENTETIK: uc-yollu el sikismasi -> ");
            serial_write((after->state == TCP_KURULDU &&
                          after->recv_next == their_isn + 1)
                          ? "BASARILI (durum KURULDU, sira no dogru, ACK gonderildi)\n"
                          : "HATA\n");

            /* Kurulu baglanti uzerinden veri gonderimi */
            if (after->state == TCP_KURULDU) {
                const char *http = "GET / HTTP/1.0";
                uint8_t buf[24];
                int n = 0;
                while (http[n] && n < 23) { buf[n] = (uint8_t)http[n]; n++; }
                uint32_t seq_before = after->send_next;
                int sr = tcp_send(g_tcp_conn, buf, (uint16_t)n);
                const tcp_connection_t *aft2 = tcp_get_connection(g_tcp_conn);
                tani_yaz("TCP-SENTETIK: veri gonderimi sonuc=");
                tani_hex((uint32_t)sr);
                serial_write(" gonderilen_bayt=");
                tani_hex(aft2->bytes_sent);
                serial_write(" sira_no_ilerlemesi=");
                tani_hex(aft2->send_next - seq_before);
                serial_write((sr == 0 && aft2->bytes_sent == (uint32_t)n &&
                              (aft2->send_next - seq_before) == (uint32_t)n)
                              ? " -> BASARILI\n" : " -> HATA\n");

                /* Duzgun kapatma (FIN) */
                tcp_close(g_tcp_conn);
                const tcp_connection_t *aft3 = tcp_get_connection(g_tcp_conn);
                tani_yaz("TCP-SENTETIK: kapatma sonrasi durum=");
                serial_write(tcp_state_name(aft3->state));
                serial_write((aft3->state == TCP_FIN_BEKLIYOR)
                              ? " -> BASARILI\n" : " -> HATA\n");
            }
        }

        static int tcp_verified = 0;
        static uint64_t tcp_check_start = 0;
        if (!tcp_verified && tcp_check_start == 0) tcp_check_start = timer_get_ticks();
        if (!tcp_verified && g_tcp_conn >= 0 && timer_get_ticks() - tcp_check_start > 120) {
            tcp_verified = 1;
            const tcp_connection_t *tc = tcp_get_connection(g_tcp_conn);
            tani_yaz("TCP-TEST: baglanti durumu=");
            serial_write(tcp_state_name(tc->state));
            serial_write(" -> ");
            if (tc->state == TCP_KURULDU) {
                serial_write("BASARILI (uc-yollu el sikismasi tamamlandi)\n");
                const char *http = "GET / HTTP/1.0\r\n\r\n";
                uint8_t buf[32];
                int n = 0;
                while (http[n] && n < 31) { buf[n] = (uint8_t)http[n]; n++; }
                tcp_send(g_tcp_conn, buf, (uint16_t)n);
                tani_yaz("TCP-TEST: kurulu baglanti uzerinden veri gonderildi\n");
            } else {
                serial_write("el sikismasi tamamlanmadi (durum yukarida)\n");
            }
        }

        static int ring3_verified = 0;
        static uint64_t ring3_check_start = 0;
        if (!ring3_verified && ring3_check_start == 0) ring3_check_start = timer_get_ticks();
        if (!ring3_verified && timer_get_ticks() - ring3_check_start > 100) {
            ring3_verified = 1;
            tani_yaz("RING3-TEST: g_ring3_proof_received=");
            tani_hex((uint32_t)g_ring3_proof_received);
            serial_write(" -> ");
            serial_write(g_ring3_proof_received ? "BASARILI (ring3 kodu calisti)\n"
                                                  : "HATA (ring3 kodu calismadi)\n");
        }

        /* Asama 41: kaynaklari periyodik orneklle (sizinti yakalama) */
        static uint64_t son_ornek = 0;
        static int rapor_verildi = 0;
        uint64_t simdi_k = timer_get_ticks();
        if (simdi_k - son_ornek > 100) {      /* ~2 saniyede bir */
            kararlilik_ornekle();
            son_ornek = simdi_k;
        }
        if (!rapor_verildi && simdi_k > 500) { /* ~10 saniye sonra rapor */
            rapor_verildi = 1;
            kararlilik_rapor();
        }

        /* Stage 18: gunluk penceresini canli tut (saniyede ~2 kez) */
        static uint64_t last_gui_refresh = 0;
        uint64_t gui_now = timer_get_ticks();
        int periyodik = (gui_now - last_gui_refresh > 25);
        if (periyodik) last_gui_refresh = gui_now;
        /* Ekran adimi TEK yerde (sinama da ayni islevi cagirir) */
        masaustu_tur(periyodik);

        /* Ag hizmetleri: gelen istekleri karsila */
        aghizmet_isle();

        /* ULAK: gelen paketleri isle */
        static uint64_t last_net_poll = 0;
        uint64_t net_now = timer_get_ticks();
        if (net_now != last_net_poll) {
            ag_yokla();          /* 1.4.9: paketler + TCP zamanlayicisi (kilitli) */
            zaman_esitle_gerekirse();
            last_net_poll = net_now;
        }

        __asm__ volatile ("hlt");
    }
}
