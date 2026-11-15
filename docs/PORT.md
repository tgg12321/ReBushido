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
through the opening movie and a whole fight and back to the menu, with no
pads but a script's. What the game draws goes into the port's own video memory, by
a GPU in C, and from there to files; its sound is made by a sound chip
in C and can be written to a file. All that way it is held to the
emulator: the game's values in three fights, and the picture pixel for
pixel in two, and the sound chip's memory and registers in one. It plays
the opening movie too, decoding it itself. Stages 1 to 3 of the seven
below are done, stage 4 but for its window, of stage 5 the fight itself,
stage 6 but for a sound card, and of stage 7 the movie.

## Where it stands

| | |
|---|---|
| The GTE, the PlayStation's geometry coprocessor, in C | done: `src/port/gte.c`, every operation the hardware has |
| The game's GTE instructions (Sony's macros) as calls into it | done, by `tools/port_gte_macros.py` |
| C beside the assembly functions | 119 of 138: Sony's libgte (48), the game's maths (20) and the renderer (51). The other 19 need none (below) |
| A program that links | done: `make port` builds `build/port/rebushido` from the game's C, with no name missing ("The program") |
| BIOS calls and Sony's libraries | every one the game calls is defined: done for real where it only works on memory or the port has the thing (the disc, time, the pads' packets, events, the movie), a stub that is counted where it has not (the memory card, the link) |
| Memory | done: the RAM and the scratchpad at the console's addresses ("Memory") |
| The disc | done: sectors from the player's image, under the game's own disc code ("The disc"), and its XA sound ("Sound") |
| Sound | done into a file, not yet a sound card: Sony's libsnd and libspu on a sound chip in C, the disc's music ("Sound"). The chip's memory, its registers and the library's are the emulator's through a fight; what is heard is compared only in part |
| A fight | done, by a script's pads: three whole fights are the emulator's, frame for frame ("How it is proved") |
| The picture | done, into the port's own video memory and out to files: `src/port/raster.c`, the PlayStation's GPU in C. The flat screens are the emulator's to the pixel, a fight's frames but for three pixels in a thousand ("The picture") |
| The movie | done: the opening plays, 3,320 frames of 320 by 240 at 29.97 a second, by the overlay's own C over a picture decompressor in C ("The movie"). Its sound is silent, which is what the game's own registers say; the emulator has not been asked yet |
| A window, a sound card, real pads, the card | not begun |

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
`build/port/rebushido`. It is a 32-bit x86 Linux program linked with no
C library, exactly as the host tests are, because a 32-bit C library is
not installed on the machines that build this (nor on GitHub's): it
needs the host's `gcc` with `-m32` and `python3`, and where that `gcc`
cannot make such a program the build says so and fails. The game's files
are compiled with their warnings off, which `make rebushido-modern`
reads; the port's own with that check's warnings as errors, less
`-Wstrict-prototypes`, which Sony's headers themselves fail.
The game's files are also compiled so that every function says when it
begins and ends (`-finstrument-functions`): a run's script can then
stop at a call of any of them, and list the calls of a frame
(`tests/play/README.md`, "Finding where two runs part").

Everything the program asks of the operating system goes through
`include/port_host.h`: a dozen functions (write, open, read at an
offset, memory at an address, catch a fault, exit). `src/port/
host_linux.c` defines them on the kernel's own calls.
`src/port/host_windows.c` defines them on kernel32 and has **not been
built or run**. When the port takes SDL for its window it will need a C
library, and that file is where it changes.

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
| libcd | the port's, all of it, over the disc image (`cd.c`), with the XA sound the drive plays (`xa.c`). Its streaming half, which only the movie uses, is Sony's own but for the two modules that talk to the drive ("The movie") |
| libpress | the port's (`mdec.c`): the picture decompressor and the chip under it ("The movie") |
| libapi | the port's: the counters (`clock.c`), the pads (`pad.c`), events (`event.c`), and nothing for `SetSp`, `SetMem` and the critical section. The card's calls and the files are stubs that answer "no card"; `Exec` runs the movie player (`movie.c`) |
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
at a fixed address on Linux (`VirtualAlloc` on Windows, not tried). The
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
  (`config/symbol_addrs.txt`), kind by kind, and no array padded out
  (`-malign-data=abi`). The build counts the result: 774 of the game's
  993 pairs of neighbours are exactly as far apart as on the
  PlayStation, and the 219 others are in `build/port/layout.txt`: 67
  are strings, which the console starts on a word; the rest are small
  variables the console's small-data sections pad to a word, and places
  where another file's unnamed data lies between two on the console.
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

**For 64-bit**, the mapping itself can stay: a 64-bit process can still
have memory at these two addresses, and a program linked without
position independence has its own variables below 4 GB as well, so an
address still fits the game's 32-bit words. What cannot stay is the
size of a pointer inside a structure: the 41 structures with pointers,
the nine kinds of file relocated in place into 4-byte slots, the
handler tables and the queue the renderer walks, and the two functions
that walk their arguments on the stack. That is the 64-bit stage's
work, and "What makes it a 32-bit program" above is the renderer's part
of the list.

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

### The movie

The game has one movie: the opening, `STR/OPENING.STR`, played once at
boot by `sys_MovieScreen` (`fight_after.c`), the only place in the game
that launches one. The endings and the staff roll are the engine's own
scenes, not streams. On the console the game shuts its sound, card and
pads down, reads the second program `STR/MOVOVL.EXE` over its primitive
buffers and runs it with the BIOS's `Exec`, handing it six words: the
movie's place on the disc, the frame to stop at (3,320), the two pieces
of memory to work in, the title picture's place and the frame from which
that picture is shown (2,817). `docs/formats/MOVOVL.md` has the whole of
it.

**The boundary.** The overlay's own C is compiled into the port and the
port's `Exec` calls its `main`, over the port's libcd, libgpu, libetc and
a picture decompressor of the port's own. So the movie behaves as on the
console by construction: the play loop, the two frame buffers, the switch
to the title picture, the ten seconds' hold, the fade in 64 steps and the
cross button that skips are the overlay's own lines, unchanged. Piece by
piece:

| What | In the port | Why |
|---|---|---|
| `movovl/src/main.c`, `movie.c` (the overlay's own 16 functions) | the overlay's, as they are | plain C over Sony's libraries. Its `main` is compiled under the name `port_MovieOverlayMain`, because the game's `main` is in the same program; nothing else of it clashes with a name of the game's (`tools/port_build.py`, `MOVOVL_FILES`) |
| libcd's streaming, 9 modules (`StSetRing`, `StClearRing`, `StSetStream`, `StSetMask`, `StFreeRing`, `StGetBackloc`, `init_ring_status`, `data_ready_callback`, `CdRead2`) | Sony's, as they are | plain C on the ring buffer |
| `StGetNext` | Sony's, with the port's wrapper around it (`-DStGetNext=port_StGetNextRing`) | it is where the player waits for the drive; on a PC that wait has to let time pass (below) |
| `StCdInterrupt`, `StUnSetRing` (libcd `C_011`, `C_003`) | the port's (`src/port/stream.c`) | they read the drive's registers and move a sector by the DMA controller |
| libpress, the whole module | the port's (`src/port/mdec.c`) | Sony's module holds both the `DecDCT...` calls and the driver that writes the chip's registers and starts its two DMA channels; it cannot be split without editing `movovl/`, so the port supplies the module, and its `DecDCT...` calls do what Sony's do |
| `DecDCTvlc` | the port's (`src/port/mdec.c`) | the original is hand-written assembly (`movovl/asm/libpress_vlc.s`): there is no C to compile |
| libetc's older pad reader (`PadInit`, `PadRead`, `PadStop`) | Sony's, over the port's `PAD_init` and `PAD_dr` (`pad.c`) | plain C over two BIOS calls, which the port supplies: both pads' buttons in one word, a bit clear for each held |

Nothing of `movovl/` is changed: the overlay still builds byte for byte
(`wsl bash tools/build.sh movovl`). The two names the port needs under
another spelling are changed with `-D` on the compiler's command line.

**What the game does is untouched.** `sys_Exec` still shuts everything
down and still reads the 122,880 bytes of `MOVOVL.EXE` off the disc into
`0x801D8800`, as the console has them while the movie runs; only the jump
into them is a call of the C instead.

**The stream.** `OPENING.STR` is 19,511 sectors of 2,048 bytes: 14,541 of
picture, 2,077 of sound and the rest the tail the file is padded out
with. A picture sector is a 32-byte header (what it says it is, the
sector's place in its frame and how many the frame has, the frame's
number and size, the picture's size, and the frame's own four halfwords:
the length of the decoded data, `0x3800`, the quantisation scale and the
version) and 2,016 bytes of bitstream. The frames are 1 to 3,320, 320 by
240 pixels, **version 2**, four to six sectors each (5.0054 on average),
and their quantisation scale runs from 1 to 25 from frame to frame. At
the drive's 150 sectors a second that is **29.97 frames a second** and
110.8 seconds of picture; the sound is one stereo XA stream, four bits a
sample at 37,800 a second, 18.75 sectors a second, and it runs 130
seconds, to the end of the file.

In the port the drive turns by the port's own clock (`src/port/
stream.c`): a sector passes under the head every 20,979/200 scan lines,
which is 150 a second of the clock the sound is counted in, and each is
read from the image and handed either to the drive's sound decoder
(`xa.c`, as on the console) or to the streaming library's ring. The
handler that fills the ring is Sony's own, written again without the
drive's registers and the DMA: the same slots, the same wrap, the same
giving back of a frame whose sectors came out of order, and the same call
of `data_ready_callback` when a frame's last sector is in. A read is a
stream, and not a file's sectors, when the streaming library has hung its
callback on the drive (`CdDataCallback`); `CdInit` takes it off again, as
Sony's does, which is what ends a stream the player left running when the
cross button skipped it.

**The wait.** The player asks `StGetNext` over and over until a whole
frame is in the ring, with no `VSync` in that loop: on the console the
drive's interrupt fills the ring meanwhile. The port has no interrupts,
so each try that finds nothing moves the clock on one scan line
(`port_WaitOneLine`, `clock.c`) and turns the drive. Time therefore
passes in that wait as it does on the console -- the sound is mixed, the
pads are read, the blanks go by -- and the loop settles at the drive's
own rate: a frame every two blanks, which is the movie's 29.97 a second.
A run of the whole movie drops no sector.

**The chip** (`src/port/mdec.c`), written from what is published of the
MDEC and from the format, with no emulator's code:

- `DecDCTvlc` expands a frame's bitstream into run-level halfwords. The
  bitstream is 16-bit little-endian words read from the top bit down:
  ten bits of the lowest coefficient of each block, then variable-length
  codes, each a run of zeroes and the next coefficient's size, until the
  code that ends the block, with an escape code for six bits of run and
  ten of value. The code table is MPEG-1's for the coefficients of a
  block, 224 codes; the port's copy of it is held on every push to the
  table the original library itself looks codes up in, which the
  decompilation of the overlay has (`tests/port/mdec.py` reads
  `movovl/asm/data/libpress_vlc.data.s`). Version 3, which codes the
  lowest coefficient as a difference, is not written: the game's one
  movie is version 2, and a frame that says otherwise stops the movie
  and says so;
- the chip makes an 8 by 8 block of each run of pairs, laid out along the
  zigzag, the lowest coefficient times the first entry of the
  quantisation table and the rest times their entry and the frame's
  scale, each held to ten bits with a sign. The quantisation table and
  the cosine table are the ones Sony's library loads the chip with, and
  the test holds the port's copies to those words;
- the inverse cosine transform is two passes of the rows times the cosine
  table, each leaving the answer turned about its diagonal, with the
  table brought down by eight and each sum by 8,192 with half of that
  added: that is the arithmetic the chip is described as having. A row of
  nothing is skipped, which is where most of a frame's work is saved;
- six blocks make a macroblock of 16 by 16 pixels: Cr, Cb and the four of
  brightness (top left, top right, bottom left, bottom right), the two
  colours read at half the width and height. Red, green and blue are the
  usual 1.402, 0.3437, 0.7143 and 1.772, in 16,384ths;
- the output is 24-bit or 15-bit as the command word says, with the top
  bit of a 15-bit pixel set or not as it says: a macroblock is 16 rows of
  16 pixels, one macroblock after another. That is what the player loads
  into video memory in 20 strips of 16 pixels (`LoadImage`);
- a transfer out is done as it is asked for and the callback that ends
  one on the console runs at once, as the port does the sound chip's
  (`sound.c`). A callback that asks for the next slice is answered by a
  loop, not by calling into the chip again.

**Not sure about the chip**, and marked in the file:

- which way the transform rounds a negative sum (an arithmetic shift here)
  and a negative entry of the cosine table divided by eight (towards
  zero here);
- the fixed-point constants of the step to red, green and blue, and
  which way they round. What this is expected to cost against the
  emulator is written in `tests/play/port_movie.toml`: a pixel one step
  away in a channel here and there, and nothing structured;
- a frame that asks the chip for more than its run-level data holds is
  filled with black and counted.

**The movie has no sound, and that is what the game's own registers
say.** `sys_Exec` calls `snd_Quit` before the player starts; `SsInit`'s
`SpuInit` has set the sound chip's CD volume to 0 (`_spu_init`,
`src/main/psxsdk/libspu/spu.c`) and nothing turns the chip's CD input on
again before the main menu (`snd_EnableCdAudio`, `cdrom.c`), which the
movie is long over by. The player sets the *drive's* volume to its
maximum (`CdMix`) and mutes it again when the movie ends, but the drive's
sound goes into the sound chip, and there it is multiplied by 0. The port
reproduces every one of those writes, so its movie plays in silence. The
play test `port_movie` asks the emulator what its chip has there at three
moments of the movie; until that is run this is the one thing about the
movie that is not settled.

**What a run says.** A run that played a movie ends with four lines: the
sectors the drive gave the stream and how they were split, the frames the
player took out of the ring with the first and last of them and the
picture's size, the blanks the stream ran between, and what the chip made
of it. With the retail image, played to its end:

```
movie sectors 16618 picture 14541 sound 2077 dropped 0
movie frames 3320 first 1 last 3320 size 320x240
movie blanks 408 to 7035
movie decoded 3319 frames 995700 macroblocks, 0 short
```

6,627 blanks for 3,320 frames is 1.996 blanks a frame, which is the
disc's own 29.97 a second; 995,700 macroblocks is 300 for each of the
3,319 frames that were decoded (the last is read and not decoded: the
player stops at the end frame). The whole screen takes 7,334 blanks: 110.8
seconds of movie, ten of the title picture held and 64 frames of fade.

**How it is proved.** By made-up data on every push
(`tests/port/mdec.c`, with `mdec.py` working the answers out a second
time): the bit reader; the chip's two tables against the words Sony's
library loads it with; every one of the 224 codes read back out of a
bitstream, and the whole table against the original library's own lookup
tables; a frame's bitstream expanded, with an escape code in every block
and the padding at the end; a block of one coefficient, whose 64 values
are worked out by hand; five blocks of several coefficients at five
quantisation scales, against the chip's arithmetic written out again in
Python and against a real inverse cosine transform in floating point
(within 2); the step to red, green and blue by hand; a whole macroblock
through the chip in 24-bit and in 15-bit colour, with and without the
mask bit; and the ring, with made-up sectors in order, a sector of sound
among them, a frame whose middle sector never comes, the ring's wrap, and
the drive's rate at both speeds.

With the retail image, on the port alone: the movie plays to its end,
3,320 frames of 320 by 240, no sector dropped, the picture written out as
PNGs. The frames were looked at (the first, a dark one, two close-ups,
the title drawing itself, the title picture, the fade): they are the
film, with the softness and the ringing the MDEC gives at these
quantisation scales and nothing else -- no block out of place, no seam
between two of the 20 strips a frame is loaded in, no colour cast. The
cross button skips it at any moment and the game goes on. The three play
tests' runs are what they were: every one of the 54 watched values at
every checkpoint is the same as before the movie existed, in all three,
and only the blank each checkpoint falls at has moved (below).

**What the blanks do.** The three port plans press cross at `movie+600`,
600 blanks after the game reaches the movie screen, so the movie now
costs the port 601 blanks where it cost it none. Measured, by running
each plan with the movie and without it: `page` and `movie` are at the
same blank as before, and every checkpoint from `menu` on has moved by
exactly +601, with every watched value the same. `port_game` ends at
blank 4,481 where it ended at 3,880; `port_other_fight` at 4,721;
`port_bugfixes` at 4,511. All three are far inside their limit of 12,000,
and nothing in them is counted in blanks across the movie, so no plan
needs changing. The emulator's side does not move at all: it always
played the movie. What this does is bring the two closer: the port is now
at the top menu at blank 1,098 where it was at 497 and the emulator is at
1,902.

**What is left**: the sound (above); a frame of the movie held to the
emulator's pixel for pixel, which needs the two lined up by the stream's
own frame number (`tests/play/port_movie.toml` says how); and version 3
frames, which this game has none of.

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
| The movie overlay | its entry, one BIOS jump, the bitstream decoder (279 instructions and its tables) and the DMA routine (120) | `movovl/asm/` | The overlay's own C is compiled into the port; the bitstream decoder is written again in C and the DMA routine's work is the port's ("The movie") | done |

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
| Running the movie | `Exec`, `SetSp`, `SetMem` | A(43), A(9F) | A call of the movie player's own `main`, which is compiled into the port ("The movie"); the other two are nothing |
| The movie player's pads | `PAD_init`, `PAD_dr` | B(15), B(16) | Both pads' buttons in one word, a bit clear for each one held ("The movie") |
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
  - *The movie's work area* (the 2 KB before the load area) differed:
    only the emulator played a movie. Both do now ("The movie"); it is
    still not in the test, because the two stop the movie at different
    frames of it.
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
- **The movie by made-up bits, blocks and sectors**, on every push:
  `tests/port/mdec.c` and `mdec.py`, whose code table is held to the
  original library's own ("The movie"). The movie itself was played to
  its end on the port and its frames looked at; the emulator has not seen
  it (`tests/play/port_movie.toml`, not yet run).
- **`make port` on every push**: the program links with no name
  missing and the port's files have no warning.

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
   nothing in its place). The movie is done ("The movie"): the opening
   plays, by the overlay's own C over a picture decompressor in C.

The wide picture and the high resolution are the roadmap's next item and
build on stage 4.

## What is left

How far the program gets today: with `port_game`'s presses, through the
opening movie, a versus fight to its kill, the replay, the menu after it
and back to the main menu, as the emulator does. Nothing stops it; what
it cannot do is listed here.

**The stubs the game called on the way** to the fight's first second,
with how often, are the list of what stage 7 writes (a whole fight
and the way back to the menu call the same ones, more often, and no
other):

| Stage | Stub | Calls |
|---|---|---|
| 7, the card | `_card_info` | 24 |
| | `InitCARD`, `StartCARD`, `_bu_init` | 2 each |
| | `StopCARD` | 1 |

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
- Address 0: two places are given somewhere else to read and write; the
  others, if there are others, are not found.
- What is heard is compared with the emulator's only in part and by
  hand, and where the hardware's filters are not reproduced it is not
  the console's to the sample; the music is a tenth louder than the
  emulator's ("Sound"). There is no sound card.
- An address of the game's own variables has no top bit on a PC. One
  place tested it and is put right (`snd_FlushQueue`); linking the
  variables above `0x80000000` would end the kind.
- 219 pairs of neighbouring variables are not as far apart as on the
  PlayStation. None is known to matter; none has been ruled out one by
  one.
- `src/port/host_windows.c` has never been compiled.
- The build compiles more than it must: the headers it writes are
  written again each time, so most files are compiled again (twenty
  seconds).

## The owner's decisions

| Decision | Recommended | Why |
|---|---|---|
| A 32-bit program first, or 64-bit from the start | 32-bit first | The source keeps addresses in 32-bit integers at some 300 places, relocates nine kinds of file in place into 4-byte slots and saves 22 addresses to the card: as a 32-bit program all of it works as written, and the tests already run that way. 64-bit is then a stage of its own with the port already playing (and is needed in the end: macOS runs no 32-bit programs) |
| Memory at the PlayStation's addresses, or everything moved | At the PlayStation's addresses, while the program is 32-bit | A 32-bit program can ask for memory at `0x80010000` and `0x1F800000` (the tests do, for the scratchpad). The twelve fixed addresses, the 24-bit links and the save file's patches then need no change at all |
| A library for the window, the pads and sound | SDL 2 | One library does all three on Windows, Linux and macOS, and its licence sits with this project's. It has to be downloaded; nothing has been |
| The game's data: the disc image, or files taken out of it | The player's disc image | The game asks for sectors, never for names, so an image answers it directly; the music is tracks and streams that only the image has in one piece; and no tool has to unpack game data. Loose files can come later for modding |
| The first renderer: software as the console draws, or the PC's graphics card | Software first | It can be held to the emulator's picture pixel for pixel by the tests there already are, and needs nothing installed. The hardware renderer is then the roadmap's next item, behind the same list of primitives |
| The movies | Decode them in the port, in C | Settled, and done ("The movie"): the overlay's own C is compiled into the port and its `main` is called where the console would run the program it has just read; under it are the picture decompressor and the drive's streaming, in C |
| The frame's wait | A fixed number of calls, for now: one for each scan line waited ("Time"); to be looked at again at stage 5 | The original calls `rand()` for as long as the frame has time left, so its random numbers depend on the machine's speed. The port needs a rule; none reproduces a console exactly |
