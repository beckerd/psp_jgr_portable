# Kernel helper: no newlib/crt0 startup, which calls ForUser imports.
PSPSDK := $(shell psp-config --pspsdk-path)
CFLAGS := -O2 -G0 -Wall -std=gnu99 -fno-builtin -I$(PSPSDK)/include -D_PSP_FW_VERSION=660
LDFLAGS := -specs=$(PSPSDK)/lib/prxspecs -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -Wl,-zmax-page-size=128 -nostdlib -nostartfiles
LIBS := -lpspmodinfo -lpspkernel -lgcc
all: jgr_me.prx
main.o: main.c power.inc ../../src/me_shared.h ../../src/me_protocol.h
	psp-gcc $(CFLAGS) -c $< -o $@
me_stub.o: me_stub.S
	psp-gcc $(CFLAGS) -c $< -o $@
jgr_me.elf: main.o me_stub.o
	psp-gcc $(LDFLAGS) $(PSPSDK)/lib/prxexports.o $^ -L$(PSPSDK)/lib $(LIBS) -o $@
	psp-fixup-imports $@
jgr_me.prx: jgr_me.elf
	psp-prxgen $< $@
clean:
	rm -f main.o me_stub.o jgr_me.elf jgr_me.prx
