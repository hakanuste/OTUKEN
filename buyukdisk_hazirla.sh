#!/bin/bash
# 8 TB seyrek sinama diski uretir (gercek disk alani kaplamaz)
IMG="${1:-/tmp/buyuk.img}"
[ -f "$IMG" ] && exit 0
truncate -s 8T "$IMG"
python3 - "$IMG" <<'PY'
import sys
f = open(sys.argv[1], 'r+b')
f.seek((6*1024**4)//512*512)          # 6 TB konumu
f.write(b'OTUKEN-6TB-IMZA-' + bytes(496))
f.close()
PY
