# MOVOVL.EXE, the movie player overlay. Standalone: `make -C movovl` builds it
# and checks its SHA1 (movovl.sha1). Nothing here is part of the main
# executable's build; the compiler and maspsx are the main build's.

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DELETE_ON_ERROR:

CC1     := ../tools/gcc-2.7.2/build/cc1
MASPSX  := python3 ../tools/maspsx/maspsx.py
CPP     := mipsel-linux-gnu-cpp
AS      := mipsel-linux-gnu-as
LD      := mipsel-linux-gnu-ld
OBJCOPY := mipsel-linux-gnu-objcopy

# The game code was compiled with a small-data threshold of 8 (-G8); the Sony
# libraries were not. -mel and -msoft-float as in the main build.
CPP_FLAGS    := -Iinclude -I../include -nostdinc -undef -Wall -lang-c -fno-builtin 	-Dmips -D__GNUC__=2 -D__OPTIMIZE__ -D__mips__ -D__mips -Dpsx -D__psx__ -D__psx
CC_FLAGS     := -O2 -G8 -funsigned-char -quiet -mcpu=3000 -mips1 -mno-abicalls -fno-builtin -mel -msoft-float -Wimplicit -Werror
MASPSX_FLAGS := --expand-div --aspsx-version=2.34 --comm-own-sections -G8
AS_FLAGS     := -Iinclude -march=r3000 -mtune=r3000 -no-pad-sections -O1 -G0

# The Sony library modules that are also in the main executable are built from
# the main build's source (../src/main/psxsdk/), with the main build's flags
# for library code: no small data, each uninitialised variable in a section of
# its own so that movovl.ld can place it.
SDK_DIR          := ../src/main/psxsdk
SDK_CPP_FLAGS    := -I../include -nostdinc -undef -Wall -lang-c -fno-builtin 	-Dmips -D__GNUC__=2 -D__OPTIMIZE__ -D__mips__ -D__mips -Dpsx -D__psx__ -D__psx 	-D_PSYQ -D__EXTENSIONS__ -D_MIPSEL -D_LANGUAGE_C -DLANGUAGE_C
SDK_CC_FLAGS     := -O2 -G0 -funsigned-char -quiet -mcpu=3000 -mips1 -mno-abicalls -fno-builtin -mel -msoft-float -Wimplicit -Werror
SDK_MASPSX_FLAGS := --expand-div --aspsx-version=2.34 --expand-lb --expand-lb-funcs=../config/expand_lb_funcs.txt 	--expand-dest-funcs=../config/expand_dest_funcs.txt --prefill-label-funcs=../config/maspsx_prefill_label_funcs.txt --comm-own-sections
# -I..: the modules that are assembly include their listings as asm/funcs/<name>.s
SDK_AS_FLAGS     := -I.. -I../include -march=r3000 -mtune=r3000 -no-pad-sections -O1 -G0
SDK_MODULES := 	libapi/a07 libapi/a18 libapi/a19 libapi/a20 libapi/a21 libapi/a23 libapi/a24 libapi/a25 	libapi/a36 libapi/a37 libapi/a53 libapi/a91 libapi/c114 libapi/c68 libapi/c73 libapi/chclrpad 	libapi/l02 libapi/l03 libapi/l10 libapi/pad libapi/patch libapi/sendpad 	libc2/ctype libc2/memchr libc2/memcpy libc2/printf libc2/prnt libc2/putchar libc2/puts 	libc2/setjmp libc2/strlen 	libcd/bios libcd/cdread libcd/event libcd/sys 	libetc/intr libetc/intr_dma libetc/intr_vb libetc/vmode libetc/vsync 	libgpu/ext libgpu/prim libgpu/sys

# The compiled library modules only this program has (sdk/), built the same way.
SDK_OWN_MODULES := 	libcd/cdrom libcd/cdread2 libcd/c_002 libcd/c_003 libcd/c_004 libcd/c_005 libcd/c_007 	libcd/c_008 libcd/c_009 libcd/c_010 libcd/c_011 libetc/pad libds/dscb libpress/libpress

C_SRC := $(wildcard src/*.c)
ASM_S := $(wildcard asm/*.s) $(wildcard asm/data/*.s)
OBJS  := $(patsubst src/%.c,build/src/%.o,$(C_SRC)) $(patsubst asm/%.s,build/asm/%.o,$(ASM_S)) 	$(patsubst %,build/sdk/%.o,$(SDK_MODULES) $(SDK_OWN_MODULES))

all: check

build/src/%.o: src/%.c Makefile $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(CPP) $(CPP_FLAGS) $< | $(CC1) $(CC_FLAGS) | $(MASPSX) $(MASPSX_FLAGS) | $(AS) $(AS_FLAGS) -o $@

build/sdk/%.o: $(SDK_DIR)/%.c Makefile $(wildcard ../include/*.h ../include/psxsdk/*.h $(SDK_DIR)/*/*.h ../asm/funcs/*.s)
	@mkdir -p $(dir $@)
	$(CPP) $(SDK_CPP_FLAGS) $< | $(CC1) $(SDK_CC_FLAGS) | $(MASPSX) $(SDK_MASPSX_FLAGS) | $(AS) $(SDK_AS_FLAGS) -o $@
	$(OBJCOPY) --set-section-alignment .rodata=4 $@

build/sdk/%.o: sdk/%.c Makefile $(wildcard include/*.h ../include/*.h ../include/psxsdk/*.h sdk/*/*.h asm/funcs/*.s)
	@mkdir -p $(dir $@)
	$(CPP) -Iinclude $(SDK_CPP_FLAGS) $< | $(CC1) $(SDK_CC_FLAGS) | $(MASPSX) $(SDK_MASPSX_FLAGS) | $(AS) $(SDK_AS_FLAGS) -o $@
	$(OBJCOPY) --set-section-alignment .rodata=4 $@

build/asm/%.o: asm/%.s
	@mkdir -p $(dir $@)
	$(AS) $(AS_FLAGS) $< -o $@

build/movovl.elf: $(OBJS) movovl.ld undefined_syms.txt
	$(LD) -nostdlib --no-check-sections -Map build/movovl.map 		-T movovl.ld -T undefined_syms.txt -o $@

build/MOVOVL.EXE: build/movovl.elf
	$(OBJCOPY) -O binary $< $@

check: build/MOVOVL.EXE
	@cd build && sha1sum -c ../movovl.sha1 || { echo "MISMATCH: MOVOVL.EXE does not match"; exit 1; }
	@echo "OK: movovl matches!"

clean:
	rm -rf build

.PHONY: all check clean
