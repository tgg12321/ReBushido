# tests/

Tests of what the game's C computes. The build's check is that the bytes
match; these run the game's own functions on the host and compare what they
return with things known some other way: real maths, cases made by hand, and
the disc's own data. They are for after a change: a mod that touches the
motion decoder, the save record or a table can see at once whether it still
does what it did.

```
make test                                  # in WSL or on Linux
wsl bash tools/build.sh test               # from Windows
wsl bash tools/build.sh test -v roots      # one test, showing what it printed
python3 tests/run.py --list
```

It prints a line for each test and exits 0 if none failed:

```
ok    arena                 39 checks  The arena's bookkeeping (arena.c) through a script of opens and frees.
...
skip  motion_decode    no disc/MOTION
OK: tests: 11 passed, 2 skipped, 0 failed
```

It needs the host's `gcc` (x86, with `-m32`) and `python3`, about twenty
seconds, and nothing else: no build of the game, no 32-bit C library, no
emulator. The tests that read the disc are skipped, saying so, where it is
not there.

## ReBushido

On the `main` branch the same tests run a second time with ReBushido's
master switch on (`include/rebushido.h`), together with ReBushido's own:

```
make rebushido-test                                    # in WSL or on Linux
python3 tests/run.py --rebushido -v rebushido/settings # one, showing what it printed
```

- A test of ReBushido's own is `tests/rebushido/NAME.c` (and `NAME.py`),
  named `rebushido/NAME`, made like any other; it includes the file of
  `src/rebushido/` it tests. It runs only with `--rebushido`.
  `rebushido/settings` checks that every setting starts as the original's
  behaviour and goes back to it. `rebushido/quickboot` runs quick boot's
  one function with the game stood in for: the chosen fight is asked for
  and its mode started once. `rebushido/play_state` holds the offsets the
  play tests read the fighters at (`tests/play/state.toml`) to the
  structures of `include/`.
- The game's tests are linked with `src/rebushido/settings.c` in this run,
  the settings as they are when the game starts. A file of the game's whose
  change asks a setting therefore still has to compute what the original
  does: the same answers as in the first run.
- The programs are in `build/rebushido/tests/`.

## Play tests

These tests run the game's functions, never the game. The tests that run
the game itself, in an emulator with no window, are in
[`play/`](play/README.md): `make playtest`. They need your own retail
image and the emulator, so they are run on the developer's machine and
are not among the checks.

## The tests

| Test | What it runs | Compared with |
|---|---|---|
| `sin_table` | `g_math_sin_table` (`math_sin.c`) | `math.sin`: every entry is the sine with its fraction dropped |
| `roots` | the three root tables (`fight_data.c`, `math_asm.c`) and `math_TableSqrt` (`math.h`) on 134,000 numbers | `math.isqrt`: the tables exact, the function never above the real root nor a 32nd below it |
| `rotation` | `math_RotMatrixZXY`, `YXZ`, `XYZ`, `math_Transpose`, `math_MatrixToAnglesYXZ`, `math_Rotate2D` (`math.c`) on 1,331 triples of angles | the same rotations in floating point; the angles read back make the same matrix |
| `color` | `math_HsvToRgb`, `math_RgbToHsv` (`math.c`) | Python's `colorsys` |
| `geometry` | `math_PointInTriangleXZ`, `math_InvertMatrix` (`hit_test.c`) | exact arithmetic: which side of each edge; matrix times inverse is one |
| `random` | `rng_Next`, `math_FovToScreenDist` (`main.c`), Sony's `rand` | the rule in `main.c`'s comment, worked out in Python; the tangent; the C standard's example sequence |
| `bitstream` | `bitstream_ReadBits`, the tracks (`bitstream.c`) | a bit reader in Python on the same words |
| `arena` | `arena.c` through opens, a resize, a free and a reopen | where each block and its bytes should be, by hand |
| `save_record` | `memcard_PackSaveData`, `memcard_UnpackSaveData`, `memcard_BuildFileHeader` (`memcard_save.c`) | the record back whole, a spoiled copy passed over, patches applied only inside RAM; the title decoded from Shift-JIS |
| `ndata` | the archive's names, directory, readers and packs (`ndata.c`) | `docs/formats/ndata_filemap.csv`; the disc's `NDATA.INF`; the packed models against their own entries of `NDATA.DAT` (disc) |
| `cd_table` | `g_cdrom_files`, `cdrom_StartRead`, `cdrom_StartReadAt` (`cdrom.c`), Sony's `CdIntToPos` and `CdPosToInt` | the directory of the disc's image, by the name in each entry's comment (disc) |
| `motion_decode` | `motion_OpenStream`, `motion_DecodeFrame` (`motion_decode.c`) on all 72,000 frames of `disc/MOTION/*.BBM`, in order, backwards and by leaps | a second decoder, in Python, written from `docs/formats/BBM.md` (disc; skipped without it) |
| `cloth` | `cloth_SetupModel` (`cloth_model.c`) and `cloth_Create` (`cloth.c`) on the 34 cloths of the 37 models, each with its bone turned eight ways | the springs worked out in Python from the model files; no point has more than it has room for (disc; skipped without it) |

"Disc" is `disc/` as `tools/extract_iso.py` writes it and, for what is
inside `NDATA.DAT` or is the disc's directory, the image
`Bushido Blade 2 (USA).bin` in the checkout (it is only read). `BB2_DISC` and
`BB2_DISC_IMAGE` name other places.

## How a test is made

A test is `tests/NAME.c`, and beside it `tests/NAME.py` if its answers come
from somewhere else than the C itself.

**The C side** includes the game's own source file, not a copy, so it can
call the file's `static` functions too, then the runtime, and defines
`test_Main`:

```c
/* The sine table (math_sin.c) against the real sine. */
#include "src/main/math_sin.c"
#include "runtime/runtime.h"

void test_Main(void) {
    s32 angle;

    for (angle = 0; angle < ANGLE_TURN; angle++) {
        test_Print("%d\n", g_math_sin_table[angle]);
    }
    CHECK_EQ(g_math_sin_table[ANGLE_TURN / 4], ONE);
}
```

`CHECK(condition)` and `CHECK_EQ(got, want)` count a check and print a line
if it fails. Everything else the program prints with `test_Print` is handed
to the Python side. The file's first line, a comment, is what `run.py`
prints beside the test's name.

**The Python side** may define two functions:

```python
def arguments():        # the program's arguments; may raise support.Skip
    return [support.disc_file('MOTION', 'COMMON.BBM')]

def check(lines, checks):   # the lines the program printed
    for angle, line in enumerate(lines):
        checks.near('sin(%d)' % angle, int(line), 4096 * math.sin(...), 1)
```

`checks` has `equal`, `near`, `true` and `note` (`tests/support.py`).
`support` also finds the disc's files (`disc_file`, `disc_image`,
`image_files`, `ndata_entry`), raising `Skip` when they are not there, and
writes a file for the program to read (`work_file`).

**What the tested file calls** and the test does not have: only the
functions the test reaches are kept in the program (each function is in a
section of its own and the linker drops the unused ones), so a test needs
stand-ins only for what those reach. Include another of the game's files if
it is plain C (`math.c` wants `math_sin.c` for the table); Sony's routines
that are C can be included too (`src/main/psxsdk/libgte/ratan.c`). For
hardware, assembly and the GTE there are stand-ins, each saying what it
stands for:

- `stubs/gte.c`: the GTE as far as it is needed: its count of leading zeros
  (`math_TableSqrt`). Any other GTE operation fails the test, by name.
- `stubs/libgte.c`: Sony's `ApplyMatrix`, `MulMatrix`, `RotTrans`,
  `Square12`, `SquareRoot0`, `SquareRoot12` as plain arithmetic.
- `stubs/sys.c`: the game's `sys_Trap` (counted, so a test can say the game
  would have stopped) and `mem_MoveWords`.
- a one-line stand-in particular to a test (`DrawSync`, `tex_IsGrayscale`,
  the CD) is in the test itself.

Do not change the game's source to make it testable. If a function cannot
be reached without hardware, it is not a candidate.

## The runtime, and why the tests are 32-bit

The game keeps addresses in `s32` everywhere (`arena_OpenBlock`, the CD's
destinations, the scratchpad at `0x1F800000`), so its C only means the same
thing compiled with 32-bit pointers: the tests are built with `gcc -m32`.
A 32-bit C library is usually not installed (it is not in this project's
WSL, nor on GitHub's runners), so the tests use none: each is a static
program linked with `tests/runtime/runtime.c` and nothing else (`-nostdlib
-ffreestanding`). The runtime is about 300 lines: `_start`, and on the Linux
system calls (`int $0x80`) a `test_Print` that knows `%d %u %x %s %c`,
the check counters, `test_ReadFile`, `test_Alloc`, `test_AllocAt` (memory at
an address of the test's choosing: the scratchpad, a page of the
PlayStation's RAM), and `memcpy` and its like, which the compiler calls.
Nothing in it divides a 64-bit number, which would need the compiler's own
32-bit library.

The source is compiled as `tools/modern/check.py` compiles it for its
warnings: the same shim (`tools/modern/shim.h`: a function that is assembly
has no body) and the same rewritten GTE headers (every GTE macro a call of
`modern_gte`, which `stubs/gte.c` defines), plus `libsn.h` with the one
MIPS instruction the game's C spells out (`pollhost`) taken away. `-fwrapv`
makes the arithmetic wrap as the PlayStation's does. The programs and the
headers are in `build/tests/`.

Two things follow. `tex.c` cannot be included (it assigns to a cast, which
`check.py` rewrites and a plain `#include` does not). And a test shows what
the C means to a modern compiler on x86, not what GCC 2.7.2 made of it for
MIPS: the SHA1 is the check of that.

## What is not tested

- Assembly: `math_Sqrt`, `math_Length`, `math_MulMatrix`, `math_Transform`,
  `math_LerpRotation`, `mem_MoveWords`, the renderer.
- What works through the GTE: the blade tests of `hit_test.c`,
  `math_MatrixToAngles`, `math_TurnAngles`, `math_ScaleAlong`,
  `math_TriangleHeading`, the cloth's step. A GTE written out in C would be
  the thing under test.
- `math_Grayscale3` (in `tex.c`, see above); `math_LobVelocity` (its root is
  Sony's table-driven `SquareRoot0`, which is assembly: the answer would be
  the stand-in's).
- Anything with a screen, a pad, the sound chip or the drive in it.
- A save file the retail game wrote: none was at hand. The header is checked
  field by field instead.
