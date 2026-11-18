# Bushido Blade 2 (SLUS-00663) Matching Decompilation
# Build system for PS1 (PsyQ 4.0 libraries, GCC 2.7.2, ASPSX 2.34)

# -- Configuration --
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DELETE_ON_ERROR:
TARGET       := bb2
# The lists the build reads, the SHA1 and the symbol table
CONFIG_DIR   := config

# -- Toolchain --
# PsyQ GCC 2.7.2 compiler (built from decompals/mips-gcc-2.7.2)
CC1          := tools/gcc-2.7.2/build/cc1
# maspsx ASPSX compatibility layer
MASPSX       := python3 tools/maspsx/maspsx.py
MASPSX_FLAGS := --expand-div --aspsx-version=2.34 --expand-lb --expand-lb-funcs=$(CONFIG_DIR)/expand_lb_funcs.txt --expand-dest-funcs=$(CONFIG_DIR)/expand_dest_funcs.txt --prefill-label-funcs=$(CONFIG_DIR)/maspsx_prefill_label_funcs.txt --comm-own-sections

# GNU MIPS cross-tools
AS           := mipsel-linux-gnu-as
LD           := mipsel-linux-gnu-ld
OBJCOPY      := mipsel-linux-gnu-objcopy
CPP          := mipsel-linux-gnu-cpp

# -- Compiler Flags --
# -O2: standard optimization level for PsyQ games
# -G8: the small-data threshold of every file of the game (CC_FLAGS_GP)
# -G0: no small data, for Sony's library code and the game's data files (CC_FLAGS)
# -funsigned-char: common PsyQ convention
# -mcpu=3000: target R3000A
# -mel: MANDATORY. The prebuilt cc1's mips-mips-gnu triple defaults BIG-endian;
# -mel flips BYTES_BIG_ENDIAN (spill-slot layout, bitfield direction, lwl/lwr).
# Load-bearing for the oracle match — do not remove (see AGENTS.md).
# -msoft-float: MANDATORY. The same triple defaults HARD-float; the PS1 has no
# FPU and PsyQ cc1psx prints "# Cc1 defaults: -mgas -msoft-float". Hard float
# leaves 32 FP regs allocatable, doubling loop.c's invariant-hoist threshold
# (122 vs 58). Do not remove.
CC_FLAGS     := -O2 -G0 -funsigned-char -quiet -mcpu=3000 -mips1 -mno-abicalls -fno-builtin -mel -msoft-float
CC_FLAGS_GP  := -O2 -G8 -funsigned-char -quiet -mcpu=3000 -mips1 -mno-abicalls -fno-builtin -mel -msoft-float
# Warnings never change the object; they are a source-hygiene gate. TUs in
# NOWARN_FILES (below) still compile with -w; every other TU gets WARN_FLAGS.
WARN_FLAGS   := -Wimplicit -Werror
AS_FLAGS     := -Iinclude -march=r3000 -mtune=r3000 -no-pad-sections -O1 -G0
# -nostdinc: nothing of the host's is read (where the host has a
# /usr/include/stdc-predef.h, cpp would include it and cc1 reject its line marker).
CPP_FLAGS    := -Iinclude -nostdinc -undef -Wall -lang-c -fno-builtin
CPP_DEFS     := -Dmips -D__GNUC__=2 -D__OPTIMIZE__ -D__mips__ -D__mips -Dpsx -D__psx__ -D__psx \
                -D_PSYQ -D__EXTENSIONS__ -D_MIPSEL -D_LANGUAGE_C -DLANGUAGE_C
LD_FLAGS     := -nostdlib --no-check-sections

# -- Directories --
ASM_DIR      := asm
SRC_DIR      := src
BUILD_DIR    := build
INCLUDE_DIR  := include
# The play tests' disc images; `clean` leaves it alone.
PLAY_DIR     := build/play

# -- ReBushido --
# The modded build (docs/REBUSHIDO.md): the same source with the master
# switch of include/rebushido.h on, and the files of src/rebushido/, into a
# build folder of its own, so that it shares no object with the matching
# build. `make rebushido` and the targets beside it run this Makefile again
# with REBUSHIDO=1; without it nothing here is any different from before.
REBUSHIDO    ?= 0
RB_SRC_DIR   := $(SRC_DIR)/rebushido
OUT          := $(TARGET)
ifeq ($(REBUSHIDO),1)
BUILD_DIR    := build/rebushido
OUT          := rebushido
CPP_DEFS     += -DREBUSHIDO=1
endif

# -- Quick boot --
# A ReBushido build that starts one fight in place of the boot image, the
# opening and the menus: a developer's tool, for trying a change and for the
# play tests (docs/REBUSHIDO.md, "Booting straight into a fight"). The fight
# is chosen here, by the game's own names; `python3 tools/quickboot.py
# --list` shows what each can be. `make quickboot` runs this Makefile again
# with REBUSHIDO=1 QUICKBOOT=1, into a build folder of its own.
QB_MODE      ?= FIGHT_MODE_VS
QB_P1        ?= ROSTER_KANNUKI
QB_P2        ?= ROSTER_GENGORO
QB_WEAPON1   ?= WEAPON_ID_KATANA
QB_WEAPON2   ?= WEAPON_ID_KATANA
QB_STAGE     ?= STAGE_BAMBOO_THICKET
QB_CPU2      ?= 1
# 1: the fight starts with the "Bugfixes" setting on (the build has no
# options page to turn it on at).
QB_BUGFIXES  ?= 0
# The disc image `make quickboot-disc` writes: build/play/$(QB_NAME).bin.
QB_NAME      ?= QuickBoot
QB_SETTINGS  := QB_MODE=$(QB_MODE) QB_P1=$(QB_P1) QB_P2=$(QB_P2) QB_WEAPON1=$(QB_WEAPON1) \
                QB_WEAPON2=$(QB_WEAPON2) QB_STAGE=$(QB_STAGE) QB_CPU2=$(QB_CPU2) \
                QB_BUGFIXES=$(QB_BUGFIXES)
QUICKBOOT    ?= 0
ifeq ($(REBUSHIDO)$(QUICKBOOT),11)
BUILD_DIR    := build/quickboot
OUT          := quickboot
CPP_DEFS     += -DRB_QUICK_BOOT_BUILD=1
endif

# -- Output --
ELF          := $(BUILD_DIR)/$(OUT).elf
BIN          := $(BUILD_DIR)/$(OUT).bin
EXE          := $(BUILD_DIR)/$(OUT).exe

# -- Collect source files --
# Top-level assembly: header.s (the PS-X EXE header, linked as .data)
S_FILES      := $(wildcard $(ASM_DIR)/*.s)
S_O_FILES    := $(patsubst $(ASM_DIR)/%.s,$(BUILD_DIR)/$(ASM_DIR)/%.o,$(S_FILES))

# C source files (decompiled functions), found recursively. A TU id is the path
# under src/ without .c (`main/tex`, `main/psxsdk/libcomb/comb`); its object is
# build/src/<id>.o, the path bb2.ld links.
# src/rebushido/ is the ReBushido build's alone. src/port/ is the native
# port's (docs/PORT.md): no PlayStation build compiles or links it.
PORT_SRC_DIR := $(SRC_DIR)/port
C_FILES      := $(sort $(shell find $(SRC_DIR) -name '*.c' -not -path '$(PORT_SRC_DIR)/*' $(if $(filter 1,$(REBUSHIDO)),,-not -path '$(RB_SRC_DIR)/*')))
C_IDS        := $(patsubst $(SRC_DIR)/%.c,%,$(C_FILES))
C_O_FILES    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(SRC_DIR)/%.o,$(C_FILES))

# Per-function asm objects linked directly (explicit opt-in, not a wildcard:
# asm/funcs/ holds the listings the C files include, which are not objects).
# Used where a function cannot live inside a C translation unit: under cc1 -G8
# cc1 buffers function bodies to a temp file (TARGET_FILE_SWITCHING),
# so a file-scope __asm__ floats to the top of the TU instead of staying in
# place. Empty today; kept for any future raw-asm object.
LINKED_ASM_FUNCS :=
ASM_FUNC_O_FILES := $(patsubst %,$(BUILD_DIR)/$(ASM_DIR)/funcs/%.o,$(LINKED_ASM_FUNCS))

# All objects
ALL_O_FILES  := $(S_O_FILES) $(C_O_FILES) $(ASM_FUNC_O_FILES)

# -- Top-level targets --
.PHONY: all clean check clean-check shift movovl modern test checks \
	rebushido rebushido-disc rebushido-patch rebushido-test rebushido-modern rb-build \
	quickboot quickboot-compile quickboot-disc playtest playtest-discs playtest-console gte-probe port \
	patcher port-sdl port-sdl-windows port-gl-windows port-rollback port-net

ifeq ($(REBUSHIDO),1)
all: rb-build

# The byte check and the relink are the matching build's.
check clean-check shift:
	@echo "make $@ is the matching build's: run it without REBUSHIDO=1"; exit 1
else
all: check

# Build and verify match
check: $(EXE) $(CONFIG_DIR)/$(TARGET).sha1
	@sha1sum -c $(CONFIG_DIR)/$(TARGET).sha1 || { echo "MISMATCH: $(TARGET) does not match"; exit 1; }
	@echo "OK: $(TARGET) matches!"

# Force a clean rebuild before checking the final hash.
clean-check:
	$(MAKE) clean
	$(MAKE) check
endif

# The ReBushido build: compile with the master switch on into
# build/rebushido/, link, and report the size and the room left; fails if
# the program does not fit below the fixed buffers (tools/rebushido.py).
# First the files that ask RB_QUICK_BOOT are compiled with it on, so that
# the check of this build keeps quick boot compiling too.
rebushido:
	$(MAKE) REBUSHIDO=1 QUICKBOOT=1 quickboot-compile
	$(MAKE) REBUSHIDO=1 rb-build

rb-build: $(EXE)
	python3 tools/rebushido.py fit $(ELF) $(EXE)

# A disc to play: the retail image, which is only read, with the ReBushido
# executable in place of the game's (tools/build_disc.py).
RETAIL_IMAGE ?= Bushido Blade 2 (USA).bin
rebushido-disc: rebushido
	@[ -f "$(RETAIL_IMAGE)" ] || { echo "rebushido-disc: no retail image \"$(RETAIL_IMAGE)\" here (your own copy of the disc; RETAIL_IMAGE=... names another)"; exit 1; }
	python3 tools/build_disc.py --image "$(RETAIL_IMAGE)" --out $(PLAY_DIR)/ReBushido.bin --exe build/rebushido/rebushido.exe
	python3 tools/playtest.py --symbols build/rebushido/rebushido.elf $(PLAY_DIR)/ReBushido.sym
	@echo "OK: $(PLAY_DIR)/ReBushido.cue"

# The players' patcher (tools/patcher/patcher.c; docs/REBUSHIDO.md,
# "Applying the patch"): one file of C that applies a release's patch,
# built for this host and, where mingw-w64 is installed, for Windows, into
# build/release/. The Windows one is linked whole (-static), so that it
# asks a player's machine for nothing but the system's own libraries.
PATCHER_DIR    := build/release
PATCHER_SRC    := tools/patcher/patcher.c
PATCHER_CFLAGS := -std=c99 -O2 -Wall -Wextra
PATCHER_LIBS   := -lcomdlg32 -lshell32
HOST_CC        ?= cc
MINGW_CC       ?= x86_64-w64-mingw32-gcc
patcher:
	@mkdir -p $(PATCHER_DIR)
	$(HOST_CC) $(PATCHER_CFLAGS) -o $(PATCHER_DIR)/ReBushidoPatcher $(PATCHER_SRC)
	@if command -v $(MINGW_CC) > /dev/null 2>&1; then \
		$(MINGW_CC) $(PATCHER_CFLAGS) -static -s -o $(PATCHER_DIR)/ReBushidoPatcher.exe $(PATCHER_SRC) $(PATCHER_LIBS) && \
		echo "OK: $(PATCHER_DIR)/ReBushidoPatcher and ReBushidoPatcher.exe"; \
	else \
		rm -f $(PATCHER_DIR)/ReBushidoPatcher.exe; \
		echo "patcher: no $(MINGW_CC) here (apt install mingw-w64): ReBushidoPatcher.exe, the Windows program, is not built"; \
		echo "OK: $(PATCHER_DIR)/ReBushidoPatcher"; \
	fi

# What a release is made of (docs/REBUSHIDO.md, "Making a release"): the
# matching build, which must match, the ReBushido disc, and from the two
# images a patch with a README for players in build/release/
# (tools/rebushido_release.py) beside the patcher; the patch's own tool
# and the patcher are both tried on the retail image before the patch is
# kept. RB_RELEASE=1 builds the version without -dev and refuses a tree
# with anything uncommitted; without it the patch is a trial. It needs the
# retail image, so it is no check: where there is none it says so.
rebushido-patch:
	@if [ ! -f "$(RETAIL_IMAGE)" ]; then \
		echo "rebushido-patch: skipped: no retail image \"$(RETAIL_IMAGE)\" here (your own copy of the disc; RETAIL_IMAGE=... names another)"; \
	elif [ "$(RB_RELEASE)" = 1 ] && [ -n "$$(git status --porcelain)" ]; then \
		echo "rebushido-patch: no release from a tree with changes or files that are not committed"; exit 1; \
	else \
		$(MAKE) check && $(MAKE) rebushido-disc && $(MAKE) patcher && \
		python3 tools/rebushido_release.py --retail "$(RETAIL_IMAGE)" $(if $(filter 1,$(RB_RELEASE)),--release); \
	fi

# The quick-boot build: the ReBushido build with RB_QUICK_BOOT on and the
# fight of the QB_ variables, into build/quickboot/. A developer's tool.
quickboot:
	python3 tools/quickboot.py check $(QB_SETTINGS) QB_NAME=$(QB_NAME)
	$(MAKE) REBUSHIDO=1 QUICKBOOT=1 rb-build

# The files that ask RB_QUICK_BOOT, compiled as the quick-boot build
# compiles them and no more: a few seconds, where the whole build is a
# minute.
ifeq ($(REBUSHIDO)$(QUICKBOOT),11)
QB_ASKERS    := $(sort $(shell grep -l -w RB_QUICK_BOOT -r $(SRC_DIR) --include='*.c'))
quickboot-compile: $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(SRC_DIR)/%.o,$(QB_ASKERS))
	@echo "OK: quick boot compiles ($(QB_ASKERS))"
endif

# Its disc, beside the ReBushido disc and not in its place, with the table
# of its names and the fight it starts for tools/playtest.py.
quickboot-disc: quickboot
	@[ -f "$(RETAIL_IMAGE)" ] || { echo "quickboot-disc: no retail image \"$(RETAIL_IMAGE)\" here (your own copy of the disc; RETAIL_IMAGE=... names another)"; exit 1; }
	python3 tools/build_disc.py --image "$(RETAIL_IMAGE)" --out $(PLAY_DIR)/$(QB_NAME).bin --exe build/quickboot/quickboot.exe
	python3 tools/playtest.py --symbols build/quickboot/quickboot.elf $(PLAY_DIR)/$(QB_NAME).sym
	python3 tools/quickboot.py write $(PLAY_DIR)/$(QB_NAME).txt $(QB_SETTINGS)
	@echo "OK: $(PLAY_DIR)/$(QB_NAME).cue"

# The scripted play tests (tests/play/README.md): the game run in an
# emulator with no window, on the developer's machine only. Builds the
# discs the tests want beside the retail one, the ReBushido disc and three
# quick-boot discs of the tests' own (a fight to play to its end, and a
# fight that goes through two of the original's bugs, built with the
# "Bugfixes" setting off and with it on), then runs the tests; where the
# retail image or the emulator is missing the tests say what they skipped.
PLAYTEST_QB  := QB_NAME=QuickBoot-playtest QB_MODE=FIGHT_MODE_VS QB_P1=ROSTER_MIKADO QB_P2=ROSTER_UTAMARU \
                QB_WEAPON1=WEAPON_ID_NAGINATA QB_WEAPON2=WEAPON_ID_KATANA QB_STAGE=STAGE_LIGHTHOUSE QB_CPU2=1
# The vanisher with a naginata, off the shrine's stage: its first frame
# sweeps the shaft from a stale place, and struck it comes back at another
# stage's place (tests/play/first_frame_sweep.toml, vanish.toml).
PLAYTEST_BUGS := QB_MODE=FIGHT_MODE_VS QB_P1=ROSTER_VANISHER QB_P2=ROSTER_UTAMARU \
                QB_WEAPON1=WEAPON_ID_NAGINATA QB_WEAPON2=WEAPON_ID_KATANA QB_STAGE=STAGE_LIGHTHOUSE QB_CPU2=1
playtest-discs:
	$(MAKE) port
	$(MAKE) playtest-console

# The tests' programs of the PlayStation alone, without the port's: what
# tools/gate.py builds beside `make port`.
playtest-console:
	$(MAKE) gte-probe
	@if [ -f "$(RETAIL_IMAGE)" ]; then \
		$(MAKE) rebushido-disc && $(MAKE) quickboot-disc $(PLAYTEST_QB) && \
		$(MAKE) quickboot-disc $(PLAYTEST_BUGS) QB_NAME=QuickBoot-bugs QB_BUGFIXES=0 && \
		$(MAKE) quickboot-disc $(PLAYTEST_BUGS) QB_NAME=QuickBoot-bugfixes QB_BUGFIXES=1; \
	else \
		echo "playtest: no retail image \"$(RETAIL_IMAGE)\" here: no disc is built"; \
	fi

playtest: playtest-discs
	python3 tools/playtest.py $(PLAYTEST)

# The host tests with the master switch on: the game's, and ReBushido's own
# (tests/rebushido/).
rebushido-test:
	python3 tests/run.py --rebushido

# `modern` with the master switch on, src/rebushido/ included.
rebushido-modern:
	python3 tools/modern/check.py --check --rebushido

# The movie overlay (disc/STR/MOVOVL.EXE) is a separate program with its own
# Makefile and SHA1. Not part of `check`.
movovl:
	$(MAKE) -C movovl clean
	$(MAKE) -C movovl check

# Shift test (non-matching): relink the objects with padding into build/shift/
# and report address assumptions (tools/shift_test.py). Never writes the EXE.
ifneq ($(REBUSHIDO),1)
shift: $(ELF)
	python3 tools/shift_test.py
endif

# The game's C through the host's modern gcc, for its warnings and its
# analyser (tools/modern/check.py). Fails on a serious finding that
# tools/modern/baseline.tsv does not have. Makes nothing the build uses.
modern:
	python3 tools/modern/check.py --check

# The tests of what the game's C computes (tests/README.md): its pure
# functions built for the host and compared with real maths, hand-made cases
# and the disc's own data. Needs the host's gcc and python3, no build of the
# game; the tests that read the disc are skipped without it.
test:
	python3 tests/run.py

# The native port (docs/PORT.md): the game's C with the port's switch on,
# Sony's library code where it is plain arithmetic, and src/port/, built
# for this host (tools/port_build.py). An x86 Linux program with no C
# library, as the host tests are: it needs the host's gcc and python3, and
# says so where that gcc cannot make one. No PlayStation build has anything
# of it. It is built twice, with 64-bit pointers and with 32-bit ones
# (docs/PORT.md, "64 bits"): the default's program is build/port/rebushido
# and the other's is beside it, in build/port64/ or build/port32/. Then:
#   - every structure's layout in the two programs and on the PlayStation
#     is held to the list of those that may differ (tools/port_layout.py,
#     config/port_layout.txt);
#   - no pointer of the 64-bit program is made of a number that is not a
#     32-bit address (tools/port_casts.py);
#   - the places where either compiler may evaluate an expression in
#     another order than the PlayStation's did are held to the list of
#     those that were looked at (tools/port_order.py, config/port_order.txt).
# `make port PORT_BITS=64` (or 32) builds that one program and asks only
# what can be asked of one.
#   build/port/rebushido --image "Bushido Blade 2 (USA).cue" --frames 600 --trace
port:
ifeq ($(PORT_BITS),)
	python3 tools/port_build.py --bits 32
	python3 tools/port_build.py --bits 64
	python3 tools/port_layout.py --check
	python3 tools/port_casts.py --check
	python3 tools/port_order.py --check --bits 32
	python3 tools/port_order.py --check --bits 64
else
	python3 tools/port_build.py --bits $(PORT_BITS)
	python3 tools/port_order.py --check --bits $(PORT_BITS)
endif

# The native port's rollback self-test (docs/NETPLAY.md, "Save, load and
# hash"; tests/README.md): both programs `make port` built (build them
# again first: it runs what is there) run port_game's
# plan headless, once as they are and once saving the game's state, running
# eight frames, putting it back and running them again every 24 frames; it
# fails on any difference of the state, of what the two runs print or of
# what they write. About half a minute. It needs the retail disc's image
# (REBUSHIDO_RETAIL, or the .cue at the checkout's root) and no emulator,
# so it is not one of the checks, which run without the disc.
port-rollback:
	python3 tools/port_rollback.py

# Two programs of the port playing each other over UDP on 127.0.0.1
# (docs/NETPLAY.md, sections 11 to 13; tools/port_net.py): the game's own
# link-cable mode in lockstep, both consoles printing the same trace to
# the end of a fight; rollback netplay of port_game's whole session with
# 100 ms of latency and 5% of datagrams lost, both ending on the same
# state hashes; a session set up by --host and --join through versus
# select, a fight, a rematch and the guest's leaving; and the same
# between two of the window's programs, headless and in real time, and
# from the settings menu's Play online page. Both sizes; the four at
# once, each on UDP ports of its own from PORT_NET_BASE up (96 of them);
# it needs the disc's image, as port-rollback does, and runs what make
# port and make port-sdl built (the window's part is skipped where there
# is none).
PORT_NET_BASE ?= 47000
port-net:
	python3 tools/port_net.py all --port-base $(PORT_NET_BASE)

# The native port with a window (docs/PORT.md, "The window"): the same
# program with SDL 2 and a C library, for a person to play, into
# build/port-sdl/ (tools/port_sdl_build.py, which says what each needs
# installed). Local: no check builds them, and `make port` stays the
# program the checks and the play tests use.
# Each builds the program of 64-bit pointers; PORT_BITS=32 builds the
# 32-bit one beside it, into build/port-sdl/linux32/ or windows32/.
#   build/port-sdl/linux/rebushido --image "Bushido Blade 2 (USA).cue"
port-sdl:
	python3 tools/port_sdl_build.py

port-sdl-windows:
	python3 tools/port_sdl_build.py --windows

# The picture provers' program for Windows, on the machine's own graphics card
# with a window never shown (tools/port_gl_build.py --windows; docs/PORT.md,
# "The provers on a graphics card"): build/port-gl/windows/rebushido-gl.exe.
port-gl-windows:
	python3 tools/port_gl_build.py --windows

# Every check GitHub Actions runs, in its order, with a summary
# (tools/checks.sh): the counts, the lint, modern, test, the game's bytes,
# the overlay's, shift; then ReBushido's build, tests and modern, and the
# native port's link.
checks:
	bash tools/checks.sh

# -- SHA1 checksum file --
$(CONFIG_DIR)/$(TARGET).sha1:
	@echo "62efab4f73f992798c43e8c730aa43baa10bb4fa  $(EXE)" > $@

# -- Link --
# The matching build links by bb2.ld. The ReBushido build links by a script
# tools/rebushido.py writes from bb2.ld and the objects: the same order, and
# what the objects hold that bb2.ld does not place added at the end of its
# kind. bb2.ld itself is never changed for ReBushido.
ifeq ($(REBUSHIDO),1)
LD_SCRIPT    := $(BUILD_DIR)/$(OUT).ld
# The list of the objects, written again only when it changes: taking a
# file out of src/rebushido/ has to make the script, and so the link, again.
RB_OBJECTS   := $(BUILD_DIR)/objects.txt
$(shell mkdir -p $(BUILD_DIR); echo "$(ALL_O_FILES)" | cmp -s - $(RB_OBJECTS) || echo "$(ALL_O_FILES)" > $(RB_OBJECTS))
$(LD_SCRIPT): $(ALL_O_FILES) $(RB_OBJECTS) $(TARGET).ld tools/rebushido.py tools/elf32.py
	@python3 tools/rebushido.py ld --out $@ $(ALL_O_FILES)
else
LD_SCRIPT    := $(TARGET).ld
endif

$(ELF): $(ALL_O_FILES) $(LD_SCRIPT) $(CONFIG_DIR)/named_syms.txt
	$(LD) $(LD_FLAGS) -Map $(BUILD_DIR)/$(OUT).map -T $(LD_SCRIPT) \
		-T $(CONFIG_DIR)/named_syms.txt -o $@

# Extract raw binary from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary -j .main $< $@

# Construct PS-X EXE: the linked .header (asm/header.s) with pc0 / t_addr / t_size
# computed from the ELF (tools/make_psexe.py), then the binary
# The ReBushido build's file ends where the start-up code begins to clear
# memory, on a sector: its last zeroes are not in it (tools/make_psexe.py).
$(EXE): $(BIN) $(ELF) tools/make_psexe.py tools/elf32.py
	python3 tools/make_psexe.py $(ELF) $< $@ $(if $(filter 1,$(REBUSHIDO)),--zeroed-from main_SBSS_START)

# -- The small-data threshold --
# Every file of the game is compiled -G8: the whole game builds to the original's
# code that way (docs/FAKES.md, "The original's source files"), and eight files build to it no other
# way. At -G8 this cc1 writes all of a file's strings before its jump tables, as
# the original's files have them, and treats a variable of eight bytes or fewer
# whose size it has seen as reachable through gp. Sony's library code and the
# game's data files (the two lists below) are compiled -G0. So are the three files
# of hand-written assembly, which were assembler sources: at -G8 this cc1 holds
# the functions' bodies back, and the assembly between them would come out first.
ASM_FILES := main/render_asm main/math_asm main/render_skin_asm

# -- Small data: maspsx -G8 except Sony library code --
# maspsx runs -G8 for every file: a file uses gp for a small symbol only if it DEFINES it (Sony
# ASPSX 2.34), statics are its own .sbss, small initialized objects its own .sdata (cc1psx -G8).
# Sony PsyQ library code was compiled -G0 and keeps it.
PSYQ_LIBRARY_FILES := main/psxsdk/libcomb/comb main/psxsdk/libgpu/sys main/psxsdk/libgpu/prim main/psxsdk/libetc/intr main/psxsdk/libspu/spu main/psxsdk/libcd/bios main/psxsdk/libc2/prnt main/psxsdk/libc2/sprintf main/psxsdk/libapi/c67 main/psxsdk/libapi/c112 main/psxsdk/libapi/c159 main/psxsdk/libapi/a08 main/psxsdk/libapi/a09 main/psxsdk/libapi/a11 main/psxsdk/libapi/a12 main/psxsdk/libapi/a36 main/psxsdk/libapi/a37 main/psxsdk/libapi/a39 main/psxsdk/libapi/a50 main/psxsdk/libapi/a52 main/psxsdk/libapi/a53 main/psxsdk/libapi/a54 main/psxsdk/libapi/a65 main/psxsdk/libapi/a66 main/psxsdk/libapi/a67 main/psxsdk/libapi/a91 main/psxsdk/libapi/counter main/psxsdk/libapi/pad main/psxsdk/libapi/a18 main/psxsdk/libapi/a19 main/psxsdk/libapi/a20 main/psxsdk/libapi/a21 main/psxsdk/libapi/l02 main/psxsdk/libapi/l03 main/psxsdk/libapi/patch main/psxsdk/libapi/c68 main/psxsdk/libapi/sendpad main/psxsdk/libapi/chclrpad main/psxsdk/libc2/memcpy main/psxsdk/libc2/rand main/psxsdk/libc2/strcpy main/psxsdk/libc2/strlen main/psxsdk/libc2/printf main/psxsdk/libapi/a71 main/psxsdk/libapi/a72 main/psxsdk/libc2/ctype main/psxsdk/libc2/memchr main/psxsdk/libc2/putchar main/psxsdk/libc2/memmove main/psxsdk/libcard/c171 main/psxsdk/libcard/c172 main/psxsdk/libcard/card main/psxsdk/libcard/a78 main/psxsdk/libcard/a80 main/psxsdk/libcard/init main/psxsdk/libcard/a74 main/psxsdk/libcard/a75 main/psxsdk/libcard/a76 main/psxsdk/libcard/end main/psxsdk/libgpu/ext main/psxsdk/libapi/c73 main/psxsdk/libgte/geo_00 main/psxsdk/libgte/geo_01 main/psxsdk/libgte/msc00 main/psxsdk/libgte/msc01 main/psxsdk/libgte/msc06 main/psxsdk/libgte/msc09 main/psxsdk/libgte/mtx_000 main/psxsdk/libgte/mtx_003 main/psxsdk/libgte/mtx_004 main/psxsdk/libgte/mtx_005 main/psxsdk/libgte/mtx_00a main/psxsdk/libgte/mtx_01 main/psxsdk/libgte/mtx_03 main/psxsdk/libgte/mtx_04 main/psxsdk/libgte/mtx_05 main/psxsdk/libgte/mtx_08 main/psxsdk/libgte/mtx_09 main/psxsdk/libgte/mtx_11 main/psxsdk/libgte/mtx_12 main/psxsdk/libgte/reg04 main/psxsdk/libgte/reg09 main/psxsdk/libgte/reg10 main/psxsdk/libgte/reg11 main/psxsdk/libgte/reg12 main/psxsdk/libgte/reg13 main/psxsdk/libgte/smp_00 main/psxsdk/libgte/smp_02 main/psxsdk/libgte/smp_03 main/psxsdk/libgte/smp_04 main/psxsdk/libgte/cmb_00 main/psxsdk/libgte/fgo_01 main/psxsdk/libgte/fgo_03 main/psxsdk/libgte/fgo_04 main/psxsdk/libgte/fgo_05 main/psxsdk/libgte/fgo_06 main/psxsdk/libgte/ratan main/psxsdk/libgte/patchgte main/psxsdk/libcd/event main/psxsdk/libapi/a07 main/psxsdk/libcd/sys main/psxsdk/libc2/puts main/psxsdk/libcd/cdread main/psxsdk/libetc/vsync main/psxsdk/libapi/l10 main/psxsdk/libapi/c114 main/psxsdk/libapi/a23 main/psxsdk/libapi/a24 main/psxsdk/libapi/a25 main/psxsdk/libc2/setjmp main/psxsdk/libetc/intr_vb main/psxsdk/libetc/intr_dma main/psxsdk/libetc/vmode main/psxsdk/libsn/open main/psxsdk/libsn/close main/psxsdk/libsn/lseek main/psxsdk/libsn/snmain main/psxsdk/libsn/sndef main/psxsdk/libapi/c57 main/psxsdk/libsn/read main/psxsdk/libsn/snread main/psxsdk/libsnd/ssend main/psxsdk/libsnd/ssinit_c main/psxsdk/libsnd/ssinit main/psxsdk/libsnd/ssquit main/psxsdk/libsnd/scssattr main/psxsdk/libsnd/scsmvol main/psxsdk/libsnd/ssstart main/psxsdk/libsnd/sscall main/psxsdk/libsnd/cres main/psxsdk/libsnd/decre main/psxsdk/libsnd/pause main/psxsdk/libsnd/play main/psxsdk/libsnd/midiread main/psxsdk/libsnd/miditime main/psxsdk/libsnd/next main/psxsdk/libsnd/replay main/psxsdk/libsnd/stop main/psxsdk/libsnd/scssvol main/psxsdk/libsnd/sstick main/psxsdk/libsnd/tempo main/psxsdk/libsnd/ut_ako main/psxsdk/libsnd/ut_gvba main/psxsdk/libsnd/ut_keyv main/psxsdk/libsnd/ut_rdep main/psxsdk/libsnd/ut_rev main/psxsdk/libsnd/ut_roff main/psxsdk/libsnd/ut_ron main/psxsdk/libsnd/ut_vvol main/psxsdk/libsnd/vm_aloc2 main/psxsdk/libsnd/vm_doff main/psxsdk/libsnd/vm_f main/psxsdk/libsnd/vm_init main/psxsdk/libsnd/vm_n2p main/psxsdk/libsnd/vm_no1 main/psxsdk/libsnd/vm_nowof main/psxsdk/libsnd/vm_nowon main/psxsdk/libsnd/vm_seq main/psxsdk/libsnd/vm_vsu main/psxsdk/libsnd/vs_auto main/psxsdk/libsnd/vs_mono main/psxsdk/libsnd/vs_srv main/psxsdk/libsnd/vs_vab main/psxsdk/libsnd/vs_vfb main/psxsdk/libsnd/vs_vh main/psxsdk/libsnd/vs_vtb main/psxsdk/libsnd/vs_vtc main/psxsdk/libspu/s_i main/psxsdk/libspu/s_ini main/psxsdk/libspu/s_dcb main/psxsdk/libspu/s_q main/psxsdk/libapi/a13 main/psxsdk/libspu/s_m_init main/psxsdk/libspu/s_m_m main/psxsdk/libspu/s_m_int main/psxsdk/libspu/s_m_f main/psxsdk/libspu/s_snv main/psxsdk/libspu/s_sav main/psxsdk/libspu/s_snc main/psxsdk/libspu/s_sr main/psxsdk/libspu/s_m_util main/psxsdk/libspu/s_srmp main/psxsdk/libspu/s_sra main/psxsdk/libspu/s_srv main/psxsdk/libspu/s_crwa main/psxsdk/libapi/a10 main/psxsdk/libspu/s_sk main/psxsdk/libspu/s_gks main/psxsdk/libspu/s_r main/psxsdk/libspu/s_w main/psxsdk/libspu/s_stsa main/psxsdk/libspu/s_stm main/psxsdk/libspu/s_itc main/psxsdk/libspu/s_it main/psxsdk/libspu/s_sca main/psxsdk/libspu/sr_gaks main/psxsdk/libspu/s_sva main/psxsdk/libspu/s_n2p main/psxsdk/libspu/s_gvv main/psxsdk/libspu/s_gvex

# -- Data-only files without small data --
# A file with no code whose small initialized objects the original keeps in .data (below the
# .sdata range) is built without maspsx -G8 too.
DATA_ONLY_NO_SDATA_FILES := main/fight_data main/fight_sdata main/stage_data main/resource_data main/cpu_data main/snd_data main/hud_data main/eff_data main/select_data main/menu_data main/psxsdk/libspu/s_m

# -- Per-file lh expansion opt-in --
# ASPSX expands lb→lbu+sll+sra and lh→lhu+sll+sra in certain contexts.
# maspsx replicates the lb form for every file (--expand-lb in MASPSX_FLAGS);
# files listed here also get --expand-lh. None needs it today.
EXPAND_LH_FILES :=

# -- Rodata alignment: object-relative, one rule for every C object --
# Sony ASPSX pads `.align 3` (switch tables) relative to each object's start and
# PSYLINK places objects on 4-byte boundaries.
# GNU as already pads relative to the section start, so setting every object's
# .rodata alignment to 4 after `as` reproduces it.
RODATA_OBJ_ALIGN := --set-section-alignment .rodata=4

# -- Per-file -fno-strength-reduce opt-in --
# Some functions were originally compiled without GCC's loop strength-reduction.
# These files get the flag applied; other files in NO_SR_FILES below.
NO_SR_FILES :=

# -- Per-file -w --
# Files that still compile with -w: they warn under WARN_FLAGS (implicit declarations,
# pointer/integer mixes). Fix a file's warnings, then drop it from this list.
NOWARN_FILES := main/psxsdk/libspu/spu

# Every per-file list names TU ids. An id that is not a TU stops the build: a
# moved or renamed file must take its flags along, never silently lose them.
FLAG_LISTS := ASM_FILES PSYQ_LIBRARY_FILES DATA_ONLY_NO_SDATA_FILES EXPAND_LH_FILES NO_SR_FILES NOWARN_FILES
# (A file of src/rebushido/ is a TU of the ReBushido build only, so the test
# is that the file is there.)
$(foreach l,$(FLAG_LISTS),$(foreach f,$($(l)),$(if $(wildcard $(SRC_DIR)/$(f).c),,$(error $(l) names '$(f)', which is not a TU (no $(SRC_DIR)/$(f).c)))))

# Helper: resolve CC/MASPSX flags based on whether file needs GP-relative
cc_flags_for = $(if $(filter $1,$(PSYQ_LIBRARY_FILES) $(DATA_ONLY_NO_SDATA_FILES) $(ASM_FILES)),$(CC_FLAGS),$(CC_FLAGS_GP))$(if $(filter $1,$(NO_SR_FILES)), -fno-strength-reduce)$(if $(filter $1,$(NOWARN_FILES)), -w, $(WARN_FLAGS))
maspsx_flags_for = $(MASPSX_FLAGS)$(if $(filter $1,$(PSYQ_LIBRARY_FILES) $(DATA_ONLY_NO_SDATA_FILES)),, -G8)$(if $(filter $1,$(EXPAND_LH_FILES)), --expand-lh)

# Shared pipeline dependencies for every C object. Without these, changing
# pipeline/toolchain config can leave stale objects in place because
# make only notices src/**/*.c timestamps.
PIPELINE_DEPS := Makefile \
	$(CC1) \
	$(CONFIG_DIR)/expand_lb_funcs.txt $(CONFIG_DIR)/expand_dest_funcs.txt $(CONFIG_DIR)/maspsx_prefill_label_funcs.txt \
	$(wildcard tools/maspsx/*.py tools/maspsx/maspsx/*.py) \
	$(shell find $(INCLUDE_DIR) -type f) $(wildcard asm/funcs/*.s) $(shell find $(SRC_DIR) -name '*.h')

# -- ReBushido's version text --
# src/rebushido/version.c is given the commit it is built from, with -dirty
# when the tree has changes not committed. The commit is kept in a file that
# is written again only when it changes, so version.o is rebuilt then and
# not otherwise.
ifeq ($(REBUSHIDO),1)
# (given by tools/mirror.sh where the build runs in a copy with no .git)
ifeq ($(origin RB_GIT_HASH),undefined)
RB_GIT_HASH  := $(shell h=$$(git rev-parse --short HEAD 2>/dev/null) && { git diff --quiet HEAD 2>/dev/null || h=$$h-dirty; }; echo $${h:-unknown})
endif
# A release's version has no -dev after its number: RB_RELEASE=1, which
# `make rebushido-patch RB_RELEASE=1` is given (include/rebushido.h).
RB_VERSION_AS := $(RB_GIT_HASH)$(if $(filter 1,$(RB_RELEASE)), release)
RB_HASH_FILE := $(BUILD_DIR)/git_hash.txt
$(shell mkdir -p $(BUILD_DIR); [ "$$(cat $(RB_HASH_FILE) 2>/dev/null)" = "$(RB_VERSION_AS)" ] || echo "$(RB_VERSION_AS)" > $(RB_HASH_FILE))
$(BUILD_DIR)/$(RB_SRC_DIR)/version.o: $(RB_HASH_FILE)
$(BUILD_DIR)/$(RB_SRC_DIR)/version.o: CPP_DEFS += -DRB_GIT_HASH='"$(RB_GIT_HASH)"' $(if $(filter 1,$(RB_RELEASE)),-DRB_VERSION_SUFFIX='""')
endif

# -- Quick boot's fight --
# src/rebushido/quickboot.c is given the QB_ variables as RB_QB_ names. They
# are kept in a file that is written again only when one changes, so that
# quickboot.o is rebuilt then and not otherwise.
ifeq ($(REBUSHIDO)$(QUICKBOOT),11)
QB_FILE      := $(BUILD_DIR)/quickboot_settings.txt
$(shell mkdir -p $(BUILD_DIR); [ "$$(cat $(QB_FILE) 2>/dev/null)" = "$(QB_SETTINGS)" ] || echo "$(QB_SETTINGS)" > $(QB_FILE))
$(BUILD_DIR)/$(RB_SRC_DIR)/quickboot.o: $(QB_FILE)
$(BUILD_DIR)/$(RB_SRC_DIR)/quickboot.o: CPP_DEFS += $(patsubst QB_%,-DRB_QB_%,$(QB_SETTINGS))
endif

# -- Compile C source (decompiled functions) --
# Pipeline: cpp | cc1 | maspsx | as -> .o, then objcopy (.rodata alignment 4)
$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(PIPELINE_DEPS)
	@mkdir -p $(dir $@)
	$(CPP) $(CPP_FLAGS) $(CPP_DEFS) $< | $(CC1) $(call cc_flags_for,$*) | $(MASPSX) $(call maspsx_flags_for,$*) | $(AS) $(AS_FLAGS) -o $@
	$(OBJCOPY) $(RODATA_OBJ_ALIGN) $@

# -- Assemble .s files (non-decompiled asm) --
$(BUILD_DIR)/$(ASM_DIR)/%.o: $(ASM_DIR)/%.s $(shell find $(INCLUDE_DIR) -type f)
	@mkdir -p $(dir $@)
	$(AS) $(AS_FLAGS) $< -o $@

# -- The GTE probe --
# A PlayStation program of the play tests' own, no part of the game
# (tests/play/README.md, "The GTE probe"): it runs made-up cases on the
# emulator's GTE and compares what it reads back with what the port's GTE
# in C (src/port/gte.c) made of the same cases; and it calls the game's and
# Sony's routines that are assembly, linked into it from the matching
# build's own objects, and compares what they do with what the C the port
# has in their place does. So it is built in two steps: the host tests
# tests/port/gte_vectors.c and asm_vectors.c write down what the port's C
# makes of every case (expected.h), and the probe is compiled with that, by
# the game's own compiler, and linked as a program the emulator loads with
# no disc. It needs no retail image.
ifneq ($(REBUSHIDO),1)
PROBE_SRC    := tests/play/probe
PROBE_DIR    := build/probe
PROBE_HOST   := build/rebushido/tests/port
# The routines under test, as the game has them: the assembly of math_asm.c
# with its tables, the sine table (whose file has a function that wants
# Sony's rsin and rcos), and every module of Sony's libgte that has the
# port's C beside its listing (but InitGeom's, which patches the console's
# kernel), with the two tables they read.
PROBE_GTE_MODULES := $(patsubst $(SRC_DIR)/%.c,%,$(shell grep -l '^.if RB_PORT' $(SRC_DIR)/main/psxsdk/libgte/*.c))
PROBE_GAME_OBJS := $(patsubst %,$(BUILD_DIR)/$(SRC_DIR)/%.o,main/math_asm main/math_sin \
		main/render_asm main/render_skin_asm main/render \
		main/psxsdk/libgte/geo_00 main/psxsdk/libgte/geo_01 main/psxsdk/libgte/sincos \
		main/psxsdk/libgte/cstbl main/psxsdk/libgte/sqrtbl $(filter-out %/msc00,$(PROBE_GTE_MODULES)))
PROBE_OBJS   := $(BUILD_DIR)/$(ASM_DIR)/header.o $(PROBE_DIR)/start.o $(PROBE_DIR)/probe.o \
		$(PROBE_DIR)/render_stubs.o $(PROBE_GAME_OBJS)

$(PROBE_DIR)/expected.h: tests/port/gte_vectors.c tests/port/asm_vectors.c tests/port/asm_vectors.py \
		tests/port/render_vectors.c tests/port/render_vectors.py \
		$(PROBE_SRC)/cases.h $(PROBE_SRC)/routines.h $(PROBE_SRC)/render_cases.h \
		$(PORT_SRC_DIR)/gte.c $(INCLUDE_DIR)/port_gte.h $(INCLUDE_DIR)/render.h \
		$(SRC_DIR)/main/math_asm.c $(SRC_DIR)/main/render_asm.c $(SRC_DIR)/main/render_skin_asm.c \
		$(SRC_DIR)/main/render.c $(wildcard $(SRC_DIR)/main/psxsdk/libgte/*.c) tests/run.py
	@mkdir -p $(dir $@)
	python3 tests/run.py --rebushido port/gte_vectors port/asm_vectors port/render_vectors > /dev/null; \
		[ -x $(PROBE_HOST)/gte_vectors ] && [ -x $(PROBE_HOST)/asm_vectors ] && [ -x $(PROBE_HOST)/render_vectors ]
	{ $(PROBE_HOST)/gte_vectors table && $(PROBE_HOST)/asm_vectors table && $(PROBE_HOST)/render_vectors table; } \
		| grep -v '^checks ' > $@

$(PROBE_DIR)/%.o: $(PROBE_SRC)/%.c $(PROBE_SRC)/cases.h $(PROBE_SRC)/routines.h $(PROBE_SRC)/render_cases.h \
		$(PROBE_DIR)/expected.h $(PIPELINE_DEPS)
	@mkdir -p $(dir $@)
	$(CPP) $(CPP_FLAGS) $(CPP_DEFS) -I$(PROBE_SRC) -I$(PROBE_DIR) $< | $(CC1) $(CC_FLAGS) $(WARN_FLAGS) | $(MASPSX) $(MASPSX_FLAGS) | $(AS) $(AS_FLAGS) -o $@

$(PROBE_DIR)/start.o: $(PROBE_SRC)/start.s
	@mkdir -p $(dir $@)
	$(AS) $(AS_FLAGS) $< -o $@

$(PROBE_DIR)/probe.elf: $(PROBE_OBJS) $(PROBE_SRC)/probe.ld
	$(LD) $(LD_FLAGS) -Map $(PROBE_DIR)/probe.map -T $(PROBE_SRC)/probe.ld -o $@ $(PROBE_OBJS)

$(PLAY_DIR)/GteProbe.exe: $(PROBE_DIR)/probe.elf tools/make_psexe.py tools/elf32.py
	$(OBJCOPY) -O binary -j .main $< $(PROBE_DIR)/probe.bin
	python3 tools/make_psexe.py $< $(PROBE_DIR)/probe.bin $@ --entry _start
	python3 tools/playtest.py --symbols $< $(PLAY_DIR)/GteProbe.sym

gte-probe: $(PLAY_DIR)/GteProbe.exe
	@echo "OK: $(PLAY_DIR)/GteProbe.exe"
endif

# -- Clean --
# build/play/ is kept: it holds a disc image built for a play test
# (tools/build_disc.py), which takes minutes to make again. So is
# build/release/, the patch of `make rebushido-patch`. The ReBushido
# build (build/rebushido/), the quick-boot build (build/quickboot/) and the
# play tests' pictures (build/playtest/) go with the rest.
clean:
	[ ! -d build ] || find build -mindepth 1 -maxdepth 1 ! -name play ! -name release -exec rm -rf {} +
