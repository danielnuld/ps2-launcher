## Why

Today covers reach the USB only through `tools/fetch_covers.py` on a PC with the USB plugged in. The user wants the
PS2 to get them itself, with no PC involved (roadmap item 3). They first chose option B, a PC server; on 2026-10-01
they asked for the version without it. That is option A: the PS2 fetches the JPG from xlenore/ps2-covers itself.
xlenore is reachable only over HTTPS (sources: V), so the PS2 needs TLS and a JPEG decoder. Both ship in ps2sdk ports.

## What Changes

- `config.ini` gets two new sections:
  - `[red]`: `ip = dhcp`, or a fixed IP with `mascara`, `puerta` and `dns`;
  - `[portadas]`: `descargar = si | no`, default `si`.
- After the home screen is up, if some games have no cover and downloads are on, the launcher:
  - starts the network (built-in Ethernet);
  - fetches `https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/<SERIAL>.jpg` for each one;
  - decodes, resizes and dithers it on the EE with the same steps as `covers.py`;
  - saves the `.c16` pair to `mass0:/covers/` and shows it in the carousel at once.
- The home screen shows the download state in a status line: connecting, `n / m`, or the error.

## Capabilities

### New Capabilities
- `cover-download`: network config, HTTPS fetch, on-console conversion, and download of missing covers.

### Modified Capabilities
None. The `.c16` format and the drawing rules of `cover-art` stay the same, and downloaded files are ordinary
cover files.

## Impact

- New `net.c` / `net.h`: network bring-up and an HTTPS GET with wolfSSL. Includes an HTTP response parser with a
  host selftest.
- New `cover.c` / `cover.h`: JPEG to `.c16` pixels, with a host selftest that compares against `covers.py`.
- Changed `launcher.c`: download step, status line, config keys.
- Changed `Makefile`:
  - IRX ps2dev9, netman, smap;
  - `-lwolfssl -ljpeg -lnetman -lps2ip`;
  - wrapped `open`/`read` for the TLS seed.
- The network modules load only when a download is needed.
