#ifndef OTUKEN_CONF_H
#define OTUKEN_CONF_H

/* --- ÖTÜKEN yapilandirmasi ---
 * Denetim bulgusu: cekirdek her acilista onlarca oz-sinama calistirip
 * ciktiyi kirletiyordu. Gercek bir sistem sessiz acilir; sinamalar
 * istege bagli bir TANI MODU'nda calismalidir. */

/* 1 yapilirsa acilista tum oz-sinamalar calisir ve ayrintili rapor
 * verir. 0 (varsayilan) uretim modudur: sadece anlamli olaylar. */
/* =====================================================================
 * UMAY BAGLANTISI
 * UMAY, OTUKEN'in bir parcasi DEGILDIR: diskten yuklenen ayri bir Ring3
 * uygulamasidir. Bu anahtar, acilista otomatik baslatilip
 * baslatilmayacagini belirler.
 *   0 (varsayilan) : OTUKEN tek basina acilir, UMAY yuklenmez.
 *   1              : UMAY.ELF diskten okunup Ring3'te baslatilir.
 * Kapaliyken cekirdegin UMAY DESTEK katmani (src/umay.c: sozlesme
 * denetimi, yetki profili, IPC portu) yerinde kalir; yalnizca kullanici
 * uygulamasi baslatilmaz. Boylece baglanti tek kelimeyle geri gelir.
 * Tani kipi bu anahtari ACIK kosar: butunlesme sinanmaya devam eder.
 * ===================================================================== */
#ifndef OTUKEN_UMAY_BAGLI
#define OTUKEN_UMAY_BAGLI 0
#endif

#ifndef OTUKEN_TANI_MODU
#define OTUKEN_TANI_MODU 0
#endif

/* Gosterim amacli arka plan gorevleri (cok gorevliligi gosterir).
 * Bunlar artik her donguden sonra UYUR - eskiden bos donguyle CPU'yu
 * %100 mesgul ediyorlardi ki bu gercek bir sistemde kabul edilemez. */
#ifndef OTUKEN_DEMO_GOREVLER
#define OTUKEN_DEMO_GOREVLER 1
#endif

/* Demo gorevlerin is yapmadan once uyuyacagi tik sayisi (50Hz). */
#define OTUKEN_DEMO_UYKU 25

#endif
