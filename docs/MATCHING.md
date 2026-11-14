# Changing code and keeping the bytes

The build is only right when it reproduces the original executable, so every
edit to a `.c` file is checked against the bytes. This page is what to expect
from GCC 2.7.2 when you change how a function is written: what is free, what
moves the generated code, and how to find out what moved.

If you are changing what the game *does* (a mod), none of this applies: the
bytes will differ and that is the point. Read [`STYLE.md`](STYLE.md) instead.

## The loop

```bash
mkdir -p tmp/base                                  # from a build that matches:
cp build/src/main/fight.o build/src/main/cpu.o tmp/base/   # the objects of the files you will edit
# ... edit src/main/fight.c, src/main/cpu.c ...
bash tools/build.sh check                          # "OK: bb2 matches!", or MISMATCH
python3 tools/objdiff.py tmp/base/fight.o build/src/main/fight.o --summary   # which functions changed?
python3 tools/objdiff.py tmp/base/fight.o build/src/main/fight.o             # and how
```

`tools/objdiff.py` compares two objects function by function, ignoring the
differences that are not code (branch offsets, the name `objdump` prints for
a call). Change several things per build; when a function stops matching, put
that function back and keep the rest. Finish with
`bash tools/build.sh clean-check`.

`python3 tools/listings.py --write <function>...` writes the disassembly of
any function of the build into `build/reference/`, to read beside the C.

## What is free

These do not change the generated code:

- names of anything: functions, variables, types, members, labels, macros;
- comments and layout;
- moving a declaration to a header, or a type between headers;
- a named constant or enum value for a number (`sizeof(T)` for its size,
  `ARRAY_COUNT`, Sony's macro names);
- a cast between types of the same size and signedness;
- in most cases: `for` in place of a counted `do`/`while`; `switch (x)` with
  named cases in place of `switch (x - 4)`; `break` in place of a `goto` to
  just past the loop; dropping a local that only held a constant.

## What moves the code

- **The order locals are declared in** decides which register each one gets.
  It is the first thing to try when only register numbers differ, and the
  reason not to reorder declarations for tidiness.
- **Adding or removing a local**, or splitting one that is reused for two
  things, changes register assignment and often the stack frame.
- **Types.** The type of the destination picks the load (`lh` or `lhu`, `lb`
  or `lbu`); signedness picks `slti` or `sltiu`, `div` or `divu`, `sra` or
  `srl`. A header that declares a global with the wrong width or signedness
  makes every user of it wrong.
- **`/` and `>>`.** Signed division by a power of two has a rounding step
  (`bgez`, `addiu`) that a shift does not.
- **Statement and expression order.** The compiler schedules what it is
  given; `a + b + c` and `a + (b + c)` are different code.
- **Loop shape.** `do { } while (--i >= 0)` and `while (i--)` differ by a
  branch at the top. Counting down is common in the original.
- **Branch sense.** `if (c) return;` followed by the body, and
  `if (!c) { body }`, lay the blocks out differently.
- **Merging two `case` bodies** the original kept apart, or separating two it
  shared.
- **How a struct is declared.** Three scalars and a three-member struct are
  copied and addressed differently; so are a `Vec3` and three `s32`.
- **The number of arguments in a prototype.** A call with fewer arguments
  than the callee reads leaves out the code that sets them up.
- **Where a small global is defined.** A variable of 8 bytes or less is
  reached through `$gp` only in the file that defines it, so moving its
  definition to another file changes every access to it in both.

## Things that are not what they seem

- **`.rodata` and `.data` are part of the match.** A `switch` compiles to a
  jump table in `.rodata`: the code can be identical while the order of the
  cases in the table is not. Strings and tables sit in the order they are
  defined in, per file, and the files in the order `bb2.ld` lists them.
- **One function can move another.** Labels are numbered per file, and
  literals and small data are pooled per file, so `objdiff.py` can report
  more functions than the one you edited.
- **`/* FAKE: ... */`** marks code written a strange way because only that
  way gives the original's bytes (a `goto` where a loop would do, a value
  copied through an extra local, an expression written out). The comment
  says what to read it as. About 200 are left; each is a place where the
  authors' real source is still not known.
- **Old-style definitions** (`void f(a, b) int a; short b; { ... }`) are kept
  where the original must have had one: the parameters are promoted
  differently from a prototype's.
- **`INCLUDE_ASM`** functions and the inline `__asm__` for GTE operations
  were assembly in the original and have no C form.
- **Sony's library code** (`src/main/psxsdk/`) is built with different
  flags from the game (no small data); see the lists in the `Makefile`.

## When it will not match

Look at the difference before changing anything else: only register numbers
(declaration order, or which value a local is reused for), an instruction in
a different place (statement order, loop shape), an extra or missing
instruction (a type, a cast, a value reloaded where the original kept it), a
different stack frame size (a local too many or too few, or an array's
size). Two attempts, then leave the function in the shape that matches and
move on; a readable function that does not match is not an improvement here.

For the hard cases the tools the decompilation itself used are outside this
repository: [decomp-permuter](https://github.com/simonlindholm/decomp-permuter)
searches variations of a function for one that matches,
[m2c](https://github.com/matt-kempster/m2c) writes a first draft of C from a
listing (`tools/listings.py --write` makes one of any function), and the
compiler's own dumps (`cc1 -da`) show what each pass did.
