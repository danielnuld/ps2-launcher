/*
  menu.c - in-game menu for the ORBIT fork of Neutrino (phase 13): Reiniciar / Apagar / Cancelar over the paused game.

  The game's GS drawing state is write-only, so it is never touched: only image transfers. The menu is a solid panel
  rendered on the EE in the shown frame's own format and uploaded (host->local). When the area under it fits in
  `saved` (16-bit frames), it is read first (local->host, the sequence of OPL's ee_core/src/igs_api.c, AFL-3.0) and
  put back on Cancelar; otherwise the game's next frames draw over it. Both ways go through VIF1 DIRECT (PATH2) after
  FLUSHA, and leave the game's PATH3 mask alone (OPL unmasks it, but then reboots).
*/

#include <kernel.h>
#include "eecore_config.h"
#include "ee_asm.h"
#include "igr.h"

// 1 = when no DISPFB was captured, draw into Black's frame (FBP 0, 640 wide, CT16S). Only for PCSX2, which has no
// data breakpoints: tools/build_neutrino.sh sets it with MENU_TEST=1. In a real game a guess could hit its textures
#define MENU_TEST 0

void GSM_GetDisplay(u64 out[6]); // gsm_api.c (patch_neutrino.py step 5)

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
#define PAD_UP     0x0010
#define PAD_DOWN   0x0040
#define PAD_CIRCLE 0x2000
#define PAD_CROSS  0x4000

// 144 x 56: three items in a 6x8 font at 2x (12x16 cells), 18 px apart. A 16-bit frame's area (16 KB) is saved in
// eec.MenuSave, which the loader reserves in module storage (phase 18: it was 16 KB of the 64 KB ee_core region);
// a 24/32-bit one is not saved, nor anything when there is no buffer
#define BOX_W  144
#define BOX_H  56
#define STRIP  2 // lines per upload
#define TEXT_X 28
#define MARK_X 10
#define ROW_Y(i) (4 + (i) * 18)
#define saved ((u8 *)eec.MenuSave)
static u32 packet[(7 * 16 + BOX_W * STRIP * 4) / 4] __attribute__((aligned(64)));

// 5x7 glyphs in 6x8 cells (the 6th column and 8th row are the gaps), bit 7 = left column
enum { G_A, G_C, G_E, G_G, G_I, G_L, G_N, G_P, G_R, G_MARK, G_S, G_T, G_O, G_W, G_F, G_SP, G_END = 0xff };
static const u8 glyph[][8] = {
    {0x70, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, 0x00}, // A
    {0x78, 0x80, 0x80, 0x80, 0x80, 0x80, 0x78, 0x00}, // C
    {0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0xf8, 0x00}, // E
    {0x78, 0x80, 0x80, 0x98, 0x88, 0x88, 0x78, 0x00}, // G
    {0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0xf8, 0x00}, // I
    {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xf8, 0x00}, // L
    {0x88, 0xc8, 0xa8, 0x98, 0x88, 0x88, 0x88, 0x00}, // N
    {0xf0, 0x88, 0x88, 0xf0, 0x80, 0x80, 0x80, 0x00}, // P
    {0xf0, 0x88, 0x88, 0xf0, 0xa0, 0x90, 0x88, 0x00}, // R
    {0x80, 0xc0, 0xe0, 0xf0, 0xe0, 0xc0, 0x80, 0x00}, // selection mark
    {0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xf0, 0x00}, // S (phase 18: English labels)
    {0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00}, // T
    {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00}, // O
    {0x88, 0x88, 0x88, 0xa8, 0xa8, 0xd8, 0x88, 0x00}, // W
    {0xf8, 0x80, 0x80, 0xf0, 0x80, 0x80, 0x80, 0x00}, // F
    {0}, // space
};
enum { M_REBOOT, M_OFF, M_CANCEL, M_ITEMS };
static const u8 labels[2][M_ITEMS][10] = { // eec.IgrLang: the launcher's [ui] idioma (phase 18)
    {{G_R, G_E, G_I, G_N, G_I, G_C, G_I, G_A, G_R, G_END}, // REINICIAR
     {G_A, G_P, G_A, G_G, G_A, G_R, G_END},                // APAGAR
     {G_C, G_A, G_N, G_C, G_E, G_L, G_A, G_R, G_END}},     // CANCELAR
    {{G_R, G_E, G_S, G_T, G_A, G_R, G_T, G_END},           // RESTART
     {G_P, G_O, G_W, G_E, G_R, G_SP, G_O, G_F, G_F, G_END}, // POWER OFF
     {G_C, G_A, G_N, G_C, G_E, G_L, G_END}},               // CANCEL
};
#define label labels[eec.IgrLang ? 1 : 0]

// ORBIT colours (R, G, B): panel, border, item, selected item
static const u8 rgb[4][3] = {{14, 18, 38}, {86, 110, 176}, {120, 132, 168}, {236, 240, 255}};
enum { C_PANEL, C_BORDER, C_ITEM, C_SEL };

static int bpp; // bytes per pixel of the shown frame: 4 (CT32), 3 (CT24), 2 (CT16 / CT16S)
static int sel;

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
    p[0] = VIF_NOP;
    p[1] = VIF_NOP; // no MSKPATH3: the game's PATH3 mask is its own state (its GIF DMA is idle; FLUSHA waits)
    p[2] = VIF_FLUSHA;
    p[3] = VIF_DIRECT(dir ? 6 : 6 + data_qw); // qwords after the DIRECT code
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

static void vram_read(u32 bp, u32 bw, u32 psm, u32 x, u32 y) // the box area into `saved`
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

static int text_pixel(const u8 *s, u32 x, u32 y) // a string at 2x, (x, y) relative to its top left
{
    if (y >= 16)
        return 0;
    for (; *s != G_END; s++, x -= 12)
        if (x < 12)
            return glyph[*s][y / 2] & (0x80 >> (x / 2));
    return 0;
}

static int menu_colour(u32 x, u32 y)
{
    if (x == 0 || y == 0 || x == BOX_W - 1 || y == BOX_H - 1)
        return C_BORDER;
    for (int i = 0; i < M_ITEMS; i++) {
        u32 ty = y - ROW_Y(i);
        if (ty >= 16)
            continue;
        static const u8 mark[2] = {G_MARK, G_END};
        if (i == sel && x >= MARK_X && text_pixel(mark, x - MARK_X, ty))
            return C_SEL;
        if (x >= TEXT_X && text_pixel(label[i], x - TEXT_X, ty))
            return i == sel ? C_SEL : C_ITEM;
    }
    return C_PANEL;
}

// Box line y into d: the menu (draw), or the saved pixels as they were
static void render_line(u8 *d, u32 y, int draw)
{
    const u8 *s = (const u8 *)UNCACHED_SEG(saved) + y * BOX_W * bpp; // DMA wrote it behind the cache
    for (u32 x = 0; x < BOX_W; x++, s += bpp, d += bpp) {
        if (!draw) {
            for (int c = 0; c < bpp; c++)
                d[c] = s[c];
            continue;
        }
        const u8 *c = rgb[menu_colour(x, y)];
        if (bpp == 2) {
            u32 p = c[0] >> 3 | (c[1] >> 3) << 5 | (c[2] >> 3) << 10 | 0x8000;
            d[0] = p, d[1] = p >> 8;
        } else {
            d[0] = c[0], d[1] = c[1], d[2] = c[2];
            if (bpp == 4)
                d[3] = 0x80;
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

// The game is paused by the caller. Returns MENU_CANCEL, MENU_REBOOT or MENU_OFF
int Menu_Run(void)
{
    u64 gs[6]; // DISPFB1, DISPFB2, PMODE, DISPLAY1, DISPLAY2, SMODE2
    GSM_GetDisplay(gs);
    int c1 = (gs[2] & 1) != 0; // circuit 1 first, as the PCRTC shows it on top
    u64 fb = c1 ? gs[0] : gs[1], disp = c1 ? gs[3] : gs[4];
#if MENU_TEST
    if (!fb)
        fb = 10 << 9 | 10 << 15;
#endif
    u32 psm = (fb >> 15) & 0x1f;
    bpp = psm == 0 ? 4 : psm == 1 ? 3 : (psm == 2 || psm == 10) ? 2 : 0;
    if (!fb || !bpp)
        return MENU_CANCEL; // no DISPFB seen (GSM unhooked), or a format the menu cannot draw: resume at once

    u32 bp = (fb & 0x1ff) * 32, bw = (fb >> 9) & 0x3f;
    u32 w = bw * 64, h = 224; // frame lines shown: DISPLAY height over its vertical magnification
    if (disp)
        h = (((u32)(disp >> 44) & 0x7ff) + 1) / (((u32)(disp >> 27) & 3) + 1); // u32: a u64 divide pulls 6 KB of libgcc
    if ((gs[5] & 3) == 3) // interlaced FRAME mode: the buffer holds one field, half the lines
        h /= 2;
    u32 x = ((fb >> 32) & 0x7ff) + (w > BOX_W ? (w - BOX_W) / 2 : 0);
    u32 y = ((fb >> 43) & 0x7ff) + (h > BOX_H ? (h - BOX_H) / 2 : 0);
    int keep = saved != NULL && BOX_W * BOX_H * bpp <= EEC_MENU_SAVE_BYTES;

    u32 bpc = _ee_disable_bpc(); // our own CSR / BUSDIR accesses must not trap into GSM
    wait_clear(D1_CHCR, CHCR_STR);
    wait_clear(D2_CHCR, CHCR_STR);
    // the game's own transfers are over now; a channel 1 completion it has not handled yet stays for its handler
    u32 game_done = *D_STAT & 2;
    if (keep)
        vram_read(bp, bw, psm, x, y);

    sel = M_CANCEL;
    vram_write(bp, bw, psm, x, y, 1);
    u16 held = IGR_Buttons(), now; // the combo is still held: only new presses count
    for (;;) {
        now = IGR_Buttons();
        u16 pressed = now & ~held;
        held = now;
        if (pressed & (PAD_UP | PAD_DOWN)) {
            sel = (sel + (pressed & PAD_UP ? M_ITEMS - 1 : 1)) % M_ITEMS;
            vram_write(bp, bw, psm, x, y, 1);
        } else if (pressed & (PAD_CIRCLE | PAD_START)) {
            sel = M_CANCEL;
            break;
        } else if (pressed & PAD_CROSS)
            break;
    }
    while (IGR_Buttons()) // the game must not see the button that closed the menu
        ;

    if (sel == M_CANCEL && keep)
        vram_write(bp, bw, psm, x, y, 0);
    // DMA done is not the end: the VIF1 FIFO and the GIF may still be moving our image
    wait_clear(VIF1_STAT, VIF_FQC);
    wait_clear(GIF_STAT, GIF_BUSY);
    // our VIF1 transfers flagged channel 1 done; left set, the game's VIF1 handler would run for a transfer it never
    // started. Write 1 clears it (the mask bits stay)
    if (!game_done)
        *D_STAT = 2;
    _ee_enable_bpc(bpc);
    return sel == M_REBOOT ? MENU_REBOOT : sel == M_OFF ? MENU_OFF : MENU_CANCEL;
}
