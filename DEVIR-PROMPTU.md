# ÖTÜKEN — Yeni Sohbet Devir Promptu

> Bu belgeyi yeni sohbetin ilk mesajı olarak yapıştır ve `otuken-1.4.7-kaynak.tar.gz` dosyasını ekle.

---

## Sen kimsin, ne yapıyoruz

Hakan ile birlikte **ÖTÜKEN** adlı, sıfırdan yazılmış 64-bit x86 işletim sistemi çekirdeği geliştiriyoruz (C + NASM). Ek olarak basit bir AArch64 portu var. Hedef donanım: **AMD Ryzen 9 (dahili Radeon), 256 GB DDR5, 2/4/8 TB NVMe/SATA SSD**, ama sistem güncel tüm x86_64 işlemcilerde çalışmalı.

İletişim Türkçe. Hakan'ın çalışma tercihleri:
- Büyük iş parçaları, küçük adımlar arasında durup onay isteme.
- Her düzenlemeyi anlatma; sonunda kısa ve somut rapor ver.
- **"GEÇTİ" yalnızca gerçek kanıtla** (QEMU seri log, `readelf`, `mtools`, `sgdisk` vb.). Kanıt yoksa açıkça "test edilmedi" de.
- Bulunan hatayı, kendi yaptığın hatalar dahil, dürüstçe raporla.

---

## ÖNCE BUNU YAP (ortam kurulumu)

Sandbox sık sık sıfırlanıyor; kaynak kayboluyor. Her oturumda:

```sh
apt-get update && apt-get install -y nasm qemu-system-x86 qemu-system-arm \
  gcc-aarch64-linux-gnu grub-pc-bin grub-common grub-efi-amd64-bin \
  xorriso mtools ovmf gdisk dosfstools python3-cryptography python3-pil
# 1.4.9: ağ sınaması TAP kipi için ayrıca: iproute2 dnsmasq-base
mkdir -p /home/claude/otukenos-kernel && cd /home/claude/otukenos-kernel
tar xzf /mnt/user-data/uploads/otuken-1.4.6-kaynak.tar.gz
chmod +x *.sh *.py
make && make iso
```

**Altın kural:** Her alt adım bittiğinde değişen dosyaları hemen `/mnt/user-data/outputs/otukenos-mini/` altına kopyala. Bir sıfırlamada kaydedilmemiş iş kayboldu ve yeniden yazılmak zorunda kalındı.

**Araç sınırı:** Tek komut 300 sn. Test takımları uzun; zaman aşımına uğrayan komutlardan sonra `pkill -9 -f qemu` yap, sonucu dosyadan (`grep -ac GECTI`) oku.

---

## Derleme ve test

| Komut | İşlev |
|---|---|
| `make && make iso` | BIOS ISO (`otukenos-mini.iso`, multiboot1) |
| `./uefi_iso_yap.sh` | UEFI ISO (`otukenos-uefi.iso`, multiboot2) |
| `cd arm64 && make` | AArch64 imajı |
| `./testler.sh` | Çekirdek takımı — **97 kontrol** (1.4.7: bölüm 14; 1.4.8: bölüm 15 ekran) (tanı kipi `-smp 1` + `-smp 4` yarış sınaması), `make clean` yapar, sonunda üretim moduna döner |
| `./donanim_testleri.sh` | Donanım takımı — **44 kontrol** (CPU, bellek, disksiz, 8 TB LBA48, GPT, UEFI/OVMF, CR0.WP, SMP/TLB) |
| `make kullanici` | Kabuğu kaynaktan derler (bayt bayt aynı), blob'u günceller |
| `make umay-disk` | `umay/`'dan UMAY.ELF (bayt bayt aynı), model ve sözlüğü diske yazar |
| `bash kur.sh [--sina\|--calistir]` | Tek komutla derleme / tüm sınamalar / QEMU |
| `make -C umay sina` | UMAY mantık takımı (ana makine): araştırma, KANIT katmanı (40), BENGİ aday→onay yolu (41) dahil — **279 kontrol** |
| `cd us1 && ./us1_sina` | US1 motoru numpy referansına karşı + KV önbelleği bit bit eşitlik — **15 kontrol** |
| `cd us1/arac && python3 sizinti.py yeni && python3 egit.py secim && python3 egit.py dagitim <adim> --plan 16000 && python3 egit.py aktar … && python3 olc.py karsilastir …` | US1 eğitimi: sızıntısız ayrım, sabit sınama kümesi, üretim F1'iyle erken durdurma; **yeni model yalnız karşılaştırma kuralını geçerse dağıtılır**. Ayrıntı `docs/UMAY-EGITIM.md` |
| `tests/umay_kanit/calistir.sh` | KANIT katmanı ölçümü: UD tabanlı alan içi/dışı kümeler, geliştirme/sınama ayrı, eşik seçimi — ayrıntı `docs/UMAY-KANIT.md` |
| `tests/us1_conformance/calistir.sh` / `tests/us1_fuzz/calistir.sh 20000` | US1 Python↔C işlem düzeyinde uyum (açgözlü 70/70) / model ayrıştırıcı bulandırma (ASan+UBSan) |
| `python3 tests/belge/umay_olculer.py denetle` | README'deki UMAY ölçüleri koddan/sınamalardan üretilenle aynı mı |
| `python3 kabuk_testi.py` | Gerçek klavyeyle kabuk oturumu + Tab ile klavye odağı (kabuk ↔ UMAY penceresi) — **9 kontrol** |
| `./agir_sinama.sh [tekrar=3] [soak_tur=64]` | AĞIR sınama — stres + soak + sızıntı: kontrol grubu + `-smp 1/2/4/8/16` × `-cpu max/qemu64/EPYC` × 3 tekrar + bellek 128/2048 MB + 64 turluk soak; **49 koşu**, ~20 dk, kanıt `kanitlar/agir/` |
| `./uyum_sinama.sh [tekrar=2]` | ÖTÜKEN↔UMAY uyumu yük altında: 3 çekirdek istemci UMAY'a eşzamanlı soru sorar; kontrol grubu (eski yönlendirmeli UMAY) + taban + AĞIR ile aynı anda `-smp 1/2/4/8`; kanıt `kanitlar/uyum/` |
| `python3 us1/arac/anlamlilik.py <karsilastirma.json>` | İki modelin farkı anlamlı mı: eşli permütasyon + bootstrap GA + kesin McNemar. **Dağıtım kararı buna göre** |
| `python3 asama_dogrula.py <tani.log> <tcp.log>` | 42 yol haritası aşamasının kanıtını arar |

**Tanı modu:** `make CFLAGS="-DOTUKEN_TANI_MODU=1 -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -Wextra -O2 -Iinclude"` — tüm öz-sınamaları ve işkence takımını çalıştırır. Varsayılan (0) üretim modu: sessiz açılış, `[UYARI]`/`[HATA]` sıfır olmalı.

**Son durum 1.4.9 (ASENA/ERGENEKON/TAMGA dahil): testler 136/136, donanım 44/44, kabuk 4×9/9, UMAY 279/279, US1 15/15, README denetimi temiz, UYUM 10/10, AĞIR 49/49.** Kanıt `kanitlar/surum149/takim_asena/`.

**Son durum 1.4.7 (varsayılan derleme, iş dağıtımı AÇIK, sessiz makine): 93/93 + 43/43 + 4×9/9 (smp 1,1,4,4), UMAY 279/279, US1 15/15, US1 uyum 17/17 (açgözlü 70/70), US1 bulandırma v1 36/36 + v2 46/46, KANIT sınama değişmedi + dağınık kanıt 398/398 ret, README denetimi temiz, UYUM 10/10, AĞIR 49/49 — sıfır başarısız.** Kanıt `kanitlar/surum147/nihai/`, 1.4.6 dayanağı `kanitlar/surum147/dayanak/`. Dağıtılan model v1 (RoPE v2 anlamlı kötü çıktı, `docs/US1-V2-ROPE.md`).

**1.4.7 (bkz. `docs/RING3-IZOLASYON.md`, `FD-YETKILENDIRME.md`, `SECURITY-AUDIT-147.md`):** çekirdek adres alanına geri düşme kaldırıldı; reap kilit dışında; FD sahip+kuşak; 36 çağrı tek politika tablosunda; ULGEN sahipliği ve boy sınırı; TÖRE temizliği; PMM sayaç hatası. Sınamalar yalıtılmış: `tests/run/<zaman>-<ad>-<pid>/`, `tests/ortak/kosu.sh`. **YAPILMADI:** Ring3 hata yalıtımı ve ikinci güvenlik denetiminin devamı (içerik güvenlik denetleyicisince durduruldu; yeniden üretilmedi).

**1.4.8 (ekran + VirtualBox):** tek birleştirici `src/ekran.c` (ortak z, hasar, kırpma; kök nedenler `docs/ULGEN-CIZIM.md`), ekran regresyonu `tests/ulgen_render` + `testler.sh` bölüm 15 (97 kontrol), çift tampon (`fb_sun`), 8×16 kenarı yumuşatılmış yazı (`gen_yazi16.py` → `include/yazi16.h`, DejaVu Sans Mono), UTF-8/ISO-8859-9 çözümü, renk alanı düzeltmesi, klavye → UMAY UTF-8, masaüstü düzeni. VirtualBox paketi ve yönerge: `docs/VIRTUALBOX.md`.

**1.4.2 notu:** AĞIR sınama, `smp_cagir_digerleri`'nde aralıklı bir SMP kilitlenmesi buldu (kesmeler açıkken alınan çağrı kilidi, uyutulan görevde kalıyordu); düzeltildi. Ayrıntı `docs/AGIR-SINAMA.md`.

---

## Mimari özeti

- Higher-half çekirdek `0xFFFFFFFF80000000`; `.boot` bölümü fiziksel `0x100000`'da.
- Kimlik haritası: CPU 1 GB sayfa (`PDPE1GB`) destekliyorsa **512 GB**, değilse 2 MB sayfalarla **4 GB**. PMM aynı CPUID denetimiyle üst sınırını kırpar. VMM 1 GB ve 2 MB sayfaları gerektiğinde böler (`split_1gb_if_needed`, `split_2mb_if_needed`).
- PMM: dinamik bitmap (256 GB'a kadar), canlı sayaç (O(1)), arama ipucu.
- Kullanıcı alanı PML4[1..255] (`0x8000000000`+). Süreç başına adres alanı, CR3 geçişi simetrik (çekirdek görevinde çekirdek CR3'üne dönülür).
- Adresler: hello ELF `0x8000400000`, kabuk `0x8000600000`, UMAY `0x8000A00000`, ring3 test kodu `0x8000100000`, kullanıcı heap penceresi `0x8001000000` + yuva × 16 MB (32 yuva, reap'te iade).
- Önyükleme: multiboot1 **ve** multiboot2 başlığı. `src/multiboot2.c` etiketleri multiboot1 yapısına çevirir; mmap adresi 64-bit ayrı tutulur (32-bit `mmap_addr` higher-half adresini kırpıyordu).
- `CR0.WP` açıkça set edilir (GRUB-BIOS temiz, GRUB-UEFI set bırakıyordu; davranış firmware'e bağlıydı).
- Syscall: `SYSCALL/SYSRET`, girişte görev-başına çekirdek yığınına geçiş, **tüm GPR'ler korunur** (ABI), TSS.RSP0 görev-başına.

### Syscall numaraları
`0 WRITE, 1 GETPID, 2 YIELD, 3 SLEEP, 4 RING3_PROOF, 5 EXIT, 6 OPEN, 7 READ, 8 CLOSE, 9 SBRK, 10 READKEY, 11 LISTDIR, 12 PUTPIXEL, 13 TICKS, 14–19 pencere API'si (PENCERE_AC, PEN_DOLDUR, PEN_YAZI, PEN_DIK, PEN_KAPAT, SERVIS_LISTE), 20 IPC_ACS, 21 IPC_BUL, 22 IPC_GONDER, 23 IPC_AL, 24 SISTEM_BIL, 25 WRITE_FILE, 26 STAT, 27 MKDIR, 28 YETKI, 29 PROC_LISTE, 30 PEN_SUN, 31 PEN_OLAY, 32 SES_NOTA, 33 OZAN_SOYLE, 34 TEMA, 35 DEDE_KONUS`, `SYS_MAX 36`.

### ÖTÜKEN ↔ UMAY sözleşmesi (1.4.3)
- **Zaman:** tik süresi sabit DEĞİL; `SYS_SISTEM_BIL(SYSBIL_TIK_HZ=4)` ile okunur (PIT 50 Hz → 20 ms). Eski çekirdek 0 döner → UMAY 50 Hz varsayar.
- **Gönderen:** `SYS_IPC_GONDER` iletinin `sender_port`'una çağıranın KENDİ portunu yazar; portu yoksa `IPC_GONDEREN_YOK` (0xFFFFFFFF). 0 geçerli bir port kimliğidir.
- **Alma:** `SYS_IPC_AL` yalnız portun SAHİBİ görevden kabul edilir (başkasınınki -1).
- **Dolu kuyruk:** `ipc_send` hedef kuyruğu (8 ileti) doluysa `IPC_DOLU` (-2) döner; UMAY ≤2 sn yeniden dener.
- **Port ömrü:** görev reap edilince portları kapanır.
- **Yanıt yönü:** UMAY eşzamansız sonucu, SORUYU getiren kanal/kaynağa yollar (`sonuc_t.kanal/kaynak`), "son gönderene" değil.
- **Klavye odağı:** tuşlar yalnız odak sahibine gider (`keyboard_pop_for`); Tab odağı kabuk ↔ ÜLGEN penceresi arasında değiştirir, varsayılan kabuk. `SYS_READKEY` kabuğun, `SYS_PEN_OLAY` pencerenin payıdır.
- **Araştırma:** `GT_ARASTIR` görevini ajan kendisi yapar (`umay/alp/arastirma.c`): plan → turlara yayılmış tarama (kendi → ortak → genel depo, kökle yeniden) → tekilleştirme → kaynaklı rapor + eksik terimler + özet. Ayrıntı `docs/UMAY-ARASTIRMA.md`.
- Kabuk UMAY'ı ≤20 sn bekler (yük altında QEMU'da yanıt 15 sn'yi gördü). Çekirdek `include/syscall.h` ile userland `userprog/ulib.h` **aynı numaraları** taşımalı.

### Kullanıcı işaretçisi güvenliği (`src/syscall.c`)
Doğrulama + kopyalama **kesmesiz tek bölgede** yapılır: `kullanici_dize_kopyala`, `kullanicidan_atomik_oku`, `kullaniciya_atomik_yaz`. Yazma hedefleri RW bitini de denetler. Doğrulama sonrası korumasız dereference yoktur.

### Alt sistemler
BİTİG (VFS, FAT16 okuma/yazma + rollback, FAT32 okuma, GPT) · ULAK (e1000, ARP önbelleği, IPv4/UDP/TCP/DHCP/DNS, soket, ağ hizmetleri 7/13/1900) · TİMUR (NX, W^X) · TÖRE/KUT (yetki, rol) · ÜLGEN (pencere, compositor) · KERVAN (paket) · servis yöneticisi · UMAY barındırıcısı · sürücüler: PCI, ATA (LBA48, IDENTIFY), AHCI (DMA okuma), NVMe (tespit), USB (UHCI + HID klavye; XHCI/EHCI tespit), PS/2, PIT.

### UMAY (1.3'te paketten birleştirildi; 1.4.6 durumu)
`umay/` ağacından derlenen Ring3 programı (OTAĞ, 12 ALP, OZAN, BİLGİ, BENGİ, ARAŞTIRMA, KANIT, US, PORT). **Deneysel küçük dil modeli + hafıza + araştırma + ajan sistemi; genel amaçlı LLM değil.** Model `us1/umay.us1` (US1 tamsayı motoru, Q8 + 16.16, FPU yok; boyutlar README ölçü tablosunda, koddan üretilir). Diskte `UMAY.ELF`, `UMAY.US1`, `SOZLUK.TXT`; `make umay-disk` ile güncellenir. Kabuk `umay <soru>` → IPC port `umay`; UMAY **sorana** (`sender_port`) yanıtlar, uzun yanıt 96 baytlık parçalar (opkod 1 devam / 0 son). TÖRE profili: bkz. aşağı (1.4.7). Syscall 25–35: WRITE_FILE (yalnız kök), STAT, MKDIR, YETKI, PROC_LISTE, PEN_SUN, PEN_OLAY, SES_NOTA, OZAN_SOYLE, TEMA, DEDE_KONUS — hepsi güvenli kopyalama yardımcılarıyla. Port katmanı numaraları `umay/port/umay_port_otuken.c`'de; `include/syscall.h` ile AYNI kalmalı.
- **KANIT (1.4.6):** soru terimleri kütüphane dayanağında yeterince karşılanmıyorsa motor ÇAĞRILMAZ, "Bunu doğrulayacak yeterli bilgim yok. İstersen araştırabilirim." Model sözü dayanağa bağlı değilse atılır, kaynak cümlesi aynen verilir. Eşikler geliştirme kümesinde seçildi (E=85, Y=40, bağlılık 60). `docs/UMAY-KANIT.md`.
- **BENGİ (1.4.6):** konuşma → aday (denetim) → onay → doğrulanmış; SONUC her zaman aday; disk biçimi satır sonu/`|` enjeksiyonuna kapalı; model dosyası BENGİ yolu olamaz. `docs/UMAY-BENGI.md`.
- **ARAŞTIRMA (1.4.6):** `ars_sonuc_t` (kaynak/kanıt/terimler/çatışma/güven ayrı); US'e yalnız kanıt cümleleri. `docs/UMAY-ARASTIRMA.md` §6.
- **US1 v2 (RoPE, 1.4.7):** motor, Python eğitici/referans, uyum ve bulandırma v2'yi destekler (`docs/US1-V2-ROPE.md`); v1 bit bit aynı. Dağıtılan modelin biçimi README ölçü tablosunda.
- **KANIT (1.4.7):** kapsama KAYIT düzeyinde (dağınık kanıt 398/398 ret); bireşim görevleri `kanit_pay_var` ister.
- **TÖRE profili (1.4.7):** UMAY = DISK_READ|MEM_ALLOC|NET_SEND|NET_RECV|IPC|EKRAN|KLAVYE|SES|SISTEM. Her çağrının yetkisi `syscall_dispatch`'teki tek politika tablosunda (`docs/FD-YETKILENDIRME.md`).

---

## Kaynak ağacı analizi

`docs/KAYNAK-ANALIZI.md` (14 bölüm) ve `docs/kaynak-envanteri.txt` (253 dosya, sınıf + kanıt). Özet: gerçekten gereksiz tek dosya `arm64/src/kernel_arm64.c` idi (bağlanamıyordu; silindi). **Üretim çekirdeğinin ~%93'ü (2,66 MB) çalışma anında ulaşılamayan UMAY tema/WAV kodu** (`duvar.c`, `tamga_gorsel.c`, `simge.c`, `orbitron.c`, `ulgen_theme.c`, `ezgi.c`); `--gc-sections` ile çekirdek 227 KB'a iner ve tam açılır. **`ses_init` hiç çağrılmıyor** → çekirdek ses yolu etkisiz.

## Şu an neredeyiz: SMP

| Adım | Durum |
|---|---|
| 1. Çıplak `cli`/`sti` → gerçek spinlock | ✅ |
| 2. Çekirdek-başına veri | ✅ |
| 3. APIC + AP başlatma | ✅ `src/smp.c`, `boot/ap_trampoline.asm` (0x8000). ACPI MADT (RSDP: MB2 etiketi 14/15 veya BIOS alanı), xAPIC/x2APIC, INIT-SIPI-SIPI, PIT kanal 2 gecikmesi (`src/gecikme.c`). |
| 4. GS/TSS + TLB vuruşu | ✅ `swapgs` düzeni: çekirdekte `GS_BASE`=çekirdek bloğu (`cpu_bu()` = `gs:0`). syscall girişi, üç kesme ortak yolu (CS RPL 3 ise), `enter_ring3` swapgs yapar. Çekirdek-başına TSS + GDT kopyası (`tss_ap_kur`). Çağrı IPI'si 0xF0 (`smp_cagir_digerleri`), sahte 0xFF. `tlb_uzak_vur` → vmm (geçerli girdi değişince / unmap) ve TİMUR izin değişikliği. Sınama: kontrol grubunda AP'ler bayat okur, vuruştan sonra hata 0. |
| 5. İş dağıtımı | ✅ Global tablo + çekirdek-başına `simdiki_gorev`, `calisiyor` bayrağı, rq kilidi geçiş boyunca tutulur (`sched_gecis_sonrasi`), kayıp uyandırma jetonu, AP boş görevleri, LAPIC zamanlayıcısı 0xEF, AP syscall MSR'leri. **Varsayılan açık** (`OTUKEN_SMP_DAGITIM=1`). |
| 6. Alt sistem kilitleri | ✅ `ozkilit_t` (uyuyan, özyinelemeli, görev-sahipli; `mutex.c`) ve `ozspin_t` (dönen, özyinelemeli, çekirdek-sahipli; `spinlock.c`). **depo_kilit**: disk/ahci/disk_cache/fat16(+dizin)/fat32/vfs/gpt. **gorsel_kilit**: ÜLGEN + GUI. **ses_kilit**: ses/dede/ozan/ezgi. ozspin: vmm, log, tore, servis, kut, process. Klavye kuyruğu ve PIT2 spinlock. Sarmalayıcılar `ad__ic` + kilit (sarmala aracı; işlev işaretçisi parametreli olanlar elle). Sıra: gorsel→depo, ses→depo; dönen kilit tutarken uyuyan kilit ALINMAZ. Ağ yığını kilitsiz (tek kullanıcı: BSP'deki ana döngü; `bsp_denetle` uyarır). Syscall tamponları **görev-başına** (`task_t.sc_tampon`). **SMP-YARIS**: 6 görev × 12 tur eşzamanlı dosya okuma/yazma + heap/VMM + günlük; `-DDEPO_KILIT_KAPALI` kontrol grubu hata bulur. |
| **7. Sıradaki** | ⏭ TLB vuruşunu CR3'e göre daraltma (hangi çekirdek hangi adres alanında); NMI/MCE için "paranoid entry" (GS denetimi); ağ syscall'ları gelirse ağ kilidi; x2APIC'i gerçek donanımda sınama. Ayrıca: ÜLGEN masaüstü temasını bağlama, BENGİ için FAT16 alt dizine dosya yazımı. |

**Bilinen noktalar:**
- AP'ler `sti; hlt` döngüsünde yalnız IPI işler; PIC kesmeleri yalnız BSP'ye (AP LINT0 maskeli).
- `cpu_yerel_t` asm ofsetleri (`CPU_OFS_*`) `_Static_assert` ile korunur.
- NMI/MCE syscall girişindeki `swapgs` öncesi pencerede gelirse GS kullanıcı değerindedir (Linux "paranoid entry" gerekir); işleyiciler şu an GS kullanmıyor.
- `smp_cagir_digerleri` kilidi bekleyen çekirdek kendine gelen isteği işler (IF=0 kilitlenmesini önler).

## Dürüst sınırlar (README'de de yazılı)

- **UMAY/ÜLGEN teması çizilmiyor:** `ulgen_theme_draw`/`tema_init` hiçbir yerden çağrılmıyor (paketten öyle geldi). Bağlamak ayrı iş.
- **BENGİ kalıcı değil:** UMAY'ın DISK_WRITE yetkisi yok ve FAT16'da alt dizine dosya yazımı yok.
- **x2APIC yolu test edilmedi** (QEMU 8.2 TCG desteklemiyor, sandbox'ta KVM yok). Sınama anahtarı: `-DSMP_X2APIC_ZORLA`.
- ACPI'siz makinede MP tablosu okunmaz → tek çekirdek.
- **256 GB RAM:** kod hazır, sandbox'ta **yalnızca 3 GB'a kadar test edildi**.
- NVMe yalnızca tespit; AHCI yalnızca okuma; USB aktarımı yalnızca UHCI.
- FAT yazma kök dizin + 8.3 ad; FAT32 yalnızca okuma.
- Yalnızca USB klavyeli makinede giriş UHCI ile çalışır; XHCI klavye çalışmaz.
- ARM64 temel port: önyükleme, PL011, PMM, heap. Kesme/zamanlayıcı/sürücü yok.
- Radeon iGPU sürücüsü yok; UEFI'de GOP framebuffer kullanılıyor.
- **Ses:** AC'97 1.4.9'da bağlandı (`ses_init` PCI taramasından sonra), akan halka; QEMU'da WAV kaydıyla sınandı, gerçek donanımda duyulmadı. Video için yalnızca sıkıştırılmamış ham kare oynatıcı gerçekçi; codec değil.
- 8x8 font büyük harf-yalnız; küçük harfler büyük görünüyor (okunabilirlik açığı, öncelik değil).

---

## Öğrenilen dersler (tekrarlama)

1. **"Yok" demeden önce kodu oku.** Grep desenleri farklı isimlendirmeyi kaçırdı (`kullanici_dize_kopyala`, `task->heap_baslangic`); iki kez var olan şeyi "eksik" diye raporladım.
2. **Ortamda beklenmedik dosyalar olabilir** (silinmiş bağlamda yapılmış iş). Üzerine yazmadan önce `ls` ve içerik kontrolü yap; bir kez `ipc2` dosyalarını ezip derlemeyi bozdum.
3. **`make clean` sonrası `make iso` unutulursa** testler olmayan ISO'ya karşı koşar ve toplu yanlış başarısızlık verir.
4. **Kabuk testinde ilk tuşlar kaybolabilir** → ısınma için boş `Enter`. Klavye **Türkçe-Q**: `i` harfi `apostrophe` tuşu, QWERTY `i` tuşu `ı` üretir.
5. **Çok parçalı `serial_write` diğer görevlerin çıktısıyla bölünür** → test sonucu tek çağrıda yazılmalı.
6. **Uyuyan/bloke olan görev kesme bayrağını geri yüklemeli** (`task_sleep`, `task_block_and_switch`, `task_wake`, `task_unblock`). Eskiden koşulsuz `sti` bunu kazara telafi ediyordu; işkence takımında artık IF-kaçak dedektörü var.
7. **Kesme kapalı uzun tarama sistemi dondurur** → PMM sayacı canlı tutuluyor.
8. **Firmware farkları gerçek hataları gizler:** CR0.WP, 1 GB sayfa desteği (`-cpu qemu64` desteklemez, `-cpu max`/`EPYC` destekler). Her iki yolu da test et.
9. **Disksiz makinede** FAT bölme işlemleri sıfıra bölünüyordu → tüm FAT giriş noktalarında bağlı-birim koruması var.
17. **Dışarıdan gelen paket/çatal ESKİ tabandan türemiş olabilir.** UMAY paketi 1.0'dan eski bir çekirdek taşıyordu (çıplak `cli/sti`, `cpu.c` yok). Toplu kopyalama SMP işini silerdi: ortak ata (1.0) ile üç yönlü karşılaştır, yalnız özellik parçalarını al.
18. **Kimlik haritasında delik bırakma:** bir sınama kimlik adresini yeniden haritalayıp `unmap` ederse PMM o çerçeveyi dağıttığında rastgele PAGE FAULT olur (0x700000 böyle çıktı; TİMUR 48 GB'ta aynısını hedef 256 GB makinede yapardı). Sınama adreslerini kimlik haritası DIŞINDA seç (PML4[254]); `KIMLIK-TEST` bunu denetler.
19. **Sayfa yürüyücüsü 1 GB (PDPT.PS) sayfayı tanımalı** — `vmm_get_physical`, `vmm_unmap_page`, TİMUR `find_pte` tanımıyordu.
21. **Arşivde derleme çıktısı yok** (ISO, ELF, `umay/kov/umay`, `userprog/shell.elf`): `bash kur.sh` hepsini üretir. `testler.sh` UMAY'ı kendisi derler (GNU_STACK denetimi için).
22. **Görev giriş işlevi geri dönerse** eskiden `task_trampoline` `cli; hlt` ile TÜM ÇEKİRDEĞİ öldürüyordu (tek çekirdekte sistem donar). Artık `task_exit` çağrılır. Yine de görev işlevleri `thread_exit()` ile bitmeli.
23. **`fat16_write_file` başarıda YAZILAN BAYT döndürür**, 0 değil. `!= 0 -> hata` varsayımı `SYS_WRITE_FILE`'ı her başarılı yazmada -1 döndürür yapmıştı (yarış sınaması yakaladı).
24. **Mutex bekleyen listesi doluysa** görev eklenmeden uyuyup SONSUZA DEK kalıyordu; artık yer açılana kadar işlemciyi bırakıp yeniden dener.
25. **Çekirdek-başına tampon kesilme altında paylaşılır:** syscall girişi `sti` yapar; aynı çekirdekte başka görev aynı tampona yazar → süreçler arası veri sızıntısı (tek çekirdekte bile). Syscall tamponları görev-başına.
26. **Dağıtım belirlenimci değil:** sınamada "4 çekirdeğin hepsi iş aldı" aranmaz (bir koşuda maske 0xB çıktı); "0 hata ve ≥2 çekirdek" aranır.
27. **Kilidin işe yaradığını kontrol grubuyla kanıtla** (kilit kapalı derleme aynı sınamada hata bulmalı), yoksa sınama kilidi ölçmüyor olabilir.
20. **Dışarıdan gelen syscall kodunu güvenlik kuralına göre denetle:** paket kullanıcı işaretçisini yalnız adres aralığıyla denetleyip doğrudan okuyordu (Ring3'ten panik). Her zaman `kullanici_dize_kopyala` / `kullaniciya_atomik_yaz` / `kullanicidan_atomik_oku`.
14. **Yapı yerleşimi değişince eski .o dosyaları eski ofsetle kalır** → Makefile artık `-MMD -MP` ile başlık bağımlılığı izliyor. Bu olmadan `cpu_yerel_t` değişikliği `simdiki_gorev`'i bir dizeyle ezdirdi ("OTUKENOS" deseni, `context_switch`'te #GP).
15. **`pkill -f qemu-system` aynı komut satırında kendi kabuğunu öldürür** (desen komut metninde geçer). `pkill -x qemu-system-x86` kullan; `kabuk_testi.py`'yi ayrı komutta koş.
16. **CPU-başına durum kurulum sırası:** `smp_baslat`'tan SONRA BSP'de kurulan her MSR/CR (syscall MSR'leri gibi) AP'de de kurulmalı.
11. **Takımlar araç oturumundan uzun sürer** → `setsid nohup ./takim.sh > log 2>&1 < /dev/null &`; yoksa arka plan süreci oturumla ölür.
12. **Kabuk çıktısı tek `puts` ile yazılmalı** (satır tamponu `satir_u`); parça yazım test desenlerini bozuyordu.
13. **ATA beklemeleri zaman tabanlı** (PIT ch2, ~5 sn); yineleme sayısına bağlı bekleme yük altında rastgele düşüyordu.
10. **Aynı disk imajını iki QEMU açamaz** (yazma kilidi); her çalıştırma için `cp diskfiles.img /tmp/xxx.img`.
28. **QEMU takımları koşarken ağır iş (eğitim) koşturma:** 2 çekirdekli makinede eğitim ile AĞIR sınama çakışınca iki koşu zaman aşımına düştü (TAKILDI); sessiz makinede aynı yapılandırmalar 6/6 geçti. Kanıt `kanitlar/surum146/agir_ara_*`.
29. **`pkill -f <betik adı>` kendi kabuğunu da öldürür** (15. dersin yinelemesi, bu kez eğitim betiğinde). Süreci PID ile durdur.
30. **Sınamalar /tmp'deki eski dosyalara bağımlı olmamalı:** BENGİ sınaması önceki koşunun dosyasını yükleyip "yinelenen" diye reddetti; sınama başında dosya silinir.
31. **Doğrulama kümesi sızıntısını ölç, varsayma:** 1.4.4 ayrımında doğrulama sorularının %28'i eğitimde "?"/büyük harf farkıyla vardı; "%24 genelleme" sızıntılıydı (sızıntısız: %7). `us1/arac/sizinti.py`.
32. **Kayıp ≠ yanıt niteliği:** geliştirme yanıt kaybıyla seçilen model sabit sınamada eskisinden KÖTÜ çıktı; seçim artık üretilen yanıtın F1'iyle.

---

33. **Uzun eğitim oturum boştayken ölebilir:** bulut kabı etkinlik yokken geri alındı, eğitim 4500. adımda kayboldu. `--devam` RNG ve erken durdurma durumunu geri yüklemez → denetimli karşılaştırmada baştan başlat; beklerken oturumu etkin tut.
34. **"Arttı" ≠ "anlamlı arttı":** 1.4.6 dağıtımının F1 kazancı eşli permütasyonla p = 0,66 çıktı. Karşılaştırmada `anlamlilik.py` koş.
35. **Belgeyi sınamanın gerçek kapsamına göre yaz:** kodda olan ama sınanmayan denetim "sınandı" diye yazılmaz.

36. **Sınama seri günlüğe bakar, göz ekrana:** 93/93 geçerken ekran bozuktu (renk kanalları kaymış, Türkçe harf yok, yarım kareler). Görsel değişiklikte `screendump` al ve bak.
37. **GRUB'un multiboot1 renk alanı 112. bayttadır, belirtimdeki 110 değil** (birlik 4 bayta hizalanır). Ham baytı ölçmeden yapıya güvenme. multiboot2 çevirici renk alanını hiç okumuyordu.
38. **Klavye baytı ≠ UTF-8:** klavye ISO-8859-9 verir (ı=0xFD); UMAY UTF-8 bekler. Sınır noktasında (pencere olayı, kabuk → UMAY) çevir. Eskiden "altı" UMAY'a "altý" gidiyordu.
39. **QEMU `sendkey` tuş adları:** nokta `dot`'tur (`period` sessizce yok sayılır). TR-Q'da ç=`dot`, ö=`comma`, ş=`semicolon`, ğ=`bracket_left`, ü=`bracket_right`, i=`apostrophe`, ı=`i`.

40. **Aynı ekrana iki birleştirici yazmasın:** gui.c ve ULGEN'in ayrı z-sıraları vardı; hangisi son çizdiyse üstteydi. Artık tek birleştirici (`src/ekran.c`), ortak z (`ulgen_z_ver`), hasar + kırpma. Pencere yerini değiştirmek bu sorunu yalnız gizler; `tests/ulgen_render` pencereleri bilerek üst üste bindirir.
41. **Uygulama yüzeyi yalnız SUNULUNCA ekrana çıkar:** her çizim çağrısında birleştirmek yarım kare gösterir (UMAY yan sütunu eksikti). Kontrol grubu: `-DULGEN_SUNUM_KAPALI`.
42. **Metni bayt sayısıyla bölme:** terminal (50 bayt) ve günlük (68 bayt) çok baytlı harfi ikiye bölüyordu; harf hücresi kullan.
43. **Tik frekansı 50 Hz'dir:** süreyi `timer_get_ticks()/100` ile hesaplama; `timer_hz()` kullan. Üst çubuk saati yarım sayıyordu, Sistem izleyicisi ile karşılaştırınca görüldü.
44. **Uygulamalar ulib.c'ye değil ulib_uyg.c'ye:** ulib.c değişirse gömülü kabuk ikilisi (shell.elf) değişir. Yeni kitaplık çağrıları `userprog/ulib_uyg.c` + `uyg.h`.
45. **Ortak durumlu protokol modülü = yarış:** DNS tek "son soru/son yanıt" tutuyordu; açılış sınaması ile Ring3 aynı anda sorunca `.invalid` sorusuna example.com'un adresi döndü. Artık soru yuvaları (kimlik + adla eşleşme). Sınama: AG-TEST eşanlı DNS.
46. **Gelen segmentin sağlama toplamını DOĞRULA:** TCP hiç doğrulamıyordu; sentetik sınama toplamı 0 olan segmenti "başarılı" sayıyordu. Artık bozuk segment düşer; sentetik sınama geçerli toplam üretir.
47. **Yinelenen ACK'ler aynı pencereyi taşımalı:** sıra dışı veri beklenirken pencere değişirse gönderen hızlı yeniden gönderimi tetiklemez, her kayıp bir RTO bekletir. Boşluk varken ilan edilen pencere sabit.
48. **Alt ağ dışı = ağ geçidi:** ulak_send_ipv4 hedefin kendisini ARP'lıyordu; internete hiç paket çıkmıyordu (DNS sunucusu aynı alt ağda olduğu için görünmüyordu).
49. **Ağ sınamaları:** `tests/ag/calistir.sh <tani.iso> <dizin> [smp] [slirp|tap]`. slirp'in TCP'si eski BSD (RTO ~1.5 sn): kayıp hızını TAP + Linux ile ölç (`-DOTUKEN_AG_KAYIP=N` konuk tarafında %N düşürür).
50. **Makefile'da blok değiştirirken genel kuralı silme:** `userprog/%.elf` kuralı bir düzenlemede silinmiş, ELF'ler önceden derli olduğu için fark edilmemişti. Temiz ağaçtan (`rm userprog/*.elf *.uo`) derleyerek dene.
51. **KERVAN imza anahtarı:** üretim özel anahtarı `anahtarlar/kervan_uretim.pem` (git DIŞI; Hakan'da yedeği olmalı). Sınama anahtarı `tests/kervan/sinama_anahtari.pem` depoda ve YALNIZ TANI yapısında geçerli. Anahtar değişirse: `araclar/kervan_paketle.py acik-anahtar-c ... > src/kervan_anahtar.c`.
52. **Dağıtılan güven listesine yerel kök girmesin:** `/etc/ssl/certs` bu makinenin vekil köklerini içeriyordu. Kökler yalnız `/usr/share/ca-certificates/mozilla`'dan.
53. **Hiç çağrılmayan sürücü, hiç sınanmamış sürücüdür:** `ses_init` yıllarca çağrılmadı; bağlanınca iki hata birden çıktı: 16 KB yazılan tampona 4 KB ayrılmıştı (ikinci tonda haritasız sayfaya yazıp PAGE FAULT), ve her `ses_cal` DMA'yı baştan başlatıp öncekini kesiyordu. Ses sınamasını AC'97 + `-audiodev wav` ile yap, WAV'ı ölç. Ses bağlanınca Dede Korkut UMAY'ın her yanıtını söylemeye başladı ve `SYS_DEDE_KONUS` konuşma bitene dek UMAY'ı bekletti (UYUM hepsi KALDI); konuşma artık eşzamansız sıra (`dede_konus_sirala`).
54. **Menü/üst çubuk ekrandan okunur:** birleştiricinin doğrudan çizdiği katmanlar (üst çubuk, Uygulamalar menüsü) yüzey değil; `sinama_ekran_ocr` VRAM'dan (`fb_ekran_pikseli`) okur. Pencere içeriği `sinama_satir_ocr` ile sunulan kareden.
55. **Sınama sırası bayraklarla:** ekranı kullanan sınamalar (UYGULAMA → AG/TULPAR → KERVAN → UYG) `*_bitti()` bayraklarını bekler; aynı anda tıklarlarsa birbirinin penceresine tıklar.
56. **Adlar:** UMAY yapay usun adıdır; KONUŞAN Dede Korkut'tur (ses, `src/dede.c`). Belgede ve arayüzde "UMAY söyler/konuşur" yazma: "UMAY'ın yanıtını Dede Korkut söyler".
57. **Yıkıcı çağrı, bütün argümanları 0 olan çağrıyla tetiklenmemeli:** ISKENCE sınaması her çağrıyı (0,0,0,0) ile çağırır; ilk sürümde SYS_ASENA_ONAR diski açılışta "onarıp" ASENA-TEST'in bozukluklarını siliyordu. Diske yazan/yeniden başlatan çağrılar artık `SYS_ONAY` sözcüğü ve çıktı yeri ister.
58. **Salt okunur bölümde değişen veri olmasın:** GDT `.rodata`'daydı; TSS tanımlayıcısı ve CPU erişim bitleri yazılıyordu. TAMGA'nın çekirdek özeti ana makinede hesaplanandan farklı çıktı; GDT `.data`'ya taşındı. Özet: `tests/tamga/ozet.py`.
59. **Bozuk diskte önce yazılırsa hasar büyür:** "boş" görünen ama bir zincirin ortasındaki küme, açılıştaki sınama yazımına verildi ve çapraz bağ doğdu. ASENA sınamasının KIRIK bozukluğu bu yüzden aralık dışı küme kullanır. Gerçek diskte de önce denetle, sonra yaz.
60. **ERGENEKON imajı:** `make iso` fabrika diskinin kök dosyalarını (`ERGENEKON_DOSYALAR`) üretim anahtarıyla imzalar; anahtar yoksa sınama anahtarıyla (yalnız TANI'da geçerli). GRUB `module /boot/ergenekon.krv ergenekon` (UEFI: `module2`); çekirdek modül çerçevelerini PMM'de ayırır.

---

## İlk mesajda senden beklenen

1. Ortamı kur, arşivi aç, `make && make iso`.
2. Takımları çalıştırıp 1.4.7 sonucunu doğrula: `./testler.sh` 97/97, `./donanim_testleri.sh` 43/43, `python3 kabuk_testi.py` 9/9, `make -C umay sina` 279/279, `tests/us1_conformance/calistir.sh` 17/17, `python3 tests/belge/umay_olculer.py denetle`. Geçmiyorsa önce onu düzelt.
3. Sonraki işler (öncelik sırası):
   - **Ring3 hata yalıtımı** (1.4.7'de yapılmadı): Ring3 kaynaklı hata yalnız o süreci sonlandırsın.
   - **İkinci güvenlik denetiminin devamı** (1.4.7'de yapılmadı); ağ yığını ve sürücüler hiç denetlenmedi.
   - `-Werror` (uyarı 0; açılabilir).
   - Rıhtım (Hakan'ın kararı): en çok kullanılan tamgalar varsayılan gelir, diğerleri tercihle seçilir. Uygulama ızgarasından ekleme/çıkarma (sürükle-bırak ve/veya sağ tık), seçim `RIHTIM.CFG`'ye (FAT16 kök) yazılır. Şimdilik sabit 7'li liste kalıyor (`src/ulgen_theme.c` RIHTIM[]); TAMGA'nın uygulaması yok. KERVAN → paket yöneticisi (1.4.9, imzalı .KRV, `src/kervan2.c`). TULPAR → ağ gezgini (1.4.9, `userprog/tulpar.c` + `html.c` + `http.c`). ÜLGEN → Görünüm (tema seçici), üst çubuk ÖTÜKEN/"Sistem" → Sistem izleyicisi (1.4.8, `src/uygulama.c`).
   - Uygulama ekleme yolu: `userprog/<ad>.c` (`uyg.h`), Makefile `UYG_ELF` + `uygulama-disk`, `src/uygulama.c` tablosuna satır (yol, profil), `include/tore.h`'de en az yetkili profil, `gui.c`'de tıklama. Sınama: `src/sinama148.c` (UYGULAMA-TEST, TANI).
   - US1: RoPE başka tohumlar/düzenlileştirmeyle; `--devam` kipine RNG ve erken durdurma durumunu ekle.
   - Çekirdek: bekleyen listesinde bloke görevi güvenle sonlandırma, shm kilidi, >16 GB RAM'de sabit sanal adres çakışması.
