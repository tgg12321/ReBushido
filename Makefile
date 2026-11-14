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

# -- Output --
ELF          := $(BUILD_DIR)/$(TARGET).elf
BIN          := $(BUILD_DIR)/$(TARGET).bin
EXE          := $(BUILD_DIR)/$(TARGET).exe

# -- Collect source files --
# Top-level assembly: header.s (the PS-X EXE header, linked as .data)
S_FILES      := $(wildcard $(ASM_DIR)/*.s)
S_O_FILES    := $(patsubst $(ASM_DIR)/%.s,$(BUILD_DIR)/$(ASM_DIR)/%.o,$(S_FILES))

# C source files (decompiled functions), found recursively. A TU id is the path
# under src/ without .c (`main/tex`, `main/psxsdk/libcomb/comb`); its object is
# build/src/<id>.o, the path bb2.ld links.
C_FILES      := $(sort $(shell find $(SRC_DIR) -name '*.c'))
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
.PHONY: all clean check clean-check shift movovl modern test checks

all: check

# Build and verify match
check: $(EXE) $(CONFIG_DIR)/$(TARGET).sha1
	@sha1sum -c $(CONFIG_DIR)/$(TARGET).sha1 || { echo "MISMATCH: $(TARGET) does not match"; exit 1; }
	@echo "OK: $(TARGET) matches!"

# Force a clean rebuild before checking the final hash.
clean-check:
	$(MAKE) clean
	$(MAKE) check

# The movie overlay (disc/STR/MOVOVL.EXE) is a separate program with its own
# Makefile and SHA1. Not part of `check`.
movovl:
	$(MAKE) -C movovl clean
	$(MAKE) -C movovl check

# Shift test (non-matching): relink the objects with padding into build/shift/
# and report address assumptions (tools/shift_test.py). Never writes the EXE.
shift: $(ELF)
	python3 tools/shift_test.py

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

# Every check GitHub Actions runs, in its order, with a summary
# (tools/checks.sh): the counts, the lint, modern, test, the game's bytes,
# the overlay's, shift.
checks:
	bash tools/checks.sh

# -- SHA1 checksum file --
$(CONFIG_DIR)/$(TARGET).sha1:
	@echo "62efab4f73f992798c43e8c730aa43baa10bb4fa  $(EXE)" > $@

# -- Link --
$(ELF): $(ALL_O_FILES) $(TARGET).ld $(CONFIG_DIR)/named_syms.txt
	$(LD) $(LD_FLAGS) -Map $(BUILD_DIR)/$(TARGET).map -T $(TARGET).ld \
		-T $(CONFIG_DIR)/named_syms.txt -o $@

# Extract raw binary from ELF
$(BIN): $(ELF)
	$(OBJCOPY) -O binary -j .main $< $@

# Construct PS-X EXE: the linked .header (asm/header.s) with pc0 / t_addr / t_size
# computed from the ELF (tools/make_psexe.py), then the binary
$(EXE): $(BIN) $(ELF) tools/make_psexe.py tools/elf32.py
	python3 tools/make_psexe.py $(ELF) $< $@

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
$(foreach l,$(FLAG_LISTS),$(foreach f,$($(l)),$(if $(filter $(f),$(C_IDS)),,$(error $(l) names '$(f)', which is not a TU (no $(SRC_DIR)/$(f).c)))))

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

# -- Clean --
# build/play/ is kept: it holds a disc image built for a play test
# (tools/build_disc.py), which takes minutes to make again.
clean:
	[ ! -d $(BUILD_DIR) ] || find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name play -exec rm -rf {} +
