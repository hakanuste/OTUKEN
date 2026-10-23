# ÖTÜKEN 1.4 + UMAY

Sıfırdan yazılmış 64-bit işletim sistemi (x86_64, temel AArch64 portu)
ve üzerinde Ring3'te koşan yapay us uygulaması UMAY.

## Hızlı başlangıç

```sh
bash kur.sh              # araçları denetle, çekirdek + UMAY + US1'i derle
bash kur.sh --calistir   # QEMU'da başlat (-smp 4, AC'97)
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

Bu depo yalnız yazılımı içerir; sınama takımları (`testler.sh`, `tests/`,
`kanitlar/` ve benzerleri) dağıtıma konmadı. `bash kur.sh --sina` bu yüzden
"sınama yapılmadı" diyerek başarısız döner. Çekirdeğin öz-sınamaları
kaynakta durur ve `OTUKEN_TANI_MODU=1` ile derlenince açılışta çalışır.

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

Ölçülerin bir kısmı bu depoda olmayan sınamalarla alındı (kaynak sütununda *dağıtımda yok*).

| Ölçü | Değer | Kaynak |
|---|---|---|
| US1 modeli | d=96, 4 katman, 4 baş, ffn=256, bağlam 96, **459.168 parametre** (464 KB, biçim v1) | `us1/umay.us1` başlığı |
| Sözlük | 83 **bayt** (bayt düzeyi; harf/alt sözcük değil) | `us1/sozluk.txt` |
| Konum kodlaması | dağıtılan modelde yok (biçim v1); motor v2/RoPE modelleri de okur | `us1/umay.us1` başlığı, `docs/US1-V2-ROPE.md` |
| Motor sınırları | d ≤ 96, katman ≤ 4, ffn ≤ 256, sözlük ≤ 128, bağlam ≤ 96 | `us1/c/us1.h` |
| Eğitim derlemi | 180 KB (1462 soru-yanıt çifti + düzyazı), 21 doktrin kaydından | `us1/arac/derlem.py` |
| Sabit sınama kümesi | 85 soru; hiçbir yazım biçimi eğitimde yok | `us1/derlem/sinama_kumesi.tsv` |
| Model, sınama kümesi | ilk cümle doğru %12, sözcük F1 0,20; alan dışında kendi başına ret %9 | `kanitlar/us1/olcum.json` (`us1/arac/olc.py`), *dağıtımda yok* |
| Kanıt katmanı, sınama kümesi | alan içi doğru dayanakla yanıt %95 (yüklemli soru %81); alan dışı ret ≥ %99,5; yönerge örnekleri 5/5 ret; dağınık kanıt (terimler ayrı kayıtlarda) 398 soruda yanlış kabul 0 | `tests/umay_kanit`, *dağıtımda yok* |
| Kanıt eşikleri | kapsama ≥ %85, söz bağlılığı ≥ %60, yüklem ağırlığı 40 | `umay/include/umay_kanit.h` |
| UMAY ikilisi | 145 KB | `umay/kov/umay` |
| UMAY mantık sınaması | 279 geçti, 0 kaldı | `make -C umay sina`, *dağıtımda yok* |
| US1 motor sınaması | 15 geçti, 0 kaldı | `us1/us1_sina`, *dağıtımda yok* |

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
umay/        UMAY yapay us uygulaması (core, alp, us, ses, gui, port)
us1/         US1 çıkarım motoru (c/, UMAY de bunu derler), eğitici (arac/), model, sözlük
belge/       ses örnekleri
docs/        KAYNAK-ANALIZI.md (dosya sınıfları, bağımlılıklar, ölü kod) + kaynak-envanteri.txt
arm64/       AArch64 portu (ayrı derleme)
kur.sh       kurulum / derleme betiği
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
- **Masaüstü teması (1.4.8'de bağlandı):** duvar kâğıdı, masaüstü simgeleri,
  üst çubuk ve 7 tamgalı rıhtım çizilir. Rıhtımda UMAY, BİTİG (dosyalar),
  ORKUN (kabuk), ÜLGEN (**Görünüm**: tema seçici), TULPAR (**ağ
  gezgini**), KERVAN (**paket yöneticisi**) ve TAMGA (**dijital kimlik**)
  çalışır. Üst çubukta ÖTÜKEN tamgası ve
  "Sistem" **Sistem** izleyicisini, "Görünüm" tema seçiciyi açar;
  **"Uygulamalar" (1.4.9)** 15 tamganın hepsini listeleyen açılır menüdür:
  15'inin de uygulaması var.
  "Dosya", "Düzen", "Yardım" eylem tanımlamaz. Uygulama ızgarası çizilmez.
- **ULAK, DEDE KORKUT, TÖRE, TİMUR, KUT (1.4.9):** "Uygulamalar"
  menüsünden açılan Ring3 pencereleri, her biri en az yetkiyle:
  **ULAK** ağ durumu (kart, DHCP adresi, ağ geçidi, DNS, sayaçlar, açık TCP
  bağlantıları; ad çözme ve :80 bağlantı denemesi). **DEDE KORKUT** seçilen
  atasözünü ya da yazılan sözü çekirdeğin formant sentezleyicisiyle söyler;
  ses kartı yoksa bunu yazar. **TÖRE** görevlerin yetkilerini ve her sistem
  çağrısının istediği yetkiyi gösterir (salt okunur; politika buradan
  değiştirilmez). **TİMUR** korumaların durumunu gösterir: NX ve W^X açık,
  **SMEP/SMAP kapalı** (işlemci desteklese de çekirdek açmıyor) — kapalı
  olan kapalı yazılır. **KUT** kullanıcıları, rolleri ve süreç bağlarını
  gösterir; masaüstü uygulamaları bugün kullanıcı kimliğiyle değil TÖRE
  profiliyle koşar.
- **ASENA, ERGENEKON, TAMGA (1.4.9):**
  **ASENA (kurtarma)** FAT16 birimini denetler (alt dizinler dahil): FAT
  kopyalarının eşitliği, geçersiz başlangıç kümesi, kırık ve döngülü
  zincir, çapraz bağlı küme, boy/zincir uyuşmazlığı, kayıp küme. Denetim
  diske yazmaz; "Onar" (iki kez basılınca) fsck.fat'ın yaptığı yönde düzeltir
  ve kesilen veri GERİ GELMEZ. Dizge dosyalarını imajla karşılaştırır, bozuk
  olanı imajdan geri yükler. **ERGENEKON (yeniden kurulum)** ISO'daki imzalı
  dizge imajını (`/boot/ergenekon.krv`, GRUB modülü, KERVAN v2 biçimi ve
  anahtarları) kullanır; imza ve her dosyanın SHA-256'sı doğrulanmadan
  yazmaz: "Dizgeyi yenile" yalnız eksik/bozuk dizge dosyalarını yazar
  (kullanıcı dosyaları ve paketler kalır); "Sıfırdan kur" diski biçimler,
  bütün dosyalar ve paketler gider (EVET yazılarak onaylanır); "Yeniden
  başlat". Alt dizinler biçimlemede gider. **TAMGA (dijital kimlik)**
  çekirdeğin kod + salt okunur verisinin SHA-256'sını, KERVAN/ERGENEKON imza
  anahtarlarının parmak izlerini, imajı imzalayan anahtarı ve TULPAR'ın güven
  listesini gösterir; bir dosyanın SHA-256 mührünü çıkarır. Kullanıcıya ait
  bir anahtar çifti (kişisel dijital kimlik) YOK. Diske yazan çağrılar yeni
  KURTARMA yetkisi ve açık bir onay sözcüğü ister.
- **Masaüstü uygulamaları (1.4.8):** diskteki ELF'ler (`SISTEM.ELF`,
  `GORUNUM.ELF`), `src/uygulama.c` her birini KENDİ adres alanında ayrı
  süreç olarak, yalnız kendi TÖRE profiliyle (disk/ağ/IPC/heap yok)
  Ring3'te başlatır; açıksa yenisini açmaz, penceresini öne getirir.
  Sistem izleyicisi süreç listesini göstermez (o çağrı SÜREÇ yetkisine
  bağlı, verilmedi). Temalar arasındaki fark küçüktür (duvar kâğıdı
  aynı; değişen çubuk ve çerçeve renkleri).
- **Ağ ve TULPAR (1.4.9):** TCP artık gerçek istemci TCP'si (alma halkası,
  pencere, yeniden gönderme, sıra dışı saklama, sağlama denetimi, düzgün
  kapanış); alt ağ dışına ağ geçidi üzerinden çıkılır. Ring3 programları
  TÖRE ağ yetkisiyle bağlan/gönder/al/kapat ve ad çözme çağrılarını
  kullanır; süreç ölünce bağlantıları kapanır. **TULPAR** metin tabanlı
  gezgindir: HTML'den okunur metin, bağlantılar, GET formları. HTTPS:
  TLS 1.2, BearSSL 0.6 (`ucuncu/bearssl`, MIT), Mozilla kökleri (146),
  ad + tarih + zincir doğrulaması; doğrulanamayan site açılmaz; donanım
  rastgele sayı üreteci (RDRAND/RDSEED) yoksa HTTPS reddedilir. TLS 1.3 yok.
  **JavaScript, resim, CSS yerleşimi, çerez ve POST yok.** Yalnız istemci
  (dinleyen soket yok), IPv6 yok, TCP pencere ölçekleme ve SACK yok
  (en çok 32 KB pencere). Sınamalar yerel sunucuya karşıdır (QEMU slirp ve
  TAP + Linux TCP); gerçek internette ilk deneme VirtualBox'ta yapılacak.
- **KERVAN (1.4.9):** paketler `.KRV`, ECDSA P-256 ile imzalı; çekirdek
  imzayı ve her dosyanın SHA-256'sını doğrulamadan diske yazmaz. Dizge
  dosyalarının ve başka paketin dosyalarının üzerine yazılmaz; kayıt
  `/KERVAN.LST`. Paketler yalnız kök dizine kurulur (FAT16'da alt dizine
  yazım yok), paket başına en çok 16 dosya ve 2 MB. Ağdan paket indirme ve
  kurulan uygulamanın menüye eklenmesi henüz yok. Üretim imza anahtarının
  özel yarısı depoda DEĞİLDİR (`anahtarlar/`, git dışı).
- **Üst çubuk saati (1.4.9):** CMOS RTC'den gerçek tarih ve saat, UTC+3
  (`OTUKEN_ZAMAN_DILIMI_DK`; Türkiye yaz saati uygulamaz). RTC okunamazsa
  çalışma süresi gösterilir.
- **Ses (1.4.9'da bağlandı):** AC'97 sürücüsü artık açılışta başlatılıyor
  (önceden `ses_init` hiç çağrılmıyordu, ses yolu tümüyle etkisizdi) ve
  akan çıkış yapıyor: 32 tamponluk halka, ardışık çağrılar birbirini
  kesmez, konuşma sonunda kuyruk boşalınca durur. QEMU'da çıkış WAV'a
  kaydedilerek sınandı (DEDE sözü 21 birim, ~2,1 sn, kart hepsini çaldı).
  Gerçek donanımda ve VirtualBox'ta duyulup duyulmadığı denenmedi.
  **Konuşan Dede Korkut'tur; UMAY yapay usun adıdır.** Ses kartı varken
  UMAY'ın yanıtlarını Dede Korkut söyler: yanıt SIRAYA alınır, çekirdekteki
  "Dede" iş parçacığı seslendirir, UMAY beklemez (sıra 4 metin; dolarsa en
  eski bekleyen düşer). Konuşma sentezi yaşlı erkek sesi
  taklidi, doğal değil.
