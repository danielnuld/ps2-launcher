#pragma once
int iop_init(void);        // loads USB + pad modules; returns 1 if mass0: is mounted
unsigned pad_buttons(void); // PAD_* bits currently held on port 0
