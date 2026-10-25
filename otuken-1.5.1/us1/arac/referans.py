#!/usr/bin/env python3
"""C motorunun karşılaştıracağı referans değerleri üretir.
Model dosyasını GERİ AÇARAK okur: böylece iki taraf da aynı
nicelenmiş ağırlıkları görür, karşılaştırma gerçek olur."""
import os
KOK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")  # us1/
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from us1 import model_oku, ileri

if __name__ == "__main__":
    m = model_oku(os.path.join(KOK, "umay.us1"))
    # Gercek bir istem: bayt sozlugunden "S: kiler kac derece\nU: " (32 ime kadar)
    sozluk = open(os.path.join(KOK, "sozluk.txt"), "rb").read()
    im = {b: i for i, b in enumerate(sozluk)}
    istem = "S: kiler kaç derece\nU: Yer altı kileri yıl boyu".encode("utf-8")
    imler = [im[b] for b in istem if b in im][: m.baglam]
    if len(imler) < 4:
        imler = [1, 5, 9, 2]
    y = ileri(m, imler)
    open(os.path.join(KOK, "sinama", "referans_imler.txt"), "w").write(" ".join(map(str, imler)))
    with open(os.path.join(KOK, "sinama", "referans_mantik.txt"), "w") as f:
        for t in range(len(imler)):
            f.write(" ".join(f"{v:.6f}" for v in y[t]) + "\n")
    print("referans:", y.shape, "genlik", round(float(abs(y).max()), 3))
