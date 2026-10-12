# Kernel helper: no newlib, crt0, file I/O or user syscall imports.
PSPSDK := $(shell psp-config --pspsdk-path)
CFLAGS := -O2 -g -G0 -Wall -Wextra -Werror -std=gnu99 -fno-builtin -fstack-usage -I$(PSPSDK)/include -D_PSP_FW_VERSION=660
LDFLAGS := -specs=$(PSPSDK)/lib/prxspecs -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -Wl,-zmax-page-size=128 -nostdlib -nostartfiles
LIBS := -lpspmodinfo -lpspkernel -lgcc
all: jgr_crash.prx
main.o: main.c ../../src/crash_shared.h Makefile
	psp-gcc $(CFLAGS) -c $< -o $@
exception.o: exception.S Makefile
	psp-gcc $(CFLAGS) -c $< -o $@
jgr_crash.elf: main.o exception.o
	psp-gcc $(LDFLAGS) $(PSPSDK)/lib/prxexports.o $^ -L$(PSPSDK)/lib $(LIBS) -o $@
	psp-fixup-imports $@
jgr_crash.prx: jgr_crash.elf
	psp-prxgen $< $@
clean:
	rm -f main.o exception.o main.su jgr_crash.elf jgr_crash.prx
