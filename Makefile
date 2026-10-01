# make            -> bench.elf
# make APP=modetest -> modetest.elf
# make APP=launcher -> launcher.elf (ORBIT: gfx.c + generated font_data.c, ui_data.c)
APP ?= bench
EE_BIN = $(APP).elf
IRX_FILES = iomanX fileXio sio2man mcman mcserv freepad bdm bdmfs_fatfs usbd_mini usbmass_bd_mini
EE_OBJS = $(APP).o iop.o $(if $(filter launcher,$(APP)),gfx.o font_data.o ui_data.o) $(IRX_FILES:=_irx.o)
EE_LIBS = -lmc -lfont -lpacket -ldma -lgraph -ldraw -lpad -lfileXio -lpatches -lc

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

test:
	cc -std=c99 -Wall -DSELFTEST bench.c -o /tmp/bench_selftest && /tmp/bench_selftest
	cc -std=c99 -Wall -DSELFTEST gfx.c font_data.c -o /tmp/gfx_selftest && /tmp/gfx_selftest

clean:
	rm -f *.elf *.o *_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
