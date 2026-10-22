# Dosya tanıtıcıları ve çağrı yetkilendirmesi — 1.4.7

Tarih: 2026-09-28 · İlgili: `src/vfs.c`, `src/syscall.c`, `src/tore.c`, `src/ulgen.c`, `src/sinama147.c`

## 1. Dosya tanıtıcısı (FD) sahipliği

1.4.6'da tanıtıcı açık dosyalar dizisinin **yuva numarasıydı**; hangi görevin açtığı tutulmuyordu. Bir süreç başka bir sürecin tanıtıcısını tahmin edip okuyabilir/kapatabilirdi; kapatılıp yeniden kullanılan yuvanın eski tanıtıcısı yeni dosyaya işaret ederdi.

1.4.7:

```
tanıtıcı = (kuşak << 4) | yuva
```

- **sahip**: dosyayı açan görevin kimliği. Yalnız o görev okur, konumlar (lseek) ve kapatır.
- **kuşak**: yuva her açılışta 1 artar. Kapanmış ya da yeniden kullanılmış yuvanın eski tanıtıcısı **bayat** sayılır ve reddedilir.
- Kuşak 1'den başlar: tanıtıcı her zaman ≥ 16 (0/1/2 standart akışlarla karışmaz) ve < 2³⁰ (negatif hata dönüşleriyle karışmaz).
- `tut_coz()` üç şeyi birlikte denetler: yuva geçerli ve açık, kuşak güncel, sahip = çağıran.
- Kilit: `fd_kilit` (kısa, dönen). Disk G/Ç `depo_kilit` (uyuyan) altındadır. Sıra **depo_kilit → fd_kilit**, asla tersi. Reap yalnız `fd_kilit` alır; uyumaz.
- Süreç ölünce reap `vfs_gorev_bitti(kimlik)` ile o sürecin tüm açık dosyalarını kapatır.

Sınama `SINAMA147 FD`: başka görevin tanıtıcısı reddedilir, bayat tanıtıcı reddedilir, reap sonrası `vfs_acik_sayisi()` başlangıca döner.

## 2. Çağrı politika tablosu

1.4.6'da 36 sistem çağrısından **33'ünün** yetenek denetimi yoktu (yalnız mkdir, write_file, proc_liste içeride `tore_check` yapıyordu). 1.4.7'de `syscall_dispatch` içinde **tek karar noktası** vardır: `sc_politika[SYS_MAX]` tablosu ve `sc_izinli()`.

- Tabloda satırı olmayan (politikası yazılmamış) çağrı **reddedilir** ve günlüğe yazılır. `syscall_politikasiz_sayisi()` 0 olmalı; `SINAMA147 POLITIKA` bunu denetler.
- SERBEST çağrılar yetenek istemez.
- Çağıran Ring0 çekirdek görevi ise (`task->kullanici == 0`) denetim atlanır; Ring3 görevleri `ring3_hazirla` ile `kullanici=1` alır.
- Çağrı içindeki eski dağınık `tore_check`'ler kaldırıldı (çift denetim yok).

| No | Çağrı | Gereken yetenek |
|---:|---|---|
| 0 | SYS_WRITE (konsol) | EKRAN |
| 1 | SYS_GETPID | SERBEST |
| 2 | SYS_YIELD | SERBEST |
| 3 | SYS_SLEEP | SERBEST |
| 4 | SYS_RING3_PROOF | TANI |
| 5 | SYS_EXIT | SERBEST |
| 6 | SYS_OPEN | DISK_READ |
| 7 | SYS_READ | DISK_READ |
| 8 | SYS_CLOSE | DISK_READ |
| 9 | SYS_SBRK | MEM_ALLOC |
| 10 | SYS_READKEY | KLAVYE |
| 11 | SYS_LISTDIR | DISK_READ |
| 12 | SYS_PUTPIXEL | EKRAN |
| 13 | SYS_TICKS | SERBEST |
| 14 | SYS_PENCERE_AC | EKRAN |
| 15 | SYS_PEN_DOLDUR | EKRAN |
| 16 | SYS_PEN_YAZI | EKRAN |
| 17 | SYS_PEN_DIK | EKRAN |
| 18 | SYS_PEN_KAPAT | EKRAN |
| 19 | SYS_SERVIS_LISTE | SISTEM |
| 20 | SYS_IPC_ACS | IPC |
| 21 | SYS_IPC_BUL | IPC |
| 22 | SYS_IPC_GONDER | IPC |
| 23 | SYS_IPC_AL | IPC |
| 24 | SYS_SISTEM_BIL | SISTEM |
| 25 | SYS_WRITE_FILE | DISK_WRITE |
| 26 | SYS_STAT | DISK_READ |
| 27 | SYS_MKDIR | DISK_WRITE |
| 28 | SYS_YETKI | SERBEST |
| 29 | SYS_PROC_LISTE | PROC_CREATE |
| 30 | SYS_PEN_SUN | EKRAN |
| 31 | SYS_PEN_OLAY | KLAVYE |
| 32 | SYS_SES_NOTA | SES |
| 33 | SYS_OZAN_SOYLE | SES |
| 34 | SYS_TEMA | AYAR |
| 35 | SYS_DEDE_KONUS | SES |

Toplam: 30 yetenek isteyen, 6 SERBEST.

Yeni yetenek bitleri (`include/tore.h`): IPC (8), EKRAN (9), KLAVYE (10), SES (11), SISTEM (12), AYAR (13), TANI (14). Profiller: `docs/RING3-IZOLASYON.md` §5.

Bilinçli sonuçlar:
- KABUK ve UMAY profillerinde **DISK_WRITE yok**: Ring3'ten dosya yazma/dizin oluşturma reddedilir (1.4.6'da UMAY'ın profilinde de DISK_WRITE yoktu; çağrının içindeki denetim reddediyordu).
- PROC_CREATE yalnız Ring0'da: Ring3 süreç listesini okuyamaz.
- AYAR (tema) Ring3 profillerinde yok.

## 3. ULGEN pencere sahipliği

1.4.6'da pencere kimliği bilen her program başkasının penceresine çizebilir, olaylarını okuyabilir (klavye girdisi dahil) ya da onu kapatabilirdi. 1.4.7:

- Yüzeyin `sahip` alanı var; `yuzey_benim()` pen_doldur / yazı / dik / kapat / olay çağrılarında sahipliği denetler.
- Yüzey boyu sınırlı (ekran boyu; çerçeve arabelleği yoksa 1920×1080). 1.4.6'da tek bir pencere 64 MB çekirdek yığınını tüketebiliyordu.
- Reap `ulgen_gorev_bitti()` ile ölen sürecin yüzeylerini kapatır.

Sınama `SINAMA147 YUZEY`: sahibi çizebilir; yabancı görev aynı yüzeye çizemez ve onu kapatamaz; 65535×65535 ve sıfır boy reddedilir; sahibi ölünce reap yüzeyi siler. (Yabancının olay okuması aynı `yuzey_benim()` denetiminden geçer ama ayrıca sınanmadı.)

## 4. TÖRE tablosu sızıntısı

1.4.6'da süreç ölünce TÖRE kaydı silinmiyordu; tablo dolunca yeni programlar yetkisiz kalıyordu. `tore_gorev_bitti()` reap'te kaydı siler. Sınama `SINAMA147 TOREREAP` yalnız kendi yardımcı görevlerinin kimliklerini izler (kabuğun aynı anda kayıt açması sınamayı etkilemez).
