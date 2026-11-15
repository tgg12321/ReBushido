# The native port

A build of this same source for a PC, beside the PlayStation one: item 6
of the [roadmap](ROADMAP.md). The game's code stays C and stays one
source. The PlayStation builds keep their assembly and are not changed by
any of this: `decomp` is not touched, the matching build still makes the
original's bytes, and nothing of the port is compiled or linked into the
ReBushido PlayStation program.

This page is what stands between the source and a PC, counted; what has
been done about it; the stages that are left; and the decisions that are
the owner's. **No port runs yet.** What exists is the first stage, whole:
C for the assembly, with its proof.

## Where it stands

| | |
|---|---|
| The GTE, the PlayStation's geometry coprocessor, in C | done: `src/port/gte.c`, every operation the hardware has |
| The game's GTE instructions (Sony's macros) as calls into it | done, by `tools/port_gte_macros.py` |
| C beside the assembly functions | 119 of 138: Sony's libgte (48), the game's maths (20) and the renderer (51). The other 19 need none (below) |
| BIOS calls | named below; nothing written |
| Sony's libraries, a window, sound, the disc | not begun |

## The switch

`RB_PORT` (`include/rebushido.h`) says "this is the port" at compile time.
It is `(REBUSHIDO && RB_PORT_BUILD)`: off in every PlayStation build, on
where a build gives `RB_PORT_BUILD`. Today that is the two builds for the
host: `make rebushido-test` and `make rebushido-modern`
(`tests/run.py --rebushido`, `tools/modern/check.py --rebushido`). So the
checks that run on every push compile and run the port's C, and the
checks with the switches off still see the source exactly as before.

Where a function is assembly, the port's C sits beside it and the
original's line stays as it is:

```c
#if RB_PORT
s32 math_Sqrt(s32 number) { ... }
#else
INCLUDE_ASM("asm/funcs", math_Sqrt);
#endif
```

The port's own files are `src/port/*.c` with headers `include/port_*.h`,
and its names are `port_VerbNoun`, `g_port_noun`, `PortType`,
`PORT_CONSTANT`. They have a prefix of their own, not `rb_`, because
`rb_` marks what is in the ReBushido PlayStation program and these never
are: the `Makefile` leaves `src/port/` out of every PlayStation build.
Its tests are `tests/port/`.

## The assembly

Everything in the build that is not C, by kind. "Listings" are the files
of `asm/funcs/`, which the source includes with `INCLUDE_ASM`: 139 files,
138 functions and one piece of data, 11,012 instructions.

| What | How much | Where | Its form in the port | State |
|---|---|---|---|---|
| GTE instructions in the game's C | 177 uses of 33 macros, in 9 files (`hit_test.c` 63, `cloth.c` 43, `eff.c` 30, `fighter_pose.c` 17, `object.c` 12, `fighter_hit.c` 6, `stage.c` 2, `camera.c` 1, `fight_event.c` 1) | Sony's `inline_c.h` (241 macros), `inline_o.h` (174) and `gtemac.h`, which is made of those | Each macro written again as C, an instruction a statement, on the GTE in C. `tools/port_gte_macros.py` writes the two headers into `build/port/include/`; Sony's files are not changed. 133 and 120 macros have C; the rest have no instruction word in Sony's header and the game uses none | done |
| The GTE itself | 22 operations; the listings run 16 of them, by 24 instruction words, 195 times | the console | `src/port/gte.c`: the 64 registers and every operation, with their limits, flags and the division | done |
| Sony's libgte in assembly | 48 functions | `src/main/psxsdk/libgte/`, 33 files | C beside each | done |
| The game's maths in assembly | 20 functions: the 19 of `math_asm.c` and `math_RotMatrixZYX` | `math_asm.c`, `render_asm.c` | C beside each. `sys_Trap` becomes `port_Trap`, which the port supplies | done |
| The renderer | 51 functions, 8,029 instructions, and the data listing | `render_asm.c` (50), `render_skin_asm.c` | C beside each ("The renderer in C"). They are not C functions as they stand: they pass the mesh cursor, the scratchpad and the handler table to each other in fixed registers, jump into each other through two tables of labels, and eight of them have their outline colour written into their instructions while the game runs (`render_SetOutlineColor`). The C gives all that a form: a structure for the shared state, two switches for the jumps, a variable for the colour | done |
| BIOS calls | 40 two-instruction jumps (`BIOS_A_FUNCTION` and its like), 2 system calls, `_96_remove` | `src/main/psxsdk/libapi/`, `libcard/` | Not C equivalents: the kernel does not exist on a PC. The port supplies what the game calls (below) | named |
| Sony's other assembly | 8 functions that patch the kernel's pad and card drivers, `_patch_gte`, `setjmp`, `SetSp` | `libapi/`, `libcard/`, `libc2/`, `libgte/patchgte.c` | None: they serve Sony's interrupt and driver code, which the port replaces whole | not needed |
| Start-up | the PS-X EXE header, `__SN_ENTRY_POINT`, `__main`, `__do_global_dtors` | `asm/header.s`, `libsn/snmain.c` | The PC's own start-up; `main` is the game's | not needed |
| The development host's file calls | `PCopen`, `PCclose`, `PClseek`, `_SN_read`, and `pollhost` (a `break`) | `libsn/` | The game's two loaders that use them (`main.c`) are never reached on a retail disc; the port leaves them out or gives them the PC's files | not needed |
| The movie overlay | its entry, one BIOS jump, the bitstream decoder (279 instructions and its tables) and the DMA routine (120) | `movovl/asm/` | See "The movie overlay" | left |

### The BIOS calls the game makes

Sony's headers already declare these (`include/psxsdk/libapi.h`,
`kernel.h`), and the game's code calls them by those names; the port's
platform layer has to define them. Counted in the game's own code and
ReBushido's, not in Sony's.

| What for | Calls | Kernel entries | What the port supplies |
|---|---|---|---|
| Events: how the game hears from the card and the link | `OpenEvent` 10, `CloseEvent` 10, `EnableEvent` 10, `TestEvent` 34 | B(08), B(09), B(0C), B(0B) | A table of flags that the port's card and link code set and `TestEvent` reads and clears |
| The memory card's files | `open` 5, `close` 11, `read` 2, `write` 2, `firstfile`, `nextfile`, `format`; `_bu_init`, `InitCARD`, `StartCARD`, `StopCARD`, `_card_info` 2, `_card_load` 2, `_card_clear` 2 | B(32), B(36), B(34), B(35), B(42), B(43), B(41), A(70), B(4A) to B(4C), A(AB), A(AC), B(4E), B(50) | A memory card as a file on the PC, in the console's card format (128 KB), read and written by these names, with the events the game waits for |
| The pads | `InitPAD`, `StartPAD`, `StopPAD`, `ChangeClearPAD` 2 | B(12) to B(15), B(5B) | The two buffers `InitPAD` is given, filled once a frame in the console's packet form from the PC's keyboard and controllers |
| The counters | `SetRCnt`, `StartRCnt`, `GetRCnt` 7, `ResetRCnt` 8 | Sony's C over `ChangeClearRCnt` C(0A) | A count of scan lines made from the PC's clock: the frame loop waits on it ("Timing") |
| Interrupts off and on | `EnterCriticalSection` 5, `ExitCriticalSection` 4 | system calls 1 and 2 | Nothing, or a lock if the port runs sound or the disc on a thread of its own |
| Running the movie | `Exec`, `SetSp`, `SetMem` | A(43), A(9F) | A call ("The movie overlay"); the other two are nothing |
| Stopping | `sys_Trap` | (a write to an address with no memory) | `port_Trap` (`include/port_platform.h`): report and stop |

The other kernel entries (`InitHeap`, `FlushCache`, `GPU_cw`,
`DeliverEvent`, `WaitEvent`, `DisableEvent`, `ReturnFromException`,
`ResetEntryInt`, `HookEntryInt`, `AddDrv`, `DelDrv`, `SysEnqIntRP`,
`SysDeqIntRP`, `_card_write`, `_new_card`, `_96_remove`) are reached only
from inside Sony's libraries.

## The renderer in C

The 50 routines of `render_asm.c` and `render_LightSkin` have C beside
them under `RB_PORT`, in the same files.

**What the routines share.** The listings keep where the walk over the
queue is in registers, from one routine to the next. In C that is one
structure, `g_render_walk` (`render.h`):

| Member | The listings' register | |
|---|---|---|
| `queue` | `$s7` | the next node of the queue |
| `node` | `$s2` | the node being drawn |
| `mesh` | `$s0` | the mesh cursor: every routine reads it and leaves it moved on |
| `handlers` | `$s4` | the handler table in use |
| `scale_shift` | `$s5` | how many bits the node's coordinates are scaled down by |
| `view` | `$s6`, `$fp` | the view matrix |
| `lights` | `$s3` | the node's light setup |

A routine of the handler tables is `void render_Emit...(s32 count)`: it
is given its group's count, as the listing is in `$a0`, and finds its
records at `g_render_walk.mesh`. The tables themselves (`render.c`) are
not changed. What the listings keep on the scratchpad stays there, at
the same places: the words `render.h` names, the projected points and
their depths, the work area of the stage tiles. The C reaches it by the
console's address, which is what "Memory at the PlayStation's
addresses" (below) is for.

**The two tables of labels** are two switches. `g_render_kind_jumps`,
by the node's kind, is `render_SetupNode`: it loads the GTE for the kind
and says whether the node's mesh is then walked (a mesh turned by its
node, one only placed, a shadow, the tiny model), or draws the node by
its own routine (a fighter's body, a run of stage tiles, a cloth, the
surf). `g_render_format_jumps`, by the format of a part's vertices, is
`render_DrawPart`: SVECTORs with the table in use; packed with the usual
table, or its second half for a node drawn as outlines; the tiny
model's, about its own point of the screen; none; packed with the packed
table.

**The outline colour** is a variable, `g_render_outline_color`, where
the listings have it in two instructions of each of eight routines.
`render_SetOutlineColor` (`model.c`) sets it.

**What is one function in C and several listings.** The eight outline
routines are two (triangles, quads); the four two-sided routines and the
four of cloth are four, cloth differing by the depth a face is sorted
at; `render_EmitF3` and `render_EmitTinyF3` are one; the two routines
that draw a stage tile's faces are one, and their cutting keeps its
levels where the listings do, as an array of frames on the scratchpad;
`render_EmitPackedGT3` and `GT4` call the routines whose listings they
repeat.

**Where the C is not the listing.** The listings leave return addresses
and saved registers on the scratchpad (`render_DrawGroups`, the two tile
routines and their cutting); the C writes nothing there. A handler given
a group of no faces stores a register it never loaded as the primitive
cursor; the C leaves the cursor (the walks never pass such a group). A
node's kind or a part's format past the end of its table, which the
listing would jump through whatever follows the table for, draws
nothing. Everything else is the listing's, the odd things too: the
points a face writes before its depth is known, the colour some routines
load once for a whole group, the normals `render_EmitTwoSidedGT4` turns
only in part, the scale two kinds of node work out and then do not use.

**What makes it a 32-bit program.** An ordering-table link is 24 bits
of a primitive's address (`render_LinkEntry`, the tiles' chain), and
some primitives are in the mesh itself. Addresses are kept in 32-bit
words of the scratchpad (the ordering table, the handler table, a tile's
vertices and its ordering-table entry) and in 4-byte slots of the data:
the tables of mesh pointers, the list of a body's bone matrices, the
pointer after a body's or a cloth's node, a shadow's parent. The mesh
cursor is brought to a whole word by its address. With 64-bit pointers
each of these has to change; none is hidden.

## Everything else

Measured on the source as it is (`src/main/*.c`, 55,684 lines).

**Addresses are 32-bit integers.** The game keeps addresses in `s32`
everywhere: about 130 places turn an integer into a pointer and about 120
a pointer into an integer (counted from the casts; perhaps 300 to 400 in
all), and whole interfaces carry them (`cdrom_StartRead(file, dest)`,
`arena_OpenBlock`, the HUD's primitive cursors). Compiled with 64-bit
pointers none of that means the same. Besides:

- 41 structures hold pointers, 136 members. Nine of them are files read
  from the disc whose offsets are turned into pointers in place, in
  4-byte slots (`MenuFile`, `SelectFile`, `TeamSelectFile`,
  `NarrationFile`, the sprite files); others are laid over the
  scratchpad, or read by the renderer's assembly at fixed offsets
  (`SceneNode`, `Model`, the cloth's).
- The save file's record (`SaveData`) holds 22 addresses of the
  PlayStation's memory, which are written into on load.

**Fixed addresses.** Twelve constants: the scratchpad at `0x1F800000`
(`SCRATCHPAD`, used 457 times in C through 17 names, and by 47 of the
listings) and eleven buffers above the program (`include/sys.h`,
`motion.h`): the common sound bank at `0x8010DB00`, the effects at
`0x8010E800`, the movie's work area, the load and staging areas at
`0x80118800` and `0x80190800`, two buffers of primitives at `0x801D8800`
and `0x801EBC00`, the stack's top, and three places motion files are put.
No address of the console's hardware is in the game's code: those are all
inside Sony's libraries (77 in 14 files).

**Ordering tables and primitives.** A primitive's first word is a link:
24 bits of the next primitive's address under 8 bits of length. The game
links through Sony's `AddPrim` (149 calls) and `addPrim` (8), and writes
the word itself at four places in C and in 40 of the renderer's listings;
`ClearOTagR` 9, `DrawOTag` 5. A 24-bit link holds an address only while
memory is where the PlayStation has it, or is made an offset into one
block.

**Byte order and alignment.** The source assumes a little-endian machine
that reads a word at any address the PlayStation would: loaded files are
used in place as structures; 98 places read memory through a cast to
another type; 16 unions lay bytes over words; the two tables of roots are
words read as halfwords. There are 17 bitfields in the game's headers (14
of them `GameSettings`), whose layout is the compiler's. Nothing is
packed or unaligned. So: little-endian hosts only, which is every PC.

**The compiler.** The game was written for GCC 2.7.2 with `char`
unsigned. The host builds already settle what a modern compiler needs
(`-funsigned-char -fwrapv -fno-strict-aliasing -std=gnu89`). Three things
are not standard C: a cast assigned to (`((u16 *)cursor)++`, 16 times in
`tex.c`, which the host builds rewrite), arguments walked by hand on the
stack (`font_Printf`, `reloc_KeepListedEntries`), and one zero-length
array.

**Sony's libraries.** What the game's own code calls, by library (the
calls of ReBushido's code and the overlay's are in the counts):

| Library | Functions | Calls | The most called |
|---|---|---|---|
| libgpu | 30, and 18 macros | 577, and 158 | `AddPrim` 149, `SetDrawMode` 76, `DrawSync` 64, `SetSemiTrans` 62, `SetTile` 38, `LoadImage` 31; the macros `setRGB0` 67, `setXY4` 13 |
| libgte | 36 | 236 | `ratan2` 63, `rsin` 27, `rcos` 16, `SquareRoot0` 16, `RotMatrixY` 14; all are C now |
| libsnd | 25 | 63 | `SsUtAllKeyOff` 19, the reverb's four 22, the sound banks' (`SsVab...`) 11 |
| libspu | 7 | 9 | transfers to sound memory, key status |
| libcd | 15 | 74 | `CdControlF` 17, `CdSync` 14, `CdPosToInt` 8, `CdReadyCallback` 6 |
| libcd streaming, libpress | 8, 4 | 10, 7 | the overlay only |
| libetc | 6 | 52 | `VSync` 43; `PadRead` in the overlay |
| libapi, libcard | 25, 6 | 122, 9 | the table above |
| libcomb | 2, and 8 macros | 2, and 21 | the link cable |
| libc | 7 | 87 | `rand` 57, `printf` 14, `sprintf` 7 |

**The disc.** Files are never found by name. `g_cdrom_files[159]`
(`cdrom.c`) holds each file's minute, second and frame and its size, and
`NDATA.DAT` is read through its own directory, `NDATA.INF`, by entry
number. Reading is a state machine run once a frame (`cdrom_Update`): it
asks the drive to seek and read (`CdControlF`), and a callback takes each
sector as it arrives (`cdrom_ReadyCallback`, `CdGetSector`). Music is CD
audio tracks and XA streams, both played by the drive itself.

**The movie overlay.** `MOVOVL.EXE` is a second PlayStation program. The
game shuts its sound, card and pads down, loads the overlay over its own
primitive buffers at `0x801D8800` and runs it with the BIOS's `Exec`,
handing it six words (`sys_ExecMovieOverlay`, `cdrom.c`); when its `main`
returns the game sets itself up again. It reads the movie through
Sony's streaming calls and decodes it with libpress, whose decoder is
assembly and whose second half is the console's own decoder chip.

**Timing.** `main`'s loop (`main.c`) ends each frame by waiting on the
scan-line counter until the frame's time is up, calling `rand()` while it
waits, then `VSync` and `DrawSync`. So the random sequence depends on how
long a frame took to compute. The frame is two fields (30 a second), one
in the main menu; the link's time-outs read the same counter. Nothing
uses `VSyncCallback`.

## How it is proved

- **The GTE against the emulator's.** A PlayStation program of the tests'
  own, the probe, runs 2,000 made-up cases of each of 42 instruction
  words on the emulator's GTE: all 64 registers loaded, the operation,
  all 64 read back. The same cases run on `src/port/gte.c` on the host.
  Every register of all 84,000 agrees (the play test `gte_model`,
  [`tests/play/README.md`](../tests/play/README.md)).
- **The C against the assembly.** The probe links the game's and Sony's
  routines as the game has them and calls each with 500 made-up cases;
  the host calls the C. What each wrote, and the GTE's 64 registers after
  it, agree for all 32,000 cases of 64 routines.
- **The renderer against its listings.** The probe links the renderer as
  the game has it (`render_asm.c`, `render_skin_asm.c` and `render.c`,
  the matching build's own objects) and runs 200 made-up cases of each
  of 49 things: the 38 routines of the handler tables and the two no
  table has, each on a group of its own records; the two projections;
  `render_DrawGroups` with each of the three tables the walk chooses,
  every type of a table in some cases; `render_LightSkin`; a whole
  skinned mesh of thirteen sections, by itself and through
  `render_DrawSkinned` with the game's two tables; a cloth; the two tile
  routines, cut up to three times; a run of tiles; and
  `render_DrawQueue` on queues of two to five nodes of every kind, with
  every format of part. A case makes up the mesh, the scratchpad, the
  nodes, matrices and lights, and the GTE's registers; faces come out
  drawn, turned away, off the screen, too near, too far and behind the
  camera, and the cutoff, the fog's end and the distances at which a
  node is scaled down are hit exactly in some. Both programs keep what
  a routine reads and writes at the same addresses, so what is compared
  is every word as it is: the ordering tables with their links, the
  primitives (the ones a routine began and gave up too), the mesh, the
  nodes, the scratchpad, where the cursors were left, the colours of a
  body's normals, and the GTE's 64 registers. All 9,800 agree.
- **By hand and by real maths**, on every push: `tests/port/gte.c` (each
  operation's limits and flags, worked out by hand; the division against
  true division) and `tests/port/asm.c` (roots against real roots,
  rotations against real rotations, products against whole-number
  arithmetic).
- **On every push, without the emulator:** `tests/port/gte_vectors`,
  `asm_vectors` and `render_vectors` run the probe's cases on the host
  and hold the results to the numbers written down when the emulator
  last agreed.
- **Deliberate mistakes.** 122 one-line mistakes were put into the
  renderer's C, one at a time: 117 changed a number the host test holds
  (or stopped it), and those numbers are what the probe compares. The
  other five change nothing at all, and were looked at: a cursor moved
  on after its last use, a translation cleared that the product before
  it had cleared already, a starting value every kind of node replaces,
  two operations that do not touch each other swapped, and a shift
  count that differs only where the result is nothing either way.
  Twelve more went through the emulator itself, to see the probe say
  so. The mistakes that slipped through at first were all of one sort,
  a comparison one off at a limit no case stood exactly on; the cases
  now stand on each of those limits (the cutoff, the fog's end, the
  sizes and distances things change at).

Not proved that way: `InitGeom` (it patches the console's kernel; its
register values are checked by hand), `sys_Trap`, and anything the
emulator's GTE itself has wrong. Of the renderer: what "Where the C is
not the listing" names (those words of the scratchpad are left out of
the comparison), a mesh that ends wrongly and so stops the game, and the
two functions of the game's C the walk calls for a shadow and for the
surf, which both programs have stand-ins for. The cases are made up, so
a mesh of the game's that does something no made-up one does is not
covered: the first whole frame drawn by the port, at stage 4, is the
check of that. Where the emulator and what is written
about the hardware might differ, the port has the emulator's reading, and
the comments in `src/port/gte.c` say where: the depth cue of the first
two points of `RTPT`, which sets no flag; and three corners the game
never goes into (the far colour added by `MVMVA`, a fourth matrix that
does not exist, and `RTPS` with its fraction kept). An instruction word
that names no operation does nothing in the port.

Three routines read outside their table for a number they were not made
for: the two square roots and `math_Sqrt` for a negative number,
`gte_NormalizeIR` for a vector of zeroes. The port gives 0 there. That is
the one place the C does not do what the assembly does.

## The stages

Each ends with something that can be shown.

1. **C for the assembly.** Done: the GTE, the maths and the renderer,
   each held to its listing by the probe. (The two tables of labels at
   the top of `render_asm.c` still name the listings: stage 2 puts them
   behind the switch when the program is first linked.)
2. **A program that links.** The game's C, the port's, and an empty
   platform layer: every Sony function the game calls, defined to do
   nothing or to fail. *Done when* `make port` links with no name
   missing, on the host, and the program runs to its first frame's wait.
3. **Memory and the disc.** The fixed buffers and the scratchpad given
   memory; the disc's sectors read from the player's image. *Done when*
   the game's own loaders have read the files of the boot, the menus and
   a fight, and the values the play tests watch are the emulator's at the
   same checkpoints.
4. **The picture.** The ordering table drawn by a rasteriser in C that
   does what the console's GPU does. *Done when* the pictures at the play
   tests' checkpoints are the emulator's, pixel for pixel, with no window
   open; then the menu in a window.
5. **Pads, time, a fight.** *Done when* a scripted fight on the port goes
   through the same watched values as the same script on the emulator.
6. **Sound.** The sound chip, Sony's sequencer, the disc's audio.
7. **The rest.** Movies, the memory card as a file, the link (or
   nothing in its place).

The wide picture and the high resolution are the roadmap's next item and
build on stage 4.

## The owner's decisions

| Decision | Recommended | Why |
|---|---|---|
| A 32-bit program first, or 64-bit from the start | 32-bit first | The source keeps addresses in 32-bit integers at some 300 places, relocates nine kinds of file in place into 4-byte slots and saves 22 addresses to the card: as a 32-bit program all of it works as written, and the tests already run that way. 64-bit is then a stage of its own with the port already playing (and is needed in the end: macOS runs no 32-bit programs) |
| Memory at the PlayStation's addresses, or everything moved | At the PlayStation's addresses, while the program is 32-bit | A 32-bit program can ask for memory at `0x80010000` and `0x1F800000` (the tests do, for the scratchpad). The twelve fixed addresses, the 24-bit links and the save file's patches then need no change at all |
| A library for the window, the pads and sound | SDL 2 | One library does all three on Windows, Linux and macOS, and its licence sits with this project's. It has to be downloaded; nothing has been |
| The game's data: the disc image, or files taken out of it | The player's disc image | The game asks for sectors, never for names, so an image answers it directly; the music is tracks and streams that only the image has in one piece; and no tool has to unpack game data. Loose files can come later for modding |
| The first renderer: software as the console draws, or the PC's graphics card | Software first | It can be held to the emulator's picture pixel for pixel by the tests there already are, and needs nothing installed. The hardware renderer is then the roadmap's next item, behind the same list of primitives |
| The movies | Decode them in the port, in C | The overlay is a separate PlayStation program started through the BIOS; on a PC a function that plays a movie is simpler than a program loader, and the decoder chip has to be written in C either way |
| The frame's wait | To be decided at stage 5 | The original calls `rand()` for as long as the frame has time left, so its random numbers depend on the machine's speed. The port needs a rule (a fixed number of calls is the simplest); none reproduces a console exactly |
