#pragma once
int iop_init(void);        // iop_load + usb_wait; returns 1 if mass0: is mounted
int iop_load(void);        // IOP reset, all modules (USB, pad, memory cards, sound); returns 0 if one failed
int usb_wait(void);        // waits up to 10 s for mass0:; returns 1 if mounted
unsigned pad_buttons(void); // PAD_* bits currently held on port 0
