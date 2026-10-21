# UMAY

Türkçe konuşan, çevrim dışı çalışan bir yapay zekâ yardımcısı.
Dede Korkut'un dilinden söyler.

Tek bir Linux programıdır. ÖTÜKEN'e bağlı değildir: ne çekirdek
kaynağı, ne bağlayıcı betiği, ne de ÖTÜKEN'e özgü bir koşullu derleme
içinde kalmıştır.

## Kurmak

    ./kur.sh              # derler, sinar, kurar, dogrular
    ./kur.sh --ses        # sesi de kurar
    ./kur.sh --onek=/usr/local

Betik derleyiciyi ve make'i yoklar, 207 sinamayi kosar, `~/.local`a
kurar, PATH'i denetler ve sonunda **baska bir dizinden calistirarak
dogrular**. Eksik olan sessizce gecilmez.

Kaldirmak:

    make kaldir

## Kullanmak

    umay                  konusma kipi (ucbirim)
    umay "bahcede domates nasil yetisir"    tek soru, yanitlar, cikar
    umay --surum
    umay --yardim

Tek soru kipi betiklerden cagirmak icindir: ayni ajanlar, ayni OZAN,
ayni ses; yanit verilir ve program ciker.

## Ne yapar

- **OTAĞ** — görevleri dağıtan meclis.
- **12 ALP** — uzman ajanlar: AKÇA (ekonomi), YER-SU (permakültür),
  KÜN (güneş enerjisi), DEMİR (elektronik), TULGA (otomotiv), OTACI
  (sağlık), YARGU (hukuk), BALBAL (yapı), ALPAGU (güvenlik), KOPUZ
  (müzik), OYUN (dans), YAZIT (arşiv). Yanında dört görevli: BİLGE,
  GÖZCÜ, ONARICI, YAZMAN.
- **OZAN** — soruyu anlar, niyeti çıkarır, yanıtı Dede Korkut diliyle
  söyler.
- **BİLGİ** — `kut/` altındaki belgeleri alanlara göre indeksler.
- **BENGİ** — kalıcı bellek; konuşmalar arasında taşınır.
- **US** — dil modeli katmanı. Değiştirilebilir arka uç: yerel US1
  motoru, IPC servisi ya da güvenli stub.
- **Ses** — Piper (sinir ağı tabanlı, çevrim dışı, Türkçe).

## Ses

    ./ses/piper_kur.sh

Piper'ı kurar, Türkçe ses modelini indirir, çalıcıyı yoklar. Sonra
UMAY bütün yanıtlarını sesle söyler.

**Sahte başarı yok.** Piper, ses modeli ya da çalıcı — biri bile
eksikse ses sürücüsü AÇILMAZ, günlüğe neden yazılır ve UMAY yalnız
yazıyla yanıtlar. Metni günlüğe basıp "konuştum" demek yankı yapmaktır.

Ayarlar:

| değişken | ne |
|---|---|
| `UMAY_PIPER` | piper çalıştırılabiliri |
| `UMAY_PIPER_SES` | `.onnx` ses modeli |
| `UMAY_SES_OYNAT` | çalıcı komutu |
| `UMAY_KUT` | kütüphane kökü |

## Kütüphane

UMAY verisini şu sırayla arar: `UMAY_KUT`, çalışma dizinindeki `kut/`,
kurulumda gömülen yer, `~/.local/share/umay/kut`,
`/usr/local/share/umay/kut`.

Bir aday yalnızca DİZİN olduğu için seçilmez; içinde gerçekten
kütüphane bulunmalıdır (`umay.us1` ya da `ortak/`). Yoksa önceki bir
koşudan kalma boş bir `kut/` klasörü düzgün kurulumu gölgeliyor ve
program sessizce yedek motora düşüyordu.

Kendi belgelerini `kut/` altındaki alan klasörlerine koy; UMAY açılışta
indeksler. Her belgenin başında künye olmalı:

    #kategori: otaci
    #baslik:   Temel İlk Yardım
    #etiket:   ilkyardim,travma
    #kaynak:   Saglik Bakanligi

Künye yoksa belge "ortak" havuza düşer ve 12 uzmanın hiçbiri göremez.

Ham bir arşivi (PDF'ten çıkarılmış metinler, klasörlenmiş belgeler)
otomatik çevirmek için:

    python3 arac/kitaplik_kur.py <ham_arsiv> kut

Künyeyi klasör adından yazar, PDF gürültüsünü temizler, büyük
belgeleri parçalara böler. Ayrıntı: `arac/KITAPLIK.md`.

## ÖTÜKEN'e bağlamak

UMAY tek başına çalışır ama ÖTÜKEN'e bağlanmak üzere bırakıldı.
Bağlanma noktası tek bir katmandır: `port/`. Çekirdekte (`core/`,
`alp/`, `us/`) tek bir koşullu derleme yoktur.

    make otuken-denetle      # dikiş sağlam mı (ÖTÜKEN ağacı gerekmez)
    make otuken OT=../ot     # gerçek yapım -> kov/umay.elf

`otuken-denetle` her değişiklikten sonra koşulabilir; dikişin sessizce
bozulmasını engeller. Ayrıntı: `port/OTUKEN.md`.

## Sınamak

    make sina

207 sınama. Ses katmanının dört hata yolu da sınanıyor: piper yok, ses
modeli yok, çalıcı yok, piper çöküyor.

## Dil modeli

`kut/umay.us1` şu an bir OYUNCAK modeldir: 2 katman, 64 boyut, 45
imlik sözlük, yaklaşık 50 bin değişken, ~900 karakterlik bir metinle
bir kez eğitilmiş. Motor doğru çalışıyor ve sınanıyor, ama bu modelden
anlamlı metin beklenmemeli. Gerçek bir Türkçe derlem gerekiyor.
