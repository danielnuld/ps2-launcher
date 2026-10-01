#!/usr/bin/env python3
"""Synthesize the ORBIT sounds (original, no samples) into sfx/*.wav: 16-bit mono 48 kHz (SPU2 native rate).
Style: cybercore Y2K with a 2026 finish — FM stabs, detuned supersaws through resonant sweeps, bitcrush, data
chirps and glitch stutters, over a clean sub.

usage: sfx.py [OUT_DIR]  (default: sfx/ next to the Makefile)  |  sfx.py --selftest
The Makefile turns each WAV into SPU2 ADPCM with ps2sdk's adpenc and links it into launcher.elf.
Splash cue times follow the splash timeline in launcher.c (frames / 60 after the black hold).
"""
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SR = 48000
BPM = 140
S16 = 60 / BPM / 4                                  # one 16th note, seconds
rng = np.random.default_rng(2026)
NOTE = lambda n: 440 * 2 ** ((n - 69) / 12)        # MIDI note -> Hz
E1, E2, B2, E3, B3, E4, FS4, GS4, B4, DS5, E5, FS5, GS5, B5, DS6, E6, GS6, B6 = \
    28, 40, 47, 52, 59, 64, 66, 68, 71, 75, 76, 78, 80, 83, 87, 88, 92, 95


def t_(sec):
    return np.arange(int(sec * SR)) / SR


def env(n, a, d, s=0.0, r=0.0, sus=0.0):  # attack / decay to sustain level / hold / release, seconds
    a, d, sus, r = (int(x * SR) for x in (a, d, sus, r))
    e = np.concatenate([np.linspace(0, 1, max(a, 1)), np.linspace(1, s, max(d, 1)), np.full(sus, s),
                        np.linspace(s, 0, max(r, 1))])
    return np.pad(e, (0, max(0, n - len(e))))[:n]


def osc_saw(f, n):
    p = np.cumsum(np.broadcast_to(f, (n,))) / SR
    return 2 * (p % 1) - 1


def osc_sq(f, n, duty=0.5):
    p = np.cumsum(np.broadcast_to(f, (n,))) / SR
    return np.where(p % 1 < duty, 1.0, -1.0)


def supersaw(f, n, voices=7, detune=0.012):
    return sum(osc_saw(f * (1 + detune * (k - voices // 2) / (voices // 2)), n) for k in range(voices)) / voices


def svf(x, fc, q=4.0):  # state-variable low-pass with resonance; fc may sweep
    fc = np.broadcast_to(fc, x.shape)
    lo = bp = 0.0
    y = np.empty_like(x)
    for i in range(len(x)):
        f = 2 * np.sin(np.pi * min(fc[i], SR / 6) / SR)
        lo += f * bp
        hi = x[i] - lo - bp / q
        bp += f * hi
        y[i] = lo
    return y


def crush(x, bits=6, hold=4):  # bit depth + sample-rate reduction: the Y2K digital grit
    x = np.repeat(x[::hold], hold)[:len(x)]
    q = 2 ** (bits - 1)
    return np.round(x * q) / q


def stutter(x, slice_s=0.03, reps=3):  # glitch: repeat the first slice before playing on
    s = x[:int(slice_s * SR)]
    return np.concatenate([np.tile(s * np.linspace(1, 0.6, len(s)), reps), x])


def fm(f, sec, ratio=2.0, index=3.0, decay=6.0):  # DX-style 2-op FM, index decays
    t = t_(sec)
    return np.sin(2 * np.pi * f * t + index * np.exp(-t * decay) * np.sin(2 * np.pi * f * ratio * t))


def sub_drop(sec, f0=110, f1=45):  # sub boom with pitch drop
    t = t_(sec)
    f = f1 + (f0 - f1) * np.exp(-t * 18)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * env(len(t), 0.002, sec * 0.9)


def noise(n):
    return rng.standard_normal(n)


def hp(x, fc):  # one-pole high-pass
    k = np.exp(-2 * np.pi * fc / SR)
    y, prev_x, prev_y = np.empty_like(x), 0.0, 0.0
    for i in range(len(x)):
        prev_y = k * (prev_y + x[i] - prev_x)
        prev_x = x[i]
        y[i] = prev_y
    return y


def echo(x, delay_s=S16 * 3, fb=0.35, mix=0.35, taps=4):  # tempo-synced digital delay
    d = int(delay_s * SR)
    out = np.pad(x, (0, d * taps))
    wet = np.zeros_like(out)
    for k in range(1, taps + 1):
        wet[d * k:d * k + len(x)] += x * fb ** k
    return out + mix * wet


def place(buf, x, at):
    i = int(at * SR)
    buf[i:i + len(x)] += x[:max(0, len(buf) - i)]


def splash():
    buf = np.zeros(int(5.2 * SR))
    # 0.0 s boot chirp: crushed FM sweep up, glitch-stuttered
    n = int(0.35 * SR)
    sweep = np.sin(2 * np.pi * np.cumsum(np.geomspace(300, 4200, n)) / SR) * env(n, 0.005, 0.33)
    place(buf, 0.35 * stutter(crush(sweep, 5, 3), 0.025, 3), 0.0)
    # 0.5-1.6 s orbit drawing: supersaw through a resonant sweep, gated in 16ths; data ticks fade in
    n = int(1.3 * SR)
    saw = sum(supersaw(NOTE(m), n) for m in (E3, B3, E4))
    gate = np.where((t_(1.3) / S16) % 1 < 0.6, 1.0, 0.25)
    place(buf, 0.28 * svf(saw, np.geomspace(250, 6000, n), 6) * gate * env(n, 0.4, 0.9), 0.5)
    for k in range(int(1.1 / S16)):
        tick = hp(noise(int(0.012 * SR)), 6000) * env(int(0.012 * SR), 0.0005, 0.011)
        place(buf, 0.18 * (k / 9) ** 1.2 * tick, 0.5 + k * S16)
    # 0.6 s towers: sub pad swelling underneath
    n = int(3.8 * SR)
    place(buf, 0.22 * np.sin(2 * np.pi * NOTE(E2) * t_(3.8)) * env(n, 1.0, 0.3, 0.7, 1.6, 0.9), 0.6)
    # 1.2-1.6 s riser into the orb: filtered noise swell
    n = int(0.4 * SR)
    place(buf, 0.25 * svf(noise(n), np.geomspace(500, 9000, n), 3) * np.linspace(0, 1, n) ** 2, 1.2)
    # 1.6 s orb: sub drop + FM stab chord (E maj9 colours) with a 3/16 echo
    place(buf, 0.75 * sub_drop(0.9), 1.6)
    stab = sum(fm(NOTE(m), 1.4, 2.0, 2.5, 5) for m in (E4, GS4, B4, DS5, FS5)) * env(int(1.4 * SR), 0.003, 1.3)
    place(buf, 0.16 * echo(stab), 1.6)
    # 2.4 s wordmark: crushed arpeggio up in 16ths + sparkle blips
    for k, m in enumerate((B4, E5, GS5, B5, E6, GS6)):
        note = crush(fm(NOTE(m), 0.35, 3.0, 1.8, 9), 7, 2) * env(int(0.35 * SR), 0.002, 0.3)
        place(buf, 0.2 * note, 2.4 + k * S16)
    for k in range(14):
        blip = np.sin(2 * np.pi * rng.uniform(3000, 7000) * t_(0.02)) * env(int(0.02 * SR), 0.001, 0.019)
        place(buf, 0.06 * blip, 2.6 + rng.uniform(0, 1.6))
    return buf


def move():  # carousel step: crisp digital click + short crushed swish (the transition)
    n = int(0.16 * SR)
    click = fm(2600, 0.16, 1.5, 3.0, 40) * env(n, 0.0005, 0.03)
    swish = crush(svf(noise(n), np.geomspace(7000, 1500, n), 2), 6, 2) * env(n, 0.01, 0.13)
    return 0.45 * click + 0.18 * swish


def edge():  # end of the row: "denied" double low square blip, crushed
    out = np.zeros(int(0.2 * SR))
    for k in range(2):
        n = int(0.05 * SR)
        place(out, crush(osc_sq(NOTE(E3), n, 0.3), 5, 2) * env(n, 0.002, 0.045), k * 0.075)
    return svf(out, 2500, 1.5)


def confirm():  # X: quick FM arpeggio up, crushed sparkle on top
    out = np.zeros(int(0.7 * SR))
    for k, m in enumerate((E5, B5, E6)):
        place(out, fm(NOTE(m), 0.4, 2.0, 2.2, 7) * env(int(0.4 * SR), 0.002, 0.38), k * 0.035)
    sparkle = crush(fm(NOTE(B6), 0.3, 3.5, 2.0, 10), 6, 2) * env(int(0.3 * SR), 0.002, 0.28)
    place(out, 0.4 * sparkle, 0.1)
    return echo(out, S16 * 2, 0.3, 0.25, 2)


def panel():  # SELECT: data chirp, resonant square sweep, glitch-stuttered
    n = int(0.09 * SR)
    sq = osc_sq(np.geomspace(900, 2400, n), n, 0.25)
    return stutter(svf(sq, np.geomspace(1500, 6000, n), 5) * env(n, 0.002, 0.085), 0.015, 2)


SOUNDS = {"splash": splash, "move": move, "edge": edge, "confirm": confirm, "panel": panel}


def write_wav(path, x):
    x = x / max(1e-9, np.abs(x).max()) * 0.89  # normalize to -1 dBFS
    loud = np.nonzero(np.abs(x) > 0.89 * 10 ** (-60 / 20))[0]  # cut the tail below -60 dB (SPU2 RAM)
    x = x[:loud[-1] + 1] * np.minimum(1, (loud[-1] - np.arange(loud[-1] + 1)) / (0.02 * SR))  # 20 ms fade
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())


def selftest():
    for name in ("move", "edge", "panel", "confirm"):  # the fast ones; splash is long
        x = SOUNDS[name]()
        assert np.isfinite(x).all() and np.abs(x).max() > 0.01 and len(x) < 2 * SR, name  # UI sounds stay short
    e = env(SR, 0.1, 0.1)
    assert e[0] == 0 and abs(e[int(0.1 * SR)] - 1) < 0.01 and e[-1] == 0
    c = crush(np.linspace(-1, 1, 64), 2, 4)
    assert len(set(np.round(c, 6))) <= 2 ** 2 + 1 and all((c[i] == c[i - i % 4]) for i in range(64))  # 2 bits = steps of 1/2 (5 levels incl. both ends), held 4
    print("sfx selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "sfx"
        out.mkdir(parents=True, exist_ok=True)
        for name, fn in SOUNDS.items():
            write_wav(out / f"{name}.wav", fn())
            print(out / f"{name}.wav")
