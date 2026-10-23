#ifndef OTUKEN_CONF_H
#define OTUKEN_CONF_H

/* --- ÖTÜKEN yapilandirmasi ---
 * Denetim bulgusu: cekirdek her acilista onlarca oz-sinama calistirip
 * ciktiyi kirletiyordu. Gercek bir sistem sessiz acilir; sinamalar
 * istege bagli bir TANI MODU'nda calismalidir. */

/* 1 yapilirsa acilista tum oz-sinamalar calisir ve ayrintili rapor
 * verir. 0 (varsayilan) uretim modudur: sadece anlamli olaylar. */
#ifndef OTUKEN_TANI_MODU
#define OTUKEN_TANI_MODU 0
#endif

/* Gosterim amacli arka plan gorevleri (cok gorevliligi gosterir).
 * Bunlar artik her donguden sonra UYUR - eskiden bos donguyle CPU'yu
 * %100 mesgul ediyorlardi ki bu gercek bir sistemde kabul edilemez. */
#ifndef OTUKEN_DEMO_GOREVLER
#define OTUKEN_DEMO_GOREVLER 1
#endif

/* SMP IS DAGITIMI (adim 5 altyapi, adim 6 kilitler): AP'ler zamanlayiciya
 * katilir ve gorev calistirir. Alt sistem kilitleri (depolama, VMM,
 * gunluk, TORE, surec, servis, KUT, ULGEN/GUI, ses, klavye, PIT2) ile
 * VARSAYILAN ACIK. SMP-YARIS sinamasi -smp 4'te 4 cekirdekte 0 hata;
 * kilit kapatilinca (kontrol grubu) hata bulur. Kapatmak icin:
 * -DOTUKEN_SMP_DAGITIM=0 */
#ifndef OTUKEN_SMP_DAGITIM
#define OTUKEN_SMP_DAGITIM 1
#endif

/* Demo gorevlerin is yapmadan once uyuyacagi tik sayisi (50Hz). */
#define OTUKEN_DEMO_UYKU 25

/* AGIR SINAMA (stres/soak). Yalniz TANI kipiyle birlikte anlamli; cok
 * gorevli uzun sureli yuk altinda cekirdegi zorlar ve bellek sizintisini
 * (PMM cerceve + heap bayt, tur basina) olcer. Varsayilan KAPALI: normal
 * tani kosusunu yavaslatmasin. Acmak icin: -DOTUKEN_AGIR_SINAMA=1
 * Tur sayisi (her tur = bir dalga eszamanli gorev): -DOTUKEN_AGIR_TUR=N */
#ifndef OTUKEN_AGIR_SINAMA
#define OTUKEN_AGIR_SINAMA 0
#endif
#ifndef OTUKEN_AGIR_TUR
#define OTUKEN_AGIR_TUR 8
#endif

/* UMAY UYUM sinamasi (yalniz TANI kipiyle): cekirdekte UYUM_ISTEMCI
 * istemci gorev, her biri KENDI portundan Ring3 UMAY'a eszamanli soru
 * sorar, tam yaniti (son parca opkod 0) bekler. Olculen: yanit sayisi,
 * gecikme, parca, zaman asimi, YANLIS istemciye dusen yanit. AGIR
 * sinama aciksa onunla AYNI ANDA kosar (yuk altinda uyum).
 * Acmak icin: -DOTUKEN_UMAY_UYUM=1  Soru sayisi: -DOTUKEN_UYUM_SORU=N */
#ifndef OTUKEN_UMAY_UYUM
#define OTUKEN_UMAY_UYUM 0
#endif
#ifndef OTUKEN_UYUM_SORU
#define OTUKEN_UYUM_SORU 12
#endif

#endif
