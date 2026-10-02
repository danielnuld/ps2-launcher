# Shared settings for the PCSX2 test scripts. Override from the environment.
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=${PCSX2_WORK:-$T/work}          # usb/ (files copied to the virtual USB), usb.img, cfg/, logs, captures
PCSX2=${PCSX2:-$W/pcsx2/squashfs-root/AppRun} # PCSX2 2.4 AppImage, extracted (--appimage-extract)
export DISPLAY=${DISPLAY:-:99}
export PULSE_SERVER=${PULSE_SERVER:-unix:$(ls -d /tmp/pulse-*/native 2>/dev/null | head -1)}
mkdir -p $W
