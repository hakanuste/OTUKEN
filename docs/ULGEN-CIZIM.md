# ÜLGEN / GUI çizim hattı — 1.4.8 kök neden çözümlemesi

Tarih: 2026-09-28 · İlgili: `src/ekran.c`, `src/gui.c`, `src/ulgen.c`, `src/fb.c`, `src/ekran_sinama.c`, `tests/ulgen_render/`

## 1. Belirti

Aynı masaüstünün bir görüntüsünde:
- karakterler üst üste biniyordu;
- yazı pencere dışına taşıyordu;
- UMAY penceresi terminalin önüne ve arkasına sırayla geçiyordu;
- pencereler yanıp sönüyordu;
- Türkçe harfler yerinde "Ä", "±" ya da "?" görünüyordu.

## 2. Kök nedenler (sınamayla doğrulandı)

| # | Kök neden | Belirti | Denetim (önce) |
|---|---|---|---|
| 1 | **İki bağımsız birleştirici aynı çerçeve tamponuna yazıyordu.** gui.c (Terminal, Dosya, Görüntüleyici, Günlük; kendi `zorder[]`) ve ULGEN (UMAY vb.; kendi `z`). Ortak z-sırası yoktu. ULGEN, bir yüzeyi altında ne olduğunu bilmeden "üstüne boyuyordu". gui.c'nin tam çizimi ULGEN yüzeylerini siliyordu; yüzeyler ancak yarım saniyelik yenilemede geri geliyordu. | yanlış z-sırası, pencere yanıp sönmesi, üst üste binen içerik | B, C, D, G |
| 2 | **Terminal çıktısının kısa yolu:** terminal "GUI'nin kendi sırasında en üstte" ise yalnız o pencere doğrudan çiziliyordu. Bu, ULGEN yüzeylerinin (UMAY) üstüne boyamak demekti. | terminal UMAY'ın önüne çıkıyor | B, C |
| 3 | **Fare hedeflemesi yalnız GUI pencerelerine bakıyordu.** UMAY'a tıklamak altındaki terminali öne getiriyordu; UMAY hiç tık ya da kapatma olayı almıyordu. | yanlış pencere öne geliyor | E, E2 |
| 4 | **Kırpma yoktu.** Yazı ve satırlar pencere dikdörtgenine kırpılmıyordu. Terminal, penceresi ne kadar kısa olursa olsun sabit 20 satır çiziyordu; günlük satırları pencere kenarını aşıyordu. | pencere dışına taşma | A |
| 5 | **ULGEN taşınan ya da gizlenen yüzeyin arkasını yeniden çizmiyordu** (arka planı "GUI çizer" varsayımı). | hayalet pencere | F, H |
| 6 | **Metin BAYTLA bölünüyordu:** terminal satırı 50. baytta, günlük satırı 68. baytta kesiliyordu. Çok baytlı harf ikiye bölünüyor, iki bozuk glif çıkıyordu. | "dağ" → "daÄ" + "?" | C2 |
| 8 | **Uygulama yüzeyi yarım çizilmişken birleştiriliyordu.** UMAY karesini onlarca ayrı çağrıyla çizip `SYS_PEN_SUN` ile bitirir. Ama her çizim çağrısı yüzeyi "kirli" yapıyordu ve ana döngü onu o an ekrana alıyordu. `SYS_PEN_SUN` yüzey kimliğini hiç kullanmıyordu. Bu kök neden, düzeltmeden sonra alınan üretim ekran görüntüsünde bulundu: UMAY'ın ajan sütunu ve son satırları eksikti. | pencere içeriği eksik ya da yarım kare | K2 (kontrol grubuyla) |
| 7 | (önceki turda düzeltildi) doğrudan VRAM'e çizim: yarım kare ve titreme; renk alanının yanlış bayttan okunması. | | |

## 3. Düzeltme

- **Tek birleştirici (`src/ekran.c`).**
  - GUI pencereleri ve ULGEN yüzeyleri **tek bir z-sayacından** (`ulgen_z_ver`) z alır. Öne getirilen öğe, hangi sistemden olursa olsun, en üstte olur.
  - Değişiklik yalnız **hasar** olarak işaretlenir. `ekran_isle()` hasarlı bölgeyi şu sırayla yeniden çizer: masaüstü → bütün öğeler (z artan) → görev çubuğu → imleç.
  - **Her öğe kendi dikdörtgenine kırpılır.** GUI penceresi içeriği ayrıca iç alanına kırpılır.
  - Çift tampon sayesinde ara durum ekrana hiç çıkmaz.
- **Kırpma dikdörtgeni (`fb.c`):** `fb_kirp_*`. Bütün ilkeller (piksel, dikdörtgen, glif) buna uyar.
- **ULGEN:**
  - `ulgen_birlestir` kirli yüzeylerin çerçevesini hasarlar, çizimi birleştiriciye bırakır.
  - Taşıma, gizleme ve yok etme eski yeri de hasarlar.
  - Kapatma düğmesi artık çiziliyor (tıklanabilir yer vardı ama görünmüyordu).
- **Sunum (ULGEN):**
  - Uygulama yüzeyleri (`SYS_PENCERE_AC`) "elle sun" kipindedir: uygulama `tampon`a çizer; `SYS_PEN_SUN` tamamlanan kareyi tek seferde `sunulan`a kopyalar.
  - Birleştirici yalnız `sunulan`ı gösterir.
  - `SYS_PEN_SUN` artık yüzey kimliğini kullanır ve yalnız sahibinin sunmasına izin verir.
- **Fare:** tek hedefleme yolu, ortak z ile.
  - ULGEN yüzeyi en üstteyse o öne gelir.
  - Başlığa tık yüzeyi sürükler; kapatma düğmesi KAPAT olayı verir; içerik tıkı yüzey-yerel koordinatla TIK olayı verir.
- **Metin:**
  - Terminal artık **harf hücreleri** tutuyor. UTF-8 bayt bayt çözülüyor; yarım dizide bekleyen bayt ISO-8859-9 sayılıyor. Pencereye sığan son satırlar gösteriliyor.
  - Günlük satırı bayt sınırında kesilmiyor; fazlası kırpılıyor.
  - Görüntüleyici satır sonundaki yarım harfi atıyor.
- **Ana döngünün ekran adımı tek işlev:** `masaustu_tur`. Sınama da aynı işlevi çağırıyor.

UMAY ve OTAĞ kodu bu düzeltmede **değişmedi**.

Önceki turdaki UMAY değişikliği (konuşma satırlarını sütun genişliğinde bölmek) hâlâ gerekli. Nedeni: UMAY'ın konuşma alanı ile ajan sütunu **aynı yüzeyin içindedir**. ÜLGEN pencereyi kırpar, ama uygulamanın kendi iç bölgelerini bilemez.

Önceki turdaki masaüstü yerleşimi (pencereler üst üste binmiyor) yalnız bir görünüm tercihi. Sınama sahnesi pencereleri **bilerek üst üste bindirir** ve doğru çizimi o durumda kanıtlar.

## 4. Regresyon sınaması

`tests/ulgen_render/calistir.sh`: gerçek `fb.c`, `gui.c`, `ulgen.c`, `ekran.c`, `log.c` ve `fat16.c` ana makinede derlenir; bellekteki bir VBE tamponuna 4 düzende koşar:

| bpp | satır dolgusu |
|---|---|
| 32 | 0 |
| 32 | 64 bayt |
| 24 | 40 bayt |
| 16 | 0 |

Aynı sahne ve denetimler (`src/ekran_sinama.c`) `-DOTUKEN_EKRAN_SINAMA=1` ile çekirdekte, QEMU'nun gerçek VBE ekranında da koşar (`testler.sh` bölüm 15).

**Sahne (1024×768):**
- Terminal (40,40 · 424×300);
- Çekirdek günlüğü (x=0; dar: satırlar kırpılmalı);
- Dosya yöneticisi (sağ kenardan taşar);
- OTAĞ yüzeyi (x=0, y=0; başlığı ekran dışında);
- UMAY yüzeyi (terminalin üstüne biner);
- köşe yüzeyi (sağ-alt kenardan ve görev çubuğunun altından taşar).

Metinler uzun Türkçe UTF-8 içerir ve kırpma sınırlarına yerleştirilmiştir:
- x=0 ve y=0;
- 1023. sütun ve 767. satır;
- çok baytlı harf tam yüzey kenarında;
- satır kaydırma;
- saydam yazı.

Adımlar:
- A: ilk sahne;
- B: terminale yazma;
- C: terminal başlığına tık;
- D: UMAY öne;
- E: UMAY içeriğine tık (altında terminal var);
- F: OTAĞ taşındı;
- G: periyodik tam yenileme;
- H: köşe yüzeyi gizlendi;
- K: uygulama yüzeyi ("elle sun") için üç denetim:
  - K1: sunulan kare görünür;
  - K2: ikinci kare yarıdayken (sunulmadan) ekranda hâlâ birinci kare kalır, periyodik tam yenilemede de;
  - K3: sunulunca ikinci kare görünür.

**Kontrol grubu:** `-DULGEN_SUNUM_KAPALI` ile derlenen sürümde K2 **kalmalıdır**; kalmazsa sınama sunumu ölçmüyor demektir. `calistir.sh` bunu da denetler.

**Denetimler.** Çizim kodunu kullanmazlar. Harmanlama, UTF-8 çözümü, glif arama ve z-modeli sınamada ayrıca yazılmıştır; ortak olan yalnız glif verisidir.
1. **Piksel sahipliği:** her piksel, beklenen z-sırasındaki en üst öğenin renk kümesinde olmalı. Masaüstünde tam renk. Görev çubuğu her zaman üstte.
2. **Yazı:** kayıtlı her metin, göründüğü her pikselde beklenenle birebir eşleşmeli (yüzey, ekran ve örtme kırpması hesaplanarak).
3. **Glif okuma (OCR):** terminal ve günlük satırları glif glif okunur; yazılan metinle birebir eşleşmeli, bilinmeyen hücre olmamalı.
4. **Ekran belleği:** `fb_sun` sonrası VRAM = arka tampon. Satır dolgusu ve tampon sonrası nöbetçi baytlar ezilmemiş olmalı.
5. **Özet:** arka tampon FNV-1a özeti. Ana makine ile çekirdek aynı olmalı; QEMU ekran görüntüsü ana makine çizimiyle piksel piksel aynı olmalı.

**Sonuç:**
- önce: 10 denetim KALDI (`kanitlar/ulgen_render/once/`); sunum kontrol grubunda K2 KALDI;
- sonra: 4/4 düzen, bütün denetimler GEÇTİ;
- çekirdek (QEMU VBE) arka tampon özeti ana makineyle aynı;
- QEMU ekran görüntüsü ile ana makine çizimi arasında 0 farklı piksel (`testler.sh` bölüm 15; görüntüler `kanitlar/ulgen_render/sonra/`).
- Üretim masaüstü, iki ayrı açılış: `uretim_masaustu_1.png`, `uretim_masaustu_2.png`.

## 5. Tema bağlandı (1.4.8, ikinci adım)

Tema kodu (duvar kâğıdı, simgeler, üst çubuk, rıhtım) derleniyordu ama hiçbir yerden çağrılmıyordu. Birleştiriciye şöyle bağlandı:

- **Masaüstü katmanı:** duvar kâğıdı, simgeler ve rıhtım açılışta ve `tema` komutunda bir kez çizilip bellekte tutulur (`ekran_masaustu_hazirla`). Birleştirici arka planı hasarlı bölge kadar bu katmandan kopyalar (`fb_blit`).
- **Üst çubuk** görev çubuğu gibi her zaman üsttedir; çalışma süresi saniyede bir yenilenir. Uygulama pencereleri (`SYS_PENCERE_AC`) üst çubuğun altında başlar.
- **Katman kırpması:** üst çubuk ve görev çubuğu da kendi dikdörtgenlerine kırpılır. Ekran sınaması, üst çubuktaki tamga görselinin çubuğun altına (pencerelerin üstüne) taştığını yakaladı; bu kırpma o hatayı düzeltti.
- **Tıklamalar:**
  - rıhtımda UMAY, BİTİG (dosyalar) ve ORKUN (kabuk) ilgili pencereyi öne getirir;
  - masaüstü simgeleri dosya yöneticisini açar.
  - Eski `ulgen_theme_dock_hit`, rıhtım sırası yerine 15 tamganın tümüne göre ve farklı payla hesaplıyor, yanlış tamga döndürüyordu. Çizim ve tıklama artık aynı yerleşim işlevini kullanıyor.
- **Görsel kusurlar:**
  - duvar kâğıdının sol kenarındaki basılı arayüz kalıntısı ölçeklemede kırpılır;
  - küçük harfli yazılar DejaVu ile çizilir (Orbitron'un küçük harfleri bozuktu: "Belgeler" → "Beloeler");
  - rıhtım 28 piksellik görev çubuğunun üstüne yerleşir.

**Sınama:**
- arka plan pikselleri masaüstü katmanıyla birebir;
- üst çubuk her adımda sahne başındaki anlık görüntüyle birebir (saat sınamada sabitlenir);
- yeni L1–L5 adımları: rıhtımdan ORKUN, BİTİG, UMAY; masaüstü simgesi.
