# The compiler

The game was compiled with GCC 2.7.2 as SN Systems shipped it in Sony's PsyQ
SDK (`cc1psx.exe`, a DOS program that calls itself `2.7.2.SN.1`) and assembled
with ASPSX 2.34. This build uses open-source stand-ins for both: GCC 2.7.2
built from source, and maspsx (`tools/maspsx/`) in front of GNU `as`.

## What is built

```
source : https://github.com/decompals/mips-gcc-2.7.2
commit : 43d1cdb67ed135879869b5266f01efaaada5e35a
patch  : tools/cc1-reorg-negate-rtx-decl.patch
build  : bash tools/build_cc1.sh        ->  tools/gcc-2.7.2/build/cc1
```

`tools/build_cc1.sh` clones the source at that commit, applies the patch,
configures for `mips-mips-gnu` and builds `cc1`. `tools/gcc-2.7.2/` is not in
git. The `Makefile` runs `tools/gcc-2.7.2/build/cc1` and nothing else from
that folder.

**The patch** adds one declaration to `reorg.c`. `negate_rtx` returns a
pointer and `reorg.c` calls it without a declaration, so on a 64-bit host the
pointer is cut to 32 bits and the compiler crashes while filling delay slots
on a few of this project's files. Declaring it changes nothing the compiler
emits. No other change is made to the compiler: the code has to match with
GCC 2.7.2 as it was released.

**How it is compiled.** Every file of the compiler is built without
optimisation (`-g`) except `combine.c`, which gets `-O`; `-fgnu89-inline` is
needed for it to link with a modern host compiler. This is the build the
whole project was matched with, so it is the build that is reproduced. The
binary itself differs from one build to the next (the host toolchain leaves
its marks); what has to be the same is the assembly it emits. A compiler
built by `tools/build_cc1.sh` from a fresh clone emits the same assembly as
the project's long-standing one for every source file (checked 2026-11-11,
280 files), and the full build's SHA1 is the standing check.

**The flags.** The stand-in's target, `mips-mips-gnu`, defaults to
big-endian and hardware floating point. The PlayStation is little-endian
with no FPU, and `cc1psx` reports `-msoft-float` as its default, so `-mel`
and `-msoft-float` are in `CC_FLAGS` and must stay there: without `-mel`
bitfields, unaligned loads and stack slots come out differently, and without
`-msoft-float` loops are optimised differently (the floating-point registers
count as available).

## Sony's compiler

The real `cc1psx.exe` (`CC1PSX.EXE` from the PsyQ SDK) was run under dosemu2
during the decompilation, reading preprocessed C and writing assembly like
`cc1`, to compare its output with ours. The program is Sony's and is not in
the repository; the build never uses it.

The libraries linked into the game are PsyQ 4.0-era: all 175 library modules
that are in the executable unchanged match PsyQ 4.0's byte for byte, and 79 of
them match no 3.3, 3.5 or 3.6 library. That dates the libraries, not the
compiler.

## One open question

GCC 2.7.2 rewrites `a + b` as `a | b` when it can prove the two share no
set bits. Whether the compiler the game was built with did that everywhere
was studied in September 2026 and not settled: most places in the original
agree with stock GCC, including some where only the rewrite produces the
original's `ori`, but at one place found then the original had `andi` then
`addiu` where stock GCC, and `cc1psx` as well, emitted `ori` for every way
of writing the C that was tried.

A compiler changed to never do the rewrite, and one changed to skip it only
for register-plus-constant sums, were both used for a time and both dropped:
neither is a compiler that existed, and with a changed compiler anything can
be made to match. Every function has since been matched with the stock
compiler. The study, with the places and the measurements, is kept in
the project's private archive and is not published.

## The assembler

ASPSX differs from GNU `as` in how it expands pseudo-instructions and in what
it reaches through `$gp`. maspsx rewrites the compiler's output so that GNU
`as` produces what ASPSX would have:

- `div` and `rem` with their zero and overflow checks, `lb`/`lh` and loads or
  stores to a symbol the way ASPSX 2.34 expands them, the `nop` after a
  multiply or divide result is read;
- **small data**: a file reaches a variable of 8 bytes or less through `$gp`
  only if the file itself defines it. Statics go to the file's own `.sbss`,
  small initialised variables to its `.sdata`. Sony's library code was built
  without small data and is built so here;
- uninitialised globals are common symbols, placed by the linker script.

Three functions needed something said by name, in the three small `.txt`
files at the repository root the `Makefile` passes to maspsx; each file says
what and why.
