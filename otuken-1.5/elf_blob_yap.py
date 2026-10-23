#!/usr/bin/env python3
"""ELF ikilisini cekirdege gomulecek C dizisine cevirir.
Kullanim: elf_blob_yap.py <girdi.elf> <cikti.c> <dizi_adi>"""
import sys
g, c, ad = sys.argv[1], sys.argv[2], sys.argv[3]
b = open(g, 'rb').read()
satirlar = []
for i in range(0, len(b), 16):
    satirlar.append("    " + " ".join("0x%02X," % x for x in b[i:i+16]))
with open(c, 'w') as f:
    f.write("/* OTOMATIK URETILDI */\n#include <stdint.h>\n\n")
    f.write("const unsigned char %s[] = {\n" % ad)
    f.write("\n".join(satirlar) + "\n};\n\n")
    f.write("const unsigned long %s_size = %d;\n" % (ad, len(b)))
