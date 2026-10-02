#!/bin/bash
# usage: run.sh <elf> <logname>   PCSX2 (Vulkan lavapipe) on $DISPLAY, window moved to 0,0 at 1280x720; EE output in $W/<logname>.log
. "$(dirname "$0")/env.sh"
pkill -x AppRun.wrapped 2>/dev/null; sleep 1
cd $W && XDG_CONFIG_HOME=$W/cfg nohup $PCSX2 -nogui -fastboot -elf "$1" > "$W/$2.log" 2>&1 &
for i in $(seq 1 40); do sleep 0.25; WIN=$(xdotool search --name "\\[" 2>/dev/null | head -1); [ -n "$WIN" ] && break; done
[ -n "$WIN" ] && xdotool windowmove $WIN 0 0 windowsize $WIN 1280 720 windowfocus $WIN
echo "window $WIN"
