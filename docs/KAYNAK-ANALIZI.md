# ÖTÜKEN 1.4 — Kaynak Ağacı Analizi

> Tarih: 2026-09-27 · Taban: git `a24eb2d` (ÖTÜKEN 1.4 + UMAY) · 253 izlenen dosya
> Ek: `docs/kaynak-envanteri.txt` — her dosya için sınıf, kategori, boyut ve makine kanıtı.

**Yöntem.** Hiçbir karar dosya adına, boyutuna ya da "eski görünmesine" dayanmaz. Her dosya için şunlar makineyle çıkarıldı: kök, `umay/` ve `arm64/` Makefile referansları, `#include` eden dosya sayısı, test/kurulum betiklerindeki adı, disk imajı içeriği (`mdir`) ve belgelerdeki adı. Makine referans bulamadığında dosya elle doğrulandı (desen kuralları, Python `import`, `.S` hedefleri). Ölü kod için **bağlayıcının kendisi** kullanıldı: `-ffunction-sections -fdata-sections` ile derleyip `--gc-sections --print-gc-sections -e _start` ile bağlamak, hiçbir giriş noktasından referans zinciriyle ulaşılamayan her işlevi ve veriyi listeler. IDT, syscall tablosu ve işlev işaretçileri de referans sayılır. Analiz hem **üretim** hem **tanı** kipinde yapıldı.

---

## [1] GENEL DURUM

- **253 izlenen dosya** (temizlikten sonra 251: 4 çıktı, 2 rapor dosyası eklendi). Sınıf dağılımı (ek tablo): A 198 · A-eğitim 3 · A-araç 1 · A/C 4 · B 25 · C 16 · C/B 1 · D 1 · D\* 2 · Doküman 2.
- **Build zinciri sağlam.** Çekirdek, Ring3 kabuğu, UMAY, US1, disk imajı ve ISO temiz ağaçtan sıfır uyarıyla üretiliyor; kabuk, UMAY ve yazı tipi başlığı kaynaktan **bayt bayt aynı** çıkıyor.
- **Gerçekten gereksiz dosya: 1** (`arm64/src/kernel_arm64.c`). Ayrıca üç derleme çıktısı yanlışlıkla git'te izleniyordu (D\*).
- **En büyük bulgu dosya değil, ölü kod:** üretim çekirdeğinin 2 871 825 baytlık kod+salt-okunur bölümünün **~2,66 MB'ı (%93)** çalışma anında hiçbir yoldan ulaşılamayan UMAY tema ve WAV kodu/verisi. Bağlayıcı ayıklamasıyla çekirdek **2 933 048 → 226 912 bayta** iniyor ve bu ayıklanmış çekirdek `-smp 4`'te her şeyiyle açılıyor (kabuk, UMAY, US1, iş dağıtımı; panik 0, [HATA]/[UYARI] 0).
- **İşlevsel bulgu:** `ses_init` hiçbir yerden çağrılmıyor → AC'97 sürücüsü hiç başlatılmıyor → `dede_konus`, `ozan_soyle`, `ses_ton`, `ses_cal` her çağrıda hemen −1 dönüyor. Çekirdekteki ses yolu çalışma anında **etkisiz**. (Temizlik konusu değil; [14]'te.)

## [2] KESİNLİKLE GEREKLİ DOSYALAR (A)

Kanıt: bağlanan ikiliye giriyor (Makefile `SRC_C`/`BOOT_ASM`, `#include`), ya da çalışma anında okunuyor.

| Katman | Dosyalar | Kanıt |
|---|---|---|
| Boot | `boot/*.asm` (8), `linker.ld`, `grub.cfg` | Makefile `BOOT_ASM`; ISO |
| Kernel çekirdeği | PMM/VMM/heap, sched/process/thread, spinlock/mutex, idt/pic/timer, serial, driver/pci, kararlilik, servis, kervan, multiboot2, log | Makefile `SRC_C`, `--gc-sections` canlı |
| Syscall / IPC | `syscall.c`, `ipc2`, `msgqueue`, `pipe`, `shm` | canlı; Ring3 ↔ çekirdek yolu |
| Ring3 | `userprog/{crt0.c,ulib.c,ulib.h,shell.c,shell.ld}`, `ring3.c`, `elf.c`, `src/shell_elf_blob.c` | kök Makefile `userprog/%.uo` desen kuralı; kabuk bayt bayt yeniden üretilir |
| VFS/BİTİG | vfs, vfs_fat16/32, fat16, fat16_dizin, fat32, disk, disk_cache, gpt, depo_kilit | canlı; UMAY dosyaları buradan okunur |
| Sürücü | ahci, nvme, usb, uhci, keyboard, mouse | canlı |
| ULAK | ulak, tcp, socket, dhcp, dns, aghizmet | canlı (ana döngü) |
| TÖRE/KUT/TİMUR | tore, kut, timur | canlı; UMAY yetki profili |
| SMP/APIC/ACPI | smp, cpu, gecikme, `boot/ap_trampoline.asm` | canlı; 4 çekirdekte iş dağıtımı |
| ÜLGEN | ulgen, gui, fb, font8x8.h, gorsel_kilit | canlı |
| SES (çekirdek) | ses, dede, ozan_ses, ses_kilit | syscall'lardan bağlı (ama bkz. [1]: `ses_init` yok) |
| UMAY (Ring3) | `umay/` altındaki 36 üretim dosyası (21 `.c`, 13 başlık, `Makefile`, `umay.ld`) | `umay/Makefile` `KAYNAK`; `--gc-sections`'da 21 `.c`'nin hiçbiri ölü değil |
| US1 | `us1/c/us1.{c,h}`, `us1/umay.us1`, `us1/sozluk.txt` | UMAY `$(US1_DIZIN)/us1.c`; model ve sözlük disk imajında |
| Disk | `diskfiles.img` | **çalışma zamanı**: UMAY.ELF, UMAY.US1, SOZLUK.TXT, 3 belge, 2 metin |
| Build | `Makefile`, `kur.sh`, `elf_blob_yap.py`, `uefi_iso_yap.sh`, `.gitignore`, `gen_font.py` | `gen_font.py` → `include/font8x8.h` **bayt bayt aynı** |
| ARM64 | `arm64/{Makefile,linker.ld,boot/boot.S,src/uart.c,src/cekirdek.c}` | `arm64/Makefile` `OBJ` |

## [3] TEST İÇİN GEREKLİ DOSYALAR (B)

| Dosya | Kanıt |
|---|---|
| `testler.sh`, `donanim_testleri.sh`, `kabuk_testi.py`, `asama_dogrula.py` | test takımları; `asama_dogrula.py` `testler.sh`'tan çağrılır |
| `buyukdisk_hazirla.sh`, `gpt_disk_hazirla.sh` | `donanim_testleri.sh` 8 TB ve GPT imajlarını üretir |
| `src/iskence.c`, `include/iskence.h` | yalnız **tanı** kipinde (`kernel.c`: `if (OTUKEN_TANI_MODU) … iskence_tumu()`); üretimde 11/11 işlev ölü, tanıda canlı; `testler.sh` 12 kontrol |
| `src/kervan_ornek.c` | `ornek_paket`: `kernel.c` KERVAN sınaması (üretimde de); `testler.sh` "paket kuruldu" |
| `src/user_elf_blob.c`, `userprog/hello.c`, `userprog/user.ld` | `ring3.c` `embedded_user_elf`; **Ring3ELF görevi üretim açılışında** ELF yükleyici + Ring3 kanıtı (`SYS_RING3_PROOF`). `hello.c` gerçekten kaynak: `.text` (34 B) ve `.rodata` (57 B) gömülü ikiliyle **bayt bayt aynı**; tek fark yeni bağlayıcının eklediği boş bir LOAD segmenti |
| `src/umay.c`, `include/umay.h` | **eski çekirdek-içi UMAY barındırıcısı** (bkz. [8]); `UMAY-TEST` sınamaları kullanır |
| `umay/sina/sina.c`, `umay/port/umay_port_host.c` | UMAY ana makine takımı (202 kontrol) |
| `us1/sinama/{us1_sina.c,referans_imler.txt,referans_mantik.txt}` | US1 motoru numpy referansına karşı (10 kontrol); referanslar `referans.py` üretir |
| `kanitlar/*` (6) | son test koşularının kanıtları |

## [4] ASSET / TEST FIXTURE DOSYALARI (C)

| Dosya | Boyut | Kanıt / durum |
|---|---|---|
| `src/duvar.c` | 7 960 411 B kaynak, **2 075 047 B** kod/veri | gömülü duvar kâğıdı; yalnız `duvar_ciz` → yalnız `ulgen_theme.c` → hiç çağrılmıyor |
| `src/tamga_gorsel.c` | 1 937 340 B kaynak, **496 692 B** | gömülü tamga görselleri; aynı zincir |
| `src/simge.c` | 140 017 B kaynak, 23 402 B | simge bitmapleri; yalnız `ulgen_theme.c` |
| `src/orbitron.c` | 66 522 B kaynak, 28 632 B | Orbitron yazı tipi (SIL OFL); 7/7 işlev ölü |
| `src/ulgen_theme.c` | 3 353 B + 16 KB bss | masaüstü/rıhtım/ızgara; **9/9 işlev ölü** (`ulgen_theme_draw` hiç çağrılmıyor) |
| `src/ezgi.c` | 1 396 B + **532 KB bss** | WAV çalar; 4/4 işlev ölü |
| `src/tamga.c`, `src/tema.c` (A/C) | 30 636 B + 132 KB bss | `SYS_TEMA` → `tema_uygula` → `tamga_onbellek_bosalt` canlı; kalan 10/11 + 4/5 işlev ölü |
| `fat32test.img` | 64 MB (sıkışınca küçük) | `testler.sh` ikinci disk; tek dosya `FAT32F.TXT` |
| `diskfiles/{OTUKEN.TXT,NOTLAR.TXT}` | 513 B | `diskfiles.img` içindekilerle **bayt bayt aynı**; imajın metin kaynağı (onu üreten betik yok) |
| `belge/*.wav` | 669 KB | Dede Korkut sentezi örnekleri; **kod referansı yok**, disk imajında değil. QEMU'da ses duyulmadığından sentezin tek duyulabilir kanıtı |

Tema dosyaları (ilk yedi satır) **silinmedi**: UMAY'ın GUI tasarımının parçası, derlenip bağlanıyorlar; eksik olan yalnızca `ulgen_theme_draw`'un çağrılması.

## [5] GERÇEKTEN GEREKSİZ DOSYALAR (D)

| Dosya | Kanıt (hepsi sağlanıyor) |
|---|---|
| `arm64/src/kernel_arm64.c` | (1) `arm64/Makefile` `OBJ = boot/boot.o src/uart.o src/cekirdek.o` — yok. (2) Hiçbir dosya `#include` etmiyor; `kernel_arm64`/`kernel_main_arm64` adı ağaçta başka hiçbir yerde geçmiyor. (3) `boot.S` `bl cekirdek_ana` çağırır, `kernel_main_arm64`'ü değil. (4) Çağırdığı `uart_write`/`uart_write_hex` hiçbir yerde tanımlı değil (`uart.c`: `uart_putc`, `uart_yaz`, `uart_hex`) — bugünkü ağaçla **bağlanamıyor** (`ld: undefined reference`). `cekirdek.c`'nin eski öncülü. |

**D\* — git hijyeni (dosya silinmez, izleme bırakılır):** `umay/kov/umay`, `us1/us1_sina` ve `arm64/otuken-arm64.img` derleme çıktıları (üçüncüsü doğrulama sırasında fark edildi). `.gitignore`'a eklendiler ama daha önce izlenmeye başladıkları için izlenmeye devam ediyorlardı. Arşive zaten girmiyorlardı; `kur.sh` üretir.

## [6] DUPLICATE / LEGACY DOSYALAR

| Dosya | Durum |
|---|---|
| `arm64/src/kernel_arm64.c` | legacy (D) — `cekirdek.c` tarafından aşılmış |
| `src/umay.c` | **legacy UMAY**: 1.0 döneminin çekirdek-içi UMAY barındırıcısı ("umay-userland-v1"). Gerçek UMAY artık Ring3'te (`umay/`). 15 işlevin 8'i iki kipte de ölü (`umay_istek`, `umay_cek_*`, `umay_durdur`…); canlı kısmı sözleşme sınamaları (`UMAY-TEST: yasakli dosya YAZMA/AG/bellek`). Test bağımlılığı olduğu için **tutuldu**; ayrım önerisi [13]'te. |
| `userprog/hello.c` + `user.ld` + `src/user_elf_blob.c` | legacy görünür ama **canlı** (üretimde Ring3ELF kanıtı). Ölçüt: kod+veri bayt bayt aynı. Derleme kuralı yok: bugünkü bağlayıcı boş bir LOAD segmenti ekliyor. |
| Önceki turlarda çıkarılanlar | `umay/us/us1.{c,h}` (`us1/c` kopyası), `tools/elf_blob.py`, eski `userprog/umay.*` — 1.3'te kanıtla çıkarıldı. |
| Ağaçta **başka duplicate yok** | 253 izlenen dosyanın `sha256` özetleri karşılaştırıldı: aynı içerikli iki dosya yok. |

## [7] TAŞINABİLECEK BÜYÜK DOSYALAR

| Dosya | Öneri | Neden şimdi yapılmadı |
|---|---|---|
| `src/duvar.c` (8 MB), `src/tamga_gorsel.c` (1,9 MB), `src/simge.c`, `src/orbitron.c` | `assets/tema/` altına ham ikili (`.bin`/`.raw`) + bağlama anında `ld -r -b binary` ya da `.incbin`; kaynak ağaçta C dizisi olarak tutmayı bırakmak | Build sistemini değiştirir; talimat gereği otomatik yeniden yapılandırma yapılmadı |
| Aynı dört dosya — **daha ucuz ve ölçülmüş seçenek** | Üretim derlemesine `-ffunction-sections -fdata-sections` + `--gc-sections`: çekirdek 2,9 MB → 227 KB; tema bağlandığı gün bağlayıcı onu kendiliğinden tutar | Ayıklanmış çekirdek `-smp 4`'te tam açıldı (kanıt [1]), ama tam takımlar bu ayarla koşulmadı; ayrı adım |
| `fat32test.img`, `belge/*.wav` | `testdata/` ve `assets/ses/` | `testler.sh` yolu değişir; tek satırlık iş, sonraki adım |

## [8] UMAY BAĞIMLILIK ANALİZİ

```
Ring3 süreç (kabuk)
  └─ syscall 22/23 (IPC_GONDER/IPC_AL)  src/syscall.c
      └─ IPC port "umay"                 src/ipc2.c
          └─ UMAY (Ring3, /UMAY.ELF)     umay/  — ring3_umay_calistir, TÖRE profili
              ├─ OTAĞ     umay/alp/otag.c + 5 alp_*.c   (7 işlev ölü, dosya canlı)
              ├─ BENGİ    umay/core/umay_bengi.c        (6 işlev ölü: bengi_oku/ara/sil…)
              ├─ BİLGİ    umay/core/umay_bilgi.c
              ├─ US       umay/us/{umay_us,us_stub,umay_us_ipc,umay_us_yerel}.c
              ├─ US1      ../us1/c/us1.c  ← /UMAY.US1, /SOZLUK.TXT
              ├─ GUI      umay/gui/umay_ulgen.c  → SYS_PEN_* (14–19, 30–31)
              ├─ SES      umay/ses/umay_ses.c    → SYS_SES_NOTA/OZAN/DEDE (32,33,35)
              └─ port     umay/port/umay_port_otuken.c  (syscall numaraları = include/syscall.h)
```

- **Gerçek UMAY:** `umay/` (Ring3, 109 040 B, kaynaktan bayt bayt aynı). `--gc-sections` ile **ölü dosya yok**; 34 ölü işlev dosyalara dağılmış API yüzeyi. Ana makine takımı (202) bunların çoğunu kullanır.
- **Eski UMAY:** `src/umay.c` (çekirdek-içi). İkisi karışmıyor: çekirdek-içi katman `UMAY:` önekiyle, Ring3 UMAY `UMAY-HOST:` / `[n]` önekiyle yazar.
- **Kırılgan bağlar:** (1) port katmanındaki syscall numaraları `include/syscall.h` ile elle eşleniyor. (2) `SYS_TEMA` ve ses syscall'ları çalışıyor, ama arkalarındaki tema çizimi ve ses donanımı bağlı değil ([1], [4]). (3) BENGİ'nin yazma yolu (`up_yaz_yola`) UMAY ikilisinde ölü: UMAY'ın disk yazma yetkisi yok.

## [9] US1 BAĞIMLILIK ANALİZİ

| Parça | Dosya | Bağ |
|---|---|---|
| C çıkarım motoru | `us1/c/us1.c`, `us1.h` | UMAY'a bağlanır; `us1_yukle`, `us1_ileri`, `us1_sec` |
| Model biçimi | `us1/arac/us1.py` (`model_yaz`: `<IIIIIII` başlık, sürüm 1; satır başına Q8) ↔ `us1.c` (`US1_SURUM`) | iki uç aynı biçim; C sınırları d≤96, katman≤4, ffn≤256, sözlük≤128, bağlam≤96 |
| Model yükleyici | `umay/us/umay_us_yerel.c` | `/UMAY.US1` ve `/SOZLUK.TXT`; sınır dışı modeli reddeder, IPC'ye, sonra stub'a düşer |
| Belirteçleyici | `us1/sozluk.txt` | karakter düzeyi, **45 simge** (Türkçe harfler dahil) |
| Python referans | `us1/arac/us1.py` (`Kucuk`, `ileri`) | `egit.py` ve `referans.py` `from us1 import` |
| Eğitim | `us1/arac/egit.py` | numpy, elle geri yayılım; `umay.us1` + `sozluk.txt` yazar |
| Doğrulama | `us1/arac/referans.py` → `us1/sinama/referans_*.txt` → `us1/us1_sina` | C motoru referansa en çok %0,72 fark |
| Dışa aktarma | `model_yaz` + `make umay-disk` (`mcopy` ile disk imajına) | çekirdek yeniden derlenmez |

Hiçbir US1 dosyası gereksiz değil.

## [10] BUILD BAĞIMLILIKLARI

```
make  ─┬─ boot/*.asm (nasm) ───────────────────────────────┐
       ├─ src/*.c (gcc -mcmodel=kernel, -MMD bağımlılık) ────┤
       ├─ userprog/{crt0,shell,ulib}.c ─► shell.elf ─► elf_blob_yap.py ─► src/shell_elf_blob.c ─┤
       └─ src/user_elf_blob.c (gömülü hello; kuralı yok) ─┴─► ld -T linker.ld ─► otukenos-mini.elf
make iso        ─► grub-mkrescue + grub.cfg ─► otukenos-mini.iso
uefi_iso_yap.sh ─► otukenos-uefi.iso
make -C umay    ─► umay/*.c + ../us1/c/us1.c ─► umay/kov/umay
make umay-disk  ─► mcopy: UMAY.ELF, us1/umay.us1→UMAY.US1, us1/sozluk.txt→SOZLUK.TXT ─► diskfiles.img
cd arm64 && make ─► boot.S + uart.c + cekirdek.c ─► otuken-arm64.img
                    (önceden var olan uyarı: "LOAD segment with RWX permissions" - 1.4'te de aynı)
gen_font.py (elle) ─► include/font8x8.h
kur.sh = hepsinin sırası + sınama ikilileri
```

## [11] SİLİNEBİLECEK DOSYALAR

1. `arm64/src/kernel_arm64.c` — **silindi** (kanıt [5]).
2. `umay/kov/umay`, `us1/us1_sina`, `arm64/otuken-arm64.img` — git izlemesinden **çıkarıldı**; diskteki çıktı yerinde duruyor, `kur.sh` üretir.

Başka dosya silinmedi. Ölü kod içeren ama bağlanan dosyalar (tema, WAV, eski UMAY, işkence) sınıf B/C'dir.

## [12] DOKUNULMAMASI GEREKEN DOSYALAR

- `diskfiles.img` — çalışma zamanı: UMAY, modeli ve sözlüğü buradan yüklenir.
- `src/shell_elf_blob.c`, `src/user_elf_blob.c` — elle düzenlenmez; ilki `make` ile üretilir, ikincinin kaynağı `hello.c`.
- `include/syscall.h` ↔ `umay/port/umay_port_otuken.c` ↔ `userprog/ulib.h` — syscall numaraları üç yerde aynı olmalı.
- `include/cpu.h` `CPU_OFS_*` ↔ `boot/syscall_entry.asm` — `_Static_assert` korur.
- `boot/ap_trampoline.asm` — 0x8000'e kopyalanır; veri alanı ofseti 0xF00 sabit.
- `us1/umay.us1` ↔ `us1/sinama/referans_*.txt` — model değişirse referans yeniden üretilmeli.
- `kanitlar/` — elle düzenlenmez; test çıktısıdır.

## [13] ÖNERİLEN TEMİZ KAYNAK AĞACI

Bugünkü düz yapı build'i ve testleri bozmadan çalışıyor; taşımak betik yollarını ve Makefile'ları değiştirir. Önerilen hedef (aşamalı):

```
otuken/
├── production/          (bugün: boot/ src/ include/ userprog/ linker.ld grub.cfg Makefile)
│   └── src/ … (tema dosyaları hariç)
├── assets/
│   ├── tema/            duvar, tamga_gorsel, simge, orbitron (ham ikili + .incbin)
│   └── ses/             belge/*.wav
├── tests/
│   ├── cekirdek/        testler.sh, donanim_testleri.sh, kabuk_testi.py, asama_dogrula.py, *_hazirla.sh
│   ├── cekirdek-ici/    src/iskence.c, src/kervan_ornek.c (yalnız tanı derlemesine)
│   ├── testdata/        fat32test.img, diskfiles/
│   └── kanitlar/
├── tools/               elf_blob_yap.py, gen_font.py, kur.sh, uefi_iso_yap.sh
├── docs/                README, DEVIR-PROMPTU, KAYNAK-ANALIZI
└── arm64/

umay/
├── runtime/             core/ alp/ us/ gui/ ses/ port/umay_port_otuken.c  (bugün: umay/)
├── model/               umay.us1  (bugün: us1/)
├── tokenizer/           sozluk.txt
├── engine/              us1.c, us1.h  (bugün: us1/c; UMAY ve sınama ortak kullanır)
├── training/            egit.py, us1.py
├── evaluation/          referans.py, us1_sina.c, referans_*.txt, sina/ (202)
└── export/              model_yaz + umay-disk kuralı
```

**Sıra önerisi:** (1) `src/umay.c` sözleşme sınamalarını `tests/cekirdek-ici`'ye al, çekirdek-içi eski UMAY'ı kaldır. (2) `iskence.c` ve `kervan_ornek.c`'yi yalnız tanı derlemesine bağla. (3) Tema verisini `assets/`'e ve `.incbin`'e taşı. (4) Dizinleri taşı, betik yollarını tek seferde güncelle, üç takımı koştur.

## [14] EĞİTİM AŞAMASINA GEÇİŞ İÇİN KALAN İŞLER

1. **Eğitim verisi yok.** `egit.py` 900 karakterlik gömülü `METIN` ile eğitiyor. Gerekli: `umay/training/veri/` + okuyucu; README'nin önerisi birkaç MB düz Türkçe metin.
2. **Model kapasitesi motor sınırlarına dayalı.** d≤96, 4 katman, sözlük≤128, bağlam≤96 Ring3 belleği için seçilmiş. Daha büyük model için `us1.h` sınırları, UMAY yığını (16 sayfa) ve `UMAY_AZAMI_BOYUT` birlikte büyümeli.
3. **Belirteçleyici karakter düzeyi (45 simge).** Kelime/alt-kelime (BPE) için `sozluk.txt` biçimi ve `us1_sec` değişir.
4. **Eğitim numpy/CPU, elle geri yayılım.** Büyük veri için yeniden yazım gerekir; biçim sözleşmesi (`model_yaz` ↔ `us1_yukle`) korunmalı ve sürümlenmeli.
5. **Değerlendirme yalnız sayısal eşleşmeyi ölçüyor** (C ↔ numpy). Dil kalitesi ölçütü (karmaşıklık/perplexity, ayrılmış doğrulama kümesi) yok.
6. **Ses yolu etkisiz:** `ses_init` çağrılmıyor. Çözüm: sürücü yöneticisine `ac97` kaydı (`-device AC97` olmadan sessizce devre dışı).
7. **Masaüstü teması bağlı değil:** `ulgen_theme_draw`'u ÜLGEN birleştiricisine bağlamak; ya da bağlanana kadar `--gc-sections` ile 2,7 MB'ı imajdan çıkarmak.
8. **BENGİ kalıcı değil:** DISK_WRITE yetkisi ve FAT16 alt dizine dosya yazımı yok.

---

### Yeniden üretim

```sh
# ölü kod (üretim):  işlev başına bölüm + bağlayıcı ayıklaması
make CFLAGS="-ffunction-sections -fdata-sections <her zamanki bayraklar>" \
     LDFLAGS="-m elf_x86_64 -T linker.ld -nostdlib --gc-sections --print-gc-sections -e _start"
# (linker.ld'de: KEEP(*(.multiboot)) KEEP(*(.boot)); *(.text .text.*) vb.)
# tanı kipi için -DOTUKEN_TANI_MODU=1 ekleyin; "her iki kipte ölü" = iki listenin kesişimi
```
