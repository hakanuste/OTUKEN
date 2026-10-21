#!/usr/bin/env python3
"""ELF dosyasini cekirdege gomulecek C dizisine cevirir."""
import sys
yol, ad = sys.argv[1], sys.argv[2]
d = open(yol, 'rb').read()
print("/* %s dosyasindan uretildi - ELLE DUZENLEMEYIN */" % yol)
print("#include <stdint.h>\n")
print("const uint8_t %s[] = {" % ad)
for i in range(0, len(d), 12):
    print("    " + ", ".join("0x%02X" % b for b in d[i:i+12]) + ",")
print("};")
print("const uint64_t %s_size = sizeof(%s);" % (ad, ad))
