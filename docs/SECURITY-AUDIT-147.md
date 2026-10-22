# Güvenlik denetimi — 1.4.7

Tarih: 2026-09-28

## 1. Kapsam ve sınır

Bu belge **savunma amaçlı bulgu listesidir**: ne bulundu, nerede, nasıl düzeltildi, hangi sınama doğruluyor. Sömürü ayrıntısı içermez.

**İkinci denetimin devamı (her bulgu için sömürülebilirlik çözümlemesiyle SEC-XXX kayıtları) YAPILMADI.** Bu çalışma sırasında üretilen içerik bir güvenlik denetleyicisi tarafından durduruldu; aynı içeriği yeniden üretmedim. Aşağıdaki liste, 1.4.7 geliştirmesi sırasında kod okunarak ve sınamalarla bulunmuş, **düzeltilmiş** sorunlardır; kapsamlı bir denetimin yerini tutmaz.

Ring3 hata yalıtımı da yapılmadı (bkz. `docs/RING3-IZOLASYON.md` §2).

## 2. Bulunan ve düzeltilen sorunlar

| # | Alan | Sorun | Düzeltme | Sınama |
|---|---|---|---|---|
| 1 | Süreç | Adres alanı kurulamazsa süreç çekirdek adres alanında koşuyordu (yalıtım sessizce kayboluyordu) | Süreç oluşturulmaz | SINAMA147 ASYOK |
| 2 | Çağrılar | 36 çağrının 33'ünde yetenek denetimi yoktu | Tek noktada politika tablosu; tanımsız = ret | SINAMA147 POLITIKA |
| 3 | FD | Tanıtıcı = yuva numarası; sahip ve kuşak yoktu (başkasının dosyası, bayat tanıtıcı) | Sahip + kuşak | SINAMA147 FD |
| 4 | ULGEN | Pencere sahipliği yoktu (başkasının penceresine çizme, olay okuma, kapatma) | `sahip` + `yuzey_benim()` | SINAMA147 YUZEY |
| 5 | ULGEN | Pencere boyu sınırsız: tek pencere çekirdek yığınını (64 MB) tüketebiliyordu | Ekran boyu sınırı; `size_t` döngü | SINAMA147 YUZEY |
| 6 | TÖRE | Ölen sürecin kaydı silinmiyordu; tablo dolunca yeni süreçler yetkisiz | `tore_gorev_bitti` | SINAMA147 TOREREAP |
| 7 | Zamanlayıcı | Reap, kesmeleri kapatan kilit altında koşuyordu (IPI + uyuyan kilit → kilitlenme tehlikesi) | Reap kilit dışına alındı | SINAMA147 SUREC-SMP |
| 8 | PMM | Serbest çerçeve sayısı fazla gösteriliyordu (`g_used_frames` eşitlenmiyordu) | Yeniden sayımdan sonra eşitleme | SINAMA147 ASYOK |
| 9 | UMAY KANIT | Dağınık kanıt kabulü: sorunun terimleri farklı kayıtlarda geçince "kanıtlı" sayılıyordu | Kayıt düzeyinde kapsama | UMAY sina 279, `olc.py` dağınık kümesi |

## 3. Denetim sırasında değişmeyenler

- Kullanıcı işaretçisi doğrulaması (çağrı bağımsız değişkenleri) 1.4.6'daki gibidir; bu sürümde genişletilmedi.
- US1 model ayrıştırıcısı v2 alanları için bulandırıldı (`tests/us1_fuzz`: v1 36/36, v2 46/46).

## 4. Açık kalanlar

- Ring3 hata yalıtımı (P0) — yapılmadı.
- İkinci denetimin devamı — yapılmadı.
- Ağ yığını ve sürücüler bu sürümde denetlenmedi.
