#include "kararlilik.h"
#include "pmm.h"
#include "heap.h"
#include "sched.h"
#include "timer.h"
#include "log.h"
#include "serial.h"

static kararlilik_t k;

void kararlilik_baslat(void) {
    uint64_t t, kul, bos, blok;
    heap_get_stats(&t, &kul, &bos, &blok);

    k.ilk_bos_cerceve = pmm_free_frames();
    k.son_bos_cerceve = k.ilk_bos_cerceve;
    k.ilk_heap_bos    = bos;
    k.son_heap_bos    = bos;
    k.ornek_sayisi    = 0;
    k.sizinti_supheli = 0;
    log_write(LOG_BILGI, "KARARLILIK", "izleme baslatildi");
}

void kararlilik_ornekle(void) {
    uint64_t t, kul, bos, blok;
    heap_get_stats(&t, &kul, &bos, &blok);

    k.son_bos_cerceve = pmm_free_frames();
    k.son_heap_bos    = bos;
    k.gorev_sayisi    = scheduler_task_count();
    k.calisma_tik     = timer_get_ticks();
    k.ornek_sayisi++;

    /* Sizinti supheci: kaynaklar TEK YONLU olarak azaliyorsa isaretle.
     * Kucuk dalgalanmalar normaldir; esik olarak baslangicin %10'unu
     * kalici kayip sayiyoruz. */
    if (k.ornek_sayisi > 3) {
        uint64_t esik = k.ilk_bos_cerceve / 10;
        if (k.ilk_bos_cerceve > k.son_bos_cerceve &&
            (k.ilk_bos_cerceve - k.son_bos_cerceve) > esik) {
            if (!k.sizinti_supheli) {
                k.sizinti_supheli = 1;
                log_write(LOG_UYARI, "KARARLILIK",
                          "fiziksel bellekte kalici azalma - sizinti suphesi");
            }
        }
    }
}

const kararlilik_t *kararlilik_al(void) { return &k; }

void kararlilik_rapor(void) {
    serial_write("KARARLILIK: ornek=");
    serial_write_hex(k.ornek_sayisi);
    serial_write(" tik=");
    serial_write_hex((uint32_t)k.calisma_tik);
    serial_write(" gorev=");
    serial_write_hex(k.gorev_sayisi);
    serial_write("\n");
    serial_write("KARARLILIK: bos cerceve ilk=");
    serial_write_hex((uint32_t)k.ilk_bos_cerceve);
    serial_write(" son=");
    serial_write_hex((uint32_t)k.son_bos_cerceve);
    serial_write(" | heap bos ilk=");
    serial_write_hex((uint32_t)k.ilk_heap_bos);
    serial_write(" son=");
    serial_write_hex((uint32_t)k.son_heap_bos);
    serial_write("\n");
    serial_write("KARARLILIK: sizinti suphesi -> ");
    serial_write(k.sizinti_supheli ? "VAR\n" : "YOK\n");
}

int kararlilik_baski_sinamasi(int tur_sayisi) {
    int hata = 0;
    uint64_t once_cerceve = pmm_free_frames();
    uint64_t t, kul, once_heap, blok;
    heap_get_stats(&t, &kul, &once_heap, &blok);

    for (int tur = 0; tur < tur_sayisi; tur++) {
        /* 1) Degisken boyutlu heap ayirma + veri butunlugu */
        void *p[8];
        for (int i = 0; i < 8; i++) {
            uint32_t boyut = 32 + (uint32_t)((tur * 13 + i * 29) % 400);
            p[i] = kmalloc(boyut);
            if (!p[i]) { hata++; continue; }
            uint8_t *b = (uint8_t *)p[i];
            uint8_t imza = (uint8_t)(0xA0 + i);
            for (uint32_t j = 0; j < boyut; j++) b[j] = imza;
            for (uint32_t j = 0; j < boyut; j++)
                if (b[j] != imza) { hata++; break; }
        }
        for (int i = 0; i < 8; i++) if (p[i]) kfree(p[i]);

        /* 2) Fiziksel cerceve ayirma/iade */
        uint64_t f[4];
        for (int i = 0; i < 4; i++) {
            f[i] = pmm_alloc_frame();
            if (f[i] == 0) hata++;
        }
        for (int i = 0; i < 4; i++) if (f[i]) pmm_free_frame(f[i]);
    }

    /* 3) Kaynak dengesi: baski sonrasi baslangica DONMELI */
    uint64_t sonra_cerceve = pmm_free_frames();
    uint64_t sonra_heap;
    heap_get_stats(&t, &kul, &sonra_heap, &blok);

    serial_write("BASKI: ");
    serial_write_hex((uint32_t)tur_sayisi);
    serial_write(" tur | cerceve once=");
    serial_write_hex((uint32_t)once_cerceve);
    serial_write(" sonra=");
    serial_write_hex((uint32_t)sonra_cerceve);
    serial_write(" | hata=");
    serial_write_hex((uint32_t)hata);
    serial_write("\n");

    if (sonra_cerceve != once_cerceve) {
        serial_write("BASKI: UYARI - cerceve dengesi bozuk (sizinti)\n");
        hata++;
    }
    return hata;
}
