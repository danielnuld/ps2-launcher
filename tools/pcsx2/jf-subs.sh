#!/bin/bash
# Example session: jfplay on the first movie, screenshots while its subtitles run, Square twice (off, back on).
. "$(dirname "$0")/env.sh"; K=$T/key.sh; shot(){ import -window root $W/s_$1.png; }
$T/mkimg.sh
$T/run.sh $W/usb/APPS/Jellyfin/jfplay.elf subs >/dev/null
$T/rec.sh subs 75 &
sleep 26; shot browse
$K k; sleep 3; shot p0
for t in 1 2 3 4 5 6; do sleep 1.5; shot p$t; done
$K j; sleep 1; shot off; sleep 3
$K j; sleep 1; shot on; sleep 4
$K l; sleep 6; shot end
wait; pkill -x AppRun.wrapped
