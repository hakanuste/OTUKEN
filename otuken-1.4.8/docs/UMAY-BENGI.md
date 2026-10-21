# BENGİ: konuşmadan belleğe yol (1.4.6)

Kod: `umay/core/umay_bengi.c`, `umay/include/umay_bengi.h`, `umay/alp/alp_yazman.c`, `umay/alp/otag.c`.
Sınama: `make -C umay sina`, bölüm 41 (21 kontrol).

## 1. 1.4.5'teki durum

| Sorun | Sonuç |
|---|---|
| "kaydet" buyruğu sözü olduğu gibi (buyruk sözcükleri dahil) doğrulanmış KULLANICI kaydı olarak yazıyordu. | Boş, soru biçiminde ya da yinelenen söz de kayda giriyordu. |
| Görev çıktıları (SONUC) bir kurala göre (SORU değil, onaysız, çatışmasız) kendiliğinden "doğrulanmış" sayılıyordu. | Kimse onaylamadan doğrulanmış kayıt oluşuyordu. |
| **Biçim hatası:** disk biçimi `tur\|dogrulandi\|anahtar\|deger`, satır başına bir kayıt. Çok satırlı görev çıktısı kaydı bölüyordu. | Geri yüklemede kaydın ilk satırı kalıyordu. Sonraki satırlar AYRI KAYIT gibi ayrıştırılıyordu: çıktıda `karar\|1\|…` biçiminde bir satır, doğrulanmış bir KARAR olurdu. |

## 2. Yol

```
konuşma
  -> ADAY: bengi_denetle
       baştaki/sondaki buyruk sözcükleri atılır ("şu notu kaydet:", "..., kaydet");
       RED: boş, soru (? ya da soru eki), > 191 bayt, denetim karakteri, yineleme
  -> ONAY: YAZMAN işi GT_UYGULA'dır; kullanıcı "onay ver onayN" demeden koşmaz
  -> DOĞRULANMIŞ kayıt (onaysız koşarsa ADAY kalır: dogrulandi=0)
  -> disk (bengi_sakla)
```

- **Görev çıktısı (SONUC)** her zaman ADAY girer, tek satıra düzlenir (`bengi_duzle`). Kullanıcı "doğrula <anahtar>" derse doğrulanır; bu da onaylı bir YAZMAN işidir. SONUC doğrulansa bile US bağlamına **hiç** girmez (`bengi_baglam`).
- **Disk biçimi koruması:** `bengi_kaydet` denetim karakterli (satır sonu dahil) değeri ve `|` içeren anahtarı REDDEDER. Eskiden biçim bozulabiliyordu; artık bozulmaz.

## 3. Model ağırlıkları

BENGİ hiçbir yoldan US1 modeline ya da eğitim derlemine yazmaz:

- UMAY'da diske yazan yalnız üç yer vardır: BENGİ dosyası, ayar dosyası, günlük (`grep UP_YAZ umay/`).
- Model yalnız diskteki `UMAY.US1` dosyasından okunur.
- Ek koruma: BENGİ yolu bir model dosyası olamaz (`*.us1`, `sozluk.txt`). Ayar dosyası "bengi=/UMAY.US1" dese bile `bengi_kur` reddeder ve hafıza yalnız bellekte kalır.
- Eğitim derlemi (`us1/arac/derlem.py`) yalnız depodaki metinlerden kurulur.
