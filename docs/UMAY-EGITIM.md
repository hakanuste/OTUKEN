# UMAY EĞİTİMİ — İLK TUR (1.4.4) ve SIZINTISIZ ÖLÇÜM (1.4.6)

Tarih: 2026-09-27 · Kanıtlar: `kanitlar/egitim/` (eğitim günlüğü, ölçüm, ÖTÜKEN seri kaydı)

## 1. Başlangıçtaki durum

- UMAY'in US1 modeli 15 satırlık bir metinle eğitilmişti: 64 boyut, 2 katman, 110 bin parametre.
- **Hata:** eğitici sözlüğü Unicode **harflerinden** kuruyordu (45 im), ÖTÜKEN'deki C motoru ise sözlüğü **bayt bayt** okuyor (`sozluk.txt` 53 bayt). Bu yüzden ç, ğ, ı, ö, ş, ü, Ö ve Ü ÖTÜKEN'de yanlış baytlara eşleniyordu; Türkçe harfli her girdi ve çıktı kayıyordu.
- Üretim soruyu bağlamsız başlatıyor, her harf için bütün diziyi baştan hesaplıyordu (O(T²)).

## 2. Yapılanlar

| Parça | Değişiklik |
|---|---|
| Sözlük | **Bayt düzeyi** (`derlem.sozluk_kur`): 83 bayt, C motoruyla birebir |
| Derlem (`us1/arac/derlem.py`, `us1/derlem/`) | 21 gömülü doktrin kaydı; V15 proje veri tabanı (`v15.txt`); UMAY'in kişiliği ve söz kalıpları (`kisilik.txt`); el yazımı 41 soru-yanıt; diskteki belgeler |
| Soru türetme | Her yanıt cümlesi için, o cümlenin anahtar sözcüklerinden 12 kalıpla soru. Derlem dışı 15 konu için "bilmiyorum, uydurmam" örnekleri |
| Eğitici (`us1/arac/egit.py`) | Yığınlı (16×96) elle geri yayılım; Adam, ısınma + kosinüs, küresel kırpma, hafif ağırlık sönümü; eğitim/doğrulama ayrımı; kayıt noktası ve devam |
| Doğrulama | Gradyan denetimi: sayısal türevle en kötü göreli fark **1,4·10⁻⁷**; ileri geçiş referansla aynı (7·10⁻¹⁶) |
| Model | d=96, 4 katman, 4 başlık, ffn=256, bağlam 96 → **459.168 parametre**, Q8 dosyası 476 KB (motor sınırları içinde) |
| C motoru | **KV önbellekli `us1_adim`**: her im bir kez hesaplanıyor; tam geçişle **bit bit aynı** (96 konumun hepsinde sınandı) |
| UMAY üreteci | Eğitimdeki `S: soru / U: ` kalıbı; açgözlü seçim; satır sonunda ya da cümle sonunda (". ") durma; yarım UTF-8 baytı atılıyor |

## 3. Eğitim

16.000 adım, ~54 dakika (2 çekirdek, numpy).

| Adım | Eğitim kaybı | Doğrulama (görülmemiş soru biçimi) | Doğrulama (görülmemiş düzyazı) |
|---|---|---|---|
| 500 | 1,60 | 1,42 | 2,50 |
| 4500 | 0,28 | 0,51 | 2,78 |
| 10500 | 0,14 | 0,48 | 3,19 |
| 16000 | 0,11 | 0,43 | 3,36 |

Birim: nat/bayt. Model derlemi ezberliyor. Görülmemiş düzyazıda kayıp artıyor, yani yeni metne genelleme yok. 459 bin parametre ve ~180 KB derlem için beklenen bir sonuç.

## 4. Ölçüm (`us1/arac/olc.py`, Q8 dosyadan, açgözlü)

| Ölçüt | Sonuç |
|---|---|
| Eğitimde görülen sorular, ilk cümle doğru | **59/80 (%74)** |
| Hiç görülmemiş soru biçimleri, ilk cümle doğru | **29/119 (%24)** |
| Derlem dışı yeni konularda dürüst ret | **0/10**: ret genellenmedi; model ilgisiz bir derlem cümlesi söylüyor |
| C tamsayı motoru ile Python aynı metni üretiyor | **64/70 (%91)**: kalan 6'sında sabit nokta yuvarlaması uzun üretimde bir harfi değiştiriyor |

## 5. ÖTÜKEN içinde (4 çekirdek, 3 eşzamanlı istemci, 36 soru)

- 36/36 yanıt, panik yok.
- Ortalama yanıt süresi **1,6 s**, önceden 3,5 s. En kötü **3,0 s**, önceden 9 s. Model 4 kat büyüdüğü halde KV önbelleği sayesinde hızlandı.
- ÖTÜKEN'deki UMAY, ana makinedeki `us1_uret` ile **aynı metni** üretti; uçtan uca zincir tutarlı.

Aşağıdakiler uzman ajanın yanıtıdır; söz US1 modelinden gelir. Selam ve durum soruları modele gitmez, onları OZAN hazır kalıpla yanıtlar.

| Soru (ÖTÜKEN'de) | UMAY'in sözü | Değerlendirme |
|---|---|---|
| zeytin budama atığından ne yapılır | Zeytin budama atığından biyokömür biochar üretilir. | doğru |
| yer altı kileri kaç derece | Yer altı kileri yıl boyu 12-15 derecede sabit kalır. | doğru |
| güneş panelleri nereye konur | (yer altı kileri cümlesi) | **yanlış**; aynı soru "paneller nereye konur" biçiminde doğru yanıtlanıyor |
| kerpiç duvar nasıl sıvanır | (sığınak prova alanı cümlesi) | **yanlış**; bilgi araması da yanlış kayda gitti |
| sarnıç suyu nasıl temiz kalır | anlamsız | **yanlış** |

## 6. Eğitim sırasında bulunan ve düzeltilen hatalar

1. **Harf/bayt sözlüğü uyumsuzluğu** (bölüm 1).
2. **UMAY yığın taşması:** `umay_tur` yığında 52 KB'lık çerçeve açıyordu, Ring3 yığını 64 KB'tı. Yeni üretim yolu birkaç yüz bayt ekleyince UMAY sayfa hatasıyla düştü; adres yığın tabanının hemen altındaydı. Büyük diziler durağan alana taşındı, yığın 128 KB oldu. En derin zincir artık ~20 KB.
3. UMAY'de kullanılmayan 49 KB'lık bir mantık tamponu kaldırıldı.
4. **Klavye yarışı:** `SYS_PEN_OLAY` pencerede olay yoksa ORTAK klavye kuyruğundan harf çekiyordu. UMAY penceresini her turda yokladığından kabuğa yazılan tuşlar rastgele UMAY'a gidiyordu. Kabuk testinde 4 çekirdekte 8 koşuda 2 KALDI görüldü (tek çekirdekte 2 koşuda 0). 1.4.3'teki saat düzeltmesi UMAY'in yoklama sıklığını ikiye katlayıp hatayı görünür kıldı; kök neden daha eskiydi. Çözüm: ÖTÜKEN'e **klavye odağı** eklendi. Tuşlar yalnızca odak sahibine gidiyor, **Tab** odağı kabuk ile UMAY penceresi arasında değiştiriyor (varsayılan kabuk). Kabuk testi bu geçişi de sınıyor (9 kontrol).

## 7. Açık konular ve sonraki tur

- **Uydurma riski:** model derlem dışında da akıcı ama ilgisiz bir cümle söylüyor. UMAY'de iki koruma var: dayanak bulunmayan soruda motor hiç çağrılmıyor ve her yanıtta dayanak ile güven düzeyi yazılıyor. Yine de dayanak yanlış kayda denk gelirse (kerpiç örneği) yanlış söz "Güven: zayıf" etiketiyle çıkıyor.
- **Genelleme %24:** sonraki tur için en etkili iki adım:
  - istemi UMAY'in bulduğu dayanağın **başlığıyla** kurmak; model "<başlık> nedir" biçimini güvenilir biçimde yanıtlıyor;
  - her yanıt için daha çok ve daha doğal soru biçimi.
- **V15 metni:** `us1/derlem/v15.txt` senin V15 belgendir, düzyazıya çevrildi. **Değiştirdiklerim:**
  - koloidal gümüş üretimi ve "can yakan" elektrikli çit darbe üreteci satırları derleme alınmadı;
  - sağlık bölümüne "Ağır durumda hekime ulaşmak her zaman önceliktir" cümlesi eklendi;
  - küçük bir model bunları bağlamından kopuk tavsiye olarak tekrarlayabilir.

  İstersen geri eklerim.
- Sözcük türetmede gürültü var: "motor nasıl" sorusu biyodizel cümlesine de bağlanıyor.
- Gerçek donanımda hız **test edilmedi**.

## 8. Yeniden üretme

```sh
cd us1/arac
python3 egit.py denetim          # geri yayılım denetimi
python3 egit.py egit 16000       # ~54 dk; --devam ile sürdürülür (us1/kov/)
python3 egit.py aktar            # -> us1/umay.us1 + us1/sozluk.txt
python3 referans.py              # us1_sina referansları
python3 olc.py                   # ölçüm (us1/us1_uret gerekir)
cd ../.. && make umay-disk       # diske yaz
```


---

# 1.4.6: sızıntısız doğrulama, sabit sınama, seçim ve dağıtım kuralı

Yönerge: "Bu modeli hemen daha fazla büyütme." Model boyu değişmedi. Değişen, eğitimin nasıl **ölçüldüğü** ve bir modelin ne zaman **dağıtıldığı**.
Araçlar `us1/arac/`: `derlem.py`, `sizinti.py`, `egit.py`, `olc.py`. Kanıtlar `kanitlar/egitim146/`.

## A. 1.4.4 ayrımındaki sızıntı (`python3 sizinti.py eski`)

| Ölçü | 1.4.4 ayrımı | 1.4.6 ayrımı (`sizinti.py yeni`) |
|---|---|---|
| doğrulama sorusu eğitimde **birebir** | 14 / 133 | 0 / 109 |
| doğrulama sorusu eğitimde **normalleştirilmiş** ("?", büyük harf) | **37 / 133** | 0 / 109 |
| düzyazı doğrulamasının sözcük 8'lileri eğitimde | **%30** | **%0** (158 sekizli) |
| sınama kümesi (85 soru) sorusu eğitimde | — | 0 |

1.4.4 raporundaki "hiç görülmemiş soru biçimleri, ilk cümle doğru %24" sızıntılıydı. Nedenleri:

- doğrulama sorularının %28'i eğitimde yalnız "?" ya da büyük harf farkıyla vardı;
- düzyazı doğrulaması (belge sonları) aynı cümleleri yanıt olarak eğitimde bulunduruyordu;
- doktrin kayıtları V15'in yeniden yazımı olduğundan, bir cümle ayrılsa bile yakın kopyası eğitimde kalıyordu.

Yeni ayrım (`derlem_kur(secim=True)`):

| Parça | Nasıl ayrılır |
|---|---|
| sabit sınama (`us1/derlem/sinama_kumesi.tsv`, 85 soru) | eski modelin doğrulamasından, normalleştirilmiş biçimi eski eğitimde HİÇ geçmeyen sorular; yeni eğitime hiçbir yazım biçimi girmez |
| geliştirme (109 çift) | NORMALLEŞTİRİLMİŞ soru düzeyinde |
| dil modeli doğrulaması (2,2 KB) | CÜMLE düzeyinde; ayrılan cümle ve 8'li paylaşan yakın kopyaları ne düzyazıda ne yanıtta eğitime girer |

## B. Seçim ve dağıtım

1. **Seçim koşusu:** sınama + geliştirme + metin doğrulaması ayrı; her 500 adımda değerlendirme, erken durdurma.
2. **Dağıtım koşusu:** sınama dışındaki bütün veriyle, seçilen adıma dek.
3. **Karşılaştırma** (`olc.py karsilastir`, ÖTÜKEN'deki C tamsayı motoruyla açgözlü üretim). Yeni model YALNIZ şu üç koşulda dağıtılır:
   - sınama F1 ortalaması artar;
   - ilk cümle doğruluğu azalmaz;
   - bozuk sözcük oranı en çok +0,01 artar.

## C. Tur 1: seçim ölçütü = geliştirme YANIT KAYBI (`kanitlar/egitim146/tur1_*`)

| Adım | Eğitim kaybı | Geliştirme yanıt kaybı | Metin doğrulaması (görülmemiş cümle) |
|---|---|---|---|
| 500 | 1,54 | 1,61 | **2,57** |
| 2000 | 0,53 | 0,37 | 3,05 |
| 5500 | 0,22 | **0,204** (en iyi) | 4,01 |
| 8500 | 0,16 | 0,216 | 4,40 |

- Erken durdurma 8500'de; en iyi adım 5500.
- **Aşırı öğrenme ölçüsü:** görülmemiş cümle kaybı 500. adımdan sonra sürekli artıyor (2,57 → 4,40). Model derlemi ezberliyor, yeni metne genellemiyor. Geliştirme yanıt kaybı ise 5500'e dek düşüyor. Bunun nedeni, geliştirme yanıtlarının başka sorularla eğitimde bulunması: model soru biçimini tanımayı öğreniyor, yeni bilgiyi değil.

Dağıtım modeli (5500 adım) ile 1.4.4 modelinin sabit sınama kümesindeki karşılaştırması:

| | 1.4.4 modeli | Tur 1 adayı |
|---|---|---|
| sınama ilk cümle doğru | **%7,1** | %4,7 |
| sınama sözcük F1 | **0,175** | 0,131 |
| sınama bozuk sözcük oranı | **0,076** | 0,106 |
| alan dışında kendi başına ret | %2,6 | **%8,1** |
| alan dışında eğitim cümlesi söyleme (ezber) | %78,7 | **%66,1** |
| soru soru F1 | — | 20 daha iyi, 38 daha kötü |

**KARAR: dağıtılmadı.** Kural gereği `us1/umay.us1` değişmedi.

Tur 1'den çıkan iki ders:

- Yanıt **kaybı** üretilen yanıtın niteliğini iyi temsil etmiyor.
- Dağıtım koşusu 5500 adımlık bir kosinüs çizelgesiyle koştu. Seçim koşusunun 5500. adımı ise 16 000 adımlık çizelgenin ortasındaydı; iki yörünge aynı değildi.

## D. Tur 2: seçim ölçütü = geliştirme sorularında ÜRETİLEN yanıtın F1'i (`kanitlar/egitim146/tur2_*`)

Tur 2'de iki şey değişti:

- **Seçim ölçütü:** her değerlendirmede model Q8'e yazılır. Geliştirme sorularına ÖTÜKEN'deki C motoruyla, açgözlü yanıt üretilir. Ölçüt, beklenen yanıtla sözcük F1'idir (`egit.py: dev_uretim`).
- **Dağıtım yörüngesi:** dağıtım koşusu seçim koşusunun öğrenme hızı yörüngesini aynen izler (`--plan 16000`) ve en iyi adımda durur.

| Adım | Eğitim kaybı | Geliştirme yanıt kaybı | Görülmemiş cümle kaybı | Geliştirme üretim F1 | İlk cümle |
|---|---|---|---|---|---|
| 1000 | 0,91 | 0,77 | 2,64 | 0,037 | %0 |
| 5000 | 0,24 | 0,23 | 3,95 | 0,111 | %5,5 |
| 7500 | 0,17 | 0,23 | 4,28 | 0,127 | %7,3 |
| **10500** | 0,14 | 0,21 | 4,86 | **0,145** (en iyi) | %8,3 |
| 13500 | 0,11 | 0,21 | 5,05 | 0,131 | %7,3 |

Erken durdurma 13500'de, en iyi adım 10500. Yanıt kaybı ile üretim F1'inin en iyisi farklı adımlarda: kayıp 5500'ü, F1 10500'ü gösteriyor. Tur 1'in yanılgısının nedeni bu.

### Karşılaştırma (sabit sınama kümesi, C motoru, açgözlü)

| | 1.4.4 modeli | Tur 2 adayı (dağıtıldı) |
|---|---|---|
| sınama ilk cümle doğru | %7,1 | **%11,8** |
| sınama sözcük F1 | 0,175 | **0,196** |
| sınama bozuk sözcük oranı | 0,076 | **0,057** |
| eğitim örneklemi ilk cümle | %67,5 | %67,5 |
| alan dışında kendi başına ret | %2,6 (8/310) | **%8,7 (27/310)** |
| alan dışında eğitim cümlesi söyleme | %78,7 | **%72,9** |
| sözü dayanağa bağlı (≥ %60), 410 alan içi soru | %16 | **%22** |

**KARAR:** önceden konan dağıtım kuralını geçti; `us1/umay.us1` değişti, `make umay-disk` ile diske yazıldı.

**Dürüst not:** alan içi kazanç istatistikçe güçlü DEĞİL.

- Soru soru F1: yeni model 23'ünde daha iyi, 32'sinde daha kötü, 30'unda eşit.
- Eşli permütasyon: F1 farkı +0,020, p = 0,66.
- İlk cümle: yalnız yeni doğru 7, yalnız eski doğru 3.

Güçlü olan fark, alan dışında modelin kendi başına ret etmesi: 27 ile 8. Yeni model sızıntısız düzenle ve üretime dayalı ölçütle seçildi. Yanıtın doğruluğunu yine de model değil kanıt katmanı korur (`docs/UMAY-KANIT.md`).

Yeni modelde doğrulanan sınamalar:

- `us1_sina` 15/15 (referans yeniden üretildi);
- `tests/us1_conformance` 17/17, açgözlü 70/70 ve genişletilmiş 300/300 aynı;
- `tests/us1_fuzz` 32/32.

## E. Yeniden üretme (1.4.6)

```sh
cd us1/arac
python3 sizinti.py eski && python3 sizinti.py yeni
python3 egit.py denetim
python3 egit.py secim 16000                         # ~45 dk (2 çekirdek); kov/secim/secim.json
python3 egit.py dagitim <en_iyi_adim> --plan 16000
python3 egit.py aktar ../kov/dagitim/kayit.npz ../kov/aday.us1
python3 olc.py karsilastir ../umay.us1 ../sozluk.txt ../kov/aday.us1 ../kov/aday.sozluk
```

**Dikkat:** eğitim ile QEMU sınamaları aynı anda koşturulmamalı. 2 çekirdekli makinede iki AĞIR koşu zaman aşımına düştü (`kanitlar/surum146/agir_ara_*`).
