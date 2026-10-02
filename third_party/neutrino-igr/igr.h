/*
  igr.h - In Game Reset for Neutrino (ORBIT launcher fork)

  Ported from Open PS2 Loader ee_core padhook.h:
  Copyright 2009-2010, Ifcaro, jimmikaelkael & Polo
  Copyright 2006-2008 Polo
  Licenced under Academic Free License version 3.0
*/

#ifndef IGR_H
#define IGR_H

#include <tamtypes.h>

#define PADOPEN_HOOK  0
#define PADOPEN_CHECK 1

extern int padOpen_hooked;

int Install_PadOpen_Hook(u32 mem_start, u32 mem_end, int mode);
int IGR_Enabled(void);
u16 IGR_Buttons(void); // buttons held now (PAD_* bits), 0 while the pad is not stable
void Menu_Run(void);   // menu.c: runs over the paused game, returns when the game should go on

#endif
