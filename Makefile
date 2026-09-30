# make            -> bench.elf
# make APP=modetest -> modetest.elf
APP ?= bench
EE_BIN = $(APP).elf
IRX_FILES = iomanX fileXio sio2man freepad bdm bdmfs_fatfs usbd_mini usbmass_bd_mini
EE_OBJS = $(APP).o iop.o $(IRX_FILES:=_irx.o)
EE_LIBS = -lfont -lpacket -ldma -lgraph -ldraw -lpad -lfileXio -lpatches -lc

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

test:
	cc -std=c99 -Wall -DSELFTEST bench.c -o /tmp/bench_selftest && /tmp/bench_selftest

clean:
	rm -f *.elf *.o *_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
