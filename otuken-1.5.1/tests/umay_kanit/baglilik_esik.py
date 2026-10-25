#!/usr/bin/env python3
"""KANIT_BAGLILIK_ESIK seçimi: elle etiketli US1 sözleri (baglilik_etiket.tsv).
Eşik GELİŞTİRME satırlarında F1'e göre seçilir (eşitlikte yüksek eşik);
SINAMA satırları ayrı raporlanır. Bant dışı (bağlılık < 20 ya da >= 90)
sözler etiketlenmedi; oranlar YALNIZ bu bant içindir."""
import os, re
Y = os.path.join(os.path.dirname(os.path.abspath(__file__)), "baglilik_etiket.tsv")
r = [l.rstrip("\n").split("\t") for l in open(Y, encoding="utf-8") if not l.startswith("#")]
k = {"gelistirme": [], "sinama": []}
for s in r: k[s[0]].append((int(s[2]), int(s[3])))

def olc(v, e):
    tp = sum(1 for b, y in v if b >= e and y); fp = sum(1 for b, y in v if b >= e and not y)
    fn = sum(1 for b, y in v if b < e and y)
    p = tp / (tp + fp) if tp + fp else 1.0; rc = tp / (tp + fn) if tp + fn else 1.0
    return p, rc, (2 * p * rc / (p + rc) if p + rc else 0.0), tp, fp, fn

en = None
print("eşik  geliştirme(kesinlik duyarlılık F1)   sınama(kesinlik duyarlılık F1)")
for e in range(30, 91, 5):
    a, b = olc(k["gelistirme"], e), olc(k["sinama"], e)
    print(f"  {e:2d}   {a[0]:.2f} {a[1]:.2f} {a[2]:.2f}                     {b[0]:.2f} {b[1]:.2f} {b[2]:.2f}")
    if en is None or a[2] > en[1] + 1e-12 or abs(a[2] - en[1]) <= 1e-12: en = (e, a[2])
c = None
for l in open(os.path.join(os.path.dirname(Y), "..", "..", "umay", "include", "umay_kanit.h"), encoding="utf-8"):
    m = re.match(r"#define KANIT_BAGLILIK_ESIK\s+(\d+)", l)
    if m: c = int(m.group(1))
b = olc(k["sinama"], en[0])
print(f"Seçilen (geliştirme): {en[0]}   [C'deki: {c}]")
print(f"SINAMA @ {en[0]}: kesinlik {b[0]:.2f}, duyarlılık {b[1]:.2f}, F1 {b[2]:.2f} (tp {b[3]}, fp {b[4]}, fn {b[5]})")
print(f"etiketli: geliştirme {len(k['gelistirme'])}, sınama {len(k['sinama'])}")
