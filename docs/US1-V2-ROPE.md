# US1 v2: RoPE (döner konum kodlaması), uygulama ve doğrulama

Tarih: 2026-09-28 · Tasarım: `docs/US1-V2-TASARIM.md` · İlgili: `us1/c/us1.{h,c}`, `us1/arac/{us1.py,egit.py,referans.py}`, `tests/us1_conformance`, `tests/us1_fuzz`

## 1. Biçim

48 baytlık başlık v1 ile aynı. v2'de değişenler:

| Konum | v1 | v2 |
|---|---|---|
| 4 | sürüm = 1 | sürüm = 2 |
| 32 | 0 | bayrak: bit0 = RoPE; başka bit → ret |
| 36 | 0 | rope_taban (bit0 = 1 ise > 0, değilse 0 olmalı) |
| 40, 44 | 0 | 0 olmalı |

bit0 = 1 ise çıkış tensöründen sonra RoPE çizelgesi gelir:

```
uint32 satir (= baglam), uint32 sutun (= bas_boyu/2)
int32  cos[baglam][sutun]   (Q30)
int32  sin[baglam][sutun]   (Q30)
```

Bugünkü boyutlarla (bağlam 96, baş boyu 24) ek boy 8 + 2·96·12·4 = **9 224 bayt**.

Ayrıştırıcı (`us1_yukle`):
- toplam boy birebir tutmalı;
- baş boyu tek ise ret (çift kurulamaz);
- her çizelge değeri |x| ≤ 2³⁰ olmalı ve cos² + sin² değeri 2⁶⁰'tan en fazla 2⁴⁰ sapmalı (bozuk çizelge döndürmeyi norm bozan bir işleme çeviremez).
- v1 dosyalarında ayrılmış alanlar 1.4.6'daki gibi sıfır olmalı. **v1 yolu değişmedi.**

C tarafında trigonometri yoktur. Çizelge Python'da float64 ile hesaplanıp Q30'a yuvarlanır ve dosyaya yazılır; C ve Python aynı tamsayıları okur. Sınır: C, çizelgenin başlıktaki `rope_taban` ile üretildiğini denetleyemez; yalnız biçim ve normu denetler.

## 2. Matematik

Her baş içinde yan yana çiftler (2i, 2i+1), konum t, açı t·θᵢ, θᵢ = taban^(−2i/bs):

```
x0' = yuv64(x0·c − x1·s, 30)   → doyur32
x1' = yuv64(x0·s + x1·c, 30)   → doyur32
```

- Yalnız **q ve k** döndürülür; v döndürülmez.
- KV önbelleğine **döndürülmüş k** yazılır (`us1_adim`, `us1_ileri` aynı).
- İz noktaları: `q`/`k` döndürmeden önce, `q_rope`/`k_rope` sonra.
- Python eğitimi (`egit.py --rope`) aynı Q30'a yuvarlanmış cos/sin'i kullanır; geri yayılımda Rᵀ uygulanır (sin'i eksi alınmış `rope_f`).

## 3. Doğrulama (tümü gerçek koşu çıktısı)

| Denetim | Sonuç |
|---|---|
| v1: eski motor ↔ yeni motor, dağıtılan model | 303 448 logit **bit bit aynı** |
| v1 yazıcı çıktısı (Python `model_yaz`) | bayt bayt aynı |
| Gradyan denetimi (sayısal ↔ çözümsel, bağıl hata) | v1 1,39e-7 · RoPE 6,74e-7 |
| Uyum, v1 dağıtılan model | 17/17 (70/70 işlem) |
| Uyum, v2 rastgele model | 25/25: RoPE farkı **0** (tamsayı öykünmesiyle birebir), R bölümü 7/7, 70/70 işlem, geniş küme 289/300 + 11 yakın eşitlik, 0 HATA |
| Bulandırma | v1 36/36, v2 46/46 |
| US1 motor sınaması (`us1_sina`) | 15/15 + RoPE bloğu |

RoPE için keyfi bir float/int toleransı **yoktur**: döndürme Python'da C'nin tamsayı işlemleri birebir öykünülerek hesaplanır ve fark 0'dır. Kalan işlemler (matris çarpımı, softmax, norm) 1.4.6'daki uyum ölçütleriyle denetlenir; bu ölçütler değiştirilmedi.

## 4. Denetimli karşılaştırma (v1 ↔ v2)

Protokol (`kanitlar/egitim147/`): aynı derlem ve ayrım, tohum 7, aynı iyileştirici, adım üst sınırı 16 000, aynı erken durdurma, aynı dağıtım planı, aynı ölçüm kodu (`us1/arac/olc.py`), aynı 85 soruluk sınama kümesi. Tek fark `--rope`. v1 ve v2 aynı makinede **sırayla** eğitildi; eğitim sırasında ağır sınama koşturulmadı.

Kanıt: `kanitlar/egitim147/` (`karsilastir_v1_v2.txt`, `karsilastirma_v1_v2.json`, `anlamlilik_v1_v2.json`, eğitim günlükleri, `aday_v2.us1`). İlk koşu 4 500. adımda bulut kabı geri alındığı için kesildi (`kesilen_1/`); `--devam` RNG ve erken durdurma durumunu geri yüklemediğinden sürdürülmedi, iki model **baştan** eğitildi.

**Tekrarlanabilirlik:** aynı protokolle yeniden eğitilen v1 (`aday_v1.us1`), dağıtılan `us1/umay.us1` ile **bayt bayt aynı** çıktı.

| | v1 | v2 (RoPE) |
|---|---|---|
| seçim: en iyi adım (geliştirme üretim F1) | 10 500 (0,145) | 7 500 (0,153) |
| eğitim kaybı, adım 1 500 | 0,654 | 0,234 |
| **sınama (85 soru)**: sözcük F1 ort. | **0,196** | 0,106 |
| sınama: ilk cümle doğru | 10/85 | 5/85 |
| sınama: F1 ≥ 0,6 | %16,5 | %7,1 |
| sınama: bozuk sözcük oranı | 0,057 | 0,020 |
| eğitim örnekleri (80): F1 | 0,880 | 0,799 |
| alan dışı (310): kendi başına ret | 27 | 26 |
| alan dışı: eğitim yanıtı ezberden söyleme | %72,9 | %85,8 |
| dosya boyu | 476 100 B | 485 324 B |

İstatistik (`us1/arac/anlamlilik.py`, eşli, tohum 147):

- F1 farkı (v2 − v1) −0,090; %95 GA [−0,172; −0,010]; eşli permütasyon p = 0,035. Soru soru: v2 20 soruda iyi, 27 soruda kötü, 38 eşit.
- İlk cümle: yalnız v2 doğru 3, yalnız v1 doğru 8; kesin McNemar p = 0,23 (anlamlı değil).
- Alan dışı ret: 21 / 22 uyuşmayan çift, p = 1,0 (fark yok).
- **Karar: ANLAMLI KÖTÜLEŞME.** `olc.py` kuralı da "üstün değil" dedi.

Yorum (varsayım, kanıtlanmadı): RoPE eğitim kaybını çok daha hızlı düşürüyor, ama geliştirme F1'i daha erken tepe yapıyor ve sınama kümesinde genelleme kötüleşiyor. Alan dışında eğitim yanıtını ezberden söyleme de arttı. Bu küçük model ve küçük derlemle konum bilgisi, ezberlemeyi kolaylaştırmış görünüyor. Tek tohumla ölçüldü; başka tohumlar ve düzenlileştirme (bırakma, daha güçlü ağırlık sönümü) denenmedi.

**Eğitilmiş v2 modelde uyum** (`kanitlar/egitim147/uyum_v2_egitilmis.txt`): 25/25; RoPE farkı 0; R bölümü 7/7; açgözlü üretim 70/70; genişletilmiş örneklem 300/300, 0 HATA.

## 5. Dağıtım kararı

Karar kuralı (önceden yazıldı): v2 yalnız sınama kümesinde ilk cümle doğruluğu ve F1'de istatistiksel olarak anlamlı bir kazanç gösterirse (eşli permütasyon sınaması, p < 0,05, güven aralığı sıfırı içermez) **ve** alan dışı ret ile dayanaklı yanıt oranı kötüleşmezse dağıtılır. Aksi durumda v1 kalır ve bu yazılır.

**Sonuç: v2 DAĞITILMADI.** `us1/umay.us1` v1 olarak kaldı. Motor, eğitici, uyum ve bulandırma v2'yi desteklemeye devam eder; RoPE modelini dağıtmak için önce yukarıdaki kuralı geçen bir model gerekir.
