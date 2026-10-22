#!/usr/bin/env python3
"""KERVAN v2 paketi hazırlar ve imzalar (ECDSA P-256, SHA-256).

  python3 araclar/kervan_paketle.py paketle --ad ornek-belgeler --surum 1.0.0 \\
      --aciklama "..." --anahtar anahtarlar/kervan_uretim.pem --anahtar-no 0 \\
      --cikti ORNEK.KRV dosya1 dosya2 ...
  python3 araclar/kervan_paketle.py acik-anahtar-c URETIM.pem SINAMA.pem > src/kervan_anahtar.c

Biçim (küçük sonlu):
  başlık 160 B: "KERVAN2\\0", sürüm=2, girdi sayısı, ad[24], paket_sürüm[16],
                açıklama[96], veri_boyu, anahtar_no
  girdi 56 B:  ad[13] (8.3, NUL), tür (0 veri, 1 uygulama ELF), 2 B boşluk,
               ofset (paket başından), boy, sha256[32]
  veri
  imza 64 B:   ECDSA P-256 ham r||s, önceki bütün baytların SHA-256'sı üzerine
Anahtar 0 = üretim (özel anahtar depoda DEĞİL), 1 = sınama (yalnız TANI
yapısında geçerli)."""
import sys, struct, hashlib, os, re, argparse
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives import hashes, serialization

BASLIK = struct.Struct("<8sII24s16s96sII")
GIRDI = struct.Struct("<13sBHII32s")
assert BASLIK.size == 160 and GIRDI.size == 56

def ad83(yol):
    a = os.path.basename(yol).upper()
    if not re.fullmatch(r"[A-Z0-9_-]{1,8}(\.[A-Z0-9_-]{1,3})?", a):
        sys.exit(f"8.3 olmayan ad: {a}")
    return a

def paketle(ns):
    k = serialization.load_pem_private_key(open(ns.anahtar, "rb").read(), None)
    dosyalar = [(ad83(y), open(y, "rb").read()) for y in ns.dosyalar]
    n = len(dosyalar)
    veri_bas = BASLIK.size + n * GIRDI.size
    girdiler, veri, ofs = b"", b"", veri_bas
    for ad, icerik in dosyalar:
        tur = 1 if ad.endswith(".ELF") else 0
        girdiler += GIRDI.pack(ad.encode(), tur, 0, ofs, len(icerik), hashlib.sha256(icerik).digest())
        veri += icerik
        ofs += len(icerik)
    b = BASLIK.pack(b"KERVAN2\0", 2, n, ns.ad.encode(), ns.surum.encode(),
                    ns.aciklama.encode("utf-8")[:95], len(veri), ns.anahtar_no)
    govde = b + girdiler + veri
    der = k.sign(govde, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    open(ns.cikti, "wb").write(govde + r.to_bytes(32, "big") + s.to_bytes(32, "big"))
    print(f"{ns.cikti}: {n} dosya, {len(govde) + 64} bayt, anahtar {ns.anahtar_no}")

def acik_anahtar_c(yollar):
    print("/* OTOMATIK: araclar/kervan_paketle.py acik-anahtar-c — KERVAN imza acik anahtarlari")
    print(" * (ECDSA P-256, sikistirilmamis nokta). 0: uretim, 1: sinama (yalniz TANI). */")
    print("#include <stdint.h>")
    for i, y in enumerate(yollar):
        k = serialization.load_pem_private_key(open(y, "rb").read(), None)
        q = k.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        print(f"const uint8_t KERVAN_ANAHTAR_{i}[65] = {{")
        for j in range(0, 65, 13):
            print("    " + ", ".join("0x%02X" % x for x in q[j:j+13]) + ",")
        print("};")

a = argparse.ArgumentParser()
alt = a.add_subparsers(dest="komut", required=True)
p = alt.add_parser("paketle")
p.add_argument("--ad", required=True); p.add_argument("--surum", required=True)
p.add_argument("--aciklama", default=""); p.add_argument("--anahtar", required=True)
p.add_argument("--anahtar-no", type=int, default=0); p.add_argument("--cikti", required=True)
p.add_argument("dosyalar", nargs="+")
c = alt.add_parser("acik-anahtar-c"); c.add_argument("yollar", nargs=2)
ns = a.parse_args()
if ns.komut == "paketle": paketle(ns)
else: acik_anahtar_c(ns.yollar)
