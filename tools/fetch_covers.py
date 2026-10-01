#!/usr/bin/env python3
"""Make covers for the games on a USB drive: read each ISO's serial (SYSTEM.CNF, else the OPL file-name prefix),
download its cover from xlenore/ps2-covers and convert it with covers.py into <usb>/covers/<SERIAL>.c16 + _s.c16.

usage: fetch_covers.py USB_ROOT [--force]   (e.g. E:/)   |   fetch_covers.py --selftest
Existing covers are kept unless --force. Same ISO9660 logic as iso.c.
"""
import io
import re
import struct
import sys
import urllib.request
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from covers import SIZES, c16_bytes, quantize, resize  # noqa: E402

URL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/{}.jpg"
SECTOR = 2048


def iso_serial(f):  # "SLUS_209.46" or None
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if len(pvd) < SECTOR or pvd[0] != 1 or pvd[1:6] != b"CD001":
        return None
    lba, size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
    f.seek(lba * SECTOR)
    root = f.read(min(size, 64 * SECTOR))
    p = 0
    while p < len(root):
        n = root[p]
        if n == 0:  # records do not cross sectors: skip to the next one
            p = (p // SECTOR + 1) * SECTOR
            continue
        name = root[p + 33:p + 33 + root[p + 32]]
        if name.startswith(b"SYSTEM.CNF"):
            clba, csize = struct.unpack_from("<I", root, p + 2)[0], struct.unpack_from("<I", root, p + 10)[0]
            f.seek(clba * SECTOR)
            m = re.search(rb"BOOT2\s*=\s*cdrom0:\\?([A-Z]{4}_\d{3}\.\d{2})", f.read(min(csize, 4096)))
            return m.group(1).decode() if m else None
        p += n
    return None


def name_serial(name):
    m = re.match(r"([A-Z]{4}_\d{3}\.\d{2})\.", name)
    return m.group(1) if m else None


def dash(serial):
    return serial.replace("_", "-").replace(".", "")


def main(root, force):
    out = root / "covers"
    out.mkdir(exist_ok=True)
    for iso in sorted(p for d in ("DVD", "CD") if (root / d).is_dir() for p in (root / d).iterdir()
                      if p.suffix.lower() == ".iso"):
        with open(iso, "rb") as f:
            serial = iso_serial(f) or name_serial(iso.name)
        if not serial:
            print(f"{iso.name}: no serial, skipped")
            continue
        s = dash(serial)
        if (out / f"{s}.c16").exists() and not force:
            print(f"{iso.name}: {s} already has a cover")
            continue
        try:
            img = Image.open(io.BytesIO(urllib.request.urlopen(URL.format(s), timeout=20).read()))
        except Exception as e:  # noqa: BLE001 - report and go on with the next game
            print(f"{iso.name}: {s} no cover online ({e})")
            continue
        for size, suffix in SIZES:
            (out / f"{s}{suffix}.c16").write_bytes(c16_bytes(quantize(resize(img, size), True)))
        print(f"{iso.name}: {s} cover written")


def selftest():
    iso = bytearray(SECTOR * 21)
    iso[16 * SECTOR:16 * SECTOR + 6] = b"\x01CD001"
    struct.pack_into("<I", iso, 16 * SECTOR + 158, 18)
    struct.pack_into("<I", iso, 16 * SECTOR + 166, SECTOR)
    r = 18 * SECTOR
    iso[r] = 34                                       # "." record
    iso[r + 34] = 46
    struct.pack_into("<I", iso, r + 34 + 2, 20)
    struct.pack_into("<I", iso, r + 34 + 10, 40)
    iso[r + 34 + 32] = 12
    iso[r + 34 + 33:r + 34 + 45] = b"SYSTEM.CNF;1"
    iso[20 * SECTOR:20 * SECTOR + 32] = b"BOOT2 = cdrom0:\\SLUS_209.46;1\r\n"
    assert iso_serial(io.BytesIO(bytes(iso))) == "SLUS_209.46"
    assert name_serial("SCUS_973.99.God of War.iso") == "SCUS_973.99" and name_serial("Okami.iso") is None
    assert dash("SLUS_209.46") == "SLUS-20946"
    print("fetch_covers selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif len(sys.argv) >= 2:
        main(Path(sys.argv[1]), "--force" in sys.argv)
    else:
        sys.exit(__doc__)
