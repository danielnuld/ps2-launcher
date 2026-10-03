#!/bin/bash
# usage: key.sh <key> [hold_ms]   pad 1: arrows, k=X, l=O, i=triangle, j=square, q=L1, e=R1, BackSpace=SELECT, Return=START
. "$(dirname "$0")/env.sh"
WIN=$(xdotool search --name "\\[" | head -1)
xdotool windowfocus $WIN keydown $1; sleep 0.${2:-15}; xdotool keyup $1
