/* =====================================================================
 * UMAY — ana bilgisayar giriş noktası (komut satırı kabuğu)
 *
 * NEDEN AYRI DOSYA: çekirdek (core/) platform bağımsızdır; ÖTÜKEN'de
 * stdio yoktur. main() bir süre çekirdeğin içinde durdu ve orada
 * printf kullandı — yanlıştı. Giriş noktası artık burada; ÖTÜKEN'de
 * ise port/umay_port_otuken.c içindeki _start'tır. Çekirdek ikisini
 * de tanımaz, yalnız yaşam döngüsü çağrılarını bilir.
 *
 * İki kip:
 *   umay                konuşma kipi — uçbirimde sürekli
 *   umay "soru"         tek soru — yanıtlar ve çıkar (betikler için)
 * ===================================================================== */
#include <stdio.h>

#include "umay.h"
#include "umay_kabuk.h"
#include "umay_str.h"
#include "umay_port.h"

static void kullanim(const char *ad)
{
    printf("UMAY %s — Türkçe konuşan, çevrim dışı yardımcı\n\n", UMAY_SURUM);
    printf("Kullanım:\n");
    printf("  %s                konuşma kipi (uçbirim)\n", ad);
    printf("  %s \"soru\"         tek soru sorar, yanıtlar, çıkar\n", ad);
    printf("  %s --surum        sürümü yazar\n", ad);
    printf("  %s --yardim       bu yazı\n\n", ad);
    printf("Ortam değişkenleri:\n");
    printf("  UMAY_KUT          kütüphane kökü\n");
    printf("  UMAY_PIPER        piper çalıştırılabiliri\n");
    printf("  UMAY_PIPER_SES    .onnx ses modeli\n");
    printf("  UMAY_SES_OYNAT    ses çalıcı komutu\n\n");
    printf("Kütüphane sırayla aranır: UMAY_KUT, ./kut, kurulum yeri,\n");
    printf("~/.local/share/umay/kut, /usr/local/share/umay/kut\n");
}

int main(int argc, char **argv)
{
    const char *ad = (argc > 0 && argv[0]) ? argv[0] : "umay";

    if (argc > 1) {
        if (um_esit(argv[1], "--yardim") || um_esit(argv[1], "-y") ||
            um_esit(argv[1], "--help")   || um_esit(argv[1], "-h")) {
            kullanim(ad);
            return 0;
        }
        if (um_esit(argv[1], "--surum")   || um_esit(argv[1], "-s") ||
            um_esit(argv[1], "--version") || um_esit(argv[1], "-V")) {
            printf("UMAY %s\n", UMAY_SURUM);
            return 0;
        }
        if (argv[1][0] == '-') {
            fprintf(stderr, "UMAY: bilinmeyen seçenek: %s\n", argv[1]);
            fprintf(stderr, "  %s --yardim\n", ad);
            return 2;
        }

        /* --- tek soru kipi --- */
        char soru[UMAY_ILETI_MAX];
        soru[0] = 0;
        for (int i = 1; i < argc; i++) {
            if (i > 1) um_ekle(soru, sizeof soru, " ");
            um_ekle(soru, sizeof soru, argv[i]);
        }
        if (umay_kur() != 0) {
            fprintf(stderr, "UMAY: baslatilamadi\n");
            return 1;
        }
        umay_sor(soru);
        /* Ajanlar koşut çalışır; yanıt gelene dek dönülür. Üst sınır
         * var: bir ajan takılırsa program sonsuza dek beklemesin. */
        for (int t = 0; t < 600 && umay_mesgul(); t++) {
            umay_tur();
            up_uyu(16);
        }
        umay_tur();
        umay_kapan();
        return 0;
    }

    if (umay_kur() != 0) {
        fprintf(stderr, "UMAY: baslatilamadi\n");
        return 1;
    }
    while (umay_kosuyor()) {
        umay_tur();
        up_uyu(16);            /* ~60 tur/sn; boşta işlemci yakmaz */
    }
    umay_kapan();
    return 0;
}
