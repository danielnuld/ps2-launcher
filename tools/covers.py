#!/usr/bin/env python3
"""Convert cover images (PNG/JPG) to .c16: 256x368 RGB555 in the GS CT16 layout. Spec: openspec cover-art.

usage: covers.py IN_DIR OUT_DIR [--no-dither]     |     covers.py --selftest
Resize: Lanczos in linear light (sRGB resizing darkens fine detail). Quantize: Floyd-Steinberg to 5 bits per
channel (default) or plain rounding. Levels are k*8, k = 0..31, the GS's 5 -> 8 bit expansion (estimate,
docs/sources.md).
"""
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 256, 368


def to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def resize(img):
    lin = to_linear(np.asarray(img.convert("RGB"), np.float32) / 255)
    ch = [np.asarray(Image.fromarray(lin[:, :, i], "F").resize((W, H), Image.LANCZOS)) for i in range(3)]
    return to_srgb(np.stack(ch, 2)) * 255  # float sRGB 0..255


def quantize(rgb, dither):  # rgb: HxWx3 float 0..255 -> HxWx3 int 0..31
    if not dither:
        return np.clip(np.rint(rgb / 8), 0, 31).astype(np.int32)
    a = rgb.astype(np.float32).copy()
    out = np.empty(a.shape, np.int32)
    h, w, _ = a.shape
    for y in range(h):
        row, nxt = a[y], a[y + 1] if y + 1 < h else None
        for x in range(w):
            k = np.clip(np.rint(row[x] / 8), 0, 31)
            out[y, x] = k
            e = row[x] - k * 8
            if x + 1 < w: row[x + 1] += e * 7 / 16
            if nxt is not None:
                if x > 0: nxt[x - 1] += e * 3 / 16
                nxt[x] += e * 5 / 16
                if x + 1 < w: nxt[x + 1] += e * 1 / 16
    return out


def c16_bytes(k):
    px = (k[:, :, 0] | k[:, :, 1] << 5 | k[:, :, 2] << 10 | 0x8000).astype("<u2")
    h, w, _ = k.shape
    return b"C16\0" + struct.pack("<II", w, h) + bytes(4) + px.tobytes()


def selftest():
    for dither in (True, False):
        flat = np.full((8, 8, 3), [10 * 8, 20 * 8, 31 * 8], np.float32)  # exactly representable
        k = quantize(flat, dither)
        assert (k == [10, 20, 31]).all(), dither
    b = c16_bytes(np.array([[[1, 2, 3]]], np.int32))
    assert b[:4] == b"C16\0" and struct.unpack("<II", b[4:12]) == (1, 1) and len(b) == 18
    assert struct.unpack("<H", b[16:])[0] == 1 | 2 << 5 | 3 << 10 | 0x8000
    assert resize(Image.new("RGB", (512, 736), (255, 255, 255))).min() > 254.5  # white stays white
    print("covers selftest ok")


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    args = [a for a in sys.argv[1:] if a != "--no-dither"]
    if len(args) != 2:
        sys.exit(__doc__)
    src, dst = Path(args[0]), Path(args[1])
    dst.mkdir(parents=True, exist_ok=True)
    for f in sorted(p for p in src.iterdir() if p.suffix.lower() in (".png", ".jpg", ".jpeg")):
        (dst / (f.stem + ".c16")).write_bytes(c16_bytes(quantize(resize(Image.open(f)), "--no-dither" not in sys.argv)))
        print(f.name)


if __name__ == "__main__":
    main()
