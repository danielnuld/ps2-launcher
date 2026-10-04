/*
  ra.c - RetroAchievements telemetry for the ORBIT fork of Neutrino (phase 16b).

  Every VBLANK (from the IGR handler, igr.c) the watched addresses are read into the snapshot buffer and the buffer
  is SIF-DMA'd to the IOP agent (raagent.irx), which sends it to xeRAbora's client. Everything lives in the watch
  block the loader placed in module storage (-ra=): the list, a 64-byte mailbox at ModStorageStart + 128 KB (past
  the module checksum) and the snapshot buffer after it. The agent writes its IOP buffer address into mailbox[0].
  Data path and snapshot layout from xeRAbora's OPL agent (ee_core/src/ra.c, AFL-3.0) and its protocol ra_snap.h.
  Direct addresses only: the list's pointer-chain tail is not resolved (design decision, phase-16b-ra-agent).
*/

#include <kernel.h>
#include <sifdma.h>
#include "eecore_config.h"
#include "ee_asm.h"
#include "igr.h"
#include "ra_snap.h"

extern int isceSifSetDma(SifDmaTransfer_t *dmat, int count); // libkernel, interrupt-safe; not in sifdma.h
extern int isceSifDmaStat(int trid);

#define RA_START_DELAY 600 // frames before the first read: the game loads its own ELF first (xeRAbora RA_START_DELAY)
#define RA_RAM_LOW  0x00080000
#define RA_RAM_HIGH 0x02000000

static u32 frames, seq, skip, fail, last_ticks;
static int dma_id;

static inline u32 ticks(void) { u32 t; asm volatile("mfc0 %0, $9" : "=r"(t)); return t; }

void RA_OnVblank(void) // interrupt context: no waiting
{
    u32 now = ticks(), frame_ticks = now - last_ticks, iop;
    last_ticks = now;
    if (eec.RaMbox == NULL || ++frames <= RA_START_DELAY)
        return;
    iop = *(volatile u32 *)UNCACHED_SEG(eec.RaMbox);
    if (iop == 0) // the agent has not said where its buffer is
        return;
    if (dma_id != 0 && isceSifDmaStat(dma_id) >= 0) { // the last copy still runs: skip, never wait
        skip++;
        return;
    }
    u8 *buf = eec.RaMbox + 64;
    struct ra_snap *s = (struct ra_snap *)UNCACHED_SEG(buf);
    u8 *v = (u8 *)UNCACHED_SEG(buf + RA_SNAP_HDR);
    u32 n = eec.RaCount, bytes = eec.RaBytes, off = 0, i, j;
    s->magic = RA_SNAP_MAGIC;
    s->seq = ++seq;
    s->frames = frames;
    s->dma_skip = skip;
    s->dma_fail = fail;
    s->count = n;
    s->bytes = bytes;
    for (i = 0; i < sizeof(s->game_id); i++)
        s->game_id[i] = i < sizeof(eec.GameID) ? eec.GameID[i] : 0;
    for (i = 0; i < n; i++) { // uncached reads: RAM as is, without pulling the game's data through the D-cache
        u32 e = eec.RaWatch[i], a = RA_WATCH_ADDR(e), z = RA_WATCH_SIZE(e);
        if (off + z > bytes)
            break;
        if (a < RA_RAM_LOW || a + z > RA_RAM_HIGH) // outside the game's RAM: zero, never read
            for (j = 0; j < z; j++) v[off++] = 0;
        else if (z == 4 && !(a & 3)) {
            u32 x = *(volatile u32 *)UNCACHED_SEG(a);
            v[off++] = x, v[off++] = x >> 8, v[off++] = x >> 16, v[off++] = x >> 24;
        } else if (z == 2 && !(a & 1)) {
            u16 x = *(volatile u16 *)UNCACHED_SEG(a);
            v[off++] = x, v[off++] = x >> 8;
        } else // a byte, or an unaligned wider value: byte loads (an unaligned word load faults in a handler)
            for (j = 0; j < z; j++) v[off++] = *(volatile u8 *)UNCACHED_SEG(a + j);
    }
    s->read_cycles = ticks() - now;
    s->frame_cycles = frame_ticks;
    s->seq_end = seq;
    *(volatile u32 *)UNCACHED_SEG(buf + RA_SNAP_TRAILER_OFF(bytes)) = seq; // last word to land: the tear check
    SifDmaTransfer_t t = {buf, (void *)iop, RA_SNAP_DMA_SIZE(bytes), 0};
    if ((dma_id = isceSifSetDma(&t, 1)) == 0)
        fail++;
}
