# ÖTÜKEN

Sıfırdan yazılmış 64 bit x86 işletim sistemi.

```
make             # x86_64 derler
make iso         # açılabilir ISO
bash testler.sh  # tam gerileme sınaması
```

## Ne var

Uzun kip, dört düzeyli sayfalama, süreç başına CR3, Ring3 yalıtımı,
ELF64 yükleyici ve W^X. Zamanlayıcı, iş parçacıkları, kilitler, boru
hatları, paylaşımlı bellek, ileti kuyrukları. VFS üstünde FAT16 (alt
dizinlerle) ve FAT32, disk önbelleği, AHCI/NVMe/IDE, USB. TCP/IP,
DHCP, DNS, soketler. ÜLGEN pencere dizgesi, ÜLGEN teması, Orbitron
yazı tipi. TÖRE yetki dizgesi, KUT yetenek bitleri. AC'97 ses.

Sınama takımı 80 başlıkta koşar; iki başarısız başlık ortamdan gelen
DNS kaynaklıdır (sanal ağ A kaydı döndürmüyor), çekirdek doğru
davranıp "A kaydı yok" diyor — sınama zayıflatılmadı.

## Ses

`src/dede.c` — Dede Korkut'un sesi. Tamsayı aritmetiğiyle çalışan
formant sentezleyici: Rosenberg gırtlak darbesi, kademeli ses yolu
(altı çınlayıcı), ünsüz boğumlanma yerleri, hece vurgusu, ağızdan
yayılım. Kayan nokta birimi kapalıyken koşar.

Ses kalitesi **gerileme sınamasındadır** (`ses_sinama/`): çekirdeğin
kendi dede.c'si ana bilgisayarda derlenir, üretilen dalga ölçülür ve
altı koşul doğrulanır — ünlülerin amaçlanan formantlarda olması,
ünsüzlerin birbirinden ayırt edilebilmesi, ses düzeylerinin doğal
aralıkta kalması, hece vurgusunun varlığı, duraklarda gerçek
sessizlik, çıkışta taşma olmaması.

Formant sentezi belirli bir insanın ses kimliğini taşıyamaz; ölçülebilir
niteliklerini eşler. Doğal ses isteyen kullanım için UMAY'ın Piper
arka ucuna bakın.

## UMAY

Üstünde koşan yapay us uygulaması ayrı depodadır:
https://github.com/hakanuste/UMAY

Bağlanma noktası UMAY'ın `port/` katmanıdır; UMAY'ın çekirdek kodunda
ÖTÜKEN'e özgü tek bir koşullu derleme yoktur.
