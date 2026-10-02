#!/bin/bash
# Rebuilds usb.img (FAT32, 256 MB) from usb/. PCSX2 must not be running: it corrupts an image changed under it.
. "$(dirname "$0")/env.sh"; export MTOOLS_SKIP_CHECK=1
pkill -x AppRun.wrapped; sleep 1
rm -f $W/usb.img; dd if=/dev/zero of=$W/usb.img bs=1M count=0 seek=256 2>/dev/null
mkfs.vfat -F 32 -n ORBIT $W/usb.img >/dev/null && cd $W/usb && mcopy -s -i $W/usb.img ./* ::/
