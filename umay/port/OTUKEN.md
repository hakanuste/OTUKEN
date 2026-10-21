# UMAY'ı ÖTÜKEN'e bağlamak

UMAY tek başına çalışan bir Linux programıdır, ama ÖTÜKEN'e bağlanmak
üzere bırakıldı. Bağlanma noktası **tek bir katman**dır: `port/`.

## Değişen tek şey

```
ana bilgisayar        ÖTÜKEN
───────────────       ─────────────────────
port/umay_port_host.c  →  port/umay_port_otuken.c
gui/umay_gui_konsol.c  →  gui/umay_ulgen.c
ses/umay_piper.c       →  (çekirdekteki src/dede.c, syscall ile)
kabuk/umay_cli.c       →  _start (ÖTÜKEN portunun içinde)
```

Çekirdekte (`core/`, `alp/`, `us/`) **tek bir koşullu derleme yoktur**.
Platform farkı ne varsa port katmanındadır.

## Komutlar

    make otuken-denetle     # dikiş sağlam mı: ÖTÜKEN kipinde derlenir
    make otuken OT=../ot    # gerçek yapım: kov/umay.elf

`otuken-denetle` ÖTÜKEN ağacına dokunmaz, yalnız derlemeyi denetler.
Her değişiklikten sonra koşulabilir; dikişin sessizce bozulmasını
engeller.

## Port yüzeyi

Çekirdeğin ÖTÜKEN'den istediği her şey `include/umay_port.h`
içindedir: zaman, günlük, bellek, dosya, IPC, pencere, süreç, yetki,
US motoru, ses, kütüphane kökü. Yaklaşık otuz işlev. Yeni bir platform
eklemek, bu otuz işlevi yazmak demektir.

İki işlev kütüphane yerini belirler ve platform farkını orada tutar:

| işlev | ana bilgisayar | ÖTÜKEN |
|---|---|---|
| `up_kut_kok()` | `UMAY_KUT`, `./kut`, kurulum yeri, `~/.local/share/umay/kut`, `/usr/local/share/umay/kut` | `/` |
| `up_kut_dosya("umay.us1", ...)` | `<kök>/umay.us1` | `/UMAY.US1` (8.3, büyük harf) |

ÖTÜKEN'in FAT16 birimi düz köktür ve adlar 8.3 büyük harftir. Bu bilgi
bir zamanlar çekirdeğin ortasında `#ifdef` ile duruyordu; artık yalnız
portta.

## Çekirdekten beklenenler

ÖTÜKEN tarafında şunlar hazır olmalı (hepsi bugün var):

- Çağrı numaraları `include/syscall.h` ile uyumlu (port 0..35 kullanır)
- Dördüncü argüman **R10**'dan okunur (SYSCALL, RCX'i dönüş adresi için
  kullanır; genel kısıt verilirse çekirdeğe çöp gider — bu yaşanmış
  bir hatadır, port dosyasının başında yazılı)
- `UMAY.ELF`, `UMAY.US1`, `SOZLUK.TXT` FAT16 kökünde
- Çekirdekte `OTUKEN_UMAY_BAGLI` açık (`include/otukenconf.h`)

## Ses

ÖTÜKEN'de Piper koşmaz: ONNX çalışma zamanı ve kayan nokta ister,
ikisi de yok. Orada çekirdeğin kendi formant sentezleyicisi
(`src/dede.c`) devrededir; port `SYS_DEDE_KONUS` ile çağırır.

Piper'ı ÖTÜKEN'e taşımak, VITS çıkarımını tamsayı C'ye yazmak demektir
— US1'de bir kez yapılan işin büyüğü.
