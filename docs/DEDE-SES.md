# Dede Korkut sesi — UMAY içinde konuşma sentezi (1.5)

UMAY yapay usun adıdır; yanıtını **Dede Korkut** söyler. 1.5'ten önce bu
ses çekirdekteki tamsayı formant sentezleyicisiydi (yaşlı erkek sesi
taklidi). Şimdi UMAY, kullanıcının verdiği Piper/VITS modelini (Türkçe,
22050 Hz) kendi içinde çalıştırır. Zincirde dış kitaplık yoktur:
onnxruntime, espeak, libc ve libm kullanılmaz.

```
metin ─► Türkçe sesletim (umay/ses/oses/turkce.c)
      ─► fonem kimlikleri (^ ... $, aralarda 0)
      ─► OSES: VITS çizgesi (umay/ses/oses/oses.c, model /DEDE.SES)
      ─► 22050 Hz float ─► 48000 Hz ikili kanal int16 (oses_ornekle.c)
      ─► SYS_SES_PCM (57, beklemez) ─► AC'97 DMA halkası
```

## Parçalar

| Dosya | İş |
|---|---|
| `umay/ses/oses/oses.c` | ONNX alt kümesi yorumlayıcısı: çizgenin kullandığı 50 işlem. Tensör ömrü son kullanıma göre; Reshape/Squeeze veriyi paylaşır. Öğe öğe işlemler, girdinin son kullanımıysa yerinde hesaplanır. |
| `umay/ses/oses/oses_mat.c` | exp, log, sqrt, tanh, erf, pow, ceil, softplus. Çift duyarlık, libm'e göre ≤1e-13 sapma. |
| `umay/ses/oses/oses_yigin.c` | Motorun öbeği. Tek alan üzerinde bölen ve birleştiren ayırıcı. UMAY'ın genel öbeği bölmez ve birleştirmez; binlerce farklı boyutta ayırma ile parçalanırdı. |
| `umay/ses/oses/turkce*.c` | Sesletim. Bağlam pencereli harf kuralları, ek tabanlı vurgu kuralları, istisna listeleri, sayılar ve kısaltmalar. 80 bayttan uzun cümle virgülden, yoksa boşluktan bölünür. Cümle başındaki ünlünün ardına uzunluk imi (ː) konur: model kısa başlangıç ünlüsünü yutuyordu; süreyi zorla uzatmak sessizlik üretti, ː sesli çalınıyor (dinlenerek seçildi). |
| `umay/ses/oses/oses_ornekle.c` | Catmull-Rom ile 22050 → 48000 Hz yeniden örnekleme. UMAY ve sınama aracı aynı işlevi kullanır. |
| `umay/ses/umay_dede_ses.c` | UMAY ses sürücüsü `dede-korkut-oses`. Modeli açılışta diskten okur. `ses_soyle` sözü kuyruğa atar. Her UMAY turunda pompa en çok bir parça sentezler ve halkaya sığdığı kadarını verir; UMAY beklemez. |
| `src/fpu.c`, `src/sched.c` | x87/SSE açılır (CR0.MP/NE, CR4.OSFXSR/OSXMMEXCPT). Görev başına 512 baytlık FXSAVE alanı tutulur; geçişte fxsave64/fxrstor64 yapılır. |
| `src/ses.c` | `ses_cal_dene`: halkada yer olduğu kadarını alır, dolunca 0 döner. |
| `src/syscall.c` | `SYS_SES_PCM` (KUT_SES). Görev başına heap sınırı: varsayılan 4 MB, UMAY 192 MB. Pencere 256 MB'tır. |
| `src/fat16.c` | Küme zinciri konum önbelleği. 64 MB'lık dosya önce karesel sürede okunuyordu (QEMU'da 293 sn); şimdi 24 sn. Her disk yazımı ve yeniden bağlama önbelleği geçersiz kılar. |

UMAY'ın yalnız `ses/umay_dede_ses.c` ve `ses/oses/*` dosyaları SSE ile
derlenir. UMAY'ın geri kalanı `-mno-sse` kalır ve arayüzde float değer
geçmez.

## Model

Model kullanıcının kendi Piper modelidir (izinli). Depoya konmaz;
`dede_model/` klasörü `.gitignore`'dadır.

```
python3 umay/ses/oses/arac/ses_cevir.py Claud1.onnx dede_model/dede.oses   # ONNX -> OSES (Constant'lar katlanır)
make umay-disk          # UMAY.ELF diskfiles.img'e
make dede-disk          # diskfiles.img içeriği + DEDE.SES -> 128 MB FAT16 diskfiles_dede.img
```

Ad 8.3 biçimindedir: `DEDE.SES`. FAT16 sürücüsü uzun ad okumaz. Diskte
model yoksa sürücü açılmaz; UMAY formant yoluna düşer ve bunu günlüğe
yazar.

## Ölçümler

- **OSES ile onnxruntime düğüm düğüm karşılaştırıldı.** 3854 düğümde
  sapan düğüm yok. Çıkıştaki en büyük fark 1,8e-5 (`umay/ses/oses/arac/karsilastir.py`; sürücü: `arac/sina_ana.c`).
- **Sesletim espeak-ng'nin Türkçe çıktısıyla karşılaştırıldı.** Ayrılmış
  sözcüklerde dizi tutması %95,7, vurgu tutması %95,8. Cümlelerde fonem
  hatası %1,3. Kalan farkların çoğu kısaltma ve yabancı sözcük.
- **Bellek:**
  - Kod çözücünün tepe belleği ses süresiyle orantılı, saniyede yaklaşık
    17 MB.
  - Uzun cümle bölündüğü için tepe 68 MB'ta kalır. 96 MB'lık alan
    kullanılır; 72 MB parçalanmaya yetmiyor.
- **Hız:**
  - Ana makinede (-O3) 8,3 sn ses 4,7 sn'de üretilir.
  - QEMU TCG'de (KVM yok) gerçek zamanın yaklaşık 75 katı yavaştır.
  - Gerçek donanımda ölçülmedi.

## Sınama (testler.sh, "Dede Korkut sesi")

1. Üretim ISO'su modelli diskle açılır; AC'97 çıkışı WAV'a (48 kHz)
   yakalanır.
2. UMAY'ın günlüğe yazdığı söz (`DEDE-SES: soz=`) ana makinede **aynı
   kaynaklarla** sentezlenir (`tests/dede_ses/kanit.c`).
3. Kartın çaldığı her parça yakalanan seste ayrı hizalanır ve beklenenle
   örnek örnek karşılaştırılır (`tests/dede_ses/karsilastir.py`). Eşik:
   ilinti ≥ 0,999, en büyük fark ≤ 64, sol = sağ. TCG yavaş olduğu için
   ilk 2 parça beklenir.
4. **Karşı sınama:** sözdeki bir sözcük değiştirilerek üretilen beklenen
   ses ESLEŞMEMELİ.

Elle yapılan tam koşu: açılış sözü 6 parça, 11,6 sn. Altı parçanın hepsi
bit bit aynı çıktı (ayni=1,0000, en büyük fark 0).

## Açık konular

- **Sesletim tablolarının kaynağı:** `turkce_tablo.c`, espeak-ng'nin
  (GPL-3) sözcük çıktılarından öğrenilmiştir. Tablonun dağıtım durumu
  karara bağlanmadı.
- **Gerçek donanım:** hız ve ses gerçek donanımda ve VirtualBox'ta
  denenmedi.
- **Yeniden örnekleme:** Catmull-Rom'dan sonra görüntü süzgeci yok.
- **Uzun sözler:** UMAY'ın söz kuyruğu 4 KB'tır; dolarsa yeni söz atlanır
  ve bu günlüğe yazılır.
- **Model ayrıştırıcısı:** eksen ve boyut denetimleri eklendi; tam
  denetimi yapılmadı. Model güvenilir disk dosyası sayılıyor.
