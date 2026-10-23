#!/usr/bin/env python3
"""gen_yazi16.py — ÖTÜKEN ekran yazı tipini üretir: include/yazi16.h

Kaynak: DejaVu Sans Mono (Bitstream Vera türevi; lisansı gömmeye ve
dönüştürmeye izin verir — bkz. https://dejavu-fonts.github.io/License.html).
Her glif 8x16 hücreye, KENARI YUMUŞATILMIŞ (8 bit örtüklük, 0..255)
olarak basılır. Çekirdek yazıyı ön renkle arka rengi bu örtüklükle
karıştırarak çizer; kayan nokta gerekmez (tamsayı karışım).

  python3 gen_yazi16.py [ttf] [boy]   (varsayılan: DejaVuSansMono.ttf, 13)

Üretilen başlık depoya işlenir; derleme PIL gerektirmez.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
BOY = int(sys.argv[2]) if len(sys.argv) > 2 else 13
EN, YUK, TABAN = 8, 16, 12          # hücre eni, boyu, taban çizgisi (üstten)

KODLAR = list(range(32, 127)) + [ord(c) for c in
    "ÇçĞğİıÖöŞşÜüÂâÎîÛûÉéÈèÊêÀàÔôÁáÍíÓóÚúÑñÄäßØø"
    "°·«»—–…‘’“”•×÷±€₺²³½¼¾§¶©®™←→↑↓✓■□▶"]
# 1.4.9 (TULPAR): Orta Asya Türk dilleri ve Avrupa için harf ekleri.
# Latin Genişletilmiş-A (Türkmen ň ž ý, Azerî, Avrupa dilleri), Azerî ə/Ə,
# Özbek ʻ, Kiril (Rusça, Kazakça, Kırgızca, Özbek Kirili ekleri), Yunan,
# ve web sayfalarında sık görülen imler. Eski 173 glif SIRASI VE ŞEKLİ
# DEĞİŞMEDİ (ekran sınamasının özetleri aynı kalır).
_EK = (list(range(0x00A1, 0x0100)) + list(range(0x0100, 0x0180)) + [0x018F, 0x0259, 0x02BB, 0x02BC] +
       list(range(0x0391, 0x03AA)) + list(range(0x03B1, 0x03CA)) +
       list(range(0x0400, 0x0460)) +
       [0x0492, 0x0493, 0x049A, 0x049B, 0x04A2, 0x04A3, 0x04AE, 0x04AF, 0x04B0, 0x04B1,
        0x04BA, 0x04BB, 0x04D8, 0x04D9, 0x04E8, 0x04E9, 0x0496, 0x0497, 0x04B2, 0x04B3] +
       [0x2020, 0x2021, 0x2030, 0x2039, 0x203A, 0x201A, 0x201E, 0x2032, 0x2033, 0x2500, 0x2502,
        0x25BE, 0x25C0, 0x2122])
KODLAR += [k for k in _EK if k not in KODLAR and k not in (0x03A2, 0x00AD)]

font = ImageFont.truetype(TTF, BOY)
asc, desc = font.getmetrics()
glifler = []
for k in KODLAR:
    im = Image.new("L", (EN, YUK), 0)
    d = ImageDraw.Draw(im)
    ch = chr(k)
    # yatay: ilerleme ~7.8 px; hücreye ortala
    ilerle = font.getlength(ch)
    x = int(round((EN - ilerle) / 2))
    d.text((x, TABAN - asc), ch, font=font, fill=255)
    glifler.append(list(im.getdata()))

with open("include/yazi16.h", "w", encoding="utf-8") as f:
    f.write("/* OTOMATIK URETILDI: gen_yazi16.py — ELLE DUZENLEME.\n")
    f.write(f" * Kaynak: DejaVu Sans Mono, {BOY} px; hucre {EN}x{YUK}, taban {TABAN}.\n")
    f.write(" * DejaVu yazi tipi lisansi (Bitstream Vera turevi) gommeye izin verir.\n")
    f.write(" * Her deger: pikselin ortuklugu 0..255 (kenar yumusatma). */\n")
    f.write("#ifndef OTUKENOS_YAZI16_H\n#define OTUKENOS_YAZI16_H\n#include <stdint.h>\n\n")
    f.write(f"#define YAZI_EN {EN}\n#define YAZI_YUK {YUK}\n#define YAZI_GLIF_SAYISI {len(KODLAR)}\n\n")
    f.write("static const uint32_t yazi16_kod[YAZI_GLIF_SAYISI] = {\n")
    for i in range(0, len(KODLAR), 12):
        f.write("    " + ", ".join(f"0x{k:04X}" for k in KODLAR[i:i+12]) + ",\n")
    f.write("};\n\n")
    f.write("static const uint8_t yazi16[YAZI_GLIF_SAYISI][YAZI_YUK * YAZI_EN] = {\n")
    for k, g in zip(KODLAR, glifler):
        ad = chr(k) if k not in (0x5C, 0x2A, 0x2F) else f"U+{k:04X}"
        f.write(f"    /* {ad} */ {{" + ",".join(str(v) for v in g) + "},\n")
    f.write("};\n\n#endif\n")

# önizleme (belge ve denetim için)
if "--onizle" in sys.argv:
    sutun = 32
    satir = (len(KODLAR) + sutun - 1) // sutun
    im = Image.new("L", (sutun * EN, satir * YUK), 0)
    for i, g in enumerate(glifler):
        gi = Image.new("L", (EN, YUK)); gi.putdata(g)
        im.paste(gi, ((i % sutun) * EN, (i // sutun) * YUK))
    im.resize((im.width * 4, im.height * 4), Image.NEAREST).save("yazi16_onizleme.png")
print(f"include/yazi16.h: {len(KODLAR)} glif, {EN}x{YUK}, DejaVu Sans Mono {BOY}px (ascent {asc}, descent {desc})")
