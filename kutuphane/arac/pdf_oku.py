#!/usr/bin/env python3
"""PDF -> sayfa sayfa metin (UMAY kütüphanesi için, ANA MAKİNEDE koşar).

  python3 pdf_oku.py <kitap.pdf> <cikti.jsonl> [--dpi 300] [--sayfa 1-35]

Her sayfa için:
  - metin katmanı yeterliyse (>= 40 sözcük) doğrudan pdftotext;
  - değilse sayfa görüntüye basılır ve metin tanıma (tesseract, eng) yapılır:
      * tablolar: ızgara çizgileri bulunur, HER HÜCRE ayrı okunur;
      * düz metin: ızgara silinmiş görüntüde okunur.
  - SAYILAR İKİ KEZ okunur (iki farklı eşikleme). İki okuma aynı değilse ya
    da tanıma güveni < 85 ise sayı 'şüpheli' işaretlenir. UMAY şüpheli
    sayıyı kanıt olarak VERMEZ; sayfa numarasını gösterir.
Çıktı: satır başına bir JSON: {sayfa, kaynak: metin|ocr, bloklar:[{tur, satirlar}],
        sayilar:{toplam, supheli}}
"""
import json, re, subprocess, sys, os, tempfile
import cv2
import numpy as np

SAYI = re.compile(r"\d+(?:[.,]\d+)?")


def tess(img, psm, ek=()):
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        cv2.imwrite(f.name, img)
        yol = f.name
    try:
        r = subprocess.run(["tesseract", yol, "-", "-l", "eng", "--psm", str(psm), *ek, "tsv"],
                           capture_output=True, text=True)
    finally:
        os.unlink(yol)
    sozcuk = []
    for s in r.stdout.splitlines()[1:]:
        p = s.split("\t")
        if len(p) == 12 and p[11].strip():
            sozcuk.append((p[11], float(p[10]), int(p[2]), int(p[4])))   # metin, güven, blok, satır
    return sozcuk


def satirla(sozcuk):
    satirlar, anahtar = [], None
    for t, g, b, s in sozcuk:
        if (b, s) != anahtar:
            satirlar.append([]); anahtar = (b, s)
        satirlar[-1].append((t, g))
    return satirlar


def izgara_bul(gri):
    bw = cv2.adaptiveThreshold(255 - gri, 255, cv2.ADAPTIVE_THRESH_MEAN_C, cv2.THRESH_BINARY, 15, -2)
    H, W = bw.shape
    yatay = cv2.morphologyEx(bw, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (W // 30, 1)))
    dikey = cv2.morphologyEx(bw, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (1, H // 60)))
    return cv2.dilate(yatay | dikey, np.ones((3, 3), np.uint8))


def hucreler(izgara):
    H, W = izgara.shape
    n, _, st, _ = cv2.connectedComponentsWithStats(255 - izgara, 4)
    h = [tuple(int(v) for v in st[i][:4]) for i in range(1, n)
         if 18 < st[i][2] < W * 0.95 and 22 < st[i][3] < H * 0.2]
    h.sort(key=lambda c: (c[1] // 15, c[0]))
    return h


def iki_okuma(parca):
    """Aynı hücreyi iki eşiklemeyle oku; (metin, güven, uyuşma)."""
    a = tess(parca, 7)
    _, ikili = cv2.threshold(parca, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    ikili = cv2.resize(ikili, None, fx=1.5, fy=1.5, interpolation=cv2.INTER_CUBIC)
    b = tess(ikili, 7)
    ta = " ".join(w[0] for w in a); tb = " ".join(w[0] for w in b)
    ga = min([w[1] for w in a] or [0])
    return ta, ga, SAYI.findall(ta) == SAYI.findall(tb), tb


def sayfa_ocr(gri):
    izg = izgara_bul(gri)
    temiz = gri.copy(); temiz[izg > 0] = 255
    bloklar, toplam, supheli = [], 0, 0
    cs = hucreler(izg)
    maske = np.zeros_like(gri)
    satir, sy, tablo = [], None, []
    for x, y, w, h in cs:
        maske[y:y + h, x:x + w] = 255
        parca = temiz[y + 2:y + h - 2, x + 2:x + w - 2]
        if parca.size == 0 or parca.min() > 200:
            hucre = ""                                # boş hücre
        else:
            parca = cv2.copyMakeBorder(parca, 16, 16, 16, 16, cv2.BORDER_CONSTANT, value=255)
            t, g, uy, t2 = iki_okuma(parca)
            n = len(SAYI.findall(t)); toplam += n
            if n and (not uy or g < 85):
                supheli += n
                t = "⟦" + t + "⟧"                    # şüpheli işaret
            hucre = t
        if sy is not None and abs(y - sy) > 15:
            tablo.append(satir); satir = []
        satir.append(hucre); sy = y
    if satir: tablo.append(satir)
    if tablo:
        bloklar.append({"tur": "tablo", "satirlar": [" | ".join(s) for s in tablo]})
    # düz metin: tablo hücreleri beyazlatılmış görüntü
    duz = temiz.copy(); duz[maske > 0] = 255
    ss = satirla(tess(duz, 4))
    metin = []
    for s in ss:
        cumle = " ".join(t for t, _ in s)
        for t, g in s:
            n = len(SAYI.findall(t)); toplam += n
            if n and g < 85:
                supheli += n; cumle = cumle.replace(t, "⟦" + t + "⟧", 1)
        if cumle.strip(): metin.append(cumle)
    if metin:
        bloklar.insert(0, {"tur": "metin", "satirlar": metin})
    return bloklar, toplam, supheli


def main():
    pdf, cikti = sys.argv[1], sys.argv[2]
    dpi = int(sys.argv[sys.argv.index("--dpi") + 1]) if "--dpi" in sys.argv else 300
    bilgi = subprocess.run(["pdfinfo", pdf], capture_output=True, text=True).stdout
    n = int(re.search(r"Pages:\s+(\d+)", bilgi).group(1))
    a, b = 1, n
    if "--sayfa" in sys.argv:
        a, b = map(int, sys.argv[sys.argv.index("--sayfa") + 1].split("-"))
    with open(cikti, "w", encoding="utf-8") as f, tempfile.TemporaryDirectory() as td:
        for p in range(a, b + 1):
            txt = subprocess.run(["pdftotext", "-layout", "-f", str(p), "-l", str(p), pdf, "-"],
                                 capture_output=True, text=True).stdout
            txt = "\n".join(s for s in txt.splitlines() if "Manualslib" not in s)
            if len(txt.split()) >= 40:
                kayit = {"sayfa": p, "kaynak": "metin",
                         "bloklar": [{"tur": "metin", "satirlar": [s for s in txt.splitlines() if s.strip()]}],
                         "sayilar": {"toplam": 0, "supheli": 0}}
            else:
                subprocess.run(["pdftoppm", "-r", str(dpi), "-gray", "-f", str(p), "-l", str(p),
                                pdf, f"{td}/s"], check=True)
                dosya = sorted(os.listdir(td))[0]
                gri = cv2.imread(f"{td}/{dosya}", cv2.IMREAD_GRAYSCALE)
                os.unlink(f"{td}/{dosya}")
                bl, t, s = sayfa_ocr(gri)
                kayit = {"sayfa": p, "kaynak": "ocr", "bloklar": bl, "sayilar": {"toplam": t, "supheli": s}}
            f.write(json.dumps(kayit, ensure_ascii=False) + "\n"); f.flush()
            print(f"sayfa {p}: {kayit['kaynak']} blok={len(kayit['bloklar'])} "
                  f"sayi={kayit['sayilar']['toplam']} supheli={kayit['sayilar']['supheli']}", flush=True)


if __name__ == "__main__":
    main()
