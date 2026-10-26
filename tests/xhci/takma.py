#!/usr/bin/env python3
"""XHCI sicak takma: acilista USB aygit YOK (PS/2 kapali); acildiktan sonra
QMP device_add ile usb-kbd takilir, tus gonderilir, cikarilir, yeniden takilir.
  takma.py <iso> <disk> <dizin>"""
import subprocess, socket, time, os, sys, json
iso, disk, D = sys.argv[1:4]
log, qs = f"{D}/seri.log", f"{D}/qmp.sock"
for f in (log, qs):
    if os.path.exists(f): os.remove(f)
p = subprocess.Popen(["qemu-system-x86_64", "-machine", "pc,i8042=off", "-cpu", "max", "-cdrom", iso,
    "-drive", f"file={disk},format=raw,if=ide,index=0", "-device", "qemu-xhci,id=x",
    "-m", "1024", "-smp", "2", "-vga", "std", "-display", "none", "-serial", f"file:{log}",
    "-qmp", f"unix:{qs},server,nowait", "-no-reboot", "-boot", "d", "-nic", "none"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
g = lambda: open(log, 'rb').read().decode('latin-1') if os.path.exists(log) else ""
try:
    for _ in range(240):
        if "Ota\xc4\x9f kuruldu" in g(): break
        time.sleep(1)
    time.sleep(4)
    q = socket.socket(socket.AF_UNIX); q.connect(qs); q.settimeout(5); q.recv(65536)
    def cmd(o, w=0.3):
        q.sendall((json.dumps(o) + "\n").encode()); time.sleep(w)
        try: return q.recv(65536)
        except Exception: return b""
    cmd({"execute": "qmp_capabilities"})
    def tus(k): cmd({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": k}]}}, 0.15)
    print(cmd({"execute": "device_add", "arguments": {"driver": "usb-kbd", "bus": "x.0", "id": "k1"}}, 3))
    for k in ("t", "a", "k", "ret"): tus(k)
    time.sleep(1.5)
    print(cmd({"execute": "device_del", "arguments": {"id": "k1"}}, 3))
    print(cmd({"execute": "device_add", "arguments": {"driver": "usb-kbd", "bus": "x.0", "id": "k2"}}, 3))
    for k in ("i", "k", "i", "ret"): tus(k)
    time.sleep(2)
finally:
    p.kill(); p.wait()
