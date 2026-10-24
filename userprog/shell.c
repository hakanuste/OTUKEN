/* ÖTÜKEN Kabuğu (Shell 2.0)
 * Ring3'te, kullanici modunda calisan gercek bir komut yorumlayicisi.
 * Cekirdege SADECE syscall'lar uzerinden erisir. */
#include "ulib.h"

#define MAX_LINE 128
#define MAX_ARGS 8

/* Satir tamponu: her cikti satiri TEK puts (tek SYS_WRITE) ile yazilir.
 * Parca parca yazilan satirlar, seri portta diger gorevlerin ciktisiyla
 * bolunuyordu ("Surec kimligi (PID): [ThreadE] ..."). */
typedef struct { char b[192]; int n; } satir_u;
static void s_bas(satir_u *s) { s->n = 0; s->b[0] = 0; }
static void s_ekle(satir_u *s, const char *t) {
    while (*t && s->n < (int)sizeof(s->b) - 1) s->b[s->n++] = *t++;
    s->b[s->n] = 0;
}
static void s_sayi(satir_u *s, uint64_u v) {
    char t[24]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 23);
    while (i-- > 0 && s->n < (int)sizeof(s->b) - 1) s->b[s->n++] = t[i];
    s->b[s->n] = 0;
}
static void s_yaz(satir_u *s) { puts(s->b); }

static void cmd_yardim(void) {
    puts("OTUKEN Kabugu - kullanilabilir komutlar:\n");
    puts("  yardim         bu listeyi goster\n");
    puts("  listele [yol]  dizin icerigini listele (varsayilan /)\n");
    puts("  goster <dosya> dosya icerigini yazdir\n");
    puts("  kimlik         surec kimligini (PID) goster\n");
    puts("  saat           zamanlayici tik sayisini goster\n");
    puts("  bellek         userland bellek ayirma sinamasi\n");
    puts("  ciz            ekrana renkli bir sekil ciz\n");
    puts("  umay <soru>    UMAY'a IPC ile soru sor (ornek: umay durum)\n");
    puts("  pencere        ULGEN penceresi acan GUI uygulamasi\n");
    puts("  servisler      calisan sistem servislerini listele\n");
    puts("  yankila <söz>  verilen sozu geri yazdir\n");
    puts("  cik            kabugu sonlandir\n");
}

static void cmd_listele(const char *path) {
    dirent_u entries[16];
    int n = listdir(path, entries, 16);
    satir_u s;
    if (n < 0) {
        s_bas(&s); s_ekle(&s, "HATA: dizin okunamadi: "); s_ekle(&s, path); s_ekle(&s, "\n"); s_yaz(&s);
        return;
    }
    s_bas(&s); s_ekle(&s, "Dizin: "); s_ekle(&s, path); s_ekle(&s, "\n"); s_yaz(&s);
    for (int i = 0; i < n; i++) {
        s_bas(&s);
        s_ekle(&s, "  "); s_ekle(&s, entries[i].name); s_ekle(&s, "  ");
        s_sayi(&s, entries[i].size); s_ekle(&s, " bayt");
        if (entries[i].is_dir) s_ekle(&s, " [dizin]");
        s_ekle(&s, "\n"); s_yaz(&s);
    }
    s_bas(&s); s_ekle(&s, "Toplam: "); s_sayi(&s, (uint64_u)n); s_ekle(&s, " girdi\n"); s_yaz(&s);
}

static void cmd_goster(const char *path) {
    int fd = open(path);
    if (fd < 0) {
        satir_u e; s_bas(&e); s_ekle(&e, "HATA: dosya acilamadi: "); s_ekle(&e, path); s_ekle(&e, "\n"); s_yaz(&e);
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
    satir_u s; s_bas(&s); s_ekle(&s, "\n--- "); s_sayi(&s, (uint64_u)total); s_ekle(&s, " bayt okundu ---\n"); s_yaz(&s);
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

    satir_u s; s_bas(&s);
    s_ekle(&s, "  64 ve 128 baytlik bloklar ayrildi, veri butunlugu: ");
    s_ekle(&s, ok ? "BASARILI\n" : "HATA\n"); s_yaz(&s);

    free(a);
    char *c = (char *)malloc(64);   /* free edilen blok yeniden kullanilmali */
    s_bas(&s); s_ekle(&s, "  free sonrasi yeniden ayirma: ");
    s_ekle(&s, (c == a) ? "AYNI BLOK (yeniden kullanim calisiyor)\n" : "farkli blok\n"); s_yaz(&s);
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

    satir_u s; s_bas(&s); s_ekle(&s, "Pencere acildi (kimlik: "); s_sayi(&s, (uint64_u)w); s_ekle(&s, ")\n"); s_yaz(&s);
}

static void cmd_servisler(void) {
    servis_bilgi_u sv[8];
    int n = servis_listesi(sv, 8);
    if (n <= 0) { puts("Servis bilgisi alinamadi\n"); return; }
    const char *durumlar[5] = {"DURDU","BASLIYOR","CALISIYOR","COKTU","DEVRE_DISI"};
    puts("Sistem servisleri:\n");
    for (int i = 0; i < n; i++) {
        satir_u s; s_bas(&s);
        s_ekle(&s, "  "); s_ekle(&s, sv[i].name);
        s_ekle(&s, "  durum="); s_ekle(&s, sv[i].durum < 5 ? durumlar[sv[i].durum] : "?");
        s_ekle(&s, "  nabiz="); s_sayi(&s, sv[i].nabiz); s_ekle(&s, "\n"); s_yaz(&s);
    }
}

/* --- UMAY'a IPC uzerinden soru sor ---
 * Kabuk ve UMAY AYRI kullanici programlaridir; birbirlerini yalnizca
 * IPC port ADIYLA bulur ve mesajlasirlar. Ikisi de cekirdegin parcasi
 * degildir - gercek bir servis mimarisi budur. */
/* Klavye Turkce harfleri tek baytlik ISO-8859-9 olarak verir (ı=0xFD);
 * UMAY UTF-8 bekler. Soru yollanmadan once UTF-8'e cevrilir: yoksa
 * "altı" UMAY'a "altý" olarak gider ve kutuphanede eslesmez. */
static void latin5_utf8(const char *g, char *c, int azami) {
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)g; *p && o < azami - 3; p++) {
        unsigned int k = *p;
        if (k < 0x80) { c[o++] = (char)k; continue; }
        switch (k) {                          /* Latin-5'in Latin-1'den farkli harfleri */
        case 0xD0: k = 0x011E; break;  case 0xDD: k = 0x0130; break;
        case 0xDE: k = 0x015E; break;  case 0xF0: k = 0x011F; break;
        case 0xFD: k = 0x0131; break;  case 0xFE: k = 0x015F; break;
        default: break;
        }
        c[o++] = (char)(0xC0 | (k >> 6));
        c[o++] = (char)(0x80 | (k & 0x3F));
    }
    c[o] = 0;
}

static void cmd_umay(const char *ham_soru) {
    char soru[MAX_LINE * 2];
    latin5_utf8(ham_soru, soru, (int)sizeof soru);
    int hedef = ipc_bul("umay");
    if (hedef < 0) {
        puts("UMAY servisi bulunamadi (henuz baslamamis olabilir)\n");
        return;
    }
    int kendi = ipc_bul("kabuk");
    if (kendi < 0) kendi = ipc_ac("kabuk");

    satir_u s; s_bas(&s); s_ekle(&s, "UMAY'a soruluyor: "); s_ekle(&s, soru); s_ekle(&s, "\n"); s_yaz(&s);
    if (ipc_gonder(hedef, 1, soru, (uint32_u)strlen(soru)) != 0) {
        puts("HATA: istek gonderilemedi\n");
        return;
    }

    /* Yaniti bekle (en fazla ~20 saniye). UMAY uzun yaniti 96 baytlik
     * PARCALARA boler: opkod 1 = devami var, 0 = son parca. Hepsi
     * okunmazsa artan parcalar kuyrukta kalir ve SONRAKI soruya
     * karisir; bu yuzden son parcaya kadar okunur. */
    ipc_mesaj_u y;
    char yanit[160];
    uint32_u dolu = 0;
    int parca = 0, basildi = 0;
    /* UYUM: UMAY'in cevabi cikarim motorundan gecer; yuk altinda (baska
     * istemciler + agir is) QEMU'da 9 sn'yi buldu. Eski sinir 150 x 2 tik
     * = 6 sn idi -> 'zaman asimi' deyip yaniti kuyrukta birakiyordu (bir
     * SONRAKI soruya karisirdi). Sinir 20 sn; parca gelince yenilenir. */
    /* 1.5: arastirma agda da arar (ARAYICI); QEMU'da (TCG) TLS ile birkac
     * sayfa dakikalar surebilir. Sinir ~6 dk; parca gelince yenilenir. */
    for (int bekle = 0; bekle < 18000; bekle++) {
        if (kendi >= 0 && ipc_al(kendi, &y) == 1) {
            /* 1.5: uzun yanit (arastirma raporu, kaynaklariyla) 160 baytta
             * KESILIYORDU. Tampon dolunca basilir, okumaya devam edilir. */
            for (uint32_u i = 0; i < y.length; i++) {
                if (dolu >= sizeof(yanit) - 1) {
                    yanit[dolu] = 0;
                    s_bas(&s); if (!basildi) s_ekle(&s, "UMAY yaniti: "); s_ekle(&s, yanit); s_yaz(&s);
                    basildi = 1; dolu = 0;
                }
                yanit[dolu++] = (char)y.payload[i];
            }
            parca++;
            if (y.opcode == 0) {
                yanit[dolu] = 0;
                s_bas(&s); if (!basildi) s_ekle(&s, "UMAY yaniti: "); s_ekle(&s, yanit); s_ekle(&s, "\n"); s_yaz(&s);
                return;
            }
            bekle = 0;          /* devami geliyor: sureyi yenile */
            continue;
        }
        sleep_ticks(2);
    }
    if (parca > 0) {
        yanit[dolu] = 0;
        s_bas(&s); s_ekle(&s, basildi ? " (eksik) " : "UMAY yaniti (eksik): "); s_ekle(&s, yanit); s_ekle(&s, "\n"); s_yaz(&s);
        return;
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
            satir_u s; s_bas(&s); s_ekle(&s, "Surec kimligi (PID): "); s_sayi(&s, (uint64_u)getpid()); s_ekle(&s, "\n"); s_yaz(&s);
        } else if (strcmp(argv[0], "saat") == 0) {
            satir_u s; s_bas(&s); s_ekle(&s, "Zamanlayici tik: "); s_sayi(&s, (uint64_u)ticks()); s_ekle(&s, "\n"); s_yaz(&s);
        } else if (strcmp(argv[0], "bellek") == 0) {
            cmd_bellek();
        } else if (strcmp(argv[0], "umay") == 0) {
            /* 1.4.8: sorunun TAMAMI yollanir (eskiden yalniz ilk sozcuk) */
            char birlesik[MAX_LINE];
            int bo = 0;
            birlesik[0] = 0;
            for (int ai = 1; ai < argc; ai++) {
                for (const char *q = argv[ai]; *q && bo < MAX_LINE - 2; q++) birlesik[bo++] = *q;
                if (ai + 1 < argc && bo < MAX_LINE - 2) birlesik[bo++] = ' ';
            }
            birlesik[bo] = 0;
            cmd_umay(argc > 1 ? birlesik : "durum");
        } else if (strcmp(argv[0], "ciz") == 0) {
            cmd_ciz();
        } else if (strcmp(argv[0], "pencere") == 0) {
            cmd_pencere();
        } else if (strcmp(argv[0], "servisler") == 0) {
            cmd_servisler();
        } else if (strcmp(argv[0], "yankila") == 0) {
            satir_u s; s_bas(&s);
            for (int i = 1; i < argc; i++) { s_ekle(&s, argv[i]); s_ekle(&s, " "); }
            s_ekle(&s, "\n"); s_yaz(&s);
        } else if (strcmp(argv[0], "cik") == 0) {
            puts("Kabuk sonlandiriliyor.\n");
            return 0;
        } else {
            satir_u s; s_bas(&s); s_ekle(&s, "Bilinmeyen komut: "); s_ekle(&s, argv[0]);
            s_ekle(&s, "  ('yardim' deneyin)\n"); s_yaz(&s);
        }
    }
}
