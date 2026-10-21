# UMAY — AJANLARIN ARAŞTIRMA GÖREVLERİ (1.4.5)

Kanıtlar: `kanitlar/arastirma/` (sınama 39, ÖTÜKEN içindeki raporlar)

## 1. İnceleme: önceki durum

- 12 uzman (AKÇA … YAZIT) ve BİLGE, `GT_ARASTIR` görevini kabul ediyordu. Ancak araştırma, **sıradan bir soru gibi** işleniyordu: tek bir kütüphane araması (en çok 3 kayıt) ve tek bir yanıt.
- `GT_ARASTIR`'ın tanımında "kaynak tarama, derleme" yazıyordu, ama böyle bir yordam yoktu:
  - soru terimlere ayrılmıyordu;
  - her terim ayrıca aranmıyordu;
  - bulunamayan terim söylenmiyordu;
  - aynı kayıt tekilleştirilmiyordu;
  - Türkçe çekim ("sarnıcı", "harcı") tam sözcük eşleşmesinde kayboluyordu.
- Araştırmanın ARAŞTIR diye işaretlenmesinin tek etkisi, sonucun BENGİ'ye doğrulanmamış olarak yazılmasıydı.

## 2. Yeni yordam (`umay/alp/arastirma.c`)

Araştırmayı **ajan kendisi** yapıyor. İş OTAĞ turlarına yayılıyor, her turda tek sorgu; bu sırada öteki ajanlar koşut çalışmayı sürdürüyor.

1. **Plan:** soru terimlere ayrılır. Durak sözcükleri ve buyruk sözcükleri (araştır, kaynak, tara, nedir, …) atılır; en çok 6 terim kalır.
2. **Tarama:** önce bütün terimler birlikte, sonra her terim tek tek aranır. Her sorguda kademe:
   - ajanın **kendi** kategorisi,
   - bildirdiği **ortak** kategoriler,
   - **genel depo**.

   Tam sözcük tutmazsa terim **köküyle** (önek) yeniden aranır. Kütüphaneye bunun için `bilgi_suzgec_t.onek` eklendi.
3. **Derleme:** aynı kayıt bir kez sayılır (başlık + kaynak), bulgular skorla sıralanır, en çok 6 bulgu tutulur.
4. **Rapor:**
   - her bulgu için alıntıdan **en çok terimi taşıyan tam cümle** ve kaynağı ile bulunduğu kademe;
   - **bulunamayan terimler**;
   - kökle bulunan terimler;
   - çatışan kaynaklar ("birleştirmedim");
   - hiç bulgu yoksa: "Bulgu yok … uydurmam."

   Raporda kaynağı olmayan tek cümle yoktur.
5. **Özet:** en iyi 3 bulgu US motoruna dayanak olarak verilir. Özet raporun **altına** eklenir, bulguların yerini almaz.

Görev dağıtımı değişmedi:
- alanı belli araştırmayı o alanın uzmanı yapar,
- birden çok alanı ilgilendireni meclis (birkaç uzman) yapar,
- alanı olmayanı BİLGE yapar (doğrudan genel depo).

## 3. İnceleme sırasında düzeltilen iki kusur

İkisi de raporları okurken bulundu:
1. Bütün-soru sorgusu buyruk sözcüklerini de arıyordu: "kaynak tara" sorusu "Umay Kitaplığı kaynakları…" kaydını getiriyordu. Artık yalnızca terimlerle aranıyor.
2. Bulgu cümlesi terimin ilk geçtiği yerden seçiliyordu: "zeytin", "zeytin**lik**" içinde yakalanıyor, asıl cümle kaçıyor ve "ayrılır." gibi kırpıntılar çıkıyordu. Artık en çok terimi taşıyan tam cümle seçiliyor; 15 bayttan kısa kırpıntı seçilmiyor.

## 4. Sınama

**Ana makine, sınama 39 (20 yeni kontrol; UMAY takımı 202 → 222):**
- Plan buyruk sözcüğünü terim saymıyor.
- Tarama, soru + terim başına birer sorguyla bitiyor.
- Kayıt tekilleşiyor, bulgular sıralı, kendi alanı önde.
- Olmayan terim raporda "bulunamayan" olarak geçiyor; "sarnıcı" kökle "sarnıç"a ulaşıyor.
- Her bulgunun kaynağı var; hiç bulgu yoksa uydurmuyor.
- OTAĞ üzerinden: alanlı araştırmayı BALBAL, alansız araştırmayı BİLGE yaptı; ara ilerleme görüldü (turlara yayıldı); rapor bulgu, kaynak ve özet taşıyor.

**ÖTÜKEN içinde** (4 çekirdek, 3 eşzamanlı istemci, 36 soru, 36/36):
- "kiler ve sarnıç harcı araştır" → **BALBAL** araştırdı: 3 kayıt; kendi alanı, ortak havuz ve genel depo ayrı ayrı işaretli.
- "zeytin budama biyokömür kaynak tara" → **YER-SU** araştırdı: tek ve doğru bulgu, *"Zeytin budama atığından biyokömür biochar üretilir."*
- Bu raporlar ~1 KB (11 IPC parçası, kuyruk 8) ve eksiksiz ulaştı. Bunun dolu kuyrukta yeniden deneme yolundan geçip geçmediği sayılmadı.

## 5. Sınırlar

- **Kaynak yalnızca yerel kütüphane:** 21 doktrin kaydı ve diskteki belgeler. V15 belgesi modelin eğitim derlemindedir, ama kütüphaneye kayıt olarak girmemiştir; araştırma onu **bulamaz**. İstersen V15'i kütüphaneye belge olarak ekleyebilirim.
- Ağ araştırması yok.
- Eşleşme sözcük düzeyinde: "sarnıç, kiler" geçen bir maliyet cümlesi de bulgu sayılıyor. Rapor bunu "genel depodan" diye işaretliyor, ama alaka sıralaması kaba.
- Güven düzeyi (US katmanı) bulgu sayısına bakıyor, alakaya bakmıyor.

## 6. 1.4.6: sonuç yapısı ve US'e yalnız kanıt

`ars_sonuc_t` (`umay/include/umay_arastirma.h`) bir araştırmanın sonucunu **ayrı alanlarda** tutar. Rapor metni bu yapıdan yazılır (`ars_sonuc_yaz`), metin parçalanarak yeniden kurulmaz.

| Alan | İçerik |
|---|---|
| `kaynak[]` | kategori, başlık, dosya, kaynak türü, kademe (kendi / ortak / genel), skor |
| `kanit[]` | kaynağın alıntısından, soru terimlerini en ağır taşıyan cümle (ve karşılanmayan terimleri taşıyan ikincisi); **aynen** |
| `bulunan` / `kokle` / `eksik` | terimlerin durumu; ayrı listeler |
| `catisma` | aynı kategoride ayrı kaynaklı kayıtlar |
| `guven`, `yeterli` | kanıt cümlelerinin soru terimlerini ağırlıklı kapsaması (`umay_kanit.h`); rapora "Kanıt gücü: %N" olarak yazılır |

- **US'e ham metin gitmez.** `ars_dayanak` özet için dayanağı kurarken kaynak bilgisini korur, alıntı penceresinin (800 bayta dek ham metin) yerine yalnız kanıt cümlelerini koyar.
- **Terimler tek listeden çıkar.** Araştırma planının attığı buyruk/soru sözcükleri artık kanıt katmanıyla ortak tek listedir; eskiden `arastirma.c`'de ayrı bir liste vardı.
- **Sınama:** `make -C umay sina`, bölüm 39. Denetlenenler:
  - her bulgunun kaynağı ayrı alanda;
  - kanıt cümlesi kaynaktan aynen alınmış;
  - eksik ve kökle bulunan terimler ayrı listelerde;
  - çatışma ayrı alanda;
  - güven kanıttan geliyor ve olmayan terim yeterliliği düşürüyor;
  - US'e ham alıntı değil kanıt cümlesi gidiyor.
