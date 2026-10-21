# ÖTÜKEN 1.4.8 (depolama sürümü)

Kökteki çekirdekten AYRI tutulan tam sürüm; UMAY ile bağlantı için gereken dosyaları
(`umay/`, `us1/`, `userprog/`, `src/umay.c`, `src/kervan.c` ...) birlikte taşır.

İçerik: denetim düzeltmeleri (PMM, heap, sched, syscall, VFS, TCP, ulak, FAT16, AHCI, DNS, DHCP,
iskence), her disk için okuma+yazma (ATA master/slave, AHCI, NVMe), FAT32 yazma + alt dizin +
GPT bölüm ofseti, VFS `write_file`/`mkdir`/`stat` işlemleri.

Test (QEMU): testler.sh 94/94, donanim_testleri.sh 43/43, kabuk_testi.py 1 ve 4 çekirdek 9/9.
Gerçek donanımda denenmedi. Bilinen eksikler: TCP yeniden gönderim/alma tamponu, TLS, e1000 dışı ağ
sürücüsü, FAT'ta uzun dosya adı.

Disk imajları (`diskfiles.img`, `fat32test.img`) ve derleme çıktıları depoya konmadı
(.gitignore); testler için yeniden üretilmelidir (`kur.sh`, `mkfs.fat`).
