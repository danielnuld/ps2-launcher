# loader.elf: second stage that starts Neutrino without an IOP reset (loader.c). Built by the main Makefile.
EE_BIN = loader.elf
EE_OBJS = loader.o
EE_LINKFILE = linkfile
EE_CFLAGS += -Os -fdata-sections -ffunction-sections
EE_LDFLAGS += -s -Wl,--gc-sections

all: $(EE_BIN)

clean:
	rm -f *.o *.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
