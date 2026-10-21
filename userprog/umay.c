/* ============================================================
 *  UMAY — ÖTÜKEN üzerinde çalışan AYRI bir kullanıcı programı
 * ============================================================
 *
 * MİMARİ: UMAY çekirdeğin parçası DEĞİLDİR. Diskten yüklenen bağımsız
 * bir ELF programıdır ve Ring3'te (kullanıcı modunda) çalışır.
 * Çekirdeğe yalnızca syscall'lar üzerinden erişir; diğer bileşenlerle
 * IPC ile konuşur. Bu sayede:
 *   - UMAY çökerse çekirdek etkilenmez (yalıtım)
 *   - Yetkileri TÖRE tarafından sınırlanır
 *   - Modeli güncellemek için çekirdeği yeniden derlemek GEREKMEZ,
 *     yalnızca diskteki dosya değiştirilir
 *
 * KAPSAM DÜRÜSTLÜĞÜ: Bu program gerçek bir dil modeli ÇALIŞTIRMAZ.
 * Modelin yerinde, çekirdek yeteneklerini kullanarak istekleri
 * yanıtlayan bir "karar motoru" iskeleti vardır. Gerçek model
 * hazır olduğunda değişecek tek yer istek_isle() fonksiyonudur;
 * çevresindeki her şey (yükleme, IPC, yetki, kaynak) hazırdır.
 */
#include "ulib.h"

#define UMAY_PORT_ADI "umay"
#define UMAY_SURUM    "1.0"

/* --- Basit yanit uretici: istegi cozumleyip cekirdek verisiyle
 * yanitlar. Gercek modelin yerini alan karar motoru. --- */
static int istek_isle(const char *istek, char *yanit, int azami) {
    int p = 0;
    #define EKLE(s) do { const char *_q=(s); while(*_q && p<azami-1) yanit[p++]=*_q++; } while(0)
    #define SAYI(v) do { \
        uint64_u _v=(v); char _t[24]; int _n=0; \
        if(!_v){ if(p<azami-1) yanit[p++]='0'; } \
        else { while(_v && _n<23){_t[_n++]=(char)('0'+(_v%10)); _v/=10;} \
               while(_n>0 && p<azami-1) yanit[p++]=_t[--_n]; } } while(0)

    /* Istegi anahtar sozcuklere gore cozumle */
    if (strncmp(istek, "bellek", 6) == 0) {
        EKLE("UMAY: bellek durumu -> bos cerceve ");
        SAYI(sistem_bil(SYSBIL_BOS_CERCEVE));
        EKLE(", toplam ");
        SAYI(sistem_bil(SYSBIL_TOPLAM_BELLEK) / (1024 * 1024));
        EKLE(" MB");
    } else if (strncmp(istek, "gorev", 5) == 0) {
        EKLE("UMAY: sistemde ");
        SAYI(sistem_bil(SYSBIL_GOREV_SAYISI));
        EKLE(" gorev calisiyor");
    } else if (strncmp(istek, "zaman", 5) == 0) {
        EKLE("UMAY: calisma suresi ");
        SAYI(sistem_bil(SYSBIL_TIK) / 50);
        EKLE(" saniye");
    } else if (strncmp(istek, "dosya", 5) == 0) {
        dirent_u d[8];
        int n = listdir("/", d, 8);
        EKLE("UMAY: kok dizinde ");
        SAYI(n < 0 ? 0 : (uint64_u)n);
        EKLE(" dosya");
        if (n > 0) { EKLE(", ilki: "); EKLE(d[0].name); }
    } else if (strncmp(istek, "durum", 5) == 0) {
        EKLE("UMAY " UMAY_SURUM " HAZIR | bellek ");
        SAYI(sistem_bil(SYSBIL_BOS_CERCEVE));
        EKLE(" cerceve | gorev ");
        SAYI(sistem_bil(SYSBIL_GOREV_SAYISI));
    } else {
        EKLE("UMAY: '");
        EKLE(istek);
        EKLE("' anlasilmadi. Bilinen: bellek, gorev, zaman, dosya, durum");
    }
    yanit[p] = 0;
    return p;
    #undef EKLE
    #undef SAYI
}

int main(void) {
    puts("\n[UMAY] kullanici alaninda baslatildi (surum " UMAY_SURUM ")\n");
    puts("[UMAY] surec kimligi: ");
    print_dec((uint64_u)getpid());
    puts("\n");

    /* --- 1) IPC portunu ac: diger bilesenler bize bu isimle ulasir --- */
    int port = ipc_ac(UMAY_PORT_ADI);
    if (port < 0) {
        puts("[UMAY] HATA: IPC portu acilamadi\n");
        return 1;
    }
    puts("[UMAY] IPC portu acildi: ");
    print_dec((uint64_u)port);
    puts("\n");

    /* --- 2) Cekirdek yeteneklerinin gercekten calistigini dogrula --- */
    puts("[UMAY] cekirdek yetenekleri sinaniyor:\n");
    puts("  bos cerceve : "); print_dec(sistem_bil(SYSBIL_BOS_CERCEVE)); puts("\n");
    puts("  gorev sayisi: "); print_dec(sistem_bil(SYSBIL_GOREV_SAYISI)); puts("\n");

    int fd = open("/OTUKEN.TXT");
    if (fd >= 0) {
        char t[48];
        int n = read(fd, t, 40);
        close(fd);
        if (n > 0) { t[n] = 0; puts("  dosya okuma : TAMAM ("); print_dec((uint64_u)n); puts(" bayt)\n"); }
    } else {
        puts("  dosya okuma : REDDEDILDI\n");
    }

    char *blok = (char *)malloc(2048);
    puts("  bellek      : ");
    puts(blok ? "TAMAM\n" : "REDDEDILDI\n");
    if (blok) free(blok);

    /* --- 3) Kendi kendine bir kac istek isle (yetenek gosterimi) --- */
    static const char *ornekler[] = {"durum", "bellek", "gorev", "zaman", "dosya"};
    char yanit[220];
    for (int i = 0; i < 5; i++) {
        istek_isle(ornekler[i], yanit, sizeof(yanit));
        puts("  > "); puts(ornekler[i]); puts("\n    ");
        puts(yanit); puts("\n");
    }

    /* --- 4) Hizmet dongusu: IPC uzerinden gelen istekleri yanitla --- */
    puts("[UMAY] hizmet dongusu basladi - IPC istekleri bekleniyor\n");
    ipc_mesaj_u m;
    int islenen = 0;
    for (;;) {
        if (ipc_al(port, &m) == 1) {
            char istek[100];
            uint32_u n = m.length < 99 ? m.length : 99;
            for (uint32_u i = 0; i < n; i++) istek[i] = (char)m.payload[i];
            istek[n] = 0;

            istek_isle(istek, yanit, sizeof(yanit));
            islenen++;

            puts("[UMAY] IPC istegi #"); print_dec((uint64_u)islenen);
            puts(": "); puts(istek); puts("\n    -> "); puts(yanit); puts("\n");

            /* Yaniti gonderene geri yolla */
            if (m.sender_port != 0) {
                ipc_gonder((int)m.sender_port, 1, yanit, (uint32_u)strlen(yanit));
            }
        }
        sleep_ticks(10);   /* CPU'yu bosuna tuketme */
    }
    return 0;
}
