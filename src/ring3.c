#include "ring3.h"
#include "smap.h"
#include "tore.h"
#include "sched.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "elf.h"
#include "vfs.h"
#include "timur.h"

extern uint8_t ring3_test_program[];
extern uint8_t ring3_test_program_end[];
extern void enter_ring3(uint64_t entry_rip, uint64_t user_rsp);

/* 1.4.7: Ring3'e gecmeden HEMEN ONCE: gorev AÇIK TORE profilini alir ve
 * "kullanici" olarak isaretlenir. Bundan sonra her syscall'u politika
 * tablosundaki yetkiyle denetlenir (syscall.c). Profilde olmayan cagri
 * REDDEDILIR - varsayilan izin yoktur. */
static void ring3_hazirla(uint32_t profil) {
    task_t *ben = scheduler_current_task();
    if (!ben) return;
    tore_set_capabilities((uint32_t)ben->id, profil);
    ben->kullanici = 1;
}

void ring3_gir(uint64_t giris, uint64_t yigin, uint32_t profil) {
    ring3_hazirla(profil);
    enter_ring3(giris, yigin);
    for (;;) __asm__ volatile ("cli; hlt");   /* enter_ring3 geri donmez */
}

/* 0-4GB kimlik haritasinin ve onceki testlerin (shm: 0x6...,
 * vmm: 0x5.../0x700000) disinda, bu asamaya ozel taze adresler. */
/* Denetim duzeltmesi: eski adres (0x900000000) PML4[0]-da, yani
 * CEKIRDEK kimlik haritasi dalindaydi. VMM artik o dala USER izni
 * VERMEDIGI icin dogru olan kullanici bolgesine (PML4[1]) tasindi. */
#define RING3_CODE_VIRT 0x8000100000ULL
#define RING3_STACK_VIRT 0x8000200000ULL
#define RING3_STACK_SIZE PMM_FRAME_SIZE

void ring3_run_test(void) {
    uint64_t code_phys = pmm_alloc_frame();
    uint64_t stack_phys = pmm_alloc_frame();

    /* Stage 19 duzeltmesi: her erken donus yolunda, O ANA KADAR
     * ayrilmis TUM cerceveler serbest birakilmali - aksi halde
     * bellek sizintisi olur (bu, kod taramasinda bulunan GERCEK bir
     * hataydi). */
    if (code_phys == 0 || stack_phys == 0) {
        if (code_phys)  pmm_free_frame(code_phys);
        if (stack_phys) pmm_free_frame(stack_phys);
        serial_write("ring3: HATA - PMM'den cerceve alinamadi\n");
        return;
    }

    /* Kod sayfasi: ONCE yazilabilir haritala (icerigi kopyalayabilmek
     * icin), kopyalama bittikten SONRA salt-okunura daralt.
     *
     * DENETIM BULGUSU: eskiden dogrudan salt-okunur haritalanip
     * cekirdek oraya YAZIYORDU. Bu yalnizca CR0.WP biti TEMIZ
     * oldugunda calisir; GRUB-BIOS onu temiz birakiyordu, GRUB-UEFI
     * ise SET ediyor - UEFI'de sayfa hatasi aliyorduk. Gercek
     * donanimda (UEFI) cokme demekti. */
    if (vmm_map_page(RING3_CODE_VIRT, code_phys,
                      VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        serial_write("ring3: HATA - kod sayfasi haritalanamadi\n");
        return;
    }
    /* Yigin sayfasi: kullanici + yazilabilir. */
    if (vmm_map_page(RING3_STACK_VIRT, stack_phys,
                      VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        vmm_unmap_page(RING3_CODE_VIRT);
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        serial_write("ring3: HATA - yigin sayfasi haritalanamadi\n");
        return;
    }

    uint64_t prog_size = (uint64_t)(ring3_test_program_end - ring3_test_program);
    if (prog_size > PMM_FRAME_SIZE) {
        vmm_unmap_page(RING3_CODE_VIRT);
        vmm_unmap_page(RING3_STACK_VIRT);
        pmm_free_frame(code_phys);
        pmm_free_frame(stack_phys);
        serial_write("ring3: HATA - test programi bir sayfadan buyuk\n");
        return;
    }

    uint8_t *dst = (uint8_t *)(uintptr_t)RING3_CODE_VIRT;
    kullanici_ac();                         /* 1.4.9 SMAP: kullanici sayfasi */
    for (uint64_t i = 0; i < prog_size; i++) dst[i] = ring3_test_program[i];
    kullanici_kapat();

    /* Kopyalama bitti: artik kod sayfasi YAZILAMAZ olmali (W^X). */
    timur_protect_code(RING3_CODE_VIRT);

    serial_write("ring3: kod (");
    serial_write_hex((uint32_t)prog_size);
    serial_write(" bayt) ve yigin sayfalari hazir, ring3'e atlaniyor...\n");

    uint64_t user_rsp = RING3_STACK_VIRT + RING3_STACK_SIZE - 16; /* 16-hizali, tepe */
    ring3_hazirla(TORE_PROFILE_SINAMA);
    enter_ring3(RING3_CODE_VIRT, user_rsp);
    /* BURAYA ASLA DONULMEZ */
}

/* --- Stage 10 tamamlanmasi: GERCEK ELF64 programi calistirma --- */
extern const unsigned char embedded_user_elf[];
extern const unsigned long embedded_user_elf_size;

#define ELF_USER_STACK_VIRT 0x8000800000ULL

void ring3_run_elf(void) {
    elf_image_t img;

    serial_write("ring3-elf: gomulu ELF ayristiriliyor (");
    serial_write_hex((uint32_t)embedded_user_elf_size);
    serial_write(" bayt)...\n");

    if (elf_load(embedded_user_elf, embedded_user_elf_size, &img) != 0) {
        serial_write("ring3-elf: ELF yuklenemedi\n");
        return;
    }

    serial_write("ring3-elf: ");
    serial_write_hex((uint32_t)img.segments_loaded);
    serial_write(" segment yuklendi, giris noktasi=");
    serial_write_hex((uint32_t)(img.entry_point >> 32));
    serial_write_hex((uint32_t)img.entry_point);
    serial_write("\n");

    /* Kullanici yigini ayir (yazilabilir + NX - W^X politikasi) */
    uint64_t stack_phys = pmm_alloc_frame();
    if (stack_phys == 0) {
        serial_write("ring3-elf: yigin cercevesi ayrilamadi\n");
        return;
    }
    if (vmm_map_page(ELF_USER_STACK_VIRT, stack_phys,
                      VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        pmm_free_frame(stack_phys);
        serial_write("ring3-elf: yigin haritalanamadi\n");
        return;
    }
    timur_protect_data(ELF_USER_STACK_VIRT); /* yigin CALISTIRILAMAZ olmali */

    serial_write("ring3-elf: ELF programina atlaniyor (Ring3)...\n");
    uint64_t user_rsp = ELF_USER_STACK_VIRT + PMM_FRAME_SIZE - 16;
    ring3_hazirla(TORE_PROFILE_SINAMA);
    enter_ring3(img.entry_point, user_rsp);
    /* GERI DONMEZ */
}


/* --- Asama 25: OTUKEN Kabugunu Ring3'te calistir --- */
extern const unsigned char embedded_shell_elf[];
extern const unsigned long embedded_shell_elf_size;

#define SHELL_STACK_VIRT 0x8000C00000ULL
#define SHELL_STACK_PAGES 4

void ring3_run_shell(void) {
    elf_image_t img;

    serial_write("kabuk: ELF ayristiriliyor (");
    serial_write_hex((uint32_t)embedded_shell_elf_size);
    serial_write(" bayt)...\n");

    if (elf_load(embedded_shell_elf, embedded_shell_elf_size, &img) != 0) {
        serial_write("kabuk: ELF yuklenemedi\n");
        return;
    }

    /* Kabuk daha derin cagri yapar (readline, tokenize, komutlar) -
     * tek sayfa yigin yetmez, 16KB veriyoruz. */
    for (int i = 0; i < SHELL_STACK_PAGES; i++) {
        uint64_t sp = pmm_alloc_frame();
        if (sp == 0) { serial_write("kabuk: yigin ayrilamadi\n"); return; }
        if (vmm_map_page(SHELL_STACK_VIRT + (uint64_t)i * PMM_FRAME_SIZE, sp,
                          VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            pmm_free_frame(sp);
            serial_write("kabuk: yigin haritalanamadi\n");
            return;
        }
        timur_protect_data(SHELL_STACK_VIRT + (uint64_t)i * PMM_FRAME_SIZE);
    }

    serial_write("kabuk: Ring3'e geciliyor, giris noktasi=");
    serial_write_hex((uint32_t)img.entry_point);
    serial_write("\n");

    uint64_t rsp = SHELL_STACK_VIRT + (uint64_t)SHELL_STACK_PAGES * PMM_FRAME_SIZE - 64;
    ring3_hazirla(TORE_PROFILE_KABUK);
    enter_ring3(img.entry_point, rsp);
}


/* --- UMAY'i DISKTEN yukleyip Ring3'te calistir ---
 * Kritik mimari fark: kabuk ve demo program cekirdege GOMULUDUR;
 * UMAY ise diskteki bir DOSYADAN okunur. Bu, uctan uca zinciri
 * kanitlar: disk -> BITIG(FAT16) -> VFS -> ELF yukleyici -> Ring3.
 * Ayrica modeli guncellemek icin cekirdegi yeniden derlemek GEREKMEZ. */
#define UMAY_YIGIN_VIRT  0x8000E00000ULL
/* UMAY derin cagri zincirleri ve buyuk yerel tamponlar kullanir;
 * 4 sayfa (16 KB) yigin tasmasina yol aciyordu. 16 sayfa = 64 KB.
 * 1.4.4: US1 egitimi sirasinda 64 KB de TASTI (umay_tur 52 KB cerceve;
 * UMAY'da duzeltildi, en derin zincir artik ~20 KB). Pay icin 32 sayfa
 * = 128 KB. Altindaki sayfa haritasizdir: tasma sessiz bozulma degil,
 * yakalanan sayfa hatasidir. */
#define UMAY_YIGIN_SAYFA 32
/* 64 KB sinir UMAY'i KESIK okutuyordu (ikili 106 KB). Sinir buyutuldu;
 * sinirdan buyuk dosya artik SESSIZCE KESILMEZ, acikca reddedilir. */
/* 1.5: Dede Korkut ses motoru (OSES + Turkce sesletim tablolari, ~470 KB)
 * UMAY'a girdi: ikili 512 KB'i asar. Sinir 1 MB. */
#define UMAY_AZAMI_BOYUT (1024 * 1024)
/* 1.5: UMAY'in heap siniri. Ses modeli (DEDE.OSES, 64 MB float32) +
 * motorun calisma alani (96 MB; uzun cumle parcalanir, tepe ~68 MB) +
 * UMAY'in kendi obegi. Varsayilan gorev siniri (4 MB) degismedi. */
/* 1.5: + US1 egiticisi (iki alan x 40 MB: buyutmede eski ve yeni model) */
#define UMAY_HEAP_AZAMI  (320ULL * 1024 * 1024)

int ring3_umay_calistir(const char *yol) {
    /* SMP adim 6: asagidaki 512 KB statik tampon tek kopya. Ikinci bir
     * eszamanli yukleme onu ezerdi; islev Ring3'e gectigi icin GERI
     * DONMEZ, kilitle sarilamaz -> tek kullanim korumasi. */
    static volatile int yukleniyor = 0;
    if (__atomic_exchange_n(&yukleniyor, 1, __ATOMIC_ACQUIRE)) {
        serial_write("UMAY-HOST: REDDEDILDI - baska bir UMAY yuklemesi suruyor\n");
        return -1;
    }
    /* Ring3'e girmeden ONCE yetenek profili verilir: TORE olmadan UMAY
     * kendi kutuphanesini bile okuyamaz. Dogrudan donanim YOK. */
    {
        /* 1.4.7: AÇIK UMAY profili (1.4.6'daki 4 bit + eskiden hic
         * denetlenmeyen cagrilarin yetkileri). DISK_YAZ/SUREC_OLUSTUR yine YOK. */
        task_t *ben = scheduler_current_task();
        if (ben) { tore_set_capabilities((uint32_t)ben->id, TORE_PROFILE_UMAY); tore_yazma_koku((uint32_t)ben->id, UMAY_YAZMA_KOKU); ben->heap_azami = UMAY_HEAP_AZAMI; }
    }

    static uint8_t elf_tampon[UMAY_AZAMI_BOYUT];

    int fd = vfs_open(yol);
    if (fd < 0) {
        serial_write("UMAY-HOST: program diskte bulunamadi: ");
        serial_write(yol);
        serial_write("\n");
        __atomic_store_n(&yukleniyor, 0, __ATOMIC_RELEASE);
        return -1;
    }

    /* Dosyanin TAMAMINI parcalar halinde oku */
    uint32_t toplam = 0;
    int tasti = 0;
    for (;;) {
        if (toplam + 4096 > UMAY_AZAMI_BOYUT) {
            uint8_t sinama[1];
            if (vfs_read(fd, sinama, 1) > 0) tasti = 1;
            break;
        }
        int n = vfs_read(fd, elf_tampon + toplam, 4096);
        if (n <= 0) break;
        toplam += (uint32_t)n;
    }
    vfs_close(fd);
    if (tasti) {
        serial_write("UMAY-HOST: REDDEDILDI - ikili tampon sinirini asiyor\n");
        __atomic_store_n(&yukleniyor, 0, __ATOMIC_RELEASE);
        return -1;
    }

    serial_write("UMAY-HOST: '");
    serial_write(yol);
    serial_write("' diskten okundu, boyut=");
    serial_write_hex(toplam);
    serial_write(" bayt\n");

    elf_image_t img;
    if (elf_load(elf_tampon, toplam, &img) != 0) {
        serial_write("UMAY-HOST: ELF ayristirilamadi\n");
        /* Denetim (1.4.6): bayrak basarisizlikta sifirlanmiyordu; bir kez
         * basarisiz olan UMAY bir daha HIC yuklenemiyordu. */
        __atomic_store_n(&yukleniyor, 0, __ATOMIC_RELEASE);
        return -1;
    }

    for (int i = 0; i < UMAY_YIGIN_SAYFA; i++) {
        uint64_t sp = pmm_alloc_frame();
        if (sp == 0) return -1;
        if (vmm_map_page(UMAY_YIGIN_VIRT + (uint64_t)i * PMM_FRAME_SIZE, sp,
                          VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            pmm_free_frame(sp);
            return -1;
        }
        timur_protect_data(UMAY_YIGIN_VIRT + (uint64_t)i * PMM_FRAME_SIZE);
    }

    serial_write("UMAY-HOST: Ring3'e geciliyor, giris=");
    serial_write_hex((uint32_t)img.entry_point);
    serial_write("\n");

    uint64_t rsp = UMAY_YIGIN_VIRT + (uint64_t)UMAY_YIGIN_SAYFA * PMM_FRAME_SIZE - 64;
    ring3_hazirla(TORE_PROFILE_UMAY);
    { task_t *ben = scheduler_current_task(); if (ben) tore_yazma_koku((uint32_t)ben->id, UMAY_YAZMA_KOKU); }
    enter_ring3(img.entry_point, rsp);
    return 0;   /* buraya ulasilmaz */
}
