# Ham arşivi UMAY kitaplığına çevirmek

    python3 arac/kitaplik_kur.py <ham_arsiv> kut

Klasör adından alanı çıkarır, PDF çıkarım gürültüsünü temizler,
belgeleri parçalara böler, her parçanın başına künye yazar.

## Neden gerekli

UMAY'ın BİLGİ katmanının dört sınırı vardı; dördü de bu arşiv
denenince görüldü:

| sınır | sonuç | durum |
|---|---|---|
| tarayıcı dizini okumuyordu, alan başına 6 sabit ad yokluyordu | belgeler hiç görünmüyordu | `up_dizin_listele` eklendi, tarayıcı gerçekten tarıyor |
| listeleme sayfalanmıyordu | 1012 parçanın 258'i indekslendi | sayfalama eklendi |
| belgelerde `#kategori:` künyesi yoktu | hepsi "ortak" havuza düşüyordu | araç künye yazıyor |
| bir kayıt en çok 3072 bayt | 348 KB'lik belge ilk sayfasına kırpılıyordu | belge parçalara bölünüyor |

## Künye biçimi

    #kategori: otaci
    #baslik:   Temel İlk Yardım (3/64)
    #etiket:   ilkyardim,travma
    #kaynak:   01_Saglik-Bakanligi-Ilk-Yardim.txt

Kategori **dosya yolundan çıkarılmaz**; belgenin ilk satırları esastır.

## Alan eşlemesi

Klasör adındaki anahtar sözcükten bulunur: mimari/kerpiç → `balbal`,
biyodizel/mercedes/musso → `tulga`, telsiz/çark → `demir`,
sera/su/kompost/kümes → `yer-su`, radar/kamera/siber → `alpagu`,
tıp/ilk yardım → `otaci`, sahne/folklor → `oyun`, kültür/tarih/dil →
`yazit`. Eşleşme yoksa `ortak`.

Eşlemeyi `ESLEME` listesinden değiştirebilirsin.

## Dokunmadıkları

- `00_Onay_Bekleyenler/` — insan onayından geçmemiş ham döküm.
- `99_Kisisel_Saglik_Korumali/` — kişisel sağlık verisi.

İkisi de hiç okunmaz. Arşivin kendi MANIFEST'i de böyle diyor
(`"saglik_indekse_dahil": false`).

## İndeks sınırı

`BILGI_KAYIT_MAX`: ana bilgisayarda 4096, ÖTÜKEN'de 96 (ELF bellek
payı dar). Sınır dolarsa günlüğe açıkça yazılır — sessizce kesilmez.
