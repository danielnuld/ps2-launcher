## Context

Checked on 2026-10-02 against Jellyfin 12.1.0 (official Docker image, in the build container) with a 1080p H.264 /
AAC test movie, a 480p H.264 / AC3 5.1 movie and a two-episode series.

- `GET /Videos/{id}/stream.mpeg?static=false&container=mpeg&videoCodec=mpeg2video&audioCodec=mp2&maxWidth=640
  &maxHeight=480&videoBitRate=3000000&...` returns MPEG-2 Main profile, 640x360 for 16:9, 24 fps, progressive, in an
  MPEG program stream (video 0xE0, audio 0xC0), MP2 48 kHz stereo; 2.9 Mbit/s measured over the whole movie.
- `audioCodec=pcm_s16le / pcm_s16be / lpcm` are ignored with `container=mpeg`: the audio is always MP2. So the EE
  decodes MP2 (libmad).
- The body comes with `Transfer-Encoding: chunked` and `Accept-Ranges: none`: no byte seeking; seek = a new request
  with `startTimeTicks`.
- JSON strings escape non-ASCII (`Película Ñandú`).
- API paths that work on 12.1: `/Users/AuthenticateByName`, `/UserViews?userId=`, `/Items?userId=&ParentId=`,
  `/Shows/{id}/Episodes`, `/Items/{id}/Images/Primary?maxWidth=`, `POST /Sessions/Playing[/Progress|/Stopped]`
  (204). A bad token gives 401.

## Decisions

1. **MPEG-2 + MP2 in a program stream.** The IPU decodes MPEG-2 (ps2sdk `libmpeg`, which gives RGBA32 pictures in
   16x16 macroblocks); MP2 is the cheapest audio Jellyfin will pair with it. Program stream over transport stream:
   no 188-byte packets, no PAT / PMT.
2. **640x480 max, 3 Mbit/s.** Main-level pictures, about 1 MB of RGBA32 per picture, under 1/10 of 100BASE-TX.
3. **Pictures to the screen through the existing engine.** VRAM holds the 1280x720 CT16 double buffer, so there is
   no room for a 640x360 CT32 texture: the EE converts each picture to three CT16 strips (256 / 256 / 128 wide, the
   `gfx_image` limit) and `gfx_image` scales them (2x for 640x360). The cost (one pass over ~230 k pixels) is the
   first thing the gate measures.
4. **Audio is the clock**: first audio PTS + samples played (sent − `audsrv_queued`). Pictures more than two frame
   times behind are decoded but not drawn; early ones wait (2 s at most).
5. **Threads**: audio 0x28, HTTP reader 0x30, lwIP 0x56-0x59 (ps2sdk), decode + draw 0x60, so the network never
   waits behind the conversion.
6. **Plain HTTP, LAN only**: Jellyfin's default 8096; https is refused (no TLS in this client).

## Risks

- EE time per picture (IPU decode waits + CT16 conversion + upload) at 24 / 30 fps: the gate.
- lwIP receive throughput at ~400 KB/s with the EE busy.
- Picture PTS: libmpeg takes the PTS current when the data was fed; pictures without one are extrapolated.
- 25 / 50 Hz sources on a 60 Hz output: no pulldown; pictures are shown at the next vsync after their time.
