# The native port

A build of this same source for a PC, beside the PlayStation one: item 6
of the [roadmap](ROADMAP.md). The game's code stays C and stays one
source. The PlayStation builds keep their assembly and are not changed by
any of this: `decomp` is not touched, the matching build still makes the
original's bytes, and nothing of the port is compiled or linked into the
ReBushido PlayStation program.

This page is what stands between the source and a PC, counted; what has
been done about it; the stages that are left; and the decisions that are
the owner's. **The port is a program that draws and sounds, and a
second build of it has a window, a sound card and the PC's keys and
controllers:** it links, gives the game the PlayStation's memory, reads
the player's disc image, and runs the game's own `main` through
start-up, the options page, the opening movie, the menus and character
select, through a whole fight and back to the menu, by a script's pads or a person's.
What the game draws goes into the port's own video memory, by a GPU in
C, and from there to files or to the window; its sound is made by a
sound chip in C and goes to a file or to the sound card. All that way
it is held to the emulator: the game's values in three fights, and the
picture pixel for pixel in two, and the sound chip's memory and
registers in one. The window is held to the build without one, by
numbers, on Linux and on Windows ("The window"). It is a 64-bit
program, and a 32-bit one beside it that does the same to the byte
("64 bits"). Stages 1 to 3 of the seven below are done, stages 4 and 5
as far as a machine can tell, stage 6 but for someone to listen, and
of stage 7 the memory card and the movie. Beyond the seven, the
roadmap's item 7: the scene drawn again by a graphics card at the
window's resolution, from the geometry the PlayStation threw away,
which is what "Graphics: Remastered" means ("Remastered"); the window
takes an OpenGL 3.3 context for it and F2 changes picture as the game
runs ("The window", "Remastered in the window").

## Where it stands

| | |
|---|---|
| The GTE, the PlayStation's geometry coprocessor, in C | done: `src/port/gte.c`, every operation the hardware has |
| The game's GTE instructions (Sony's macros) as calls into it | done, by `tools/port_gte_macros.py` |
| C beside the assembly functions | 119 of 138: Sony's libgte (48), the game's maths (20) and the renderer (51). The other 19 need none (below) |
| A program that links | done: `make port` builds `build/port/rebushido` from the game's C, with no name missing ("The program") |
| BIOS calls and Sony's libraries | every one the game calls is defined: done for real where it only works on memory or the port has the thing (the disc, time, the pads' packets, events, the memory card, the movie), a stub that is counted where it has not (the link) |
| Memory | done: the RAM and the scratchpad at the console's addresses ("Memory") |
| The disc | done: sectors from the player's image, under the game's own disc code ("The disc"), and its XA sound ("Sound") |
| Sound | done, into a file and, in the build with a window, to a sound card: Sony's libsnd and libspu on a sound chip in C, the disc's music ("Sound"). The chip's memory, its registers and the library's are the emulator's through a fight; what is heard is compared only in part |
| A fight | done, by a script's pads: three whole fights are the emulator's, frame for frame ("How it is proved") |
| The picture | done, into the port's own video memory and out to files: `src/port/raster.c`, the PlayStation's GPU in C. The flat screens are the emulator's to the pixel, a fight's frames but for three pixels in a thousand ("The picture") |
| 64 bits | done: the program has 64-bit pointers, the game's addresses stay 32-bit numbers below 4 GB, and it is held to the 32-bit program and to the emulator ("64 bits") |
| A window, a sound card, the PC's keys and controllers | built on SDL 2 for Linux and for Windows (`make port-sdl`, `make port-sdl-windows`) and held to the build without a window by numbers: the same traces, pictures and samples ("The window") |
| "Graphics: Remastered" | done in the window: the scene drawn again by a graphics card at the window's resolution, from the geometry the PlayStation threw away — OpenGL 3.3 core, high resolution, sub-pixel corners, textures in perspective, true-colour mixing, the 2D layer crisp; supersampled edges, the whole stage drawn, and a wider picture at 16:9. Proved with nothing shown against the software GPU's own picture, and hung on the window, where F2 changes picture as the game runs ("Remastered", "Remastered in the window") |
| The memory card | done: a standard card image of 128 KB a slot, under the BIOS calls the game makes ("The memory card"). The card the port writes for the options page is byte for byte the one the emulator's BIOS wrote |
| The movie | done: the opening plays, 3,320 frames of 320 by 240 at 29.97 a second, by the overlay's own C over a picture decompressor in C ("The movie"). Its sound is the drive's own, as the console has it |

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
`src/port/host_windows.c` defines them on kernel32: built with MinGW
for either size and run on Windows, with a window and without ("The
window"). The build with a window has a C library, because SDL has one;
on Linux its host is `src/port/window/host_posix.c`.

The program's options: `--image` (the `.cue` or the `.bin`), `--frames
N` (end after N vertical blanks; 3,600 if nothing says), `--trace` (the
values of `tests/play/state.toml` at every blank where one changed),
`--plan FILE` (a play test's plan: presses, checkpoints, a limit),
`--dump NAME=ADDRESS:BYTES` and `--out FOLDER` (memory to a file at the
end), `--shots FOLDER`, `--every N` and `--vram FILE` (the picture as
PNGs, and video memory: "The picture"), `--wav FILE` (the run's sound,
with a line for each voice started and each sound the game asked for)
`--spu FILE` (sound memory and the chip's registers at the end:
"Sound"), and `--card1 FILE`, `--card2 FILE` and `--no-cards` (the memory
cards: "The memory card"). It ends by printing a line for each stub the
game called, with how often. Pads are pressed only by a plan: a line `in 0 300 1 START 6`
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
| libapi | the port's: the counters (`clock.c`), the pads (`pad.c`), events (`event.c`), the memory card and the BIOS's files (`card.c`), and nothing for `SetSp`, `SetMem` and the critical section. `Exec` runs the movie player (`movie.c`) |
| libsnd, libspu | Sony's, all of both but the one module of libspu that talks to the chip, which is the port's on its sound chip in C ("Sound") |
| libcomb | the port's, all stubs: nothing at the cable's other end |
| libcard | Sony's `card.c` (`_card_clear`, which is two BIOS calls the port supplies); the rest is BIOS jumps and the kernel patch, which the port's card code stands for |
| libsn | not needed: the development host's files, which nothing reaches |

A stub counts its calls and gives the answer the library gives when all
is well, where the game would otherwise stop or wait for ever: no card
is in the slot.

### Memory

The game's own variables are the PC program's, placed by its linker.
What the game reaches by a number written into its source is given
memory at that number (`src/port/memory.c`): the 2 MB of RAM at
`0x80000000` and the kilobyte of scratchpad at `0x1F800000`, by `mmap`
at a fixed address on Linux (`VirtualAlloc` on Windows), in the 64-bit
program as in the 32-bit one. The
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
  result: in the 32-bit program 871 of the game's 991 pairs of
  neighbours are exactly as far apart as on the PlayStation, and the
  120 others are in `build/port32/layout.txt`: nearly all are closer
  together than on the console, where another file's unnamed data lies
  between the two or a small variable is padded to a word. In the
  64-bit program 746 are; of its 245 others, 137 follow a variable
  that has pointers in it, which are twice as wide there, and the rest
  are the 32-bit program's (`build/port/layout.txt`).
- **Address 0.** The console has its kernel's memory there, so the
  original reads and writes through pointers it never set, and nothing
  happens. A PC stops the program. Linux will not map address 0 for a
  program, so each place is given somewhere else, or left alone, under
  `RB_PORT`, with a note where it is:

  | Where | What it reaches through a null | What the port does |
  | --- | --- | --- |
  | `fighter_TakeHit` | the word it clears first | writes the word the "Bugfixes" setting clears, whatever the setting; nothing the game does changes |
  | `eff_Start` | the direction an effect was never given | reads `g_port_low_memory`, sixteen words of zeroes, which is what the console has there |
  | `snd_PlayAt`, from `story_HenchmanShout` | the camera a shout is placed by, before a story fight has chosen one | the same zeroes, so the shout is placed as the console places it (`fight_event.c`) |
  | `motion_OpenStreams` | in chanbara the second fighter's motion file, whose room holds the pack of opponent models instead | leaves that decoder alone: both fighters fight with the one motion set, so nothing reads it (`fighter.c`) |
  | `fight_after.c`, after a kaishaku | the same, and the first slot too | opens the streams of the slots that hold a file |
  | `select_DrawTeamHeading` | the heading after the last, as a side takes its last weapon in the team battle | draws the heading of the step it is drawing, which is the last (`select_team.c`) |

  The first two were found on the way to a fight's end, the others by
  the sweep of the menu's rows ("How it is proved"). The next one will
  stop the port and say where.

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

The port makes the game's sound: it mixes into memory, and `--wav FILE`
writes a run's sound as a WAV file. The build with a window plays the
same samples as they are made ("The window").

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
never waits. The picture is that memory: a run writes it to files, and
the build with a window shows it ("The window").

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

## The memory card

The port has the PlayStation's memory card: a **standard raw card image**
of 128 KB, one file a slot, under the BIOS calls the game makes
(`src/port/card.c`, `include/port_card.h`). So a save the port writes is
an ordinary PlayStation memory card file, which an emulator or a card tool
opens, and a card file an emulator left works here. There is nothing of
the port's own in it.

**The boundary is the BIOS's**, because that is where the game's card code
is (`memcard.c`, `memcard_save.c`) and ReBushido's settings code with it
(`src/rebushido/save.c`): the game starts something on the card and waits
for the events the driver reports through.

| | |
|---|---|
| `InitCARD`, `StartCARD`, `StopCARD`, `_bu_init` | the driver up and down. Every card is one it has not been told about, as after power on; nothing is open |
| `_card_info` | is a card in the slot, and is it one the driver has not been told about? |
| `_card_load` | read the card's directory |
| `_new_card`, `_card_write` | Sony's `_card_clear` is these two (`libcard/card.c`, which the port compiles): the driver stops calling the card new, and a frame is written to prove the card takes one |
| `open`, `close`, `read`, `write` | the card's files, by the names `bu00:...` (slot 1) and `bu10:...` (slot 2). A name that is not a card's is the link cable's port, as before |
| `firstfile`, `nextfile` | the directory, with `*` and `?` in the pattern |
| `delete`, `format` | the game calls neither; the boundary is the BIOS's and the host test and `tools/card.py` use them |

**The images.** `--card1 FILE` and `--card2 FILE` name them; `--no-cards`
empties both slots. Where an option does not say, a slot's image is
`card1.mcd` or `card2.mcd` **beside the program**, which is where the
program's own folder is writable in every way the port is run today (the
build's folder, and a release's folder for a player). A per-user data
folder is the window's stage to choose, with the rest of a player's
settings. A file that is not there is made, formatted and empty, the first
time the game looks at that slot; a file that is not 128 KB, or that
cannot be opened or made, leaves the slot empty and the port says so once
on its error output, which is a state the game is written for.

**The format** is the card's own, which is public knowledge and is written
out from the rules in `src/port/card.c` and again in `tools/card.py` and
in the host test, with no emulator's code read. A card is a header block
and fifteen blocks of 8192 bytes; a block is 64 frames of 128 bytes, which
is what the card reads and writes at a time. The header block is the
identifier (`MC`), a directory frame for each of the fifteen blocks, the
list of twenty broken frames and the twenty frames that replace them, and
the identifier again in the last frame; every frame's last byte is the
exclusive or of its other 127. A directory frame holds the block's state
(a file's first block, a middle one, the last; free, keeping the low half
of what it was, as the BIOS's delete leaves it; unusable), the whole
file's size in the frame of its first block, the next block of the file,
and the file's name of at most twenty characters.

**The events, and what the game waits for.** A software event (`SwCARD`)
is what a whole operation reports, and the game polls for it once a frame
(`memcard_PollSwEvent`, `rb_PollLookEvent`), giving up after 120 polls. A
hardware event (`HwCARD`) is the card itself answering, and the game waits
for one in loops with **no vertical blank in them** (`memcard_WaitHwEvent`,
and `_card_clear` inside `memcard_ProbeCard`), where the port's clock does
not move at all: those are reported the moment the call returns, as they
must be. The whole table is in `tests/port/card.c`, with the source of
each row; in short:

| Call | Software event | Hardware event |
|---|---|---|
| `_card_info`, a card the driver has not been told about | `EvSpNEW` after 1 blank | |
| `_card_info`, a card it has | `EvSpIOE` after 1 blank | |
| `_card_info`, an empty slot | `EvSpTIMOUT` after 1 blank | |
| `_card_load`, a formatted card | `EvSpIOE` after 16 blanks | |
| `_card_load`, a card that is not formatted | `EvSpNEW` after 16 blanks | |
| `_card_load`, an empty slot or an image that cannot be read | `EvSpTIMOUT` after 16 blanks | |
| `_new_card` | | |
| `_card_write` | `EvSpIOE` at once | `EvSpIOE` at once; `EvSpTIMOUT` with no card |
| `read`, `write` of N bytes | `EvSpIOE` after (N + 127) / 128 blanks | `EvSpIOE` at once; `EvSpERROR` if the system refuses it |
| `open`, `close`, `firstfile`, `nextfile`, `delete`, `format` | | |

`EvSpNEW` from `_card_load` is how the game reads "the card is not
formatted" (`memcard.c`, `PROBE_WAIT_LOAD`; `save.c`, `LOOK_WAIT_LOAD`),
so that is what the port gives for a card whose identifier or whose
directory frames do not hold. A read or a write the host refuses is the
card's error, which the game turns into "the load failed" or "the save
failed"; the port never gives `EvSpERROR` for a card that is simply
missing, because the game's own handling of a card error at a save screen
is to wait for ever (`memcard_RunRequest` has no arm for it: one of the
original's bugs, `STATUS.md`).

**The timing**, and what it is assumed from: **a frame of 128 bytes a
vertical blank**. The console's BIOS driver moves one frame at each of its
turns, which the vertical blank's interrupt gives it, and a whole block of
64 frames then takes 64 blanks, which is the second a console is known to
take to write one block. So `_card_info` is one command and takes 1 blank;
`_card_load` is the identifier's frame and the fifteen directory frames,
16 blanks; a read or a write of the 512 bytes either save file is, 4
blanks. Nothing waits on the host's clock: a run is the same every time,
as the rest of the port is ("Time"). A card operation that is done by now
reports itself at the port's next vertical blank, beside the pads
(`clock.c`, `port_RunCardBlank`).

**What is not the console's**, and is marked in the file:

- Making a file takes no blanks: the console's BIOS writes a directory
  frame there, which would be one.
- `format` is done when it returns; the console takes about a second over
  the header block's 64 frames. It writes the header block alone and
  leaves the blocks' own bytes, as the BIOS does.
- `_card_clear` writes the last frame of the header block from address 0,
  which the port has no memory for; the port writes nothing there. On a
  formatted card that frame holds the identifier again, and the emulator's
  own cards show it untouched after the game has run.
- A card cannot be taken out or put in while the program runs: it is
  there, or the slot is empty, from the start. The game's "the card was
  pulled out" paths are therefore never taken.
- There is no multitap: only card 0 of each of the two ports answers.

**The patch table.** The game's save holds a table of 22 addresses of the
PlayStation's memory and halfwords that `memcard_UnpackSaveData` writes
into them as it loads ("Everything else", and `STATUS.md`: a development
leftover the game always writes empty). On a PC a save file from anywhere
would be a way to write anything anywhere. The port's `read` of the game's
own save (`BASLUS-00663BUSHIDO2`) therefore hands the game **an empty
table** and says once on its error output that it did; the record itself
is loaded as it is, and a record marked "patches only" then applies
nothing. The game's own lines are not touched, on either build: the
PlayStation program still does what it always did, and `tests/save_record.c`
still holds it to that.

**What is proved.**

- **On every push, by made-up data alone** (`tests/port/card.c` and its
  `.py`, 550 checks): the card as a format leaves it, frame by frame, with
  every frame's exclusive or worked out a second time in Python from the
  rules and a third by `tools/card.py`; the events of each call and the
  blanks each takes, against the table above; a file made, written, read
  back and deleted, and its blocks had again; a file of three blocks, its
  directory frames linked first, middle and last and its bytes written and
  read across them; the directory with `*` and `?` in the pattern; a full
  card (fifteen files of a block, and a file of all fifteen), and the file
  too many; a card that is not formatted, a damaged directory frame, a
  damaged identifier and an image that is not 128 KB; two slots, each with
  its own card; `--no-cards`; a name of twenty characters, which has no
  zero after it in the directory; and the patch table refused, with the
  rest of the save coming back whole and another file's bytes untouched.
  No image of a real card is read: the test's host files are memory.
- **The emulator's own cards, without running the emulator.** The card
  the port writes for the options page's round trip, with the retail image
  and the port alone, is **byte for byte** the card PCSX-Redux's BIOS code
  wrote in the play test `options_page` (`build/playtest/options_page/
  rebushido/card1.mcd`), and a card the port makes new is byte for byte
  the empty formatted one the emulator makes. The other way too: given a
  copy of the emulator's card, the port reads ReBushido's settings off it
  (`rb_save` is `RB_SAVE_LOADED` and the setting is the one the emulator
  wrote).
- **The round trips, on the port with the retail image.** The options
  page: one boot changes a setting, leaves the page and the file is
  written; a second boot with the same card comes up with the setting
  read back. The game's own save: from the top menu's Options, Memory
  Card, Save, the game writes `BASLUS-00663BUSHIDO2`, one block; a second
  boot finds it at the top menu, asks whether to load it and loads it,
  with the unlocks it saved. A save with a patch in its table loads, and
  the address it names is not written.
- **The three plans that run with no card** (`port_game`,
  `port_other_fight`, `port_bugfixes`) give the same checkpoint values at
  the same blanks as before the card was written, to the line.
- **Against the emulator**: the play test `port_card`, four boots with one
  card passed from each to the next, the first two in the emulator and the
  last two on the port. It passes: every value it compares is the same on
  all four runs, and afterwards the four card images are **one file, byte
  for byte** -- a card of 131,072 bytes with `BASLUS-00663REBUSHI` of one
  block and fourteen blocks free. So the settings' file was written by the
  console's own BIOS code through the emulator and read back twice by the
  port, which wrote nothing.

**The window build** needs nothing of its own: the card is the same
boundary over the same three calls of `port_host.h` (a file opened for
reading and writing, written at an offset, and pushed out to the disc),
which `src/port/host_windows.c` has on kernel32 (`CreateFileA`,
`SetFilePointerEx`, `WriteFile`, `FlushFileBuffers`) and
`src/port/window/host_posix.c` on a C library, for the build with a
window. All three are built and run.

**What is left**: a card taken out or put in while the program runs (which
needs the window's own way of asking for one), and a per-user folder for
the images.

## The movie

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

**The movie's sound**, and how the port came to be silent and is not.
The game's own code says nothing turns the sound chip's CD input on
before the main menu: `sys_Exec` calls `snd_Quit` before the player
starts, `SsInit`'s `SpuInit` has set the chip's CD volume to 0
(`_spu_init`, `src/main/psxsdk/libspu/spu.c`), and
`snd_EnableCdAudio` (`cdrom.c`) is first called at the menu, which the
movie is long over by. So the port's movie had a picture and no sound.

**The emulator says otherwise, and it is right.** Read at three moments
of the movie (`port_movie`, and a run of its own), the emulator's chip
has its CD volume at `0x3FFF` on each side, its main volume at `0x3FFF`
and its control register at `0xC001` -- the disc's input taken -- from
the moment the player starts until the movie ends, when they go back to
`0` and `0xC000`. What sets them is **Sony's libcd**: `CD_initvol`
(`src/main/psxsdk/libcd/bios.c`), which `CdInit` reaches through
`CdReset(1)`, writes exactly those three registers, and `sys_Exec` calls
`CdInit` as it hands the machine over. The port's libcd is its own
(`cd.c`) and did not do that part of Sony's, so the port's chip
multiplied the disc's sound by 0. It does it now
(`port_InitCdSpuVolume`, `sound.c`, called from the port's `CdInit`),
and:

- the port's movie has sound: 127.5 seconds of it in a run's WAV, peak
  20,132 and 25,336, RMS 4,182 and 4,011 (`tools/port_wav.py`);
- at all three checkpoints of `port_movie` the port's sound memory and
  every register a program writes are the emulator's.

The player sets the *drive's* volume to its maximum (`CdMix`) and mutes
it again when the movie ends; the port's drive did that already. One
thing of the chip's is still not the port's: its control register's CD
bit is written and not read -- the port's chip mixes the disc in by the
CD volume alone, which Sony's library sets and clears together with the
bit, so nothing turns on it.

**What a run says.** A run that played a movie ends with four lines: the
sectors the drive gave the stream and how they were split, the frames the
player took out of the ring with the first and last of them and the
picture's size, the blanks the stream ran between, and what the chip made
of it. With the retail image, played to its end:

```
movie sectors 16618 picture 14541 sound 2077 dropped 0
movie frames 3320 first 1 last 3320 size 320x240
movie blanks 281 to 6908
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

**A frame of the movie against the emulator's, pixel for pixel.** The two
programs are at different frames of the movie at the same vertical blank
(measured: the emulator is at a given frame 570 blanks after the port,
which is the blank it reaches the movie screen at, 835 against 276, and
the time it spends reading the player and the title picture, which the
port spends none on), so the two were lined up by the pictures
themselves: twelve of the port's, 81 of the emulator's two blanks apart
over the same stretch, and every pair compared. One pair fits and its
neighbours are plainly worse, which is what says the two are the same
frame of the movie. On that pair, of the 76,800 pixels:

| | |
|---|---|
| Pixels with every channel within 1 | 66,144 (86.1%) |
| ... within 2 | 76,071 (99.05%) |
| Pixels more than 2 apart in a channel | 729 (0.95%), the largest difference 7 of 255 |
| The mean signed difference | red -0.18, green +0.49, blue -0.18: no cast |
| Where the far pixels are against the 16-pixel macroblock grid | spread evenly, 32 to 65 in each of the sixteen columns and 30 to 62 in each of the sixteen rows: no seam, no block out of place |
| The runs of far pixels along a row | 316 single, 104 of two, nine of six, two of eight: detail edges |

So the frame is the emulator's but for rounding in the inverse cosine
transform and in the step to red, green and blue, which is where
`mdec.c` says it is not sure of the hardware. It is a little looser than
was guessed before it was run (no channel more than 2 apart and nine in
ten within 1): the largest is 7 and 86 in a hundred are within 1.

**What is left**: version 3 frames, which this game has none of; and the
sound compared with the emulator's sample for sample, which the chip's
own comparison stands in for ("Sound").

## The window

A second build of the port has a window, a sound card and the PC's keys
and controllers, by SDL 2: `make port-sdl` on Linux,
`make port-sdl-windows` for Windows. It is for a person to play
([`PLAYING.md`](PLAYING.md)). **Nobody has yet looked at the window or
listened to it.** It was built, and run only with SDL told to show and
sound nothing, and what it hands SDL was compared with the build
without a window by numbers ("How the window is proved"). The first
person to see it will be the owner.

The build without a window (`make port`) is not changed by it and stays
the one the checks and the play tests use: it needs nothing installed,
and this one needs SDL and a C library.

**How it hangs on the port.** The port calls out at four places, through
pointers that are empty in the build without a window
(`include/port_platform.h`, `port_spu.h`): at a vertical blank before
the pads' packets are filled, at a blank's end, as a blank's samples are
made, and as the program ends. The window's files are
`src/port/window/`:

| File | |
|---|---|
| `scale.c`, `ring.c`, `pace.c`, `keymap.c`, `logname.c`, `fps.c`, `window.h` | the arithmetic: where the picture goes in the window, the queue of samples, when a blank is due, which key is which button, the settings file, a log's name and lines, the counter's rate, digits and place. No SDL and no C library in them, so the host tests run them on every push (`tests/port/window.c`) |
| `gamestate.c` | reads the game's variables out for the log, and has the names the log gives screens, modes, fighters, weapons and stages. It only reads |
| `sdl_window.c` | the window, the sound card and the controllers, on SDL; the counter; the log's notes of the game |
| `sdl_log.c` | the session's log file |
| `sdl_main.c` | where the program starts: its own options, the settings file, the disc image when none is named |
| `host_posix.c` | `port_host.h` on a C library, for Linux (Windows has `src/port/host_windows.c`) |

One thread does everything but hand samples to the sound card: SDL is
called from the game's own stack, at the blank, so what the game sees of
a key does not depend on how the PC's threads fall.

**How it is built** (`tools/port_sdl_build.py`). The game defines names
a C library has too: `main`, `printf`, `memcpy`, `rand`, and `open`,
`read`, `write` and `close`, which are the BIOS's memory-card files. A
program linked with a C library would give SDL the game's `open`. So
everything of the game and the port is first linked into one object,
and every name in it is made that object's own, except the port's
(`port_...`, `g_port_...`). What the object still asks for is listed,
and the build fails if it is anything but the host's functions. The
game's variables keep the PlayStation's order inside the object, each
given the boundary the PlayStation's compiler gives it, and the build
counts the neighbours as `make port` does: the 64-bit program for Linux
has the same 746 of 991 as `make port`'s own, 137 of the others
following a variable whose pointers are wider; the one for Windows has
666 of 1,030 with 139 such (a Windows program's names carry no sizes, so
the pairs are counted differently). On Windows the boundary is set in the
object itself, as it is on Linux, because a Windows object's sections
would otherwise be padded out to it (`cap_alignment_coff`).

The program has 64-bit pointers, and the 32-bit one is built beside it by
`PORT_BITS=32`, into `build/port-sdl/linux32/` or `windows32/`. What it
needs, and nothing of it is needed by any check:

- **Linux:** `gcc` and `libsdl2-dev`. For the 32-bit program as well, on
  Ubuntu: `sudo dpkg --add-architecture i386 && sudo apt update && sudo
  apt install lib32gcc-13-dev libsdl2-dev:i386`. **Not
  `gcc-multilib`:** it removes the MIPS cross compiler the PlayStation
  builds use.
- **Windows:** MinGW's `x86_64-w64-mingw32-gcc` (`i686-w64-mingw32-gcc`
  for the 32-bit program), in WSL or on Linux (`apt install
  gcc-mingw-w64-x86-64 gcc-mingw-w64-i686`), and SDL 2's development
  package for MinGW, whose two halves are each size's (`BUILD.md`, "The
  port with a window"). The build writes
  `build/port-sdl/windows/rebushido.exe`, puts `SDL2.dll` beside it, and
  also writes `rebushido-headless.exe`, the program without a window for
  Windows. The 64-bit program is compiled `-mcmodel=small`, because
  without it the compiler reaches a variable of another file through a
  shared slot that cannot survive the link into one object.

**The picture.** At each blank the displayed rectangle of the port's
video memory is turned into pixels of eight bits a channel (five bits
stretched as the port's PNGs stretch them) and given to SDL as a
texture, which SDL scales into the window. Nothing is drawn another way:
it is what the GPU in C drew. The game sets three sizes: 640 by 240 for
nearly everything, 320 by 240 for one screen, and the chip's own 256 by
240 before it has set any. All fill a screen of four by three, so the
picture is placed as the largest four by three the window holds
(`scale = fit`), or as 320 by 240 a whole number of times (`integer`),
or over all of the window (`stretch`); each pixel a block (`filter =
nearest`) or smoothed (`linear`). The window can be resized; F11 or
Alt+Enter fills the screen and comes back. With the display blanked the
window is black.

**Time.** The game's clock is still the port's count of scan lines, and
nothing the game computes waits for anything: a blank's pads are read,
its sound mixed and its picture drawn, and only then does the window
wait until the blank is due. The game's time is the samples it has
mixed, 737 and 341/2500 a blank (the sound's own constants), so a blank
is due every 16.715 ms: the console's 59.83 a second. The PC's clock is
read in the same unit, and most of a wait is slept and its last three
milliseconds watched out. If the program was held up for more than a
tenth of a second (a window dragged, the PC asleep), the time lost is
let go and not caught up.

The picture waits for the display (vsync) where the display shows 59
pictures a second or more, which is no cost at 60: the game's rate is a
little under the display's, so one picture in about 350 is shown twice.
A slower display would hold the game back; the port then says so and
stops waiting for it, at the start if the display says its rate, or
when time has been let go three times in half a minute.

**The sound card and the clock.** The card plays 44,100 samples a second
by its own crystal and the PC's clock counts by another. So the length
of the queue of samples steers the blanks: where the queue is longer
than it should be each blank comes later, where shorter sooner, by at
most eight samples' time in a blank's 737 (one part in a hundred; a
card is off by far less). In the long run the game keeps the sound
card's time, and no sample is dropped or made up to do it.

**Sound.** SDL's default device, 44,100 a second, stereo, 16 bits, asked
for 1,024 samples at a time; SDL converts where the card wants
otherwise. Each blank's samples go into a queue of 8,192 that is kept
2,560 long (58 ms), and SDL's thread takes them from it. From a sound
being made to its being heard is that and the card's own buffer (23
ms): about 80 ms. If the card asks for more than is waiting, the last
sample fades to silence over 128 samples, and play begins again, fading
in, only when the queue has its length back: a stall is a short silence
with soft edges, not a crackle. If the queue is ever full, its oldest
samples go.

**The pads.** The game reads a digital pad's buttons, and takes both
kinds of analog pad for digital ones without reading a stick (`pad.c`);
so the port's two pads are digital, and a controller's left stick
pushed past the dead zone is the direction's button. The first
controller plugged in is player one's, the second player two's, at any
time: one that is pulled out lets go of what it held, and the next one
plugged in takes the free place. Both players also have keys. The
layout built in:

| | Player one | Player two | A controller |
|---|---|---|---|
| Up, Down, Left, Right | the arrow keys | I, K, J, L | the direction pad, the left stick |
| Cross, Circle | Z, X | G, H | A, B (the lowest and the right of the four) |
| Square, Triangle | A, S | T, Y | X, Y |
| L1, R1 | Q, W | R, U | the shoulders |
| L2, R2 | 1, 2 | 5, 6 | the triggers |
| Start, Select | Enter, Right Shift | P, O | Start, Back |

Escape, or closing the window, ends the program. The program says the
layout in use on its error output as it starts.

**The settings file** is `rebushido.cfg` beside the program, or the one
`--config FILE` names; with none, everything is as built in, and
`rebushido --help` prints the built-in settings as such a file, to copy
from. A line is `name = value`, and `#` begins a comment:

```
scale = integer            # fit, integer or stretch
filter = nearest           # nearest or linear
fullscreen = off           # on or off
vsync = on                 # on or off
deadzone = 40              # how far a stick counts, in parts of 100
p1.cross = key Space       # takes Z away from player one's Cross
p1.cross = key Keypad 0    # ... and a second key for it
p2.start = pad a           # player two's controller: A is Start
p1.r2 = pad +righttrigger  # an axis pushed one way: + or -
p2.select = none           # nothing presses it
```

A line for a pad's button takes the built-in keys of that button away
(or its built-in controller buttons, for a `pad` line) and adds its own,
and takes its own key or button away from whatever that pressed as
built in; more lines add more. The names are SDL's: a key's as SDL names it (`Up`,
`Space`, `Return`, `Left Shift`, `Keypad 4`), a controller's button
`a`, `b`, `x`, `y`, `back`, `start`, `leftshoulder`, `rightshoulder`,
`dpup`, `dpdown`, `dpleft`, `dpright`, an axis `leftx`, `lefty`,
`rightx`, `righty`, `lefttrigger`, `righttrigger`. A line the port
cannot use is said, with why, and the rest is used. There is no page
for any of this in the game.

**The session's log.** Every session writes one, always:
`logs/rebushido-YYYYMMDD-HHMMSS.log` beside the program, the newest
twenty kept (if the program's folder cannot be written to, in the
folder SDL names for a program's own files; the console says where;
`--log FILE` names it, `--no-log` leaves it out). Everything the port
prints, to either output, goes into it too, a line at a time, each
written out as it ends and with the seconds since the start before it:
so what was said before a crash is in the file. It begins with the
version, the system and its bits, SDL's version, the command line, the
image (each file's size and a CRC-32 of its first megabyte), the
settings file and any line of it not used, the pads' layout; then, as
the window opens, the video driver, renderer, display and vsync, the
sound driver, device and form, and each controller as it is found.
While the game runs it notes each change of the game's screen, a
fight's beginning (mode, stage, both fighters and weapons, by name) and
its end, every read of the disc (first sector and how many; the game
knows files by position, so there is no name), a controller plugged in
or pulled out, vsync given up, time let go, the sound running out
(counted by the minute), and a line every ten seconds with the blank,
the game's frame, both rates and the queue's length. At a fault the
port's report goes in (what, the address, the function, its callers,
the blank, the screen and frame), then the last 32 functions of the
game that began (a ring the build's instrumentation fills, one that
repeats kept once), the fight's mode, stage and fighters, and the file
is shut before anything else is tried; on Windows a box then tells the
player the game stopped and where the log is. The port's own stops, a
start that failed and an ordinary end are each said, with how long the
session was. The log is the window build's alone: the build without a
window has two more empty pointers and the ring of the last calls
(`include/port_platform.h`), and prints what it printed.

**The counter.** In a corner of the picture, top left unless the
settings say otherwise (`fps = on|off`, `fps_corner = top-left|
top-right|bottom-left|bottom-right`, `--no-fps`; F3 shows and hides
it): `59.8/30.0 100%` is the pictures shown a second, the game's own
frames a second, and the pictures as parts in a hundred of the
console's 59.83. The game works a frame out every second blank in a
fight and every blank in the main menu, so 30 in a fight is full speed.
It is drawn by the window, in digits of its own of five pixels by
seven, as a small texture over the picture after the picture has been
handed to SDL: nothing of it is in the game's video memory, and a run's
pictures are compared without it. Each of its pixels is a whole number
of the window's, as many as the picture's 240 lines are, so it grows
with the window and stays sharp. On by default for now.

**The game's data.** `--image FILE` as before. Without it the first
`.cue`, or else the first `.bin`, beside the program is taken; if there
is none, on Windows the system's dialog for choosing a file opens, and
elsewhere the program says what it needs and ends. **The memory card is
still a stub:** the game finds no card, so its settings and what a
player has unlocked are not kept from one run to the next.

**Windows.** The program is 32-bit, linked at `0x08400000` with address
randomisation off and marked as able to use addresses from 2 GB up
(`--large-address-aware`), which the PlayStation's RAM at `0x80000000`
needs. On a 64-bit Windows such a program has all 4 GB, and the three
pieces of memory were free where they were tried (Windows 11): the RAM,
the scratchpad and the game's stack at `0x7FC00000`. They are asked for
before SDL starts. If one is taken the program says which and stops.
Three things Windows has its own way (`src/port/host_windows.c`):

- Addresses are given in blocks of 64 KB, and the sound chip's registers
  are 4 KB past the scratchpad: whole blocks are kept, and a piece
  inside one already kept only has its pages made usable.
- Windows keeps where a thread's stack is, and passes a fault to nobody
  while the stack pointer is outside it. The switch to the game's stack
  tells it the new stack's two ends.
- A stack has a chain of fault handlers. The game's stack is given one
  of the port's at its top, which is asked only when nothing above it
  took the fault (a system library's own handlers among them), and
  reports as on Linux; before the switch the system's last filter does
  the same.

Two things MinGW's compiler has its own way, which the build sets
right: bitfields are laid out as on every other host
(`-mno-ms-bitfields`), and a small variable's section, which a Windows
object pads to four bytes, is kept as small as the variable (three
neighbours of two bytes that the game reads as one array,
`g_fight_blow_dir_x` and the two after it, were four bytes apart before
that: found by the build's count of neighbours, not by a fight).

**How the window is proved**, with nothing shown or sounded: SDL's
`dummy` video and audio drivers, on Linux (WSL) and on Windows 11, with
the three plans of `port_game`, `port_other_fight` and `port_bugfixes`
(the port's side of each: 3,880, 4,120 and 3,910 blanks), with no card in
either slot, as those tests have it. Both programs are the 64-bit ones,
and what they are held to is the 64-bit build without a window: on Linux
`make port`'s own, on Windows the `rebushido-headless.exe` built beside
the window (the same object, kernel32 alone). Each program is given a
plan written for its own addresses, and a card must be ruled out: a
program that reads a card of its own beside itself loads a setting a run
before it saved there, and the runs differ. The plans of those three
tests say `nocards` and the port obeys it ("What is left"); `--no-cards`
says the same thing on the command line.

| | Linux | Windows |
|---|---|---|
| The trace of every watched value at every blank, against the build without a window | identical, all three (2,333, 2,457 and 2,334 lines), run as fast as the PC goes and run at the console's rate | identical, all three, both ways |
| The same with the plan's presses made as SDL key events, read back through the layout | identical, all three | identical, all three |
| ... and as buttons of two controllers SDL makes up (its virtual joysticks), plugged in as the run starts | identical, all three | identical, all three |
| Every twentieth picture as SDL is handed it, against the PNG the build without a window writes at the same blank | 688 pictures (225, 237 and 226), 42,270,720 pixels, none different (a PNG writes each row twice where the picture is wide, which the window leaves to SDL) | the same 688, and all but the last of each run are the same: in the last 4,531 pixels of 153,600 differ, the main menu's panel still fading in, because the two programs end a frame apart (in `port_bugfixes` the one without a window writes one picture more) |
| Every sample as it is queued for SDL, against the build without a window's WAV | 10,108,350 samples, identical | identical |
| The program without a window (`rebushido-headless.exe`, kernel32 alone) | | runs, and its trace is the same |
| A fault made on purpose, on the game's stack (`--fault-at`) and on the sound card's thread (`--fault-in-sound`) | both reported with the address, exit code 70 | the same: the 64-bit program hears of the game's thread first and leaves the other threads to the system's last handler |

At the console's rate, with no sound card, 3,880 blanks took 64.839
seconds: 16.715 ms each. On every push `tests/port/window.c` holds the
arithmetic to 881 cases worked out by hand: the picture's place in
windows of every shape, pixels at video memory's edges, the queue
running out and filling up with no step larger than a fade's, the clock
going round at 32 bits, time let go, a sound card two parts in a
thousand fast and one as slow for 20,000 blanks with nothing running
out, every button's name, keys and sticks, a settings file's lines and
the ones it cannot use; a log's name, which of 25 logs go, lines out
of pieces, a rate each half second with more pictures a second than the
console's blanks, every digit's shape, the counter's
place; the remastered picture's own shape in a window of every shape,
how many times the console's lines a scale draws so that its pixels are
square, and the five settings that choose it. 33 mistakes made on
purpose in the first four files, one at a time, each failed it.

With the log and the counter on, every run of the table above was made
again on both systems: the traces, the pictures and the samples are the
build without a window's still. The log of `port_game`
has its header, the screens in their order, the fight's beginning by
name and its end; a fault made on purpose on the game's stack
(`--fault-at`) and one on the sound card's thread (`--fault-in-sound`)
are both in the log with the function's name, on Linux and on Windows.

**Not proved, and what may be wrong:**

- Nobody has seen the window or heard the sound. That the picture
  handed to SDL is right is proved; that SDL shows it the right way up,
  in the right colours and at the right size is read from SDL's
  documents and not seen. The same for the sound card, for full screen,
  for a real keyboard and a real controller: the events were SDL's own,
  made by the test.
- The file dialog on Windows and the box that says the game stopped
  were written and read, and never opened. The counter was never seen:
  its pixels are tested, its look is not.
- SDL's dummy sound card is not a clock: it took samples unevenly and
  up to one part in a hundred fast on Linux, and more than one part in
  a hundred slow on Windows. The steering followed it as far as it goes
  (on Linux the sound ran out 5 to 23 times in a minute; on Windows the
  blanks were moved by the most allowed and the queue threw three
  samples in a thousand away: as they should with such a card). Whether
  a real card ever makes the sound run out is not known until one is
  heard.
- The wait for the display was never run against a display.
- Windows was tried on one machine, Windows 11, and only as a 64-bit
  program. Nothing else is known to be at the three addresses the port
  asks for, and nothing promises it.
- A fault's report has the address, the function and its callers, not
  every register: the host's interface keeps three of them.
- The game's stack is a megabyte, and SDL and the display's driver now
  run on it too.

## Remastered

"Graphics: Remastered" (`g_rb_settings.graphics`, the options page's
second line) on the port means **the scene drawn again, by the graphics
card, at the window's resolution, from the geometry the PlayStation
threw away**. With "Graphics: Original" nothing of this runs and the
port draws exactly what it drew before: the software GPU's picture, the
same pixels, still the one the play tests compare with the emulator.

This is the roadmap's item 7. What it does now: high resolution,
sub-pixel corners, textures in perspective, no dither, true-colour
mixing, the flat things crisp (the first step); then the stage drawn to
the far end of it with its fog pushed out to match ("How far the stage
is drawn"), the scene's edges smoothed ("Edges"), and a wider view of
the scene at a wider shape of picture ("A wider picture"), and more
pictures a second than the game has frames ("More pictures a second").
What it leaves for the step after it, by design and not by accident: new
lighting and shadows ("What each later step needs").

**Three settings of a run** say how much of each (`port_remaster.h`,
`PortRemasterView`), each of them the drawing path alone:

| | | |
|---|---|---|
| `draw_distance` | `original`, `far`, `max` | how many of the stage's cells are queued, and how far out the stage's fog is placed |
| `antialiasing` | `off`, `2`, `4` | how many times over each way the scene is drawn before it is averaged down |
| `aspect` | `4:3`, `16:9`, any `W:H` | the shape of the picture: a wider one is a wider view of the same scene |

They are a run's and are settled before the game starts: the primitive
buffers the port hands the game are chosen from the first of them. Every
one of them is `original`, `off` and `4:3` with nothing said, so a run
that asks for none of them is the first step's picture to the pixel.
With "Graphics: Original" none of them does anything at all, whatever
the run was started with, and the game computes and draws what it always
did.

### Why the card, and not a wider rasteriser

The owner's list is high resolution, then anti-aliasing, then no pop-in
and more to see, then the wide picture, then 144 pictures a second, then
better lighting and shadows. The last three are what decides it: a
software rasteriser could be made wider, but lighting a fighter's
vertices and sorting a stage at 144 Hz with 4 by 8 times the pixels is
work a card does for nothing and a PC's processor does not. The
measurement here, on this machine with Mesa's software driver
(llvmpipe, no card at all), is half a millisecond a frame at the
console's resolution: a real card at 8 by 16 the pixels has room that a
rasteriser in C, which takes about 6 ms a frame at 1x, does not.

OpenGL 3.3 core through SDL is what the window already has to hand on
both systems, and the headless prover gets the same context from EGL.
Nothing is vendored: the names of the 45 GL functions this uses are
declared in `src/port/gl/gl_draw.c` and looked up through the host
(`SDL_GL_GetProcAddress`, `eglGetProcAddress`).

### The side channel

A PlayStation primitive is already flattened: whole-pixel screen
coordinates held to eleven bits, no depth, and a texture that runs
evenly across the screen instead of in perspective. That is where the
wobble and the warping come from, and no amount of resolution mends it.
So the renderer's own arithmetic is kept, beside the primitive, and the
hardware draws from that.

**What is kept.** The GTE's projection (`port_GteProject`,
`src/port/gte.c`) works out the screen position with sixteen bits of
fraction in MAC0 and then shifts it down and holds it to eleven bits.
Both halves are now written down, with the depth before it was held to
sixteen bits and the quotient the projection divided by
(`PortPrecisePoint`, `include/port_remaster.h`). Three of them stand
for the three screen positions the coprocessor keeps, and a position
the program puts in a register itself clears the one it lands on.

**How it reaches the primitive.** Two things carry it:

- *Where a point was stored.* `port_GteStoreData` writes down the exact
  point under the address it stored the whole-pixel one at. The
  renderer stores a part's points onto the scratchpad and names its
  faces' corners among them; a stage tile that is cut has the GTE store
  its corners into the primitive itself, and that address is the
  primitive's own word, which is enough by itself.
- *Which points a face is.* `render_LoadFace` (`src/main/render_asm.c`)
  is where every one of the fifty emitters looks its face's corners up,
  and `render_LinkEntry` is where every primitive of the scene is
  linked into an ordering table. Two lines, one in each, say "this face
  is these points" and "that face became this primitive". The side
  channel is then a table from a primitive's address to its corners.
- *What a primitive is a drawing of.* Four more lines say which thing
  the renderer is about to draw (`render_DrawQueue`,
  `render_DrawStageTiles`), which mesh of it is being walked
  (`render_LoadFace`, the tile loop) and that a tile's chained
  primitive is the next piece of its face (`render_ChainTile`). That
  is the name the pictures between two frames pair by ("Which
  primitive of one frame is which of the next").

Those lines, and the one in `port_GteStoreData`, are the whole of
what the renderer gives up. Nothing is computed differently and nothing
of the game's memory is written: the traces of the three play tests are
the same with the remastered renderer on and off ("How it is proved").

**Why not keyed by the primitive's address alone**, as a side channel
usually is: the renderer writes a primitive's corners from the
scratchpad, so the primitive's own words are not where the GTE put
them. Keying the *points* by where they were stored and the *faces* by
the primitive they became covers both the ordinary path and the cut
tiles, with no table of exceptions.

**What is forgotten, and when.** Both tables are open tables of 8,192
slots whose entries carry the frame they were written in; a new frame
begins where the game clears the next frame's ordering table
(`ClearOTagR`). An entry of the frame before the last counts as
nothing, which is safe because the game has two primitive buffers and
two ordering tables and uses them on alternate frames: nothing of a
frame's own can have been written over. When every slot a key could
have is another live entry's, the oldest goes and that primitive is
drawn flat; that happened 114 times in 687,410 faces of a whole run.

### How a primitive gets to the card

The software GPU is not changed and not bypassed. `port_RunCommand`
(`src/port/raster.c`) draws the command as it always did, and then
hands the same words to `port_RemasterCommand`, which decodes them
again — the same corners, colours, texture points, page, colour table,
window, drawing area and mix that `port_SetUpPaint` works out — and
adds, for each corner, the exact position and depth the side channel
has for it. `DrawOTag` says which primitive the words belong to. The
decoded primitive (`PortRemasterPrim`) goes to a backend through four
function pointers; with no backend attached (`make port`, every check,
and the window with "Graphics: Original") every one of these returns at
once.

So the hardware path never sees the game, only the GPU's own command
stream: a primitive kind that is added or a mode that changes reaches
it without a line of new code, and a primitive it cannot draw is
counted rather than lost.

### What the card draws into

The colour buffer is **all of video memory at the scale**: 1024 by 512
pixels times `scale` across and `tall` down. A scale of 4 and 8 is
2,560 by 1,920 for a 640 by 240 display — square pixels, because a line
of the console's picture is two lines of the screen.

- A **polygon** is drawn where the GTE really projected it, with
  sixteen bits of fraction, as two triangles for a four-sided one, as
  the chip does. `gl_Position` is `(x·w, y·w, 0, w)` with `w` the
  corner's depth, so the card's own perspective divide puts the corner
  back where it belongs and interpolates the texture in perspective. A
  colour runs evenly across the screen (`noperspective`), as the chip's
  does.
- A **texel** is read from a mirror of the software GPU's video memory,
  a 1024 by 512 texture of 16-bit words, by the shader: four bits or
  eight through the colour table or fifteen as they are, through the
  texture window, a texel of nothing not drawn. Nothing is decoded on
  the processor and no texture is cached by page: the mirror is the
  whole of video memory, so a colour table that is tinted, a texture
  that is scrolled and a page that is loaded over are right by
  construction.
- **Rectangles, sprites, the fonts, the menus and the options page**
  have no side data and are drawn as they are, whole-pixel, at the
  scale: crisp, and in their proper places. **Such a primitive's texel
  and colour are read once for each pixel of video memory**, not once
  for each pixel of the picture: the chip settled them at the pixel's
  own whole coordinate, so the shader steps back from the pixel it is
  drawing to the corner of the block of `scale` by `tall` pixels it
  lies in and reads them there (the flag `FLAG_FLAT`, and `dFdx` of the
  two, which are exact because both run evenly across such a
  primitive). Without that step the half-texel the chip starts in by is
  half a *picture* pixel and not half a pixel of video memory, so the
  right part of every block shows the next texel along: at the console's
  resolution it is nothing, at eight times it is half the block. That
  was what made the menus come apart into black lines on a real card —
  where the next texel along is nothing, the fragment is thrown away
  and the background shows through. `--gl-blocks` measures it ("How it
  is proved, with nothing shown").
- The **four mixes** are the card's blending at eight bits a channel
  instead of the chip's five: half and half by `SRC_ALPHA`, added and a
  quarter added by `ONE, ONE_MINUS_SRC_ALPHA` with the shader setting
  the alpha, and "what is there less this" by the reverse subtraction.
  Where a texture mixes only at the texels whose top bit is set, the
  alpha says which; the subtracting mix cannot say that with one
  blending state, so its batch is drawn twice, once for each kind of
  texel.
- **Dithering** is off: the pattern is there to hide the chip's five
  bits a channel, and nothing here has five bits a channel.
- The **drawing area** is the scissor, and the drawing offset is in
  every corner, as the chip has it.
- **What the chip would not draw is not drawn.** A triangle that spans
  more than 1023 pixels across or 511 down, or has no area, is left out
  by the same whole-pixel rule the software GPU uses: a corner the
  projection put far off the screen is held to eleven bits with a sign,
  so a polygon that is partly behind the camera comes out spanning the
  whole range, and the game counts on the chip dropping it. Leaving
  this out was the one thing that showed as a wrong picture: a stage's
  surf drew a bright band across the water (`port_GlIsDrawn`).
- Primitives are **batched** in the order the game drew them, and the
  batch is flushed when the mix, the drawing area or the buffer's room
  changes, and whenever the game writes video memory: the ordering
  table's order is the order the card draws in, so the painter's order
  the game sorts by is kept exactly. A fight frame comes to about 5,400
  vertices in 113 draws, and 110 of those draws are the fills and
  copies the game makes in the middle of the frame — its fog is 31
  colour tables copied each frame — each of which ends the batch
  before it. Everything else about a frame is one draw.

**The texture filter is nearest, and that is the default.** The game's
textures are 4-bit and small, and its pages pack unrelated textures
against each other with no border: smoothing reads a neighbour that
belongs to another texture, so an edge bleeds. A bilinear filter is
written (`--filter linear`, four decoded texels, those that are nothing
left out so that an edge does not bleed into black), and it is for the
owner to look at; nearest is what the game was drawn for.

**Anti-aliasing is not done**, and the hook is where it goes: the
colour buffer is a texture of the port's own, so turning it into a
multisampled one and resolving it is a change to `port_GlMakeTarget`
and nothing else. Why it was left: with primitives in painter's order
and no depth buffer, multisampling is correct (every sample of a
coverage mask is blended in the same order), but the mixes above are
done at a sample's own coverage, and a half-transparent primitive drawn
over an edge then mixes twice along it. The honest first look is the
high resolution alone, which the owner can see; the step after it
should measure the edges.

### What the game does to video memory, and what becomes of it

Everything the game makes by moving pixels about is drawn by stretching
the mirror of video memory into the colour buffer, in its turn among
the primitives: a fill, a copy inside video memory, or a load from the
program says which rectangle changed, the batch so far is flushed, that
rectangle is sent to the card and drawn at the scale. So these all
work, and all of them keep the console's own resolution, which is what
they are:

| What the game does | Where | How it comes out |
|---|---|---|
| The loading screens' picture, and the noise over them | `disp_DrawLoadingScreen` | a load from the program, then rectangles drawn over it |
| The fades and the grayscale | `disp_`, `tex_TintClut` | colour tables written in video memory: the mirror has them, so a fogged or tinted texture is right |
| The stage's fog | `stage_QueueFog` | copies inside video memory, 31 rows of colour tables a frame |
| The character select's portraits | `select.c`'s sprite pools | loads from the program into pages, then textured rectangles |
| The replay and the menus over the scene | 2D primitives | drawn as they are, crisp |
| The movie | the overlay, 24-bit mode | **not done**: the display in the mode of three bytes a pixel is left to the software GPU's picture |

**Nothing reads back what the card drew.** The game reads video memory
with `StoreImage`, and that is the software GPU's, which is still
exact. A texture made by drawing polygons into video memory would be
the one thing the mirror could be stale for, and the port counts it: in
the three play tests, 0 textured primitives of 1.5 million read a page
this frame had drawn into.

### How it is proved, with nothing shown

There is no window and no display. The prover
(`src/port/gl/gl_main.c`, built by `tools/port_gl_build.py` into
`build/port-gl/rebushido-gl`) takes an OpenGL 3.3 core context from
EGL's surfaceless platform and draws into a framebuffer object; Mesa's
software driver is the card. Nothing was installed for it: Ubuntu
24.04's `libegl-dev`, `libegl-mesa0` and `mesa-libgallium`, which the
window's own needs already brought in, are enough, in 32 bits as the
port is. It is run by `tools/port_gl_prove.py`, which writes each play
test's plan for the port, runs it three ways and prints what it
measured.

**At any scale, and at two scales unlike each other, what the game
draws flat must be the console's own picture scaled up by whole
blocks** (`--gl-blocks`). A pixel of video memory is a block of `scale`
by `tall` pixels of the remastered picture, so for every pixel of the
console's own picture the prover asks two things of its block: that it
is of one colour at all, and that the colour is the console's. The
bands beside the console's 640 pixels in a wider picture are the
scene's alone and are left out. What a flat screen may still differ in
is a pixel that mixes, which the card mixes at eight bits a channel and
the chip at five, and the edge of a flat polygon that is not up and
down, which the card draws at the picture's own resolution.

Measured over the boot, the options page, the top menu and the
character select, with `aspect 16:9`, every twentieth blank:

| The scale | Pictures | Pixels | | Blocks not of one colour | Blocks not the console's colour | ... by more than a step | The worst channel |
|---|---|---|---|---|---|---|---|
| 4 across, 3 down | 28 | 3,686,400 | before | 645,949 | 1,170,316 | 515,940 | 255 |
| | | | after | **0** | 968,059 | 7,439 | 11 |
| 8 across, 6 down | 8 | 614,400 | before | 4,066 | 9,719 | 4,066 | 255 |
| | | | after | **0** | 7,904 | **0** | 7 |
| 4 across, 8 down | 8 | 614,400 | before | 4,066 | 9,719 | 4,066 | 255 |
| | | | after | **0** | 7,904 | **0** | 7 |

The 255 is the measure of the defect: black where the console has a
colour. What is left is the pixels that mix, which the card mixes at
eight bits a channel and the chip at five: that is the whole of the
"not the console's colour" column, and two of a five-bit step is as far
as any of it goes. `tools/port_gl_prove.py` runs this on each of the
three plans and fails the run if any block is broken up or any pixel is
more than two steps out.

(At 8 across the 640-wide screens come to more pixels than the prover
keeps a picture in, so only the 320-wide ones — the boot and the
options page — are measured there. The run at 4 across covers every
flat screen.)

**At scale 1, with everything that changes a value turned off**
(`--gl-exact`: the corners where the PlayStation held them, the texture
flat across the polygon, five bits a channel with the chip's own
dither), the hardware's picture is compared with the software GPU's,
pixel for pixel, every twentieth blank of each of the three plans. A
blank at which the display is blanked, or in the mode of three bytes a
pixel, is not compared: there is nothing on show to hold to anything.

| The plan | Pictures | The same | Pixels | That differ | ... within one step | ... further |
|---|---|---|---|---|---|---|
| `port_game` | 183 | 35 | 27,417,600 | 612,850 (2.2%) | 581,940 (95%) | 30,910 (169 a picture) |
| `port_other_fight` | 195 | 33 | 29,260,800 | 6,789,077 (23%) | 5,593,381 (82%) | 1,195,696 (6,131 a picture) |
| `port_bugfixes` | 186 | 37 | 27,648,000 | 639,084 (2.3%) | 607,554 (95%) | 31,530 (169 a picture) |

(Measured again on the tree that puts the HUD at the picture's edges,
where a run of a plan that says `nocards` has no memory card at all and
reaches each checkpoint a few blanks sooner, so the pictures are not the
same frames. The tree before it gave 184, 196 and 188 pictures and
600,577, 6,644,816 and 613,035 pixels that differ, and the tree the
renderer was written on 633,002, 6,809,595 and 645,040: the same picture
to within a twentieth of the count each time, the game's own code having
moved on in between.)

A *step* is one of a five-bit channel's 31, eight of a byte's 255: a
pixel that differs by a step or less is the same colour the chip can
show, one value along. The pictures that are the same to the pixel are
the options page, the boot image, the menus and the character select —
everything the game draws flat. The second fight's stage is the cave,
whose walls are one texture across the whole screen close to the
camera, and that is where the pixels beyond a step are.

What differs, and why:

- **Pixels that mix.** The chip mixes at five bits a channel and the
  card at eight, so a half-transparent pixel comes out within half a
  step of the chip's value and almost never on it. That is most of the
  count above: the menu's panel, the clouds, the character select's
  grid. They are all within one step (eight of a byte's 255).
- **Shaded polygons.** The chip interpolates a colour as whole numbers
  with twelve bits of fraction from the triangle's leftmost corner; the
  card interpolates in floating point from the pixel's middle. The two
  agree to a step.
- **A texel's edge on a pixel's middle.** Where the texture coordinate
  comes out exactly whole at a pixel, which of the two texels is read
  follows from the last bit of the arithmetic, and the two arithmetics
  are not the same: the chip walks the polygon in whole numbers with
  twelve bits of fraction from its leftmost corner, and the card
  interpolates in floating point from the pixel's middle. These are the
  pixels that differ by more than a step, and they are the seams of a
  texture: 168 a picture on the rooftop, 6,100 a picture in the cave,
  where one texture is spread over the whole screen close to the
  camera. It is the same kind of difference as the halfway values the
  port already has against the emulator ("The picture"), and at the
  resolution the remastered picture is really drawn at there is no
  seam to put wrong: this is a cost of the comparison, not of the
  renderer.

**With the renderer on and off, the game computes the same thing.** The
trace of every watched value at every blank of the three plans is
identical, line for line, with `--graphics remastered` and `--graphics
original`: 2,333, 2,457 and 2,334 lines on this tree. (For a while
`port_bugfixes` parted company at the options page, and so did two runs
of the one program that differed only in `--gl-scale`. That was a memory
card image left beside the program by the run before, not the renderer
and not a read of memory the game never wrote; the plan says `nocards`
and the port obeys it now — see "What is left".) **And with each of the three settings**: the traces of
`port_game` with `draw_distance far` and `max`, and with the remastered
picture off but the run started with `max` (so that the game has the
port's own primitive buffers), are all the same 2,340 lines as the run
with the picture off and nothing asked for.

**The build without a window draws and computes what it did before.**
With `RB_REMASTER_VIEW` off the game's files are exactly what they were
before this work, so `make port` built both ways and run on the three
plans is the before and the after: the traces are identical (2,340,
2,464 and 2,341 lines) and all 1,048,576 bytes of video memory at the
end of each run are identical.

**The software GPU is untouched.** The build without a window, before
this work and after it, run on the three plans: the traces are
identical line for line (2,339, 2,463 and 2,340 lines) and all
1,048,576 bytes of video memory at the end of each run are identical.
(The two programs were built from the two trees and run side by side;
the script that does it was scratch and is not kept.)

**No kind of primitive is missed.** Every command the GPU is given is
counted by kind, and every one the backend drew: in the three plans the
two counts are equal for every kind of polygon, line and rectangle (the
fills, copies and loads are not primitives and are counted apart).

**Pictures.** At scale 3 across and 6 down, the hardware's picture and
the software GPU's are written side by side at the same blanks
(`build/remaster/<plan>/gl-*.png` and `software-*.png`).

**What a frame costs**, on this machine with no graphics card at all
(Mesa's llvmpipe, 32-bit), over the whole of `port_game`'s 3,880
blanks with nothing read back:

| | The run | The hardware path, a frame |
|---|---|---|
| Graphics: Original (the software GPU alone) | 5.88 s | nothing |
| Remastered, 1 by 1 (the console's own resolution) | 24.64 s | 0.47 ms |
| Remastered, 2 by 4 (1,280 by 960) | 29.02 s | 1.17 ms |
| Remastered, 4 by 8 (2,560 by 1,920) | 44.08 s | 3.20 ms |

The software GPU draws a frame of its own in about 1.5 ms (the whole
run, less the game, over the blanks), so a software rasteriser four
times across and eight down would be some fifty times that. 3.2 ms a
frame at 2,560 by 1,920 **with no card** is what makes 144 pictures a
second a question of the game's own rate and not of the drawing.

### What is not done, and what may be wrong

- Nobody has seen any of it on a screen, and nothing of it has run on a
  real graphics card: the pictures are files, drawn by Mesa's software
  renderer. The window the player runs does use this path
  ("Remastered in the window").
- The movie's 24-bit display is the software GPU's picture.
- The mask bit is not drawn: the game never uses it (0 primitives of
  1.5 million in the three plans were drawn with it set or looked at).
- Lines are not drawn by the hardware path. The game drew none in any
  of the three plans — the renderer's outline routines, which are the
  only lines it has, are not reached by anything the plans do. When
  something does reach them they will be counted as not drawn, which is
  what the counts are for.
- In the prover the scale is `--gl-scale`'s; in the window it is the
  window's own size by `port_RemasterScaleFor`, worked out again
  whenever the window is resized or the game changes its display mode.
- New lighting is a later step.
- **Nobody has seen the pictures between the game's frames move**
  ("More pictures a second"), so `frame_rate original` is still the
  default. They were written to files and held to the two frames they
  lie between, by the pixels and by a watched point, and that is all.
  What may show: a face the frame after it has not — something spawned
  or gone, or one the projection put outside the eleven bits the chip
  holds — is drawn where it was and waits for the next frame instead of
  moving; the 2D layer, the HUD and the clock stay at the game's rate by
  design; a camera cut shows the game's own picture until the cut is
  over; and a primitive the side channel has no depth for (18% of a
  scene's) is one of those that do not move.
- **A stage drawn whole shows where it stops.** The stages were built
  to be seen through a fog that ends 26,000 units out; at
  `draw_distance max` the ground reaches its own far edge, and on some
  stages that edge is a straight cut with nothing behind it. It is the
  stage's data and not a fault of the drawing: the cells simply end.
  What would hide it is a backdrop at the far edge, which only the
  castle rooftop has (`BACKDROP_DISTANCE`, `stage_hooks.c`).
- **The level of detail still steps.** A tile within three cells of the
  camera is cut into sixteen, within six into four, and beyond that not
  at all (`stage_ResetCellFlags`, `g_stage_detail_pattern`). On ground
  that is not flat the shape changes a little at those two distances,
  and the remastered picture shows it more clearly than the console's
  does. Giving every cell the finest detail, so that nothing of the
  ground ever changes shape, was written and measured and taken out
  again. The primitive buffer holds it — 95 KB of the port's megabyte —
  but the side channel does not: a tile that is cut has the GTE store
  its corners into the primitive itself, so sixteen ways instead of one
  is sixteen times the addresses in a table of 8,192 slots. In a fight
  it was full 845,713 times instead of 99, and the share of the scene
  drawn flat — the corners at the console's whole pixels, the texture
  not in perspective — went from 16% to 31%. A worse picture for a
  steadier one is not a trade worth making; what it needs first is a
  side channel that does not key a point by the address it was stored
  at.
- **The smoothing is held to a power of two.** Three times over moves
  8,983 pixels of a flat picture of 1,228,800, where two and four move
  7. The reason was looked for and not found, so the setting takes 2
  and 4 and rounds anything else down.
- **The HUD does not move out to the edges of a wide picture** ("A
  wider picture", "What is not done").
- A primitive whose corners the GTE did not project — a 2D one, or one
  of the scene's that the side channel has no depth for — is drawn
  flat, exactly where the PlayStation put it. That is right for the 2D
  layer; for the rest it is the console's picture at a higher
  resolution and nothing worse. **How much of the scene that is**, over
  a whole fight with the camera swung right round at `draw_distance
  max`: of 927,425 polygons, 62,207 (6.7%) were drawn flat, and all
  107,070 rectangles, which have no depth by nature. Such a polygon
  sits at the console's own whole pixel while the ones about it sit
  where the projection really put them, so the two part by up to half a
  pixel of video memory — four pixels of the picture at eight times the
  resolution — and which of them it is changes from frame to frame.
  Which primitives they are was not traced; where to look is the
  drawing that works out a screen position itself instead of having the
  GTE store one (the shadows, the effects, the cloth). Nothing here can
  recover what was never written down: covering them means more lines
  of the side channel in the game's own drawing code.
- **A thin seam where two textured polygons meet.** The chip counts a
  texture point across a polygon in steps of its own and starts half a
  step in; the hardware path starts half a texel in, which is the same
  thing only where one texel is one pixel. Where a texture is drawn
  larger or smaller than it is, the two part by up to half a texel, and
  at the join between two polygons of different pages that shows as a
  line a pixel or two wide: 609 pixels of 153,600 on the screen where
  the stage is chosen, whose map is two textures side by side. Half a
  step is what the side channel would have to carry for it, or the
  attribute would have to be interpolated from the pixel's corner
  rather than its middle. (**The scale no longer makes it worse for a
  flat primitive**: the attribute is read at the corner of the block of
  picture pixels that one pixel of video memory is, so what is left is
  this rounding at the console's own resolution and not a strip of the
  block that grows with the scale. The 609 pixels are measured at scale
  1, where the two are the same point and the step back is nothing.)

### How far the stage is drawn

**What the console's limit is.** `stage_Draw` (`src/main/stage.c`) walks
a window of 31 by 31 cells of 2,000 units about the camera and queues
the tiles and pieces of the cells a mask names. The mask is one of 64,
by the way the camera looks (`g_stage_view_masks`, `stage_data.c`): a
wedge that reaches 15 cells ahead and is 8 to 11 cells wide. In the
usual view that wedge is **anded with `g_stage_near_mask`**, a disc of
13 cells — 26,000 units, which is `STAGE_FAR`. The window is 31 cells of
the stage's 32, so the window is not the limit: the wedge and that disc
are. The game's own far view (`stage_SetFarView`, which
`camera_UpdateSideView` turns on when the camera is far from its focus)
drops the disc and adds `stage_QueueFarCells`, the cells outside the
window.

**The fog ends where the disc does.** The stage's fog is not a shader:
it is 31 rows of colour tables, each blended further towards the
stage's far colour, copied in video memory by `DR_MOVE` primitives
linked into the ordering table at even steps of depth
(`stage_QueueFog`, `stage_BuildFogLevels`). The steps run from the
camera's focus to `STAGE_FAR` and cover at most the stage's own span
(`g_stage_fog_spans`, 13,242 to 32,000 units), and the most fogged copy
is linked at the table's last slot — so everything beyond the fog is
drawn in the fog's colour. That is why the ground ends in darkness on a
night stage: not a clip, a colour.

**Which side of the line.** Every one of those is the drawing path
alone. The masks, `g_stage_cell_flags`, `g_scene_settings.far_view` and
`far_shift` are read by `stage.c` and the renderer and by nothing else;
the stage's collision has its own grid (`g_collision_grid`), its
obstacles their own table (`g_fight_obstacles`), and the opponents'
navigation its own (`g_cpu_stage_obstacles`). Nothing about what is
queued for drawing reaches a fighter, a hit, the random numbers or the
sound.

**What the port does** (`#if RB_REMASTER_VIEW`, five places in
`stage.c`):

| `draw_distance` | The cells | The fog |
|---|---|---|
| `original` | the wedge and the disc, as the console | as the console |
| `far` | the whole wedge, and the cells outside the window | twice as far |
| `max` | every cell of the stage, whichever way the camera looks | four times as far |

The fog's colours are not touched, only where its levels are placed, so
a night stage stays as dark as it was and its gradient reaches further.
`max` also sets `g_scene_settings.far_shift` to 2: a tile's place and
its depth are kept in sixteen bits after the renderer's scale, and at
the near scale a tile 65,536 world units off reads negative and is
dropped, where the stage's grid is 90,510 units across its diagonal. The
cost of that scale is that a tile's place is kept to four units instead
of two, which is what the game's own far view already does.

**`max` is what the window starts with** (`keymap.c`, and the settings
file it writes). It is the only one of the three that draws the same
cells whichever way the camera looks: `original` and `far` both take
the wedge of the 64 masks for the way the camera is facing, so a cell
near the camera comes into the picture and goes out again as the camera
turns, which on a real screen is a stage that flickers at its edges.
With `max` the mask is every cell, and the count of tiles a frame
queued is the same every frame — the prover prints both ends of it
(`tiles` and `tiles_least` of `gl run limits`; 328 and 328 over a whole
fight with the camera swung right round).

**The limits, found and measured.** Each is counted every frame of every
run and reported (`port_remaster.h`, `PortRemasterLimits`; the prover
prints `gl run limits`):

Over a whole run on **each of the game's 32 fight stages**, with the
first fighter run across it (`tools/port_remaster_prove.py --stages
0-31`; the table is `build/remaster2/step3/measured.tsv`):

| What | How large | The worst frame of any stage |
|---|---|---|
| A frame's primitive buffer | 0x13400 (78,848) bytes on the console, at a fixed address in its RAM (`SYS_PRIM_BUFFER_0`) | 54,476 bytes used at `original` (the shrine), 56,468 at `max` (the parking lot) |
| The renderer's queue | `RENDER_QUEUE_SIZE` 640 entries | 198 at `original`, 547 at `max` (the dump site) |
| The ordering table | 4,096 entries; a depth beyond its reach is clamped to the last | the farthest slot used was 3,341 |
| The side channel's two tables | 8,192 slots each, 8 probes | full 114 times of 687,410 primitives at `original` and 89 times in a run at `max` |

So the console's own primitive buffer would in fact have held the whole
of every one of the 32 stages, with 22,380 bytes to spare. It is not relied on: the
game only looks after the frame is drawn, and an overflow prints
`common prim over flow` and hangs for ever (`src/main/main.c`). **Where
a run asks for more than the console's draw distance the port hands the
game two buffers of its own**, a megabyte each, out of memory it gives
itself at `PORT_PRIMS_ADDR` (`include/port_platform.h`) — a fixed
address, as the game's stack is, whose low 24 bits are no other
memory's, named to the ordering tables' links like the console's RAM
(`port_AddLinkRegion`). Nothing from `malloc` is ever given to the game.
With `draw_distance original` that memory is never asked for and the
game has the console's buffers at the console's addresses.

The ordering table never saturates: `render_DepthToOtShifted` spreads a
depth over the table in octaves of doubling width, and the farthest
slot any frame reached was 3,341 of 4,096 — the same with the stage
drawn whole, because the far cells land in the octaves the table has
room for. Nothing sorts wrongly at the new distances.

**What it is worth.** Over those 32 runs, `max` queues 1.1 to 2.9 times
the tiles (the dump site most, 184 to 535) and draws 0.2 to 5.8 per cent
more primitives (the parking lot most, 551,055 to 582,996). The extra tiles are mostly behind the camera or outside the
view and are thrown away by the renderer for nothing, which is why the
primitive count moves so little. **What the player sees is not the
primitives but the fog**: at `original` the ground fades to the fog's
colour a few thousand units out, and at `max` the stage is drawn and
lit to its own far edge.

### The ground as one mesh

The owner's remaining complaints about the remastered picture on a real
card were all of the stage's floor: pieces of it went missing for a
picture at a time, black boxes appeared, the level of detail stepped as
the camera moved, and pairing a tile with a tile between two frames was
the most fragile part of the pictures drawn between them. All of them
come from the same thing: **the console builds the ground again every
frame**. `stage_Draw` walks 31 by 31 cells about the camera, keeps the
ones a mask of the way it looks names, and `render_DrawStageTiles` makes
every tile's faces into new primitives, cutting each into four or sixteen
near the camera and halving its texture points far from it. Which cells
are drawn, how finely, and which primitive a face became all change from
frame to frame.

The owner: "we dont have to stick to the 'tile' approach the original
devs took primarily for optimization."

**`stage_mesh on` draws the whole of the ground from one static mesh**
built when the stage loads and kept in the card's own memory
(`src/port/gl/gl_stage.c`). The game goes on building its tiles exactly
as it did -- its logic, its primitive buffers, its ordering table and its
traces are untouched -- and the backend simply **does not draw the
primitives that came from a stage tile**, and draws the mesh in the place
of the first of them.

**What is in the mesh.** Every tile of `g_stage_tiles`, every face of the
mesh it names, cut as often as the renderer ever cuts it (the two cells
nearest the camera: sixteen ways), with:

- the corners the cutting makes, which are the game's own arithmetic
  (`render_SplitFT4`, `render_SplitMidpoint`: the middle of two corners,
  the two bytes of a texture point averaged apart and each kept whole).
  A quad whose four corners are in one plane takes the same shape
  however often it is cut and is kept whole: four corners instead of a
  hundred, and a texture the card runs across the whole of it instead of
  a step at every piece's edge where the cutting rounded a texture
  point. (Cutting those as well was measured: 3.4 times the vertices,
  and the picture no nearer the tiles' own.);
- the face's texture page, colour table and texture points as its record
  has them. The renderer halves the points for a cell far from the camera
  and for a face small on the screen (`render_TileShrink`); with the
  renderer's own options (`RENDER_OPT_DEFAULT`) and a cell cut this
  finely it halves them not at all, so these are the points the console
  itself uses on the ground nearest the camera;
- the colour the coprocessor lights the face with, worked out by running
  the GTE's own `NCS` on the face's normal with the tiles' light setup
  (`LIGHT_SCENERY`), exactly as `render_EmitTile` does. The whole of the
  coprocessor is put back afterwards, so nothing the game computes
  changes.

**Where the camera comes from.** Five numbers of the renderer's are
carried out through the side channel where it draws the tiles
(`port_RemasterNoteStageView`, one call in `render_DrawStageTiles`): the
view's rotation, where the camera is in the world, the scale what is far
off is kept at (`far_shift`), the shift a depth takes into the ordering
table, and the near cutoff. The projection's own distance and screen
offsets are read from the coprocessor there, because the tiny model and
the menus change them later in the frame. The card then works out, for
each vertex, exactly what the GTE worked out:

    v      = M * (point - camera) / 4096 / 2^far_shift, its y halved
    screen = h * v.xy / v.z + the screen's middle

written so that every component of `gl_Position` is linear in the point,
which is what lets the card cut a face at the near plane where a straight
line says.

**What is left out, as the console leaves it out.** A face no corner of
which is farther than `RENDER_OT_CUTOFF` is the ground between the camera
and the fight, and the renderer drops it; the mesh carries each face as a
ball -- its middle and how far its corners reach -- and leaves out the
whole face by the same rule. (A test of each pixel's own depth was tried
and cuts into a face the game drew whole: the ground at the camera's feet
went with it.)

**The fog.** The stage's fog is not a shader on the console: it is 31
rows of colour tables, each blended further towards the fog's colour,
copied over the row the textures read at even steps of depth through the
frame ("How far the stage is drawn"). One mesh drawn at one moment can
have only one of those rows, which would be the whole ground in the fog's
colour. So the mesh **reads the row the fog would have had for that
pixel's own depth**: every level is still in video memory, which is what
the card reads texels from, and the side channel carries the
ordering-table slot each level's copy was linked at this frame
(`port_RemasterNoteFogLevel`). The level a depth is fogged by is the first
whose slot is farther than its own, which the shader finds by halving.
That is the game's own fog, to the colour, and by the pixel instead of by
the face.

**Depth.** The mesh is sorted against itself by a depth buffer of its own,
attached to a framebuffer of its own over the same colour buffer; nothing
else of the picture has a depth and nothing else is tested against it. The
mesh is drawn where the first of the frame's tile primitives would have
been, which is the farthest of them, so the fighters, their shadows, the
effects and the stage's pieces are all painted over it in the order the
ordering table has them.

**The ground is drawn from both sides.** The renderer leaves out a face
the camera sees from behind (`render_FaceFront`), but it asks that of a
quad's first three corners and then draws both of the quad's triangles,
which wind opposite ways. Telling the card to leave out the back of a
triangle took half of a stage's walls away with it, so it is not told to:
the depth buffer settles which face a pixel belongs to, and what is drawn
that the console did not is the underside of a face with nothing in front
of it.

**A tile the game opens up.** A tile that can hide a fighter from the
camera is drawn half-transparent while it is in the way
(`STAGE_TILE_OPEN`). The mesh holds those tiles' faces last and draws
them in a second pass with no depth written, once for each of the chip's
four mixes that any of them asks for; which tiles are open is this
frame's, read from `draw_flags` where the mesh is drawn. With no tile
open -- which is most frames of most stages -- there is one draw and
nothing else.

**The pictures between two frames.** A tile's primitive is no longer
written down at all (`gl_tween.c`): there is nothing to pair, and they
were thousands of a frame's. In their place the mesh is drawn again at
the event the first of them came at, **with the camera between the two
frames** -- the rotation, the place, the projection distance and the
screen offsets each a part of the way along. That is the whole of what
moves in the ground, so the holes a real card showed in the floor of a
picture between two frames cannot happen: there are no tiles to pair.

**What falls back to the tiles**, so that nothing is drawn wrongly where
anything is uncertain: the setting off; the original picture; the
comparison with the software GPU (`--gl-exact`, which wants the game's
own primitives); a stage with more tiles than the mesh holds; a stage
whose tiles or meshes have moved and not been read again (the mesh is
built once more); and every frame before the first in which the renderer
drew a tile at all. Nothing of the stage but its ground is the mesh's:
the pieces that stand on it, the surf, the flames, the backdrop and the
scrolling textures are the game's own nodes and primitives and are drawn
exactly as they were. The stages whose ground changes are none: the
hooks a stage has of its own (`stage_hooks.c`) move props, scroll a
texture in video memory or draw a backdrop, and the surf is a node; the
one thing that moves a tile's mesh is the arena moving the stage's block
(`resource_OnStageMoved`), which says so.

**What may still be wrong.**

- **The ground is painted where the farthest of the frame's tiles was**,
  so a piece of ground that stands between the camera and a fighter no
  longer veils him; the game paints such a tile after him, at its own
  depth. The one case the game itself cares about is a tile it opens up
  for exactly that reason, and that one is left to the game's own
  primitive (above); a step or a wall of ground that is not a tile that
  can be opened is not. Nothing of the kind was seen in the 32 stages
  measured, and a stage where it showed would want the mesh's depth
  buffer tested by the rest of the scene, which is a change to every
  primitive and not to this one.
- **A face is drawn from both sides**, where the console drew it from
  one (above).
- **The ground near the camera is cut by a ball round each face** and
  not by the face's own farthest corner, so a little more of it is drawn
  than the console drew.
- **A picture between two frames reads the tiles as they stand**, not as
  the frame on show had them, so a tile the game opened up in the
  meantime is left out of the mesh a picture early. It is one tile of a
  stage for one picture.
- **Nobody has seen it on a screen**, and nothing of it has run on a
  real graphics card: the pictures are files, drawn by Mesa's software
  renderer. That is why `stage_mesh` is off with nothing said.
- The practice stages and the modes' own (32 to 37) are not among the
  32 measured: a versus fight cannot be set on them.

**How it is proved** (`tools/port_stage_prove.py`, which needs a graphics
card and so is no check of CI's): each of the game's 32 fight stages is
run twice over the same tour -- the first fighter run across the stage,
which swings the camera right round -- once with the mesh off and once
with it on, and the two runs' traces and pictures are held to each other.
At `draw_distance max`, the console's shape, twice its resolution, five
pictures of each run (`build/stagemesh/measured.tsv`):

| | |
|---|---|
| Stages | 32 |
| **The trace of every watched value, the mesh on and off** | **the same on all 32** |
| Pictures compared | 160, 196,608,000 pixels |
| Pixels that differ at all | 35,094,050 (17.9%) |
| ... by more than a twentieth of a channel | 9,953,693 (5.1%) |
| **Black where the tiles drew the stage** | **104,540 (0.053%)**; none at all on four stages |
| Drawn where the tiles drew nothing | 852,697 (0.43%) |
| Vertices a frame, the mean over a run | 2,545 from the tiles, 3,094 with the mesh |

**What the differences are.** Not holes: the ground is in both pictures,
and the pixels that differ are the texels of it. The mesh's corners are
worked out in floating point from the full-precision view, where the
console's are the coprocessor's sixteen-bit arithmetic with its
truncations and its reciprocal, so the two part by a fraction of a pixel;
on a ground whose texture is noise (grass, gravel, rubble) that fraction
is enough to read the next texel along, and the count runs to a fifth of
the picture. On a ground of smooth boards it is a twentieth: the castle
rooftop differs in 4.4% of its pixels and shows no black at all, where
the abandoned shipyard's rubble differs in 32%. The side-by-side
pictures are what to look at
(`build/stagemesh/pictures/*-tiles-and-mesh.png`, six stages at 2,560 by
1,369, the owner's own window size, drawn through SDL's `offscreen`
driver on Mesa's software renderer).

The 0.43% drawn where the tiles drew nothing is the gain: the cells and
the faces the console's wedge, its near cutoff and its screen tests left
out. The railway stage gains 521,365 pixels of ground over five
pictures, the snake cliff 510,490.

**The pictures between two frames**, on the shrine with
`--gl-tween 4` (four pictures a frame): the trace is the same with the
mesh on and off, every picture lies between its two frames
(`out_of_order 0`), and a picture drawn at phase 0 -- which must be the
game's own frame again -- differs in 3,014,067 pixels of 769,228,800
(0.39%), where with the tiles it differs in none. (That 0.39% is the
same sub-pixel reading of the ground as above, and which tiles the game
had opened up, which the replay reads as it stands rather than as the
frame on show had it.) The primitives a frame has to pair fall from 889
to 823: the ground is no longer among them.

**What it costs.** On this machine, which has no graphics card at all
(Mesa's llvmpipe) and was busy with other work throughout, a run of
4,200 blanks over the 32 stages took 30.9 s with the tiles and 32.3 s
with the mesh, and the mesh adds about 550 vertices to a frame. It is
drawn in one go: one draw of the whole stage, with no blending.

### Edges

The scene is drawn **two or four times over each way and averaged
down** (`antialiasing`): the colour buffer is `scale * sample` pixels to
a pixel of video memory, and a pass at the end of the frame makes the
picture by averaging each block of `sample` by `sample`
(`port_GlResolve`, `src/port/gl/gl_draw.c`). With `off` there is no
second pass at all and the picture is read from the colour buffer
itself, so the first step's proof at scale 1 is of the same pixels it
always was.

**Why this and not the usual two.** The step before left a warning: the
game's primitives are painted in the order of the ordering table and
many of them are half-transparent, so anything that mixes coverage with
colour mixes twice.

- *Multisampling* keeps one colour and several coverage masks a pixel.
  Where a half-transparent primitive is painted over an edge that is
  already part-covered, the coverage is folded in once when the edge is
  resolved and again when the primitive is mixed: the edge shows
  through what should hide it. There is no order to draw in that avoids
  it, because the game's order is the ordering table's.
- *A filter over the finished picture* (FXAA and its like) is the
  cheapest, and it cannot tell the scene from the 2D layer: it would
  soften the HUD, the fonts and the menus, which are the one thing in
  this picture that is exactly right. Drawing the 2D layer after it
  would mean telling the two apart in the stream of primitives, which
  the side channel can do (a primitive the projection gave a depth to is
  the scene) — but then the filter would still work on a picture whose
  texels are blocks, and guess edges that are texture and not geometry.
- *Drawing it more times over* is exact with any order of painting,
  because every mix happens at the finer resolution and the averaging
  comes after all of it. And it leaves the 2D layer exactly where it
  was: a pixel of video memory is a block of `scale * sample` pixels of
  one colour, every block of `sample` averaged lies inside one of
  those, and the averaging is done in whole numbers.

**Measured.** A flat picture the game draws (the top menu, 1,228,800
pixels at 2 by 4) against the same picture with no smoothing: **7
pixels differ at `2` and 7 at `4`**, the worst channel by 12 and 31.
(Three times over moves 8,983 of them, which is why the setting is held
to a power of two; the reason was looked for and not found.) A sword's
edge, a fighter against the sky and the HUD's letters, cropped and
enlarged: `build/remaster2/step2/sword.png`, `fighter.png`, `hud.png`,
each with no smoothing, 2 and 4 side by side.

**What a frame costs**, on this machine with no graphics card at all
(Mesa's llvmpipe), the hardware path alone over a whole run at 2 across
and 4 down:

| `antialiasing` | The colour buffer | A frame |
|---|---|---|
| `off` | 2,048 by 2,048 | 1.27 ms |
| `2` | 4,096 by 4,096 | 5.80 ms |
| `4` | 8,192 by 8,192 | 23.0 ms |

Four times over is sixteen times the pixels and costs eighteen times as
much, which says the cost is fill rate and nothing else. A real card
fills at least a hundred times faster than llvmpipe does, so `2` at a
1080p window is of the order of a tenth of a millisecond there and `4`
half of one; the colour buffer is 64 MB at `2` and 256 MB at `4`, which
is the number to watch on a small card, and it is all of video memory's
1024 by 512 at the scale, most of which is never shown. **Default:
`off` until someone has looked at it on a screen**; `2` is what the
measurements say to take.

### A wider picture

`aspect 16:9` makes the picture **a wider view of the same scene, not a
stretched one**: the vertical field of view is the console's and the
horizontal grows. Nothing of the game's projection changes — no screen
offset, no projection distance, no clip — for the simple reason that
**the game never culls to the right or the bottom of its screen**: a
face is dropped only when every corner is off the left or above the top
(`render_FaceOffScreen`, the sign bits of the packed position), and a
stage tile also when no corner is above line 240. Everything to the
right is already projected and already emitted; the console's GPU
throws it away at the edge of its 640-pixel drawing area, and the
remastered renderer, which has every corner where the projection really
put it, need only keep it.

**The left is the one edge the game does cull at**, and that is in the
drawing code and not in the projection: a face every corner of which has
a negative screen x is dropped before a primitive is ever built, so a
wider picture showed black wedges in the new left band where the ground
and a wall ended at the console's edge. Under the remaster's own switch
the test is against the picture's own edge instead, which is
`port_RemasterLeftMargin()` pixels further out: the margin is worked out
once a frame where the game clears its ordering table
(`port_RemasterNewFrame`), so that every face of a frame is cut at the
same place, and it is 0 with the original picture or the console's shape,
where this is the console's own test to the instruction. A face is looked
at thousands of times a frame, so the test reads a cached number and
calls nothing. The tiles go through the same test, so one change covers
the stage, the fighters, the cloth and the effects; the tiles' own extra
test is the bottom of the screen, which a wider picture does not
touch.

So the whole of it is in the drawing path:

- the picture is `port_RemasterMargin(640, 16, 9)` = 106 pixels of video
  memory wider on each side, 852 in all, which still fits across video
  memory's 1024 — the colour buffer the renderer draws into;
- everything the game draws is placed 106 pixels to the right, so that
  the console's own 640 sit in the middle of it;
- a primitive the projection gave a depth to — the scene — is drawn with
  the drawing area widened to the whole picture, where the game was not
  restricting that area to part of the screen (the pause menu, the
  replay's panels), and so is a group of the fight's HUD the game
  anchored to an edge ("The HUD's own edges"); the rest of the 2D layer
  keeps the console's own area and stays four by three in the middle;
- the two bands beside the console's picture are cleared where the game
  clears its own drawing area, which is the moment that matters: the
  game draws its frame after the picture of the frame before it has been
  read (`DrawOTag` comes past the vertical blank in its loop), so a
  clear at the top of a frame wipes the bands of the picture about to be
  read;
- what the game wrote into video memory — a fill, a copy, the loading
  screens, the portraits — is stretched into the console's 640 alone and
  not into the bands, which are the scene's; a plain rectangle as wide
  as the console's picture is a fade or a page of one colour and is
  widened to cover all of it.

**Measured.** The middle 640 pixels of a 16 by 9 picture against the
same frame at four by three: **25 pixels of 1,228,800 differ**, all of
them where a polygon now carries on past the old edge instead of being
cut at it. (Measured again with the HUD at the edges, on three frames of
a fight at scale 2: 15,308, 16,470 and 17,096 pixels of the middle
differ, which is the HUD leaving it; **under the HUD's own rows, 24, 21
and 18 do** — the same polygons as before. Outside a fight the middle is
what it was: the menus and character select differ in 0 to 93 pixels.)

**Draw the stage further with it.** The stage's cells are chosen by a
wedge cut for the console's field of view, so a wider view at
`draw_distance original` has nothing to show at the sides past about 15
cells. `max` is what fills a wide picture, and the two settings are
meant to be used together.

**The HUD's own edges.** The fight's HUD is built at the console's places
— the WINS counters (`COUNTER_LEFT_X` 80, `COUNTER_RIGHT_X` 521 in
`hud.c`), the chanbara clock (`TIME_MINUTES_X` 418 to `TIME_RULE_END_X`
568), the team battle's points and warnings — which sit in the middle
band of a wider picture. Each group of it is drawn at the picture's own
edge instead:

- **the game says which edge a group belongs to, and nothing more**
  (`port_RemasterSetAnchor`, `PORT_ANCHOR_LEFT` and `PORT_ANCHOR_RIGHT`):
  the primitives themselves are built exactly where they always were, so
  nothing the game computes changes and the picture the software GPU
  draws into video memory is the same to the pixel. "Graphics: Original"
  is untouched by all of this, and so is every test that holds video
  memory to the emulator's;
- a sprite says so where it is linked (`sprite_Draw`, `select.c`) and a
  tile at the few places in `hud.c` that build one, each under
  `RB_REMASTER_VIEW`: two macros that are nothing at all on a
  PlayStation (`HUD_ANCHOR`, `HUD_ANCHOR_PRIM`). The groups are those of
  `hud_DrawWinCounts` (and `hud_DrawOpponentCount` through it),
  `hud_DrawTimeAndCount` and `hud_DrawPointsAndTime`; each puts the
  anchor back to the middle when it has built its own, and a new frame
  clears it;
- the side channel keeps those primitives by address, as it keeps a face
  (a third open table of 256 slots, two frames deep), and the decoder
  hands the backend each primitive's anchor. Only a rectangle — a sprite
  or a tile — is looked up, which is all the HUD is, so the scene's
  thousands of polygons a frame cost nothing;
- the backend draws an anchored primitive a margin further out
  (`port_GlPutVertex`) and widens its drawing area to the whole picture,
  which the scene already had (`port_GlDraw`, `state.is_wide`). Every
  other flat primitive keeps the console's own area: the pause menu, the
  menus, character select, the loading pictures, the captions, the team
  battle's table and the kill count are the other callers of the same
  sprite code, they say nothing, and they stay four by three in the
  middle of a pillarboxed picture.

Where the console's picture is shown — the original picture, or the
console's shape — the margin is 0 and every anchor is a shift of nothing.
**The default shape of the remastered picture in the window is `16:9`**
(`PORT_ASPECT_W_WIDE`, `src/port/window/keymap.c`); the builds with no
window keep the console's shape with nothing said, so that their picture
can still be held to the software GPU's pixel for pixel
(`tools/port_gl_prove.py`).

**How the HUD's edges were proved.** The program as it was before this
and the program after it, on `port_game`'s plan with no card in either
slot:

| | Before against after |
|---|---|
| With "Graphics: Original": the trace of every watched value at every blank | identical, 2,333 lines |
| ... and all of video memory at the end of the run | identical, all 1,048,576 bytes |
| With the remastered picture at the console's shape: the trace | identical, 2,333 lines |
| ... and every picture of the run at scale 2 (1,228,800 pixels each) | identical, all 35, no pixel different |

And the renderer's own prover (`tools/port_gl_prove.py`) on the three
plans: the game computes the same thing with the remastered picture on
and off, 2,333, 2,457 and 2,334 lines, `port_bugfixes` among them (the
card image it used to read is gone; see "What is left").

### Remastered in the window

The window the player runs shows either picture, and **F2 changes between
them while the game runs** (`src/port/window/sdl_window.c`). What the
prover does with EGL and no display, the window does with SDL:

- the window is made with `SDL_WINDOW_OPENGL` and a 3.3 core context is
  asked for (`SDL_GL_SetAttribute`, `SDL_GL_CreateContext`), and
  `port_GlStart(SDL_GL_GetProcAddress, &options)` hands the renderer the
  context. Nothing of OpenGL is linked to: `gl_draw.c` looks every
  function up through SDL, so the program needs no GL library at build
  time and runs where there is none;
- at a blank's end `port_RemasterShow()` and `port_GlFinishFrame()`
  finish the card's frame, and then the picture goes into the window:
  `port_GlShowPicture`, one textured quad of the colour buffer into the
  rectangle `port_FitShape` gives it, and `SDL_GL_SwapWindow`. The
  picture's pixels are square and it is as wide as the view asks for, so
  its own shape and not the console's four by three is what it is fitted
  by;
- with Graphics Original the same path shows the software GPU's own
  pixels as a texture (`port_GlShowPixels`), so that one context draws
  both pictures and F2 costs nothing but a different source;
- the counter in the corner (F3) is its own few pixels over either
  picture, by the same call with the box's alpha kept; the session's log
  is the window's as it was;
- **F12 keeps a picture of the window** as the player sees it: the
  window's own pixels read back (`port_GlReadWindow`, or SDL's renderer
  where there is no card) and written as a PNG into a `screenshots/`
  folder beside the program, named for the date, the time of day and a
  number, with a line of the log saying where it went
  (`port_KeepShot`, `port_WriteShot`, `port_WritePixelsPng`;
  `PLAYING.md`). It is **the picture that is shown**, bands and all:
  the last of the blank it was pressed in, which where more pictures a
  second are drawn than the game has frames is one of those between two
  frames. (It was the game's own frame until now, which is why the
  owner could not catch the holes in the floor with it: they were never
  in a frame of the game's own.) `--shot-at N[,P]` does the same at one
  blank, for a test, since no plan can press a key that is no pad's
  button, and `P` names which picture of that blank to keep -- 0 the
  game's own frame, and -1 the last shown. Asked for a picture that
  comes after the blank has time for, it is drawn all the same: this
  machine's software OpenGL has no time in a blank for a second
  picture, and this is how the key was proved to keep one (a window of
  854 by 480, `--frame-rate 120`, `--shot-at 3250,2`: "a picture
  between two frames"); `--window WxH` opens the window at a size this
  machine's screen may not have, which is how a player's 2,560 by 1,369
  is reproduced here.

**Where there is no OpenGL 3.3** — an old card, or SDL told to show
nothing — the window is made again without a context and SDL's renderer
shows the software GPU's picture, exactly as it did before there was a
card path: the remastered renderer has no backend then, so
`port_RemasterIsOn` is false whatever F2 or a setting says, and the log
says why. This is the path every headless run takes (`SDL_VIDEODRIVER=dummy`
has no GL at all), which is why the window's own proof is untouched by
all of this.

**The settings** are the window's, read from `rebushido.cfg` and the
command line as the others are (`keymap.c`, `PLAYING.md`): `graphics`
(`--graphics original|remastered`), `resolution` (`--resolution N`),
`draw_distance`, `antialiasing`, `aspect`, `stage_mesh`
(`--stage-mesh on|off`, "The ground as one mesh") and `frame_rate`
(`--frame-rate original|display|N`, "More pictures a second").
`draw_distance`, `antialiasing` and `aspect` are a run's
and are settled before the game starts, because they choose the
primitive buffers the game is handed; `graphics` and the window's size
may change as it runs. With nothing said: the picture follows the
options page, the scene is drawn at as many times the console's
resolution as the window's size asks for (`port_RemasterScaleFor`, which
is recomputed when the window is resized or the game changes its display
mode), the draw distance is `max`, the smoothing is 2, the shape is
`16:9`, the wider view of the same scene with the fight's HUD at its
edges ("A wider picture"), and the ground is drawn from the game's own
tiles and not from a mesh. With "Graphics: Original" the window shows the
console's own four by three whatever the shape says.

**What the log says**: the card's vendor, renderer and GL version; which
picture a run starts with and all five settings; every F2 with the blank
it happened at; the scale each time the window's size changes it; and at
the end which picture was on, how many times F2 was pressed, the
vertices and draws of the last frame, and — where more pictures a second
were asked for — how many pictures were shown, how many of them were
between the game's frames, and how many could not be drawn.

**How it was proved.** With Graphics Original, SDL's dummy drivers and
the three plans of `port_game`, `port_other_fight` and `port_bugfixes`:
the trace of every watched value is the build without a window's, line
for line (2,333, 2,457 and 2,334 lines), and every twentieth picture as
SDL is handed it is that build's PNG pixel for pixel — 685 pictures,
94,556,160 pixels, none different, none missing. That is "How the window
is proved" again, on this tree, and it is the path a machine with no
OpenGL takes as well, since SDL's dummy driver has none. With Graphics Remastered the window was run through
SDL's `offscreen` driver on Mesa's software renderer (`llvmpipe`), which
gives a real 3.3 core context with nothing on screen: the card path
starts (OpenGL 4.5 core, Mesa 25.2, llvmpipe), the scale follows the
window (3 at 960 by 720 where the game's display is 320 wide, 2 where it
is 640), the picture changes at a blank, and the window's own pixels
were read back and written as PNGs (`--tap-window`). A fight at 16:9 and
`draw_distance max` drew about 31 of the game's frames a second on that
software renderer, which is the console's own rate.

**The wider picture's left band, measured.** The same frame of the same
fight at 16:9, with the face test against the picture's edge and against
the console's: of the two bands' 171,360 pixels 3,869 differ, and 3,505
of those went from black to drawn — the wedges where the ground and the
far wall stopped. 152 pixels of the middle differ too, where a face that
was dropped altogether reaches back into the console's own 640: a wider
picture's middle is no longer the four-by-three picture to the pixel,
which is the price of filling the band and is only paid when the picture
is widened.

**What a real card showed.** The first run on one (an NVIDIA RTX 3090,
OpenGL 3.3, a window of 2,560 by 1,369 on a screen of 2,560 by 1,440)
found the menus full of black lines that are not in the game: the sample
point of a flat primitive, which had been right only at the console's
own resolution ("What the card draws into"). It is mended, and
`--gl-blocks` is the measure that holds it mended. The same run was
reproduced here with `--window 2560x1369` through SDL's `offscreen`
driver, which is what that option is for: the window opens at a size
this machine's screen has not, and `--shot-at N` keeps its pixels.

The same card's second run found what the menus had hidden: **in a
fight, pieces of the floor went missing for a picture at a time**,
leaving strips and rectangles of the backdrop's colour beside the
painted lines, in another place each time, and the fighters' shadows
juddered against a scene that was otherwise smooth. Neither could be
caught with F12, which is what said where to look: the pictures
between two frames (below, "Which primitive of one frame is which of
the next"). A recording of 150 frames had the holes on 30 of them.

**Not proved**: everything but that one session is still Mesa's
software renderer with no window on screen, so how full screen behaves
and whether a card keeps up are known for one machine and no other. The
counter and the fade of a resize were read from the pixels, not seen.

What the window still needs of the remastered picture is new lighting
(below).

### More pictures a second

**The game's own rate, which does not change.** `main` waits
`g_sys_frame_period` fields a frame and counts `g_disp_frame_count`: 2
fields in a fight (30 frames a second) and 1 in the menus, the character
select and the options page (60 — the console's 59.83). Everything the
fight computes is counted in frames, so the game has to go on stepping
at exactly that rate and computing exactly what it always did: a higher
frame rate here is **more pictures of the same frames**, never more
steps. A frame the game skips (`g_sys_skip_frames`, which a long disc
read sets) is not drawn and not counted on: the frame before it stays on
the display, and nothing of this is different for it — there is simply
no new frame to move towards.

**Which two frames a picture lies between.** The game draws frame F into
one half of video memory and puts the *other* half, which holds frame
F-1, on the display (`g_disp_db[i].draw` is half `i` and
`g_disp_db[i].disp` is half `1-i`, `src/main/main.c`). So at the blank
where frame F's primitives have just been handed to the GPU, what the
screen shows is frame F-1, and frame F is **already worked out**. A
picture between the two is therefore behind nothing and ahead of
nothing: at the start of frame F-1's time on the display the picture is
frame F-1 exactly, which is what the console shows then, and by the end
of it the picture is frame F, which is what the console puts up at that
instant. **There is no added latency** — a picture is never later than
the console's own, and nothing is extrapolated.

**How a frame is drawn again** (`src/port/gl/gl_tween.c`). The renderer
that draws between frames puts itself in front of the one that draws
(`port_RemasterGetBackend`), so it sees every primitive the decoder
makes and every rectangle of video memory a frame writes, and hands each
on unchanged. Of each frame it writes down:

- every decoded primitive, in the order the game drew it;
- every write of video memory **with the pixels as they were**: a frame
  drawn again needs the screen its own clear left, not the finished one
  the software GPU has since drawn over it. Video memory is put back
  before anything else looks at it, and the mirror the card reads texels
  from is sent the true pixels again (`port_GlUploadVram`);
- three frames are kept: the frame on show, the frame after it, and the
  one the game is building now.

A picture at phase *t* is then the frame on show's own list of writes
and primitives given to the renderer again, with each primitive that the
frame after it has as well moved *t* of the way there. **A primitive
with no depth from the side channel is drawn exactly where it was**: the
2D layer, the HUD, the clock, the fonts and the menus stay at the game's
own rate, which is where they belong.

**How a corner moves.** Not in a straight line on the screen: a point
coming towards the camera does not. With `za`, `zb` the two depths and
`xa`, `xb` the two screen positions, the place the projection would have
given a point moving evenly in view space is

    x = ((1-t)·za·xa + t·zb·xb) / ((1-t)·za + t·zb),

and the screen's own centre, which the projection added to both, falls
out of that sum: so nothing of the projection's own numbers has to be
carried. The depth moves evenly, and so do the texture point and the
colour (the light on a vertex changes as it turns). The drawing offset
is taken off both corners first and the frame on show's put back, which
is what makes the two halves of video memory comparable. The whole-pixel
corner, which is what the chip's rule about what it will not draw is
applied to, is moved by the same step rather than taken from the moved
place, so that at the first phase the picture is the game's own to the
pixel.

**Which primitive of one frame is which of the next** was the hard
part, and is now a look-up. **Every primitive of the scene says which
face of which thing it is a drawing of** (`PortRemasterIdentity`,
`port_remaster.h`), in four parts:

- *the thing*: the address of the node of the scene or the tile of the
  stage the renderer is drawing (`port_RemasterNoteObject`, called in
  `render_DrawQueue` and in `render_DrawStageTiles`). Both are elements
  of arrays that do not move while a fight runs;
- *the mesh*: which part of that thing is being walked
  (`port_RemasterNoteGroup`, from `render_LoadFace` and the stage's
  tile loop), which tells two parts of one model apart;
- *the face*: the corners of the mesh that face is made of, packed as
  the mesh itself has them (`port_RemasterNoteFace`, which every
  emitter already called to find its points). It is the face's own name
  and not a count of the faces before it, so a face the camera turned
  away from in one frame and not in the other moves nothing else's
  name;
- *the piece*: which piece of that face this primitive is, where a face
  becomes several — a tile cut four or sixteen ways, a face cut by the
  screen's edge. The pieces are made in an order of the renderer's own
  and numbered as they are linked (`port_RemasterAttachFace`, which a
  tile's primitives reach through `render_ChainTile`: they are chained
  into the ordering table by hand and never pass `render_LinkEntry`).

The same face of the same thing has the same name in the next frame
however far it moved, however the camera turned, and however the walk
about it changed; and no two primitives of one frame can share one. So
the pairing is a table look-up, **exact and one-to-one by
construction**, and a camera swinging right round is no harder than a
camera standing still. On the practice stage (stage 35, the one the
owner played) with the camera swung right round it names 669 of the
752 primitives of a frame that can be moved at all, and never two of
them the same. (Naming a face by its
place in the order the faces are drawn, rather than by the corners it
is made of, named 157 of the same 752: one face the camera turns away
from moves every name after it. That is the whole reason the name is
the corners.)

What has no name at all — drawing the renderer's walk knows nothing of
— is still looked for by what it is painted out of and where it was:
the nearest of the same signature (the kind of primitive, its texture
page, its colour table, how it is painted and the texture point of
every corner) in the nine squares of 128 pixels about the one it was
in, and never one another primitive has taken already. A primitive that
*has* a name and whose name the next frame has not is left where it is:
nothing else may be taken for it.

**Why likeness was not enough.** Faces of one texture — which is what a
stage's ground is made of, every tile alike, and a fighter's sword and
a shadow too — are told apart only by where they are, and under a
turning camera "where they are" moves further than a search can look.
It paired 699 of the 752 and **11 of them with the wrong primitive in
every frame** (`clashes`): one face drawn stretched from its own place
to another's, and its own place left showing whatever was behind it.
That is what the owner saw on a real card — strips and rectangles of
the backdrop beside the painted lines of the dojo floor, in another
place each frame, on 30 of the 150 frames of a recording, and the
fighters' weapons and shadows shaking where nothing had moved — and
never in a picture of the game's own frame, which is why F12 could not
catch it. On the practice stage with the camera swung round, the
middle picture of a frame had 1,011 pixels changed that both ends of
it agreed about; named, 448 ("How it is proved").

**A picture between two frames holds every primitive the frame on show
held.** The chip draws no triangle spanning more than 1023 pixels
across or 511 down and none of no area, and the backend leaves out the
same ones by the same whole-pixel rule (`port_GlWouldDraw`): a corner
moved part of the way can fall outside that where the frame on show's
did not. So a primitive that would not be drawn moved is **drawn where
it is** (`held`: 15 of a frame's 752 on the practice stage, where the
two places of a face at the edge of the view are more than a screen
apart). The prover holds two of these exactly, every picture: the same
count of primitives, and the same triangles left out by the chip's
rule. The third, what changed that both ends of the frame agree about,
it counts and prints ("How it is proved").

Carrying such a face, and one the next frame does not hold at all, the
way the thing it belongs to went -- the average of the faces of it
that were paired -- was tried and **measured worse**: a face with no
partner is usually one that is going, and moving it uncovers what it
was covering. It is drawn where it is.

**What is not moved, and what then happens.** A primitive the frame
after it has not (something spawned or gone), one whose corners the
projection put off the eleven bits the chip holds, one the projection
gave no place to at all, or one with no name that no search found, is
drawn where the frame on show had it: so it waits for the next frame
instead of moving. **A depth is not asked for**, only a place
(`has_place`, `port_remaster.h`): a polygon the renderer flattens --
a fighter's shadow is projected with a matrix that flattens it onto
the ground -- would be moved between its two places all the same, with
its texture run evenly across it as the console's is. In the practice
stage measured here no primitive came out that way: every point the
GTE wrote down had a depth with it, shadows and all
(`with_place` is `with_depth` to the primitive). So the shadows were
already moved, and the shimmer the owner saw in them was the same
mispairing as the floor's: a shadow is many small faces of one
texture, which is exactly what a search by likeness confuses.

And when the faces that **cannot be paired are more than a quarter as
many as those that can** (`PORT_TWEEN_CUT_SHARE`) the two frames are
not one scene a frame apart — a camera cut, a screen that changed, a
model replaced — and no picture is drawn at all: the window shows the
game's own, which is exactly right. In `port_game` that is every blank
of the menus and the loading screens, where there is nothing with a
place to move, and the game runs at 60 there anyway.

**The window's own path** was run headless as well, through SDL's
`offscreen` driver: at `--frame-rate 120`, with the scene at the
console's own resolution so that a blank has time left in it on a
software renderer, a run drew 59 pictures between the game's frames and
1,272 blanks where the two frames were unlike. At a larger scale
llvmpipe is slower than the blank itself and there is no time left to
draw one in, which is why the measurements above are the prover's and
not the window's.

**Presentation** (`src/port/window/sdl_window.c`). The setting is
`frame_rate = original | display | N` (`--frame-rate`), `original` with
nothing said. With `original` the window draws one picture a vertical
blank, waits for the blank's own moment and swaps, exactly as it did.
Otherwise it draws a picture every slot of the rate asked for until less
than half a slot of the blank is left; with the display waited for, the
swap itself is the wait. **The blank is not over until its own moment**
whatever happened in it — however many pictures were shown, and whether
or not any lay between two frames — so the game keeps the console's
59.83 blanks a second as before, and the pace takes up what a swap ran
over (`pace.c`). (Leaving that last wait out was the one thing that
showed as the game running too fast: in a menu, where there is nothing
with a place to move and no picture between frames is drawn, the blank
ended as soon as its one picture was shown and the game ran at the
display's rate.) `display` takes the rate `SDL_GetCurrentDisplayMode` says, and 60
where it says none. Eight pictures a blank is the most, whatever is
asked for. The counter in the corner shows both rates and the game's own
speed: `143.8/30.0 100%` is 143.8 pictures a second of a game stepping
30 times a second and keeping the console's time.

**How it is proved, with nothing shown**
(`tools/port_fps_prove.py`, `--gl-tween N`). Each game frame of each of
the three plans is drawn as N pictures, the first the frame on show and
the last the frame after it, and three things are held:

- **the first picture is the game's own, pixel for pixel.** The frame on
  show drawn again with nothing moved must come out as the picture the
  software GPU and the card already made. This is what proves the whole
  of the writing-down and the drawing again: the list of primitives, the
  pixels of video memory put back, the order of the batches;
- **every picture in between lies between the two.** Every channel of
  every pixel of it against the first picture and against the last: the
  distance from the first must not fall and the distance from the last
  must not rise, from phase to phase;
- **the point the renderer watched** — the corner that moved farthest,
  the same primitive at every phase — lies between its own two ends;
- **every corner of every primitive lies between its own two ends**
  (`off_the_line`, counted as each picture is drawn: a pixel either
  side is allowed for the rounding of the two). This is the one that a
  partner wrongly found shows in, since such a corner is pulled towards
  a place that is not its own. It was 0 in 5,744 pictures of a fight
  whose camera swung right round;
- **every picture of the frame draws every primitive the frame on show
  drew** (`fewer_prims`), and leaves out exactly the triangles the
  chip's own rule left out of it (`more_dropped`): one more left out is
  a hole where that face was;
- **what both ends of the frame agree about, the picture between them
  draws the same** (`holes`, `hole_pixels`). This is the symptom
  itself, measured: a pixel of the floor a long way from anything that
  moved is the same in the frame on show and in the frame after it, and
  the one thing that can change it in between is a face that was not
  drawn where it belongs. Asking the ends to agree first is what keeps
  honest motion -- a fighter's edge sweeping over the floor -- out of
  the count. This one is a number watched and not a gate: a face the
  next frame has not is left where it is while its neighbours move, and
  the edge between them is a few pixels of it. On the practice stage
  with the camera swung right round, 426,687 such pixels over 531
  pictures became 191,819 over 522 when the primitives were named, and
  the pictures with more than half a thousandth of themselves changed
  that way, 207, became 35. The share is the same at the console's own
  size and at the owner's 2,556 by 1,440 (25,571 pixels and 427,313,
  35 pictures either way), and `fewer_prims` and `more_dropped` are 0
  at all three;
- **a scene that did not move at all is drawn exactly as it is**
  (`still`, `still_moved`): where the farthest corner moved 0 pixels
  between the two frames, every picture in between must be the frame on
  show, pixel for pixel. A fight paused with Start, where nothing of the
  scene moves, gave `still 1 still_moved 0`.

And the game computes the same thing with the pictures on and off: the
trace of every watched value at every blank is identical, line for line,
for both pointer sizes (`port_game`: 2,342 lines at 64 bits and at 32;
`port_other_fight`: 2,466). Nothing of this is in `make port` or in any
check: `src/port/gl/` is the window's and the prover's alone.

**What it measured**, with every game frame drawn as five pictures at
twice the console's resolution (1,280 by 960):

| The plan | Frames | Drawn again | Pictures | The first against the game's own | Out of order | The point off the line | A picture |
|---|---|---|---|---|---|---|---|
| `port_game` | 2,752 | 696 | 5,568 | **0 of 855,244,800 pixels** | 0 | 0 | 22.0 ms |
| `port_other_fight` | 2,872 | 816 | 6,528 | **0 of 1,002,700,800** | 1 | 0 | 24.4 ms |
| `port_bugfixes` | 2,784 | 696 | 5,568 | **0 of 855,244,800** | 0 | 0 | 24.2 ms |

A picture of `port_game` is 1,201 primitives: 1,102 moved, 79 the frame
after it has not, 18 flat (the 2D layer and the HUD). The farthest a
corner moved was 128 pixels, which is the most that is moved at all.
1,645 frames were not drawn again — the boot, the movie, the menus and
the loading screens, where nothing has depth to move — and 6 were too
large to write down. Over the whole run a picture cost 40.6 ms of the
program's own time against 22.0 ms inside the renderer, on Mesa's
software renderer with **no graphics card at all**; the runs took 271
seconds against 45 with the pictures off.

**The one picture out of order** is `port_other_fight`'s blank 3,206,
where the camera swings fastest. Its first phase is farther from the
last picture than the phase before it, by the sum of every channel of
every pixel — and that is a weakness of the measure, not of the
geometry: where a textured scene slides across the screen, a shift of a
quarter of the way can line fewer texels up than a shift of none. The
watched point is in order at every phase of it, and so is the distance
from the *first* picture.

**`port_bugfixes`' trace parts company at the options page**, as it does
for anything that moves the program's memory about: the page reads
memory the game never wrote ("What is left"), the Bugfixes setting then
comes out on in one run and off in the other, and 29 of 2,346 lines
differ from there on. Two runs of the one program that differ only in
`--gl-scale`, with no pictures between frames at all, part at **the same
29 lines**: nothing of this is the renderer's.

A strip of the pictures of six frames one after another is in
`build/fps/port_game/gl-0030NN-pNN.png`: the last picture of a frame
differs from the game's own next frame in 4,314 pixels of 1,228,800
(0.35% — the faces that appeared or went, and the HUD, which stays at the
game's rate), and the first picture of the same frame differs from it in
408,541 (33%). That is the whole of what the pictures in between are
for, in two numbers.

**In the window**, through SDL's `offscreen` driver on Mesa's software
renderer (`--frame-rate 144 --no-vsync --resolution 1`, the plan of
`port_game`): the renderer between frames starts, the log says so, and
the game keeps the console's time — 59.8 blanks a second through the
movie and the menus, where one picture a blank is all there is to show
because nothing of a menu has depth. In the fight it showed about two
pictures for each of the game's frames, and the game itself fell to
about 15 frames a second: **a software renderer with no card has no room
to draw a frame twice**. 4,669 pictures were shown in the run, 188 of
them between frames. Nothing was seen; the log and the numbers are all
there is.

### What each later step needs

- **Lighting and shadows.** Today every vertex of a model is lit by the
  GTE, by three directional lights and an ambient colour from the
  stage's `LightSetup` (`src/main/scene.c`), and the colour is in the
  primitive. A shadow is the model flattened by a matrix
  (`render_SetupNode`) and drawn in black (`render_EmitBlackF3`,
  `render_EmitBlackF4`); there is no shadow of one thing on another and
  no light that moves. Better lighting means the normals, which the
  mesh has and the primitive does not: the side channel is where they
  would go, beside the position and the depth.

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

**Why these forms and not others.** The source is one source for the
PlayStation and the PC. Port code beside the PlayStation's under `#if
RB_PORT`, which is how the port is written where the two need different
code, would here have meant an `#if` block round each of 250 casts and
each of 800 uses of a member, and would bury the source. The forms above
are written into the game's own lines and change no byte of any
PlayStation build: they are macros that expand to the plain expression
there, the edit is mechanical, and `decomp` is not touched. (They were
chosen while this branch's rule was still that the original's lines
stay as they are; the ReBushido program built before and after them is
the same file.) What was weighed against it:

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
  off the stack like the player's pad's. Two of the seven stopped the
  same way in both sizes, on the story's first fight reading through a
  pointer that was not set yet; that place is put right, and the sweep
  of the rows that followed ("How it is proved") drives every one of
  them to its end.
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

- `src/port/host_windows.c` is built and run in both sizes, with a
  window and without ("The window"). Windows gives memory at an address
  in blocks of 64 KB, which it allows for (the sound chip's page is in
  the scratchpad's block), and it tells the system where the game's
  stack is, which the system wants to know of before it will hand a
  fault to a handler of that stack's. A 64-bit program has no such
  chain of handlers, so the game's thread is taken by a handler the
  system calls first for every fault.
- macOS: an Intel Mac's 64-bit program can be linked low and have the
  memory. On Apple's own processors a program's first 4 GB are kept
  empty by the system, and whether that can be turned off is not known
  here; if not, "everything below 4 GB" does not hold there and the
  port needs another way (the console's memory at an offset, the slots
  as offsets into it).
- A C library is taken, in the build with a window ("The window"): its
  own allocations may be above 4 GB, and none is given to the game.
  Memory the port hands the game's code is the mapped memory, the
  program's own variables and the game's stack, all below 4 GB; the
  window asks the library for one thing only, the settings file's text,
  which the window's own code reads and the game never sees. Either
  program is still linked without position independence, at its fixed
  address.
- Floating point is in the window's SDL files and nowhere else. Every
  file of the game, of Sony's and of `src/port/` is compiled without the
  processor's vector instructions (`-mno-sse -mno-mmx`,
  `tools/port_build.py`, `NO_VECTORS`), because a 64-bit compiler would
  want the 16-byte boundaries the build takes away so that the game's
  variables stay neighbours. The files that talk to SDL and to the C
  library (`src/port/window/sdl_*.c`, `host_posix.c`,
  `src/port/host_windows.c`) are compiled apart, with those instructions
  and with floating point, and none of them copies or clears an array of
  the game's: they read the port's video memory a pixel at a time
  (`port_ConvertDisplay`, in a file of the game's kind) and hand SDL
  buffers of their own.
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
- The 32-bit program is still built and tested, and so is the 32-bit
  window build (`PORT_BITS=32`), which costs only a flag. Comparing the
  two sizes is the cheapest proof there is, so both stay for now.

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
| BIOS calls | 40 two-instruction jumps (`BIOS_A_FUNCTION` and its like), 2 system calls, `_96_remove` | `src/main/psxsdk/libapi/`, `libcard/` | Not C equivalents: the kernel does not exist on a PC. The port supplies what the game calls (below) | defined; `Exec` is a stub |
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
in the port": events, the counters, the pads' packets, the memory card and
the nothings are done, `Exec` returns at once.

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
  PlayStation's memory, which are written into on load. On the port the
  card hands the game an empty table of them ("The memory card", "The
  patch table").

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
- **The memory card by made-up images**, on every push:
  `tests/port/card.c` and its `.py` ("The memory card"), and the card the
  port writes with the retail image held to the one the emulator's BIOS
  wrote, which needs no emulator run.
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
  original library's own ("The movie"). Against the emulator: the play
  test `port_movie` -- the game's state through the whole movie and
  after it, the title picture to the pixel, and the sound chip at three
  moments of it -- and a frame of the movie pixel for pixel, lined up by
  hand ("The movie").
- **`make port` on every push**: the program links with no name
  missing and the port's files have no warning, with 64-bit pointers
  and with 32-bit ones; and the three checks of "64 bits" (the layouts,
  the casts, the order of evaluation in each size).
- **The 64-bit program against the 32-bit one**, and every host test
  as a 64-bit program ("64 bits", "What is proved").
- **Every row of the menu, by hand once**: ten runs of the port alone,
  both sizes, driven by plans of their own into each mode and through
  its first fight or two (story's narration, first two fights and the
  menu that follows a loss; versus the CPU to a kill, the menu after a
  fight and back; practice, with the pause menu it opens on and its
  toggles; versus with two pads, other fighters, weapons and stage;
  giving up, so that the kaishaku's cutscene runs; the team battle's
  rules, clans, three fighters and three weapons a side, and its first
  bout; the link cable's row with nothing at the other end; the game's
  options page, each row of it and its card menu, which writes the
  game's own save file; slash mode and the gunners, from a save written
  for the sweep). Three crashes of the kind "Memory" names were found
  this way and put right; after them every run reaches its end, and the
  two sizes' traces are the same line for line. The plans are not kept:
  they are a sweep by hand, not a test of every push.

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
   in a thousand, each one accounted for ("The picture"). The window is
   built ("The window"). Left of it: someone to look at it.
5. **Pads, time, a fight.** *Done when* a scripted fight on the port goes
   through the same watched values as the same script on the emulator.
   That part is done, in three fights to their ends ("How it is
   proved"). The PC's own keys, controllers and time are in the build
   with a window, and a plan's presses made as key and controller events
   give the same fights ("The window"). Left of it: a person at the
   keys.
6. **Sound.** The sound chip, Sony's sequencer, the disc's audio.
   Done into a file ("Sound"): the game's effects and music are in a
   run's WAV, the game's state is what it was, and the chip's memory
   and registers are the emulator's (`port_sound`). The build with a
   window gives the same samples to a sound card ("The window"). Left
   of it: someone to listen.
7. **The rest.** Movies, the memory card as a file, the link (or
   nothing in its place). The card is done ("The memory card"): a
   standard card image a slot, with the BIOS's calls over it, and the
   image the port writes is the emulator's own byte for byte. So is the
   movie ("The movie"): the opening plays, by the overlay's own C over a
   picture decompressor in C.

The wide picture and the high resolution are the roadmap's next item and
build on stage 4.

## What is left

How far the program gets today: with `port_game`'s presses, through the
opening movie, a versus fight to its kill, the replay, the menu after it
and back to the main menu, as the emulator does. Nothing stops it; what
it cannot do is listed here.

**No stub is called on the way** to the fight's first second, or in a
whole fight and back to the menu: a run ends with no `stub` line at all.
What is still a stub, and never called on that way: a CD audio track
(`CdlPlay`), which the disc has none of, and the link cable's three. A
name that is not a card's file is the link cable's port, which is a stub
too.

**Known, and not done:**

- Three fights are proved, all of two players, of the same two
  fighters, each ended by a cut. Not run against the emulator: a fighter
  played by the CPU, the other modes, guns, thrown things, a fall from
  a stage, a lock of blades.
- Words off the stack differ, in padding and in places nothing was seen
  to read ("What the comparison found"). The port's are addresses of
  its own stack, which is at the same place in every run, so a run is
  still the same twice; they are not the console's.
- The window has been seen by nobody, and its sound heard by nobody
  ("The window", with what is not proved). The two things the port
  draws as the chip and not as the emulator are proved by made-up cases
  alone ("The picture", with what is not sure).
- The picture is drawn when the game asks, with no time taken: a frame
  the console would not have finished drawing is finished here.
- What a function leaves in the result's register when it returns
  nothing was looked for by the compiler's warnings and by reading the
  one table of handlers that is called for a result; a `void` function
  reached through some other pointer for a result would not have been
  seen.
- Address 0: six places are found and put right ("Memory"). Three of
  them were found by driving every row of the menu, which is as far as
  looking has gone; the others, if there are others, are not found. A
  mode the sweep does not reach may hold one: the table of best times
  after a whole chanbara run, a story ending, the staff roll.
- **What looked like one more place where the game reads what it never
  wrote, on the options page, was no such thing**: it was a memory card
  image left behind between runs.
  The plan of a play test whose runs have no card says `nocards`, and the
  port read that line and did nothing with it: only `--no-cards` on the
  command line, which `tools/playtest.py` passes and the renderer's own
  provers do not, emptied the slots. So a run of `port_bugfixes` made
  `card1.mcd` beside the program, turned Bugfixes on at the options page
  and wrote it there; the run after it loaded that file and the plan's
  Right turned the setting back off. Two runs of the same program and the
  same plan therefore ended with it on and off, deterministically, by
  whatever had run before them — `--gl-scale 1` against `--gl-scale 4`
  only chose which went first. The plan's line is now obeyed
  (`port_LoadPlan`, `src/port/plan.c`), so a run of a plan that says it
  has no card has none whatever is beside the program: the two scales now
  trace the same, line for line, with that stale card image still there.
- What is heard is compared with the emulator's only in part and by
  hand, and where the hardware's filters are not reproduced it is not
  the console's to the sample; the music is a tenth louder than the
  emulator's ("Sound"). Nobody has listened to the sound card.
- An address of the game's own variables has no top bit on a PC. One
  place tested it and is put right (`snd_FlushQueue`); linking the
  variables above `0x80000000` would end the kind.
- 123 pairs of neighbouring variables are not as far apart as on the
  PlayStation in the 32-bit program, and 248 in the 64-bit one
  ("Memory"). None is known to matter; none has been ruled out one by
  one.
- macOS on Apple's processors may not allow the memory the port needs
  ("64 bits", "What is left of it"); nothing of the port has been built
  there.
- The game's own save file is written and read on the port (the options
  page's card menu, driven by the sweep), but no run has put a save
  back on an emulator to be read by the console's own code: what is
  cross-checked that way is ReBushido's file (`port_card`).
- The build compiles more than it must: the headers it writes are
  written again each time, so most files are compiled again (twenty
  seconds).

## The owner's decisions

| Decision | Recommended | Why |
|---|---|---|
| A 32-bit program first, or 64-bit from the start | 32-bit first; done, and 64-bit after it, done ("64 bits") | The source keeps addresses in 32-bit integers at some 300 places, relocates nine kinds of file in place into 4-byte slots and saves 22 addresses to the card: as a 32-bit program all of it works as written. The 64-bit program was then a stage of its own with the port already playing, and the 32-bit one is what it is held to |
| Memory at the PlayStation's addresses, or everything moved | At the PlayStation's addresses, in both sizes | A program linked low can ask for memory at `0x80010000` and `0x1F800000`, whatever the size of its pointers. The twelve fixed addresses, the 24-bit links and the save file's patches then need no change at all. Where a system will not give a program memory below 4 GB (Apple's processors, perhaps), this is the decision to take again |
| A library for the window, the pads and sound | SDL 2 | One library does all three on Windows, Linux and macOS, and its licence sits with this project's. Taken: "The window". SDL is linked to, and none of it is in this repository |
| The game's data: the disc image, or files taken out of it | The player's disc image | The game asks for sectors, never for names, so an image answers it directly; the music is tracks and streams that only the image has in one piece; and no tool has to unpack game data. Loose files can come later for modding |
| The first renderer: software as the console draws, or the PC's graphics card | Software first | It can be held to the emulator's picture pixel for pixel by the tests there already are, and needs nothing installed. The hardware renderer is then the roadmap's next item, behind the same list of primitives |
| The movies | Decode them in the port, in C | Settled, and done ("The movie"): the overlay's own C is compiled into the port and its `main` is called where the console would run the program it has just read; under it are the picture decompressor and the drive's streaming, in C |
| The frame's wait | A fixed number of calls, for now: one for each scan line waited ("Time"); to be looked at again at stage 5 | The original calls `rand()` for as long as the frame has time left, so its random numbers depend on the machine's speed. The port needs a rule; none reproduces a console exactly |
