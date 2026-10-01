#!/bin/bash
# Build the ORBIT fork of Neutrino v1.8.0 (phase 12: In Game Reset) into dist/neutrino/.
# usage (from Windows): wsl bash /mnt/c/Users/dgsc/Documents/ps2-launcher/tools/build_neutrino.sh
#
# Only the two pieces the IGR touches are rebuilt: the loader (neutrino.elf, new -igr / -igroff / -igrexit options)
# and modules/ee_core.elf (the IGR itself, third_party/neutrino-igr). The IOP modules stay the official v1.8.0
# release files already on the USB: they do not use the changed ee_core struct, and v1.8.0's cdvdfsv no longer
# builds with the current ps2sdk (irx check on _retonly), which is unrelated to this fork.
set -e
export PS2DEV=/usr/local/ps2dev PS2SDK=/usr/local/ps2dev/ps2sdk GSKIT=/usr/local/ps2dev/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$REPO/third_party/neutrino-igr"
WORK="${WORK:-$HOME/orbit-build}"
mkdir -p "$WORK"
cd "$WORK"
[ -d neutrino ] || git clone -q --branch v1.8.0 --depth 1 https://github.com/ps2max32/neutrino.git
cd neutrino
git checkout -q -- . && git clean -qfd ee/ee_core ee/loader common
cp "$FORK/igr.c" ee/ee_core/src/
cp "$FORK/igr.h" "$FORK/padpatterns.h" ee/ee_core/include/
python3 "$FORK/patch_neutrino.py"
make -C ee/ee_core clean all > "$WORK/ee_core.log" 2>&1 || { tail -20 "$WORK/ee_core.log"; exit 1; }
# the packed neutrino.elf needs ps2-packer's stubs, which this toolchain cannot find; the unpacked ELF runs the same
make -C ee/loader neutrino_unpacked.elf > "$WORK/loader.log" 2>&1 || { tail -20 "$WORK/loader.log"; exit 1; }
mkdir -p "$REPO/dist/neutrino/modules"
cp ee/loader/neutrino_unpacked.elf "$REPO/dist/neutrino/neutrino.elf"
cp ee/ee_core/ee_core.elf "$REPO/dist/neutrino/modules/ee_core.elf"
ls -la "$REPO/dist/neutrino" "$REPO/dist/neutrino/modules"
echo "copy dist/neutrino/neutrino.elf and dist/neutrino/modules/ee_core.elf over mass0:/neutrino/ (official v1.8.0)"
