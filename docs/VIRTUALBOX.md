# ÖTÜKEN + UMAY'ı VirtualBox'ta çalıştırma (Intel Mac)

Paket: `otuken-vbox.zip`. İçinde:

| Dosya | Ne |
|---|---|
| `otukenos-mini.iso` | ÖTÜKEN çekirdeği (GRUB, BIOS açılışı) |
| `otuken_disk.vdi` | FAT16 disk: `UMAY.ELF`, model `UMAY.US1`, `SOZLUK.TXT`, belgeler |
| `otuken_vbox_kur.sh` | Sanal makineyi doğru donanımla kurup başlatır |
| `beklenen_ekran.png` | Açılıştan sonra görmen gereken ekran |
| `uygulamalar_menusu.png` | Üst çubuktaki Uygulamalar menüsü açıkken |
| `dede_ornek_ses.wav` | DEDE KORKUT'un "Damlaya damlaya göl olur." sözü (QEMU kaydı) |
| `KURULUM.md` | Bu belge |

## 1. VirtualBox'ı kur

virtualbox.org → Downloads → **macOS / Intel hosts**. Mac'in Intel işlemcili (MacBook Air 2017) olduğu için Intel sürümü gerekir. Kurulumdan sonra macOS "Sistem Ayarları → Güvenlik ve Gizlilik" bölümünde Oracle'ın sistem uzantısına izin istemesi olağandır; izin ver ve gerekirse Mac'i yeniden başlat.

## 2. Makineyi kur ve başlat

Zip'i aç, Terminal'de o dizine geç:

```bash
cd ~/Downloads/otuken-vbox
bash otuken_vbox_kur.sh
```

Betik `OTUKEN` adlı makineyi kurar ve açar (1 çekirdek, 1 GB). Başka ayar için: `bash otuken_vbox_kur.sh OTUKEN2 2 2048` (ad, çekirdek, MB).

Betiğin seçtiği donanım ÖTÜKEN'in sınandığı donanımdır. Elle kurarsan aynılarını seç:

- Tür: Other / Unknown (64-bit), **BIOS** (EFI değil), yonga seti PIIX3
- Depolama: **IDE denetleyici (PIIX4)**. Birincil ana (port 0): `otuken_disk.vdi`. İkincil ana (port 1): `otukenos-mini.iso`
- Klavye/fare: **PS/2**. Ekran: **VBoxVGA**, 16 MB
- Ağ: NAT, kart **Intel PRO/1000 MT Desktop (82540EM)**
- Seri kapı 1: COM1, kip "Ham dosya" → bir `.log` dosyası
- `VBoxManage modifyvm OTUKEN --x2apic off` (x2APIC yolu sınanmadı)

SATA/AHCI denetleyiciye takılan disk **okunmaz** (çekirdek diski ATA/IDE yoluyla okur); UMAY o zaman "program diskte bulunamadi" der.

## 3. Ne görmelisin

GRUB bekletmeden çekirdeği açar. Birkaç saniye sonra temalı masaüstü gelir:
- **Üstte:** ÖTÜKEN çubuğu, sağında tarih ve saat (Türkiye saati).
- **Solda:** masaüstü simgeleri.
- **Ortada:** solda Terminal; UMAY'ın ajanları yüklenince sağda **UMAY — Ötüken** penceresi.
- **Altta:** 7 tamgalı rıhtım ve görev çubuğu.

Rıhtımdaki **UMAY**, **BİTİG** (dosyalar) ve **ORKUN** (kabuk) ilgili pencereyi öne getirir; **ÜLGEN** tema seçiciyi (Görünüm) açar. Üst çubukta ÖTÜKEN tamgası ya da **Sistem** dizge izleyicisini açar. Üst çubuktaki **Uygulamalar** menüsü bütün tamgaları listeler: **ULAK** (ağ durumu), **DEDE KORKUT** (atasözü ve yazdığın sözü sesli söyler), **TÖRE** (hangi programın hangi yetkisi var), **TİMUR** (güvenlik korumaları; kapalı olan "kapalı" yazar), **KUT** (kullanıcılar), **KERVAN** (paketler), **ASENA** (diski denetler ve onarır), **ERGENEKON** (dizgeyi ISO'daki imzalı imajdan yeniden kurar), **TAMGA** (çekirdeğin ve anahtarların kimliği, dosya mührü), **TULPAR** ve diğerleri.

**Bir şey bozulursa:** önce ASENA → "Denetle"; sorun varsa "Onar" (iki kez). Program açılmıyorsa ERGENEKON → "Dizgeyi yenile" (senin dosyaların kalır). Hiçbiri olmazsa ERGENEKON → "Sıfırdan kur" (EVET yaz; diskteki HER ŞEY silinir, dizge ISO'dan yeniden yazılır) → "Yeniden başlat". Uygulama pencereleri başlıktaki kırmızı **x** ile kapanır. Çekirdek günlüğü görev çubuğundaki "Günlük" düğmesiyle açılır. Bkz. `beklenen_ekran.png`.

Fareyi ve klavyeyi makineye vermek için pencereye tıkla; geri almak için **sol ⌘** (VirtualBox "host" tuşu).

## 4. Kullanım

Klavye düzeni **Türkçe-Q**'dur (Mac'in Türkçe-Q klavyesiyle aynı tuş yerleri).

Kabuk (TERMINAL):

| Komut | İş |
|---|---|
| `yardim` | komut listesi |
| `listele` | kök dizin |
| `goster OTUKEN.TXT` | dosya içeriği |
| `kimlik`, `saat`, `bellek`, `servisler` | sistem bilgisi |
| `umay <soru>` | UMAY'a IPC ile soru (ör. `umay durum`, `umay yer altı kileri kaç derece`) |

**TULPAR (ağ gezgini):** rıhtımdaki TULPAR (kanatlı at) tamgası. Adres çubuğuna tıkla (ya da F6), adresi yaz, Enter. Nokta içermeyen ya da boşluklu yazı DuckDuckGo'da aranır. Bağlantılara tıkla; ↑↓ PgUp/PgDn kaydırır, ←→ bağlantı seçer, Backspace geri, F5 yeniler. HTTPS sertifikası doğrulanamayan site açılmaz. Metin tabanlıdır: JavaScript, resim ve sayfa biçimi yoktur. Türkçe-Q klavyede adres için: `:` = Shift+nokta tuşu, `/` = Shift+7, `@` = AltGr+Q.

**UMAY penceresinde konuşmak:** `Tab` tuşu klavyeyi UMAY penceresine geçirir; soruyu yazıp Enter. `Tab` ile kabuğa dönülür.

Örnek sorular: `merhaba`, `yer altı kileri kaç derece`, `zeytin budama atığından ne yapılır`, `kuantum bilgisayar nedir` (bu sonuncusu kütüphanede yok: UMAY "yeterli bilgim yok" demeli).

## 5. Bir şey ters giderse

Önce `otuken_seri.log` dosyasına bak (çekirdeğin bütün açılış günlüğü). Bana gönderirsen oradan hatayı bulurum. Önemli satırlar:

- `framebuffer adres ... R/G/B konum` — ekran kipi
- `disk: ATA birincil master hazir` — disk bulundu
- `UMAY-HOST: '/UMAY.ELF' diskten okundu` — UMAY yüklendi
- `IPC kapısı açık: umay` — UMAY soru bekliyor
- `PANIGI` / `PAGE FAULT` — çöküş

| Belirti | Olası neden |
|---|---|
| Siyah ekran, günlük boş | EFI açık kalmış (BIOS seç) ya da ISO takılı değil |
| Masaüstü var, UMAY yok; günlükte "diskte bulunamadi" | Disk IDE yerine SATA'ya takılmış |
| Tuşlar çalışmıyor | Klavye USB seçilmiş (PS/2 seç) ya da pencereye tıklanmadı |
| 2+ çekirdekte donma | Önce 1 çekirdekle dene; `--x2apic off` olduğundan emin ol |

## 6. Bilinen sınırlar (dürüst)

- **Gerçek VirtualBox'ta henüz denenmedi.** Aynı donanım QEMU'da (i440FX, Broadwell işlemci, IDE, PS/2, 82540EM, 1 GB, 1 ve 2 çekirdek) bu VDI diskle sınandı: kabuk + UMAY 9/9. VirtualBox'ın VBE ve APIC öykünmesi QEMU'dan farklı olabilir; ilk açılışta 1 çekirdekle başla.
- Ekran 1024×768 sabit. Yazı 8×16, kenarı yumuşatılmış (DejaVu Sans Mono); Türkçe harfler çizilir.
- Terminal penceresinde uzun satırlar sözcük ortasından bölünür; UMAY'ın kendi iletileri de terminale düşer.
- **Ağ ve TULPAR gerçek internette henüz denenmedi.** QEMU'da yerel sunucuya karşı sınandı (HTTP, HTTPS, %5 paket kaybı, Linux TCP'si ile TAP). VirtualBox NAT'ta ilk deneme sende olacak; sorun olursa `otuken_seri.log`'daki `ULAK` ve `UYGULAMA` satırlarını gönder.
- **Ses:** betik AC'97 ses kartını açar (macOS coreaudio). Konuşan Dede Korkut'tur (UMAY yapay usun adı): ses kartı varken UMAY'ın yanıtlarını ve DEDE KORKUT penceresindeki sözleri Dede Korkut söyler. Ses QEMU'da WAV'a kaydedilerek sınandı; VirtualBox'ta ve Mac hoparlöründe duyulup duyulmadığını ilk sen göreceksin. Sesi istemezsen: `VBoxManage modifyvm OTUKEN --audio-enabled off`.
- HTTPS için sanal işlemcide **RDRAND/RDSEED** olmalı (MacBook Air 2017'nin işlemcisinde var). TULPAR "donanım rastgele sayı üreteci yok" derse: `VBoxManage modifyvm OTUKEN --cpu-profile host`.
- HTTPS sertifika tarihini sanal makinenin saatine göre denetler: betik saati UTC verir (`--rtcuseutc on`). Saat yanlışsa siteler "süresi dolmuş" diye reddedilir.
- BENGİ (kalıcı hafıza) diske yazmaz; makine kapanınca unutulur.
