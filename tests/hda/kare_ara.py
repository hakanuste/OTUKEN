#!/usr/bin/env python3
"""WAV kaydinda SES-TON-TEST'in kare dalgasini ORNEK ORNEK arar:
1000 Hz (48 kHz'de periyot 48: 24 ornek +8000, 24 ornek -8000), iki kanal ayni.
En uzun tam eslesen kosuyu (kare) yazar.   kare_ara.py <ses.wav>"""
import sys, wave, struct
w = wave.open(sys.argv[1]); n = w.getnframes(); ch = w.getnchannels(); hz = w.getframerate()
v = struct.unpack("<%dh" % (n * ch), w.readframes(n))
sol = v[0::ch]; sag = v[1::ch] if ch > 1 else sol
en = 0; i = 0
while i < n:
    if sol[i] == 8000 and (i == 0 or sol[i - 1] == -8000):
        j = i; ok = True
        while j < n and ok:
            beklenen = 8000 if ((j - i) % 48) < 24 else -8000
            ok = sol[j] == beklenen and sag[j] == beklenen
            if ok: j += 1
        en = max(en, j - i); i = j + 1
    else: i += 1
print(f"KARE-ARA: hiz={hz} kanal={ch} en_uzun_tam_eslesme={en} kare")
