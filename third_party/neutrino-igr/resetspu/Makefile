# resetspu.irx: Open PS2 Loader modules/iopcore/resetspu (AFL-3.0), unchanged. Run by the IGR after the IOP reset:
# stops the game's SPU2 DMA, which otherwise hangs the BIOS's CLEARSPU when OSDSYS starts
IOP_BIN = resetspu.irx
IOP_OBJS = resetspu.o imports.o

all: $(IOP_BIN)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
