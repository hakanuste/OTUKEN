# ÖTÜKEN 1.5.1

1.5 üzerine: **USB 3 (xHCI) klavye sürücüsü** (`src/xhci.c`, `include/xhci.h`).

Neden: güncel bilgisayarların USB portları xHCI üzerindedir; eski çekirdekte yalnızca UHCI (USB 1.1)
klavyesi vardı, xHCI yalnızca algılanıyordu.

Yapılan:
- BIOS/UEFI'den sahiplik devri (USB Legacy Support), denetleyici sıfırlama, komut/olay/aktarım halkaları,
  scratchpad, 32 ve 64 baytlık bağlam desteği, port sıfırlama, cihaz adresleme, EP0 azami paket düzeltme,
  yapılandırma tanımlayıcısından BOOT klavyesi bulma, kesme uç noktası yoklama.
- HID tuş kodu → set-1 tarama kodu → PS/2 ile AYNI Türkçe Q tablosu (`keyboard_scancode_to_char`),
  CapsLock desteği. Tuşlar PS/2 kuyruğuna beslenir; kabuk/GUI değişmedi.
- `testler.sh` bölüm 15: xHCI klavye numaralandırma + 6 tuş doğrulaması.

Test (QEMU 128-256 MB): testler.sh 99 GEÇTİ / 2 KALDI. Kalan ikisi DNS testleri: korumalı ortamda
ağ erişimi olmadığı için (xHCI ile ilgisi yok). Gerçek donanımda DENENMEDİ.

Kapsam dışı (bilinen eksikler): USB hub arkasındaki cihazlar, USB fare (xHCI), tuş tekrarı, ok/F tuşları,
MSI/MSI-X (olay halkası yoklanır), Intel Panther Point çipsetlerinde EHCI→xHCI port yönlendirme,
Intel HDA ses, TCP yeniden gönderim, TLS, e1000 dışı ağ, MacBook'un dahili (SPI) klavyesi.
