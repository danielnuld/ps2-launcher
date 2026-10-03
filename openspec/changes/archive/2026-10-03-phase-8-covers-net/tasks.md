## 1. Conversion on the EE

- [x] 1.1 `cover.c`: libjpeg decode, linear-light Lanczos as PIL, `gfx_fs_dither`; host selftest (flat colour) in `make test`
- [x] 1.2 Host comparison with `covers.py` on SLUS-21376 (`make test COVER_CHECK=...`): no channel value off by more than 1

## 2. PS2 network and HTTPS

- [x] 2.1 `net.c` / `net.h`: load ps2dev9 / netman / smap, lwIP init, DHCP or fixed IP + DNS, link and lease waits with error codes
- [x] 2.2 `https_get` with wolfSSL (SNI, HTTP/1.0), `/dev/urandom` seed through wrapped `open`/`read`; response parser with a host selftest
- [x] 2.3 Makefile: IRX objects, ports include/libs, `--wrap`, `net.o`, `cover.o`

## 3. Launcher

- [x] 3.1 `config.ini` `[red]` (`ip`, `mascara`, `puerta`, `dns`) and `[portadas] descargar` in the template; read them
- [x] 3.2 Download step after the splash: missing covers only, convert, write the pair to `mass0:/covers/`, publish small then big
- [x] 3.3 Status line (connecting / n / m / error), fades after success
- [x] 3.4 PCSX2 with its network adapter, if it can be set up: download works or fails cleanly, no crash, status line shown

## 4. Gate

- [x] 4.1 Console with internet: covers download and appear, the next boot loads them from the USB; 0 missed vsyncs during the download; Black launches after a download run. Record the time per cover, missed vsyncs and launch result in `docs/phase8-results.md`
