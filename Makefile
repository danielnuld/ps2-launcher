# make            -> bench.elf
# make APP=modetest -> modetest.elf
# make APP=launcher -> launcher.elf (ORBIT: gfx.c + generated font_data.c, ui_data.c)
APP ?= bench
SFX = splash move edge confirm panel # sfx/*.wav from tools/sfx.py -> SPU2 ADPCM (ps2sdk adpenc)
EE_BIN = $(APP).elf
IRX_FILES = iomanX fileXio sio2man mcman mcserv freepad libsd audsrv bdm bdmfs_fatfs usbd_mini usbmass_bd_mini
EE_OBJS = $(APP).o iop.o $(if $(filter launcher,$(APP)),gfx.o iso.o ini.o font_data.o ui_data.o loader_elf.o $(SFX:%=sfx_%.o)) $(IRX_FILES:=_irx.o)
EE_LIBS = -laudsrv -lmc -lfont -lpacket -ldma -lgraph -ldraw -lpad -lfileXio -lpatches -lc

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

sfx_%.c: sfx/%.wav
	$(PS2SDK)/bin/adpenc $< sfx_$*.adp && $(PS2SDK)/bin/bin2c sfx_$*.adp $@ sfx_$*

loader/loader.elf: loader/loader.c loader/linkfile
	$(MAKE) -C loader

loader_elf.c: loader/loader.elf
	$(PS2SDK)/bin/bin2c $< $@ loader_elf

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

test:
	cc -std=c99 -Wall -DSELFTEST bench.c -o /tmp/bench_selftest && /tmp/bench_selftest
	cc -std=c99 -Wall -DSELFTEST gfx.c font_data.c -o /tmp/gfx_selftest && /tmp/gfx_selftest
	cc -std=gnu99 -Wall -DSELFTEST iso.c -o /tmp/iso_selftest && /tmp/iso_selftest
	cc -std=gnu99 -Wall -DSELFTEST ini.c -o /tmp/ini_selftest && /tmp/ini_selftest

clean:
	rm -f *.elf *.o *_irx.c sfx_*.c *.adp loader_elf.c
	$(MAKE) -C loader clean

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
