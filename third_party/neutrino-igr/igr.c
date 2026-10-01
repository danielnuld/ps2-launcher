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
  eec.IgrOffCombo) instead of fixed L1+L2+R1+R2 + START+SELECT / L3+R3; exit loads eec.IgrExitPath from the
  memory card after an IOP reset to the ROM modules; power off sends CDVD S-command 0x0F (the one cdvdman's
  sceCdPowerOff sends) from the EE; no IGS screenshot, no SPU reset module, no debug colours.
*/

#include <kernel.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <sbv_patches.h>
#include "ee_debug.h"
#include "util.h"
#include "eecore_config.h"
#include "cheat_api.h"
#include "gsm_api.h"
#include "igr.h"
#include "padpatterns.h"

void DisableGSM(void); // gsm_api.c
void Remove_Kernel_Hooks(void); // iopmgr.c

// EE registers (the local ee_regs.h shadows ps2sdk's): DMA enable, control, channel CHCRs, GS CSR
#define R_D_ENABLER ((vu32 *)0x1000f520)
#define R_D_ENABLEW ((vu32 *)0x1000f590)
#define R_D_CTRL    ((vu32 *)0x1000e000)
#define R_D_STAT    ((vu32 *)0x1000e010)
#define R_GS_CSR    ((vu64 *)0x12001000)
static vu32 *const dma_chcr[] = {(vu32 *)0x10008000, (vu32 *)0x10009000, (vu32 *)0x1000a000, (vu32 *)0x1000b000,
                                 (vu32 *)0x1000b400, (vu32 *)0x1000d000, (vu32 *)0x1000d400}; // not SIF 5-7

// CDVD registers, as OPL padhook.h
#define CDVD_R_NDIN ((volatile u8 *)0xBF402005)
#define CDVD_R_POFF ((volatile u8 *)0xBF402008)
#define CDVD_R_SCMD ((volatile u8 *)0xBF402016)
#define CDVD_R_SDIN ((volatile u8 *)0xBF402017)

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

enum { IGR_NONE = 0, IGR_EXIT, IGR_POWEROFF };

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
#define IGR_STACK_SIZE (4 * 1024)
static u8 IGR_Stack[IGR_STACK_SIZE] __attribute__((aligned(16)));
extern void *_gp;
extern void *_end;

int IGR_Enabled(void) { return eec.IgrExitCombo || eec.IgrOffCombo; }

static void iResetEE(u32 init_bitfield) // ResetEE from an interrupt handler: syscall -1 (OPL asm.S)
{
    __asm__ __volatile__("move $a0, %0\n li $v1, -1\n syscall\n nop\n" ::"r"(init_bitfield) : "$a0", "$v1", "memory");
}

static void power_off(void)
{
    ee_kmode_enter();
    *CDVD_R_SDIN = 0x00;
    *CDVD_R_SCMD = 0x0F; // S-command 0x0F: power off (sceCdPowerOff in cdvdman)
    ee_kmode_exit();
    for (;;)
        ;
}

// Back to the frontend: ROM modules only (the USB is gone), so the exit ELF lives on a memory card
static void exit_to_frontend(void)
{
    t_ExecData elf;
    char *argv[1] = {eec.IgrExitPath};

    SifInitRpc(0);
    sbv_patch_disable_prefix_check();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    WipeUserMemory((void *)&_end, (void *)GetMemorySize());
    FlushCache(0);
    if (eec.IgrExitPath[0] && SifLoadElf(argv[0], &elf) == 0) {
        SifLoadFileExit();
        SifExitRpc();
        FlushCache(0);
        FlushCache(2);
        ExecPS2((void *)elf.epc, (void *)elf.gp, 1, argv);
    }
    Exit(0); // no exit ELF: the browser
}

static void IGR_Thread(void *arg)
{
    (void)arg;
    SleepThread(); // woken by the interrupt handler

    SifInitRpc(0);
    if (pad.action == IGR_POWEROFF)
        power_off();

    Remove_Kernel_Hooks(); // our SifSetDma hook must not catch this reset
    if (eec.GsmVideoMode != EECORE_GSM_VMODE_NONE)
        DisableGSM();
    if (eec.CheatList != NULL)
        DisableCheats();
    while (!SifIopReset("", 0))
        ;
    InitTLB(); // some games change the memory map (OPL: GT4, GTA)

    u32 perf; // stop the performance counters some games start (GT4): their overflow raises an exception
    __asm__ __volatile__("mfc0 %0, $25" : "=r"(perf));
    if (perf & 0x80000000)
        __asm__ __volatile__("mfc0 $3, $25\n lui $2, 0x8000\n or $3, $3, $2\n xor $3, $3, $2\n mtc0 $3, $25\n sync.p" ::: "$2", "$3");

    while (!SifIopSync())
        ;
    exit_to_frontend();
}

static int IGR_Intc_Handler(int cause)
{
    (void)cause;
    if (pad.pad_buf != NULL) {
        u8 *b = (u8 *)UNCACHED_SEG(pad.pad_buf); // bypass the cache
        u8 state = b[pad.pos_state], frame = b[pad.pos_frame];
        u16 pressed = ~(b[pad.pos_buttons] | b[pad.pos_buttons + 1] << 8) & 0xFFFF;

        if ((pad.libpad == IGR_LIBPAD && state == IGR_PAD_STABLE_V1) || (pad.libpad == IGR_LIBPAD2 && state == IGR_PAD_STABLE_V2)) {
            // the frame counter moving means the buffer is alive; if it stops, ask for the hook again
            if (pad.vb_count++ >= 10) {
                padOpen_hooked = frame != pad.prev_frame;
                pad.prev_frame = frame;
                pad.vb_count = 0;
            }
            if (eec.IgrExitCombo && pressed == eec.IgrExitCombo) // exact: no other button held
                pad.action = IGR_EXIT;
            else if (eec.IgrOffCombo && pressed == eec.IgrOffCombo)
                pad.action = IGR_POWEROFF;
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

    if (pad.action != IGR_NONE) {
        int i;
        asm volatile("sync.l\n");
        u32 en = *R_D_ENABLER; // stop every DMA but SIF (5, 6, 7)
        *R_D_ENABLEW = en | 0x10000;
        (void)*R_D_CTRL;
        (void)*R_D_STAT;
        for (i = 0; i < (int)(sizeof(dma_chcr) / sizeof(dma_chcr[0])); i++)
            *dma_chcr[i] = 0;
        *R_D_ENABLEW = en;
        asm volatile("sync.l\n");
        *R_GS_CSR = 0x100; // reset the GS
        asm volatile("sync.l\n");
        while (*R_GS_CSR & 0x100)
            ;
        iResetEE(0x7F);
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
