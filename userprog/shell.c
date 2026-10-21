/* ÖTÜKEN Kabuğu (Shell 2.0)
 * Ring3'te, kullanici modunda calisan gercek bir komut yorumlayicisi.
 * Cekirdege SADECE syscall'lar uzerinden erisir. */
#include "ulib.h"

#define MAX_LINE 128
#define MAX_ARGS 8

static void cmd_yardim(void) {
    puts("OTUKEN Kabugu - kullanilabilir komutlar:\n");
    puts("  yardim         bu listeyi goster\n");
    puts("  listele [yol]  dizin icerigini listele (varsayilan /)\n");
    puts("  goster <dosya> dosya icerigini yazdir\n");
    puts("  kimlik         surec kimligini (PID) goster\n");
    puts("  saat           zamanlayici tik sayisini goster\n");
    puts("  bellek         userland bellek ayirma sinamasi\n");
    puts("  ciz            ekrana renkli bir sekil ciz\n");
    puts("  umay <soru>    UMAY'a IPC ile soru sor (durum/bellek/gorev/zaman/dosya)\n");
    puts("  pencere        ULGEN penceresi acan GUI uygulamasi\n");
    puts("  servisler      calisan sistem servislerini listele\n");
    puts("  yankila <söz>  verilen sozu geri yazdir\n");
    puts("  cik            kabugu sonlandir\n");
}

static void cmd_listele(const char *path) {
    dirent_u entries[16];
    int n = listdir(path, entries, 16);
    if (n < 0) {
        puts("HATA: dizin okunamadi: ");
        puts(path);
        puts("\n");
        return;
    }
    puts("Dizin: "); puts(path); puts("\n");
    for (int i = 0; i < n; i++) {
        puts("  ");
        puts(entries[i].name);
        puts("  ");
        print_dec(entries[i].size);
        puts(" bayt");
        if (entries[i].is_dir) puts(" [dizin]");
        puts("\n");
    }
    puts("Toplam: "); print_dec((uint64_u)n); puts(" girdi\n");
}

static void cmd_goster(const char *path) {
    int fd = open(path);
    if (fd < 0) {
        puts("HATA: dosya acilamadi: "); puts(path); puts("\n");
        return;
    }
    char buf[65];
    ssize_u total = 0;
    for (;;) {
        ssize_u n = read(fd, buf, 64);
        if (n <= 0) break;
        buf[n] = 0;
        puts(buf);
        total += n;
    }
    close(fd);
    puts("\n--- "); print_dec((uint64_u)total); puts(" bayt okundu ---\n");
}

static void cmd_bellek(void) {
    puts("Userland bellek sinamasi basliyor...\n");
    char *a = (char *)malloc(64);
    char *b = (char *)malloc(128);
    if (!a || !b) { puts("HATA: malloc basarisiz\n"); return; }

    for (int i = 0; i < 64; i++)  a[i] = (char)('A' + (i % 26));
    for (int i = 0; i < 128; i++) b[i] = (char)('a' + (i % 26));

    int ok = 1;
    for (int i = 0; i < 64; i++)  if (a[i] != (char)('A' + (i % 26))) ok = 0;
    for (int i = 0; i < 128; i++) if (b[i] != (char)('a' + (i % 26))) ok = 0;

    puts("  64 ve 128 baytlik bloklar ayrildi, veri butunlugu: ");
    puts(ok ? "BASARILI\n" : "HATA\n");

    free(a);
    char *c = (char *)malloc(64);   /* free edilen blok yeniden kullanilmali */
    puts("  free sonrasi yeniden ayirma: ");
    puts((c == a) ? "AYNI BLOK (yeniden kullanim calisiyor)\n" : "farkli blok\n");
    free(b); free(c);
}

/* --- Asama 34: gercek bir GUI uygulamasi (userland'dan) --- */
static void cmd_pencere(void) {
    puts("ULGEN penceresi aciliyor (userland uygulamasi)...\n");
    int w = pencere_ac("OTUKEN UYGULAMASI", 120, 470, 380, 150);
    if (w < 0) { puts("HATA: pencere acilamadi\n"); return; }

    pencere_doldur(w, 0xF2EFE6);
    pencere_dikdortgen(w, 0, 0, 380, 26, 0x3E8B72);
    pencere_yazi(w, 8, 9, "USERLAND GUI UYGULAMASI", 0xFFFFFF);
    pencere_yazi(w, 10, 44, "Bu pencere RING3'ten cizildi.", 0x1A1A1A);
    pencere_yazi(w, 10, 60, "Uygulama ekrani HIC bilmiyor -", 0x1A1A1A);
    pencere_yazi(w, 10, 76, "yalnizca kendi tamponuna yazdi.", 0x1A1A1A);

    /* renk kutulari */
    uint32_u renkler[5] = {0xB33A3A, 0x3E8B72, 0x2E5B4F, 0x8A6D1F, 0x123B4A};
    for (int i = 0; i < 5; i++)
        pencere_dikdortgen(w, 12 + i * 46, 100, 40, 30, renkler[i]);

    puts("Pencere acildi (kimlik: "); print_dec((uint64_u)w); puts(")\n");
}

static void cmd_servisler(void) {
    servis_bilgi_u sv[8];
    int n = servis_listesi(sv, 8);
    if (n <= 0) { puts("Servis bilgisi alinamadi\n"); return; }
    const char *durumlar[5] = {"DURDU","BASLIYOR","CALISIYOR","COKTU","DEVRE_DISI"};
    puts("Sistem servisleri:\n");
    for (int i = 0; i < n; i++) {
        puts("  ");
        puts(sv[i].name);
        puts("  durum=");
        puts(sv[i].durum < 5 ? durumlar[sv[i].durum] : "?");
        puts("  nabiz=");
        print_dec(sv[i].nabiz);
        puts("\n");
    }
}

/* --- UMAY'a IPC uzerinden soru sor ---
 * Kabuk ve UMAY AYRI kullanici programlaridir; birbirlerini yalnizca
 * IPC port ADIYLA bulur ve mesajlasirlar. Ikisi de cekirdegin parcasi
 * degildir - gercek bir servis mimarisi budur. */
static void cmd_umay(const char *soru) {
    int hedef = ipc_bul("umay");
    if (hedef < 0) {
        puts("UMAY servisi bulunamadi (henuz baslamamis olabilir)\n");
        return;
    }
    int kendi = ipc_bul("kabuk");
    if (kendi < 0) kendi = ipc_ac("kabuk");

    puts("UMAY'a soruluyor: "); puts(soru); puts("\n");
    if (ipc_gonder(hedef, 1, soru, (uint32_u)strlen(soru)) != 0) {
        puts("HATA: istek gonderilemedi\n");
        return;
    }

    /* Yaniti bekle (en fazla ~3 saniye) */
    ipc_mesaj_u y;
    for (int bekle = 0; bekle < 150; bekle++) {
        if (kendi >= 0 && ipc_al(kendi, &y) == 1) {
            char yanit[128];
            uint32_u n = y.length < 127 ? y.length : 127;
            for (uint32_u i = 0; i < n; i++) yanit[i] = (char)y.payload[i];
            yanit[n] = 0;
            puts("UMAY yaniti: "); puts(yanit); puts("\n");
            return;
        }
        sleep_ticks(2);
    }
    puts("UMAY yanit vermedi (zaman asimi)\n");
}

static void cmd_ciz(void) {
    puts("Ekrana ciziliyor...\n");
    /* Renk gecisli bir dikdortgen - grafik syscall'ini kanitlar */
    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 200; x++) {
            uint32_u r = (uint32_u)(x * 255 / 200);
            uint32_u g = (uint32_u)(y * 255 / 60);
            putpixel(700 + x, 640 + y, (r << 16) | (g << 8) | 0x60);
        }
    }
    puts("Cizim tamamlandi (ekranin sag altina bakin).\n");
}

/* Basit sozcuk ayirici: satiri bosluklardan boler */
static int tokenize(char *line, char **argv, int max) {
    int argc = 0;
    int i = 0;
    while (line[i] && argc < max) {
        while (line[i] == ' ') line[i++] = 0;
        if (!line[i]) break;
        argv[argc++] = &line[i];
        while (line[i] && line[i] != ' ') i++;
    }
    return argc;
}

int main(void) {
    puts("\n");
    puts("========================================\n");
    puts(" OTUKEN Kabugu 2.0 - kullanici modunda\n");
    puts(" 'yardim' yazip Enter'a basin\n");
    puts("========================================\n");

    char line[MAX_LINE];
    char *argv[MAX_ARGS];

    for (;;) {
        puts("otuken> ");
        int len = readline(line, MAX_LINE);
        if (len <= 0) continue;

        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "yardim") == 0) {
            cmd_yardim();
        } else if (strcmp(argv[0], "listele") == 0) {
            cmd_listele(argc > 1 ? argv[1] : "/");
        } else if (strcmp(argv[0], "goster") == 0) {
            if (argc > 1) cmd_goster(argv[1]);
            else puts("kullanim: goster <dosya>\n");
        } else if (strcmp(argv[0], "kimlik") == 0) {
            puts("Surec kimligi (PID): "); print_dec((uint64_u)getpid()); puts("\n");
        } else if (strcmp(argv[0], "saat") == 0) {
            puts("Zamanlayici tik: "); print_dec((uint64_u)ticks()); puts("\n");
        } else if (strcmp(argv[0], "bellek") == 0) {
            cmd_bellek();
        } else if (strcmp(argv[0], "umay") == 0) {
            cmd_umay(argc > 1 ? argv[1] : "durum");
        } else if (strcmp(argv[0], "ciz") == 0) {
            cmd_ciz();
        } else if (strcmp(argv[0], "pencere") == 0) {
            cmd_pencere();
        } else if (strcmp(argv[0], "servisler") == 0) {
            cmd_servisler();
        } else if (strcmp(argv[0], "yankila") == 0) {
            for (int i = 1; i < argc; i++) { puts(argv[i]); puts(" "); }
            puts("\n");
        } else if (strcmp(argv[0], "cik") == 0) {
            puts("Kabuk sonlandiriliyor.\n");
            return 0;
        } else {
            puts("Bilinmeyen komut: "); puts(argv[0]);
            puts("  ('yardim' deneyin)\n");
        }
    }
}
