# ÖTÜKEN 1.5

1.4.8 (depolama sürümü) üzerine kurulu sürüm; UMAY bağlantısı için gereken dosyalarla
(`umay/`, `us1/`, `userprog/`, `src/umay.c`, `src/kervan.c` ...) birlikte kökteki çekirdekten ayrı tutulur.

İçerik: denetim düzeltmeleri (PMM, heap, sched, syscall, VFS, TCP, ulak, FAT16, AHCI, DNS, DHCP,
iskence), her disk için okuma+yazma (ATA master/slave, AHCI, NVMe), FAT32 yazma + alt dizin +
GPT bölüm ofseti, VFS `write_file`/`mkdir`/`stat` işlemleri.

Test (QEMU): testler.sh, donanim_testleri.sh, kabuk_testi.py (1 ve 4 çekirdek) geçti.
Gerçek donanımda denenmedi.

Bilinen eksikler: TCP yeniden gönderim/alma tamponu, TLS, e1000 dışı ağ sürücüsü, FAT'ta uzun dosya
adı, AC'97 dışı ses (Intel HDA yok) ve ses sürücüsünün başlatılmaması (`ses_init()` çağrılmıyor).

Disk imajları (`diskfiles.img`, `fat32test.img`) ve derleme çıktıları depoya konmadı (.gitignore).
