## 1. Assets

- [x] 1.1 `ui_art.py`: carousel, grid and list icons (18 px); regenerate `ui_data.c/h`; atlas still fits its VRAM
- [x] 1.2 Half-size covers (2×2 linear-light mean + Floyd–Steinberg) at load and after downloads; host check of a flat colour

## 2. Views

- [x] 2.1 Per-cover animated rect / alpha / focus with snapping; draw by source size; the carousel rebuilt on it, looking as before
- [x] 2.2 Grid layout, navigation, row scroll and edge fades
- [x] 2.3 List layout: rows, gliding pill, scroll, big cover on the right, row entrance stagger
- [x] 2.4 □ view switch with staggered morph, view name label, `estado.ini`, □ "Vista" hint

## 3. Checks

- [x] 3.1 PCSX2: screenshots of the three views at rest and mid-switch; frame windows per view (correctness only)

## 4. Gate

- [x] 4.1 Console: frame windows per view and during switches, 0 missed vsyncs and max ≤ 8 333 µs; the user approves the look; `docs/phase10-results.md`

## 5. Animated background

- [x] 5.1 `ambient()`: aurora glows, flowing floor with parallax, horizon sweep, breathing floor glow, rising motes, twinkling sparkles; checked in PCSX2 (frame differences on idle screens)
- [x] 5.2 Console: frame windows with the ambience on, user verdict on the TV
