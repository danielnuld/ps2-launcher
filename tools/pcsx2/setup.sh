#!/bin/bash
# One-time: Xvfb 1280x720, a PulseAudio null sink "rec" to record from, PCSX2 config with the USB image and BIOS.
# usage: setup.sh <bios.bin>
. "$(dirname "$0")/env.sh"
pgrep -x Xvfb >/dev/null || (Xvfb $DISPLAY -screen 0 1280x720x24 >/dev/null 2>&1 &)
pgrep -x pulseaudio >/dev/null || pulseaudio --start --exit-idle-time=-1
pactl list short sinks | grep -qw rec || pactl load-module module-null-sink sink_name=rec >/dev/null
mkdir -p $W/cfg/PCSX2/inis $W/cfg/PCSX2/bios $W/usb
sed "s#@DIR@#$W#" $T/PCSX2.ini.in > $W/cfg/PCSX2/inis/PCSX2.ini
[ -n "$1" ] && cp "$1" $W/cfg/PCSX2/bios/
echo "ready: put files in $W/usb, then mkimg.sh and run.sh"
