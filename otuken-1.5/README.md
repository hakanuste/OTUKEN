# ÖTÜKEN 1.4 + UMAY

Sıfırdan yazılmış 64-bit işletim sistemi (x86_64, temel AArch64 portu)
ve üzerinde Ring3'te koşan yapay us uygulaması UMAY.

## Hızlı başlangıç

```sh
bash kur.sh              # araçları denetle, çekirdek + UMAY + US1'i derle
bash kur.sh --calistir   # QEMU'da başlat (-smp 4, AC'97)
bash kur.sh --sina       # tüm sınama takımları
```

Elle:

```sh
make && make iso
qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=diskfiles.img,format=raw,if=ide,index=0 \
  -netdev user,id=n0 -device e1000,netdev=n0 -m 128
```

Kabuk açıldığında `yardim` yazın. **Klavye Türkçe-Q düzenindedir**:
`i` harfi apostrof tuşundadır, QWERTY'deki `i` tuşu `ı` üretir.

### ARM64 (temel port — kapsam için 'Bilinen sınırlar'a bakın)

```sh
cd arm64 && make
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -nographic \
  -kernel otuken-arm64.img
```

### Testler

```sh
./testler.sh            # çekirdek takımı (tanı kipi -smp 1 ve -smp 4 dahil)
./donanim_testleri.sh   # donanım + SMP
python3 kabuk_testi.py  # gerçek klavyeyle kabuk oturumu (Tab ile klavye odağı dahil)
./uyum_sinama.sh 2      # UMAY IPC uyumu: 1-8 çekirdek, ağır yükle birlikte, kontrol grubu
./agir_sinama.sh 3 64   # ağır SMP yarış / sızıntı / soak
make -C umay sina       # UMAY mantık takımı (ana makinede)
cd us1 && ./us1_sina    # US1 motoru öz-sınaması (KV önbelleği = tam geçiş, bit bit)
tests/us1_conformance/calistir.sh    # Python <-> C US1 uyumu, işlem düzeyinde
tests/us1_fuzz/calistir.sh 20000     # US1 model ayrıştırıcısı bulandırma (ASan+UBSan)
tests/umay_kanit/calistir.sh         # kanıt katmanı: alan içi / alan dışı (geliştirme + sınama)
python3 tests/belge/umay_olculer.py denetle   # aşağıdaki UMAY ölçüleri koddan mı
```

### Kullanıcı programları

Kabuk çekirdeğe gömülüdür (`src/shell_elf_blob.c`); UMAY, model ve
sözlük diskten okunur (`UMAY.ELF`, `UMAY.US1`, `SOZLUK.TXT`):

```sh
make kullanici   # userprog/shell.elf -> src/shell_elf_blob.c
make umay-disk   # umay/ -> UMAY.ELF; us1/umay.us1 + sozluk.txt -> diskfiles.img
```

Kabuk ve UMAY kaynaktan **bayt bayt aynı** yeniden üretilir. ISO'lar ve
ELF'ler arşivde yoktur: `bash kur.sh` (ya da `make && make iso`) üretir.
`hello` (`src/user_elf_blob.c`) kapsam dışıdır.

## UMAY

Diskten yüklenen, Ring3'te koşan UMAY uygulaması (`umay/`): OTAĞ görev
motoru ve ajan meclisi, 12 uzman ALP, OZAN kişilik katmanı, BİLGİ
kütüphanesi (gömülü doktrin + diskteki belgeler), BENGİ kalıcı hafıza,
ARAŞTIRMA yordamı, KANIT karar katmanı, US çıkarım katmanı, PORT taşıma
katmanı. Kabuktan `umay <soru>` ile IPC üzerinden konuşulur
(ör. `umay durum` → "Ocağım tüter, gözüm açıktır.").

<!-- UMAY-OLCULER-BASLA (tests/belge/umay_olculer.py yazar; elle düzenleme) -->
**UMAY nedir:** ÖTÜKEN içinde Ring3'te çalışan **deneysel** bir sistem: küçük bir dil modeli (US1) + kalıcı hafıza (BENGİ) + yerel kütüphane araştırması + ajan meclisi (OTAĞ). Genel amaçlı bir dil modeli DEĞİLDİR; bildikleri yalnız kütüphanesindeki kayıtlardır.

| Ölçü | Değer | Kaynak |
|---|---|---|
| US1 modeli | d=96, 4 katman, 4 baş, ffn=256, bağlam 96, **459.168 parametre** (464 KB, biçim v1) | `us1/umay.us1` başlığı |
| Sözlük | 83 **bayt** (bayt düzeyi; harf/alt sözcük değil) | `us1/sozluk.txt` |
| Konum kodlaması | yok (tasarım: `docs/US1-V2-TASARIM.md`) | `us1/c/us1.c` |
| Motor sınırları | d ≤ 96, katman ≤ 4, ffn ≤ 256, sözlük ≤ 128, bağlam ≤ 96 | `us1/c/us1.h` |
| Eğitim derlemi | 180 KB (1462 soru-yanıt çifti + düzyazı), 21 doktrin kaydından | `us1/arac/derlem.py` |
| Sabit sınama kümesi | 85 soru; hiçbir yazım biçimi eğitimde yok | `us1/derlem/sinama_kumesi.tsv` |
| Model, sınama kümesi | ilk cümle doğru %12, sözcük F1 0,20; alan dışında kendi başına ret %9 | `kanitlar/us1/olcum.json` (`us1/arac/olc.py`) |
| Kanıt katmanı, sınama kümesi | alan içi doğru dayanakla yanıt %95 (yüklemli soru %81); alan dışı ret ≥ %99,5; yönerge örnekleri 5/5 ret | `tests/umay_kanit` |
| Kanıt eşikleri | kapsama ≥ %85, söz bağlılığı ≥ %60, yüklem ağırlığı 40 | `umay/include/umay_kanit.h` |
| UMAY ikilisi | 141 KB | `umay/kov/umay` |
| UMAY mantık sınaması | 274 geçti, 0 kaldı | `make -C umay sina` |
| US1 motor sınaması | 15 geçti, 0 kaldı | `us1/us1_sina` |

Model küçüktür ve genellemesi zayıftır: yanıtın doğruluğunu model değil **kanıt katmanı** korur. Soru kütüphanede yeterli kanıtla karşılanmıyorsa motor hiç çağrılmaz ve UMAY "Bunu doğrulayacak yeterli bilgim yok." der; modelin sözü dayanağa bağlı değilse atılır ve kaynaktaki cümle aynen verilir (`docs/UMAY-KANIT.md`).
<!-- UMAY-OLCULER-BITIS -->

**US1 motoru** (`us1/c/us1.c`): tamsayı çıkarım — ağırlıklar satır başına
Q8, etkinleşmeler 16.16 sabit nokta; FPU/SIMD yok. RMSNorm, nedensel
dikkat, SwiGLU, KV önbelleği. Python float64 referansıyla işlem işlem
karşılaştırılır (`tests/us1_conformance`). Model ayrıştırıcısı bozuk ya da
kötü niyetli dosyayı reddeder (`tests/us1_fuzz`). Model diskten okunur;
değiştirmek çekirdek derlemesi gerektirmez. Eğitim ve dağıtım
(`docs/UMAY-EGITIM.md`):

```sh
cd us1/arac
python3 sizinti.py yeni                 # eğitim/doğrulama sızıntı denetimi
python3 egit.py secim                   # sızıntısız ayrım + erken durdurma
python3 egit.py dagitim <en_iyi_adim>   # sınama dışı bütün veriyle
python3 egit.py aktar ../kov/dagitim/kayit.npz ../kov/aday.us1
python3 olc.py karsilastir ../umay.us1 ../sozluk.txt ../kov/aday.us1 ../kov/aday.sozluk
# yalnız "YENİ MODEL ÜSTÜN" ise: umay.us1'i değiştir, make umay-disk
```

**Ses:** AC'97 PCM sürücüsü, WAV çalar, Dede Korkut için Türkçe formant
sentezleyici (tamsayı, 84 Hz). Örnekler `belge/` altında.

**Güvenlik:** UMAY TÖRE yetki profiliyle koşar (disk okuma, bellek, ağ);
**disk yazma ve süreç yönetimi yetkisi yoktur**. Yeni syscall'lar
(25–35) kullanıcı işaretçilerini sayfa varlığı + RW denetimiyle kesmesiz
tek bölgede kopyalar.

## Kabuk komutları

| Komut | İşlev |
|---|---|
| `yardim` | komut listesi |
| `listele [yol]` | dizin içeriği |
| `goster <dosya>` | dosya içeriği |
| `kimlik` | süreç kimliği (PID) |
| `saat` | zamanlayıcı tiki |
| `bellek` | userland bellek sınaması |
| `ciz` | ekrana grafik çizer |
| `umay <soru>` | UMAY'a IPC ile soru sor (ör. `umay durum`) |
| `cik` | kabuğu kapat (otomatik yeniden başlar) |

## Mimari

```
Ring3  │  KABUK        UMAY         (ayrı ELF programları)
       │     │           │
       │     └── syscall ┘
═══════╪══════════════════════════════════════════════
Ring0  │  ÇEKİRDEK (higher-half, 0xFFFFFFFF80000000)
       │  ├─ PMM / VMM / heap / süreç başına adres alanı
       │  ├─ Zamanlayıcı (preemptive) / süreç / thread
       │  ├─ SMP: ACPI MADT, xAPIC/x2APIC, INIT-SIPI-SIPI, çekirdek-başına
       │  │   GS/TSS/GDT (swapgs), çağrı IPI'si, TLB vuruşu, LAPIC
       │  │   zamanlayıcısı, İŞ DAĞITIMI (varsayılan açık), alt sistem
       │  │   kilitleri: depolama / ÜLGEN+GUI / ses (uyuyan, özyinelemeli),
       │  │   VMM / günlük / TÖRE / süreç / servis / KUT (dönen, özyinelemeli)
       │  ├─ Senkronizasyon (atomic/spinlock/mutex)
       │  ├─ IPC (mesaj kuyruğu, pipe, paylaşılan bellek, port)
       │  ├─ BİTİG: VFS + FAT16 (oku/yaz) + FAT32
       │  ├─ Sürücüler: PCI, ATA, AHCI, NVMe, USB, PS/2, PIT, e1000
       │  ├─ ULAK: Ethernet/ARP/IPv4/UDP/TCP/DHCP/DNS + soket
       │  ├─ TİMUR (NX, W^X) / TÖRE (yetki) / KUT (kimlik)
       │  ├─ ÜLGEN: pencere + compositor
       │  ├─ KERVAN: paket sistemi
       │  └─ Servis yöneticisi, günlük, kararlılık izleme
```

## Yapılandırma

`OTUKEN_TANI_MODU=1` ile derlerseniz tüm öz-sınamalar çalışır ve
ayrıntılı rapor verir. Varsayılan (0) üretim modudur: sessiz açılış.

```sh
make CFLAGS="-DOTUKEN_TANI_MODU=1 -mcmodel=kernel -ffreestanding ..." 
```

## Dizin yapısı

```
boot/        x86_64 önyükleme + kesme + syscall assembly
src/         çekirdek kaynağı
include/     başlıklar
userprog/    kullanıcı programları (kabuk, ulib, hello)
umay/        UMAY yapay us uygulaması (core, alp, us, ses, gui, port, sina)
us1/         US1 çıkarım motoru (c/, UMAY de bunu derler), eğitici (arac/), model, sözlük
belge/       ses örnekleri
kanitlar/    son test koşularının çıktıları (kanıt)
docs/        KAYNAK-ANALIZI.md (dosya sınıfları, bağımlılıklar, ölü kod) + kaynak-envanteri.txt
arm64/       AArch64 portu (ayrı derleme)
kur.sh       kurulum / derleme / sınama betiği
testler.sh   otomatik regresyon takımı
```

## Bilinen sınırlar

- USB: UHCI aktarım katmanı çalışır (HID klavye numaralandırılır ve
  okunur). XHCI/EHCI yalnızca tespit edilir; aktarım UHCI ile sınırlı
- AHCI: gerçek DMA okuma çalışır (READ DMA EXT). Yazma yok.
  NVMe: yalnızca denetleyici tanınır, veri yolu yok
- FAT: yazma yalnızca kök dizinde, 8.3 dosya adı
- **ARM64: TEMEL port.** Çalışan: önyükleme, PL011 seri çıktı, bitmap
  bellek yöneticisi, yığın, 64-bit aritmetik. **Yok:** GIC (kesme
  denetleyicisi), zamanlayıcı, MMU/sayfalama, görev değiştirme,
  sürücüler, kullanıcı modu. x86_64'teki alt sistemler ARM64'e
  taşınmamıştır — bu bir "tam destek" değildir.
- **SMP:** iş dağıtımı varsayılan açık; `-smp 4`'te yarış sınaması 0 hata
  (kilit kapatılınca hata bulur). Kalan sınırlar: ağ yığını kilitsizdir
  çünkü tek kullanıcısı BSP'ye bağlı ana döngüdür (BSP dışından çağrılırsa
  günlüğe `[HATA]` düşer); TLB vuruşu tüm çekirdeklere gider (CR3'e göre
  daraltılmadı); ACPI'siz makinede MP tablosu okunmaz; **x2APIC yolu test
  edilmedi**; NMI/MCE `swapgs` öncesi pencerede gelirse GS kullanıcı
  değerindedir (işleyiciler GS kullanmıyor). Kapatmak için
  `-DOTUKEN_SMP_DAGITIM=0`.
- **UMAY'ın dil modeli küçüktür ve genellemesi zayıftır** (ölçüler UMAY
  bölümündeki tabloda, koddan üretilir). Görülmemiş soru biçimlerinde
  doğru yanıt oranı düşüktür; UMAY'ı uydurmaktan model değil kanıt katmanı
  alıkoyar. Konum kodlaması yoktur, sözlük bayt düzeyindedir.
- **BENGİ (kalıcı hafıza) diske yazamaz:** UMAY'ın disk yazma yetkisi
  yok ve FAT16'da alt dizine (`/kut/umay/...`) dosya yazımı yok.
  `SYS_WRITE_FILE` yalnız kök dizine yazar.
- **Masaüstü teması çizilmiyor:** duvar kâğıdı, rıhtım, uygulama
  ızgarası, tamga ve Orbitron kodu derlenir ama `ulgen_theme_draw`
  hiçbir yerden çağrılmaz; `tema` komutu yalnız renk tablosunu değiştirir.
- **Ses QEMU'da duyulmadı:** AC'97 kodeği yanıt verir, ama sürekli DMA
  ölçülemedi; gerçek donanımda denenmedi. Yalnız WAV var.
