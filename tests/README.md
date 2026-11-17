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
| `port/card` | the memory card (`src/port/card.c`): the card image's format, the BIOS's calls over it and the events each reports | made-up images, built in memory by the test's own stand-ins for the host's files: the format of a new card frame by frame, worked out a second time in Python from the rules and a third by `tools/card.py`; the events of each call and the blanks it takes, against a table derived from Sony's libcard and from what the game waits for, in the order it waits (the sources are named beside each row); files made, written, read back and deleted, a file of three blocks across its chain, the directory with `*` and `?`, a full card, a card that is not formatted, a damaged directory frame, an image that is not 128 KB, two slots, `--no-cards`, and the game's save's table of patches refused. Its Python side also runs `tools/card.py` end to end on an image of its own |
| `port/platform` | the port's clock, pads, events, primitives and printf (`src/port/`) | cases by hand: `VSync` as Sony's waits, the frame's wait and its count of `rand()` calls, a pad's packet, an event set and asked, the words of a mode, area and offset primitive, formats and what they print |
| `port/spu` | the sound chip (`src/port/spu.c`) on made-up waveforms and registers, and the reverb on the settings of Sony's nine modes | cases by hand (a block's samples, the envelope's steps, the keys and the ended bits sample by sample, the mixer's limits), and a second decoder, interpolation and envelope in Python written from the rules as documented; the interpolation's table against what such a table must be; a click through each reverb mode comes back, never louder, and dies away |
| `port/mdec` | the movie (`src/port/mdec.c`, `stream.c`) on made-up bits, blocks and sectors: the bitstream decoder, the chip, and the ring the drive's stream fills | the code table of the original library itself (`movovl/asm/data/libpress_vlc.data.s`), the chip's two tables against the words that library loads it with, the chip's arithmetic written out a second time in Python and a real inverse cosine transform (within 2), cases by hand (a block of one coefficient, the step to red, green and blue, a macroblock in both depths), and the ring with a sector of sound among the picture, a frame that loses a sector, the ring's wrap and the drive's rate at both speeds |
| `port/xa` | the disc's sound (`src/port/xa.c`) on made-up sectors in each of the format's forms | cases by hand, and a second decoder in Python written from the format; a straight line through the change of rate stays one; the drive's four volumes |
| `port/raster` | the PlayStation's GPU in C (`src/port/raster.c`), Sony's libgpu on it (`gpu.c`) and the pictures it writes (`picture.c`) | pixels worked out by hand, and the fill rule said a second way: a triangle's ten pixels; 300 made-up triangles and 300 four-sided primitives, the second drawn by adding, so that a pixel left out or drawn twice along a shared edge shows; the limits of size, the drawing area, the offset; each texture depth, the texture window, a texel times a colour; the four mixes and the texels they apply to; the mask bit set and looked at by each kind of primitive; the dither's pattern typed again, on what it applies to and what it does not; shading across, down and over a four-sided primitive; twelve bits of fraction on a triangle 1023 across; lines, of one piece and of many points; the fill and the three copies; an ordering table walked, a drawing and a display environment put, `ClearImage`, `LoadImage`, `StoreImage`, `MoveImage`; a PNG read back chunk by chunk, its sums done a bit at a time |
| `port/play_spu` | the play tests' reader of a sound chip (`tools/play/spu.py`), Python alone | a saved state made up by hand in the emulator's format, with the chip where the emulator has it: the chip comes out whole, and a state without one, or cut short, is refused; two chips that differ in the chip's own buffers and registers are the same, and in a byte of a waveform or a register a program writes are not |
| `port/window` | the arithmetic of the port's window (`src/port/window/scale.c`, `ring.c`, `pace.c`, `keymap.c`, `logname.c`, `fps.c`), which has no SDL in it; the window itself is built by no check ([`docs/PORT.md`](../docs/PORT.md), "The window") | cases by hand: the picture's place in windows of every shape and in each way of scaling; pixels of five bits as eight, at video memory's edges and in the mode of three bytes; the queue of samples fading in, running out with no step larger than a fade's, and throwing its oldest away when full; when a blank is due, time let go, the clock going round at 32 bits, and a made-up sound card two parts in a thousand fast and one as slow for 20,000 blanks with nothing running out; every button's name against Sony's masks, keys and sticks and the dead zone, a controller pulled out, a settings file's lines and each kind it cannot use; a log file's name, which of 25 logs are old, printed pieces as whole lines, CRC-32 against the known sum; the counter's rate each half second, its text — with more pictures a second than the console's blanks, where the speed it says is still the blanks' (`src/port/gl/gl_tween.h`) — every digit's shape and where its pixels land, its place in each corner; the remastered picture's own shape in a window of every shape, how many times the console's lines a scale draws so that its pixels come out square, and the five settings that choose that picture (`src/port/remaster.c` is linked with it for the draw distance's names: `window.py`). 33 deliberate mistakes in the first four files each failed it |
| `port/input` | the port's one input point (`src/port/pad.c`) with the game's reader of the pads (`src/main/pad.c`) and the keys and buttons mapped onto it (`src/port/window/keymap.c`; [`docs/NETPLAY.md`](../docs/NETPLAY.md), section 4) | cases by hand: a frame's input is six bytes; nothing answers before the driver's first blank; a press reaches the game at the blank after it and not before; a stopped driver keeps the last; an input kept and given back in place of the pads' reads the same; a pad that is gone counts as Start in a fight from the frame after, the second pad only in the modes of two players; keys, controller buttons and a stick through the layout built in, a script's buttons beside them, a key remapped per player, one key for two buttons, and the layouts a file starts from |
| `port/remaster` | the remastered renderer's side channel and decoder (`src/port/remaster.c`), on the GPU in C ([`docs/PORT.md`](../docs/PORT.md), "Remastered") | cases by hand, with a backend made up here that writes down what it is given: which picture is on, with and without a backend and following the game's own setting; the rectangle of video memory that changed, taken in, gone round an edge, and its pixels; the scale for a window of every shape; a projected point written down under the address it was stored at and a face attached to the primitive it became, the corners coming out where the projection put them and how deep; a point the GTE stored into the primitive itself, as a cut stage tile has it; where a primitive is in the frame's primitive buffer, in either of the two and in neither, which is what pairs two frames' primitives (`src/port/gl/gl_tween.c`); what is forgotten after two frames; a polygon, a four-sided one, a rectangle of its own size and of a fixed one, a shaded triangle, the dither's rule, the drawing offset, a fill, a copy and a load from the program; and that with the original picture, or with no backend, nothing is called at all |
| `port/memory` | the list of fixed addresses in `src/port/memory.c` | the game's source itself: every address written into it is memory the port maps, above the program, and in the list |

`port/gte_vectors`, `asm_vectors` and `render_vectors` are the host's half of the play test `gte_model`
([`play/README.md`](play/README.md), "The GTE probe"): the emulator is
not there on every push, so what is held is that the C still gives what
it gave when the emulator last agreed. A test's Python side may name
more source files to compile apart and link (`sources()`); those three and
`port/asm` do, for Sony's modules, each of which is a file of its own.

**The rollback self-test** is not one of these, because it needs the
disc: `make port-rollback` (`wsl bash tools/build.sh port-rollback`,
`tools/port_rollback.py`) runs both programs of `make port` with
`port_game`'s plan, once as they are, once with `--rollback-test 8`:
every 24 frames the game's state is saved, eight frames run, the state
put back and the eight run again ([`docs/NETPLAY.md`](../docs/NETPLAY.md),
"Save, load and hash"), and once more with the eight run again quietly,
as netplay runs them (`--rollback-quiet`, NETPLAY.md section 10). It fails on any page of the state or line of the
output that the second run of a frame does not give as the first did,
and on any difference between the two runs' output and files; a page
that differs is named by the variables in it. About half a minute, with
your own disc image (`REBUSHIDO_RETAIL`) and no emulator; run it after a
change to the port's own variables, to what the link puts outside the
state (`config/port_state.txt`) or to what a frame of
the game reads.

**Two programs over the network**, `make port-net` (`tools/port_net.py`),
needs the disc too: two headless programs of each size on 127.0.0.1 play
the game's own link-cable mode in lockstep, which passes if both consoles
print the same trace to the end of a fight, and then rollback netplay of
`port_game`'s session with 100 ms of latency and 5% of datagrams lost,
which passes if both end well, find no desync and print the same state
hashes (NETPLAY.md, sections 11 and 12). Then a session set up by
`--host` and `--join` (`session`): the host's cards sent, both taken to
versus select, a fight, a rematch, and the guest leaving, the same
hashes on both and no card file written; the same between two of the
window's programs, headless, in real time (`window`), where the host
must play on alone; and two window programs started from the settings
menu's Play online page (NETPLAY.md, section 13). About eight minutes;
run it after a change to the step, the state, the input point, the
network, the session or the window's pacing.

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
destinations, the scratchpad at `0x1F800000`), so its C as the PlayStation
builds compile it only means the same thing compiled with 32-bit pointers:
the tests are built with `gcc -m32`. With `--rebushido`, where the port's
switch is on and the source's `ADDR()` and `PTR()` are what the port makes
of them ([`docs/PORT.md`](../docs/PORT.md), "64 bits"), every test is then
built and run a second time as a 64-bit program, and has to pass the same
checks: 32 of the 37 are programs. A 64-bit test runs on a stack of the
runtime's own below 4 GB and gets its memory there, because a test hands
the game the addresses of its locals. A test whose Python side says `BITS =
(32,)` is for that size alone (`rebushido/play_state`, which is about the
PlayStation's offsets).
A 32-bit C library is usually not installed (it is not in this project's
WSL, nor on GitHub's runners), so the tests use none: each is a static
program linked with `tests/runtime/runtime.c` and nothing else (`-nostdlib
-ffreestanding`). The runtime is about 400 lines: `_start`, and on the Linux
system calls (`int $0x80`, or `syscall` in a 64-bit program) a `test_Print` that knows `%d %u %x %s %c`,
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
