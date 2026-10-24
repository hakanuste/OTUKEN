/* =====================================================================
 * 1.4.9 — SSE-TEST (yalnız TANI): x87/SSE durumu görev değişiminde korunur.
 * Diskteki SSESINA.ELF'ten üç süreç aynı anda başlatılır; her biri
 * XMM0-15 ve MXCSR'ye kendine özgü değerler yükleyip bekler ve geri
 * okur (userprog/ssesina.c). Sonuç satırlarını süreçler kendisi yazar:
 *   "SSE-SINA: pid=.. tur=40 ... hata=0 -> BASARILI"  (üç kez)
 * Kontrol grubu: -DFPU_KAYIT_KAPALI ile derlenen çekirdekte (görev
 * değişiminde saklama yok) aynı sınama hata BULMALIDIR.
 * ===================================================================== */
#include "sinama_sse.h"
#include "sinama_uyg.h"
#include "uygulama.h"
#include "process.h"
#include "thread.h"
#include "timer.h"
#include "tore.h"
#include "serial.h"
#include "fpu.h"

static void giris(void) { uygulama_elf_calistir("/SSESINA.ELF", TORE_PROFILE_PAKET_UYG); }

static void gorev(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    for (uint64_t t0 = timer_get_ticks(); !sinama_uyg_bitti() && timer_get_ticks() - t0 < 600 * (uint64_t)hz; ) thread_sleep(1);
    if (!fpu_etkin()) { serial_write("SSE-TEST: islemcide FXSR/SSE2 yok -> atlandi\n"); thread_exit(); }
    int acilan = 0;
    for (int i = 0; i < 3; i++) if (process_create("SseSina", giris)) acilan++;
    serial_write(acilan == 3 ? "SSE-TEST: 3 surec baslatildi\n" : "SSE-TEST: surec baslatilamadi -> HATA\n");
    thread_exit();
}

void sinama_sse_baslat(void) { thread_create("SseSinama", gorev); }
