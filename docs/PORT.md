# The native port

A build of this same source for a PC, beside the PlayStation one: item 6
of the [roadmap](ROADMAP.md). The game's code stays C and stays one
source. The PlayStation builds keep their assembly and are not changed by
any of this: `decomp` is not touched, the matching build still makes the
original's bytes, and nothing of the port is compiled or linked into the
ReBushido PlayStation program.

This page is what stands between the source and a PC, counted; what has
been done about it; the stages that are left; and the decisions that are
the owner's. **The port is a program that draws and sounds, with no
window and no sound card:** it links, gives the game the PlayStation's
memory, reads the player's disc image, and runs the game's own `main`
through start-up, the options page, the menus and character select,
through a whole fight and back to the menu, with no pads but a
script's. What the game draws goes into the port's own video memory, by
a GPU in C, and from there to files; its sound is made by a sound chip
in C and can be written to a file. All that way it is held to the
emulator: the game's values in three fights, and the picture pixel for
pixel in two, and the sound chip's memory and registers in one. It is a
64-bit program, and a 32-bit one beside it that does the same to the
byte ("64 bits"). Stages 1 to 3 of the seven below are done, stage 4 but
for its window, of stage 5 the fight itself, and of stage 6 everything
but the card.

## Where it stands

| | |
|---|---|
| The GTE, the PlayStation's geometry coprocessor, in C | done: `src/port/gte.c`, every operation the hardware has |
| The game's GTE instructions (Sony's macros) as calls into it | done, by `tools/port_gte_macros.py` |
| C beside the assembly functions | 119 of 138: Sony's libgte (48), the game's maths (20) and the renderer (51). The other 19 need none (below) |
| A program that links | done: `make port` builds `build/port/rebushido` from the game's C, with no name missing ("The program") |
| BIOS calls and Sony's libraries | every one the game calls is defined: done for real where it only works on memory or the port has the thing (the disc, time, the pads' packets, events), a stub that is counted where it has not (the memory card, the link, the movie) |
| Memory | done: the RAM and the scratchpad at the console's addresses ("Memory") |
| The disc | done: sectors from the player's image, under the game's own disc code ("The disc"), and its XA sound ("Sound") |
| Sound | done into a file, not yet a sound card: Sony's libsnd and libspu on a sound chip in C, the disc's music ("Sound"). The chip's memory, its registers and the library's are the emulator's through a fight; what is heard is compared only in part |
| A fight | done, by a script's pads: three whole fights are the emulator's, frame for frame ("How it is proved") |
| The picture | done, into the port's own video memory and out to files: `src/port/raster.c`, the PlayStation's GPU in C. The flat screens are the emulator's to the pixel, a fight's frames but for three pixels in a thousand ("The picture") |
| 64 bits | done on Linux: the program has 64-bit pointers, the game's addresses stay 32-bit numbers below 4 GB, and it is held to the 32-bit program and to the emulator ("64 bits"). Windows' side is compiled and not linked |
| A window, a sound card, real pads, the card, movies | not begun |

## The switch

`RB_PORT` (`include/rebushido.h`) says "this is the port" at compile time.
It is `(REBUSHIDO && RB_PORT_BUILD)`: off in every PlayStation build, on
where a build gives `RB_PORT_BUILD`. That is the three builds for the
host: `make port`, the program itself, and `make rebushido-test` and
`make rebushido-modern` (`tests/run.py --rebushido`,
`tools/modern/check.py --rebushido`). So the checks that run on every
push compile, link and test the port's C, and the checks with the
switches off still see the source exactly as before.

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

## The program

```
make port                                   # in WSL or on Linux; wsl bash tools/build.sh port
build/port/rebushido --image "Bushido Blade 2 (USA).cue" --frames 600 --trace
```

`tools/port_build.py` compiles every file of `src/main/` and
`src/rebushido/` (quick boot's aside) with the port's switch on, as the
host tests compile them, the part of Sony's library code the next
section names, and `src/port/`, and links them into
`build/port/rebushido`. It is an x86 Linux program of 64-bit pointers,
linked with no C library, exactly as the host tests are (a C library for
32-bit programs is not installed on the machines that build this, nor on
GitHub's, and the port began as one): it needs the host's `gcc` and
`python3`, and where that `gcc` cannot make such a program the build
says so and fails. The same build makes the 32-bit program beside it,
`build/port32/rebushido` ("64 bits"). The game's files are compiled with
their warnings off, which `make rebushido-modern` reads, but the three
that hold the source to `ADDR()` and `PTR()`, which are errors; the
port's own with that check's warnings as errors, less
`-Wstrict-prototypes`, which Sony's headers themselves fail.
The game's files are also compiled so that every function says when it
begins and ends (`-finstrument-functions`): a run's script can then
stop at a call of any of them, and list the calls of a frame
(`tests/play/README.md`, "Finding where two runs part").

Everything the program asks of the operating system goes through
`include/port_host.h`: a dozen functions (write, open, read at an
offset, memory at an address, catch a fault, exit). `src/port/
host_linux.c` defines them on the kernel's own calls.
`src/port/host_windows.c` defines them on kernel32; MinGW's compilers
make an object of it for either size, and it has **not been linked or
run**. When the port takes SDL for its window it will need a C library,
and that file is where it changes.

The program's options: `--image` (the `.cue` or the `.bin`), `--frames
N` (end after N vertical blanks; 3,600 if nothing says), `--trace` (the
values of `tests/play/state.toml` at every blank where one changed),
`--plan FILE` (a play test's plan: presses, checkpoints, a limit),
`--dump NAME=ADDRESS:BYTES` and `--out FOLDER` (memory to a file at the
end), `--shots FOLDER`, `--every N` and `--vram FILE` (the picture as
PNGs, and video memory: "The picture"), `--wav FILE` (the run's sound,
with a line for each voice started and each sound the game asked for)
and `--spu FILE` (sound memory and the chip's registers at the end:
"Sound"). It ends by printing a line for each stub the game called, with
how often. Pads are pressed only by a plan: a line `in 0 300 1 START 6`
is Start on the first pad at blank 300 for six blanks
(`tests/play/README.md`, "The port as a disc").

A fault does not end it silently: it says what, at which address, in
which function and called from which, and where the game was
(`src/port/crash.c`; the names are in `build/port/rebushido.sym`, which
the build writes beside the program). The game's own stop, `sys_Trap`,
says the same.

### Sony's libraries in the port

The rule: **Sony's code is compiled into the port where it is plain C
that only works on memory, and is replaced where it touches the
console.** By library:

| Library | In the port |
|---|---|
| libgte | Sony's own, all of it but `patchgte.c` (which patches the kernel): the C modules as they are, the assembly ones by the C beside them, on `src/port/gte.c` |
| libgpu | Sony's `prim.c` (the primitives' set-up, `GetTPage`, `AddPrim`) and `ext.c` (`SetDefDrawEnv`, `SetDefDispEnv`). The module that talks to the GPU is the port's (`gpu.c`), all of what the game calls, on the port's GPU (`raster.c`): it writes the words Sony's writes the console's ("The picture") |
| libc | Sony's `memcpy`, `memmove`, `memchr`, `strcpy`, `strlen`, `rand`, `toupper`. The port's: `printf` and `sprintf` (Sony's walk a PlayStation's argument list), and `memset` and `memcmp`, which the compiler calls |
| libetc | Sony's `GetVideoMode`. The port's: `VSync` (`clock.c`), `ResetCallback`, `StopCallback` (nothing), and the interrupts' callbacks, of which the vertical blank's is run (`sound.c`) |
| libcd | the port's, all of it, over the disc image (`cd.c`), with the XA sound the drive plays (`xa.c`) |
| libapi | the port's: the counters (`clock.c`), the pads (`pad.c`), events (`event.c`), and nothing for `SetSp`, `SetMem` and the critical section. The card's calls and the files are stubs that answer "no card"; `Exec` is a stub |
| libsnd, libspu | Sony's, all of both but the one module of libspu that talks to the chip, which is the port's on its sound chip in C ("Sound") |
| libcomb | the port's, all stubs: nothing at the cable's other end |
| libcard, libsn | not needed: the first is inside Sony's card code, the second is the development host's files, which nothing reaches |

A stub counts its calls and gives the answer the library gives when all
is well, where the game would otherwise stop or wait for ever: no card
is in the slot.

### Memory

The game's own variables are the PC program's, placed by its linker.
What the game reaches by a number written into its source is given
memory at that number (`src/port/memory.c`): the 2 MB of RAM at
`0x80000000` and the kilobyte of scratchpad at `0x1F800000`, by `mmap`
at a fixed address on Linux (`VirtualAlloc` on Windows, not tried), in
the 64-bit program as in the 32-bit one. The
game's stack is a third piece, a megabyte at `0x7FC00000`, which is the
port's choice and not the console's ("The picture", "Where the links
point"). If the system will not give one of them, the program says so
and stops. (The sound chip's registers are a fourth piece, at
`0x1F801000`, which only Sony's sound libraries reach: "Sound".) The
twelve addresses are the scratchpad and the eleven of `include/sys.h`
and `motion.h`; `tests/port/memory.py` reads the game's source on every
push and fails if it has an address that is not in the mapped memory,
is below the first of them, or is not in the list at the top of
`memory.c`. Below the common sound bank the mapped RAM is unused: the
program that lives there on the console is the PC program, elsewhere.

Two things the PlayStation gives the game for nothing had to be given
it here:

- **Neighbours.** The game reads some variables as one array: the
  blow's direction is three, a side's points two. On the PlayStation
  they are next to each other because `bb2.ld` puts them so; a PC's
  linker put them in the reverse order. So the port's link is by a
  script the build writes, with each variable in a section of its own
  and the sections in the PlayStation's order
  (`config/symbol_addrs.txt`), kind by kind, and each started on the
  boundary the PlayStation's compiler gives it: a word's for an array
  or a structure, its own size's for anything else. A PC's compiler has
  other rules (an array on its elements' boundary; in a 64-bit program
  every array of 16 bytes or more on a 16-byte one), so the build sets
  the boundary in each object after it is compiled
  (`tools/port_build.py`, `cap_alignment`). The build counts the
  result: in the 32-bit program 868 of the game's 991 pairs of
  neighbours are exactly as far apart as on the PlayStation, and the
  123 others are in `build/port32/layout.txt`: nearly all are closer
  together than on the console, where another file's unnamed data lies
  between the two or a small variable is padded to a word. In the
  64-bit program 743 are; of its 248 others, 137 follow a variable
  that has pointers in it, which are twice as wide there, and the rest
  are the 32-bit program's (`build/port/layout.txt`).
- **Address 0.** The console has its kernel's memory there, so the
  original reads and writes through pointers it never set, and nothing
  happens. A PC stops the program. Linux will not map address 0 for a
  program, so each place is given somewhere else, under `RB_PORT`, with
  a note: the word `fighter_TakeHit` clears first (the port passes the
  place the "Bugfixes" setting does, whatever the setting, which
  changes nothing the game does) and the direction an effect was never
  given (`eff_Start`: zeroes). Two were found on the way to a fight's
  end; the next one will stop the port and say where.

### What the PlayStation's compiler decided

Two more things the game has on the PlayStation and C does not promise.
Both were found by the fight: the port's left the emulator's at the first
clash of blades, and after that was put right a clash's spark was missing.

**The order of evaluation.** C leaves open the order in which the
operands of an operator and the arguments of a call are worked out. GCC
2.7.2 for MIPS, measured on the compiler itself, makes the calls of one
expression in the order they are written and reads a variable or a
member that is an operand as late as it can, after those calls. A modern
GCC for x86 works a call's arguments out from the last to the first and
reads memory as it comes to it. `fighter_Update` starts a move as

```c
fighter_StartMove(index,
                  motion_GetMove(fighter, id, &fighter->move_is_common),
                  fighter->move_is_common);
```

and means the third argument to be what `motion_GetMove` has just
written: whether the move it found is in the common file or the
fighter's own. The PlayStation passes that. The port passed what the
member held before, so the first time a move of one file followed a move
of the other (the fight's 166th frame: the second fighter's parry, at
the first clash) the move was started with the other file's motion, and
the fighter left the ground. Seven calls in `fighter_Update` are written
so. In the port `fighter_StartMove` is given where the flag is and reads
it when every argument has been worked out (`fighter.c`, under
`RB_PORT`), with no line of `fighter_Update` changed.

Nothing warns of such a place, so `tools/port_order.py` finds them all:
it reads the statements the port's compiler made of every file, in the
order it will run them, and reports each statement of the source in
which a variable or member is read before a call that may write it (the
call's own writes and those of everything it calls, by variable, by
member's name, and through bare pointers to whatever has its address
taken), or two calls in two arguments are made in the other order than
written and one may write what the other uses. In the whole port that
is the seven calls above, and one more place twice, which was looked at
and does not matter (`config/port_order.txt`, with why). 68 other
places are evaluated in another order than on the PlayStation and
cannot matter (`--all` lists them): a table's sine beside a member,
`rand()` beside a member. `make port` runs it on every push and fails
on a place that is not in the list.

**A result that nothing returned.** The effects' handlers are called
through a table for a number, whether the effect lives on
(`eff_Start`, `eff_Step`). Eleven first handlers, the flashes', are
`void` and return nothing. On the PlayStation the number is then
whatever their last instructions left in the result's register, and in
each of the eleven that is the 1 they have just stored in `g_eff_stage`:
the effect lives. On a PC the register holds something else, and a
clash's spark, a lock's flash, a blunt hit's and an armour's were over
before they were drawn. The port calls the handlers through
`eff_RunHandler` (`eff.c`, under `RB_PORT`), which gives 1 for those
eleven: what the PlayStation's register holds, read from the listings
of all eleven. Four more functions end without the value they are
declared to return (`snd_Play`, `fighter_TryDropSecondSword`,
`eff_SetUpParticles`, and `font_Printf` called with the text alone in
`select.c`); no caller uses what they return.

### The disc

`src/port/disc.c` reads the player's image: the `.bin` of raw 2,352-byte
sectors, opened by itself or through its `.cue`'s first `FILE` line. It
hands out a sector by number, checked for its sync and for saying it is
that sector, and the 2,048 bytes of data of a run of mode 2 form 1
sectors. It knows nothing of the game.

`src/port/cd.c` is Sony's libcd on top of it, and the game's own disc
code (`cdrom.c`, its table of files, its state machine, its callback)
runs unchanged above that. There is no drive to wait for, so the
simplest behaviour that the game's code accepts was chosen:

- a command is done when it is given, and `CdSync` says "complete" from
  the next time it is asked;
- a read's sectors all arrive the next time the game asks the drive
  anything, one call of the game's callback each, in order, until the
  callback pauses the drive, as it does after the last;
- `CdRead`, Sony's blocking reader, which the game uses for the movie
  player, has read everything when it returns.

A sector that cannot be read (past the end of the image, or not a
sector) reaches the game as the drive's error, and the port says which
sector on its error output. Sound off the disc is the drive's too: an
XA stream is played ("Sound"), and the drive's position moves at the
disc's speed by the sound's clock while it plays, because the game
finds the end of a piece of music by asking where the drive is. A CD
audio track is a stub that is counted: the game asks for none.

So a load takes the frames its commands take and none for its sectors:
the port is at the top menu at blank 497 where the emulator is at
1,902. A script that has to press at the same moment of the game on both
counts the game's frames, not blanks ("Time").

### Time

The port's clock is a count of scan lines that only the game moves
(`src/port/clock.c`): `VSync` moves it to the vertical blank it waits
for, as Sony's does (263 lines a blank); `GetRCnt` of the scan line
counter moves it one line each time it is asked. What the game computes
between two waits takes no time. Nothing reads the PC's clock, so a run
is the same every time and on every machine.

**A frame of the game** begins where the game resets its counter of scan
lines, once a frame, after the wait for the blank (`ResetRCnt`,
`main.c`). The port tells a run's script there (`port_OnFrame`), and
the emulator's script stops at the same function, so a press or a
checkpoint counted in frames is at the same moment of the game on both,
however many blanks either took to get there. Counted in blanks it is
not: the emulator's blank falls while a frame is being worked out or
waited out, the port's always after, and a press an odd number of
blanks from a checkpoint landed in the next frame on one of the two.

**The frame's wait** (the owner's last open decision, settled for now by
that rule): `main` asks the counter until it has reached half a field
short of the frame, calling `rand()` each time, so on the port the wait
is exactly 384 calls of `rand()` in a frame of two fields and 128 in a
frame of one, and none in a frame that waited inside itself, as a load
does. The console's count is whatever time was left. The game's own
random numbers (`rng_Next`) do not come from `rand()`, and they are the
emulator's through three whole fights; what `rand()` feeds is effects,
the loading screen's noise and a few choices at a fight's start. The
effects' own memory is therefore not the emulator's once a fight is
under way (the particles' places), and is compared only on the fight's
first frame; nothing that decides a fight has been seen to differ.

### Sound

The port makes the game's sound and has no sound card yet: it mixes into
memory, and `--wav FILE` writes a run's sound as a WAV file. A later
stage plays the same samples as they are made.

**The boundary is the sound chip's registers.** Sony's libsnd and libspu
are compiled into the port as they are, 46 and 33 modules: they are
plain C that keeps tables, works volumes and pitches out, and writes the
chip's registers. So a call of the game's does what it does on the
console, the 524 bytes `SsVabOpenHeadSticky` fills into a bank's header
included. Under them is the chip in C (`src/port/spu.c`), which reads
the registers. By module:

| | In the port | Why |
|---|---|---|
| libsnd, all 46 modules | Sony's | plain C. `SSINIT` writes the registers by their address, `0x1F801C00`, so the port has memory there: a page at the console's own place (`sound.c`) |
| libspu, 33 of 34 modules | Sony's | plain C on the registers, through one pointer |
| libspu `SPU` (`spu.c`: the reset, transfers by the DMA controller, the timing loops) | the port's (`sound.c`) | it talks to the chip and the DMA controller. The reset writes what Sony's writes, without the waits. A transfer to or from sound memory is done whole when it is asked for, rounded up to the DMA's 64 bytes as on the console, and its interrupt is raised at once: the game waits for it in a loop with no frame between |
| `InterruptCallback`, `VSyncCallback`, `DMACallback`, `DeliverEvent`, `WaitEvent`, `DisableEvent` | the port's | the library hangs its tick on the vertical blank's interrupt (`SsStart`, at `SS_TICK60` on an NTSC console). The port runs it at each blank of its own clock |
| libcd's sound (`CdMix`, an XA stream, mute) | the port's (`cd.c`, `xa.c`) | the drive |

The one thing a register write does on the console that plain memory
cannot is start or stop a voice. The chip takes the two key registers
when it next makes samples, and clears them. Sony's library writes them
once a tick, the keys off and then the keys on, which is the order the
chip takes them in.

**The clock.** At each vertical blank (`clock.c`) the chip makes the
samples up to the blank, the tick runs, and the chip makes 32 samples
more before the game's code goes on. The game asks which voices are free
at the top of its frame, a fraction of a millisecond after the blank on
the console, and a footstep is as long as the time to the next: with the
tick before all of a blank's samples the port gave each footstep the
voice of the one before, where the console gives it another. The
emulator said so, at the fight's 68th frame; with the tick where it is
now the two agree, voice for voice and level for level. A blank is 263 scan lines of an NTSC
console at 15,734.2657 lines a second (4.5 MHz / 286), and the chip
makes 44,100 samples a second: 737 and 341/2500 samples a blank. The
fraction is carried, so a blank is 737 or 738 samples and a run's sound
is the same every time. The count of samples is the sound's clock, and
the drive goes by it.

**The chip** (`src/port/spu.c`), written from what is publicly known of
the hardware, with no emulator's code: 24 voices and 512 KB of sound
memory; a waveform's blocks decoded with the five filters and the three
flags (loop start, end, repeat) and the bit that says a voice reached
an end; the pitch counter and the four-point interpolation, whose table
of 512 numbers is the chip's own (no formula is known that gives it: a
Gaussian comes within 320 of 23,000 at best); the envelope with its
rates, steps and exponential modes; fixed volumes, and sweeps stepped
as an envelope is; noise and one voice's pitch moved by another, which
the game never uses (a run counts their samples: none in three fights);
the reverb, the documented network of two reflections, four combs and
two all-pass filters at half the rate, with libspu's own settings for
each mode; the disc's sound mixed in at its volume; the sum held within
16 bits, then the main volume.

**The music is XA.** Every piece of music and speech the game has is an
XA stream: eight stereo streams at 37,800 samples a second interleaved
in a file, one chosen by channel (`cdrom_StartAudio`; its other path,
CD audio tracks, is never taken: the stream number is a byte and cannot
be negative, and the disc is one track of data). The drive plays it
itself on the console. In the port, while a stream plays the drive's
place moves 150 sectors a second by the sound's clock, each sector that
passes is read from the image, and if it is sound of the file and
channel the game chose it is decoded (`xa.c`: four or eight bits, mono
or stereo, 37,800 or 18,900 a second), brought to 44,100 a second and
given the drive's four volumes (`CdMix`: 0x80 is all of a side, and the
game sets 0xFF). The game finds a piece's end by asking where the drive
is, as before; the drive's place went by blanks at sixty a second
then, and goes by samples now, which is the console's 59.83.

**What a PC's addresses hid.** `snd_FlushQueue` takes a bank for open
if its address has the top bit set, which every address of the
PlayStation's RAM has. The fight's banks are kept among the game's own
variables (`g_arena`, `g_snd_stage_vab_buffer`), and a PC's addresses
of those have no such bit: every sound of a fight was dropped, where
only the menus' sounded (their bank is in the mapped RAM). The stubs
had hidden it. In the port a sound whose bank is open at such an
address is started by `snd_StartQueuedSound` (`snd.c`, under
`RB_PORT`). It is the one such test the game makes; linking the port's
variables above `0x80000000` would end the whole kind, and is the
owner's to decide.

**What is not the console's**, and is marked in the files:

- The reverb's way down to half the rate and up again: the chip filters
  with 39 coefficients; the port averages two samples and draws a line
  between two answers.
- XA's way from 37,800 to 44,100: the drive filters with seven tables
  of 29 coefficients; the port draws a cubic through four samples.
- The few samples the chip takes to start a voice after its key; a
  sweep that falls or is turned over; a waveform block that names a
  sixth filter or more (played with none).
- A decay's first step: the port asks whether the level is under the
  sustain level after each sample of the decay, so a sustain level of
  everything ends the decay before it steps, unless the decay is the
  fastest. That is one reading of what is written.
- Not done, because the game uses none: the interrupt at an address,
  the external input, CD audio tracks. (The chip's own buffers of the
  disc's and two voices' last samples, at the start of sound memory,
  are written, as the console writes them: the game reads none, and
  they are how the emulator's samples were looked at.)
- The game sets the disc's volume to 0xFF, which is twice, and the
  fight's loudest moments are then more than 16 bits hold: 0.03% to
  0.05% of a run's samples are held at the limit. The emulator's music
  is 0.905 of the port's in level (below), so it would clip less
  often; which of the two the console is, is not known.

**How it is proved.** On every push, by made-up data alone
(`tests/port/spu.c`, `xa.c`, and their `.py`, which work the answers out
a second time from the rules as written, not from the port's code):
blocks of every filter and shift, by hand and by a second decoder; the
flags, the keys and the ended bits, sample by sample; the interpolation
against its table, and the table against what such a table must be
(512 weights that rise smoothly, the four of any place adding up to one
within a part in 32,000); the envelope's every kind at ten rates and a
whole envelope on a voice; the mixer's limits; a click through each of
Sony's nine reverb modes, which comes back, never louder, and dies away;
XA sectors in each of the format's forms; a straight line through the
change of rate; the drive's volumes; and the drive's place and the
sectors it plays (`tests/port/disc.c`). In the three fights the game's
state is what it was before there was sound: the trace of every watched
value at every blank is identical, and the fighters' memory at every
checkpoint differs only in addresses of the same places (the program
grew) and the words off the stack. The common sound bank's header now
has the 524 bytes it lacked. The sound itself was looked at by numbers
(`--wav` and `tools/port_wav.py`: each section's length, peak and RMS,
the voices started against the sounds the game asked for, blank by
blank) and as pictures: the menus' sounds at the presses, music in the
menus and the fight, 102 sounds in a fight of 507 frames, each on a
voice at the blank it was asked for, and no voice left sounding but the
one started a blank before the end.

**Against the emulator** (the play test `port_sound`,
[`tests/play/README.md`](../tests/play/README.md), "The sound"):
`port_game`'s run, and at the top menu, the fight's first frame, every
second of the fight, the kill, the replay, the menu after and the main
menu again:

- what Sony's libraries keep in the PlayStation's RAM is the same on
  both: the common bank's header (the 524 bytes too), where each bank is
  in sound memory and how long, each voice's registers as last set, the
  library's record of each voice with the envelope's level as it read it
  from the chip at its last tick, the keys, the reverb, the disc's
  volume. Frame by frame through the fight's first 150 frames the same
  held (a run by hand), but for the disc's volume in five of them: the
  port's drive seeks in no time, so its music is up five frames sooner;
- the chip itself: all of sound memory from `0x1000` on is the same,
  byte for byte, at every one of the twenty checkpoints (every bank's
  waveforms, where the game moved them to close the holes, the reverb's
  work area), and so is every register a program writes. The emulator's
  chip is reached by the one way a script has: its whole saved state.
  Not compared: the first `0x1000` bytes, which are the chip's buffers
  of recent samples, and the registers that are the chip's own or that
  the port's chip takes away (a voice's envelope level and loop
  address, the keys, the voices that ended, the port for data by hand):
  the emulator's state has those only as last written.

**What is heard** was compared once, by hand, through those buffers of
recent samples, 512 of each (11.6 ms) at each checkpoint:

- what voices 1 and 3 play, wherever one was sounding (eight buffers
  at seven checkpoints): lined up, in four of them half the samples
  differ by 1 or less, of amplitudes in the thousands, and in two of
  those nine in ten by 5 or less; in the other four the middle
  difference is 2, 6, 37 and 50, where the sound was quiet or was
  beginning or ending in the buffer, which is also where the largest
  differences are. So the decoder, the interpolation and the envelope
  are near the emulator's to the last bit or two; they were not held to
  it sample for sample;
- the disc's music at the top menu: the emulator's 11.6 ms are in the
  port's WAV, with a correlation of 0.9985 on each side, at 0.905 of
  the port's level; what is left after that is 4% of the signal, which
  is the two ways of bringing 37,800 samples a second to 44,100. The
  level is not explained: the port takes the drive's volume of 0xFF
  for 255/128.

**Not proved:** the reverb against the emulator (`port_game`'s stage has
none); the mix as it leaves the chip (the emulator has no way to write
its sound to a file with no window; the buffers above are before the
voices' and the main volume); and that the emulator is the console.
`--spu FILE` writes the port's sound memory and registers at a run's
end.
### The picture

The port has the PlayStation's GPU in C (`src/port/raster.c`,
`include/port_gpu.h`): the 1024 by 512 pixels of video memory and what
the words written to the chip's two ports do to them. Sony's libgpu in
the port (`src/port/gpu.c`) writes it the words Sony's own writes the
console's: a drawing environment's primitive, with the fill that clears
its area; `ClearImage`'s fill or drawn rectangle; `LoadImage`,
`StoreImage` and `MoveImage` as the three copy commands; the display's
start, range and mode; and `DrawOTag`, which walks an ordering table by
its 24-bit links and gives the GPU each primitive's words. What Sony's
keeps in a queue until the GPU is free is done at once, so `DrawSync`
never waits. There is no window: the picture is that memory, and a run
writes it to files.

**What it draws**, as the chip is publicly known to:

- triangles and four-sided polygons, flat, shaded, textured or both; a
  four-sided one is two triangles, the first three corners and the last
  three. A triangle covers the pixels inside it and those on its top
  and left edges, not on its right and bottom ones, so nothing along a
  shared edge is left out or drawn twice;
- a colour or a texture coordinate as a plane over the triangle, with
  twelve bits of fraction, from the corner furthest left;
- textures of 4 bits and 8 through a colour table and of 15 bits, the
  texture window, a texel of nothing left undrawn, a texel times the
  colour (128 is one) or as it is;
- the four ways of mixing a half-transparent primitive with what is
  there, on a textured one only where the texel's top bit is set;
- the mask bit, set and looked at;
- dithering by the chip's four-by-four pattern, on shaded and on
  modulated polygons and on lines, never on a flat polygon, a rectangle
  or a texture drawn as it is;
- lines of one piece and of many points, flat and shaded;
- rectangles of their own size and of the three fixed ones, textured or
  not; the fill; the three copies, in, out and across;
- the drawing area, the offset, the limit of 1023 by 511 on a
  primitive, coordinates of eleven bits with a sign;
- the display's rectangle, by its start, its mode's width and its lines.

**Where the links point.** A link is the low 24 bits of an address, and
the port's memory is in several places that differ above those bits. The
places primitives may be are named once (`port_AddLinkRegion`): the
console's RAM, the scratchpad, the program's own variables and the
game's stack. So that no two of them have the same low 24 bits, the
program is linked 4 MB higher than Linux usually has one
(`tools/port_build.py`), and the game runs on a stack of the port's own,
a megabyte at `0x7FC00000` (`PORT_STACK_ADDR`), not the system's, which
is somewhere else in every run. A link that is in none of the places, or
in two, stops the port, which says so.

**Frames out.** `--shots FOLDER` writes the displayed rectangle as a PNG
every sixty vertical blanks (`--every N`: every N), by an encoder of the
port's own (`src/port/picture.c`: 8 bits a channel, the pixels stored,
not packed); `--vram FILE` writes all of video memory when the run ends.
Under a plan, a checkpoint writes its picture as the emulator's script
does and, where the test asks, video memory whole
([`tests/play/README.md`](../tests/play/README.md), "The picture").

**How it is held to the emulator.** PCSX-Redux run with no window draws
with its software GPU, and its Lua gives that GPU's video memory
(`PCSX.GPU.getVRAM()`). A play test's checkpoint can ask for it
(`vram = true`); the port writes its own at the same checkpoint, and the
two are compared pixel for pixel, all 524,288, the textures and colour
tables beside the picture's two buffers. Two tests do: `port_picture`,
through every kind of screen, and `port_other_fight`, a second stage.
On every push `tests/port/raster.c` holds the GPU to 404 pixels and
counts worked out by hand, or by the fill rule written a second way:
shared edges, each texture depth, each mix, the mask, the pattern,
clipping, lines, the copies, the walk over an ordering table, the PNG.

**Two things the emulator's GPU does otherwise**, found by the
comparison. The port does them as the chip is best known to, and has a
switch that does them the emulator's way (`port_SetGpuLikeEmulator`, a
test's `port_gpu = "emulator"`), which the two tests set and nothing else
does:

- *Dithering a flat textured polygon.* The console dithers a polygon
  whose texture is multiplied by a colour, shaded or not; the emulator
  only the shaded ones. The stages' floors and walls are such polygons.
- *How fine a texture coordinate is.* With twelve bits of fraction a
  coordinate falls behind by up to a 4096th of a step for each pixel
  across. On a wall hundreds of pixels wide and close to the camera,
  where one texel is sixteen pixels, that moves the texture's seams two
  or three pixels; the emulator's are where exact arithmetic puts them.

**The numbers.** With the switch set, of the 524,288 pixels of video
memory, at each checkpoint:

| Checkpoint | Pixels that differ | What they are |
|---|---|---|
| The options page; the boot image; the top menu, before the fight and after it | 0 | |
| Character select: the fighters' list | 1,828 | 1,624 are the cursor and its mark, which swell and shrink by the screen's own count of frames: the port, which loads the screen's files in no time, is at another count. The other 204 are the ones the next screen has too |
| ... the weapons' list; the weapons crossed; the stages' list | 204; 12; 58 | halfway values (below) |
| The first fight, each of its 16 seconds | 148 to 440, and 964 in the last | halfway values; in the last seconds also the dust and sparks `rand()` places |
| ... the kill; the replay; the menu after it, twice | 586; 418; 177 and 173 | the same |
| The second fight (`port_other_fight`), each of its 20 seconds | 294 to 906 | the same |
| ... the kill; the replay; the menu after it, twice | 965; 721; 593 and 494 | the same |

Each fight checkpoint holds two whole frames, 307,200 pixels, so at most
three in a thousand differ. A *halfway value* is a colour or a texture
coordinate that comes out exactly between two whole numbers at a pixel:
the port, with the switch set, takes the higher; the emulator, which
walks down a triangle's edge and then across with fractions of its own,
has lost a little by then and takes the lower about one time in six.
Which it will be follows from its arithmetic, which is not known here:
3,042 such pixels of four frames were tried against eleven guesses at
it, and the best explains 93 in a hundred. Everything else
is the same: which pixels a primitive covers, the order they are drawn
in, the textures, the mixes, the pattern. The test allows each
checkpoint exactly what was measured.

Not compared: the fight's first frame, where the loading screen's noise
is on show (41,000 pixels differ: `rand()` places the streaks, and the
frame's wait stirs it, "Time").

Without the switch, as the port draws when nothing asks otherwise, the
same checkpoints differ in 96,000 to 111,000 pixels in the first fight,
all but 400 to 800 of them by one step of a channel's 31: the floor's
dither. The options page differs in 790, the dither of its title.

**Not sure, and not done** (each is marked in `raster.c`):

- The twelve bits of fraction and the corner the values are counted
  from are what is written of the chip, and nothing here could check
  them against one. If the chip is finer, the port is wrong where the
  emulator is right, by the two or three pixels above.
- A line's pixel when the true line passes exactly between two: the
  port takes the one away from the line's start. A flat line is dithered
  like a shaded one.
- A copy inside video memory whose two rectangles overlap is done a
  pixel at a time from the top left; the chip is said to work in pieces.
- With the display interlaced and drawing on it forbidden, the chip
  leaves out the lines of the field on show. Not done: the game's
  display never is.
- The displayed rectangle is as wide as the mode says, whatever the
  horizontal range. The 24-bit mode, which only the movie uses, is read
  for a PNG and nowhere else.

## 64 bits

The port is built with 64-bit pointers and with 32-bit ones, from the
same source: `make port` builds both, the default's program at
`build/port/rebushido` and the other's beside it (`build/port32/` or
`build/port64/`); `make port PORT_BITS=64` (or `32`) builds one. Linux
distributions are dropping their 32-bit libraries and macOS has none, so
64 bits is where the port is going. The 32-bit program stays for now
because comparing the two is the cheapest proof there is: every
difference between their runs is a mistake about pointers.

### The design

**Everything the game can point at is below 4 GB.** It already was: the
console's memory at the console's addresses ("Memory"), the game's stack
at `0x7FC00000`, and the program itself, which is linked at a fixed
address with no position independence, so its functions and variables
are below 4 GB too. An address is then a 32-bit number in a 64-bit
program exactly as in a 32-bit one, and the game's habit of keeping
addresses in `s32` loses nothing. The mapping, the twelve fixed
addresses, the 24-bit links of an ordering table (`port_AddLinkRegion`)
and the save file's patches mean what they meant. What is not below
4 GB is the port's own and the system's: the stack the system starts
the program on, the program's arguments, memory the port asks for
itself. The game is never given a pointer to any of it.

Three things are then left, and each has one form in the source.

1. **A number made a pointer** has to be the number's low 32 bits with
   nothing above them. C would carry the sign of an `s32` into the upper
   half, and every address of the console's RAM has its top bit set. The
   cast's operand goes through `ADDR()` (`include/common.h`):

   ```c
   prop = (StageProp *)ADDR(g_stage_props.props);
   ```

   On the PlayStation `ADDR(x)` is `(x)`, so the line compiles to the
   bytes it always did. In the port it is the low 32 bits, unsigned.
   The other direction needs nothing: a cast from a pointer to `s32` or
   `u32` keeps the low 32 bits, which is all of the address.

2. **A pointer kept in memory whose layout is not the port's to
   choose** stays four bytes wide: a member of a structure that is laid
   over a file from the disc and relocated in place, over the
   scratchpad, over the console's RAM or the arena, or that the renderer
   reads at the console's offsets; an entry of a list of pointers inside
   such a file; a word of the scratchpad that holds an address. Its type
   is `PTR32(type)`, it is read with `PTR()` and written with
   `SET_PTR()`:

   ```c
   SpriteSheetSlot *sheets = PTR(ctx->file->top_sheets);
   draw.header = PTR(sheets[TOP_SHEET_TITLE]);
   SET_PTR(node->parent, &bones[parent].node);
   ```

   (`SpriteSheetSlot` is `PTR32(SpriteSheet *)` with a name, for where
   one declaration's slot is another's: `sprite.h`, and its like in
   `node.h` and `eff.h`. `NEXT_PTR(slot)` and `ADD_PTR(slot, count)`
   are `slot++` and `slot += count`.) On the PlayStation `PTR32(type)`
   is the type, `PTR(slot)` the slot and `SET_PTR(slot, value)` an
   assignment. In the port a slot is a 32-bit word that carries the type
   it points at, so the compiler refuses a use that forgot `PTR()`:
   nothing has to be found by eye.

3. **A structure that is only ever the program's own** (declared as a
   variable or a local, never laid over anything, with no size or offset
   written as a number) keeps ordinary pointers and simply grows.
   `Fighter` is the largest. Each such structure is listed, with why it
   may grow, in `config/port_layout.txt`: 25 of the game's and Sony's.

Where the logic itself has to differ there is an `#if RB_PORT`, as
everywhere else in the port: nine blocks. The game's two functions that
walk their arguments on the stack use the compiler's own list (`sys.h`:
a 64-bit program's first six arguments are in registers). Two tables
that the PlayStation's compiler fills with an address in a 32-bit word
are filled as the program runs, because a 64-bit compiler cannot
(`_snd_seq_tick_env` in Sony's `ssstart.c`, and the fight's table of
best-times sheets in `hud.c`, whose members are slots because the main
menu's file has the same table); a third, the build's date in `main.c`,
which nothing reads, has no address in the port.

**Why these forms and not others.** The rule on this branch is that the
original's lines stay as they are, with the port's code beside them
under `#if RB_PORT`. Here that would have meant an `#if` block round
each of 250 casts and each of 800 uses of a member, which would bury the
source. The forms above change the original's lines in place and change
no byte of any PlayStation build: they are macros that expand to the
original expression there, the edit is mechanical, and `decomp` is not
touched. What was weighed against it:

- *Leaving the source alone and letting the build fix the casts*, from
  the compiler's own list of them. It keeps every line, and it hides
  from the reader that a cast is a place where an address is made; the
  program would not be what the source says, and it does nothing for a
  member's layout.
- *A 64-bit processor's 32-bit pointers* (`gcc -mx32`). No source
  change at all, but only Linux has that kind of program, its kernels
  are mostly built without it, and it cannot be linked with a 64-bit
  library, which is the reason for going to 64 bits. GCC has no 32-bit
  pointer type for a 64-bit program otherwise (it refuses the mode).
- *Moving the console's memory below 2 GB*, so that a sign carried up
  changes nothing. The addresses in the game's files and in what the
  play tests compare would no longer be the console's.
- *Copying each file into a structure of the port's own as it is
  loaded.* It leaves the code that uses the files alone and adds memory
  with a lifetime of its own for every kind of file; the slots need
  none.
- *Every structure's pointers as slots.* Then nothing would grow and no
  list would be needed; but `Fighter`'s `other` alone is read 241 times
  in the fight's code, and a structure that is only the program's own
  gains nothing by it.

**The program's own variables** lie as in the 32-bit program but where
a pointer is wider: the build sets each variable's boundary to the
PlayStation's ("Memory", "Neighbours"), and the 64-bit program is
compiled without the processor's vector instructions (`-mno-sse`), which
would want the 16-byte boundaries a 64-bit compiler gives and the build
takes away. Nothing in the program is a floating-point number.

**How every place is found.** By the compiler and by tools, not by
reading:

- the game's files are compiled for the port with three warnings as
  errors (`tools/port_build.py`, `GAME_ERRORS`): a number cast to a
  pointer of another size, a number where a pointer is wanted, a
  pointer of another type. A cast without `ADDR()` stops the build, and
  so does a slot used without `PTR()`, which is not a pointer at all;
- `tools/port_casts.py` finds what the compiler lets through: a cast
  whose operand is already 64 bits wide, which C makes without being
  asked (`memory + 2 * sizeof(PrimPool)`: `sizeof` is 64 bits wide
  there, so the `s32` is widened with its sign before the sum), and a
  negative number written into the source (`(SpriteSheet *)-1`, the
  mark some lists end with: `0xFFFFFFFF` in a file). It reads the form
  the compiler makes of every file in which each value has its type;
- `tools/port_layout.py` reads the size of every structure and the
  place of every member out of the debug information of the 32-bit and
  the 64-bit program and, by a probe compiled with the PlayStation's own
  compiler, of the PlayStation program. A structure whose layout
  differs anywhere and is not in `config/port_layout.txt` fails the
  build, and so does a line there for a structure that no longer
  differs;
- every host test is built and run as a 64-bit program as well
  (`tests/run.py --rebushido`), and the two programs' runs are compared
  with each other, value for value ("What is proved").

`make port` runs all of it on every push.

### What changed

| | |
|---|---|
| `ADDR()` round a cast's operand | 250, in 35 files (`hud.c` 32, `resource.c` 23, `menu_pages.c` 19, `select.c` 19, `model.c` 17, `tex.c` 16) |
| Members and words made `PTR32` slots | 91 members of 25 structures, and four words of the scratchpad (the mesh cursor, the tiles' three) |
| `PTR()` | 564, in 22 files (`menu_pages.c` 94, `fighter_hit.c` 88, `select.c` 75, `select_team.c` 71, `eff.c` 64, `hit_test.c` 48) |
| `SET_PTR()`, `NEXT_PTR()`, `ADD_PTR()` | 231, 11 and 16 |
| `#if RB_PORT` blocks | 9 more: 123 |
| Lines of the game's files and headers that are not as they were | 1,290 of 63 files, every one the substitution or the formatter's rewrapping of it |

**The structures whose layout is fixed**, and so the same in all three
builds, by what fixes it:

| What it is laid over | Structures | |
|---|---|---|
| A 2D screen's resource file, relocated in place | `SpriteFileHead`, `MenuFile`, `MenuChanbaraSheets`, `BestTimesSheets`, `SelectFile`, `SelectPortraits`, `TeamSelectFile`, `NarrationFile`, `StaffFile` | a list of sheets is `SpriteSheetSlot`s, and a file's pointer to a list a `SpriteListSlot` |
| The console's RAM after such a file | `PrimPool`, `MenuWork`, `TeamSelect` | their size is where the next thing goes |
| The arena, whose room is fixed | `Model`, `ModelNode`, `ModelCloth`, `ClothPiece`, `ClothUpdate`, `ClothBoneColliders` | the lists of a body's matrices are `MatrixSlot`s: the renderer walks them |
| A node the renderer reads at the console's offsets | `SceneNode` (its parent), and with it `PlainNode`, `StageNode`, `StageProp` | |
| The scratchpad | `EffRequest`, `DustWork`, `ParticleWork` (`EffScratch`), `WeaponHitScratch`, `SegmentCast` (`FightScratch`), `SideViewScratch`, and the renderer's words | the fighters keep what other code leaves there |
| The save file | `SaveData` (its 22 patch addresses) | a card written by one size is read by the other |
| The replay's buffer | `ReplayFighterFrame` | |

### What it found

Mistakes about pointers that were in the port already, or would have
been in the 64-bit one, each found by a tool or by a comparison:

- *A sign carried up*: the 250 casts, of which the first to stop the
  64-bit program was the common sound bank's address at start-up.
- *A sum made 64 bits wide before its cast*: five places, all a 2D
  screen's file found after its pools (`memory + 2 *
  sizeof(PrimPool)`), in `select.c`, `select_team.c`, `narration.c`
  (two) and `staff.c`. Found by `port_casts.py`; character select
  stopped on the first.
- *The end of a list*: `(SpriteSheet *)-1` compared with an entry of a
  file's list, in `menu_pages.c` and `staff.c`.
- *A slot read as a pointer*: the renderer's C read a cloth node's
  points (`render_DrawCloth`) and a body's matrices as 8-byte pointers
  out of 4-byte slots. In the fights the bytes after the slot happened
  to be zero; the host test of the renderer, which makes its memory up
  at random, stopped on it as a 64-bit program.
- *A word of the scratchpad that holds an address*: the mesh cursor and
  the tiles' three, written as 8 bytes over the word after each.
- *The tests' own*: the play tests read the fighters at the
  PlayStation's offsets, which are not the 64-bit port's ("What is
  proved"); a host test hands the game the address of a local, which in
  a 64-bit program is above 4 GB unless the test runs on a low stack.

Seen on the way, in both sizes, and not put right here: the port's
compiler leaves out the cells written after three sheets of `hud.c` (the
best times' title, ranks and marks: "excess elements in array
initializer", since a `SpriteSheet` ends in an array of none), and says
of five calls that they are "through a non-compatible type" (`hud.c`,
`select.c`, three in Sony's `midiread.c`), which it may make a stop of.
No run of the tests reaches them.

### What is proved

- **The two sizes against each other**, port side only, on the three
  fights' plans (`port_game`, `port_other_fight`, `port_bugfixes`): the
  trace of every watched value at every blank where one changed is the
  same line for line (2,647, 2,715 and 2,650 lines); the frames written
  every thirty blanks are the same files (129, 137 and 130 PNGs); the
  run's sound is the same file (11,440,400, 12,148,048 and 11,528,856
  bytes of WAV); video memory at the end is the same, and at every
  checkpoint that writes it; sound memory and the chip's registers are
  the same; and both fighters are the same at every checkpoint (21, 25
  and 21), member for member, but for the words off the stack that the
  tests leave out against the emulator too. The 32-bit program's runs
  are the ones it gave before any of this, byte for byte.
- **Wider runs, the two sizes against each other** (by hand, not kept
  as tests): seven runs of 10,000 blanks, each down another row of the
  top menu (the story and the six rows after it), with other fighters
  and weapons taken on the select screens and then some 600 made-up
  presses on both pads. They go through one-player fights against the
  CPU, other stages and other select screens than the tests'. In all
  seven the two programs' traces are the same line for line (831 to
  9,588 lines, 45,662 in all) and so is every frame written (60 to 333
  PNGs, 2,002 in all); the sound, video memory and the sound chip's are
  the same files in the five that ran to their end; and both fighters
  are the same member for member at all 260 checkpoints, but for the
  first eight bytes of a CPU fighter's pad (`cpu_pad`), which are words
  off the stack like the player's pad's. Two of the seven stop, the
  same way in both sizes: the story's first fight reads through a
  pointer that is not set yet ("What is left").
- **The 64-bit program against the emulator**: the five play tests
  (`port_game`, `port_other_fight`, `port_bugfixes`, `port_picture`,
  `port_sound`), with no tolerance changed and no comparison taken out.
  The fighters, which the 64-bit program lays out otherwise, are read
  member by member as the PlayStation has them
  ([`tests/play/README.md`](../tests/play/README.md), "Memory at a
  checkpoint"): a pointer is still compared by the name it points at,
  and one fighter's pointer to the other by the member.
- **The layouts**: `tools/port_layout.py` on every push. Of the 246
  structures the programs use (178 of them declared in the headers, and
  so in the PlayStation's probe), 38 are not laid out the same in the
  64-bit program: the 25 listed, and 13 of the port's own. None differs
  between the 32-bit program and the PlayStation.
- **The host tests as 64-bit programs**: all 32 that are programs pass
  the same checks, the renderer's 9,800 made-up cases, the GTE's and
  the assembly's with the digests written down when the emulator last
  agreed, the rasteriser's 404 pixels and the sound chip's among them.

### What is left of it

- `src/port/host_windows.c` is written for both sizes and compiled to
  an object by MinGW's two compilers with no warning; it has not been
  linked or run. Windows gives memory at an address in blocks of 64 KB,
  which it now allows for (the sound chip's page is in the scratchpad's
  block); it does not yet tell the system of the game's stack, which
  the system wants to know of before it will call a fault's handler.
- macOS: an Intel Mac's 64-bit program can be linked low and have the
  memory. On Apple's own processors a program's first 4 GB are kept
  empty by the system, and whether that can be turned off is not known
  here; if not, "everything below 4 GB" does not hold there and the
  port needs another way (the console's memory at an offset, the slots
  as offsets into it).
- A C library, when the port takes one for its window: its own
  allocations may be above 4 GB, which is right as long as the game is
  never given one. The program must still be linked without position
  independence, at its fixed address.
- 123 pairs of neighbouring variables are not as far apart in the
  32-bit program as on the PlayStation; in the 64-bit one 248 are not,
  137 of them because the first has pointers, which are twice as wide.
  (Before this stage 219 were not: a PC's compiler started an array of
  halfwords or bytes on its elements' boundary, where the PlayStation's
  starts every array on a word's; the build now sets each variable's
  boundary. A 64-bit compiler's own rule, 16 bytes for an array of 16
  or more, would have made it 485.) None is known to matter and the two
  programs' runs do not differ, but as before they have not been ruled
  out one by one.
- The 32-bit program is still built and tested. It can go when the
  window's build is 64 bits too.

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
| BIOS calls | 40 two-instruction jumps (`BIOS_A_FUNCTION` and its like), 2 system calls, `_96_remove` | `src/main/psxsdk/libapi/`, `libcard/` | Not C equivalents: the kernel does not exist on a PC. The port supplies what the game calls (below) | defined; the card's and `Exec` are stubs |
| Sony's other assembly | 8 functions that patch the kernel's pad and card drivers, `_patch_gte`, `setjmp`, `SetSp` | `libapi/`, `libcard/`, `libc2/`, `libgte/patchgte.c` | None: they serve Sony's interrupt and driver code, which the port replaces whole | not needed |
| Start-up | the PS-X EXE header, `__SN_ENTRY_POINT`, `__main`, `__do_global_dtors` | `asm/header.s`, `libsn/snmain.c` | The PC's own start-up; `main` is the game's | not needed |
| The development host's file calls | `PCopen`, `PCclose`, `PClseek`, `_SN_read`, and `pollhost` (a `break`) | `libsn/` | The game's two loaders that use them (`main.c`) are never reached on a retail disc; the port leaves them out or gives them the PC's files | not needed |
| The movie overlay | its entry, one BIOS jump, the bitstream decoder (279 instructions and its tables) and the DMA routine (120) | `movovl/asm/` | See "The movie overlay" | left |

### The BIOS calls the game makes

Sony's headers already declare these (`include/psxsdk/libapi.h`,
`kernel.h`), and the game's code calls them by those names; the port's
platform layer defines them (`src/port/`). Counted in the game's own
code and ReBushido's, not in Sony's. The last column is what the
finished port supplies; what it supplies today is in "Sony's libraries
in the port": events, the counters, the pads' packets and the nothings
are done, the card answers "no card", `Exec` returns at once.

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

**Where it keeps an address in 32 bits.** An ordering-table link is 24
bits of a primitive's address (`render_LinkEntry`, the tiles' chain),
and some primitives are in the mesh itself. Addresses are kept in 32-bit
words of the scratchpad (the ordering table, the handler table, a tile's
vertices and its ordering-table entry) and in 4-byte slots of the data:
the tables of mesh pointers, the list of a body's bone matrices, the
pointer after a body's or a cloth's node, a shadow's parent. The mesh
cursor is brought to a whole word by its address. In the 64-bit program
every one of these is still 32 bits wide, read through `ADDR()` or as a
`PTR32` slot ("64 bits"): a word that is an address stays the size the
listings have it.

## Everything else

Measured on the source as it is (`src/main/*.c`, 55,684 lines).

**Addresses are 32-bit integers.** The game keeps addresses in `s32`
everywhere: about 130 places turn an integer into a pointer and about 120
a pointer into an integer (counted from the casts; perhaps 300 to 400 in
all), and whole interfaces carry them (`cdrom_StartRead(file, dest)`,
`arena_OpenBlock`, the HUD's primitive cursors). Compiled with 64-bit
pointers none of that means the same unless every address stays below
4 GB and each cast says so, which is what the port does ("64 bits": 250
casts made a pointer of a number, some 230 a number of a pointer).
Besides:

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

Stage one, the C for the assembly, is proved by the probe, the first six
points below. Stages two and three, the program, by the last three.

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

- **The program against the emulator.** Three play tests
  ([`tests/play/README.md`](../tests/play/README.md), "The port as a
  disc") give the emulator, with the ReBushido disc and no memory card,
  and then the port, with the retail image, the same plan: Start on the
  options page, past the opening and the title, down the top menu to
  Vs. Mode, both pads through character select, a fight until one of
  the two is cut down, the replay, the menu after the fight, and from
  it back to the main menu. `port_game` is the fight of
  `same_as_original` (a kill on the fight's 507th frame);
  `port_other_fight` has other weapons and another stage (a nodachi
  against a broadsword, a kill on the 626th frame); `port_bugfixes` is
  the first with Bugfixes turned on at the options page. In each, at
  the page, the first screen, the top menu, character select, the
  fight's first frame, every second of the fight, the kill, the replay,
  the menu after and the main menu again (27 to 31 checkpoints), every
  value of `state.toml` is the same on both, 53 of the 54: the screen
  and its frame count, the menu's page, the game's random numbers, the
  mode, the stage, what was asked for, and both fighters' places,
  wounds and frames. The one left out is the count of frames shown
  since power on, which is timing (the port shows no movie and loads in
  no time). From the fight's first frame on, both fighters' whole
  structures are compared as well, 2,200 bytes, at every one of those
  checkpoints, an address counting as the same if it is the same place
  of the same name; what is left out of them is below. On the fight's
  first frame the memory the game loaded from the disc is compared byte
  for byte: the effects' tables and textures (38,912 bytes) and the
  first 256 KB of the load area, which hold the stage. All of it
  agrees. The memory is compared live, between the two runs, and kept
  nowhere. The tests run where the retail image and the emulator are,
  and are skipped, saying so, where they are not.
- **What the comparison found**, and what was done:
  - *The common sound bank* (3,328 bytes at `0x8010DB00`) differed in
    524 bytes, all in the words of its header that Sony's
    `SsVabOpenHeadSticky` fills in as it opens a bank: a stub's answer.
    Sony's own code fills them in now ("Sound"), and `port_sound`
    holds the whole header to the emulator's.
  - *The movie's work area* (the 2 KB before the load area) differs:
    only the emulator played a movie. Not in the test.
  - *The fight after its first presses.* With the presses of
    `same_as_original` the port's fight left the emulator's at the first
    strike, by one frame: a press timed an odd number of blanks from a
    checkpoint falls into the next frame on one and not on the other.
    Timing: the fight's presses and checkpoints are counted in the
    game's frames now ("Time").
  - *The first clash of blades*, on the fight's 166th frame: the
    emulator's second fighter stands, the port's leaves the ground. The
    order of evaluation ("What the PlayStation's compiler decided"),
    put right. It was found by comparing all of the game's variables,
    the mapped memory and the scratchpad frame by frame, and then
    inside the frame at the calls of one function after another
    (`tools/port_compare.py`): everything agreed as `fighter_StartMove`
    was called and not after it.
  - *A clash's spark*, on the 323rd frame: the emulator has one and the
    port had none. A result that nothing returned (the same section),
    put right. It changed no fighter's place or wound; it showed as
    what the spark leaves on the scratchpad, which the fighters keep
    for a weapon's third point and an off-hand weapon they do not have.
  - *Words off the stack*, which stay different and are left out of
    the comparison of the fighters, 28 bytes of the 2,200: the fourth,
    unused word of `foot_pos` and of `prev_foot_pos`, which
    `fighter_Update` copies from a local it sets three words of; the
    pad's types and answers, which `pad_ClearState` leaves as they were
    on the frames with no pad; and in the second fighter the last word
    of the off-hand weapon's last places, read from the scratchpad
    where a cast through the stage left such a word. The PlayStation
    has an address of its own program there, the port an address of a
    PC's stack or nothing. Nothing reads them that the comparison has
    seen: were one read, the fights would differ.
  - *Two crashes*, both the original's use of address 0 ("Memory").
    What the emulator has at address 0 was read: zeroes, as the port
    gives.
- **The picture against the emulator's**, in `port_picture` and
  `port_other_fight`: video memory at 28 and at 24 checkpoints, with
  what differs counted ("The picture").
- **The GPU by made-up cases**, on every push: `tests/port/raster.c`,
  404 checks of pixels worked out by hand ("The picture").
- **The reader and the platform layer by made-up cases**, on every
  push: `tests/port/disc.c` (an image of forty made-up sectors:
  addressing, the data's offsets, reading across sectors, a sector of
  sound, a broken one, the end of the image, and a read through the
  port's libcd as the game asks for one), `tests/port/platform.c` (the
  clock, the frame's wait and its count of `rand()` calls, a pad's
  packet, events, the primitives' words, `printf`) and
  `tests/port/memory.py` (the fixed addresses).
- **The sound by made-up waveforms and sectors**, on every push:
  `tests/port/spu.c` and `xa.c` ("Sound").
- **`make port` on every push**: the program links with no name
  missing and the port's files have no warning, with 64-bit pointers
  and with 32-bit ones; and the three checks of "64 bits" (the layouts,
  the casts, the order of evaluation in each size).
- **The 64-bit program against the 32-bit one**, and every host test
  as a 64-bit program ("64 bits", "What is proved").

Not proved that way: `InitGeom` (it patches the console's kernel; its
register values are checked by hand), `sys_Trap`, and anything the
emulator's GTE itself has wrong. Of the renderer: what "Where the C is
not the listing" names (those words of the scratchpad are left out of
the comparison), a mesh that ends wrongly and so stops the game, and the
two functions of the game's C the walk calls for a shadow and for the
surf, which both programs have stand-ins for. The cases are made up, so
a mesh of the game's that does something no made-up one does is not
covered by them: the whole frames the port now draws are the check of
that, and in two fights' frames no primitive is out of place ("The
picture"). Where the emulator and what is written
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
   the top of `render_asm.c` named the listings; they are behind the
   switch now.)
2. **A program that links.** Done: `make port`, 225 files, no name
   missing ("The program").
3. **Memory and the disc.** Done: the game's own loaders read the files
   of the boot, the menus and a fight from the image, and the values
   the play tests watch are the emulator's at six checkpoints, into the
   fight ("How it is proved"). Left of it: nothing that this stage
   named; what was found beyond it is under "What is left".
4. **The picture.** The ordering table drawn by a rasteriser in C that
   does what the console's GPU does. *Done when* the pictures at the play
   tests' checkpoints are the emulator's, pixel for pixel, with no window
   open; then the menu in a window. The first half is done as far as it
   can be: the flat screens to the pixel, a fight but for three pixels
   in a thousand, each one accounted for ("The picture"). Left of it:
   the window.
5. **Pads, time, a fight.** *Done when* a scripted fight on the port goes
   through the same watched values as the same script on the emulator.
   That part is done, in three fights to their ends ("How it is
   proved"). Left of it: the PC's own pads and the PC's own time, which
   come with the window.
6. **Sound.** The sound chip, Sony's sequencer, the disc's audio.
   Done into a file ("Sound"): the game's effects and music are in a
   run's WAV, the game's state is what it was, and the chip's memory
   and registers are the emulator's (`port_sound`). Left of it: a sound
   card (with the window's library).
7. **The rest.** Movies, the memory card as a file, the link (or
   nothing in its place).

The wide picture and the high resolution are the roadmap's next item and
build on stage 4.

## What is left

How far the program gets today: with `port_game`'s presses, through a
versus fight to its kill, the replay, the menu after it and back to the
main menu, as the emulator does. Nothing stops it; what it cannot do is
listed here.

**The stubs the game called on the way** to the fight's first second,
with how often, are the list of what stage 7 writes (a whole fight
and the way back to the menu call the same ones, more often, and no
other):

| Stage | Stub | Calls |
|---|---|---|
| 7, the card | `_card_info` | 24 |
| | `InitCARD`, `StartCARD`, `_bu_init` | 2 each |
| | `StopCARD` | 1 |
| 7, the movie | `Exec` | 1 |

Not called on that way, and stubs too: the card's files (`open`,
`close`, `read`, `write`, `firstfile`, `nextfile`, `format`),
`_card_load`, `_card_clear`, a CD audio track (`CdlPlay`), and the
link's three. Sound has no stub left ("Sound").

**Known, and not done:**

- Three fights are proved, all of two players, of the same two
  fighters, each ended by a cut. Not run against the emulator: a fighter
  played by the CPU, the other modes, guns, thrown things, a fall from
  a stage, a lock of blades.
- Words off the stack differ, in padding and in places nothing was seen
  to read ("What the comparison found"). The port's are addresses of
  its own stack, which is at the same place in every run, so a run is
  still the same twice; they are not the console's.
- The picture has no window, and the two things the port draws as the
  chip and not as the emulator are proved by made-up cases alone ("The
  picture", with what is not sure).
- The picture is drawn when the game asks, with no time taken: a frame
  the console would not have finished drawing is finished here.
- What a function leaves in the result's register when it returns
  nothing was looked for by the compiler's warnings and by reading the
  one table of handlers that is called for a result; a `void` function
  reached through some other pointer for a result would not have been
  seen.
- Address 0: two places are given somewhere else to read and write. A
  third is found and not put right: as a story fight begins, the
  henchman's shout is placed by the camera before a camera is chosen
  (`story_HenchmanShout`, `snd_PlayAt` reading `g_camera_current`), and
  the port stops there, in either size. The story cannot be played on
  the port until it is. The others, if there are others, are not found.
- What is heard is compared with the emulator's only in part and by
  hand, and where the hardware's filters are not reproduced it is not
  the console's to the sample; the music is a tenth louder than the
  emulator's ("Sound"). There is no sound card.
- An address of the game's own variables has no top bit on a PC. One
  place tested it and is put right (`snd_FlushQueue`); linking the
  variables above `0x80000000` would end the kind.
- 123 pairs of neighbouring variables are not as far apart as on the
  PlayStation in the 32-bit program, and 248 in the 64-bit one
  ("Memory"). None is known to matter; none has been ruled out one by
  one.
- `src/port/host_windows.c` has been compiled and never linked or run;
  macOS on Apple's processors may not allow the memory the port needs
  ("64 bits", "What is left of it").
- The build compiles more than it must: the headers it writes are
  written again each time, so most files are compiled again (twenty
  seconds).

## The owner's decisions

| Decision | Recommended | Why |
|---|---|---|
| A 32-bit program first, or 64-bit from the start | 32-bit first; done, and 64-bit after it, done ("64 bits") | The source keeps addresses in 32-bit integers at some 300 places, relocates nine kinds of file in place into 4-byte slots and saves 22 addresses to the card: as a 32-bit program all of it works as written. The 64-bit program was then a stage of its own with the port already playing, and the 32-bit one is what it is held to |
| Memory at the PlayStation's addresses, or everything moved | At the PlayStation's addresses, in both sizes | A program linked low can ask for memory at `0x80010000` and `0x1F800000`, whatever the size of its pointers. The twelve fixed addresses, the 24-bit links and the save file's patches then need no change at all. Where a system will not give a program memory below 4 GB (Apple's processors, perhaps), this is the decision to take again |
| A library for the window, the pads and sound | SDL 2 | One library does all three on Windows, Linux and macOS, and its licence sits with this project's. It has to be downloaded; nothing has been |
| The game's data: the disc image, or files taken out of it | The player's disc image | The game asks for sectors, never for names, so an image answers it directly; the music is tracks and streams that only the image has in one piece; and no tool has to unpack game data. Loose files can come later for modding |
| The first renderer: software as the console draws, or the PC's graphics card | Software first | It can be held to the emulator's picture pixel for pixel by the tests there already are, and needs nothing installed. The hardware renderer is then the roadmap's next item, behind the same list of primitives |
| The movies | Decode them in the port, in C | The overlay is a separate PlayStation program started through the BIOS; on a PC a function that plays a movie is simpler than a program loader, and the decoder chip has to be written in C either way |
| The frame's wait | A fixed number of calls, for now: one for each scan line waited ("Time"); to be looked at again at stage 5 | The original calls `rand()` for as long as the frame has time left, so its random numbers depend on the machine's speed. The port needs a rule; none reproduces a console exactly |
