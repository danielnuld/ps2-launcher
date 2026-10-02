/*
  menu.c - in-game menu for the ORBIT fork of Neutrino (phase 13). Spike: a "PAUSA" box over the paused game.

  The game's GS drawing state is write-only, so it is never touched: only image transfers. The area under the box
  is read to EE memory (local->host, the sequence of OPL's ee_core/src/igs_api.c, AFL-3.0), the box is rendered on
  the EE in the frame's own format and uploaded (host->local), and the saved pixels go back on resume. Both ways go
  through VIF1 DIRECT (PATH2) after FLUSHA, and leave the game's PATH3 mask alone (OPL unmasks it, but then reboots).
*/

#include <kernel.h>
#include "eecore_config.h"
#include "ee_asm.h"
#include "igr.h"

void GSM_GetDisplay(u64 *dispfb1, u64 *dispfb2, u64 *pmode); // gsm_api.c (patch_neutrino.py step 5)

#define D1_CHCR   ((vu32 *)0x10009000)
#define D1_MADR   ((vu32 *)0x10009010)
#define D1_QWC    ((vu32 *)0x10009020)
#define D2_CHCR   ((vu32 *)0x1000a000)
#define D_STAT    ((vu32 *)0x1000e010)
#define GIF_STAT  ((vu32 *)0x10003020)
#define VIF1_STAT ((vu32 *)0x10003c00)
#define GS_CSR    ((vu64 *)0x12001000)
#define GS_BUSDIR ((vu64 *)0x12001040)
#define CHCR_STR  0x100
#define VIF_FQC   0x1f000000
#define VIF_FDR   0x00800000
#define GIF_BUSY  0x1f000c00 // FQC and APATH
#define CSR_FINISH 2

#define VIF_NOP          0
#define VIF_FLUSHA       (0x13 << 24)
#define VIF_DIRECT(n)    ((0x50 << 24) | (n))
#define GIFTAG(nloop, eop, flg, nreg) ((u64)(nloop) | (u64)(eop) << 15 | (u64)(flg) << 58 | (u64)(nreg) << 60)
#define GIF_AD    0xe
#define GS_BITBLTBUF 0x50
#define GS_TRXPOS    0x51
#define GS_TRXREG    0x52
#define GS_TRXDIR    0x53
#define GS_FINISH    0x61

#define PAD_START  0x0008
#define PAD_CIRCLE 0x2000
#define PAD_CROSS  0x4000

// The box: 96 x 24 pixels, "PAUSA" in an 8x8 font at 2x. Saved pixels fit the ~19 KB ee_core has left
#define BOX_W  96
#define BOX_H  24
#define STRIP  4 // lines per upload
static u8 saved[BOX_W * BOX_H * 4] __attribute__((aligned(64)));
static u32 packet[(7 * 16 + BOX_W * STRIP * 4) / 4] __attribute__((aligned(64)));

static const u8 glyph[5][8] = { // P A U S, space
    {0xfc, 0xc6, 0xc6, 0xfc, 0xc0, 0xc0, 0xc0, 0x00},
    {0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0x00},
    {0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00},
    {0x7c, 0xc6, 0xc0, 0x7c, 0x06, 0xc6, 0x7c, 0x00},
    {0},
};
static const u8 text_ok[5] = {0, 1, 2, 3, 1};    // PAUSA: the frame came from the DISPFB capture
static const u8 text_guess[5] = {4, 2, 0, 3, 4}; // " UPS ": no DISPFB seen, the box went to a guessed frame
static const u8 *text;

static int bpp; // bytes per pixel of the shown frame: 4 (CT32), 3 (CT24), 2 (CT16 / CT16S)

static int wait_clear(vu32 *reg, u32 bits) // bounded: a stuck path must not hang the pause forever
{
    int i;
    for (i = 0; i < 10000000; i++)
        if (!(*reg & bits))
            return 1;
    return 0;
}

static void vif1_send(void *p, u32 qwc)
{
    FlushCache(0);
    *D1_QWC = qwc;
    *D1_MADR = (u32)p;
    *D1_CHCR = 0x101; // from memory, normal mode, start
    asm volatile("sync.l");
    wait_clear(D1_CHCR, CHCR_STR);
}

// VIF codes + GIF A+D setup for a transfer; returns the number of qwords written at p
static u32 xfer_setup(u32 *p, u64 bitbltbuf, u32 x, u32 y, u32 w, u32 h, int dir, u32 data_qw)
{
    u64 *q = (u64 *)(p + 4);
    u32 n = dir ? 6 : 6 + data_qw; // qwords after the DIRECT code
    p[0] = VIF_NOP;
    p[1] = VIF_NOP; // no MSKPATH3: the game's PATH3 mask is its own state (its GIF DMA is idle; FLUSHA waits)
    p[2] = VIF_FLUSHA;
    p[3] = VIF_DIRECT(n);
    q[0] = GIFTAG(dir ? 5 : 4, dir, 0, 1);
    q[1] = GIF_AD;
    q[2] = bitbltbuf;
    q[3] = GS_BITBLTBUF;
    q[4] = dir ? ((u64)x | (u64)y << 16) : ((u64)x << 32 | (u64)y << 48); // TRXPOS: source or destination
    q[5] = GS_TRXPOS;
    q[6] = (u64)w | (u64)h << 32;
    q[7] = GS_TRXREG;
    if (dir) {
        q[8] = 0;
        q[9] = GS_FINISH;
        q[10] = 1; // local -> host
        q[11] = GS_TRXDIR;
        return 7;
    }
    q[8] = 0; // host -> local
    q[9] = GS_TRXDIR;
    q[10] = GIFTAG(data_qw, 1, 2, 0); // IMAGE
    q[11] = 0;
    return 7;
}

static void vram_read(u32 bp, u32 bw, u32 psm, u32 x, u32 y)
{
    u32 imr = GsPutIMR(GsGetIMR() | 0x0200); // FINISH must not raise an interrupt
    u32 chcr = *D1_CHCR;
    wait_clear(VIF1_STAT, VIF_FQC);
    *GS_CSR = CSR_FINISH;
    vif1_send(packet, xfer_setup(packet, (u64)bp | (u64)bw << 16 | (u64)psm << 24, x, y, BOX_W, BOX_H, 1, 0));
    for (int i = 0; i < 10000000 && !(*GS_CSR & CSR_FINISH); i++)
        ;
    wait_clear(VIF1_STAT, VIF_FQC);
    *VIF1_STAT = VIF_FDR; // VIF1 FIFO and GS bus reversed: the GS sends
    *GS_BUSDIR = 1;
    FlushCache(0);
    *D1_QWC = BOX_W * BOX_H * bpp / 16;
    *D1_MADR = (u32)saved;
    *D1_CHCR = 0x100; // to memory
    asm volatile("sync.l");
    wait_clear(D1_CHCR, CHCR_STR);
    FlushCache(0);
    *D1_CHCR = chcr;
    asm volatile("sync.l");
    *VIF1_STAT = 0;
    *GS_BUSDIR = 0;
    GsPutIMR(imr);
    *GS_CSR = CSR_FINISH;
}

static int text_pixel(u32 x, u32 y) // x, y inside the box
{
    u32 tx = x - (BOX_W - 5 * 16) / 2, ty = y - (BOX_H - 16) / 2;
    if (tx >= 5 * 16 || ty >= 16)
        return 0;
    return glyph[text[tx / 16]][ty / 2] & (0x80 >> (tx % 16 / 2));
}

// Box line y of the box into d: the saved pixels darkened, text in white (draw = 0: the saved pixels as they were)
static void render_line(u8 *d, u32 y, int draw)
{
    const u8 *s = (const u8 *)UNCACHED_SEG(saved) + y * BOX_W * bpp; // DMA wrote it behind the cache
    for (u32 x = 0; x < BOX_W; x++, s += bpp, d += bpp) {
        int t = draw && text_pixel(x, y);
        if (bpp == 4) {
            u32 p = s[0] | s[1] << 8 | s[2] << 16 | (u32)s[3] << 24;
            p = !draw ? p : t ? (p | 0x00ffffff) : (((p >> 1) & 0x007f7f7f) | (p & 0xff000000));
            d[0] = p, d[1] = p >> 8, d[2] = p >> 16, d[3] = p >> 24;
        } else if (bpp == 3) {
            for (int c = 0; c < 3; c++)
                d[c] = !draw ? s[c] : t ? 0xff : s[c] >> 1;
        } else {
            u32 p = s[0] | s[1] << 8;
            p = !draw ? p : t ? (p | 0x7fff) : (((p >> 1) & 0x3def) | (p & 0x8000));
            d[0] = p, d[1] = p >> 8;
        }
    }
}

static void vram_write(u32 bp, u32 bw, u32 psm, u32 x, u32 y, int draw)
{
    u32 chcr = *D1_CHCR;
    u32 line = BOX_W * bpp, qw = line * STRIP / 16;
    for (u32 y0 = 0; y0 < BOX_H; y0 += STRIP) {
        u32 n = xfer_setup(packet, (u64)bp << 32 | (u64)bw << 48 | (u64)psm << 56, x, y + y0, BOX_W, STRIP, 0, qw);
        for (u32 l = 0; l < STRIP; l++)
            render_line((u8 *)(packet + n * 4) + l * line, y0 + l, draw);
        vif1_send(packet, n + qw);
    }
    *D1_CHCR = chcr;
    asm volatile("sync.l");
}

// Spike: show the box until X, then put the picture back. The game is paused by the caller
void Menu_Run(void)
{
    u64 dispfb1, dispfb2, pmode, fb;
    GSM_GetDisplay(&dispfb1, &dispfb2, &pmode);
    fb = (pmode & 1) ? dispfb1 : dispfb2; // circuit 1 first, as the PCRTC shows it on top
    text = text_ok;
    if (!fb) { // spike: no capture (PCSX2 has no data breakpoints); Black's frame: FBP 0, 640 wide, CT16S
        fb = 10 << 9 | 10 << 15;
        text = text_guess;
    }
    u32 psm = (fb >> 15) & 0x1f;
    bpp = psm == 0 ? 4 : psm == 1 ? 3 : (psm == 2 || psm == 10) ? 2 : 0;
    if (!bpp)
        return; // a format the box cannot draw: resume at once
    u32 bp = (fb & 0x1ff) * 32, bw = (fb >> 9) & 0x3f;
    u32 x = ((fb >> 32) & 0x7ff) + (bw * 64 - BOX_W) / 2, y = ((fb >> 43) & 0x7ff) + 64;

    u32 bpc = _ee_disable_bpc(); // our own CSR / BUSDIR accesses must not trap into GSM
    wait_clear(D1_CHCR, CHCR_STR);
    wait_clear(D2_CHCR, CHCR_STR);
    // the game's own transfers are over now; a channel 1 completion it has not handled yet stays for its handler
    u32 game_done = *D_STAT & 2;
    vram_read(bp, bw, psm, x, y);
    vram_write(bp, bw, psm, x, y, 1);

    while (IGR_Buttons()) // let go of the combo first
        ;
    while (!(IGR_Buttons() & (PAD_CROSS | PAD_CIRCLE | PAD_START)))
        ;
    while (IGR_Buttons())
        ;

    vram_write(bp, bw, psm, x, y, 0);
    // DMA done is not the end: the VIF1 FIFO and the GIF may still be moving our image and the PATH3 unmask. A game
    // that resumes into that froze (PCSX2, Black's title)
    wait_clear(VIF1_STAT, VIF_FQC);
    wait_clear(GIF_STAT, GIF_BUSY);
    // our VIF1 transfers flagged channel 1 done; left set, the game's VIF1 handler would run for a transfer it never
    // started. Write 1 clears it (the mask bits stay)
    if (!game_done)
        *D_STAT = 2;
    _ee_enable_bpc(bpc);
}
