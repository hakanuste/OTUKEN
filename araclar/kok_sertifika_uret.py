#!/usr/bin/env python3
"""TULPAR güven kökleri: PEM demetinden BearSSL br_x509_trust_anchor C tablosu.

  python3 araclar/kok_sertifika_uret.py /usr/share/ca-certificates/mozilla > userprog/tls_kokler.c

DİKKAT: /etc/ssl/certs/ca-certificates.crt KULLANMA. O demet, makineye
sonradan eklenen yerel kökleri de (/usr/local/share/ca-certificates: kurum
vekil sunucularının araya giren sertifika kökleri vb.) içerir; bunlar
dağıtılan işletim sistemine GİRMEMELİ. Yalnız Mozilla dizini okunur.

BearSSL'in 'brssl ta' aracıyla aynı içeriği üretir (DN'nin DER'i, RSA n/e ya
da EC eğri+nokta). Kökler Mozilla listesidir (Ubuntu ca-certificates)."""
import sys, re
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import rsa, ec
from cryptography.hazmat.primitives import serialization

EGRI = {"secp256r1": 23, "secp384r1": 24, "secp521r1": 25}

def dizi(ad, b):
    s = "static const unsigned char %s[] = {\n" % ad
    for i in range(0, len(b), 12):
        s += "\t" + ", ".join("0x%02X" % x for x in b[i:i+12]) + ",\n"
    return s + "};\n\n"

import os
kaynak = sys.argv[1]
if os.path.isdir(kaynak):
    dosyalar = sorted(os.path.join(kaynak, f) for f in os.listdir(kaynak) if f.endswith(".crt"))
    metin = "".join(open(f, encoding="ascii", errors="replace").read() for f in dosyalar)
else:
    metin = open(kaynak, encoding="ascii", errors="replace").read()
pemler = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", metin, re.S)
cikti, girdiler = [], []
atlanan = 0
for i, p in enumerate(pemler):
    c = x509.load_pem_x509_certificate(p.encode())
    pk = c.public_key()
    dn = c.subject.public_bytes()
    n = len(girdiler)
    if isinstance(pk, rsa.RSAPublicKey):
        nn = pk.public_numbers()
        nb = nn.n.to_bytes((nn.n.bit_length() + 7) // 8, "big")
        eb = nn.e.to_bytes((nn.e.bit_length() + 7) // 8, "big")
        cikti += [dizi(f"TA{n}_DN", dn), dizi(f"TA{n}_RSA_N", nb), dizi(f"TA{n}_RSA_E", eb)]
        girdiler.append(f"\t{{\n\t\t{{ (unsigned char *)TA{n}_DN, sizeof TA{n}_DN }},\n\t\tBR_X509_TA_CA,\n"
                        f"\t\t{{\n\t\t\tBR_KEYTYPE_RSA,\n\t\t\t{{ .rsa = {{\n"
                        f"\t\t\t\t(unsigned char *)TA{n}_RSA_N, sizeof TA{n}_RSA_N,\n"
                        f"\t\t\t\t(unsigned char *)TA{n}_RSA_E, sizeof TA{n}_RSA_E,\n\t\t\t}} }}\n\t\t}}\n\t}}")
    elif isinstance(pk, ec.EllipticCurvePublicKey) and pk.curve.name in EGRI:
        q = pk.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        cikti += [dizi(f"TA{n}_DN", dn), dizi(f"TA{n}_EC_Q", q)]
        girdiler.append(f"\t{{\n\t\t{{ (unsigned char *)TA{n}_DN, sizeof TA{n}_DN }},\n\t\tBR_X509_TA_CA,\n"
                        f"\t\t{{\n\t\t\tBR_KEYTYPE_EC,\n\t\t\t{{ .ec = {{\n\t\t\t\t{EGRI[pk.curve.name]},\n"
                        f"\t\t\t\t(unsigned char *)TA{n}_EC_Q, sizeof TA{n}_EC_Q,\n\t\t\t}} }}\n\t\t}}\n\t}}")
    else:
        atlanan += 1
print("/* OTOMATIK URETILDI: araclar/kok_sertifika_uret.py — ELLE DUZENLEME.")
print(" * Kaynak: %s (%d kok, %d desteklenmeyen anahtar atlandi)." % (sys.argv[1], len(girdiler), atlanan))
print(" * Mozilla guven kokleri (Ubuntu ca-certificates paketi). */")
print('#include "bearssl.h"\n')
print("".join(cikti), end="")
print("const br_x509_trust_anchor TULPAR_KOKLER[%d] = {" % len(girdiler))
print(",\n".join(girdiler))
print("};\n\nconst unsigned TULPAR_KOK_SAYISI = %d;" % len(girdiler))
