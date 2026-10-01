## Context

Covers are `.c16` pairs on the USB (cover-art spec), loaded by the loader thread at boot. The console is a SCPH-75001
slim, which has a built-in Ethernet port (sources: E, checked in this phase). ps2sdk ships the network pieces in its
`tcpip-dhcp` sample (sources: V):
- on the IOP, `ps2dev9`, `netman` and `smap`;
- on the EE, lwIP (`libps2ip`) over `libnetman`.

xlenore's covers come only over HTTPS. ps2sdk ports ship wolfSSL 5.8.2 and libjpeg (sources: V). The user asked
(2026-10-01) for the version that needs no PC server, which is roadmap option A.

## Goals / Non-Goals

**Goals:**
- Missing covers arrive from the internet with no PC involved.
- They look like the PC tool's output.
- The home screen stays at 60 Hz meanwhile, and boot is unchanged when nothing is missing.

**Non-Goals:**
- Certificate verification (see the risks).
- Redownloading covers that already exist.
- Other network uses: Jellyfin comes later and reuses `net.c`.

## Decisions

- **lwIP on the EE (`ps2ip` + `netman` + `smap`).** It is the ps2sdk sample's path, and libcglue's BSD sockets
  (`socket`, `getaddrinfo`, `send`, `recv`) sit on it, which wolfSSL needs. The alternative, the IOP stack with
  `ps2ips` RPC, is not what wolfSSL's socket I/O calls.
- **wolfSSL directly, not libcurl.**
  - One HTTPS GET is about 40 lines over a socket.
  - curl would add a second I/O layer and pthreads glue for no gain here.
  - HTTP/1.0 with `Connection: close`: GitHub raw then answers with Content-Length and no chunking (sources: V).
- **TLS seed.**
  - The problem: wolfSSL seeds its RNG from `/dev/urandom` (sources: V), and the PS2 has none.
  - The fix: the link wraps `open`/`read` (`--wrap`) and serves that one path from COP0.Count jitter.
  - The alternative, rebuilding wolfSSL with a custom seed callback, means keeping our own build of a ports library.
- **On-EE conversion matches `covers.py`.**
  - The steps: libjpeg decode, sRGB→linear LUT, PIL's Lanczos (support 3 × downscale, normalized taps, horizontal
    first), linear→sRGB, then the existing `gfx_fs_dither`.
  - The check: the host selftest against `covers.py` on SLUS-21376 measures a mean difference of 0.016 / 0.038
    levels and no value off by more than 1 (sources: V).
  - Performance on the EE is not measured yet; the gate measures it.
- **Lazy bring-up in the loader thread after the splash.** DHCP and link waits take seconds (the sample allows 10 s
  each, sources: V), and the loader thread runs below the render thread.
- **Publish to the render thread last.** `cv[i].small` is set before `cv[i].big`, after `SyncDCache`. The renderer
  treats `big == NULL` as no cover, so it never sees half a pair. The pair is shown even if the USB write fails;
  then it is fetched again next boot.

## Risks / Trade-offs

- [No certificate check: a man in the middle could serve another image] → the data is public cover art. Any image
  is decoded with libjpeg's error handler (no `exit`), bounded to 2048×2048, and resized to the fixed cover sizes.
  `ponytail:` comment in `net.c`; embed the root CA if anything private ever uses this.
- [Weak TLS entropy] → it only protects the confidentiality of public images. `ponytail:` comment in `net.c`.
- [Neutrino with `-qb` may conflict with DEV9/SMAP loaded on the IOP] → the network loads only when covers are
  missing. The gate launches Black after a download run.
- [EE time for the TLS handshake and the conversion is unknown] → it runs in the loader thread below render. The
  gate measures the time per cover and the missed vsyncs.
- [IOP RAM with three more modules] → a failed `SifExecModuleBuffer` shows as a status error, not a crash.

## Open Questions

- Does the console get a DHCP lease and DNS on the user's home network? The first console run answers it.
