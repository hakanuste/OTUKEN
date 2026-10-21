# ÖTÜKEN

Sıfırdan yazılmış 64 bit x86 işletim sistemi ve üstünde koşan UMAY
yapay us uygulaması.

| dizin | ne |
|---|---|
| `cekirdek/` | ÖTÜKEN çekirdeği — uzun kip, sayfalama, Ring3, VFS/FAT16, TCP/IP, ÜLGEN arayüzü, TÖRE yetki dizgesi |
| `umay/` | UMAY — tek başına çalışan Türkçe yapay us yardımcısı |
| `us1/` | US1 — tamsayı aritmetiğiyle koşan küçük dönüştürücü motoru ve eğitim araçları |

## Çekirdek

```
cd cekirdek
make            # x86_64 derler
make iso        # açılabilir ISO
bash testler.sh # tam gerileme sınaması
```

Sınama takımı 80 başlıkta koşar: bellek, zamanlayıcı, süreç, Ring3
yalıtımı, dosya dizgesi, ağ, ses. Ses bölümü üretilen dalgayı
**ölçerek** sınar — ünlülerin ayırt edilebilirliği, ünsüz ayrımı,
hece vurgusu, duraklarda sessizlik, çıkış taşması.

## UMAY

```
cd umay
./kur.sh              # derler, sınar, kurar, doğrular
./kur.sh --ses        # Piper ses arka ucunu da kurar
umay                  # konuşma kipi
umay "soru"           # tek soru
```

OTAĞ meclisi, 12 uzman ajan, OZAN dil katmanı, BİLGİ kitaplığı, BENGİ
kalıcı belleği, değiştirilebilir US motoru ve ses arka ucu. 207 sınama.

ÖTÜKEN'e bağlanma noktası tek bir katmandır (`umay/port/`); çekirdek
kodunda tek bir koşullu derleme yoktur.

```
cd umay
make otuken-denetle   # dikiş sağlam mı (ÖTÜKEN ağacı gerekmez)
make otuken OT=../cekirdek
```

## Nerede duruyoruz

**Dil modeli bir oyuncaktır.** US1 motorunun izin verdiği en büyük
model 467 bin değişkendir; o boyut Türkçenin harf dizilimini öğrenir,
anlamlı cümle kurmaz. Ana bilgisayarda US katmanı Ollama'ya
bağlanabilir ve gerçek bir modelle yanıt verir; ÖTÜKEN'in kendi
üstünde US1 kalır.

**Ses iki ayrı yerde iki ayrı şeydir.** ÖTÜKEN'de `cekirdek/src/dede.c`
— tamsayı aritmetiğiyle çalışan formant sentezleyici; anlaşılır ama
mekanik. Ana bilgisayarda Piper — sinir ağı tabanlı, doğal. Formant
sentezi belirli bir insanın ses kimliğini taşıyamaz.

Her ikisi de sahte başarı üretmez: arka uç hazırlanamazsa açıkça
söylenir ve bir alt seçeneğe düşülür.
