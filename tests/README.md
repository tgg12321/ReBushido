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
  behaviour and goes back to it. `rebushido/options` holds the options
  page's table to the page and the file: every setting has a row and a
  place of its own, its name and choices fit, the cursor and the choices
  go round. `rebushido/save` packs the settings' file and reads it back,
  checks its header and checksum against Python's `zlib` and Shift-JIS,
  and that a spoiled file, another version's or an empty one is refused
  with the settings untouched. `rebushido/quickboot` runs quick boot's
  one function with the game stood in for: the chosen fight is asked for
  and its mode started once. `rebushido/play_state` holds the offsets the
  play tests read the fighters at (`tests/play/state.toml`) to the
  structures of `include/`.
- A test of one of ReBushido's tools is Python alone:
  `tests/rebushido/NAME.py` with no `NAME.c`, and a function
  `run(checks)`. `rebushido/patch` is the release's patch tool
  (`tools/rebushido_patch.py`): the format's numbers, a patch written by
  hand with every kind of action, round trips of patches it makes, and a
  damaged patch, a lying one and a wrong source refused. Its bytes are
  made up in the test; it reads nothing of the game. `rebushido/patcher`
  is the players' patcher (`tools/patcher/patcher.c`), which is C for
  the player's machine: the test builds it with the host's `cc`, as
  `make patcher` does, into `build/tests/patcher/`, runs it on patches
  the tool makes and on ones written by hand, and compares what it
  writes, and what it refuses and with which exit code, with the tool.
  Made-up bytes again.
- A test of one of ReBushido's fixes is `tests/rebushido/fix_NAME.c`: it
  includes the game's file that has the fix and runs the function with
  the "Bugfixes" setting off, for what the original does, and on
  ([`docs/REBUSHIDO.md`](../docs/REBUSHIDO.md), "The fixes"). There are
  five: the boot image's fade, where the vanisher comes back, the size
  the team battle's counters return, and a splash and a loose object
  when their tables are full. The last two run only the fixed side at
  the full table: there the original writes outside it.
- The game's tests are linked with `src/rebushido/settings.c` in this run,
  the settings as they are when the game starts. A file of the game's whose
  change asks a setting therefore still has to compute what the original
  does: the same answers as in the first run.
- The programs are in `build/rebushido/tests/`.

## The native port

The same run, `--rebushido`, has the port's switch on as well (`RB_PORT`,
[`docs/PORT.md`](../docs/PORT.md)): a function that is assembly on the
PlayStation is the C the port has beside it, and Sony's GTE macros are C
on the port's GTE (`src/port/gte.c`), which the game's tests are linked
with in this run. So `roots`, whose `math_TableSqrt` counts leading
zeroes on the GTE, runs on the model there and on `stubs/gte.c` in the
plain run, and has to give the same answers.

The port's own tests are `tests/port/NAME.c`, named `port/NAME`:

| Test | What it runs | Compared with |
|---|---|---|
| `port/gte` | every operation of `src/port/gte.c` and its registers | cases worked out by hand: each limit and its bit of FLAG, the queues, the registers that read back otherwise than written; the division against true division, for every divisor of sixteen dividends |
| `port/asm` | the C beside the assembly: the game's `math_asm.c` and `math_RotMatrixZYX`, Sony's libgte | real square roots; rotations made from real sines, and for Sony's from Sony's table of sines; products and scalings in whole numbers; a blend of two rotations is a rotation; cases by hand |
| `port/gte_vectors` | the GTE on the probe's 84,000 made-up cases | the numbers written down when the emulator's GTE last made the same of them |
| `port/asm_vectors` | the C beside the assembly on the probe's 32,000 | the numbers written down when the assembly itself last made the same of them, on the emulator |
| `port/render_vectors` | the renderer's C on the probe's 9,800 made-up meshes and queues | the numbers written down when the renderer's listings last left the same in memory and in the GTE, on the emulator |
| `port/disc` | the reader of a disc image (`src/port/disc.c`) and Sony's libcd on top of it (`src/port/cd.c`) | an image of forty sectors made up in memory, none of the game's: positions to sectors and back, the image by its `.bin` and by its `.cue`, every sector's bytes, data across sectors, a sector of sound, one with no sync, one that says it is another, the end of the image; then a read as the game's disc code asks for one, sector by sector in a callback; the drive's place as sound plays, by the sound's clock, and which sectors it plays |
| `port/platform` | the port's clock, pads, events, primitives and printf (`src/port/`) | cases by hand: `VSync` as Sony's waits, the frame's wait and its count of `rand()` calls, a pad's packet, an event set and asked, the words of a mode, area and offset primitive, formats and what they print |
| `port/spu` | the sound chip (`src/port/spu.c`) on made-up waveforms and registers, and the reverb on the settings of Sony's nine modes | cases by hand (a block's samples, the envelope's steps, the keys and the ended bits sample by sample, the mixer's limits), and a second decoder, interpolation and envelope in Python written from the rules as documented; the interpolation's table against what such a table must be; a click through each reverb mode comes back, never louder, and dies away |
| `port/xa` | the disc's sound (`src/port/xa.c`) on made-up sectors in each of the format's forms | cases by hand, and a second decoder in Python written from the format; a straight line through the change of rate stays one; the drive's four volumes |
| `port/raster` | the PlayStation's GPU in C (`src/port/raster.c`), Sony's libgpu on it (`gpu.c`) and the pictures it writes (`picture.c`) | pixels worked out by hand, and the fill rule said a second way: a triangle's ten pixels; 300 made-up triangles and 300 four-sided primitives, the second drawn by adding, so that a pixel left out or drawn twice along a shared edge shows; the limits of size, the drawing area, the offset; each texture depth, the texture window, a texel times a colour; the four mixes and the texels they apply to; the mask bit set and looked at by each kind of primitive; the dither's pattern typed again, on what it applies to and what it does not; shading across, down and over a four-sided primitive; twelve bits of fraction on a triangle 1023 across; lines, of one piece and of many points; the fill and the three copies; an ordering table walked, a drawing and a display environment put, `ClearImage`, `LoadImage`, `StoreImage`, `MoveImage`; a PNG read back chunk by chunk, its sums done a bit at a time |
| `port/memory` | the list of fixed addresses in `src/port/memory.c` | the game's source itself: every address written into it is memory the port maps, above the program, and in the list |

`port/gte_vectors`, `asm_vectors` and `render_vectors` are the host's half of the play test `gte_model`
([`play/README.md`](play/README.md), "The GTE probe"): the emulator is
not there on every push, so what is held is that the C still gives what
it gave when the emulator last agreed. A test's Python side may name
more source files to compile apart and link (`sources()`); those three and
`port/asm` do, for Sony's modules, each of which is a file of its own.

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

- Assembly as assembly: the plain run has no body for a function that
  is a listing. With `--rebushido` each is run as the C the port has
  beside it, the renderer too: "The native port".
- What works through the GTE: the blade tests of `hit_test.c`,
  `math_MatrixToAngles`, `math_TurnAngles`, `math_ScaleAlong`,
  `math_TriangleHeading`, the cloth's step. With the port's GTE they can
  be run now; no test of them is written yet.
- `math_Grayscale3` (in `tex.c`, see above); `math_LobVelocity` (its root is
  Sony's table-driven `SquareRoot0`, which is assembly: the answer would be
  the stand-in's).
- Anything with a screen, a pad, the sound chip or the drive in it.
- A save file the retail game wrote: none was at hand. The header is checked
  field by field instead.
