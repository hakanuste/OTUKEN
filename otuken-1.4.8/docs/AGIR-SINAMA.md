# ÖTÜKEN — AĞIR SINAMA RAPORU (1.4.2)

Tarih: 2026-09-27 · QEMU 8.2.2 · Kanıtlar: `kanitlar/agir/ozet.txt`, `kanitlar/agir-kayitlari.tar.xz`

## 1. Ne sınanıyor

`agir_sinama()` (src/kernel.c) yalnızca `OTUKEN_TANI_MODU=1` ve `OTUKEN_AGIR_SINAMA=1` açıkken koşar. Üretim çekirdeğine girmez.

Her turda **8 görev eşzamanlı** başlatılır. Her görev 40 iç tur boyunca şunları yapar:

| Eksen | Ne yapar | Neyi yakalar |
|---|---|---|
| Yığın (heap) | 8 blok, 16–8192 bayt değişken boy, desen yaz → doğrula → bırak | bozulma, çift kullanım, parçalanma |
| Dosya okuma | `/OTUKEN.TXT` oku, başvuru kopyasıyla karşılaştır | depolama kilidi yarışı |
| Dosya yazma | görev başına `/AGIRx.TXT` 300 bayt yaz, geri oku | FAT16 yazma yolu, küme tahsisi |
| Boru | `pipe_t` yaz/oku gidiş-dönüş | IPC tamponu |
| İleti kuyruğu | `msgqueue_t` gönder/al | IPC kuyruğu |
| Muteks | ortak sayaç artırımı | kayıp güncelleme (beklenen = 8 × 40 × 6 = 0x780) |

Her turun sonunda `pmm_free_frames()` ve heap kullanımı ölçülür, ardından `AGIR-TUR GECTI/KALDI` satırı basılır.

**Sızıntı ölçütü:** 2. turdan sonraki turların **yarısından fazlasında** büyüme varsa sızıntı sayılır. Büyüme, 4 çerçeveden fazla PMM düşüşü ya da 4 KB'den fazla heap artışı demektir. Tek bir basamak sayılmaz; gerekçesi 3. bölümde.

## 2. Bulunan hata: aralıklı SMP kilitlenmesi (DÜZELTİLDİ)

**Belirti:** `-smp 4`'te yaklaşık her 4 açılıştan 1'inde sistem SMP-YARIS ya da AĞIR sınaması sırasında donuyordu. Önceki takımlarda bu görülmemişti, çünkü yük hafifti.

**Yakalama:** Takılma yakalayıcısı (QEMU monitörü, çekirdek başına RIP ve yığın zinciri) 4. denemede şu durumu kaydetti:

```
cagri_kilit = 1     cagri_maske = 0
cpu0: smp_cagir_digerleri+0x4f  <- vmm_map_page <- heap_extend <- kmalloc   (heap_kilit ONDA)
cpu1: spin_lock_irqsave (heap_kilit)  <- kmalloc
cpu2: spin_lock_irqsave (heap_kilit)  <- kmalloc
cpu3: spin_lock_irqsave (heap_kilit)  <- kfree
```

**Neden:** Çağrı kilidi `1`, fakat hiçbir çekirdek onu tutmuyor. `smp_cagir_digerleri` kilidi **kesmeler açıkken** alıyordu. Kilidi alan görev zamanlayıcı kesmesiyle askıya alınıp başka göreve geçilince kilit **uyuyan** bir görevde kaldı. Ardından cpu0 heap kilidini tutarken TLB vuruşu için çağrı kilidini beklemeye başladı. Diğer üç çekirdek heap kilidinde IF=0 ile dönüyordu. Böylece uyuyan görevi çalıştıracak hiçbir çekirdek kalmadı.

**Düzeltme (src/smp.c):** Çağrı boyunca `irq_kaydet_kapat()` / `irq_geri_yukle()` kullanılıyor. `ben` (çekirdek indeksi) de kesmeler kapandıktan sonra okunuyor; önceden görev başka çekirdeğe göçerse yanlış indeks kalabiliyordu. Öteki elle yazılmış kilitler de denetlendi: `rq_kilit` ve `seri_kilit` zaten IF=0 altında alınıyor, onlarda aynı sorun yok.

**Doğrulama:**

| Durum | Koşu | Takılma |
|---|---|---|
| Düzeltme öncesi | 4 yakalayıcı denemesi + önceki 2 serbest koşu | 3 |
| Düzeltme sonrası | 12 yakalayıcı denemesi + 49 matris koşusu + 4 kabuk koşusu | 0 |

Kalan küçük olasılık dürüstçe belirtilmeli: aralıklı bir hatanın yokluğu olasılıksal olarak kanıtlanır. Tek çekirdekte bu hata oluşamaz, çünkü çağrı IPI'si gönderilmiyor. Bu yüzden yalnızca çok çekirdekli koşular sayılmalı: 12 + 40 matris + 2 kabuk = 54 koşu. Düzeltme öncesindeki ~%25 oranla bu 54 koşunun hepsinin şans eseri temiz geçme olasılığı 0,75⁵⁴ ≈ 2·10⁻⁷.

## 3. Ölçüt hataları (benim hatalarım, düzeltildi)

1. **İlk sürüm** son ölçümü ilk ölçümle mutlak olarak karşılaştırıyordu. Heap PMM'e bellek geri vermediği için ilk turlarda bir kullanım düzeyine çıkıp orada kalıyor. Bu yüzden düzleşen eğriye yanlışlıkla "SIZINTI VAR" diyordu.
2. **İkinci sürüm** turların ikinci yarısındaki toplam farka bakıyordu. Matriste 3 yanlış alarm verdi. Kaynağı kayıtta görüldü: arka plandaki UMAY penceresi (`ULGEN: yuzey olusturuldu 'UMAY — Ötüken'`) zamanlamaya bağlı olarak herhangi bir turda **bir kez** 0xBA çerçeve (~744 KB) ayırıyor. Bu kalıcı ve meşru bir ayırma.
3. **Son ölçüt:** tekrarlayan büyüme. AĞIR görevlerinden gelen gerçek bir sızıntı her turda tekrarlar; tek seferlik bir arka plan ayırması ise yalnızca bir turda görünür.

**Kontrol grubu:** `-DAGIR_KONTROL_SIZINTI` ile her iç turda bir blok bilerek bırakılmıyor. Ölçüt bunu **6/6 turda** yakaladı (`buyuyen_tur=6/6`, `SIZINTI VAR`). Böylece ölçütün hem yanlış alarm vermediği hem de gerçek sızıntıyı kaçırmadığı gösterilmiş oldu.

## 4. Matris sonuçları (`./agir_sinama.sh 3 64`)

| Eksen | Değerler | Sonuç |
|---|---|---|
| Kontrol grubu | bilerek sızıntı, -smp 4 | beklendiği gibi KALDI (SIZINTI VAR) |
| Çekirdek × CPU × tekrar | -smp 1/2/4/8/16 × max/qemu64/EPYC × 3 | 45/45 GEÇTİ |
| Bellek kenarları | 128 MB, 2048 MB (-smp 4) | 2/2 GEÇTİ |
| Soak | 64 tur × 8 görev (-smp 4, -smp 8) | 2/2 GEÇTİ, `buyuyen_tur=0/62` |
| **Toplam** | **49 koşu** | **49 geçti, 0 kaldı, 0 takıldı** |

Ek gözlemler:

- `-smp 16`'da 16 çekirdeğin 16'sı da AĞIR görevlerini koşturdu (`cekirdek_sayisi=0x10`).
- Muteks sayacı bütün koşularda tam çıktı (`mutex_art=0x780/0x780`); kayıp güncelleme yok.
- Süreler: -smp 1 → ~10 sn, -smp 4 → ~16 sn, -smp 16 → ~30 sn; soak 64 tur → 89–103 sn.

## 5. Gerileme denetimi (düzeltmeden sonra, git 308da4d)

| Takım | Sonuç |
|---|---|
| `./testler.sh` | 81/81 |
| `./donanim_testleri.sh` | 43/43 |
| `kabuk_testi.py` | -smp 1 ×2 ve -smp 4 ×2 → her biri 5/5 |
| UMAY mantık takımı | 202/202 |
| US1 | 10/10 |

## 6. Sınanmayanlar (test edilmedi)

- Gerçek donanım (Ryzen 9). Bütün sonuçlar QEMU TCG üzerinde alındı; KVM yok.
- UEFI önyüklemeyle AĞIR sınama. UEFI yolu yalnızca `donanim_testleri.sh`'nin kendi kontrolleriyle sınandı.
- Ağ yükü: AĞIR görevleri ULAK/TCP'ye yük bindirmiyor.
- Saatler süren soak. En uzun koşu 64 tur, yaklaşık 100 sn.
- Kötü niyetli girdiyle sınama bu çalışmanın kapsamı dışında tutuldu. Yalnızca eşzamanlılık, yük, soak ve sızıntı eksenleri sınandı.
