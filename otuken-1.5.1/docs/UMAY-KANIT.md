# UMAY KANIT katmanı: "kanıt yeterli mi?"

Sürüm 1.4.6 · Kod: `umay/core/umay_kanit.c`, `umay/include/umay_kanit.h`, `umay/us/umay_us.c`
Ölçüm: `tests/umay_kanit/calistir.sh` (tek komut) · Bağlılık eşiği: `tests/umay_kanit/baglilik_esik.py`

## 1. Sorun (1.4.5)

- Motor, soruya **herhangi bir** kütüphane kaydı değdiğinde çağrılıyordu. "kerpiç duvar nasıl sıvanır" sorusu yalnız "duvar" sözcüğüyle "Sera, kümes ve biyolojik duvar" kaydına gidiyordu. Model de oradan ilgisiz bir cümle söylüyordu.
- US1 modeli bağlamı hiç okumuyor. İstem yalnız `S: <soru>\nU: ` kalıbı. Model ezberlediği bir eğitim cümlesini söylüyor.
- Alan dışı sorularda model kendi başına "bilmiyorum" diyemiyordu: 0/10.
- Sızıntısız ölçümde durum şöyle (`us1/arac/olc.py`, 310 alan dışı soru):
  - model yalnız **%2,6** oranında ret ediyor;
  - **%78,7** oranında bir eğitim cümlesini aynen söylüyor.

## 2. Karar yolu

```
soru -> OZAN (niyet, alan) -> dayanak arama (kanıt kipi)
     -> KANIT ölçüsü --YETERSİZ--> "Bunu doğrulayacak yeterli bilgim yok. İstersen araştırabilirim."
                    |               (+ neden: hangi terim yok / kapsama yüzde kaç; motor ÇAĞRILMAZ)
                    +--YETERLİ--> motor (US1) -> BAĞLILIK denetimi --bağlı değil--> kaynaktaki cümle(ler) AYNEN
                                                                  +--bağlı-------> modelin sözü
```

Hiçbir yerde soru ya da konu listesi yoktur. Atılan sözcükler yalnız **dilbilgiseldir**:

- soru adıllarının bütün durum ekli biçimleri: ne/neyi/neyle…, kim…, nere…, hangi…, kaç…;
- soru eki;
- istek/buyruk kalıpları: anlat, araştır…;
- kip yardımcıları: olur, gerekir…

### 2.1 Terim ağırlığı

Terimin kütüphanede kaç kayıtta geçtiğine (df) göre basamaklıdır:

| df | Ağırlık |
|---|---|
| 0 | 400 |
| 1 | 300 |
| 2–3 | 200 |
| kayıtların ≤ ¼'ü | 120 |
| daha yaygın | 60 |

Kütüphanede hiç geçmeyen terim en ağırdır. Bu, alan dışı sorunun işaretidir.

### 2.2 Eşleme (`kanit_esles`)

Türkçe ekler sona gelir, kök başta kalır. Bu yüzden eşleme şöyledir:

- iki sözcük de ≥ 5 harfse ilk 5 harf aynı olmalı (5. harfte ünsüz yumuşaması serbest: kireç ~ kireci);
- kısa olan 3–4 harfse, uzun olan onunla başlamalı ve ardından ünlüyle başlayan en çok 2 harflik ek gelmeli:
  - eşleşir: harç ~ harcı, taş ~ taşın;
  - eşleşmez: taş ~ taşıma, kum ~ kumanda.

### 2.3 Kapsama, odak, hüküm

- **Kapsama:** dayanakta karşılanan terimlerin ağırlıklı payı. Dayanak = başlık + etiket + alıntı + doğrulanmış BENGİ.
- **Odak:** en ağır terim. Dayanakta geçmek **zorundadır**.
- **Yüklem:** Türkçede soru yüklemle biter. SON terim, dört koşulun hepsini sağlıyorsa yüklem adayıdır:
  1. sorunun son sözcüğüdür (ardından yalnız soru eki gelebilir);
  2. kütüphanede hiç geçmez;
  3. çekimli eylem ekiyle biter ("durur", "sıvanır", "yağacak mı");
  4. sorunun başka terimi de vardır.

  Yüklem adayı odak olamaz ve ağırlığı `KANIT_YUKLEM_AGIRLIK`'tır. Aynı kural ulaç ekli terime de uygulanır ("örerken", "kullanarak"). Ek listesi dilbilgisi çizelgesinden gelir; adlarla çakışan biçimler bilerek alınmadı (-DIn: altın; -mAm: tamam).
- **Hüküm:** `yeterli` = dayanak var **ve** kapsama ≥ `KANIT_KAPSAMA_ESIK` **ve** odak karşılandı.

### 2.4 Dayanak arama (kanıt kipi)

- Yalnız terimlerle ve kök eşlemeyle aranır, ağırlık toplamıyla sıralanır.
- Uzmanın kendi kategorisi kanıtı karşılamıyorsa genel depo da denenir. Alan yönlendirmesi yanlış olabilir: "musso motoru …" sorusu BALBAL'a gitti, dayanak TULGA'dan geldi. Bu durumda kullanıcıya "genel depoya indim" denir ve güven ZAYIF'ı geçmez.
- **Bulunup düzeltilen iki hata:**
  - kayıt başına 96 sözcüklük dizin taşınca kalan sözcükler hiç aranmıyordu ("Çarklar" kaydının son 9 sözcüğü);
  - başlık ve etiket sözcükleri arama ve sıklıkta yoktu, oysa kanıt onları okuyordu ("enerji").

### 2.5 Meclis

Her uzman yalnız **payına düşeni** söyler: dayanağı, sorunun genel olmayan en az bir terimini karşılamalıdır. Bütünün hükmü, uzmanların karşıladığı terimlerin **birleşimiyle** verilir (`kanit_birlestir`). Birleşim yetmiyorsa özet şunu söyler: "Bunu bütünüyle doğrulayacak yeterli bilgim yok: "X" hiçbir uzmanın kaynağında geçmiyor." Ortak güven YOK olur.

### 2.6 Bağlılık (üretken motor)

`US_YETENEK_URETKEN` bayraklı motorların (US1 yerel, IPC) sözü ölçülür: içerik sözcüklerinin dayanakta geçen payına bakılır. Pay `KANIT_BAGLILIK_ESIK` altındaysa:

- söz atılır;
- dayanaktan önce soru terimlerini en ağır taşıyan cümle, sonra onun karşılamadığı terimleri taşıyan ikinci cümle **aynen** verilir;
- "Modelin sözü dayanağa bağlı değildi" uyarısı yazılır.

Stub motor zaten alıntı yapar ve denetlenmez.

## 3. Ölçüm düzeni (`tests/umay_kanit/olc.py`)

Kümeler üretilir; hiçbiri elle yazılmış soru listesi değildir. Geliştirme ve sınama ayrı tutulur:

| Küme | Ne | Ayrım birimi |
|---|---|---|
| ic | kütüphane cümlelerinin kendi sözcükleriyle soru | KAYIT (aynı kayıt iki yanda yok) |
| ic_ey | aynı + sona UD'den gerçek çekimli yüklem ("… düşünemeyiz") | KAYIT |
| dis / dis_ey / dis_yalin | UD Türkçe ağaç bankalarındaki ad/sıfat/özel adlardan kütüphanede HİÇ geçmeyenler (C'nin kendi eşlemesiyle df=0) | SÖZCÜK |
| zor / zor_ey / zor_yalin | bir kütüphane sözcüğü + bir yabancı ad ("zeytin <yabancı>") | SÖZCÜK |
| örnek | yönergedeki 5 alan dışı soru | yalnız raporlanır, seçime girmez |

Ayrıntılar:

- **Kaynak:** UD ağaç bankaları (CC BY-SA). IMST ticari olmayan lisanslı olduğu için kullanılmadı.
- **Sızıntı denetimi:** iki yan arasında ortak soru 0, ortak kayıt 0.
- **Karar denetimi:** C'nin kararı, döktüğü terim ayrıntısından Python'da **aynı formülle** yeniden hesaplanır; uyumsuz 0.
- **Seçim kuralı (güvenlik önce):** her alan dışı kümede yanlış kabul ≤ %1 olmalı. Bu koşulu sağlayanlar içinden alan içi doğru yanıt oranı en yüksek olan seçilir. Eşitlikte daha tutucu olan seçilir.

### 3.1 Sonuç

Geliştirme kümesinde seçildi: **E = 85, Y = 40**. Sınama kümesi (108+108 alan içi, 6×400 alan dışı):

| Ölçüt | Sınama |
|---|---|
| alan içi, doğru kayıt dayanakta ve yanıt (ic) | **%95,4** |
| alan içi, yüklemli soru (ic_ey) | **%80,6** |
| alan dışı ret: dis / dis_ey / dis_yalin | %100 / %100 / %100 |
| zor ret: zor / zor_ey / zor_yalin | %100 / %100 / **%99,5** |
| yönerge örnekleri (kuantum, basketbol, bitcoin, yağmur, telefon) | **5/5 ret** |

**Değiş tokuş** (dürüst not): tek bir adın ardından eylem gelen kısa sorular ("radar açabilirsiniz") ile iki sözcüklük "bilinen sözcük + bilinmeyen ad" soruları ("güneşin krizler") aynı biçimdedir. Güvenlik kısıtı ikincisini seçtirdi. Bu yüzden ilk tür kısa alan içi sorular bazen reddediliyor; ic_ey sonucunun %80,6'da kalmasının nedeni budur.

### 3.2 Bağlılık eşiği

Kullanılan veriler:

- gerçek US1 modelinin **ham** sözleri (410 alan içi soru);
- bağlılık 20–89 bandındaki 59 söz, elle etiketlendi (Claude): okunur mu, dayanak onu destekliyor mu; bkz. `baglilik_etiket.tsv`.

Eşik, geliştirme satırlarında F1 ile seçildi: **60**. Sınama satırlarında kesinlik 0,86, duyarlılık 0,60 (tp 6, fp 1, fn 4). Örneklem küçüktür; eşik kaba bir seçimdir.

Aynı ölçümün bulduğu şey: US1'in ham sözlerinin yalnız **~%18**'i dayanağa bağlı (≥ 50). Kalanı ya bozuk ya da başka kayıttan ezber. Doğru yanıtın asıl kaynağı özütleme; model değil. Tur 2 ile dağıtılan yeni model (`docs/UMAY-EGITIM.md` §D) aynı 410 soruda: bağlılık ≥ %60 olan söz %16 → **%22**.

## 4. ÖTÜKEN içinde (1.4.6 nihai ölçüm: yeni model, 4 çekirdek; `kanitlar/surum146/umay_yanitlari_taban_s4.txt`)

| Soru | UMAY | Açıklama |
|---|---|---|
| kerpiç duvar nasıl sıvanır | "Bunu doğrulayacak yeterli bilgim yok. İstersen araştırabilirim. En yakın kayıt (Doğal yapı ve yer altı kileri) sorunun odağı olan "kerpiç" terimine değmiyor; uydurmam." | motor çağrılmadı |
| yer altı kileri kaç derece | "Yer altı kileri Horasan harcı bağlayıcılı olup yıl boyu 12-15 derece sabit sıcaklık ve …" | model sözü bağlı değildi (%33), kaynak cümlesi verildi |
| zeytin budama atığından ne yapılır | "Zeytin budama atığından biyokömür biochar üretilir." (meclis: YER-SU + BALBAL) | model sözü bağlı değildi, kaynak cümlesi verildi |
| sarnıç suyu nasıl temiz kalır | "Yağmur suyu hasadı ve yeraltı suyu için Horasan harcı sıvalı toprak altı kadim sarnıç yapılır." Güven: zayıf | model sözü bağlıydı; §5'teki sınır: sorunun "temiz" yanını karşılamıyor |

**Not:** 1.4.4 modeli ilk iki alan içi soruda kendi sözünü doğru kuruyordu, yeni model kuramadı. Her iki durumda da UMAY'ın verdiği yanıt doğrudur, çünkü kaynak cümlesi verildi. Soru soru karışık sonuç, sınama kümesindeki "23 daha iyi / 32 daha kötü" tablosuyla tutarlı (`docs/UMAY-EGITIM.md` §D).

## 5. Sınırlar

- **Sözcük düzeyi bir ölçüdür, anlam denetimi değildir.** "sarnıç suyu nasıl temiz kalır" sorusunda kapsama %94 çıkıyor ve yanıt veriliyor. Ama "temiz" terimi gölet kaydından geliyor; kütüphane sarnıç suyunun nasıl temiz tutulacağını söylemiyor. UMAY iki ayrı kaydın cümlesini verir, genel depodan geldiği için "Güven: zayıf" der.
- Eylem eki kuralı biçime dayanır. Kütüphanede olmayan ve eylem gibi biten bir ad ("şeker") yüklem sanılabilir; yalnız sorunun son sözcüğüyse etkilenir.
- Ağırlık basamakları kütüphane büyüdükçe değişir (df/N). Eşikler bugünkü 24 kayıtlık kütüphanede seçildi; kütüphane büyüyünce `tests/umay_kanit/calistir.sh` yeniden koşturulmalı.
