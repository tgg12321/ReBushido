# Source style

The goal is source a programmer can read as if the original team had handed it
over, that still builds the original executable byte for byte. `.clang-format`
settles layout; this page settles everything else.

## The gates

`wsl bash tools/build.sh check` must report the oracle SHA1 (and
`wsl bash tools/build.sh movovl` the overlay's, when `movovl/` or Sony's code
changed). A change that keeps the bytes and reads better is good; a change
that breaks the bytes is not made. Two scripts that need no build keep the
tree from slipping back: `python tools/count.py --check` (the `FAKE`
notes and `goto`s agree with `docs/FAKES.md`) and
`python tools/lint.py --check` (no count of the checklist below went
up).

## Names

- A name says what the thing is **for** in the game, at the level a designer or
  programmer on the team would have used: `memcard_LoadScreen`, not a list of
  what the function does. Aim for 30 characters or fewer.
- Interpretation is expected. If the evidence supports a reading, use it. If it
  is a guess, name it anyway and say so in a comment at the definition; a wrong
  guess is cheap to fix, an unreadable name is paid for on every read.
- The test for a name is: would it mislead a reader? Not: is every word proven?
- Functions: `subsystem_VerbNoun`. Globals: `g_subsystem_noun`. Types:
  `PascalCase` with no addresses in them. Constants and enum values:
  `UPPER_SNAKE`. Sony code keeps Sony's names.
- Identifiers are in English. A Japanese word of the game's own (`seri`,
  `karami`, the move kinds' labels) goes in the comment at the definition,
  not in the name: `MOVE_KIND_LOCK, /* "SERI" (tsubazeriai): blades locked */`.
  Proper nouns stay: fighters, clans, places, weapons, the chanbara mode,
  the kaishaku; so do words English has taken in (dojo, kabuki).
- A member nobody understands yet stays `unkNN`; do not invent a long
  descriptive placeholder for it. A variable is never named by its address:
  one whose purpose cannot be told gets its subsystem's prefix and what is
  known (`g_snd_unused_init_halfword`: cleared at init, never read;
  `g_select_unused_word_a`: named by no code), never a made-up purpose, and
  a comment at the definition that says who writes it and marks a guess.

## Structure

- One subsystem per file or small group of files, named for the subsystem
  (`memcard.c`, `memcard_save.c`). A file's top comment says what it is, in a
  sentence or two.
- Each subsystem has a header with its types, enums and prototypes; what goes
  in it and what stays in the `.c` file is rules 7, 10 and 11 below.
- A header declares an array with its count (`extern s16 g_eff_dust_ages[16];`),
  except one of eight bytes or fewer that its users reach by full address:
  told the size, the compiler would reach it through `$gp`.
- Numbers with a meaning get a name: SDK constants use Sony's names, game
  values get an enum or `#define` next to their subsystem.
- Prefer the natural form (`for`, `switch` with named cases, early return). Try
  it, build, keep it if the bytes survive. If they do not after a couple of
  attempts, keep the shape that matches and move on.

## Comments

- Comments explain the game: what a function is for, what a state means, why a
  value is what it is. No addresses, scores, review or ruling references,
  compiler-pass names, or history. History is in git.
- Code that is shaped only to make the compiler emit the right bytes gets a
  one-line `/* FAKE: ... */` saying what a reader should mentally replace it
  with (`/* FAKE: used / CARD_BLOCK_SIZE, written out */`).

## Checklist

What a file is held to, one line a rule. The name in brackets is the count
of `python tools/lint.py` that measures it; a rule with none is
checked by reading. `python tools/lint.py --file <file>` lists a
file's findings; `--check` fails if a count went up.

**Names**

1. A local or parameter is named for what it is (`fighter`, `attacker`,
   `slot_index`), never `p`, `s`, `tmp`, `val`, `ptr`, `ret`, `flag`, `arg0`,
   `var_7`. Fine as they are: loop counters `i j k`, coordinates and sizes
   `x y z w h`, `u v` together, `r g b` together. [names]
2. A local the bytes force to do several jobs is `work`; one that only takes
   up stack is `unused`; each with a comment saying so.
3. One spelling: `index` (not `idx`), `count` (not `cnt`, `num`, `nb`),
   `table`, `matrix`, `angle`, `flag`; American spelling in identifiers
   (`color`, `gray`, `center`, `armor`, `canceled`, `-ize`). `pos`, `len`,
   `buf`, `src`, `dst` and their long forms are both in use and both stay.
   How many of a thing there are is `THING_COUNT` or the plural
   (`STAGE_CELLS`); `_MAX` is the most a value can be, `_SIZE` bytes or a
   buffer's entries; `_WIDTH` and `_HEIGHT`, not `_W` and `_H`. A place in
   a 2D screen's ordering table is a `..._LAYER_...`. In comments the mode
   is chanbara (the menu's "Slash") and the two sides are clans. [spelling]

**Numbers**

4. A value that is one concept used in several places has a constant or an
   enum; a one-off tuning value has a comment in the game's terms on its
   line or the line above. What is not understood stays a bare number, with
   a comment saying so. [hex, dec]
5. An angle is written in turns (`ANGLE_TURN / 4`) and wrapped with
   `& ANGLE_MASK`; a fixed-point value shows its unit (`ONE`,
   `8 * FIGHTER_SPEED_ONE`).
6. Decimal for counts, indices, frames, distances and screen coordinates;
   hex for masks, flags, fixed-point, colours and hardware values. Hex digits are
   upper-case; a `U` suffix only where the bytes need it. [lower-hex]
7. The values of one field are an `enum`; a lone constant or a bit flag is a
   `#define`. Either is in the subsystem's header if another file uses it,
   else at the top of the `.c` file. A constant or macro two files need is
   defined once, in a header. A `.c` file's own constant whose bare name
   another file also has, or that begins with another subsystem's prefix
   and could be taken for that subsystem's, carries its file's prefix
   (`MENU_LAYER_...` and `SELECT_LAYER_...`, `SELECT_STAGE_...`).

**Types and tests**

8. `s8` to `u32` from `common.h`; `char` for text only; no `int`, `short`,
   `long`, `unsigned`. [c-types]
9. A flag, a pointer, a mask test and a call that answers yes or no are
   tested bare (`if (fighter->airborne)`, `if (!node)`,
   `if (flags & X)`); a count, an index, a timer or a state is compared
   (`timer == 0`, `state != STATE_IDLE`). A pointer is `NULL`, never 0.

**Declarations**

10. Everything with external linkage is declared once, with `extern`, in
    the header its name's prefix names (`cpu_...` and `g_cpu_...` in
    `cpu.h`). What only its own file uses, function or variable, is
    `static` there and in no header. The few the bytes do not allow that
    for (a variable without an initial value, which the linker script
    places by name; something the assembly names) stay declared in the
    header, at its end under `/* Used only inside <file>.c. */` where a
    header collects them. [proto-plain, wrong-header]
11. A `.c` file has no `extern` and no prototype, except the label of a
    listing and a `static` function or variable declared ahead of its use
    (under `/* Defined further down. */`). [extern-c, proto-c]
12. A prototype names its parameters, and has `(void)` for none. `()` only
    where the bytes need a call that does not fit the definition, with a
    comment. [unnamed-param, empty-params]
13. A parameter the function does not use stays (the callers pass it) and
    the function's comment says it is not used. A function that returns
    nothing is `void`; where the bytes need otherwise the comment says so.
    [unused-param, no-return]
14. An old-style definition (`f(a) int a; {`) only where the bytes need
    it, with a note. [old-style]

**Layout**

15. Braces on every `if`, `else`, `for` and `while`. [no-braces]
16. A `.c` file includes `"common.h"`, `"include_asm.h"` if it has
    assembly, Sony's headers that `bb2.h` lacks (the GTE macros), then
    `"bb2.h"`, and nothing `bb2.h` already includes. [includes]
17. A header is: `#ifndef NAME_H`, `#define NAME_H`, the comment saying what
    the subsystem is, includes, its contents, `#endif /* NAME_H */`. The
    contents are grouped by theme (under `/* ---- theme ---- */` where a
    header has several), and in a group the constants and types come ahead
    of the variables and functions that use them. [guard, top-comment]
18. A table's initialiser uses the enums and flags of its fields, in columns
    under a comment that names them (`clang-format off`/`on` around it),
    its lines within 80 columns like the rest.

**Comments**

19. A file starts with a comment saying what it is; a function of 12 lines
    or more has one saying what it does in the game's terms. [top-comment,
    func-comment]
20. `/* */` only; no addresses, no `TODO`-like marks, no commented-out code,
    nothing that restates the code. [cxx-comment, address, todo]
21. A member is `unkNN` (`unk_NN` in `Fighter`) only where nothing is
    known, and its comment says who writes and reads it. Bytes that are only there to fill up are
    `padNN`: the fourth of four where three are used, a stretch of a work
    area its function leaves alone. What the compiler would leave empty
    anyway (alignment) is not declared at all. [unk]
22. A suspected bug of the original is noted where it is with "the
    original's behaviour"; the code is not changed. (ReBushido fixes it
    behind a switch, below; the note stays.)

## ReBushido

On the `main` branch everything above holds, and these as well. The
whole of it, with examples, is [`REBUSHIDO.md`](REBUSHIDO.md), "Writing a
change".

23. Every change is behind a switch of its own, `RB_NAME`, defined in
    `include/rebushido.h` as `(REBUSHIDO && 1)` with a comment saying what
    the change is; a fix is `RB_FIX_...`. The source asks `#if RB_NAME`,
    never `#ifdef`, and only switches go in such an `#if`. (A developer's
    build of its own, like quick boot, is `(REBUSHIDO && RB_NAME_BUILD)`:
    off unless the build gives that name.) [rb-flag]
24. The original's lines stay as they are, once: not deleted, not copied
    into ReBushido's side, not reworded. What is added goes in an `#if`;
    what is replaced goes in the `#if` and the original in its `#else`;
    what the player can turn off is
    `#if RB_NAME` / `if (g_rb_settings...) { ... } else` / `#endif` /
    `{ the original's lines }`.
25. Each side of an `#if RB_NAME` block is whole statements or
    declarations with its braces closed.
26. What the player can turn off asks `g_rb_settings` (`rb_settings.h`),
    whose defaults are the original's behaviour.
27. New code of any size is in `src/rebushido/`, its header
    `include/rb_<file>.h`; names are `rb_VerbNoun`, `g_rb_noun`, `RbType`,
    `RB_CONSTANT`. ReBushido's side has no `FAKE` note, and `bb2.ld` is not
    edited for it.
28. The native port ([`PORT.md`](PORT.md)) is behind `RB_PORT`. Where a
    function is assembly, its C goes in the `#if RB_PORT` and the
    `INCLUDE_ASM` line in the `#else`, with a comment on the C that
    begins "In C" and says what a reader of the listing would not
    expect. Code that only the port has is in `src/port/`, its header
    `include/port_<file>.h`, its names `port_VerbNoun`, `g_port_noun`,
    `PortType`, `PORT_CONSTANT`: no PlayStation build compiles it.
29. An address has two forms that are written into the original's own
    lines, because the port's pointers are 64 bits wide and the
    PlayStation's 32 ([`PORT.md`](PORT.md), "64 bits"); on the
    PlayStation each is the plain expression, so no byte changes
    (`include/common.h`):
    - a number cast to a pointer has its operand in `ADDR()`:
      `(Fighter *)ADDR(value)`. A pointer cast to a number has nothing;
    - a pointer kept in memory whose layout is fixed (a file's, the
      scratchpad's, the arena's) is declared `PTR32(type) name;`, read
      as `PTR(slot)` and written with `SET_PTR(slot, value)`
      (`NEXT_PTR(slot)` for `slot++`, `ADD_PTR(slot, count)` for
      `slot += count`). Where two declarations name the same slot it has
      a typedef, `ThingSlot`.

    A structure that keeps ordinary pointers grows in the port, and is
    listed in `config/port_layout.txt` with why it may. `make port`
    fails on a cast without `ADDR()`, a slot without `PTR()`, and a
    structure that grew and is not listed.

## Working method

`tools/lint.py` counts the checklist; its baseline
(`tools/lint_baseline.tsv`) is updated with `--update` in the commit
that brings a count down. What the baseline still holds is summed up in
[`STATUS.md`](STATUS.md), "The counts"; `python tools/lint.py --by-file` lists it.

`tools/` has the helpers: `rename.py` applies a whole-word rename map across
everything the build reads, `objdiff.py` reports which functions of an object
changed against a copy kept from a matching build ([`MATCHING.md`](MATCHING.md),
"The loop"). Work a subsystem at a time: read all of it, decide
what it is, name downward from that, then tidy the control flow.
