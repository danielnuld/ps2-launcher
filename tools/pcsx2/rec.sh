#!/bin/bash
# usage: rec.sh <name> <seconds>   records the screen + the "rec" sink to $W/<name>.mp4
. "$(dirname "$0")/env.sh"
ffmpeg -loglevel error -y -f x11grab -framerate 30 -video_size 1280x720 -i $DISPLAY -f pulse -i rec.monitor -t $2 \
	-c:v libx264 -preset ultrafast -crf 18 -c:a aac -b:a 160k $W/$1.mp4
