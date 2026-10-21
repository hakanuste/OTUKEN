#!/usr/bin/env python3
"""C motorunun karşılaştıracağı referans değerleri üretir.
Model dosyasını GERİ AÇARAK okur: böylece iki taraf da aynı
nicelenmiş ağırlıkları görür, karşılaştırma gerçek olur."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from us1 import model_oku, ileri

if __name__ == "__main__":
    m = model_oku("/home/claude/us1/umay.us1")
    imler = [1, 5, 9, 2]
    y = ileri(m, imler)
    open("/tmp/referans_imler.txt", "w").write(" ".join(map(str, imler)))
    with open("/tmp/referans_mantik.txt", "w") as f:
        for t in range(len(imler)):
            f.write(" ".join(f"{v:.6f}" for v in y[t]) + "\n")
    print("referans:", y.shape, "genlik", round(float(abs(y).max()), 3))
