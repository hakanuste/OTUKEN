# ÖTÜKEN 1.0

Sıfırdan yazılmış 64-bit işletim sistemi. x86_64 ve AArch64 (ARM64)
mimarilerinde çalışır.

## Hızlı başlangıç

```sh
make && make iso
qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=diskfiles.img,format=raw,if=ide,index=0 \
  -netdev user,id=n0 -device e1000,netdev=n0 -m 128
```

Kabuk açıldığında `yardim` yazın. **Klavye Türkçe-Q düzenindedir**:
`i` harfi apostrof tuşundadır, QWERTY'deki `i` tuşu `ı` üretir.

### ARM64 (temel port — kapsam için 'Bilinen sınırlar'a bakın)

```sh
cd arm64 && make
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -nographic \
  -kernel otuken-arm64.img
```

### Testler

```sh
./testler.sh      # 35 kontrol, iki mimari
```

## Kabuk komutları

| Komut | İşlev |
|---|---|
| `yardim` | komut listesi |
| `listele [yol]` | dizin içeriği |
| `goster <dosya>` | dosya içeriği |
| `kimlik` | süreç kimliği (PID) |
| `saat` | zamanlayıcı tiki |
| `bellek` | userland bellek sınaması |
| `ciz` | ekrana grafik çizer |
| `umay <soru>` | UMAY'a IPC ile soru sor |
| `cik` | kabuğu kapat (otomatik yeniden başlar) |

## Mimari

```
Ring3  │  KABUK        UMAY         (ayrı ELF programları)
       │     │           │
       │     └── syscall ┘
═══════╪══════════════════════════════════════════════
Ring0  │  ÇEKİRDEK (higher-half, 0xFFFFFFFF80000000)
       │  ├─ PMM / VMM / heap / süreç başına adres alanı
       │  ├─ Zamanlayıcı (preemptive) / süreç / thread
       │  ├─ Senkronizasyon (atomic/spinlock/mutex)
       │  ├─ IPC (mesaj kuyruğu, pipe, paylaşılan bellek, port)
       │  ├─ BİTİG: VFS + FAT16 (oku/yaz) + FAT32
       │  ├─ Sürücüler: PCI, ATA, AHCI, NVMe, USB, PS/2, PIT, e1000
       │  ├─ ULAK: Ethernet/ARP/IPv4/UDP/TCP/DHCP/DNS + soket
       │  ├─ TİMUR (NX, W^X) / TÖRE (yetki) / KUT (kimlik)
       │  ├─ ÜLGEN: pencere + compositor
       │  ├─ KERVAN: paket sistemi
       │  └─ Servis yöneticisi, günlük, kararlılık izleme
```

## Yapılandırma

`OTUKEN_TANI_MODU=1` ile derlerseniz tüm öz-sınamalar çalışır ve
ayrıntılı rapor verir. Varsayılan (0) üretim modudur: sessiz açılış.

```sh
make CFLAGS="-DOTUKEN_TANI_MODU=1 -mcmodel=kernel -ffreestanding ..." 
```

## Dizin yapısı

```
boot/        x86_64 önyükleme + kesme + syscall assembly
src/         çekirdek kaynağı
include/     başlıklar
userprog/    kullanıcı programları (kabuk, UMAY, ulib)
arm64/       AArch64 portu (ayrı derleme)
testler.sh   otomatik regresyon takımı
```

## Bilinen sınırlar

- USB: UHCI aktarım katmanı çalışır (HID klavye numaralandırılır ve
  okunur). XHCI/EHCI yalnızca tespit edilir; aktarım UHCI ile sınırlı
- AHCI: gerçek DMA okuma çalışır (READ DMA EXT). Yazma yok.
  NVMe: yalnızca denetleyici tanınır, veri yolu yok
- FAT: yazma yalnızca kök dizinde, 8.3 dosya adı
- **ARM64: TEMEL port.** Çalışan: önyükleme, PL011 seri çıktı, bitmap
  bellek yöneticisi, yığın, 64-bit aritmetik. **Yok:** GIC (kesme
  denetleyicisi), zamanlayıcı, MMU/sayfalama, görev değiştirme,
  sürücüler, kullanıcı modu. x86_64'teki alt sistemler ARM64'e
  taşınmamıştır — bu bir "tam destek" değildir.
- UMAY: gerçek dil modeli değil — barındırma ve API katmanı
  (ayrıntı: `UMAY-MIMARI.md`)
