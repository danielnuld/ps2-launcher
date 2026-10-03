# PCSX2 test harness

Runs the launcher in PCSX2 2.4 on a headless Linux box (Xvfb + Vulkan lavapipe), drives the pad with
xdotool and records video + audio. Used for the phase 14 / 15 demo videos; timing is not cycle-accurate, the
console still decides.

Needs: Xvfb, xdotool, imagemagick (`import`), ffmpeg, pulseaudio, mtools + dosfstools, the PCSX2 AppImage extracted
to `work/pcsx2/squashfs-root` (or `PCSX2=/path/AppRun`), and a BIOS dump of your own console.

```
tools/pcsx2/setup.sh ~/scph70012.bin     # Xvfb :99, "rec" sink, work/cfg/PCSX2/inis/PCSX2.ini
cp launcher.elf tools/pcsx2/work/usb/    # + orbit/, DVD/, APPS/ ...
tools/pcsx2/mkimg.sh && tools/pcsx2/run.sh tools/pcsx2/work/usb/launcher.elf run1
tools/pcsx2/key.sh Right; tools/pcsx2/key.sh k   # pad keys: see key.sh
```

`PCSX2.ini.in` settings that matter: `Renderer = 14` (Vulkan; OpenGL crashed under Xvfb), `[USB1] Type = Msd`
with the image path, `[DEV9/Eth] EthApi = Sockets` (the PS2 reaches the host's network, e.g. Jellyfin in Docker).
`throttle.py` relays :8097 -> :8096 at a fixed rate, for when PCSX2's sockets-mode TCP stalls on bursts.
Jellyfin (jfplay) and its example session moved to danielnuld/orbit-jellyfin.
