#!/usr/bin/env python3
"""Bake the ORBIT UI art into ui_data.c / ui_data.h: a PSMT4 alpha atlas (icons, disc, sparkle), a 256-level glow
(PSMT8 alpha) and the chrome orb at its two drawn sizes, Floyd-Steinberg dithered to the CT16 framebuffer's 5-bit
levels and stored as PSMT8 + RGBA CLUT (16 alpha levels and 16-bit truncation showed as bands on the console). Icons transcribe the design canvas SVGs (24-unit grid, round caps); drawn 4x with
Pillow, then downsampled for anti-aliasing. Spec: openspec change phase-4-orbit-style.

usage: ui_art.py  |  ui_art.py --selftest
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from covers import quantize  # noqa: E402  Floyd-Steinberg to RGB555 levels k*8

ROOT = Path(__file__).resolve().parent.parent
SS = 4                 # supersampling
AW, PAD = 256, 2       # atlas width (PSMT4 page 128x128: 2 pages per 128 rows), blank texels between entries


def bez(p0, p1, p2, p3, n=12):
    return [tuple((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t * t * c + t ** 3 * d
                  for a, b, c, d in zip(p0, p1, p2, p3)) for t in (i / n for i in range(1, n + 1))]


SPARK = [(12, 0)] + bez((12, 0), (13, 8), (16, 11), (24, 12)) + bez((24, 12), (16, 13), (13, 16), (12, 24)) + \
        bez((12, 24), (11, 16), (8, 13), (0, 12)) + bez((0, 12), (8, 11), (11, 8), (12, 0))

# icon: list of ops on the 24-unit grid; ("l", pts) polyline, ("p", pts) closed outline, ("f", pts) filled polygon,
# ("c", cx, cy, r) circle outline, ("d", cx, cy, r) filled circle, ("r", x0, y0, x1, y1, rad) rounded-rect outline,
# ("a", cx, cy, r, start, end) arc in degrees (0 = east, clockwise)
ICONS = {
    "play": [("f", [(7, 4.5), (7, 19.5), (19.5, 12)])],
    "chip": [("r", 5, 5, 19, 19, 2), ("r", 9.5, 9.5, 14.5, 14.5, 0.5),
             *[("l", s) for s in ([(9, 1.5), (9, 5)], [(15, 1.5), (15, 5)], [(9, 19), (9, 22.5)], [(15, 19), (15, 22.5)],
                                  [(1.5, 9), (5, 9)], [(1.5, 15), (5, 15)], [(19, 9), (22.5, 9)], [(19, 15), (22.5, 15)])]],
    "memcard": [("p", [(6, 2.5), (15, 2.5), (18.5, 6), (18.5, 21.5), (6, 21.5)]), ("l", [(9, 2.5), (9, 6.5), (15, 6.5), (15, 2.5)]),
                ("l", [(8.5, 15.5), (15.5, 15.5)]), ("l", [(8.5, 18.5), (15.5, 18.5)])],
    "dvd": [("c", 12, 12, 9.5), ("c", 12, 12, 2.5), ("a", 12, 12, 6.5, 270, 360)],
    "cd": [("c", 12, 12, 9.5), ("c", 12, 12, 4), ("d", 12, 12, 1)],
    "hdd": [("r", 2.5, 6, 21.5, 18, 2.5), ("l", [(6, 14.5), (13, 14.5)]), ("d", 17.5, 14.5, 1), ("l", [(6, 9.5), (18, 9.5)])],
    "usb": [("l", [(12, 21.5), (12, 5)]), ("f", [(9, 7.5), (12, 3), (15, 7.5)]), ("l", [(12, 15), (7, 12), (7, 9.5)]),
            ("c", 7, 8.5, 1.3), ("l", [(12, 12.5), (17, 10), (17, 7.5)]), ("r", 15.8, 5.5, 18.2, 7.9, 0)],
    "net": [("r", 4, 5, 20, 17, 1.5), ("l", [(8, 17), (8, 19.5), (16, 19.5), (16, 17)]), ("l", [(8, 9), (9.5, 9)]),
            ("l", [(11.25, 9), (12.75, 9)]), ("l", [(14.5, 9), (16, 9)]), ("l", [(8, 12.5), (16, 12.5)])],
    "mx4sio": [("p", [(7, 2.5), (14.5, 2.5), (18, 6), (18, 21.5), (6, 21.5), (6, 3.5)]), ("l", [(9, 2.5), (9, 5.5)]),
               ("l", [(11.5, 2.5), (11.5, 5.5)]), ("l", [(14, 2.5), (14, 5.5)]), ("r", 9, 14, 15, 18, 0)],
    "mmce": [("p", [(5, 2.5), (15, 2.5), (19, 6.5), (19, 21.5), (5, 21.5)]), ("l", [(8, 2.5), (8, 6.5), (14, 6.5), (14, 2.5)]),
             ("p", [(9, 12.5), (13.5, 12.5), (15, 14), (15, 18.5), (9, 18.5)])],
    "ilink": [("r", 5, 7, 19, 16, 2), ("l", [(8.5, 16), (8.5, 19)]), ("l", [(15.5, 16), (15.5, 19)]),
              ("l", [(8, 11.5), (16, 11.5)]), ("l", [(9, 7), (9, 4.5), (15, 4.5), (15, 7)])],
    "cross": [("l", [(5, 5), (19, 19)]), ("l", [(19, 5), (5, 19)])],
    "circle": [("c", 12, 12, 7.5)],
    "triangle": [("p", [(12, 4.5), (20, 18.5), (4, 18.5)])],
    "square": [("r", 5, 5, 19, 19, 0)],
    "sparkle": [("f", SPARK)],
}
# entry name -> (icon, size px, stroke in grid units)
ENTRIES = {f"{n}_18": (n, 18, 1.8) for n in ICONS if n not in ("cross", "circle", "triangle", "square", "sparkle")}
ENTRIES.update({"memcard_36": ("memcard", 36, 1.6), "memcard_48": ("memcard", 48, 1.4),
                "cross_14": ("cross", 14, 3), "circle_14": ("circle", 14, 3), "triangle_14": ("triangle", 14, 3),
                "square_14": ("square", 14, 3), "sparkle_24": ("sparkle", 24, 0), "sparkle_12": ("sparkle", 12, 0)})


def draw_icon(ops, px, stroke):
    k = px * SS / 24
    im = Image.new("L", (px * SS, px * SS))
    d = ImageDraw.Draw(im)
    w = max(1, round(stroke * k))
    P = lambda pts: [(x * k, y * k) for x, y in pts]
    cap = lambda x, y: d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    for op in ops:
        t = op[0]
        if t in "lp":
            pts = P(op[1] + (op[1][:1] if t == "p" else []))
            d.line(pts, fill=255, width=w, joint="curve")
            for x, y in pts: cap(x, y)
        elif t == "f":
            d.polygon(P(op[1]), fill=255)
        elif t in "cd":
            _, cx, cy, r = op
            box = (k * (cx - r), k * (cy - r), k * (cx + r), k * (cy + r))
            d.ellipse(box, fill=255) if t == "d" else d.ellipse(box, outline=255, width=w)
        elif t == "r":
            _, x0, y0, x1, y1, rad = op
            d.rounded_rectangle((k * x0, k * y0, k * x1, k * y1), radius=k * rad, outline=255, width=w)
        elif t == "a":
            _, cx, cy, r, a0, a1 = op
            d.arc((k * (cx - r), k * (cy - r), k * (cx + r), k * (cy + r)), a0, a1, fill=255, width=w)
    return im.resize((px, px), Image.LANCZOS)


def disc(px):  # filled circle edge to edge: quarter of it = a rounded corner (gfx_rrect)
    im = Image.new("L", (px * SS, px * SS))
    ImageDraw.Draw(im).ellipse((0, 0, px * SS - 1, px * SS - 1), fill=255)
    return im.resize((px, px), Image.LANCZOS)


def glow(px):  # radial falloff (1 - r)^2, tinted per draw
    return Image.frombytes("L", (px, px), bytes(
        int(255 * max(0.0, 1 - (((x + 0.5 - px / 2) ** 2 + (y + 0.5 - px / 2) ** 2) ** 0.5) / (px / 2)) ** 2)
        for y in range(px) for x in range(px)))


def orb(px):  # chrome sphere: canvas radial gradient at 34%/28%, 1 px light rim, anti-aliased edge
    stops = [(0, (255, 255, 255)), (.18, (220, 228, 244)), (.52, (128, 144, 178)), (.82, (38, 50, 90)), (1, (18, 26, 56))]
    def col(t):
        for (a, c0), (b, c1) in zip(stops, stops[1:]):
            if t <= b:
                f = (t - a) / (b - a)
                return tuple(round(c0[i] + (c1[i] - c0[i]) * f) for i in range(3))
        return stops[-1][1]
    S = px * SS
    im = Image.new("RGBA", (S, S))
    hx, hy, R = 0.34 * S, 0.28 * S, S / 2
    far = ((S - hx) ** 2 + (S - hy) ** 2) ** 0.5
    px_ = im.load()
    for y in range(S):
        for x in range(S):
            d = ((x + .5 - R) ** 2 + (y + .5 - R) ** 2) ** 0.5
            if d <= R:
                c = col(min(1, ((x - hx) ** 2 + (y - hy) ** 2) ** 0.5 / (far * 0.62)))
                if d > R - SS * 1.2: c = tuple(min(255, v + 70) for v in c)  # rim
                px_[x, y] = (*c, 255)
    return im.resize((px, px), Image.LANCZOS)


ORBS = (56, 110)  # header orb, splash orb (design canvas)


def palettize(im):  # RGBA -> (indices, 256 ABGR CLUT words); index 0 = transparent
    a = np.asarray(im).astype(np.float32)
    rgb = quantize(a[:, :, :3], True)                       # 0..31 per channel, error-diffused
    al = np.rint(a[:, :, 3] / 255 * 7).astype(np.int32)    # 8 alpha levels for the anti-aliased edge
    keys = rgb[:, :, 0] | rgb[:, :, 1] << 5 | rgb[:, :, 2] << 10 | al << 15
    keys[al == 0] = -1
    vals, counts = np.unique(keys[keys >= 0], return_counts=True)
    keep = vals[np.argsort(-counts)][:255]                  # rare colours beyond 255 merge into the nearest kept one
    kr = np.stack([keep & 31, keep >> 5 & 31, keep >> 10 & 31, (keep >> 15) * 4], 1).astype(np.int32)
    idx = np.zeros(keys.shape, np.uint8)
    for y, x in zip(*np.nonzero(keys >= 0)):
        k = keys[y, x]
        v = np.array([k & 31, k >> 5 & 31, k >> 10 & 31, (k >> 15) * 4])
        idx[y, x] = 1 + int(np.argmin(((kr - v) ** 2).sum(1)))
    clut = [0] + [int(r) * 8 | int(g) * 8 << 8 | int(b) * 8 << 16 | (int(al_) * 128 // 7) << 24
                  for r, g, b, al_ in zip(keep & 31, keep >> 5 & 31, keep >> 10 & 31, keep >> 15)]
    return idx, clut + [0] * (256 - len(clut))


def pack(sizes):
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    pos, x, y, shelf = [None] * len(sizes), 0, 0, 0
    for i in order:
        w, h = sizes[i]
        if x + w + PAD > AW:
            x, y, shelf = 0, y + shelf + PAD, 0
        pos[i] = (x, y)
        x, shelf = x + w + PAD, max(shelf, h)
    return pos, y + shelf


def build():
    imgs = {name: draw_icon(ICONS[i], px, s) for name, (i, px, s) in ENTRIES.items()}
    imgs["disc_64"] = disc(64)
    names = list(imgs)
    pos, used = pack([imgs[n].size for n in names])
    h = (used + 127) // 128 * 128
    atlas = Image.new("L", (AW, h))
    for n, p in zip(names, pos):
        atlas.paste(imgs[n], p)
    return names, imgs, pos, atlas, {n: palettize(orb(n)) for n in ORBS}, glow(64)


def write(names, imgs, pos, atlas, orbs, gl):
    a = [round(v * 15 / 255) for v in atlas.tobytes()]
    nib = bytes(a[i] | a[i + 1] << 4 for i in range(0, len(a), 2))
    arr = lambda name, data, per=32: [f"const unsigned char {name}[{len(data)}] __attribute__((aligned(64))) = {{"] +         ["	" + ",".join(map(str, data[i:i + per])) + "," for i in range(0, len(data), per)] + ["};"]
    hdr = ["// Generated by tools/ui_art.py. Do not edit.", "#pragma once", "enum {"]
    hdr += [f"	UI_{n.upper()}," for n in names] + ["	UI_COUNT", "};",
            f"#define UI_ATLAS_W {AW}", f"#define UI_ATLAS_H {atlas.height}", f"#define UI_GLOW {gl.width}",
            "extern const unsigned char ui_atlas[], ui_glow[];",
            "extern const unsigned short ui_rect[UI_COUNT][4]; // u, v, w, h"]
    src = ["// Generated by tools/ui_art.py. Do not edit.", '#include "ui_data.h"'] + arr("ui_atlas", nib) + arr("ui_glow", gl.tobytes())
    for n, (idx, clut) in orbs.items():
        hdr += [f"#define UI_ORB{n} {n}", f"extern const unsigned char ui_orb{n}[];", f"extern const unsigned ui_orb{n}_clut[256];"]
        src += arr(f"ui_orb{n}", idx.tobytes())
        src += [f"const unsigned ui_orb{n}_clut[256] __attribute__((aligned(64))) = {{"]
        src += ["	" + ",".join(f"0x{c:08X}" for c in clut[i:i + 8]) + "," for i in range(0, 256, 8)] + ["};"]
    src += ["const unsigned short ui_rect[UI_COUNT][4] = {"]
    src += [f"	{{{p[0]}, {p[1]}, {imgs[n].width}, {imgs[n].height}}}, // {n}" for n, p in zip(names, pos)] + ["};"]
    (ROOT / "ui_data.h").write_text("\n".join(hdr) + "\n", encoding="utf-8", newline="\n")
    (ROOT / "ui_data.c").write_text("\n".join(src) + "\n", encoding="utf-8", newline="\n")


def selftest():
    names, imgs, pos, atlas, orbs, gl = build()
    r = [(x, y, *imgs[n].size) for n, (x, y) in zip(names, pos)]
    for i, (x, y, w, h) in enumerate(r):
        assert x + w <= AW and y + h <= atlas.height
        for x2, y2, w2, h2 in r[i + 1:]:
            assert x + w <= x2 or x2 + w2 <= x or y + h <= y2 or y2 + h2 <= y, "overlap"
    for n in names:
        assert imgs[n].getbbox(), f"{n} is empty"
    d = disc(64)
    assert d.getpixel((32, 32)) == 255 and d.getpixel((0, 0)) == 0 and d.getpixel((32, 0)) > 100  # edge to edge
    for n, (idx, clut) in orbs.items():
        assert idx[0, 0] == 0 and idx[n // 2, n // 2] > 0 and clut[0] == 0 and len(clut) == 256
        c = clut[idx[n // 2, n // 2]]
        assert all((c >> s & 255) % 8 == 0 for s in (0, 8, 16)), "orb colours must sit on RGB555 levels"
    assert gl.getpixel((32, 32)) > 240 and gl.getpixel((0, 0)) == 0
    pages = AW // 128 * atlas.height // 128
    assert pages <= 4, pages
    print(f"ui_art selftest ok: {len(names)} entries, atlas {AW}x{atlas.height} = {pages} pages, orbs {list(orbs)}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        write(*build())
        print("ui_data.c / ui_data.h written")
