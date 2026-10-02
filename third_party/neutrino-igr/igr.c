/*
  igr.c - In Game Reset for Neutrino (ORBIT launcher fork, phase 12)

  Ported from Open PS2 Loader ee_core/src/padhook.c (commit 3e3f34e):
  Copyright 2009-2010, Ifcaro, jimmikaelkael & Polo
  Copyright 2006-2008 Polo
  Licenced under Academic Free License version 3.0
  PadOpen hooking inspired from ps2rd:
  Copyright (C) 2009 jimmikaelkael <jimmikaelkael@wanadoo.fr>
  Copyright (C) 2009 misfire <misfire@xploderfreax.de>

  Changes from OPL: the two combos are 16-bit libpad button masks passed by the frontend (eec.IgrExitCombo,
  eec.IgrOffCombo) instead of fixed L1+L2+R1+R2 + START+SELECT / L3+R3; exit resets the IOP to ROM, runs OPL's
  resetspu.irx, undoes Neutrino's kernel patches and LoadExecPS2s eec.IgrExitPath (the frontend passes rom0:OSDSYS:
  a reboot, FMCB then autoboots it); power off sends CDVD S-command 0x0F (the one cdvdman's sceCdPowerOff sends)
  straight from the VBLANK handler; the return shows its stage as the screen colour; no IGS screenshot.
*/

#include <kernel.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include "iopmgr.h"
#include <sifrpc.h>
#include "ee_debug.h"
#include "util.h"
#include "eecore_config.h"
#include "cheat_api.h"
#include "gsm_api.h"
#include "igr.h"
#include "padpatterns.h"

void DisableGSM(void); // gsm_api.c
void GSM_Rearm(void);   // gsm_api.c (patch_neutrino.py step 5)
void Remove_Kernel_Hooks(void); // iopmgr.c
extern unsigned char resetspu_irx[]; // resetspu/, built by tools/build_neutrino.sh
extern unsigned int size_resetspu_irx;

// EE registers (the local ee_regs.h shadows ps2sdk's): DMA enable, control, channel CHCRs, GS CSR
#define R_D_ENABLER ((vu32 *)0x1000f520)
#define R_D_ENABLEW ((vu32 *)0x1000f590)
#define R_D_CTRL    ((vu32 *)0x1000e000)
#define R_D_STAT    ((vu32 *)0x1000e010)
#define R_GS_CSR    ((vu64 *)0x12001000)
#define R_I_MASK    ((vu32 *)0x1000f010)
static vu32 *const dma_chcr[] = {(vu32 *)0x10008000, (vu32 *)0x10009000, (vu32 *)0x1000a000, (vu32 *)0x1000b000,
                                 (vu32 *)0x1000b400, (vu32 *)0x1000d000, (vu32 *)0x1000d400}; // not SIF 5-7

// CDVD registers, as OPL padhook.h
#define CDVD_R_NDIN ((volatile u8 *)0xBF402005)
#define CDVD_R_POFF ((volatile u8 *)0xBF402008)
#define CDVD_R_SCMD ((volatile u8 *)0xBF402016)
#define CDVD_R_SDIN ((volatile u8 *)0xBF402017) // write: S-command parameter; read: S-command status
#define CDVD_R_SDOUT ((volatile u8 *)0xBF402018) // S-command result
#define CDVD_S_BUSY 0x80
#define CDVD_S_NODATA 0x40

// Return progress, shown as the screen colour (the GS is reset, so its background is all that is displayed):
// blue thread awake, magenta RPC up, green IOP reset sent, yellow IOP rebooted, white LoadExecPS2 of the exit ELF,
// red LoadExecPS2 returned (then the browser)
#define STAGE(c) (*GS_REG_PMODE = 0, *GS_REG_BGCOLOR = (c))

typedef struct
{
    u32 option;
    int port;
    int slot;
    int number;
    u8 name[16];
} pad2socketparam_t;

typedef struct
{
    u32 *pattern;
    u32 *mask;
    int size;
    u16 type;
    u16 version;
} pattern_t;

#define IGR_LIBPAD  1
#define IGR_LIBPAD2 2
#define IGR_PAD_STABLE_V1 0x06
#define IGR_PAD_STABLE_V2 0x01
#define NB_PADOPEN_PATTERN 7

enum { IGR_NONE = 0, IGR_EXIT, IGR_POWEROFF, IGR_MENU };

static int (*scePadPortOpen)(int port, int slot, void *addr);
static int (*scePad2CreateSocket)(pad2socketparam_t *SocketParam, void *addr);

static struct
{
    u16 libpad, libversion;
    u8 *pad_buf;
    int vb_count, pos_buttons, pos_state, pos_frame; // buttons: 2 bytes, low byte first (libpad's active-low data)
    int action;
    u8 prev_frame;
} pad;

static struct
{
    int press, vb_count;
} power_button;

int padOpen_hooked = 0;
static int IGR_Thread_ID = -1, IGR_Intc_ID = -1;
// The thread's stack is ee_core's own main stack (linkfile stack84, 0x94000-0x95000): ee_core only runs on it before
// the game starts and when the game LoadExecPS2s, which deletes this thread anyway. The 64 KB ee_core region needs
// the room for the menu's saved pixels
#define IGR_STACK_SIZE (4 * 1024)
#define IGR_Stack ((u8 *)0x00094000)
extern void *_gp;

int IGR_Enabled(void) { return eec.IgrExitCombo || eec.IgrOffCombo || eec.IgrMenuCombo; }

static int pad_stable(const u8 *b)
{
    u8 state = b[pad.pos_state];
    return (pad.libpad == IGR_LIBPAD && state == IGR_PAD_STABLE_V1) || (pad.libpad == IGR_LIBPAD2 && state == IGR_PAD_STABLE_V2);
}

u16 IGR_Buttons(void)
{
    const u8 *b = (const u8 *)UNCACHED_SEG(pad.pad_buf); // the IOP's padman fills it over SIF DMA, behind the cache
    if (pad.pad_buf == NULL || !pad_stable(b))
        return 0;
    return ~(b[pad.pos_buttons] | b[pad.pos_buttons + 1] << 8) & 0xFFFF;
}

// ---- Pause for the in-game menu (phase 13): the game's threads stop and its interrupts are held, nothing is reset.
// The SIF stays up (SBUS, DMA channels 5-7), so padman keeps filling the pad buffer the menu reads ----
static volatile int menu_open;
static int menu_rearmed = 1;
static u32 paused_threads[8], intc_mask, dmac_mask; // what the pause took away, to give it back
static const u8 dmac_held[] = {0, 1, 2, 3, 4, 8, 9}; // not SIF 5-7

static void hold_interrupts(void) // interrupt handler context
{
    int i;
    intc_mask = *R_I_MASK;
    for (i = 0; i <= 14; i++)
        if (i != INTC_SBUS && (intc_mask & (1 << i)))
            iDisableIntc(i);
    dmac_mask = (*R_D_STAT >> 16) & 0x3ff;
    for (i = 0; i < (int)sizeof(dmac_held); i++)
        if (dmac_mask & (1 << dmac_held[i]))
            iDisableDmac(dmac_held[i]);
}

// From the IGR thread, at priority 0, when no game thread is running. Suspending the thread the VBLANK interrupted
// from the handler (iSuspendThread) left it READY but never scheduled again after ResumeThread (PCSX2: Black's main
// thread, ResumeThread -1); OPL does that too, but it never resumes
static void suspend_threads(void)
{
    int i;
    for (i = 1; i < 256; i++) {
        paused_threads[i / 32] &= ~(1 << (i % 32));
        if (i != IGR_Thread_ID && SuspendThread(i) >= 0)
            paused_threads[i / 32] |= 1 << (i % 32);
    }
}

static void game_resume(void) // the IGR thread, in reverse order
{
    int i;
    for (i = 0; i < (int)sizeof(dmac_held); i++)
        if (dmac_mask & (1 << dmac_held[i]))
            EnableDmac(dmac_held[i]);
    for (i = 0; i <= 14; i++)
        if (i != INTC_SBUS && (intc_mask & (1 << i)))
            EnableIntc(i);
    for (i = 1; i < 256; i++)
        if (paused_threads[i / 32] & (1 << (i % 32)))
            ResumeThread(i);
}

static void iResetEE(u32 init_bitfield) // ResetEE from an interrupt handler: syscall -1 (OPL asm.S)
{
    __asm__ __volatile__("move $a0, %0\n li $v1, -1\n syscall\n nop\n" ::"r"(init_bitfield) : "$a0", "$v1", "memory");
}

// Called from the interrupt handler, so it needs neither the SIF nor the IGR thread. S-command 0x0F with no
// parameter, as cdvdman's sceCdPowerOff (a stray parameter byte makes the mechacon reject it)
static void power_off(void)
{
    ee_kmode_enter();
    while (*CDVD_R_SDIN & CDVD_S_BUSY) // the IOP's cdvdman may be mid-command
        ;
    while (!(*CDVD_R_SDIN & CDVD_S_NODATA)) // drop a previous command's unread result
        (void)*CDVD_R_SDOUT;
    *CDVD_R_SCMD = 0x0F;
    for (;;)
        ;
}

// Undo the two kernel patches of Neutrino's loader (ee/loader/src/patch.c). Left in place, the LoadExecPS2 of
// OSDSYS / FMCB would start ee_core again instead of EELOAD, and the user memory clear would start at our modules
static void kernel_unpatch(void)
{
    u32 *p;
    DI();
    ee_kmode_enter();
    for (p = (u32 *)0x80001000; p < (u32 *)0x80030000; p++) // sbvpp_replace_eeload: lui s2 / ori s2 / li a3, 0
        if (p[0] == 0x8FA30010 && (p[1] >> 16) == 0x3C12 && (p[2] >> 16) == 0x3652 && p[3] == 0x24070000 &&
            p[4] == 0x18E00009) {
            p[1] = 0x0240302D; // daddu a2, s2, zero
            p[2] = 0x8FA50014; // lw    a1, 0x0014(sp)
            p[3] = 0x8C67000C; // lw    a3, 0x000C(v1)
            break;
        }
    for (p = (u32 *)0x80001000; p < (u32 *)0x80080000; p++) // sbvpp_patch_user_mem_clear: a0 = ModStorageEnd
        if ((p[0] >> 16) == 0x3C04 && (p[1] & 0xFC000000) == 0x0C000000 && (p[2] >> 16) == 0x3484 &&
            (p[0] << 16 | (p[2] & 0xFFFF)) == (u32)eec.ModStorageEnd) {
            p[0] = 0x3C040008; // lui a0, 0x0008
            p[2] = 0x34842000; // ori a0, a0, 0x2000
            break;
        }
    ee_kmode_exit();
    EI();
}


// Before the reboot, from the handler or the menu thread: stop every DMA but SIF (5, 6, 7) and reset the GS. Then
// (i)ResetEE(0x7E): OPL uses 0x7F, but bit 0 resets the whole DMAC, SIF included; a SIF transfer in flight then
// leaves the kernel's SIF queue stuck and the IOP reset never goes out
static void stop_dma_and_gs(void)
{
    int i;
    asm volatile("sync.l\n");
    u32 en = *R_D_ENABLER;
    *R_D_ENABLEW = en | 0x10000;
    (void)*R_D_CTRL;
    (void)*R_D_STAT;
    for (i = 0; i < (int)(sizeof(dma_chcr) / sizeof(dma_chcr[0])); i++)
        *dma_chcr[i] = 0;
    *R_D_ENABLEW = en;
    asm volatile("sync.l\n");
    *R_GS_CSR = 0x100;
    asm volatile("sync.l\n");
    while (*R_GS_CSR & 0x100)
        ;
}

static void IGR_Thread(void *arg)
{
    (void)arg;
    for (;;) { // woken by the interrupt handler: the menu (game paused), or the reboot combo
        SleepThread();
        if (!menu_open)
            break;
        suspend_threads();
        int item = Menu_Run();
        if (item == MENU_OFF)
            power_off();
        if (item == MENU_REBOOT) { // as the reboot combo: the game stays paused, its interrupts held
            stop_dma_and_gs();
            ResetEE(0x7E);
            break;
        }
        game_resume();
        menu_open = 0;
        ChangeThreadPriority(IGR_Thread_ID, 127);
    }
    STAGE(COLOR_BLUE);

    SifInitRpc(0);
    STAGE(COLOR_MAGENTA);
    Remove_Kernel_Hooks(); // our SifSetDma hook must not catch this reset
    DisableGSM(); // also its capture-only mode, armed for the menu

    if (eec.CheatList != NULL)
        DisableCheats();
    while (!SifIopReset("", 0))
        ;
    STAGE(COLOR_GREEN);
    InitTLB(); // some games change the memory map (OPL: GT4, GTA)

    u32 perf; // stop the performance counters some games start (GT4): their overflow raises an exception
    __asm__ __volatile__("mfc0 %0, $25" : "=r"(perf));
    if (perf & 0x80000000)
        __asm__ __volatile__("mfc0 $3, $25\n lui $2, 0x8000\n or $3, $3, $2\n xor $3, $3, $2\n mtc0 $3, $25\n sync.p" ::: "$2", "$3");

    while (!SifIopSync())
        ;
    STAGE(COLOR_YELLOW);
    // Finish the SIF handshake with the new IOP (EELOAD cannot use it otherwise), and stop the game's SPU2 DMA (the
    // looping sound): left running, it hangs the BIOS's CLEARSPU when OSDSYS starts. Both as OPL
    services_start();
    sbv_patch_enable_lmb();
    SifExecModuleBuffer(resetspu_irx, size_resetspu_irx, 0, NULL, NULL);
    services_exit();
    // Leave as any program does: LoadExecPS2 drops the game's threads and handlers, and the BIOS's EELOAD loads the
    // exit ELF (rom0:OSDSYS: a reboot, FMCB then autoboots ORBIT). OSDSYS hangs when started by a bare ExecPS2
    kernel_unpatch();
    FlushCache(0);
    FlushCache(2); // the kernel code just changed
    STAGE(COLOR_WHITE);
    LoadExecPS2(eec.IgrExitPath, 0, NULL);
    STAGE(COLOR_RED);
    Exit(0);
}

static int IGR_Intc_Handler(int cause)
{
    static int fired;
    (void)cause;
    if (fired) { // the return is under way: running again would reset the DMAC under the thread's SIF transfers
        ExitHandler();
        return 0;
    }
    GSM_Rearm(); // capture-only mode: an access the breakpoint could not emulate switched it off
    if (pad.pad_buf != NULL) {
        u8 *b = (u8 *)UNCACHED_SEG(pad.pad_buf); // bypass the cache
        u8 frame = b[pad.pos_frame];
        u16 pressed = ~(b[pad.pos_buttons] | b[pad.pos_buttons + 1] << 8) & 0xFFFF;

        if (pad_stable(b)) {
            // the frame counter moving means the buffer is alive; if it stops, ask for the hook again
            if (pad.vb_count++ >= 10) {
                padOpen_hooked = frame != pad.prev_frame;
                pad.prev_frame = frame;
                pad.vb_count = 0;
            }
            if (eec.IgrMenuCombo && pressed == eec.IgrMenuCombo) { // exact: no other button held
                if (menu_rearmed) // once per press: a menu that closes at once must not open again
                    pad.action = IGR_MENU, menu_rearmed = 0;
            } else if (eec.IgrExitCombo && pressed == eec.IgrExitCombo)
                pad.action = IGR_EXIT;
            else if (eec.IgrOffCombo && pressed == eec.IgrOffCombo)
                pad.action = IGR_POWEROFF;
            if (pressed != eec.IgrMenuCombo)
                menu_rearmed = 1;
        }
    }

    ee_kmode_enter();
    if ((*CDVD_R_NDIN & 0x20) && (*CDVD_R_POFF & 0x04)) { // power button: once = power off, twice = exit (OPL)
        power_button.press++;
        *CDVD_R_SDIN = 0x00;
        *CDVD_R_SCMD = 0x1B; // cancel the power off to catch a second press
    }
    if (power_button.press && power_button.vb_count++ >= 50)
        pad.action = power_button.press == 1 ? IGR_POWEROFF : IGR_EXIT;
    ee_kmode_exit();

    if (pad.action == IGR_POWEROFF)
        power_off();

    if (pad.action == IGR_MENU) {
        pad.action = IGR_NONE;
        if (!menu_open) {
            menu_open = 1;
            hold_interrupts(); // this handler's VBLANK too: the menu thread reads the pad itself
            iChangeThreadPriority(IGR_Thread_ID, 0);
            iWakeupThread(IGR_Thread_ID);
        }
    }

    if (pad.action != IGR_NONE) {
        int i;
        fired = 1;
        for (i = 0; i <= 14; i++) // silence the game's interrupts (GS, VBLANK, timers...); SBUS stays for the SIF
            if (i != INTC_SBUS)
                iDisableIntc(i);
        stop_dma_and_gs();
        iResetEE(0x7E);
        for (i = 1; i < 256; i++)
            if (i != IGR_Thread_ID)
                iSuspendThread(i);
        iChangeThreadPriority(IGR_Thread_ID, 0);
        iWakeupThread(IGR_Thread_ID);
    }
    ExitHandler();
    return 0;
}

static void Set_libpad_Params(void *addr)
{
    DI();
    pad.pad_buf = addr;
    if (pad.libpad == IGR_LIBPAD) {
        if (pad.libversion >= 0x0160)
            pad.pos_buttons = 2, pad.pos_state = 112, pad.pos_frame = 88;
        else
            pad.pos_buttons = 10, pad.pos_state = 4, pad.pos_frame = 0;
    } else if (pad.libpad == IGR_LIBPAD2)
        pad.pos_buttons = 28, pad.pos_state = 4, pad.pos_frame = 124;
    EI();
}

static void Install_IGR(void)
{
    ee_thread_t t;
    power_button.press = power_button.vb_count = 0;
    pad.pad_buf = NULL;
    pad.vb_count = 0;
    pad.action = IGR_NONE;
    pad.prev_frame = 0;
    if (IGR_Thread_ID < 0) {
        t.gp_reg = &_gp;
        t.func = IGR_Thread;
        t.stack = (void *)IGR_Stack;
        t.stack_size = IGR_STACK_SIZE;
        t.initial_priority = 127;
        IGR_Thread_ID = CreateThread(&t);
        StartThread(IGR_Thread_ID, NULL);
    }
    if (IGR_Intc_ID < 0) {
        IGR_Intc_ID = AddIntcHandler(INTC_VBLANK_E, IGR_Intc_Handler, 0);
        EnableIntc(INTC_VBLANK_E);
    }
}

static int Hook_scePadPortOpen(int port, int slot, void *addr)
{
    int ret;
    if (port == 0 && slot == 0)
        Install_PadOpen_Hook(0x00100000, 0x01ff0000, PADOPEN_CHECK);
    ret = scePadPortOpen(port, slot, addr);
    if (port == 0 && slot == 0) {
        Install_IGR();
        Set_libpad_Params(addr);
    }
    return ret;
}

static int Hook_scePad2CreateSocket(pad2socketparam_t *SocketParam, void *addr)
{
    int ret;
    if (SocketParam == NULL || (SocketParam->port == 0 && SocketParam->slot == 0))
        Install_PadOpen_Hook(0x00100000, 0x01ff0000, PADOPEN_CHECK);
    ret = scePad2CreateSocket(SocketParam, addr);
    if (SocketParam == NULL || (SocketParam->port == 0 && SocketParam->slot == 0)) {
        Install_IGR();
        Set_libpad_Params(addr);
    }
    return ret;
}

// Find the game's libpad open function and redirect its callers (J/JAL, else stored pointers) to our hook
int Install_PadOpen_Hook(u32 mem_start, u32 mem_end, int mode)
{
    u32 *ptr, *ptr2, inst, pattern[1], mask[1];
    int i, found = 0, patched = 0;
    pattern_t pats[NB_PADOPEN_PATTERN] = {
        {padPortOpenpattern0, padPortOpenpattern0_mask, sizeof(padPortOpenpattern0), IGR_LIBPAD, 0x0211},
        {pad2CreateSocketpattern0, pad2CreateSocketpattern0_mask, sizeof(pad2CreateSocketpattern0), IGR_LIBPAD2, 0x0200},
        {pad2CreateSocketpattern1, pad2CreateSocketpattern1_mask, sizeof(pad2CreateSocketpattern1), IGR_LIBPAD2, 0x0200},
        {pad2CreateSocketpattern2, pad2CreateSocketpattern2_mask, sizeof(pad2CreateSocketpattern2), IGR_LIBPAD2, 0x0200},
        {padPortOpenpattern1, padPortOpenpattern1_mask, sizeof(padPortOpenpattern1), IGR_LIBPAD, 0x0210},
        {padPortOpenpattern2, padPortOpenpattern2_mask, sizeof(padPortOpenpattern2), IGR_LIBPAD, 0x0160},
        {padPortOpenpattern3, padPortOpenpattern3_mask, sizeof(padPortOpenpattern3), IGR_LIBPAD, 0x0150}};

    if (!IGR_Enabled())
        return 0;
    for (i = 0; i < NB_PADOPEN_PATTERN; i++) {
        ptr = (u32 *)mem_start;
        while (ptr) {
            ptr = find_pattern_with_mask(ptr, mem_end - (u32)ptr, pats[i].pattern, pats[i].mask, pats[i].size);
            if (!ptr)
                break;
            found = 1;
            if (pats[i].type == IGR_LIBPAD)
                scePadPortOpen = (void *)ptr;
            else
                scePad2CreateSocket = (void *)ptr;
            if (mode != PADOPEN_HOOK)
                break;
            u32 hook = pats[i].type == IGR_LIBPAD ? (u32)Hook_scePadPortOpen : (u32)Hook_scePad2CreateSocket;
            pattern[0] = 0x08000000 | (0x03ffffff & ((u32)ptr >> 2)); // J or JAL to it (bit 26 masked)
            mask[0] = 0xfbffffff;
            for (ptr2 = (u32 *)mem_start; (ptr2 = find_pattern_with_mask(ptr2, mem_end - (u32)ptr2, pattern, mask, sizeof(pattern)));) {
                inst = (ptr2[0] & 0xfc000000) | (0x03ffffff & (hook >> 2));
                _sw(inst, (u32)ptr2);
                patched = 1;
                pad.libpad = pats[i].type, pad.libversion = pats[i].version;
            }
            if (!patched) { // called through a pointer (JALR): replace the stored address
                pattern[0] = (u32)ptr;
                mask[0] = 0xffffffff;
                for (ptr2 = (u32 *)mem_start; (ptr2 = find_pattern_with_mask(ptr2, mem_end - (u32)ptr2, pattern, mask, sizeof(pattern)));) {
                    _sw(hook, (u32)ptr2);
                    patched = 1;
                    pad.libpad = pats[i].type, pad.libversion = pats[i].version;
                }
            }
            ptr += pats[i].size >> 2;
        }
        if (patched || (mode == PADOPEN_CHECK && found))
            break;
    }
    return patched;
}
