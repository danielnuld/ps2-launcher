#!/bin/bash
# Build the ORBIT fork of Neutrino (base: rickgaiser/neutrino dev 7be8de2) into dist/neutrino/.
# usage (from Windows): wsl bash /mnt/c/Users/dgsc/Documents/ps2-launcher/tools/build_neutrino.sh
#
# Rebuilt: the loader (neutrino.elf: -igr / -igroff / -igrexit / -igrmenu / -ra options), modules/ee_core.elf (In
# Game Reset, in-game menu, achievements telemetry: third_party/neutrino-igr) and modules/raagent.irx (phase 16b).
# Every other module stays the official dev release's, already on the USB.
set -e
export PS2DEV=/usr/local/ps2dev PS2SDK=/usr/local/ps2dev/ps2sdk GSKIT=/usr/local/ps2dev/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$REPO/third_party/neutrino-igr"
WORK="${WORK:-$HOME/orbit-build}"
BASE=7be8de2
mkdir -p "$WORK"
cd "$WORK"
[ -d neutrino-dev ] || git clone -q https://github.com/rickgaiser/neutrino.git neutrino-dev
cd neutrino-dev
git checkout -q -- . && git checkout -q "$BASE" && git clean -qfd ee/ee_core ee/loader common iop/raagent
rm -rf "$WORK/resetspu" && cp -r "$FORK/resetspu" "$WORK/resetspu" # the IGR's SPU2 reset, embedded in ee_core
make -C "$WORK/resetspu" > "$WORK/resetspu.log" 2>&1 || { tail -20 "$WORK/resetspu.log"; exit 1; }
bin2c "$WORK/resetspu/resetspu.irx" ee/ee_core/src/resetspu_irx.c resetspu_irx
cp "$FORK/igr.c" "$FORK/menu.c" "$FORK/ra.c" ee/ee_core/src/
if [ "${MENU_TEST:-0}" = 1 ]; then # PCSX2 only: the menu guesses Black's frame (no data breakpoints there)
    sed -i 's/^#define MENU_TEST 0$/#define MENU_TEST 1/' ee/ee_core/src/menu.c
fi
cp "$FORK/igr.h" "$FORK/padpatterns.h" ee/ee_core/include/
cp "$FORK/raagent/ra_snap.h" "$FORK/raagent/ra_watch.h" common/include/ # xeRAbora's protocol: loader, ee_core, agent
rm -rf iop/raagent && cp -r "$FORK/raagent" iop/raagent
python3 "$FORK/patch_neutrino.py"
make -C ee/ee_core clean all > "$WORK/ee_core.log" 2>&1 || { tail -20 "$WORK/ee_core.log"; exit 1; }
grep -E ' _end$|_end = ' ee/ee_core/ee_core.map | head -2 # the 64 KB region ends at 0x94000
# the packed neutrino.elf needs ps2-packer's stubs, which this toolchain cannot find; the unpacked ELF runs the same
make -C ee/loader neutrino_unpacked.elf > "$WORK/loader.log" 2>&1 || { tail -20 "$WORK/loader.log"; exit 1; }
make -C iop/raagent > "$WORK/raagent.log" 2>&1 || { tail -20 "$WORK/raagent.log"; exit 1; }
mkdir -p "$REPO/dist/neutrino/modules"
cp ee/loader/neutrino_unpacked.elf "$REPO/dist/neutrino/neutrino.elf"
cp ee/ee_core/ee_core.elf iop/raagent/irx/raagent.irx "$REPO/dist/neutrino/modules/"
ls -la "$REPO/dist/neutrino" "$REPO/dist/neutrino/modules"
echo "copy dist/neutrino/ over mass0:/neutrino/ (official dev $BASE release)"
