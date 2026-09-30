EE_BIN = bench.elf
EE_OBJS = bench.o
EE_LIBS = -lpacket -ldma -lgraph -ldraw -lc

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

test:
	cc -std=c99 -Wall -DSELFTEST bench.c -o /tmp/bench_selftest && /tmp/bench_selftest

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
