# Ring3 yalıtımı — 1.4.7 durumu

Tarih: 2026-09-28 · İlgili: `src/process.c`, `src/sched.c`, `src/ring3.c`, `src/vmm.c`, `src/sinama147.c`

## 1. Özet

| Başlık | Durum |
|---|---|
| Çekirdek adres alanına geri düşme (fallback) kaldırıldı | **YAPILDI** |
| Süreç yaşam döngüsü: sonlandırma isteği ↔ geri kazanım ayrıldı | **YAPILDI** |
| Canlı adres alanı sayacı + sızıntı sınaması | **YAPILDI** |
| Ring3 hata yalıtımı (Ring3 hatası yalnız o süreci öldürsün, çekirdek sürsün) | **YAPILMADI** |

## 2. YAPILMADI: Ring3 hata yalıtımı

Görevdeki P0 "Ring3 fault isolation" maddesi bu sürümde **uygulanmadı**. Bu maddenin tasarımı ve sınama programları üzerinde çalışırken üretilen içerik bir güvenlik denetleyicisi tarafından durduruldu; aynı içeriği başka biçimde yeniden üretmemem gerekiyor. Bu yüzden:

- Ring3 kaynaklı bir sayfa hatası/genel koruma hatası bugün de **1.4.6'daki gibi** ele alınır (hata işleyicisinde değişiklik yok).
- Kasıtlı hata üreten Ring3 sınama programları **yazılmadı**; "Ring3 hatasından sonra çekirdek sürdü" diye bir iddia bu sürümde **yoktur**.
- Bu madde bir sonraki sürümde, bu oturumun dışında ele alınmalıdır. Teknik borç listesindedir.

## 3. YAPILDI: fallback kaldırıldı

1.4.6'da `process_create` süreç için adres alanı kuramazsa süreci **çekirdek adres alanında** (paylaşılan PML4) çalıştırıyordu. Bu, yalıtımın sessizce kaybolması demekti.

1.4.7:
- `process_create__ic` önce adres alanını kurar. Kuramazsa **süreç oluşturulmaz**, 0 döner ve "surec OLUSTURULMUYOR (yalitim sarttir)" günlüğe yazılır.
- Görev `task_create_adresli(ad, giris, alan, 1)` ile, **çalıştırılabilir olmadan önce** cr3'üne bağlanır (araya zamanlayıcı girip görevi yanlış cr3 ile koşturamaz).
- Görev kurulamazsa adres alanı yıkılır (sızıntı yok).
- Sınama: `SINAMA147 ASYOK` gerçek bellek tükenmesi üretir (sahte değil: PMM'den çerçeve bitene dek alınır), `process_create`'in 0 döndüğünü ve hiçbir çerçevenin sızmadığını denetler. Ölçüm: 128 MB'da 31 019, 512 MB'da 129 056 çerçeve alındı, `kalan 0`, sızan 0.

Bu sınama bir **PMM hatası** da buldu: `pmm_init` bitmapi yeniden sayıyor ama `g_used_frames`'i eşitlemiyordu; bellek haritası deliklerindeki çerçeveler "boş" sayılıyordu (serbest bellek fazla gösteriliyordu). Düzeltildi.

## 4. YAPILDI: yaşam döngüsü

- `scheduler_terminate_task` artık geri kazanımı (reap) **kendi içinde yapmaz**. 1.4.6'da reap `surec_kilit` (kesmeleri kapatan dönen kilit) tutulurken koşuyordu; reap IPI gönderebilir ve uyuyan kilit alabilir → kilitlenme tehlikesi.
- `process_destroy` önce `surec_kilit`'i bırakır, sonra `scheduler_reap_terminated()` çağırır.
- Reap şunları temizler: açık dosyalar (`vfs_gorev_bitti`), TÖRE kaydı (`tore_gorev_bitti`), ULGEN yüzeyleri (`ulgen_gorev_bitti`), adres alanı.
- `vmm_canli_alan_sayisi()`: canlı adres alanı sayacı. `SUREC-SMP` sınaması (6 süreç, smp 4) sayacın 1 → 7 → 1 döndüğünü ve eşzamanlı sonlandırmada ret sayısının > 0 olduğunu (yarış gerçekten yaşandı) denetler; smp 4'te 5 ret görüldü.

## 5. Ring3 yetki profilleri

`ring3_hazirla(profil)` Ring3'e geçişten hemen önce görevin TÖRE yeteneklerini ve `kullanici=1` bayrağını koyar. Profiller (`include/tore.h`):

| Profil | Yetenekler |
|---|---|
| SINAMA | EKRAN, TANI |
| KABUK | DISK_READ, MEM_ALLOC, IPC, EKRAN, KLAVYE, SISTEM |
| UMAY | DISK_READ, MEM_ALLOC, NET_SEND, NET_RECV, IPC, EKRAN, KLAVYE, SES, SISTEM |
| CEKIRDEK (Ring0) | tümü |

Çağrı başına denetim: `docs/FD-YETKILENDIRME.md`.
