EE_BIN = bench.elf
IRX_FILES = iomanX fileXio bdm bdmfs_fatfs usbd_mini usbmass_bd_mini
EE_OBJS = bench.o $(IRX_FILES:=_irx.o)
EE_LIBS = -lpacket -ldma -lgraph -ldraw -lfileXio -lpatches -lc

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

test:
	cc -std=c99 -Wall -DSELFTEST bench.c -o /tmp/bench_selftest && /tmp/bench_selftest

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
