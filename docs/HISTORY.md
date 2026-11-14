# Project history

A short account of how the decompilation was done, for a reader who wonders
why something is the way it is. This repository begins at the finished,
cleaned tree. The detail (about 12,400 commits, the working documents and
tools of the passes described at the end of this page, and everything
about the process that was used until November 2026) is kept in
the project's private archive, which is not published.

It is a single-person project. Nearly all of the code was written by AI
coding agents working under that person's direction.

## Getting every function to match (March to October 2026)

**March.** The first commit is 2026-03-23: the executable split into
per-function listings with splat, a Makefile, and the first functions in C.
Within a week a few hundred functions matched, the handling of `$gp` and of
ASPSX's `lb` expansion had been added to maspsx, and about 350 functions
had names, many borrowed from *Kengo: Master of Bushido*, a later game by
the same studio that shipped with its symbols.

**April.** By 2026-04-27 no function was left as a bare listing, but that was
reached by letting two post-processing steps ("regfix" and "asmfix") rewrite
the compiler's assembly per function, and by writing whole functions as
inline assembly. Several hundred functions produced the right bytes without
being C. The goal was restated as *pure C*: no post-processing, and assembly
only where the original was assembly.

**May to August.** Paying that debt down. The rules for what counts as a
real match were written and tightened, in the direction of what other
PlayStation decompilations accept (sotn-decomp above all), and every
completed function was reviewed against them by a second agent. The functions
that were hand-written assembly in the original were identified and listed.
Sony's library code was recognised module by module (the libraries are PsyQ
4.0-era) and given Sony's names; where a matching C source for a module
existed in other projects it was adopted. The read-only data moved from
assembly into the C files, and `bb2.ld` became hand-maintained. A function
that did not match yet was kept as an included listing, never as a cheat.
The post-processing rules went to zero on 2026-08-25 and the machinery was
deleted.

Much of the leverage in this period came from making the build behave like
Sony's rather than from individual functions: the compiler had been running
big-endian and with hardware floating point (`-mel`, 2026-08-04, and
`-msoft-float`, 2026-09-07, fixed whole classes of mismatch), and the
compiler binary itself turned out to carry an undocumented patch, which was
removed ([`COMPILER.md`](COMPILER.md)). Models of GCC 2.7.2's register
allocator and instruction scheduler were built to predict what a change to
the C would do.

**September.** The last few hundred functions, worked by an automated
pipeline that kept a record per function and by focused manual sessions. How
small data is reached through `$gp` was settled as a rule about files (a file
uses `$gp` for what it defines), replacing per-symbol lists, and `.rodata`
alignment became one rule for every object.

**2026-10-02.** Every function done: 1,260 in C, the rest assembly because
the original was.

## Names and structure (October to November 2026)

With the code matching, the work turned to what it said. Function and data
names that could not be backed by evidence were reset and then renamed in
about 120 waves; the source files, until then named by address, were grouped;
Sony's code moved to `src/main/psxsdk/`, one file per library module; the
addresses the linker was told were removed one by one until three were left;
the game was run in an emulator with a tracing kit to see what code ran when.

That process demanded proof for every name, and the names it produced were
long descriptions of what the code did
(`fight_UpdateSpacingTurnFacingWeaponBodyHitsAndQueueMoveEvents`) rather
than what it was for. On **2026-11-10** it was stopped and archived, and
replaced by a plain rule: make the source read like something its authors
would recognise, keep the bytes ([`STYLE.md`](STYLE.md)). The old rules,
tools, naming evidence and runtime captures are at the tag above.

## The readability pass (November 2026)

A plan split the rest into steps, mostly one subsystem each: read the
whole subsystem, decide what it is, give it its own files and
header, name it from that understanding (`fight_RunKarami`), replace
magic numbers, and simplify control flow wherever the bytes survive. Guesses
are allowed and marked. After the subsystems came the data files, the
headers, Sony's code, a sweep over what was still shaped for the compiler,
the listings and symbol tables, the movie overlay (which until then built
only from assembly), and the tools and documents.

The names were then checked against the game running in an emulator, as
far as scripted runs could show, and the tree was read through once more. On
**2026-11-11** the plan was worked through but for the checks that need a
person playing, and the source was called 1.0.

## After 1.0 (November 2026)

Two more passes followed. The **accuracy pass** asked of every
line whether it is shown to be the authors' or only compiles to the same
bytes: the assembly, the places marked `FAKE`, the `goto`s, the casts, the
layout of the variables, the boundaries of the original's files, the names
([`FAKES.md`](FAKES.md)). The **polish pass** began when a read
of `cpu.c` found a pointer called `p` and bare numbers: the source was held
to a checklist ([`STYLE.md`](STYLE.md)), a lint was written to count it,
every file was gone through one group at a time and then read once more
from start to end, and the stages and fighters were named from the game's
own pictures.

Then came tests that need no emulator: `make modern`, which gives the game's
C to a modern compiler for its warnings and its analyser, and `make test`,
which builds the game's pure functions for the host and compares what they
compute with real maths, hand-made cases and the disc's own data. A build
with everything moved was run beside the original in an emulator and then
played by hand. With the two byte checks, the counts, the lint and
`make shift` that made the seven checks that run on every push.

## The clean-up (November 2026)

With the work done, the repository was made ready for outside readers:
everything that only served the decompilation was taken out of `main`. That
was the plan and its notes (`docs/PLAN.md`, about 4,600 lines: what each step
found), the record of the names checked in the running game
(`docs/RUNTIME_CHECK.md`) and the page to play from (`docs/PLAYTEST.md`),
the listing of every function that is C (`asm/reference/`, 1,346 files), the
emulator scripts (`tools/emu_*.py`), the rename maps and snapshot helpers of
`tools/readable/`, and the configuration of the splitter the project began
with (`splat.yaml`). All of it is kept in the private archive.
What an outside reader needs from those documents was folded into
[`STATUS.md`](STATUS.md).

The cleaned tree then became the first commit of this repository.
`main` stays the byte-matching decompilation. A modded line of the game,
ReBushido, lives on a branch of its own, `rebushido`.

[`STATUS.md`](STATUS.md) says where things stand.
