#!/bin/bash
# 4 TB GPT sinama diski uretir (seyrek - gercek alan kaplamaz)
IMG="${1:-/tmp/gpt.img}"
[ -f "$IMG" ] && exit 0
truncate -s 4T "$IMG"
sgdisk -n 1:2048:+512M -t 1:EF00 -c 1:"EFI" \
       -n 2:0:+2T      -t 2:0700 -c 2:"VERI" \
       -n 3:0:0        -t 3:0700 -c 3:"ARSIV" "$IMG" >/dev/null 2>&1
