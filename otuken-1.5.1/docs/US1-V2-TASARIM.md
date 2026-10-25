# US1 v2 tasarımı: konum bilgisi (RoPE) ve biçim

Durum: **TASARIM**. Bu belge henüz kodlanmadı; kodlamadan önce karar içindir.
Tarih: 2026-09-27 · İlgili: `us1/c/us1.c`, `us1/arac/{us1.py,egit.py,referans.py}`, `tests/us1_conformance`, `tests/us1_fuzz`

## 1. Bugünkü durum (koddan doğrulandı)

- **Konum bilgisi yok.** `us1/c/us1.c` başlığı ("Konumsal kodlama YOKTUR"), `us1_ileri`/`us1_adim` (q, k, v yalnız izdüşümle üretiliyor, konuma bağlı hiçbir terim yok), `egit.py` (`ileri_ara`: `x = m.gomme[imler]`, sonra doğrudan dikkat) ve `referans.py` üçü de aynı: gömme + nedensel dikkat. Konum `us1_adim`'da yalnızca KV önbelleğinin sırasıdır.
- Sonucu: model bir önekteki imlerin **sırasını** göremez; yalnız nedensel maske "kaç im önce geldi" bilgisini dolaylı verir. Bayt düzeyinde bir model için bu ciddi bir kısıttır: "ar" ile "ra" aynı öneki farklı sırada taşır. Sınama kümesindeki düşük genelleme (bkz. `docs/UMAY-EGITIM.md`, sınama ilk cümle %7) bu kısıtla tutarlıdır. Bunu nedensellik iddiası olarak değil, **denenecek varsayım** olarak yazıyorum.
- Biçim v1 (`us1_yukle`): 48 baytlık başlık `"US1\0"`, sürüm=1, d, katman, baş, ffn, sözlük, bağlam, 16 bayt ayrılmış alan (SIFIR olmalı; değilse ret). Ardından Q8 tensörler; toplam boy birebir denetlenir.
- Bugünkü model: d=96, 4 katman, 4 baş (baş boyu 24), ffn=256, sözlük 83, bağlam 96 → 476 100 bayt.

## 2. Hedef

1. RoPE (döner konum kodlaması) ekle; Python eğitici, Python referans ve C motoru **aynı matematiği** kullansın.
2. **v1 modeller aynen çalışmaya devam etsin** (aynı sayılar, bit bit).
3. ÖTÜKEN'de kayan nokta yok: C tarafında yalnız tamsayı.
4. Biçim ayrıştırıcısı yeni alanlarda da en az bugünkü kadar sağlam olsun (bulandırma sınaması genişler).

## 3. Biçim v2

Başlık aynı 48 bayt. Değişen yalnız:

| Konum | v1 | v2 |
|---|---|---|
| 4 | sürüm = 1 | sürüm = **2** |
| 32 | 0 | **bayrak**: bit0 = RoPE; diğer bitler 0 olmalı (değilse ret) |
| 36 | 0 | **rope_taban** (tamsayı, ör. 10000); bit0=0 ise 0 olmalı |
| 40, 44 | 0 | 0 (ayrılmış) |

bit0=1 ise çıkış katmanından sonra bir **RoPE çizelgesi** gelir:

```
uint32 satir = baglam, uint32 sutun = bs/2        (bs = d / bas; bs çift olmalı)
int32  cos_q30[baglam][bs/2]
int32  sin_q30[baglam][bs/2]
```

**Neden çizelge dosyada, C'de hesap değil?** cos/sin'i tamsayıyla (CORDIC, Taylor) C'de üretmek hem kod hem uyuşmazlık kaynağı olur. Çizelgeyi Python float64'te hesaplayıp **Q30'a yuvarlayarak** dosyaya yazınca Python referansı da C motoru da **aynı tamsayıları** okur; uyum sınamasında RoPE adımının yerel hatası 0 olur. Maliyeti küçük: bağlam 96 × 12 çift × 2 × 4 bayt = 9 216 bayt.

Toplam boy formülü (`us1_yukle`) bit0=1 ise `8 + 2·baglam·(bs/2)·4` kadar büyür; yine **birebir** denetlenir.

Denetimler (ayrıştırıcı, bulandırmaya eklenecek olgular):
- sürüm ∉ {1, 2} → `US1_SURUM`
- v1'de ayrılmış alan ≠ 0 → ret (bugünkü kural değişmez)
- v2'de bilinmeyen bayrak biti, bit0=0 iken taban≠0, bs tek, çizelge satır/sütun başlıkla uyuşmuyor → ret
- her değer |cos|, |sin| ≤ 2^30, ayrıca cos²+sin² ≈ 2^60 (±2^40) → değilse ret (bozuk çizelge motoru taşırmasın)

## 4. Tamsayı matematiği

Konum `t`, baş içi çift `i` (0 ≤ i < bs/2), q ve k 16.16:

```
c = cos_q30[t][i], s = sin_q30[t][i]
q'[2i]   = yuv64( q[2i]*c - q[2i+1]*s , 30 )
q'[2i+1] = yuv64( q[2i]*s + q[2i+1]*c , 30 )
```

- `q·c` en çok 2^31·2^30 = 2^61; iki terimin toplamı 2^62 → int64'e sığar. `yuv64` bugünkü yuvarlayan kaydırmadır; sonuç `doyur32` ile 32 bite sıkıştırılır.
- Yer: `wq`/`wk` izdüşümünden hemen sonra, dikkat skorundan önce; **v'ye uygulanmaz**. KV önbelleğine **döndürülmüş k** yazılır.
- `us1_ileri`'de t = dizideki sıra, `us1_adim`'da t = `g_konum`. İkisi aynı işlevi çağırır; "adım ile tam geçiş bit bit aynı" sınaması bugünkü gibi geçmelidir.
- Bağlam dolunca (`US1_DOLU`) son yarıyla yeniden doldurma bugünkü gibi baştan hesaptır; RoPE göreli olduğundan konumların 0'dan yeniden başlaması doğrudur.

## 5. Python tarafı

- `us1.py`: v2 yazma/okuma; çizelgeyi `rope_cizelge(baglam, bs, taban)` üretir (float64 → `round(x·2^30)`).
- `egit.py`: ileri geçişte q, k döndürülür. Geri yayılım: döndürme doğrusal ve dik olduğundan `dq = Rᵀ·dq'`, `dk = Rᵀ·dk'`. `gradyan_denetimi` RoPE açıkken de koşmalı (göreli hata < 1e-5).
  Eğitimde çizelgenin **Q30'a yuvarlanmış** değerleri kullanılır, ki C'nin gördüğü sayıyla eğitilsin.
- `referans.py` (float64): aynı çizelge değerlerini (Q30 → float) kullanır.

## 6. Geriye uyum güvencesi

- v1 dosyası v2 motorda: bayrak alanı 0 → RoPE kod yolu hiç çalışmaz. **Kabul ölçütü:** bugünkü `umay.us1` ile 70 istemin mantıkları v2 motorda bit bit aynı (sınama: mantık özetinin (SHA-256) eski motorla karşılaştırılması).
- UMAY (`umay_us_yerel.c`) sürüme bakmaz; `us1_yukle` hangisini kabul ederse onu koşturur.
- `MODEL_AZAMI` (640 KB) v2 çizelgesiyle yeterli (476 KB + 9 KB).

## 7. Sınama planı (kodlamayla birlikte)

| Sınama | Ne |
|---|---|
| `tests/us1_conformance` | yeni işlem: `rope` (yerel hata 0 beklenir); v2 model için A–H bölümleri; v1 model için değişmeyen sonuç |
| `us1/sinama/us1_sina.c` | v2 yükleme; adım = tam geçiş (RoPE açık); v1 mantık özeti değişmedi |
| `tests/us1_fuzz` | §3'teki yeni ret olguları + rastgele bozma v2 dosyasında |
| `egit.py denetim` | RoPE açık gradyan denetimi |

## 8. Kapasite (v2 biçimi → daha büyük model) — PLAN, şimdi değil

Yönerge: "Bu modeli hemen daha fazla büyütme." Önce sızıntısız doğrulama ve karşılaştırma düzeni (Ö7, `us1/arac/{derlem,egit,olc,sizinti}.py`) kuruldu. Büyütme ancak şu sırayla ve her adımda sınama kümesinde ölçülerek:

1. Aynı boyda RoPE'lu model; `olc.py karsilastir` eskiye üstünse dağıtım.
2. Alt sözcük (BPE) sözlük: sözlük 83 bayt → 512–1024 im. Çıkış katmanı V×d büyür (1024×96 Q8 ≈ 100 KB). `US1_AZAMI_SOZLUK` ve `us1_kodla` değişir; bayt sözlüğü v1/v2'de kalır, BPE ayrı bir bayrakla (bit1) gelir.
3. Sınırlar: `US1_AZAMI_*` ve Ring3 bellek bütçesi. KV önbelleği `katman × bağlam × d × 2 × 4 B` (bugün 4×96×96×8 = 295 KB). d=128, 6 katman, bağlam 128 → 786 KB; `MODEL_AZAMI` ve UMAY yığın/BSS bütçesi yeniden hesaplanmalı.

Her adımın kabul ölçütü aynı: sınama kümesinde F1 artmalı, ilk cümle doğruluğu azalmamalı, bozuk sözcük oranı artmamalı (`olc.py karsilastir`).
