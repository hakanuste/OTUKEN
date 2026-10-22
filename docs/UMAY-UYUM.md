# ÖTÜKEN ↔ UMAY UYUM RAPORU (1.4.3)

Tarih: 2026-09-27 · QEMU 8.2.2 (TCG) · Kanıtlar: `kanitlar/uyum/ozet.txt`, `kanitlar/uyum-kayitlari.tar.xz`

## 1. Yöntem

Uyum iki katmanda denetlendi:

- **Statik:** çekirdeğin `include/syscall.h`, `include/ipc2.h`, `include/tore.h` ve `src/timer.c` dosyaları, UMAY'in taşıma katmanıyla (`umay/port/umay_port_otuken.c`) ve kanal/yanıt yoluyla (`umay/core/*`) satır satır karşılaştırıldı. Karşılaştırılanlar:
  - çağrı numaraları,
  - yapı düzenleri: `ipc_message_t`, `sys_stat_t`, `sys_surec_t`, `sys_olay_t`,
  - TÖRE yetenek bitleri,
  - zaman birimi,
  - IPC anlamı: gönderen, sahiplik, dolu kuyruk, parçalama.
- **Devingen:** yeni `umay_uyum_sinamasi()` sınaması (TANI kipi, `OTUKEN_UMAY_UYUM=1`) ve `uyum_sinama.sh` matrisi.

## 2. Birebir uyumlu çıkanlar

| Konu | Durum |
|---|---|
| Çağrı numaraları 0–35 | Birebir aynı. UMAY 34'ü (TEMA) kullanmıyor. Port başlığındaki "0..31" yorumu eskiydi, düzeltildi. |
| `ipc_message_t` (4×u32 + 96 bayt yük) | Aynı |
| `sys_stat_t`, `sys_surec_t`, `sys_olay_t` | Aynı; olay türleri 1/2/3 eşleşiyor |
| TÖRE bitleri (`KUT_*`) | Aynı |
| 4. argümanın R10'da olması | Doğru |
| Ring3 UMAY.ELF derlemesi | Tekrarlanabilir; iki derleme bayt bayt aynı çıktı |

## 3. Bulunan uyumsuzluklar ve düzeltmeleri

| # | Sorun | Etkisi | Düzeltme |
|---|---|---|---|
| 1 | UMAY bir tiki **10 ms** sayıyordu; çekirdek PIT'i **50 Hz** (20 ms) kuruyor | UMAY'in saati yarı hızda işliyordu; her `up_uyu` ve IPC beklemesi iki kat uzun sürüyordu | Çekirdek `SYS_SISTEM_BIL(SYSBIL_TIK_HZ=4)` ile frekansı bildiriyor, UMAY buradan hesaplıyor (eski çekirdekte 50 Hz varsayılıyor) |
| 2 | `SYS_IPC_GONDER` göndereni **sabit "kabuk" portu** yazıyordu | Kabuk dışındaki bir Ring3 istemcinin sorusunun yanıtı kabuğa gidiyordu | Göndereni çağıranın kendi portu yapıyor; portu yoksa `IPC_GONDEREN_YOK` yazılıyor (0 geçerli bir kimlik) |
| 3 | UMAY eşzamansız görev sonucunu **"en son gönderene"** yolluyordu | İki istemci aynı anda sorunca yanıtlar karışıyordu. Portu bilinmeyen gönderende UMAY yanıtı kendi portuna yazıyor, sonra kendi yanıtını soru sanıyordu | `sonuc_t` ve `meclis_t` soruyu getiren kanalı/kaynağı taşıyor; yanıt `yaz_kime` ile sorana gidiyor |
| 4 | Herhangi bir görev herhangi bir portu boşaltabiliyordu | Başka bir görev UMAY'in yanıtını alabiliyordu | `SYS_IPC_AL` yalnızca portun sahibinden kabul ediliyor |
| 5 | Ölen görevin portu kapanmıyordu | 8 yuvalı tablo doluyordu; yeniden başlayan servis aynı adı alamıyordu | Görev reap edilince portları kapanıyor (`ipc_gorev_portlarini_kapat`) |
| 6 | Kuyruk dolunca (8 ileti) UMAY ilk hatada vazgeçiyordu | 8 parçadan uzun yanıt, karşı taraf okurken bile yarım kalıyordu | `ipc_send` dolu kuyrukta `IPC_DOLU (-2)` dönüyor; UMAY 2 saniyeye kadar yeniden deniyor |
| 7 | Kabuk UMAY'i yalnızca ~6 saniye bekliyordu | Yük altında yanıt 15,5 saniyeyi buldu; kabuk "zaman aşımı" deyip yanıtı kuyrukta bırakıyordu, yanıt bir sonraki soruya karışıyordu | Bekleme sınırı 20 saniye; her gelen parçada süre yenileniyor |

## 4. Devingen sınama

3 çekirdek görevi, her biri kendi portundan, UMAY'e aynı anda 12'şer soru sorar (toplam 36). Her soru için tam yanıtı (son parça opkod 0) bekler. Sorular yalnızca okuma yapar; onay isteyen yazma/onarım soruları yoktur.

Her istemcinin aynı anda tek açık sorusu olduğu için yanlış yönlendirme iki izle görünür:
- soranda zaman aşımı,
- başka bir istemcide sondaki boşaltmada "fazla" ileti.

| Koşu | Sonuç |
|---|---|
| **Kontrol grubu** (UMAY `-DUMAY_ESKI_YONLENDIRME`, 2 koşu) | 2/2 **KALDI**, beklendiği gibi: 1. koşu 3 zaman aşımı + 4 fazla, 2. koşu 3 zaman aşımı + 6 fazla. Yanıtlar öteki istemcilere düştü |
| Taban (yalnızca uyum, -smp 4) | 2/2 GEÇTİ, 36/36 |
| Uyum + AĞIR (32 tur) aynı anda, -smp 1/2/4/8 × 2 | 8/8 GEÇTİ, 36/36, AĞIR da GEÇTİ |
| **Toplam** | **10/10 geçti**, 0 takıldı |

**Gecikme** (en kötü, tek soru): tabanda ~8 s; AĞIR ile birlikte -smp 8'de 15,5 s.

**Profil** (QEMU monitörüyle RIP örneklemesi): UMAY'in işlemci süresinin çoğu US1 çıkarımında (`matcarp`, `us1_ileri`). Gecikme uyumsuzluktan değil, SSE'siz tamsayı çıkarımının TCG öykünmesinde koşmasından geliyor. Gerçek donanımda çok daha kısa olması beklenir; bu **test edilmedi**.

## 5. Gerileme

Düzeltmelerden sonra bütün takımlar yeniden koştu. Sonuçlar `kanitlar/` altındaki dosyalarda.

## 6. Sınanmayanlar

- Kabuk dışında bir **Ring3** istemcinin UMAY'le konuşması. Düzeltme 2 (gerçek gönderen) Ring3 yolunda yalnızca kabukla (`kabuk_testi.py`) sınandı; devingen sınamanın istemcileri çekirdek görevleri, göndereni kendileri yazıyorlar.
- 8 parçadan uzun (768 bayttan uzun) bir UMAY yanıtının dolu kuyrukta yeniden denenmesi. Seçilen sorularda yanıtlar soru başına ortalama 2,2–2,5 parça tuttu (istemci başına 12 soruda 26–30 parça). Tek yanıtın en çok kaç parça olduğu ölçülmedi.
- Port kapandıktan hemen sonra aynı kimliği başka bir görev alırsa, eski gönderenin iletisi yeni sahibe düşebilir. Bu dar pencere kapatılmadı; bunun için kimliğe kuşak sayacı eklemek gerekir.
- Gerçek donanım, UEFI.
