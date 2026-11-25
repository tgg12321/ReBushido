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
takes an OpenGL 4.5 context for it and F2 changes picture as the game
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
| "Graphics: Remastered" | done in the window: the scene drawn again by a graphics card at the window's resolution, from the geometry the PlayStation threw away — OpenGL 4.5 core, high resolution, sub-pixel corners, textures in perspective, true-colour mixing, the 2D layer crisp; supersampled edges, the whole stage drawn, a wider picture at 16:9, and the fighters' shadows as a soft darkening of the ground they fall on and never of the fighter standing in one ("The shadows"). Proved with nothing shown against the software GPU's own picture, and hung on the window, where F2 changes picture as the game runs ("Remastered", "Remastered in the window") |
| The memory card | done: a standard card image of 128 KB a slot, under the BIOS calls the game makes ("The memory card"). The card the port writes for the options page is byte for byte the one the emulator's BIOS wrote |
| The movie | done: the opening plays, 3,320 frames of 320 by 240 at 29.97 a second, by the overlay's own C over a picture decompressor in C ("The movie"). Its sound is the drive's own, as the console has it |
| The stage select's kinds of stage | done: each stage Classic or Remastered Classic by Square on the stage pages, a page of the custom arenas by Select, the choice the game's state, the same on both machines of a session ("The stage select's kinds of stage") |
| A stage in every mode, every unlock | done: versus the CPU and training through the game's own stage pages (`stage_select_everywhere`), and every fighter and slash mode open without writing them into the save (`unlock_all`); the game's state, the host's in a session ("Every stage in every mode, and every unlock") |

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
cards: "The memory card"), and `--rollback-test N` with `--rollback-every
M` and `--rollback-quiet` (the self-test of the game's state saved, put
back and run again: `NETPLAY.md`, "Save, load and hash"), and the
network's: `--net-bind` and `--net-peer` (a UDP socket to one other
program), with `--link-side 0|1` (the game's link cable over it) or
`--netplay 1|2` and `--net-delay N` (rollback netplay), `--host
ADDRESS:PORT` and `--join ADDRESS:PORT` (a session of netplay, set up
between the two programs before the game starts; with `--session-at N`
begun instead in the running program after N frames of its own, and
`--sessions K` times one after another, `--net-seed N` fixing the
host's seed: `NETPLAY.md`, section 15), and
`--net-latency MS` and `--net-loss PERCENT` for tests (`NETPLAY.md`,
sections 11 to 13), and the mods' (`MODS.md`): `--mods FOLDER|off`,
`--dump-textures FOLDER`, `--music-mods on|off`, `--dump-music FOLDER`
(each piece of music as it plays) and `--dump-stages FOLDER` (each
stage's files from the disc). It ends by printing a line for each stub
the game called, with how often.

The game's main never returns: it runs on a stack of its own as a fiber,
and the program's loop runs it a step at a time, a step ending where the
game reads the pads (`src/port/step.c`; `NETPLAY.md`, "The step"). Pads are pressed only by a plan: a line `in 0 300 1 START 6`
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
`0x1F801000`, which only Sony's sound libraries reach: "Sound".)
**All of them are given first thing** (`port_MapFixedMemory`,
`port_host.h`): the window and the remastered renderer's prover call it
before SDL, EGL or the card's driver starts, because a library's memory
lands where the system likes, and the scratchpad's address is low
enough for the C library's heap to reach. Asked for after EGL had
started, it was now and then already taken, and the prover's run
stopped before its first frame with an empty log. The
port's own buffers of primitives (`PORT_PRIMS_ADDR`) are given then too,
whether or not the run uses them; nothing is written there unless it
does. The
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
- **A table on a stack that has gone.** `disp_DrawLoadingScreen`
  (`main.c`) draws the loading screen with an ordering table of two
  words on its own stack, makes it the frame's (`g_disp_ot`) and leaves
  it so when it returns. Where a loader runs inside a frame of the fight,
  what that frame draws after it is linked into a table that is no
  longer there: in the group battle, `fight_UpdateMode` ends a match with
  `fight_NextTeamMatch`, which waits on the disc behind the card of the
  scores, and then draws the points and the time
  (`hud_DrawPointsAndTime`), and each `AddPrim` writes a primitive's
  address into a word of the stack that a later call's frame holds. The
  PlayStation lives with it: the table is never drawn, so that one
  frame's HUD is lost behind the card, and whatever the word belongs to
  then on the console, the game plays on (the emulator plays the same
  group battle through). In the window's build the word was the
  pointer `hud_DrawPointsAndTime` reads the time through, and the port
  stopped as the first match of a group battle ended. Under `RB_PORT`
  the table is `static`: it outlives the call, the links land in it,
  and it is still never drawn, so the picture is the console's.
  `tools/port_match.py`'s group battle plans play through that card on
  both sizes, and the play test `port_group` holds the port to the
  emulator through it.

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

**An argument that was not passed.** When the arena moves the Castle
Rooftop's block, `resource_OnStageMoved` moves the backdrop's table of
meshes with `reloc_RebaseBlock(RELOC_STAGE_PROPS)`, no distance given
(the function has no prototype; `RB_FIX_ROOFTOP_REBASE` passes it). On
the PlayStation the distance is whatever the second argument's register
holds, and that is the distance itself: the call to `reloc_RebaseBlock`
just before passes it there and leaves it, and `collision_RebaseGrid`
does not touch it (read from the listing). On a PC the register holds
anything, so the backdrop's table and every pointer in it went off by a
number from nowhere. The arena moves the stage only when a block before
it is freed with the stage kept: the next opponent of a run against the
CPU on the stage the stage pages chose, whose classes are the last
one's. The owner's game stopped there (2026-11-19: Tony Umeda against
Kannuki and then Utsusemi on the rooftop, a fault in `render_DrawQueue`
reading a mesh of the backdrop on the next fight's first frame; on Linux
and in a run of the Windows program on the same presses, in
`reloc_RebaseBlock` itself as the next fighter's model was loaded). The
port passes the distance, the console's value, under `RB_PORT`
(`resource.c`). `tools/port_stage_select.py`, plan `vscpu_rooftop`,
holds it.

Two other calls left an argument out, neither a pointer, so nothing
crashed, but a number from nowhere differs between two machines and
would part two players' games online and a rollback's run again from
the first. Each is now the console's, read from the listing, under
`RB_PORT`. The thrown earth's `eff_StartThrownDirt(pos, dir[1])`
(`fight_RaiseEvent`) leaves out `grass`, which says dirt or grass: the
register holds the left ear's volume that `snd_PlayAt` has just passed to
`snd_Play` (neither changes it), so it is grass where that volume is 1,
and the port passes that volume. The pause menu's `pad_Update()`
(`fight_RunPauseMenu`) leaves out `no_unplug_pause`: the register holds
`0xFFFFFF`, the mask of an ordering table's address that Sony's
`ClearOTagR`, called just before, loads last, so a pad pulled out is not
taken for Start there, and the port passes it. And `select.c`'s
`font_Printf("warning\n")` leaves out an argument that a text with no
conversion never reads; the port passes 0.

**Every call is held to it.** `tools/port_order.py`, which `make port`
runs on both sizes, also reports each call in the port that passes fewer
arguments than the function it calls is defined with (`short`), from the
statements the port's compiler made of every file, across files. Such a
place is never taken from `config/port_order.txt`: the build fails until
the call passes what the PlayStation's registers hold. Its first run,
after the rooftop's was put right, found these three and nothing else.

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
sector on its error output. The disc's ISO 9660 directory is read once
as the program starts (`src/port/discfiles.c`): the game never names a
file, but the mods do, a piece of music by the file it is a stream of
and a stage file by the file it replaces, whose sectors then carry the
mod's data ([`MODS.md`](MODS.md), "Stages"). Sound off the disc is the drive's too: an
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

**A mod's music** ([`MODS.md`](MODS.md), "Music") is heard in place
of a stream of the disc's: the drive, the decoder and the chip go on as
here, and the samples heard are a copy of the chip's output with the
mod's piece where the disc's sound was (`port_RunSpu`'s `heard_out`),
outside the game's state. With no music mod the sound is unchanged.

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

### The movie made better

The movie is the software GPU's picture, 320 by 240 in the 24-bit mode,
which the card's remastered picture does not draw. The window's `movies`
setting (the settings menu's Remastered page, "Movies") says how the
remastered picture shows it; the console's picture (F2) shows it as it
always did, and nothing of this touches the decode, the player or what a
play test takes (`port_movie` holds the same files):

| `movies` | What the window shows |
|---|---|
| `original` | the console's frames, as blocks or smoothed as the `filter` setting says of the game's |
| `smooth` | the console's frames stretched smooth: what it was before |
| `enhanced` (built in) | the frames cleaned and made larger on the card, in five passes; or a mod's frames made from the disc, where there is one (MODS.md, "The movie made larger") |

**The five passes** (`src/port/gl/gl_movie.c`, after the window's other
passes, into the picture's rectangle of the window):

1. **colour**, at the console's size. The chip decodes the two colour
   differences at half the width and height and lays each over its 2 by 2
   pixels flat (`mdec.c`), which shows as stairs of colour along every
   coloured edge. The differences are taken back out of the four pixels
   (the step to red, green and blue is linear, so the four's mean is the
   cell's but where a channel was clipped) and each pixel takes them from
   the four nearest cells by how near their centres are, 9, 3, 3 and 1
   sixteenths, keeping its own brightness.
2. **blocks**, at the console's size: across each edge between two 8 by 8
   blocks, where the step is small and both sides are flat, the step is
   spread over the four pixels beside it (in the manner of H.264's
   in-loop filter's weak mode), as strong as the frame's own quantisation
   scale says. That scale comes with the frame: the port follows which
   frame of the stream each of the player's two display buffers holds
   through the pointers the player hands its steps (`StGetNext`,
   `DecDCTvlc`, `DecDCTin`, `DecDCTout`, `LoadImage`;
   `src/port/movie_shown.c`, outside the game's state).
3. **ringing**, at the console's size: each pixel the mean of the nine
   about it weighted by how near their brightness is to its own, so a
   ripple a few steps high beside an edge goes and the edge stays; as wide
   as the scale says.
4. **larger**, to the window's rectangle: AMD FidelityFX Super Resolution
   1's EASU (MIT; written again in GLSL 3.30, the notice in the file), which
   enlarges along an edge and not across it, held between the four nearest
   texels so that it never rings.
5. **sharper**: FSR 1's RCAS at 0.6 stops (0.66), lightly.

With a mod's movie the first three are not run: its frame N is decoded
(stb_image, the next frame made ready while the current one is on show)
and goes through `larger` and `sharper` (1.2 stops, 0.435).

**What it costs**, measured through the whole movie at 2560 by 1440 on the
owner's PC (RTX 3090; the window's `--profile`, blanks 600 to 5,500), the
card's time of the pass that puts the picture into the window, and the
processor's time of the window's `picture` part (which, for the mod,
decodes a JPEG of 1280 by 960 once a movie frame, every other blank):

| `movies` | The card, median (95th, most) | The processor, median (95th, most) |
|---|---|---|
| `smooth` | 0.032 ms (0.036, 0.20) | 0.18 ms (0.41, 1.0) |
| `enhanced` | 0.18 ms (0.25, 0.37) | 0.87 ms (2.2, 5.7) |
| `enhanced` with the mod | 0.13 ms (0.13, 0.78) | 3.4 ms (9.9, 14.4) |

So a frame of the movie costs the card about 0.15 ms more than before,
well inside a blank of 16.7 ms while nothing else is drawn; the mod's
decode is the most of it, on the processor, and fits too. A thread of
its own for the decode would take it off the blank; it was not needed.

**What it looks like**: the same frames in the four ways, taken from the
window at 2560 by 1440 (a run each with `--tap-window`, frames of text, a
face in the dark, two dark scenes, a face by firelight and a fast
movement), are kept outside the repository; on llvmpipe the enhanced
picture is the card's to within one step of 255 in any channel. Against
`smooth`, `enhanced` takes the colour stairs off every coloured edge,
takes the blocks out of the dark gradients and the fog, and draws edges
and lettering as lines instead of a smear; it cannot put back what the
quantisation took. The mod's frames are another thing: clean edges, eyes,
hair and lettering drawn sharp at four times, the blocks and ringing gone;
a little flatter in the hair and the fog than the film, as a learned
upscale is.

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
| `settext.c`, `choices.c` | the settings menu's arithmetic: `rebushido.cfg` changed a line at a time, keeping the rest of it, and the settings as the menu shows them (made from `settings_list.h`), set by reading the line the file would have. No SDL and no C library (`tests/port/settings.c`) |
| `settings_list.h` | every setting of `rebushido.cfg` once: its member, value built in, comment, reader and menu row (below, "Every setting is one entry of one list") |
| `gamestate.c` | reads the game's variables out for the log, and has the names the log gives screens, modes, fighters, weapons and stages; it writes one, the Bug fixes setting, for the settings menu |
| `sdl_window.c` | the window, the sound card and the controllers, on SDL; the counter; the log's notes of the game |
| `sdl_log.c` | the session's log file |
| `menu.c`, `sdl_menu.c`, `font.c`, `menu.h` | the settings menu ([`PLAYING.md`](PLAYING.md), "The settings menu"): its pages and what a press does, when it opens, beside the running game over the main menu and the pause or in a loop of its own with the game stopped at a frame's boundary elsewhere, and its text drawn at the window's resolution in the port's typeface, Shippori Mincho (below, "The port's lettering"). None of it is the game's state: these files are linked outside the game's object, as every file of the window is |
| `page_look.c` | the look of the game's own menu pages, the Option Menu's, which the settings menu and the online pages are drawn in (below, "The window's pages in the game's look") |
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

The program says the layout in use on its error output as it starts.

**Escape** is the window's own, no key of a pad's, and never ends the
program: it holds a button of player one's pad for as long as it is
down, the button the game itself uses where it is, chosen as the key
goes down (`port_EscapeButton`, `sdl_window.c`):

| Where | Escape is | What the game does with it |
|---|---|---|
| a fight | Start | opens the pause menu (`main.c`) |
| the pause menu | Triangle | closes it and goes on with the fight |
| a story's cutscene | Cross | skips it (`story.c`) |
| everywhere else | Triangle | backs out: every menu and screen of the game cancels with Triangle (`menu_pages.c`, `select.c`, `memcard_save.c`); the top menu goes back to the title |

Where there is nothing to go back to (the title, the options page at
boot, a load, a movie) Triangle does nothing, and nor does Escape. In
the settings menu Escape is that menu's back (below). A settings file's
`pN.BUTTON = key Escape` is complained of and not used, so no layout or
binding can take Escape away. The program ends when the window is
closed, or on **Quit ReBushido** at the foot of the settings menu; during
netplay, where that menu does not open, by closing the window. A test
presses Escape with `--escape-at N[,N...]` (at those blanks, let go four
later), and the log says what it pressed (`Escape: start of player
one`) and when the pause menu and the top menu come and go.

**The settings file** is `rebushido.cfg` beside the program, or the one
`--config FILE` names; with none, everything is as built in, and
`rebushido --help` prints the built-in settings as such a file, to copy
from. A line is `name = value`, and `#` begins a comment:

```
scale = integer            # fit, integer or stretch
filter = nearest           # nearest or linear
fullscreen = on            # on or off
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

Buttons joined by `+` (`p2.l1+r1 = key Right Ctrl`) are one key or
button pressing all of them; such a line takes away no single button's
keys. The bindings built in are a layout, `controls = original`
(`g_port_layouts`, `src/port/window/keymap.c`): a layout is the text of
a settings file with nothing but `pN` lines, and another layout, such as
a modern control scheme, is another text and a line of that table. A
file's `controls` line replaces the built-in bindings with that layout's
and comes before its own `pN` lines, which are read over it.

**Every setting is one entry of one list**,
`src/port/window/settings_list.h`. Each entry gives the setting's name
in the file, its value built in, the comment `--help` prints beside it,
how its value is read, what a line it cannot read is told (`NAME is
...`), and its row of the settings menu (group, rank, label, help, the
values it steps through). Everything else is made from the list:
the member of `PortSettings` (window.h includes the list once to declare
them), the reader of a file's lines and the settings built in
(`keymap.c`: the default is the value the built-in text has), the
built-in text itself (`port_GetDefaultSettings`, its comments at column
23), and the menu (`choices.c`: the entries with a group, by group and
rank, `port_FindChoice`). A value is read in one of four ways: a switch
(off or on, the default kind), one of a list of words (its place in the
list), a number from `least` to `most` with words for some numbers
(`antialiasing = off`, `frame_rate = display`), or a reader of the
setting's own (`aspect`, `shadows`, `net_address`, `controls`). A new
setting is one entry there, and two settings added at once touch
different lines; the menu's ranks go in tens so that a row can go
between two. What a setting does when it changes in the menu is still
`port_WindowApplyChoice` (`sdl_window.c`); a setting that is read where
it is used needs nothing there. `tests/port/settings.c` holds every
entry to its value built in, its menu row and the built-in text.

**Where the game's input comes from.** The window reads SDL's events at
each vertical blank and works out, through the layout, what the keys and
controllers hold on each pad; the port's pad driver reads that
(`g_port_device_hook`) together with a plan's presses at the same blank,
as the console's driver reads the pads, and the game is given it when it
reads the pads, as one value of the frame (`port_TakeInput`,
`src/port/pad.c`; [`NETPLAY.md`](NETPLAY.md), "The one input point").
`tests/port/input.c` holds the value, its timing and the mapping to what
`pad_Update` finds.

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

**Windows.** The program is 32-bit, linked at `0x08200000` (it was
`0x08400000` when this was written: "Where the program's variables
are") with address
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

### The port's lettering

Everything the port letters itself is one typeface, **Shippori Mincho**
(FONTDASU; the SIL Open Font Licence 1.1, `src/port/fonts/OFL-ShipporiMincho.txt`):
a Japanese mincho whose Latin letters were drawn to sit beside its
kanji, with wedge serifs and a brush's stroke contrast, and figures that
line up. It replaced three (Marcellus in the settings menu, Kaushan
Script on the top menu, Archivo in the sharp menus) on the owner's word
(2026-11-19): one family across the game, and a better one than the top
menu's. Five others were held to it on real screens of the game first (Zen Old
Mincho, Kaisei Tokumin, Alegreya, Cinzel, Cormorant Garamond); the
Japanese mincho families read best at small sizes and carried the
game's look, and Shippori's heaviest weight makes the best titles.

Three weights are kept, cut down to Latin by `tools/port_fonts.py`
(printable ASCII, Latin-1, dashes, quotes, the ellipsis, arrows; no
hinting): about 34 KB each where the family's own files are eight
megabytes for the Japanese. The licence allows a modified font to be
shipped under it, and the family reserves no name, so the subset keeps
it. `tools/port_sdl_build.py` puts the three into the program as C
arrays.

| Face | `font.c` | `tools/port_lettering.py` | For |
|---|---|---|---|
| Regular | `PORT_FACE_TEXT` | `'text'` | the settings menu's lines, notes, the stage select page, a roman with an edge (the select screens' titles, the stages' names) |
| Bold | `PORT_FACE_BOLD` | `'bold'` | headings, the counter, the menus' lines, names, captions |
| ExtraBold | `PORT_FACE_TITLE` | `'title'` | titles, the top menu's rows, the fight's digits |

**In the program** (`src/port/window/font.c`, `menu.h`; the window's
build alone). Letters are drawn from the outlines by stb_truetype at the
size asked for, at the window's own resolution, kerned (the face's
`GPOS` pairs), and kept for the next call of the same face and size
(eight sizes at once, the oldest let go). The call for any text the port
draws:

```c
PortTextStyle style = {0};
style.face = PORT_FACE_TITLE;    /* PORT_FACE_TEXT, _BOLD, _TITLE */
style.size = 64;                 /* pixels, top of the tallest letter to
                                    the bottom of the deepest */
style.color = 0xFFFFFFFFU;       /* 0xAARRGGBB */
style.outline = 0xFF000818U;     /* an edge all round ... */
style.outline_px = 3;            /* ... this wide; 0: none */
style.shadow = 0xFF10298BU;      /* a shadow down and right ... */
style.shadow_px = 5;             /* ... this far; 0: none */
if (port_FontStart()) {
    s32 w = port_TextMeasure("Online Play", &style);
    port_TextDraw(pixels, pixels_w, pixels_h, (pixels_w - w) / 2,
                  baseline, "Online Play", &style);
}
```

`port_TextDraw` lays the text over pixels of 0xAARRGGBB (its alpha is how
much it covers), `x` being the left of its edge and `baseline` its
baseline; `port_TextMeasure` is its width with edge and shadow,
`port_TextAscent` the baseline's distance below the tallest letter's
top. Text is UTF-8; ASCII and Latin-1 are drawn, anything else is `?`.
`port_FontSetSize` / `port_FontDraw` / `port_FontMeasure` /
`port_FontAscent` are the same for plain letters in the Regular face (the
stage select page's). The settings menu (its title and headings in
Bold), the netplay line and panel, the stage select page and the counter
(`fps`, Bold, at the window's resolution, its box placed as the digits'
was; the 5 by 7 digits of `fps.c` stay for a window with no card and for
when the typeface cannot be read) all draw through it.

**In the tools** (`tools/port_lettering.py`, Pillow). The same faces for
the pictures of the mods the port ships: `letter(text, face, size,
color, top=, bottom=, outline=, outline_px=, shadow=, shadow_px=)` gives
an RGBA picture of a text (`top` and `bottom` run the letters' colour
down them, as the game's own lettering does), `font(face, size)` the
Pillow font, and `line_mask` fits a text to a rectangle the game's
lettering fills: as tall as the rectangle (fitted to the text with a
capital, and a descender if it has small letters, so the lines of a menu
come out one size on one baseline), as wide -- narrowed only to 0.8 of
the face, and past that smaller on the same baseline; widened only to
1.1, and past that spaced a little and set in the middle (or spaced to
the ends, for a text the game cuts into pieces at fixed places) -- and
twice as wide in a texel for a screen of 640 by 240 (`aspect`), whose
texel is twice as tall as it is wide. `python tools/port_lettering.py
OUT.png TEXT --face title --size 96` draws one to look at.
`tools/port_english.py` (the top menu) and `tools/port_ui.py` (the sharp
menus) letter through it; texts of one kind there (one face, one height,
capitals or not) are drawn at one size.

What the port does not letter: the game's own brush lettering and logos
(the title, ブシドーブレード弐, SQUARESOFT), the Japanese of the weapons'
scroll and the HUD's 時間, which are pictures, not text; and the
publisher's card at boot ("Published by Square Electronic Arts L.L.C."),
which is plain text but a picture the game writes into video memory
that is not looked up while it is shown, so a mod's picture of it never
shows.

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
pictures a second than the game has frames ("More pictures a second"),
and the fighters' shadows drawn as a darkening of the ground instead of
the game's black silhouettes ("The shadows"), the effects placed and
moved where the projection put them ("The effects"), and the game's own
lights worked out at each pixel of the ground and the fighters' models
("Lighting"). What it leaves for the step after it, by design and not by
accident: lights of its own ("What each later step needs").

**Three settings of a run** say how much of each (`port_remaster.h`,
`PortRemasterView`), each of them the drawing path alone:

| | | |
|---|---|---|
| `draw_distance` | `original`, `far`, `max` | how many of the stage's cells are queued, and how far out the stage's fog is placed |
| `antialiasing` | `off`, `2`, `4` | the scene's edges multisampled, N times N samples a pixel, eight at most ("Edges") |
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
  1024 by 512 16-bit words, a buffer the card reads as a texture a line
  after another ("Measuring on the card", "The mirror sent without
  waiting"), by the shader: four bits or
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
- **A flat polygon is the chip's to its edges.** A triangle of the 2D
  layer (a loading screen's swords, the slanting panels of the stage
  select, a portrait cut into strips) is drawn by a geometry shader as
  the rectangle about it, and the fragment shader keeps a block only
  where the chip's own rule (`port_DrawTriangle`, `raster.c`: from the
  edge on the left to the pixel before the edge on the right, the
  highest line to the line above the lowest) puts the block's own pixel
  inside it, with the colour and the texture point the chip's own
  arithmetic gives that pixel: steps across and down in twelve bits of
  fraction, counted from the corner furthest left, worked out once for
  the triangle in whole numbers. Before, the card's rule at the
  picture's resolution cut a block a slanting edge crossed in two, and
  the two triangles of a quad, whose steps differ where its texture does
  not run evenly across it, left a seam of blocks a texel out along the
  diagonal: 18,917 broken blocks and a worst channel of 182 in
  `port_game`'s flat screens, which are now none and 11. It costs
  Mesa's software renderer about twice the time a frame (489 to some
  1,000 µs in the prover); on a card it is nothing.
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
  **A primitive the projection placed itself is measured by those
  places**, and allowed twice video memory's width across and twice its
  height down (`SPAN_TRUE_X_MAX`): every corner of such a one is in
  front of the camera and where the projection really put it, so it is
  an honest perspective triangle however far off the side of the
  console's screen its corners are, and the eleven bits the chip held
  them in are no rule for it. Holding it to them is what cut the ground
  off at the console's own edges in the bands of a wider picture ("A
  wider picture"). What the wider span still leaves out is a projection
  that came apart: a corner all but on the camera's own plane, whose
  place runs to tens of thousands of pixels.
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

**But nearest is for a texture made larger, and the distance makes them
smaller.** A fragment of the ground a long way off covers several
texels, and reading one of them is noise that crawls as the camera
turns, which is what the owner saw as a shimmering floor. So a polygon
of the scene — one the projection gave a depth to, and nothing else —
has **the texels its fragment covers averaged**: the texture point's own
step across and down the screen is a derivative of the perspective-correct
point, and where that step is more than a texel the shader reads up to
four points each way spread over it, through the same texture window and
colour table as one would be, so that nothing is read outside the page
the chip would have read in (the flag `FLAG_DEPTH`, `u_minify`, `steps`).
A fragment that covers a texel or less — which is every fragment of
anything near, at any scale above the console's — reads one, as before,
so the 2D layer and the fonts are untouched and the cost is only where
the noise was. And **the picture is smoothed where it is made smaller**
on its way into the window: the scene is drawn at as many times the
console's resolution as the window asks for, rounded up, so the picture
is almost always larger than the rectangle it is shown in, and taking
one pixel of it for each pixel of the window threw the rest away
(`port_GlShowPicture`; the `filter` setting still says how the
console's own picture, whose pixels are blocks, is shown).

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
| The movie | the overlay, 24-bit mode | **not done**: the display in the mode of three bytes a pixel is left to the software GPU's picture, which the window shows while the player runs ("Remastered in the window"); the title picture after it, in the 16-bit mode, is the remastered picture where a mod has a picture of it (MODS.md, "The title picture") |

**Nothing reads back what the card drew.** The game reads video memory
with `StoreImage`, and that is the software GPU's, which is still
exact. A texture made by drawing polygons into video memory would be
the one thing the mirror could be stale for, and the port counts it: in
the three play tests, 0 textured primitives of 1.5 million read a page
this frame had drawn into.

### How it is proved, with nothing shown

There is no window and no display. The prover
(`src/port/gl/gl_main.c`, built by `tools/port_gl_build.py` into
`build/port-gl/rebushido-gl`) takes an OpenGL 4.5 core context from
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
is a colour, which the card keeps at eight bits a channel with no dither
and the chip at five; a flat polygon's slanting edge is the chip's
("A flat polygon is the chip's to its edges").

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

That table was the boot and the first menus. The whole of `port_game`
at 4 across and 3 down is 109 flat pictures -- the loading screen's
swords and the stage select's slanting panels among them -- and there
the card's rule for a slanting edge broke 18,917 blocks, with a worst
channel of 182 where a portrait cut into strips came out a texel along
at the strips' diagonals. With the chip's own rule and arithmetic for a
flat polygon (above) it is **0** blocks broken, 8,037 more than a step
out and a worst channel of 11.

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

### The provers on a graphics card

The same prover is built for Windows (`python3 tools/port_gl_build.py
--windows`, `wsl bash tools/build.sh port-gl-windows`, into
`build/port-gl/windows/rebushido-gl.exe` with `SDL2.dll` beside it):
`src/port/gl/gl_main.c` with `PORT_GL_SDL`, where SDL makes the OpenGL
4.5 core context on a window that is never shown, and no sound device is
opened. Everything else -- the drawing, the read-back, the comparisons,
`--gl-exact`, `--gl-compare`, `--gl-blocks`, `--gl-tween` -- is the one
file, so the measures are the same code on Mesa in software and on the
card the players have. `port_gl_prove.py`, `port_fps_prove.py` and
`port_stage_prove.py` take `--gpu llvmpipe` (the Linux program, which
they now run with `LIBGL_ALWAYS_SOFTWARE=1` themselves) or `--gpu card`
(the Windows program; the default when Windows' Python runs them, and
from WSL it runs through Windows' interop). The log's `gl run card` line
names the card that drew. Mesa's D3D12 driver in WSL is not a way to the
card: it draws wrong (half the pixels of scale 1 differ, and the flat
screens fail where llvmpipe passes).

**llvmpipe stays the reference** where a number must be exact and the
same on every machine: the tables in this document were measured there,
and a figure quoted against an earlier one is measured there again. The
card is for iterating: the same verdicts, several times sooner.

What may differ on the card, and why the provers hold it all the same:

- **The game's traces do not.** The trace of every plan is the same,
  line for line, on the card as on llvmpipe, with the renderer on and
  off (2,333, 2,457 and 2,334 lines): the game never reads what the card
  drew.
- **The colours of pixels that mix and of shaded polygons** are each
  card's own floating-point arithmetic. At scale 1 with `--gl-exact` the
  card differs from the software GPU in 623,545, 6,805,770 and 648,625
  pixels against llvmpipe's 608,536, 6,786,976 and 634,701 (2% more at
  most), the same few in a hundred within a step. That count is a
  measure, never a gate.
- **The flat screens, block for block**, are held by the same bar on
  both: no block broken up (0 on both) and no channel more than two of
  a five-bit step out (`BLOCK_WORST_MAX`). The card's pixels a step out
  are 8,609, 8,626 and 8,744 against llvmpipe's 8,037, 8,037 and 8,154,
  and its worst channel 11, 11 and 13 against 11, 11 and 14: the card
  rounds a mixed pixel otherwise, within the same two steps.
- **The time a frame takes** is the card's (`us/f` 3, 5 and 2 against
  287, 312 and 274): the numbers of time in the tables are llvmpipe's
  unless they say otherwise.

The provers' wall-clock time, run one at a time on the owner's machine
(the RTX 3090, with other agents' work running beside them, so a few
per cent either way is noise):

| The prover | llvmpipe (WSL) | The card (Windows) |
|---|---|---|
| `port_gl_prove.py` (three plans) | 728 s (496 s with the machine quieter) | 93 s |
| `port_fps_prove.py` (three plans) | 1,087 s (1,025 s) | 264 s |
| `port_stage_prove.py` (38 stages) | 1,767 s | 527 s |

`port_fps_prove.py` holds the same on both: the same frames checked,
the first picture the game's own to the pixel, every corner between its
own ends, the same one frame out of order by the pixels and explained.
Of its time on the card most is the game and the read-back of five
pictures a frame, not the drawing (0.3 ms a picture inside the renderer
against 11 to 19 ms on llvmpipe). `port_stage_prove.py`'s verdicts come
to the same `g_stage_proved` on both: all 38 stages keep the mesh, every trace the same both ways, and the stages' shares of pixels that differ, of areas plainly another colour and of black within a tenth of a per cent of each other (the most that differs on any stage: 76.9% and 77.0%; black 0.599% on both). Its measures of
pixels are each GPU's own -- which of two texels a ground read half a
texel along takes is the last bit of each one's arithmetic -- and the
bar it holds them to (areas of 16 by 16 pixels plainly another colour)
is set well above that speckle on purpose.

### What is not done, and what may be wrong

- The owner has seen it on a real card (an RTX 3090 at 2,560 by 1,440,
  85 Hz, 16 by 9, the smoothing at 2 and the draw distance at `max`) and
  the ground drawn as one mesh came back "really good" ("The ground as
  one mesh"). Everything measured here is still measured with no card at
  all: the pictures are files, drawn by Mesa's software renderer, and
  only what the owner looked at has been seen on a screen.
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
- New lighting is a later step; the shadows are done ("The shadows"),
  but nobody has seen them on a screen either, so `shadows original` is
  the default.
- **Nobody has seen the pictures between the game's frames move**
  ("More pictures a second"), so `frame_rate original` is still the
  default. They were written to files and held to the two frames they
  lie between, by the pixels and by a watched point, and that is all.
  What may show: a face the frame after it has not — something spawned
  or gone — is drawn where it was and waits for the next frame instead
  of moving; the 2D layer, the HUD and the clock stay at the game's rate
  by design; a camera cut shows the game's own picture until the cut is
  over; and a primitive the side channel has no depth for is one of
  those that do not move (about forty of the eleven hundred a fight
  frame has at `draw_distance max`: the fight's own flat layer, and a
  few shaded triangles).
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

**`max` is what the window starts with** (`settings_list.h`, and the settings
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

The owner has seen it on a real card (an RTX 3090 at 2,560 by 1,440, 16
by 9, the smoothing at 2 and the draw distance at `max`): "That works and
looks really good! Lets continue with that". So **it is what Remastered
draws the ground with, with nothing said** (`stage_mesh = on`), on every
one of the game's 38 stages, each of them measured against the console's
own tiles; a stage the measurements did not hold would keep the tiles of
its own accord and the log would say so.

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

**What is left out, as the console leaves it out.** Before it draws a
tile's face the renderer makes two tests of the whole face
(`render_EmitTile`), and the mesh makes both. Every vertex carries the
four corners the face's record names, so the card can ask of each face
what the renderer asked, once, of the record and not of the pieces a cut
face breaks into:

- **the face turned away from the camera.** The coprocessor's `NCLIP`
  takes the cross product of the face's first three corners *on the
  screen* and the renderer keeps the face only where it comes out one
  way (`render_FaceFront`). Divided by their own depths, that sign is
  the sign of the volume the three corners make with the eye, which the
  shader works out in view space: it is the same answer wherever the
  corners are, including a face that crosses the plane of the eye. It is
  not the card's own leaving out of the back of a triangle, which was
  tried and took half of a stage's walls with it: a quad is drawn as two
  triangles that wind opposite ways, so by the card's rule one of every
  two would always go. The renderer asks it once, of the first three
  corners, and so does the mesh.
- **the face nearer than the cutoff.** The depth a tile's face is sorted
  at is its *farthest* corner's, and a face whose slot comes out below
  `RENDER_OT_CUTOFF` is the ground between the camera and the fight,
  which the renderer drops. The shader works the same depth out from the
  same four corners. (A test of each pixel's own depth was tried and
  cuts into a face the game drew whole: the ground at the camera's feet
  went with it. A ball round the face was tried next and is too
  generous: on the terraced fields it let a wall of the near terrace
  through, blown up across the whole picture.)

What the mesh does **not** do is the renderer's two screen tests -- a
face every corner of which is off the top or the left of the console's
320 by 240, or every corner below its bottom. Those throw away what
cannot be seen on a console screen, and a wider or taller picture can
see some of it.

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

**And the fog runs on instead of stepping.** One copy of the colours
covers a whole band of depth, so a long floor running away from the
camera carries a straight bright line across it where one band ends and
the next begins -- the parking lot is the plain case, and the owner saw
it on a real card. The mesh reads **both** of the levels the fragment's
depth lies between and mixes them by how far between them it is, **about
the middle of the band**: the console gives a whole band one level's
colours, so that is where the level belongs, and the colours run from the
band before's middle to the band after's. The mean over a band is then
the console's own and there is no line at either end of it. (Running them
from one band's edge to the next instead was tried first, and leaves the
ground half a band less fogged than the console had it: on the abandoned
shipyard, whose ground is wide and deeply fogged, that alone marked 5.08%
of the picture as plainly another colour, where centring it marks 1.07%.)
It costs one more read of a colour table a sample; the index into the
table is read once for both.

**Depth: the scene is held against the ground.** The mesh writes a depth
of its own, in the world's own units, into a depth buffer the renderer
keeps beside the colour buffer, and **every polygon of the scene is
tested against it** (`gl_draw.c`, `PORT_GL_DEPTH_RANGE`). The mesh is
drawn where the first of the frame's tile primitives would have been,
which is the farthest of them, so without the test everything of the
scene is painted over the whole of the ground -- and a piece of ground
that stands between the camera and a fighter no longer veils him, which
on the terraced fields showed a fence the console hides behind a terrace.

- **The one scale both are in.** The coprocessor's depth for a corner is
  the depth in the world shifted down by the scale the renderer chose for
  the thing it was drawing, which it picks by distance so that sixteen
  bits are enough (`scale_shift`, `render_SetupNode`; the tiles have one
  of their own, `g_scene_settings.far_shift`). The side channel carries
  that scale with the primitive (`PortRemasterPrim.depth_shift`, written
  where a face is attached to a primitive), so the shader can put it back
  and compare like with like. It is not `RENDER_DEPTH_SHIFT`, which is
  the shift a depth takes into the ordering table and is `3 - scale` for
  a node's parts and the scale itself for a skinned mesh.
- **Per pixel, in perspective.** A corner's depth is already the
  primitive's `w`, and the card interpolates one over `w` evenly across
  the screen, so the shader takes the depth back out of `gl_FragCoord.w`:
  that is the depth a straight line through the world has at that pixel,
  not a corner's.
- **Nothing of the scene writes the depth buffer.** The test is `LESS`
  with the writing off, so the order the scene's own primitives are
  painted in -- the ordering table's, which is the whole of how the game
  sorts -- is exactly what it was. Only the ground can hide anything,
  and a fighter it hides by its slots (below).
- **A decal's offset keeps what lies on the ground.** A fighter's shadow
  is his model flattened onto the ground and has the ground's own depth
  to within the coprocessor's rounding; so have his feet, his blood and a
  weapon he dropped. Each is taken to be `PORT_GL_DEPTH_BIAS` (128 world
  units, about a fighter's width) nearer than it is, which is nothing
  beside the thousands of units a piece of ground that veils him stands
  in front of him by.
- **A fighter is held by the console's own rule, not by depth.** The
  console hid nothing by depth: it painted each piece of ground at the
  ordering-table slot of its *farthest* corner, and a fighter's face at
  its own. So a terrace's face that stands in front of a fighter but
  runs back past him was painted *before* him, and he was drawn over
  it; held to the depth of each pixel, the port hid him behind it. A
  fighter's own -- his body, his cloth, a weapon, his model in the tiny
  mode; the renderer says which (`port_RemasterNoteFighter`, by the
  node's mesh block) -- is therefore not held to the depth at all. The
  mesh writes, beside its colour, the slot of the face each pixel shows
  (the same `slot_of` its cutoff uses), into a buffer of its own
  (`port_GlGroundSlotsBegin`), and the side channel carries the slot
  each primitive was linked at (`PortRemasterPrim.ot_slot`). A pixel of
  a fighter is left out where the ground's face there has a nearer slot
  than his face: the face the console painted after him. That keeps
  what the mesh is for -- a piece of ground whose every corner is
  nearer than the fighter still veils him -- and drops what the depth
  added. It is the console's rule at the size of a whole face: the
  console cut a near tile into four or sixteen pieces, each linked at
  its own farthest corner, so a piece by his feet could be painted
  over them where the whole face is not; that is the console cutting
  off the soles of a fighter's feet, and is not copied. A fighter's
  polygon writes its own depth as it is painted all the same
  (`port_GlMarksDepth`), for the stage's flames below. An effect has
  no node and no slot and is not hidden by the slots; a stage's flame
  is held by the depth (below, "The effects"); a shadow lies on the ground
  and keeps the depth test with its offset, as the stage's own pieces
  do, which is what still hides a fence behind a terrace.
- **A batch is held as it was when its primitives came.** The last batch
  of a frame -- the fighters, as often as not, and the 2D layer -- is
  drawn at the vertical blank, after the game has begun the next frame,
  and asked then whether the mesh had been drawn it was told no: the
  last batch of every frame was held against nothing at all. The answer
  is now the batch's own (`PortGlState.is_held`).
- **A primitive the side channel has no depth for** -- the 2D layer, the
  fonts, the menus, a polygon of the scene whose corners were not written
  down -- is put at the very front and is never hidden: it was drawn
  where the console drew it and has nothing to be behind.
- **The test is off in a frame the mesh did not draw**, and off entirely
  where the mesh is: the depth buffer then holds the frame before's, and
  there is nothing to hold anything against. What the game painted before
  the mesh -- the backdrop, the sky -- is farther than any of the ground
  and is drawn before the test is turned on.
- **The depth buffer is attached to the colour buffer only where the
  mesh will write it** (`port_GlAttachDepth`), and not to save memory:
  Mesa's software driver takes another path through its rasteriser when
  a framebuffer has a depth attachment at all, and a few hundred pixels
  of a fight picture then land on the other side of a half-texel. The
  picture at scale 1 is held to the software GPU's pixel for pixel
  ("How it is proved, with nothing shown"), so a run that draws no mesh
  must have no depth buffer either; with it attached always, `port_game`
  differed in 623,690 pixels instead of 612,810, in 13 of its 183
  pictures, all of them in a fight.

**Where two tiles lie over each other.** The tiles are cells of 2,000
units, but a tile's mesh is 2,040 across, so it lies over each
neighbour's by a strip 40 units wide, floors and walls alike (the
terraced fields have 1,406 pairs of faces in one plane in such strips,
the Meikyokan dojo 1,922, the parking lot 2,066). On the console the
painter's order hid it: of two faces the one sorted nearer -- whose
*farthest* corner is nearer, the depth a tile's face is linked at -- is
painted over the other. In one buffer with a depth test the two are the
same plane, and which of them a pixel shows was left to the rounding of
each one's depth, a step of the buffer's (a 64th of a world unit) being
about the size of that rounding: on a real card the winner flipped pixel
by pixel as the camera moved, a texture crawling along every seam. So
**every face is pushed back by a 256th of its farthest corner's depth**
(`v_push`, the depth that is written and not the one the fog reads): the
face the console sorted nearer wins the strip, as it did there, by a
margin that grows with the depth as the rounding does. Where the two
farthest corners are as far -- a camera looking straight along a seam --
**a seam number** settles it: every tile is given a small one when the
mesh is built, no two tiles whose footprints overlap the same
(`port_GlStageSeams`, a greedy colouring), and is pushed back a quarter
of a unit a number. The push is 0.4% of a depth: a fighter 5,000 units
off is still hidden by ground more than 20 units in front of him, and a
shadow or a decal is only the less likely to be eaten.

Measured with `port_stage_prove.py` on all 38 stages, held to the
console's tiles: every stage keeps the mesh, the areas of plainly other
colour summed over the stages fall from 12.40% to 11.21% (the railway
1.04% to 0.71%, the abandoned ship 0.53% to 0.34%, the Meikyokan dojo
0.08% to nothing, practice 1 0.08% to 0.04%; the worst rise, the water
tower dam, 0.06% to 0.09%), and the mesh's own dots -- a pixel apart
from all four of its neighbours where the tiles' picture has none --
from 69,097 to 54,152 (`build/stagemesh/verdict.txt`). What was tried
first, on the six stages with the most such faces: the seam number
alone, one world unit a number, mends the crawl but gives a strip to one
tile from every camera and so matches the tiles less well (the orange
fields 1.37% of the pixels against 1.12% before, practice 1 and the dojo
no better); pushing by the farthest corner matches them as well or
better, and a 256th left fewer dots than a 1,024th. Trimming each
tile's faces to its own cell instead was not done: it cuts faces and
texture points the game never cut, and which tile the console showed in
a strip depends on the camera, which no cut made once can follow.

**What it is worth, measured.** The mesh held against itself with the
test on and off (`--other nodepth`), four pictures of each of five
stages: the terraced fields 0.83% of pixels, the purple cavern 0.20%, the
parking lot 0.02%, the castle rooftop and the practice floor nothing at
all. Of the terraced fields' 0.83%, 0.016% is black where the mesh
without the test drew something: the test takes almost nothing away
altogether, it puts the ground in front of it. Looked at, every one of
those pixels is a piece of the stage the console's own tiles hide and
the mesh without the test did not -- a fence behind a terrace is the
plain case -- and nothing of a fighter, a shadow or a dropped weapon is
eaten anywhere.

That measurement was taken while the last batch of every frame escaped
the test (above), which is most of the fighters; the owner, playing,
saw them hidden by the terraces. Measured again with every batch held,
on the terraced fields' tour at scale 1 and every sixth blank (571
pictures): held by the depth of each pixel, 99 pixels of a fighter in
nine pictures were hidden by a terrace that the console's own tiles drew
him over; held by the slots, none, and the pictures are the tiles'
there. Against never hiding a fighter at all, the slots hide 45 pixels
in one picture -- a sword's tip behind a face of the ground wholly
nearer than it -- which is the veiling the mesh is for. What the slots
do not copy is measured too: the console's tiles paint over a fighter
in 348 of the tour's 4,200 blanks, a few hundred pixels each, and the
pictures looked at show it to be the feet and the hems of the
fighters, under the pieces a near tile is cut into.

**The texels a fragment covers are averaged**, by the same rule and the
same arithmetic as the renderer's own polygons of the scene (`gl_draw.c`,
`steps`): the ground runs away from the camera, so far off one fragment
covers several texels, and reading one of them is the noise that crawls
as the camera turns. The mesh is drawn only when the picture is not being
held to the console's own pixels, so it always averages; the ground and
everything standing on it are read the same way.

**A tile the game opens up.** A tile that can hide a fighter from the
camera is drawn half-transparent while it is in the way
(`STAGE_TILE_OPEN`), and the renderer links it into the ordering table
at its own depth, which is nearer than the fighter it is meant to veil.
The mesh is drawn where the *farthest* tile of the frame was, so such a
face drawn with the mesh would be painted over by that very fighter. So
it is not: the mesh leaves out every face of an open tile (the shader is
told which tiles are open where the mesh is drawn, from `draw_flags`),
and those tiles' primitives are left on the game's own path, where they
mix and sort exactly as they did. Which tiles are open is taken with the
camera, where the renderer draws the tiles (`PortStageView.open`), and
not read from the tiles when the mesh is drawn: the game sets the flags
again for the next frame before a picture of this one is drawn between
two frames, and read there a tile the next frame opens was left out of
the mesh while the frame on show drew it whole -- and no primitive of it
was drawn either, so a wall went missing for a picture. That was the
near wall of the cavern in `port_other_fight` (blanks 3260 and 3374,
83,490 and 23,078 pixels of the picture at phase 0 apart from the game's
own frame); taken with the camera, every picture at phase 0 of all three
play tests is the game's own frame to the pixel. The same goes for a face whose own
code says it mixes. With no tile open -- which is most frames of most
stages -- the mesh is one draw with no blending at all.

**The pictures between two frames.** A tile's primitive is no longer
written down at all (`gl_tween.c`): there is nothing to pair, and they
were thousands of a frame's. In their place the mesh is drawn again at
the event the first of them came at, **with the camera between the two
frames** -- the rotation, the place, the projection distance and the
screen offsets each a part of the way along. That is the whole of what
moves in the ground, so the holes a real card showed in the floor of a
picture between two frames cannot happen: there are no tiles to pair.

**Which stage is the mesh's.** `port_RemasterStageNumber` is the stage
whose data is in the arena (`resource_GetStage`) and not the fight's own
(`g_fight_stage`), because a cutscene can draw a stage of its own
(`CUTSCENE_STAGE`). Every one of the game's 38 stages is measured and
every one of them keeps the mesh.

**What falls back to the tiles**, so that nothing is drawn wrongly where
anything is uncertain: **a stage the mesh is not measured good on**
(`g_stage_proved` in `gl_stage.c`, one line per stage, built from the
prover's `verdict.txt`); no stage loaded at all; the setting off; the
original picture; the comparison with the software GPU (`--gl-exact`,
which wants the game's own primitives); a stage with more tiles than the
mesh holds; a stage whose tiles or meshes have moved and not been read
again (the mesh is built once more); and every frame before the first in
which the renderer drew a tile at all. **The run's log says which path
each stage took**, one line each time the stage changes, naming the stage
and the reason (`port_GlStageNews`), so a fallback is never silent.

Nothing of the stage but its ground is the mesh's:
the pieces that stand on it, the surf, the flames, the backdrop and the
scrolling textures are the game's own nodes and primitives and are drawn
exactly as they were. The stages whose ground changes are none: the
hooks a stage has of its own (`stage_hooks.c`) move props, scroll a
texture in video memory or draw a backdrop, and the surf is a node; the
one thing that moves a tile's mesh is the arena moving the stage's block
(`resource_OnStageMoved`), which says so.

**The pieces that are not ground do not pop in or change detail** at
`draw_distance far` or `max`, so there is nothing for the mesh to mend
there. A piece is collected from the same cells as a tile (`stage_Draw`,
`g_stage_seen_nodes`), so where no cell comes and goes no piece does
either: the prover counts the pieces a frame queued at both ends of a
whole run and they are equal (`nodes` and `nodes_least` of `gl run
limits`), as `tiles` and `tiles_least` are. And nothing halves a piece's
texture points or cuts its faces by distance -- `render_TileShrink` and
the cutting are the tiles' alone; a node is the same mesh at any
distance, and all that changes with the camera is the scale its
coordinates are projected at (`scale_shift`), which does not change what
is drawn. At `draw_distance original` the cells themselves come and go
with the way the camera looks, pieces with them, which is what that
setting is and why the window does not start with it.

**What may still be wrong.**

- **A face of the ground the console never drew is drawn at the edges
  of the picture**, on purpose: the console's two screen tests are not
  made (above), so what was off the top or the left of its 320 by 240 is
  in a wider picture.
- **The depth test is of the ground alone.** The scene is held against
  the ground and the ground against itself; two things of the scene are
  sorted against each other by the ordering table, as the console sorts
  them. So a fighter's arm through another fighter is what it always
  was.
- **The offset that keeps a shadow from being eaten is one number** (128
  world units) and not a slope: a shadow on ground that runs steeply away
  from the camera has more of the ground's own depth across it than that,
  in principle. Nothing of the kind showed in the stages measured.

**How it is proved** (`tools/port_stage_prove.py`, which needs a graphics
card and so is no check of CI's): **every stage the game can load**, 0 to
37, is run twice over the same tour -- the first fighter run across the
stage, which swings the camera right round -- once with the mesh and once
without, and the two runs' traces and pictures are held to each other. At
`draw_distance max`, the console's shape, twice its resolution, four
pictures of each run, all of them **in the fight**: the menus before it
are the same picture either way and say nothing about the ground.

**The six the menus cannot choose.** Stages 32 to 37 are the modes' own
-- the two team battle courts, chanbara's hall, the two practice floors
(35 is the dojo with the game's logo painted on the boards) and 37, which
no card names: the castle rooftop as link mode fights on it (`select.c`,
`SELECT_STAGE_LINK_TO`; `--fight 37`). A versus fight cannot be set on them, so they
had never been measured. They are reached the same way the other 32 are:
a `poke` line of the plan holds both the stage the menus chose and the
fight's own at that number from the character select to the end of the
run (`port_remaster_prove.plan_for`), and the fight's loading step reads
the stage file for it like any other (`ndata_ReadStage`, `S00` to `S37`).
A poke **changes what the game computes**, so these runs are no proof
that play is untouched -- `port_gl_prove.py` is, with the three play
tests and no poke -- but both runs of a pair have the same poke, so what
they say about the ground holds.

**`--stage-mesh every`** is what the run with the mesh is told: it draws
the mesh on whatever stage the run reaches, whatever `g_stage_proved`
says, which is what measuring a stage needs -- `on` would leave out a
stage the table does not hold yet and measure nothing at all. `on` is
what the player gets, and running the prover with `--mesh on` is how to
check that the table itself says what it should.

**How a difference is counted.** Counting the pixels that differ at all
says nothing: 41% of them do, and a ground of noise read half a texel
along differs in as many pixels as a wall drawn where no wall is. What
is counted instead is **a cell of 16 by 16 pixels whose two means are
more than 24 apart, in a connected area of four such cells or more**.
The two sizes were measured, not guessed: three pairs of pictures at
every cell from 4 to 16 and every step from 16 to 48, one pair known
wrong (an early mesh drew a wall of the terraced fields across the whole
picture) and two known right. At 16 and 24 the wrong one marks 45% of
its cells and the two right ones mark none at all; below that size the
speckle is marked too and runs together into areas of any size.

| | |
|---|---|
| Stages | **38: every stage the game can load** |
| **The trace of every watched value, the mesh on and off** | **the same on all 38** |
| Pictures compared | 152, 186,777,600 pixels |
| Pixels that differ at all | 57.1% |
| **In areas of plainly other colour** | **609,337 (0.326%)** |
| The worst stage | the orange tree fields, 1.12% |
| Stages with none at all | the main shrine, the snake cliff, Kimigaura coast, the kabuki stage, the Shainto gate, stage 37 |
| **Black where the tiles drew the stage** | **216,742 (0.116%)**, worst the terraced fields at 0.60% |
| Drawn where the tiles drew nothing | 208,309 (0.112%) |
| Vertices a frame, the mean over a run | 2,459 from the tiles, 2,923 with the mesh |
| The verdict stage by stage | `build/stagemesh/verdict.txt` |

**The bar, and what is on the other side of it.** A stage keeps the mesh
where its trace is the same, its areas of plainly other colour are under
5% of the pixels, the largest of them is under 60,000 pixels, and under
1% of the pixels are black where the tiles drew (`verdict_of`). The whole
measured range is 0.00% to 1.12%, and the one picture known to be wrong
measures 45%: the bar sits between them, not at the edge of what passes.
**All 38 stages pass it**, and `g_stage_proved` in `gl_stage.c` says so
stage by stage; a stage that stopped passing would be a 0 there and would
keep the game's own tiles with nothing else changed, and say so in the
log. (The bar is not slack: the fog mixed from one band's edge to the
next instead of about the band's middle put the abandoned shipyard at
5.08% and it fell out, which is how that was found.)

**A custom arena** stands on its host's number but is a ground of its own,
so it is held to the prover by itself: `--arena FILE.arena` puts the file
alone in a folder of mods, runs the tour on its host with `--arena`, and
writes the line of `g_arena_proved` (`gl_stage.c`) it comes to: the
arena's key and the FNV-1a of its whole file, so that the verdict is of
the arena as measured and of no other. While an arena is in play the mesh
takes its verdict, not the host's; an arena not in the table (a mod's
own, or one changed since) keeps the game's own tiles unless
`stage_mesh = every`, and the log says so. The sample, `courtyard`, keeps
the mesh (trace the same, areas 0.620%, the largest 10,752 pixels, black
0.001%), and so does the compiler's test arena, the gallery (0.651%,
4,352, 0.001%), which is not shipped and so not in the table;
`tests/rebushido/arena.py` fails when the sample as compiled is no longer
the file the table names.

**What the differences are**, looked at picture by picture and not only
counted: the ground is in both, in the same place, with the same things
standing on it, and nothing of a fighter, a shadow or a dropped weapon is
cut. What is marked is the seams between a floor's boards and the
texture's own speckle, where a fraction of a pixel reads the next texel
along; the far edge of the ground, where the mesh carries on past where
the tiles stopped; and a piece of the stage the ground now veils, which
is the console's own picture and not the mesh's fault. The side-by-side
pictures are what to look at: `--pairs N` writes them at the prover's own
scale, and `build/stagemesh2/window-*.png` are eight of them at 2,560 by
1,369, the owner's own window size, 16 by 9, through SDL's `offscreen`
driver on Mesa's software renderer. `--mark N` writes the mesh's own
picture with the cells that count reddened on it, which tells a wrongly
drawn face from speckle at a glance.

**The pictures between two frames** (`tools/port_fps_prove.py
--stage-mesh on`, the three play tests, every frame drawn as five
pictures): the trace with the pictures on and off is the same on all
three, 2,333, 2,457 and 2,334 lines. **A picture drawn at phase 0 --
which must be the game's own frame again -- is now the game's own frame
to the pixel**: 0 of 856,473,600 pixels differ on `port_game` and on
`port_bugfixes`, where before the mesh the ground's tiles had to be
paired with the next frame's and 0.39% of the pixels differed. A frame
has 923 primitives to pair instead of the ground's thousands, 679 of
them moved and 127 unpaired.

What the mesh does not mend is what was already there: the pictures of
`port_other_fight` that do not lie between their two frames went from 11
to 23 of 817 (2.8%), and the watched point that is not between its two
ends from 14 to 14 -- on `port_game` that count falls from 16 to 6. Those
were taken for a face the next frame has not, drawn where it is rather
than moved, and are not the ground's. Measured again later ("More
pictures a second", "What made a fighter standing still move"): the
watched point was off its line because it was a different primitive at
different phases, or one held where it is at the last; it is now one
primitive chosen for the pair of frames, and `port_game` and
`port_bugfixes` hold every check. `port_other_fight`'s out of order
were the cave's railings a pixel off between two frames, and are gone
("More pictures a second", "The railings that jumped a pixel").

**What it costs.** On this machine, which has no graphics card at all
(Mesa's llvmpipe) and was busy with other work throughout, a run of
4,200 blanks over all 38 stages took 27.7 s with the tiles and 29.0 s
with the mesh on the mean, and the mesh adds about 460 vertices to a
frame (2,459 to 2,923). It is drawn in one go: one draw of the whole
stage, with no blending, and one more read of a colour table a sample
for the fog.

**A mod's pictures of the stage's pages.** A mod may give a picture of a
whole texture page of the stage through one of its colour tables
([`MODS.md`](MODS.md), "The scene's textures by page"); the mesh reads it
where there is one (`src/port/gl/gl_scene.c`), each face inside its own
rectangle of texels, which every vertex carries, and with its colours
made to follow the fog's two levels as the game's texels do. With no mod
the mesh draws exactly what it did.
### The stage's pieces as meshes

The owner, on a real card at 85 Hz with the pictures between frames on:
"railings and fences are acting jittery and strangely on a lot of
maps." Everything of a stage that stands on the ground and is not a tile
-- the railings, the fences, the walls, the posts, the roofs -- is a
**piece** (`StageNode`, `stage.h`): a node with a mesh, a place and three
angles, read from the stage file (`stage_ReadCells`), queued from the
cells the camera sees (`stage_Draw`) and projected again every frame by
`render_DrawQueue`. With the ground drawn as one mesh, the pieces were
the last of the stage still drawn from the game's own primitives, and
three things made them move where nothing moved:

- **Each corner is the coprocessor's projection of that frame**, from a
  matrix the renderer rounds to 1.12 and a node's scale chosen by its
  distance (`render_SetupNode`): a corner a pixel of the console off,
  and on a thin rail seen edge on that is the whole rail.
- **The pictures between two frames pair a face with a face**
  (`gl_tween.c`): a face one frame has and the next has not -- the
  console drops one turned away, off its screen or with no depth -- is
  held where it is and then jumps; on a railing, which is many small
  faces at every angle, that is the "strangely".
- **What is left out is the console's per face**, so a fence's faces come
  and go one by one at the edges of the picture.

**`stage_props_mesh on` builds every piece into the ground's mesh**
(`src/port/gl/gl_stage.c`), in the world, when the stage is first drawn,
and draws it with the ground, by the same camera: still where the camera
is still, and in a picture between two frames moved by the camera alone.
The game goes on drawing its pieces exactly as it did; the backend leaves
out the primitives of a piece the mesh draws, as it leaves out the
tiles'.

**What is read, and how.** A piece's mesh is read as `render_DrawQueue`
reads it -- parts, each a count of points and the form they are kept in
(threes of SVECTORs, or packed halfwords), then groups of records, a
group's type naming the routine that draws it -- and each record as that
routine reads it (`render_EmitFT3` and its eleven neighbours): the
corners among the part's points, the texture page, colour table and
points, and the normals. A point is put into the world by the piece's
own matrix and place, or its place alone where the renderer draws it
unrotated (the stage file's angles test, `stage_ReadCells`). A face is
lit as the routine lights it: the coprocessor's own `NCS` on the
record's normal, through the light matrix turned into the piece's space
by the game's own `math_MulMatrix` (`render_LoadTurnedLights`), with the
whole coprocessor put back afterwards; a Gouraud face at each corner,
the first corner of a quad by the face's own normal as `render_EmitGT4`
does; the back of a face drawn from both sides with its normal turned
bit for bit, and of a `GT4` the second and fourth corners' x and y not
at all, as the renderer has it; the colour and code of a `GT` group, and
of the packed table's `FT3`, its first record's. A face whose code says
it mixes is not built: it stays the game's primitive, at its own place
in the painting. A piece holding any group of another type (the black
faces of a prebuilt mesh, a tinted one, outlines) is not built at all
and keeps its own primitives; the log says how many pieces each stage's
mesh holds. **Every piece of all 38 stages is read**: 412 pieces on the 34 stages
that have any, 6,043 faces, none left to the game's primitives.

**What is left out, as the console leaves it out**, of each face, in the
shader, from the face's own corners which every vertex carries (as the
ground's): a face turned away (the sign of the three first corners'
volume with the eye, `render_FaceFront`), or for the back of a two-sided
face one turned towards the camera; a face whose depth -- the plain
average of its corners', each held at 0 as the coprocessor holds a depth
behind the eye -- is none; and, for the packed table's faces and the
unlit ones alone, a face whose ordering-table slot is nearer than the
cutoff. A piece is drawn only in a frame that queued it (the cells), so
`draw_distance` holds for it as for the ground; which pieces those are
is taken with the camera where the renderer draws the tiles
(`PortStageView.pieces`), so a picture between two frames draws the
pieces of the frame on show.

**A piece the game changes keeps its own primitives.** The pieces are
taken down once, when the stage is first drawn (`port_RemasterNotePieces`,
`src/port/remaster.c`: its mesh, matrix and light matrix); a piece whose
kind, mesh, flags, lights or world matrix are not what they were is left
out of the mesh from that frame on and drawn from the game's faces -- a
bamboo cut down on the bamboo thicket, a sign on the bus stop, what
remains of a post on the lumberyard (`stage_CutProp`, which makes the
piece what remains and moves it).

**Where it sits in the painting.** A piece's face is sorted by the
console at its average depth, not its farthest corner's as a tile is, and
that is what it is held by: it writes its depth into the depth buffer
pushed back by a 256th of that average (so of a piece and the ground, or
two pieces, in one plane the one the console painted later wins), and
its slot into the ground's slots (`render_OtIndex`: a node's depth goes
into the table shifted by three in all, its scale undone), so a fighter
in front of a railing is drawn over it and one behind it is hidden by
it, by the console's own order. The scene is held against it as against
the ground. In the shadows' mask a piece is marked as the game's own
faces were (`port_GlStageDrawOccluders`), so a shadow on the ground
behind a railing does not darken the railing. The walls seen through
(`see_through`) thin a piece as they thin the ground, and a mod's
pictures of the stage's pages (`gl_scene.c`) are read for a piece's
faces as for the ground's. (A smoother lighting of the ground, when it
comes, has the pieces' normals to light: the hook is the colour each
vertex carries, made in one place, `port_GlStagePieceLight`.)

**How it is proved.** The pieces against the game's own primitives,
stage by stage (`tools/port_stage_prove.py --props`, into
`build/stageprops/`): both runs draw the ground from the mesh, one the
pieces too, by the ground's own bar, four pictures of each stage's tour at
`draw_distance max`, twice the console's resolution:

| | |
|---|---|
| Stages | **38, every one kept** (`g_stage_props_proved`, `build/stageprops/verdict.txt`) |
| **The trace, pieces from the mesh and from the game** | **the same on all 38** |
| Pixels that differ at all | 3.00% |
| In areas of plainly other colour | 0.14% on the mean; the most the railway's 1.04%, the largest one area the purple cavern's 40,192 pixels (the bar: 5% and 60,000) |
| Black where the game's faces drew | 0.030% |
| Drawn where the game's faces drew nothing | 0.045% |
| Vertices a frame, the mean over a run | 3,351 with the game's faces, 3,280 with the mesh |

Looked at, what differs is a piece's edge a fraction of a pixel along
(the mesh's corners are where the projection puts them, not held to the
console's whole pixels), and the rails nearest the camera: a face that
crosses the plane of the eye is drawn by the console with a corner
thrown wherever its projection put it, so the purple cavern's near rail
breaks into pieces, and the mesh draws it whole, cut at the near plane.

**How still it stands** (`tools/port_props_prove.py`): each stage's tour
run three times, each frame drawn as five pictures -- the pieces from
the game's primitives, from the mesh, and left out (which says which
pixels are theirs) -- and over a piece's pixels in neighbouring pictures
the mean change of a pixel from one picture to the next (`churn`) and
the second difference of three in a row (`jolt`: what moves smoothly
has little of it, a corner that wobbles or a face held and then jumping
a lot). Measured at blank 3,020 of the tour, eight blanks, at scale 2
and 16 by 9 (1,704 by 960), the pieces from the game's primitives and
from the mesh:

| | churn | jolt | jumps (per 1,000 pixels) | primitives paired a frame |
|---|---|---|---|---|
| the lost bridge, nobody moving | 2.44 -> **1.44** | 5.00 -> **2.91** | 13.9 -> **6.9** | 909 -> 876 |
| the lost bridge, the tour | 7.92 -> 7.74 | 14.94 -> 14.59 | 33.3 -> 32.0 | 909 -> 876 |
| the building rooftop, nobody moving | 4.50 -> 4.64 | 8.71 -> 9.51 | 32.5 -> 36.2 | 931 -> 879 |
| the building rooftop, the tour | 124.8 -> 119.4 | 175.0 -> 165.0 | 541 -> 513 | 928 -> 878 |

Where the camera barely moves -- which is where jitter is seen -- the
railings of the lost bridge change 41% less from picture to picture and
have half as many jumps; in the difference of two pictures between
frames, the top rail of the rooftop's railing moves as one line with the
game's primitives and as the ground's own speckle with the mesh. What is
left in the counts is not the pieces' corners: on the rooftop the
measure is ruled by the city's lights behind the railing, a backdrop
piece whose texture of single bright texels shimmers as any texture
smaller than a pixel does, and by the camera itself, which is never
quite still while the fighters stand; with the camera moving, the
change of a pixel is the motion itself. Every trace of the three runs
of each is the same.

`stage_props_mesh` is **on with nothing said**, and needs the ground's
mesh: where the ground is the game's tiles the pieces are too. A stage
the measurements did not hold is a 0 in `g_stage_props_proved` and keeps
the game's faces, and the log says which path a stage's pieces took.
`--stage-props every|on|off|hide` is the prover's (`hide` leaves them
out, for the mask).

**What is left.** The pieces are the game's own shapes: a railing is
still a few flat faces with a painted texture. Remodelled pieces, as the
fighters have, would be a mod's: a file for a piece's mesh read in the
place of the stage's, which this path is ready for (the faces are built
in one place from the piece's matrix). A fence's cut-out texture is cut
at its texels' edges as the game's own faces were; with `antialiasing`
the edges are smoothed as every other edge is.
### The sky

**What the console has.** No sky. A fight's frame begins with a fill of
the stage's colour of the distance (`light_SetFarColor`, `light.c`), and
that colour is what shows wherever the stage's geometry does not cover
the picture. On every one of the 38 stages it is black or nearly
(`0x000000` to `0x101020`): the game is set at night, and above an open
stage there is a flat dark. Only the castle rooftop has more: a backdrop
of a sky with a full moon, a prop kept a fixed way off from the camera
(`stage_DrawBackdrop`, `stage_hooks.c`).

**`sky = night`** (`src/port/gl/gl_sky.c`) paints a night sky of the
port's own **in the place of that fill and nowhere else**, on the stages
open to it. It is one triangle over the frame, drawn where the fill was
(`port_GlSkyCleared`, from `port_GlWroteVram` when the game's write is
the frame's own one-colour clear), so every primitive of the scene is
painted over it exactly as over the fill: nothing of the stage's own
geometry, its order or its depth changes. Each pixel works out the
direction it looks in from the view the ground's mesh is drawn with --
the coprocessor's rotation, its projection distance and its screen
offsets, carried out of the renderer (`PortStageView`) -- and the sky is
a function of that direction alone, so it turns with the camera and,
being infinitely far, never moves as the camera moves. A picture between
two frames draws it with the camera between them, as it draws the
ground (`port_GlStageViewNow`). It is made of:

- **a gradient** that is exactly the colour of the distance at the
  horizon and a little below it, which is the colour the stage's own fog
  fades to, so the fogged ground runs into the sky with no seam; just
  above the horizon a glow of the stage's own ambient light
  (`LIGHT_SCENERY`'s colour of the dark), darkening upwards to a deep
  night -- a navy sky over the lost bridge, a purple one over the
  cemetery, each from the stage's own lighting;
- **stars**, two layers on the faces of a cube about the eye, a pixel
  wide, fewer towards the horizon;
- **a moon** where the stage's light comes from (the scenery's sun,
  seen from above whichever way its vector is meant), placed low enough
  to be in a fight's view -- the camera looks along the ground and sees
  little more than a fifth of the way up -- with a halo and seas;
- **a few thin clouds**, low and lit from the moon's side, which hide
  the stars behind them.

**What it costs.** The clouds are five octaves of a noise of the
direction, forty hashes a pixel, which made the sky the card's second
dearest pass (3.1 ms a fight frame at 2560 by 1440 with the picture
supersampled, on the owner's RTX 3090). Being a function of the
direction alone, the noise is worked out once, the first time a sky is
painted, into a cube about the eye of six faces of 1,024 texels a side
(`port_GlSkyBakeClouds`, 12 MB), and each pixel reads it there, the
card smoothing between texels; the gradient, the moon, the stars and
the clouds' colours are still worked out at each pixel, so that the
stage's own light colours them. A texel is about a tenth of a degree,
two pixels of that picture; the clouds lie where the noise's finest
octave is several texels across, so they are the same clouds, softened
by the smoothing where they are finest.

**Which stages.** Looked at stage by stage over the prover's tour of
each (`tools/port_stage_prove.py`, the pictures every 300 blanks of all
38): the halls, the dojos, the parking garage, the purple cavern and
the two practice floors keep the game's colour -- a star in the
gap above a hall's walls would be a hole, not a sky -- and so does the
castle rooftop, which has a sky and a moon of its own, and stage 37, a
small model of a castle town seen from afar. The other 28 have it
(`g_sky_open`). The run's log says which, once a stage (`gl sky: stage
N is open`).

**A mod's sky.** A PNG in a mod's `skies/` folder, `stage_NN.png` for
one stage or `default.png` for every open stage without one, is used in
the place of the procedural sky: an equirectangular picture, wrapped
round the eye from left to right and from the zenith at its top to the
nadir at its bottom ([`MODS.md`](MODS.md), "Skies").

**The far edge of the ground.** At `draw_distance max` the ground is
drawn to the stage's own last cell ("How far the stage is drawn"), and
where that edge is in view it is a line against what is beyond it. The
sky does not move the edge, and keeps the colour beyond it the stage's
own colour of the distance -- the band just under the horizon is that
colour -- so the stage's fog, which is pushed out with the distance and
fades the ground towards that same colour, is what hides it; a stage
whose ground ends nearer than its fog does still shows the line, as it
did. What a stage that stands high shows under the horizon (a bridge, a
rooftop) is that same dark.

**What it is not held to.** The provers hold the picture to the
console's own: with nothing said `rebushido-gl` draws the game's fill
(`--sky original`), so `port_gl_prove.py`, `port_fps_prove.py` and
`port_stage_prove.py` measure what they measured before. `--sky night`
draws it. The window starts with `night` -- the owner's call: nobody
has looked at it on a real screen.

| Measured | |
|---|---|
| Stages with a sky | 28 of 38 |
| A frame of a fight | one more draw of three vertices, before the scene |
| The game's trace | untouched: nothing of the game is read but the stage's lighting and the view the ground already takes |
| `port_game` (the building's rooftop) drawn as five pictures a frame, with the sky and the walls seen through | 697 frames checked; phase 0 the game's own frame in every one (0 pixels apart); none out of order, and the watched point off its line in 4 -- the same numbers as without them |
| The sky between two frames | it moves a step a phase: of the picture's top fifth at blank 3000, 909, 1,999, 3,089 and 4,158 pixels apart from phase 0 at phases 1 to 4 |

### Seeing the fighters through the stage

**What the console does.** A tile of the stage that can hide a fighter
from the camera is drawn half-transparent while it is in the way
(`STAGE_TILE_OPEN`, `stage_Draw`): the cells on the line from the camera
to each fighter are marked (`stage_OpenCellsTo`), and a tile of those
cells whose data says it may open (`STAGE_TILE_CAN_OPEN`) is drawn mixed
with what is behind it.
That is the game's own intent -- the player is to see his fighter -- but
it is only for the tiles the stage's data marks, by whole cells and at
half strength, and a face of the stage that the data did not mark hides
a fighter completely. The cavern's near wall in `port_other_fight` is
the plain case: at blank 3260 it stands between the camera and the
second fighter and hides him from the knees down.

**`see_through = on`** (`src/port/gl/gl_see.c`, and the ground mesh's
shader in `gl_stage.c`) thins **every face of the ground's mesh** that
stands between the camera and a fighter, the way modern fighting and
action games do:

- **Where the fighters are.** Each fighter is an upright cylinder about
  the bones the game drew him with (`port_GetFighterView`): its axis at
  the mean of his bones, from the highest of them, with a head's height
  more, down to the lowest, and as wide as his widest bone from that
  axis with a body's width more (at least 420 world units, at most
  1,100).
- **What is in the way.** A pixel of the mesh is in the way of a fighter
  where the line from the camera through it passes, seen from above,
  within his cylinder's reach, between his head and his feet, and
  farther on than the pixel by more than a step (80 to 320 units, softly).
  A face the line meets below his feet is the ground under him and not
  a wall in front of him.
- **Never the floor.** A face that lies flat (its normal within 45
  degrees of upright) and is seen from above is ground someone may stand
  on, and is never thinned; the underside of a roof, seen from below, may
  be.
- **How it is thinned.** A pixel in the way is left out by an ordered
  dither of four by four pixels of the picture, at most four in five of
  them, fewer towards the cylinder's edge: a soft-edged hole through the
  wall the size of the fighter. With `antialiasing` the dither is
  averaged into a fade. The mesh stays one draw with no blending: the
  ground is painted first in the game's order, before the fighter, so a
  wall mixed over him by alpha could not be -- a pixel left out leaves
  the fighter, painted after, to show through.

**It overrides the console's slot rule, on purpose.** A fighter is held
against the ground by the ordering-table slot of the face each pixel
shows ("Depth: the scene is held against the ground"): a face whose
every corner is nearer than he is veils him, as the console painted it
over him. A pixel the see-through leaves out writes neither a depth nor
a slot, so where a wall is thinned the fighter behind it shows through
it, though the console would have painted the wall over him. That is
the point of the setting: the game's own `STAGE_TILE_OPEN` says the
designers meant a fighter behind a wall to be seen, and the slot rule is
kept everywhere else -- a floor is never thinned, so a fighter below the
edge of a terrace that the console hid by the terrace's top is hidden
still. With the setting off the picture is the slot rule's alone.

**What it does not thin.** The stage's pieces that are not ground -- a
fence, a tree, a pillar of the stage's props -- are the game's own
primitives, drawn by the renderer's shader and not the mesh's
(`gl_draw.c`); they are thin, and are left as they are. A tile the game
opens itself is the game's own half-transparent primitive, as before.

**Measured** (`rebushido-gl --see-through on` against `off`, every tenth
blank of the three play tests and every 30th of the prover's tour of
four stages, at scale 1 and 16:9; pictures differing by more than 24 in
a channel):

| Run | Pictures | Changed | Pixels |
|---|---|---|---|
| `port_game` (the building's rooftop) | 367 | 0 | 0 |
| `port_other_fight` (the purple cavern) | 391 | 19 | 738, of which 535 at blank 3260: the cavern's near wall over the second fighter's legs |
| `port_bugfixes` (the building's rooftop) | 370 | 0 | 0 |
| tour of the orange tree fields | 41 | 15 | 9,202: a tree's crown in front of a fighter's sword arm |
| tours of the castle rooftop, the terraced fields, the purple cavern | 41 each | 0 | 0 |

It changes nothing at all where nothing is in the way, which is almost
every picture of a fight: a fight's camera is placed to see both
fighters. The first shape tried, a capsule round the bones, reached
below a fighter's feet and thinned the face of the castle's ridge under
him in all 41 pictures of that tour; the cylinder cut at his feet thins
nothing there. At blank 3260 at twice the scale, drawn again between two
frames, 6,511 pixels of the wall are thinned and the fighter's hakama
shows through it.

The window starts with `on` -- the owner's call, not yet seen on a real
screen. The provers draw it only when asked (`--see-through on`).

### The shadows

**What the game does.** A shadow in this game is not a blob: it is the
caster's own shape. A fighter's model carries a second, simpler mesh for
each bone that casts one, and a node of kind `NODE_KIND_SHADOW` draws it
through a matrix that flattens it onto the ground along the stage's sun
(`light_SetShadowMatrix`, `light_ProjectShadow`, `src/main/scene_frame.c`;
`model_AddShadow`, `src/main/model.c`). The faces come out as **opaque
black polygons** (`render_EmitBlackF3`, `render_EmitBlackF4`), linked
into the ordering table at their own depth like everything else. So the
shadow hides the ground under it, a face over a face is the same black,
and a tile of the ground whose own depth is nearer than the shadow's is
painted **over** it: a fifth of the game's own silhouette is hidden by
the very ground it falls on, in the frames measured here.

**What `shadows soft` does instead** (`src/port/gl/gl_shadow.c`): those
polygons are not painted at all. Their silhouette goes into a mask of
the colour buffer's own resolution and, at the frame's end, the mask
**darkens what it falls on by a factor** -- 55 hundredths with nothing
said -- so the ground keeps its texture under the shadow instead of
being covered. The whole silhouette is darkened, the pieces the console
loses to its own sorting included.

- **At the picture's resolution, not the console's.** The silhouettes
  are drawn into the mask by the same transform the picture's own
  vertex shader uses, so their edges are where the projection really
  put them, and they are sampled as often as the scene is
  (`antialiasing`). The edge is then softened by a **penumbra of three
  pixels of the picture** (`PORT_SHADOW_PENUMBRA`), nine points of a
  tent spread over it, each of them the average of four of the mask's
  own pixels: a soft edge that is the same width however large the
  window is, and a middle that is flat.
- **One darkness, however many shadows overlap.** The mask holds
  coverage and nothing else: a face writes 1 over a face that has
  written 1. Measured over a fight: the darkest any pixel came to is
  0.450 of what it was, which is one shadow's 1 - 55/100 exactly, where
  the two fighters' shadows and the faces within each cross each other
  freely.
- **It never darkens a fighter, a weapon or anything else standing in
  it**, and not by comparing depths. The same mask carries, in its
  alpha, whether something that is **not the stage's ground** painted
  that pixel: every batch of the frame that is not a stage tile and is
  not half-transparent is drawn again into the mask's alpha through the
  picture's own shader (`port_GlDrawOccluders`, `gl_draw.c`), so a texel
  the picture threw away leaves the ground showing and the shadow on it.
  A pixel marked so is left exactly as the picture drew it. The test is
  **what drew the pixel**, which the side channel knows, and not how far
  away it is: a fighter's foot and the ground it stands on are at the
  same depth, which is why depth cannot tell them apart and why the
  first attempt at this painted the darkness over the caster's feet.
  **With the ground drawn as one mesh** ("The ground as one mesh") there
  are no stage tiles among the batches at all: the mesh is drawn outside
  them, so nothing of the ground is ever put into the mask's alpha and
  the shadow falls on it exactly as it falls on the tiles. Nor does the
  mesh's depth hide or double a shadow: the shadow's own faces are taken
  out of the batching before the depth test is reached, and the
  darkening is a pass of its own at the frame's end. Looked at on the
  practice floor and the terraced fields, with `shadows soft` and the
  ground both ways, the two pictures are the same shadow on the same
  ground (`build/stagemesh2/shadow-*.png`).
- **Which primitives are the shadow's** is told by the renderer and not
  guessed from their colour (the ground has black faces of its own):
  `render_DrawQueue` says `port_RemasterNoteShadow` while it walks a
  node of that kind, which is the one place `light_ProjectShadow` is
  called, and the side channel marks every primitive linked until it
  says otherwise (`port_remaster.h`; the host test `test_Shadow`).
- **The depths the side channel carries are not comparable between
  things**: the renderer scales a node's places and depths by a shift it
  chooses from that node's distance (`render_SetupNode`), so a shadow's
  depth and a fighter's are in different scales. Nothing here compares
  them; that was tried first and discarded.
- **When it is laid down.** At the moment the game clears the *next*
  frame's ordering table, which is after everything of this frame has
  been painted and before anything of the next has
  (`port_GlShadowNewFrame`; the pictures between two frames say the same
  of the camera they keep, `gl_tween.c`). A picture drawn between two
  frames is begun and finished in one call and lays its own down at the
  end of it, so the shadows move with their fighters at any frame rate
  and a still scene gives every in-between picture the same shadow.
- **What the game writes into the picture afterwards covers them**: a
  fill, a copy or a load inside the frame's own drawing area that comes
  after a shadow gives those pixels up. Only what lands in that area is
  looked at -- a frame writes video memory a hundred times over and
  nearly all of it is the fog's colour tables at the far end of it.
- **The cost** is one mask the size of the colour buffer (four bytes a
  pixel, asked for the first time a frame has a shadow in it), and three
  passes over **the box the silhouettes are in and not the screen**: the
  silhouettes, the frame's other primitives into the alpha, and the
  darkening. On this machine with no graphics card a run of 4,200 blanks
  took 65 s with the shadows soft and 64 s with the game's own.

**What is not done, and what may be wrong:**

- A wall or a step **between the camera and the shadow** is darkened
  with the ground: the mask is flat and the ground is never one of the
  things that keep the darkening off. The console does not have this,
  because it paints such a tile over its black shadow; it is the price
  of drawing the whole silhouette. With `stage_mesh on` the same is
  true, and there the mesh's own depth buffer would mend it (`gl_stage.c`
  draws the ground with a depth buffer of its own: a later step could
  hold the mask to it).
- The silhouette falls on **whatever is behind the ground** as well: at
  a cliff's edge a shadow that hangs over nothing darkens the colour of
  the distance. The console paints black there too.
- A fighter **hidden behind the ground** still keeps the darkening off
  the pixels it would have covered, because it is drawn into the mask
  whether the picture kept it or not. With `stage_mesh off` that is a
  shadow-shaped hole where a fighter stands behind a near wall; it was
  not seen in any of the frames measured.
- The penumbra is the same width everywhere. Widening it with the
  caster's height above the ground was looked at: the height is there
  (the shadow node's parent and `RENDER_NODE_EXTRA`), but a wide blur
  loses a thin feature -- a sword's shadow a few pixels across -- long
  before it softens a large one, so it needs a mask that carries the
  height and a blur whose radius is read from it after a dilation. Not
  done.
- Nothing of this is lighting: the fighters and the ground are lit by
  the coprocessor exactly as they always were ("What each later step
  needs").

**How it is proved, with nothing shown.** Three runs of the prover
(`build/port-gl/rebushido-gl`, a stage chosen by a `poke` of the plan as
`tools/port_remaster_prove.py` does) at the same blanks on each stage:
the shadows soft, the same picture with the darkening at one hundredth
-- the picture without shadows at all, since the silhouettes are taken
out of it either way -- and the mask itself in the place of the picture
(`--gl-shadow-mask`: red the coverage, green what is not the ground).
Then, over the fifteen pictures of each run, the pixels the darkening
changed and what the mask says they are:

| Stage | Pixels the darkening changed | ... with any of something else in them | ... that are **wholly not** ground | Darkest |
|---|---|---|---|---|
| the orange tree fields (12) | 80,711 of 23,719,680 | 1,047 | **0** | 0.442 |
| the castle rooftop (13) | 30,489 | 1,372 | **0** | 0.452 |
| the terraced fields (15) | 34,902 | 1,373 | **0** | 0.446 |
| ... with `stage_mesh on` (15) | 34,947 | 1,381 | **0** | 0.443 |
| the practice floor (35) | 55,612 | 1,270 | **0** | 0.450 |
| ... with `stage_mesh on` (35) | 55,612 | 1,270 | **0** | 0.450 |

(The practice floor is drawn from the game's own tiles whichever way
`stage_mesh` is set -- no versus fight can be held there, so nothing has
measured its mesh -- which is why its two rows are the same number.)

**In the pictures between two frames** (`--gl-tween 5`, the same tour of
the practice floor): the first phase of every frame, which is the frame
on show drawn again as it is, **is the game's own picture to the pixel**
with the shadows soft -- 377 frames, 616,711,680 pixels, 0 different,
which is what the picture gives with the game's own shadows -- and no
picture between two frames is out of order. So a shadow is laid down once a
picture, moves with its fighter at any frame rate, and a scene that does
not move gives every picture the same shadow. (It was not so at first:
the vertices a picture keeps for the mask were forgotten only when the
next frame began, so the game's own frame was given the picture before
it as well and a rim of 1,250 pixels a frame came out darker than it
should. `port_GlForgetOccluders`.)

**No pixel of a fighter, a weapon or anything else that is not the
ground was darkened at all.** The few that have any green in them are
the rim the picture is averaged down over (`antialiasing 2`), where one
pixel of the picture holds samples of the ground and of the fighter: the
ground's samples are darkened and the fighter's are not, which is what a
smoothed edge is. The darkest the picture came anywhere is one shadow's
own 0.45, never two shadows' 0.20 -- 0.442 where it is lower is the
eight bits a channel rounding a dark colour down.

### The shadows cast from the fighters' own faces (`shadows cast`)

`shadows soft` darkens the ground under the game's own silhouette, and
that silhouette is coarse: a separate shadow mesh of a box or two a bone
(`model_BuildShadows`), about an eighth of the body's polygons, drawn by
a node that is always at the renderer's coarsest scale (`shift = 2` in
`render_SetupNode`), so every corner of it is rounded to four world
units before it is flattened. A soft edge cannot hide boxes that large,
and a corner that steps by four units while the fighter breathes is
magnified by a long low sun into a shadow that twitches when he stands
still. **`shadows cast`** (or `cast:N`, N hundredths) keeps the mask,
the soft edge and everything that keeps the darkening off the fighters,
and changes only what is drawn into the mask:

- **The silhouette is the fighter's own faces**, every face of his body,
  his weapon and his scabbard that the renderer looked up
  (`render_LoadFace`), whether it became a primitive or not: the faces
  turned away from the camera cast a shadow as much as those that face
  it (a shadow of only the camera's half of a limb comes out up to half
  as wide), and so do the faces off the screen. The side channel keeps
  them as it looks them up (`port_RemasterCasterFaces`), some 1,600
  faces a frame for two fighters.
- **From the coprocessor's own sums, not from the screen.** Each corner
  carries where it was before the projection, the rotation and the
  translation with eight bits of fraction that the GTE rounds away
  before it divides (`PortPrecisePoint.view`, `src/port/gte.c`). The
  backend takes it back into the world with the inverse of the view the
  stage was drawn with (`PortStageView`), slides it onto the ground as
  `light_ProjectShadow` does -- x and z by the height above the ground
  times the sun's slope, y the ground's -- in floating point, and
  projects it with the same view: nothing is rounded on the way.
- **What casts, and onto what, is still the game's.** The renderer
  says, for each of the game's shadow nodes, which node casts it and the
  height of the ground under it (`port_RemasterNoteShadowCaster`,
  `render_DrawQueue`). A face casts if its node has a shadow node of its
  own (a weapon, a scabbard) or is the skinned body whose bones the
  shadow nodes hang from; anything the game gives no shadow -- a dropped
  weapon, the fighter's cloth -- casts none (33 faces a frame). The
  game's own shadow faces are still taken out of the picture and kept:
  a frame with nothing to cast from (no stage drawn) falls back to them.
- **Between two frames** each triangle is moved between the two frames'
  own places, paired by the face and the mesh it is of, and the camera
  between the two frames' views (`port_GlShadowSetPhase`, `gl_tween.c`).
  The frame on show is drawn from its own triangles and its own view,
  so the first phase is the game's frame exactly.
- **How dark: 68 hundredths with nothing said**
  (`PORT_SHADOWS_CAST_STRENGTH`). Measured under the shadow's middle on
  three stages (the tour, `tmp` runs at one hundredth beside the mask),
  the ground's own lightness is CIE L* 26 on the orange tree fields, 37
  on the castle rooftop and 52 on the practice floor; the game's black
  shadow takes all of it. `soft`'s 55 takes 16, 21 and 28, a little more
  than half of what the game's black takes, which is why it read faint
  beside the game's own shadow, and most of all on the rooftop, where the
  camera is low and the game's silhouette is a sliver. A darkening is
  the same share of the ground on any stage, so one number serves them
  all: 68 takes 20, 27 and 36, three quarters of what the black takes,
  and leaves the ground a third of its value, enough for its texture to
  show. A cast silhouette is also thinner than the boxes it replaces --
  a wrist, a blade -- and a thin shadow needs the darker middle to
  read.

**Measured.** The three runs of "How it is proved" above, with
`shadows cast` (the tour, scale 3, `antialiasing 2`, fifteen pictures
each):

| Stage | Pixels the darkening changed | ... with any of something else in them | ... that are **wholly not** ground | Darkest |
|---|---|---|---|---|
| the orange tree fields (12) | 186,402 of 37,324,800 | 2,197 | **0** | 0.312 |
| the castle rooftop (13) | 117,819 | 2,727 | **0** | 0.320 |
| the terraced fields (15) | 142,353 | 2,224 | **0** | 0.314 |
| ... with `stage_mesh on` (15) | 142,404 | 2,228 | **0** | 0.313 |
| the practice floor (35) | 209,473 | 1,990 | **0** | 0.319 |

Two to three times the pixels of the game's mesh: the arms, the hands
and the blades now have their shadows. The darkest is one shadow's
0.32, never two shadows'. Each frame casts 1,619 faces as 2,266
triangles, none lost; no frame fell back to the game's mesh.

**Standing still.** A fight in which nobody presses anything, from
the fighters' first idle frame on (159 pairs of frames a game frame
apart, the mask at the console's resolution and `antialiasing 4`, so
that a pixel's coverage is a share of sixteen samples): how far the
outline moved, the coverage that changed over the outline's length, and
how many pixels changed by half their coverage or more -- a corner that
stepped, which smooth breathing does not do:

| Stage | | Outline moved, median / 90th percentile | Pixels that stepped, a frame |
|---|---|---|---|
| the orange tree fields (12) | `soft` (the game's mesh) | 0.025 / 0.058 px | 3.5 |
| | `cast` | 0.026 / 0.040 px | **0.2** |
| the castle rooftop (13) | `soft` | 0.043 / 0.085 px | 3.7 |
| | `cast` | 0.043 / 0.075 px | **2.4** |

The median is the fighter's own breathing, which both follow; what
`cast` takes away is the steps of a corner rounded to four units. What
is left on the rooftop is the breathing magnified by its low sun.

**Between two frames** (`--gl-tween 5`, the tour of the practice floor
with `shadows cast`): the first phase of 627 frames is the game's own
picture to the pixel (770,457,600 pixels, 0 different), no picture is
out of order, and no triangle lacked its partner in the frame after.

**What is not done.** The shadow lies on the plane of the caster's
ground, as the game's does: on a step or a slope it does not follow the
ground (the ground's own mesh, `gl_stage.c`, has the heights, and a
later step could flatten each corner onto the ground under it along the
sun). Everything in "What is not done" above still holds for the mask.

### Edges

**Since 2026-11-18 the edges are multisampled** (`antialiasing N`: N
times N samples a pixel, at most eight, `port_GlSamplesOf`,
`src/port/gl/gl_draw.c`). The colour buffer, the depth buffer and the
ground's slots keep that many samples a pixel at the picture's own
scale, and the card averages them into the picture at the end
(`port_GlResolve`, a blit). The card shades a pixel of a face once and
keeps which of its samples the face covers, so a face costs what it
costs at one sample a pixel: on the owner's RTX 3090 at 2560 by 1440 the
ground's mesh went from 3.9 ms to 1.25 and the sky from 1.1 to
0.27 (see "Measuring on the card"). What makes it work with the game's
picture:

- **The 2D layer is not multisampled.** A primitive that has no depth
  (the HUD, the fonts, the menus, a rectangle of video memory stretched
  into the picture) is drawn with the card's multisampling off
  (`GL_MULTISAMPLE`, `port_GlSetState`, `port_GlDrawBlits`): it is
  rasterised from the middle of each pixel by the chip's own rule, as
  before, and writes every sample of a pixel or none. Its corners are
  half a pixel in, as the chip's are, so multisampled it covered half of
  each pixel along its edges: a grey line round every letter
  (`--gl-blocks` counted 203,742 uneven blocks in `port_game`'s flat
  screens). Off, the flat screens are block for block as they were.
  A batch is all of the scene or none of it (`PortGlState.is_scene`).
- **The ordering in which the game paints is kept exactly.** Each
  sample is mixed with what is under it, in the order of the ordering
  table, and the averaging comes after everything: a half-transparent
  primitive over an edge mixes once at each sample, as it did at each
  pixel of the larger buffer. (The worry below about multisampling is of
  coverage folded into alpha, which this is not.)
- **What reads a buffer reads one value a pixel.** The ground's slots,
  which the scene and the fighters are held against, are taken down to
  one a pixel as the mesh ends (the first sample's, by a shader:
  `port_GlGroundSlotsEnd`; a blit of them took 30 ms a frame on the
  card). The HD fighters are drawn into a layer of as many samples and
  taken down into its colour, the share of the pixel they cover, and
  their nearest depth (`port_GlFighterTakeDown`, `gl_fighter.c`), and
  the layer is mixed over the picture by that share, so a fighter's edge
  is smoothed as before. The pause's picture is drawn back, not blitted
  (no blit draws into a buffer of several samples).
- **Thinned walls fade.** A wall thinned so that a fighter behind it
  shows keeps a share of its samples (`gl_SampleMask`) and not of its
  pixels; the shadows' mask, of one sample a pixel, keeps the dither.
- **What it does not smooth that supersampling did**: what is inside a
  face (a texture is shaded once a pixel; the mod's textures are
  mipmapped and the game's are averaged over the texels a pixel covers,
  so this is the edges' smoothing only), the edge of a texel of nothing
  in a texture (a fence's gaps), and where two faces cross (the depth a
  face writes is one a pixel).

`RB_SUPERSAMPLE=1` in the environment draws it the old way, which the
rest of this section describes, to compare. With `off`, and with
`--gl-exact`, there is one sample a pixel and nothing of this runs.

**Before:** the scene was drawn **two or four times over each way and averaged
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
  read. They are cleared **to the colour the game cleared its own
  picture to**, where what it wrote was one colour -- its four corners
  and its middle, five pixels of video memory, which tells a frame's own
  clear and a fade from a picture copied in. A fight's frame begins with
  a fill of the stage's colour of the distance (`light_SetFarColor`),
  which is the sky of every stage but the few whose is black: clearing
  the bands to black instead left two black bars down the sides of the
  picture wherever no ground reached into them, which is what the owner
  saw as "strange black bars on the sides";
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

**And the bands are full.** Measured on six stages at the settings the
owner plays at — `draw_distance max`, `antialiasing 2`, `aspect 16:9` —
in a window of 2,560 by 1,369 and again filling a screen of 2,560 by
1,440, as the share of each band that is pure black (the picture's own
rectangle in the window, so that the letterbox beside a 16 by 9 picture
in a window of another shape is not counted):

| The stage | Before, left/right | After |
|---|---|---|
| practice 1 (35) | 96.7% / 88.9% | 0.3% / 0.3% |
| main Tatara shrine (0) | 64.9% / 40.7% | 18.6% / 0.3% |
| bamboo thicket (7) | 52.9% / 56.8% | 0.3% / 0.3% |
| castle rooftop (13) | 83.2% / 64.0% | 0.3% / 0.3% |
| lighthouse (19) | 47.2% / 65.1% | 0.3% / 0.3% |
| abandoned station (25) | 70.1% / 74.3% | 0.3% / 0.3% |

What is left is the scene's own black: on the shrine the left band holds
an unlit doorway and the fighter's shadow, which are black where the
console's own picture is black too.

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
(`PORT_ASPECT_W_WIDE`, `src/port/window/settings_list.h`); the builds with no
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

- the window is made with `SDL_WINDOW_OPENGL` and a 4.5 core context is
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
- **the movie is the software GPU's picture** whichever is chosen: the
  player's display is in the 24-bit mode the card does not draw.
  `port_MovieIsPlaying` (`src/port/movie.c`) is true while the player
  runs; the window then fits the frame as with Graphics Original,
  smoothed because it is video, and the counter sits on it. (With F2 on
  the window showed black through the whole movie and the title until
  this was done; the frames were decoded all along.) The title picture
  the player holds after it, in the 16-bit mode, is the remastered
  picture where a mod has a picture of it -- the art mod has, made from
  the disc -- and so is its fade (`port_GlShowsReplacedPicture`;
  MODS.md, "The title picture"); with none it is the software GPU's,
  smoothed;
- **the pause keeps the fight's picture.** The pause menu draws the
  fight's last frame again under its panel on every loop by giving the
  GPU that frame's ordering table once more (`fight_RunPauseMenu`), and
  by then the side channel has forgotten the frame's points and faces:
  the frozen fight was drawn as the console draws it, whole-pixel
  corners, the game's tiles instead of the stage's mesh, four by three.
  So `port_RemasterPause` (`port_remaster.h`), called around that one
  `DrawOTag` under `RB_REMASTER_VIEW`, tells the backend nothing of the
  table, and `src/port/gl/gl_pause.c` puts there the fight's last frame
  as the card drew it, kept at the pause's first loop from the half of
  the colour buffer it was drawn into, bands and all; the panel is drawn
  over it as ever. The software GPU still draws the table, so the trace
  is the same with the pictures on and off. A pause begun with Graphics
  Original draws the table as before; F2 during a pause shows the kept
  picture again when it comes back; a window resized during a pause
  stretches the kept picture to the new colour buffer, smoothed; no
  picture between two frames is drawn during a pause;
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

**Where there is no OpenGL 4.5** — an old card, or SDL told to show
nothing — the window is made again without a context and SDL's renderer
shows the software GPU's picture, exactly as it did before there was a
card path: the remastered renderer has no backend then, so
`port_RemasterIsOn` is false whatever F2 or a setting says, and the log
says why. This is the path every headless run takes (`SDL_VIDEODRIVER=dummy`
has no GL at all), which is why the window's own proof is untouched by
all of this.

**The settings** are the window's, read from `rebushido.cfg` and the
command line as the others are (`settings_list.h`, `PLAYING.md`): `graphics`
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
edges ("A wider picture"), and the ground is drawn from one mesh of the
whole stage and not from the game's own tiles ("The ground as one
mesh"). With "Graphics: Original" the window shows the console's own
four by three whatever the shape says.

**What the log says**: the card's vendor, renderer and GL version; which
picture a run starts with and all five settings; **which path each stage's
ground takes** -- one line each time the stage changes, saying the mesh or
the game's own tiles and why (`port_GlStageNews`); every F2 with the blank
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
gives a real 4.5 core context with nothing on screen: the card path
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
("What each later step needs"). The fighters' shadows are drawn as a
darkening of the ground where a run asks for it ("The shadows").

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
the port's frame on show, and nothing is extrapolated. (The frame on
show was itself a blank later than the console's until run-ahead: "Input
lag", below.)

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

**A piece of a tile has a place of its own, not the tile's.** The
renderer attaches one face — the whole tile's corners — to every piece
of a tile it cuts, and each piece has the GTE store its own corners
straight into the primitive (`render_SplitFT3`, `render_SplitFT4`). The
decoder took the face's corners for the piece's, found them nowhere near
the pixels the chip drew the piece at, and so gave the piece no place at
all: a third of the ground of a stage drawn at `max` stood still between
two frames while the ground about it moved, and the gaps that opened at
its edges showed the colour of the distance through the floor. Where the
face's point is not this corner's, the point the GTE stored into the
primitive itself is taken instead (`port_PointFits`), which is the same
look-up a primitive with no face of its own already used. A fight frame
on the practice stage goes from 692 of its 1,177 primitives moved to
952, and what stands still from 351 to 91 — of which some fifty are the
rectangles of the flat layer, which never move by design.

**A fighter's joint has the place of the bone before it.** A fighter's
body is one mesh skinned over thirteen bones (`render_DrawSkinnedMesh`):
each section's points are worked out under its own bone, and the points
a joint shares with the next section are then *copied* to where the next
section looks for them, by the program and with no GTE in between. The
side channel keeps a point by the address the GTE stored it at, so the
copy had nothing at its new address, and every face of the next section
that touched a joint had no place at all: drawn at whole pixels, its
texture flat, and left standing still between two frames while the faces
about it moved, so that at `frame_rate = display` the joints tore open.
The copy now tells the side channel (`port_RemasterCopyPoint`). In
`port_game`, of the 546,574 polygons of the fighters' own (their shadows
left out), 58,373 had no place (10.7%), and now none has; the prover
holds that to none (`tools/port_gl_prove.py`, `fighters`). That copy is
the renderer's only one: every other point it draws from is the GTE's own
store. An effect (`eff.c`) and the surf (`wave.c`) take their points
through Sony's `RotTransPers` and the GTE's macros and build their quads
themselves, so with `effects original` they have no place; with `exact`
or `smooth` each says which point or points it was built from and which
effect it is, and is placed and moved by that name ("The effects").

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

**What made a fighter standing still move** (the owner's "always
flickering and in motion even when they should be still", at
`frame_rate = display` on an 85 Hz screen). Measured on the strip of
`port_game` at blank 3,000 -- the fighters squared up, barely moving --
three things held faces of a fighter still between two frames while the
rest of him moved, so that at every one of the game's frames they
jumped to catch up: the last picture of a frame differed from the
game's own next frame in 3,226 pixels by more than 24 of a channel,
nearly all of them on the fighters' swords, scabbards and sandals.

- *A sliver was held* (`held`, 110 to 186 a frame there, not the 15
  above). A face nearly edge on -- a blade, the side of a sandal --
  whose corners round to one line of whole pixels was taken for one of
  no area and so as one the chip would not draw; a picture between two
  frames then drew it where it was. A face drawn where the projection
  put it is now measured by those places (`port_GlIsDrawn`), where it
  has an area, and is drawn and moved: `held` is 0 in both fights.
- *A weapon's node is not where it was.* The nodes of the fighters'
  weapons and of the shadows are built every frame in one of two places
  in turn (some 0x13400 bytes apart), so every face of them had another
  name one frame on and none was paired. A thing whose faces are left
  unpaired is now matched to the thing of the frame after it under whose
  name the most of them are found -- the same mesh, face and piece, the
  same kind, near -- and paired under that name (`port_TweenPairMoved`,
  `moved`). Unpaired faces of that frame went from 107 to 43.
- *A face one frame has not.* A face that turns away from the camera is
  not in the next frame and one that turns towards it was not in the
  last, so at a fighter's edge some faces are in one frame only, every
  frame. A face's name says which points of its mesh its corners are,
  and its neighbours, which both frames draw, are made of the same
  points: each frame's points are written down by thing, mesh and point,
  and a face one frame has not is given the other frame's places for its
  points and moved like any other (`port_TweenPlaceUnpaired`, `going`
  and `coming`). It is drawn only while it faces the way it faces in the
  frame that has it, which is what the renderer would have drawn of the
  thing at that moment; a face that is coming is never drawn at phase 0,
  so the first picture is still the game's own. A point two faces of a
  frame put more than a pixel apart is not trusted, and a face with one
  is drawn where it is as before.

With the three, the last picture of that frame differs from the next
frame in 872 pixels by more than 24 (from 3,226), and in 1,125 by more
than 3 (from 4,522); the sword no longer stands still between frames.

**What is not moved, and what then happens.** A primitive the frame
after it has not (something spawned or gone), one the projection gave no
place to at all, or one with no name that no search found, is
drawn where the frame on show had it: so it waits for the next frame
instead of moving. A corner the projection put outside the eleven bits
the chip holds a screen position in is **not** one of them: that place
is kept and moved like any other, as long as the point is in front of
the camera (`port_PlaceIsNear`, `PLACE_FAR_MAX`). **A depth is not asked for**, only a place
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

And when **too few of the faces the picture holds can be paired at all,
or too many of those that can are a jump from where they were**, the two
frames are not one scene a frame apart — a camera cut, a screen that
changed, a model replaced — and no picture is drawn at all: the window
shows the game's own, which is exactly right.

Both shares are of the faces that are **in the picture** and not of
every face handed over (`port_TweenInPicture`), so that what a stage
drawn whole hands over beyond the view cannot decide it. The lines are
as many unpaired as paired (`PORT_TWEEN_CUT_SHARE`) and half as many
jumped (`PORT_TWEEN_JUMP_SHARE`), a jump being 213 pixels, a third of
the console's width in one frame of the game (`PORT_TWEEN_JUMP`).

**Both lines used to be the one line of a quarter, and that is what the
owner saw as flicker.** In a fight on the practice stage at
`draw_distance max` an eighth of the faces the picture holds cannot be
paired — a tile the camera came nearer to is cut another way and no
piece of it has its own name — so with a quarter for a line an ordinary
frame sat just under it and a frame with a little more coming and going
sat just over, and the picture alternated between the display's rate and
the game's. A real cut leaves almost nothing paired and near, so the
lines need not be fine. **The jump is what catches a camera that cut**,
which the names alone cannot: a face of the stage has the same name
wherever the camera is, so after a cut every face is still paired and
all of them are somewhere else. In `port_game` the pictures refused are
every blank of the menus and the loading screens, where there is nothing
with a place to move and the game runs at 60 anyway — which is most of
what a session's own tally of refused pictures counts.

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
- **and no pixel of a picture between two frames is the colour the
  game cleared that picture to where neither of the two frames is**
  (`background_pixels`). This is the narrow part of the count above and
  the owner's own defect said exactly: a black box, or a navy one,
  where both frames have floor. The count above takes in any pixel that
  changed where the two ends agree, honest motion passing over a steady
  pixel included, and it grows as more of the scene is moved; this one
  does not. The colour is the one the game's own clear wrote
  (`port_GlBackground`), so a stage whose distance is black has no such
  count at all. On the practice stage at `max`, over 681 pictures of
  1,704 by 960, it is **15,939 pixels — twenty-three a picture**, and it
  was 6,182 before a piece of a tile had a place of its own: moving the
  ground instead of leaving a third of it standing opens a hairline
  where a tile whose cut changed still stands beside tiles that move.
  What it closed is larger and is in the frame itself: of the console's
  own middle of a frame of that stage, below the horizon, 1,132 pixels
  were the colour of the distance where the floor should be and 316 are
  (the other five stages measured are each a hundred or so down), which
  is the hole a polygon squashed against the chip's eleven bits left;
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

**The railings that jumped a pixel** (2026-11-18). Later the same plan
had 14 frames out of order, all on the cave, and `port_fps_prove.py`
failed on them every run; they were not the measure. At blank 3,036 the
railing a step before the camera is a pixel higher at phases 2 and 3
than at both ends, which agree to the pixel (rows 762 to 800 of 960).
The stage's mesh -- ground and railings -- is drawn between two frames
with a view mixed between theirs (`port_GlStageMixView`), and that view
kept the camera in whole units of the world and the rotation in 1.12
numbers, as the game does. The camera went from (-5000, -461, 10316) to
(-5000, -462, 10320): its depth moved a unit a phase, while its height,
a single unit for the whole frame, stayed at -461 until the last phase
and then stepped. Off the line between the two cameras, the near railing
went up with the depth and came back down with the height. The mixed
rotation and camera are now kept unrounded for the card
(`g_stage_mixed`, `src/port/gl/gl_stage.c`; the frame on show is drawn as
before): **14 frames out of order to 1**, the same on llvmpipe and on the
card. At 144 pictures a second that was a shimmer of whatever stood
close to the camera whenever it moved slowly.

The one left, blank 2,952, is right and is the measure's: a railing a
step before the camera comes down into the picture from above, 25 lines
of it in the first picture and 90 in the second, so the second is
further from the last than the first, by 7%; every corner is between its
own two ends. `port_fps_prove.py` lists such a frame with why
(`ORDER_EXPLAINED`) and fails on any frame out of order that is not
there.

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

### Where the time goes

`--profile FILE` makes the window write, at every blank, how long each
part of it took (`include/port_profile.h`, `src/port/profile.c`): the
game's own code, the software GPU, the side channel, the card's
batches, the sound, the frame's finish, the window's picture, the
pictures between frames, the swap and the wait; and, at every swap, when
it came back and the phase of the picture. `python3
tools/port_profile.py FILE --from N --to M --rate 85` reads it: each
part's mean, median, 95th centile and most, for the blanks in which the
game drew a frame and the others, and how evenly the pictures came.
`--display-rate N` makes up a display of N refreshes a second that a
swap with vsync on waits for, so that a run with no screen can be
paced as a player's is. Off, a part's switch is one test of a flag.

**Measured** (the plan of `port_game`, its fight from blank 3000 to
4050, the window at 640 by 480 through SDL's `offscreen` driver on
Mesa's `llvmpipe`, `--fast`, on a machine with other builds running, so
a millisecond either way is noise). A blank in which the game drew a
fight frame, median milliseconds:

| part | before | after |
| --- | --- | --- |
| the game's code | 1.09 | 1.12 (0.90 at `-O2`) |
| the software GPU | 2.50 | 2.55 (2.37 at `-O2`) |
| the side channel | 1.48 | 1.43 (1.35 at `-O2`) |
| the sound | 0.52 | 0.61 (0.56 at `-O2`) |
| the card's batches | 38.2 | 22.9 |

So **what the PC's processor does for a frame is about 5.5 ms**, and
everything else on this machine is Mesa drawing in software, which says
nothing of a real card. What does carry over is what the program asks of
the card: a fight frame is about 80 draws and 8,400 vertices, and it
wrote video memory 55 times (the frame's clear, the textures' pages,
and the fog's 31 colour tables a line at a time), each one a send of
the mirror and a draw of its own. Those are now gathered: the rectangles
written one after another with no primitive between them are copied as
they are written into the backend's own copy of video memory, and sent
and drawn together when the card next draws anything else
(`port_GlDrawBlits`, `port_GlMarkMirror`); one that a later one covers
is left out. The picture is the same to the byte (147 pictures of
`port_game` at scale 2 and 16:9, with and without pictures between
frames, against the program before; and again with the HD stages' pages,
the night sky and see-through walls on, against `main` as merged). Each
rectangle keeps its fade's tint; the stage's pages are told of each
write as it is made (`port_GlSceneWroteVram`); and the rectangles are
drawn before the sky goes over the frame's clear, which is what the sky
is drawn onto. The frame's finish hands the card
its work (`glFlush`) and no longer waits for all of it (`glFinish`): on
a real card the program and the card now work at once, and the swap or
a picture read back waits where it must.

**What is not done, and why.** The software GPU runs whichever picture
is shown: the card's mirror of video memory takes its texels from it,
and what the game draws into video memory to use as a texture is only
there because the software GPU drew it; it costs 2.5 ms of a frame.
Its picture is no longer converted for the window where the remastered
one is shown (0.4 ms a blank). The geometry shader of a flat polygon
runs for every batch, the scene's too, where it passes the triangle
through; a program without it for the scene would halve Mesa's cost
and is nothing on a real card, and was left. A mod's textures are
read beside the game (below, "A mod's pictures, read beside the
game"); the HD fighters' models are read during the load before a
fight ("Quick loading").

### A mod's pictures, read beside the game

**What the owner saw** (2026-11-18, his log, `fighter_models off`,
`run_ahead 2`, vsync off): the time let go 54 times in 150 seconds,
many of them as an `art` texture was decoded at its first use in a
fight, and the sound running out 111 times. Measured on his card
(`--hidden`, his settings, `port_budget.py`'s fight; the program's
own `--profile`), a blank's part `remaster` went to 50 to 320 ms where a
picture of the `art` or `stages-hd` mod was first drawn: the file read,
the PNG decoded, its colours multiplied by its alpha and, for a page of
the stage, made the array's size with every level below it, all on the
thread that draws.

**Now** (`src/port/gl/gl_load.c`): a picture asked for is read and
decoded on threads the window lends (a quarter of the processors, two
to six: SDL's, `port_LendThreads`), and handed back to the thread that
draws at the next frame's start (`port_GlLoadCollect`, at most 32 MB a
frame), which sends it to the card. Until then the face draws what it
would with no mod, the game's own texture: a picture read at first use
comes a frame or two late, and nothing waits for a file. The stage's
pages are asked for as the stage loads, as before; each waits in its
layer (`LAYER_READING`) and the pages and tables that name it are
entered again when it is in. A context made again forgets every key
and the generation of the stage's layers, and what was asked for
before is not put in. The fades (`gl_replace.c`) keep a replacement's
key with its texture, which is filled in when it is read.

The build without a window (the provers) lends no threads, and
`RB_TEXTURES_AT_ONCE=1` takes them away from the window's: then a
picture is read where it is asked for, as before, the same pictures to
the byte.

**What nothing clears is read at once.** A loading screen is drawn
over the picture on show again and again, with no clear between
(`disp_DrawLoadingScreen`), and the group battle's card is begun while
the display is off. There a picture read beside the game left the game's
own letters drawn under it for good: the card's names showed twice, the
game's and the mod's a little apart (2026-11-20). So a primitive drawn
while the display is off has its picture read where it is first asked
for (`gl_draw.c`, `port_GlReplacementOf`; `PortGlLoad.at_once`): the
card's few dozen small pictures, once each, on a screen that is waiting
anyway. Every other draw reads beside the game as before.

At first a primitive whose drawing area was the display's was read at
once as well. As the window sees them, though, every 2D primitive of the
game's frames is drawn into the display's area (its buffers swap before
the frame is drawn here), so that held for the menus and a fight's
numbers too: each picture of the ui mod first seen in play was read on
the drawing thread, 1.5 to 14 ms each, and `port_bench.py` found the
fight's first blank at 20 to 30 ms against 4 before (its card part,
2026-11-20). The card is drawn with the display off and needs only that.

The story's stage card is not: it is drawn with the display on, twice, so
that it is in both buffers, and then left on show while the stage loads,
the route's marks drawn into it once each (`story_StageCardUpdate`,
`disp_DrawLoadingScreen`). Read beside the game, the art's map and picture
came after the last frame that drew them, and the card kept the game's
own textures in the window (the provers, which read at once, never showed
it). So the game says while it draws a picture it will keep on show
(`port_RemasterKeepsPicture`, `port_remaster.h`: the card's two frames and
each call that draws a mark), and a primitive drawn then has its picture
read at once too (`port_RemasterIsPictureKept` in `port_GlReplacementOf`):
the card's few pictures, once each, while the stage loads.

**The stage's pages looked up again for nothing.** A page of the stage
is looked up again (its key over all of its 65,536 texels) when the
game writes over it. Two things that put video memory back as it was
told it so as well: every picture between two frames giving back what
it wrote (`port_TweenGiveVramBack`, a rectangle of 784 by 488), and,
with frames run ahead, the whole of video memory sent to the card at
every blank. So every page was looked up again at every picture: a
sixth of the window's thread's time in a fight on the owner's PC
(samples of its program counter: `port_TextureValue` and
`port_TexturePageKey`). Both now send the pixels without telling the
stage (`port_GlRestoreVram`), and a write that holds none of the
stage's own pixels (the fog's colour tables lie inside the pages'
rectangles) no longer makes a page stale. Nor does a write that changes
no pixel of the backend's copy (`port_GlMarkMirrorFrom` says whether
one did): since a fighter's pages are keyed too (MODS.md, "A fighter's
pages"), the 16 by 32 pixels the game writes again, the same, at the
corner of each fighter's first page every frame looked some thirty
pages up again a frame, 2.5 ms of the processor, and put `port-bench`'s
`ra0` at 9.9 ms and the lighting's 95th centile at 2.8 (2026-11-25;
6.4 and 0.8 after). A stage whose array is full
says so once, not at every lookup (Lost Bridge's log had 20,000 such
lines).

**The HD cloth and body shared out.** The places of the body posed
for the cloth and for the cast shadows, and each place of a cloth kept
out of the body, are each their own sum, so they were shared out among
four of the window's threads: the same vertices to the bit however they
were shared. All of it is now done on the card ("The HD fighters on the
card"), and the threads are gone.

**Measured** on the owner's card (RTX 3090, 2560 by 1440, his
settings, `fighter_models on`, `shadows cast`, `run_ahead 2`, the
fight of `port_budget.py` from blank 2950 to 4150; wall-clock
milliseconds, medians of the blanks in which the game drew a frame;
other agents' work was running on the machine, so a millisecond is
noise):

| part | `main` at `f7760876f` | now |
| --- | --- | --- |
| the HD cloth (`cloth`) | 9.5 | 3.6 |
| the frames ahead (`ahead`) | 9.5 | 4.5 |
| pictures between frames (`tween`), a frame's blank | 4.6 | 3.8 |
| ... a blank with no frame | 4.9 | 0.85 |
| all but the wait, a frame's blank | 39.1 | 27.9 |
| ... a blank with no frame | 6.0 | 2.9 |
| pictures shown a second | 39.6 | 48.0 |
| time let go (the whole run) | 107 | 53 |
| sound ran out | 166 | 90 |
| run-ahead, a boundary | 11.1 | 6.8 |

With the owner's own settings (`fighter_models off`), the two run one
after the other while the eleven checks ran beside them: a fight
frame's blank 28.7 ms before and 18.0 after, 46 pictures a second and
66, the time let go 67 times and 6, the sound run out 103 times and
11, and 14 blanks of the fight over 40 ms and none. What is left of a fight frame's 28 ms is the card's work
waited for at the swap (3), the frames run ahead and the state kept and
put back, the cast shadows posed and copied on the processor (3.6), the
cloth (3.6), the software GPU (2.6) and the side channel (2.1): see
docs/HANDOFF.md.

### The orange flashes

**What they were** (the owner, 2026-11-18, "occasional orange
flashes"; found by tapping every fourth blank of a fight with his
settings and looking for pictures with far more saturated orange than
both their neighbours): a band of orange right across the picture for
one picture, at a torch's height or a sword's, a few times a fight. An
effect's sprite (a torch's flame, a clash's streak) whose point the
projection put all but on the camera's plane is sized from that depth
to about 2,000 pixels across. The chip never draws a primitive more
than 1023 across, and the game counts on it; but a primitive with its
true places was measured against SPAN_TRUE_X_MAX (2,048, kept for the
ground of a wider picture), and 2,022 passed. An effect is now held to
the chip's own rule by its whole pixels as well (`port_GlChipSpans`,
`gl_draw.c`). The same search after it found no such picture; the
pictures of the dark blue of the night sky alone that it also found,
at the cut after a kill, are the game's own (the Original picture has
them at the same blanks).

**Held to `main`** (`21418d68b`) on the owner's card: the budget's fight
with the HD fighters, cast shadows, smooth light, the five mods
(`RB_TEXTURES_AT_ONCE=1`), `frame_rate original`, `--fast`, every eighth
blank's window at 640 by 360. Two runs of this branch are the same to
the byte (437 pictures); against `main`, 434 of 437 are the same to the
byte, and the three that differ are the three with the band of orange
across them, gone.

**The pictures, evenly.** At `frame_rate = display` a picture between
two frames was of the moment it was begun, counted from the moment the
game's step ended; it is shown at the display's next refresh, which is
anything up to a refresh later, so the scene moved by steps as uneven as
the refreshes are far from the blanks. And the first picture of a blank
was the game's own frame, which at the second blank of a fight frame is
whatever was last drawn: the same picture twice, then a jump. Now
(`pace.c`, "when a picture is shown"):

- a picture is of the moment it will be on the screen: the refresh after
  it is drawn, from when the last swap came back
  (`port_NextRefresh`); where there is no display to wait for, the slot
  the clock shows it at;
- the moments are the blanks' own, as the pace keeps them
  (`port_PacePhase`): the frame on show went up at the moment of the
  blank its step began at, and every picture is counted back by how late
  a frame's first picture is shown, on average, so that the first
  picture of a frame is its phase 0 and the last before the next is its
  end;
- the first picture of every blank is drawn between frames as well
  (not where a test asks for the game's own, `--shot-at N,0`);
- after a picture, another is drawn where the display's next refresh
  comes before the next blank's first picture can be ready (the blank's
  moment, then the game's step and that picture, at their recent peak),
  never more than a refresh past the blank's moment and none once it
  has passed: a PC that cannot keep up catches up.

Netplay's holding back a program that is ahead (`port_PaceHold`) makes
the blanks' own moments later, and the pictures follow those moments,
so a hold is a frame that lasts a little longer and not a jump; the
window's netplay test (`tools/port_net.py window`) passes with it.

A model of the two rules (a display waited for, a swap that comes back
at the refresh, the game's step 2 to 9 ms on a frame's blank, a picture
1.5 ms) puts the spread of what each refresh shows against when it
shows it from 7 to 9 ms down to 2 to 3 ms at 85 refreshes a second, and
the change from one refresh to the next from 11 to 13 ms down to 3 to 4
(at 60: 17 down to 1.5). It is a model: this machine's software OpenGL
cannot draw the remastered picture at 85 a second, so **whether the
fight is smooth at 85 Hz is for the owner's screen**, with the counter
(F3) and `--profile` beside it.

**`-O2`.** The window's build at `-O2` (`PORT_OPT=2 make port-sdl`,
into `build/port-sdl/linux-O2/`; `make port-sdl-windows` likewise)
takes about a tenth off what the processor does for a fight frame here
(the game's code a fifth). With the plans of `port_game` and
`port_other_fight`, SDL's dummy drivers and `--graphics original`, its
trace is the `-O1` program's line for line (2,338 and 2,462 lines) and
every twentieth picture is the same to the byte (224 and 236); the
headless programs at `-O2` pass the rollback self-test in both sizes.
**The players' build is at `-O2`** since 2026-11-18 (the owner: performance
first): `tools/port_sdl_build.py` compiles the game and the port's own
files of both window programs, Linux's and Windows', at `-O2`
(`WINDOW_OPT`; `PORT_OPT=N` says otherwise, into a folder of its own),
and the headless programs the checks and the play tests run stay at
`-O1`, the traces' reference. On the card (`tools/port_bench.py`, `ra1`,
the median of a fight frame's processor work): 22.5 ms at `-O1`, 16.0 at
`-O2` (the game 1.36 to 0.76 ms, the side channel 1.36 to 0.79, the
frame run ahead 3.75 to 2.92); with the fighters' models off 13.7 to
11.4. The traces of `port_game` and `port_other_fight` at `-O2` are the
`-O1` programs' to the byte in both sizes (2,338 and 2,462 lines), and so
are they with `--rollback-test 8 --run-ahead 2`.

**The files with a C library** (the remastered renderer, the HD cloth,
the pictures between frames, the window) of the 64-bit programs are at
`-O3 -march=x86-64-v3 -ffp-contract=off`, linked whole (`-flto`;
`HOSTED_FAST`, `RB_HOSTED_FLAGS` for a measurement): three turns each
against `-O2` on the card, the medians of `ra1`'s parts went from 4.11,
3.17 and 4.09 ms to 2.87, 3.00 and 2.46 for the pictures between frames,
1.62, 1.11, 1.31 to 0.98, 1.03, 1.12 for the shadows, 3.02, 2.70, 2.92 to
2.66, 2.76, 2.63 for the cloth; the work's median 18.3, 14.4, 17.9 to
14.4, 15.3, 14.3. An x86-64 from 2013 on (AVX2) is what the window now
needs; no multiply-add is fused, so the arithmetic is the same to the
bit, and none of it is the game's state.

Measured and not taken, with the bench run in turns against the `-O2`
build because other agents' work was on the machine (a run's median
moved by 4 ms from one turn to the next, the same program):

- **`-march=x86-64-v3`** (Haswell, 2013) for every file: the game's,
  the software GPU's and the side channel's medians the same to a tenth
  of a millisecond. The game's files are built without SSE (`-mno-sse`,
  the 64-bit port's rule), so there is nothing for it to use.
- **LTO of the game's files** is not possible as they are linked: the
  game's variables are put in the PlayStation's order by a script over
  one section each, which LTO's own sections do not keep. `-fno-plt`
  has nothing to do on Windows (no PLT) or in the statically placed
  Linux program; profile-guided optimisation was not tried.

### The HD cloth and run-ahead

**What the owner saw** (2026-11-17, RTX 3090, 2,560 by 1,440 at 85 Hz,
Remastered, every default on, the five mods): "like 2 FPS, very
stuttery, large artifacting". His session's log at `1e6878df3`: a fight
at 22 pictures and 11 of the game's frames a second, the pace letting
time go every fourth blank, and run-ahead at **24.7 ms a boundary**,
against 10.8 ms at `9cdd97dd6`, the build before the HD cloth was
merged. F2 (the original picture) brought the fight back to 60 and 30;
`run_ahead = 0` brought it back too.

**Why.** The HD cloth was kept out of the posed HD body on the
processor once a *picture*: for the frame's picture, again for its cast
shadow, again for each picture between it and the next frame (three
or so at 85 Hz), and all of that again for each frame run ahead. With
`run_ahead 2` a fight's boundary did it seven times or more where once
is enough, about 3.5 ms each on his processor: more than the 33 ms a
fight's frame has. Measured here (the owner's PC, WSL, the window's
build in the offscreen driver at 640 by 360, `port_game`'s plan with
Kannuki against Mikado on Tatara Shrine, `--profile`; a blank in which
the game drew a fight frame, median ms):

| part | `run_ahead 2`, before | after |
| --- | --- | --- |
| the HD cloth (`cloth`) | 36.8 | 9.4 |
| the frames ahead, besides what is counted elsewhere (`ahead`) | 49.4 | 22.9 |
| the side channel (`remaster`) | 33.3 | 27.3 |

and the samples of the main thread (a profiler of its own, by
`SIGPROF`) had `port_GlClothBodyNear` at 40% of all of them before and
2% after. Now (`port_GlFighterHeldCloth`) a frame's cloth is kept out
once, written down under what it was made from, and read by its
picture, its shadow, the pictures between frames (which mix two frames'
kept-out surfaces) and a frame ahead that comes to the same pose; the
walk of the body's cubes is made once where the sum looks in the same
cubes as the search for the nearest vertex (`port_GlClothBodyGather`,
the same sum to the bit); and only the prover looks again for the
places left inside (`port_GlFighterCountCloth`). At 320 by 240 with all
of it, a fight frame's cloth is 9.3 ms with `run_ahead 0` and 8.7 ms
with 2: a frame ahead pays for it only where its pose is new.

**The artifacting.** Every one of the owner's sessions of the day says
"the display holds the game back: it is no longer waited for (vsync
off)", 10 to 60 seconds in: three times the time was let go within 1,800
blanks (a load, a texture decoded at its first use, and then the fight's
slow frames), and the window gave up waiting for the display for good.
Without vsync every picture after it was torn, at 85 Hz and more so at
11 frames a second. That give-up was for a display slower than the game,
which is now never waited for from the start (`REFRESH_MIN`); the window
gives up at run time only where the display's rate is not known
(`port_ShowBlank`).

**The fighters' colour tables** (the owner, 2026-11-18: with
`fighter_models` off the fight is a steady 60, with it on about 50
and the time let go every 16 to 20 blanks). Where the game has changed
a model's colour tables -- every drawing of a tinted fighter, the second
player's, the CPU's -- the tables' texture was made again
(`glTexImage2D` of a floating-point texture) for each drawing of each
picture: 3,689 times in `port_game`'s fight with Mikado against Kaun,
each a texture the card may still be drawing from, which a driver can
only answer with new memory. It is now sent where the tables differ
from what that place's texture holds: 4 times in the same fight
(`palette_sent`; the session's log says how many at its end).

**run_ahead's default** is now 1 (`PORT_RUN_AHEAD_DEFAULT`): two frames
ahead cost a menu's blank (16.7 ms) 10.8 ms on the owner's PC before any
of the above, and a press is still seen 50 ms sooner than on the console
with one. `run_ahead = 2` is a line of `rebushido.cfg` or the settings
menu's "Run ahead".

**A frame run ahead paints nothing** into the software GPU's video
memory where the remastered picture is what is shown
(`g_port_ahead_unpainted`, `PORT_PAINTS_NOTHING`, as a step netplay runs
again): its picture is the card's, and the state, video memory with it,
is put back after it. With `run_ahead 1` the frame ahead is the frame
whose picture is shown (the step after it gives the card nothing), so
what it costs over a bare step is its own picture, drawn there in place
of the step. The software GPU's part of a fight frame at `run_ahead 1`
went from 6.99 ms to 3.42 (`run_ahead 0`: 3.4), and the window's
picture of the software GPU's from 2.71 to 0.11.

**The state kept and put back by the pages written** (2026-11-18). A
boundary saved the whole state (about 7 MB in the window) and put it
back: 1.5 to 1.9 ms of it on the owner's PC, of which only a hundred or
so pages a fight's frame had written. Now a save and a load copy only
the pages written since the snapshot was taken, found by the software
GPU's marks for video memory and by the system's page protection for
the rest, with the pages written every frame left writable and copied
each time (docs/NETPLAY.md, "Save, load and hash", "Only the pages
written"). `port_game`'s plan, the state's part of a boundary (all of
it on a machine other agents were using; `--whole-state` is the old
way):

| | `run_ahead 1` | `run_ahead 2` |
| --- | --- | --- |
| the window on the card (`port_bench.py`), before | 1.49 to 1.95 ms of 3.2 to 4.1 | 1.80 of 5.68 |
| ... after | 0.18 of 2.08 | 0.14 of 2.72 |
| Windows without a window (`rebushido-headless.exe`), whole / pages | 0.20 / 0.07 ms | 0.44 / 0.08 |
| Linux, 64-bit, whole / pages | 0.26 / 0.11 ms | 0.41 / 0.09 |

besides about 3 first writes a boundary that fault (2 microseconds each
on Windows, 5.5 in WSL). The traces are the plain run's with
`run_ahead` 0, 1 and 2 in both sizes and on Windows; `port-rollback`,
`port_net.py all` and the play tests `port_game` and `port_sound` pass.
What a frame ahead still costs besides its own step is its sound
(`sound`), which the sound chip makes as ever.

**The cast shadows projected by the card.** With the HD fighters the
shadows are cast from some 44,000 triangles a picture, and each picture
mixed every one of them between two frames and projected its corners on
the processor, then sent them to the card. Now a pair of frames is sent
once (`port_GlShadowSendPair`: each vertex the frame on show's corner and
the frame after it's, in the world's units), and a picture gives the
card the mix, the rotation, the camera and the projection as uniforms;
the vertex shader projects as `port_GlShadowProject` did and a geometry
shader leaves out a triangle with a corner nearer than `CAST_NEAR`. The
box the mask is cleared and darkened over is the projected corners of
the box in the world holding a frame's triangles, grown by 8 of the
console's pixels. The pictures of `port_game`'s fight with Mikado
against Kaun (`frame_rate original`, 142 pictures at 640 by 360) are the
same as before but for one, by at most 8 of 255 in a channel.

**The budget** (`tools/port_budget.py`, a step of `tools/gate.py`): the
window's build, the player's settings and the hd mod, the fight above,
`--profile` with `run_ahead` 0, 1 and 2 side by side. It fails where,
with nothing run ahead, the game, the software GPU, the sound and the
cloth take more than 26 ms of the window's thread's own processor time
in a fight frame (`RB_PROFILE_CPU`, which another program on the machine
does not lengthen; 20.5 here, of it the cloth 10; the gate also runs
this step alone, after every other); where
`run_ahead 1` costs more over `run_ahead 0` than a bare step of the game
and the state kept and put back by more than 4 ms; where `run_ahead 2`
costs more than twice `run_ahead 0`, or the cloth more than a fifth of
it; where the cloth is kept out more than once a frame written down
(0.4 here); or where more than 64 colour tables are sent (4 here).

### Pacing on a real display

**A hidden window on the card is held to the real display.** On the
owner's RTX 3090 (driver 616.92, Windows 11, a screen of 85 Hz) a
`--hidden` window with vsync on swaps at the screen's refreshes: the
median between two swaps of a paced fight is 11.53 ms (85 Hz is 11.76;
the shorter ones are swaps that came back within a refresh). So the
pacing is measured on the card itself, with nothing shown
(runs of the Windows build: `--hidden --window 2560x1440
--fight tatara-shrine kannuki mikado --profile FILE`, every default on,
the five mods, SDL's dummy sound). What a hidden window cannot show is
what Windows' compositor does to a shown one, full screen's own flips,
a screen of another rate than the PC's own (60, 120, 144: those are the
models below), and anything heard.

**What held the game back with vsync on.** In a fight the frame's blank
is 17 ms of the processor's work here (`run_ahead 1`, the HD fighters),
more than a blank, and its first picture's swap then waited for the
next refresh on top (5.5 ms, the median), and with `low_latency` for
the card as well: the game fell behind, a step at a time, and the time
was let go. Now **a blank more than a refresh late shows no picture of
its own** where the blank before showed one (`port_ShowPictures`): its
swap would make the game later still, and the screen shows the picture
before for that refresh either way. A step that runs long costs a
refresh of the picture, not the game's pace and not the sound.

| a fight, 40 s, 2560x1440 at 85 Hz (hidden) | vsync on | vsync off (the owner's) |
| --- | --- | --- |
| before the files with a C library were at `-O3` (more work a blank): time let go | 2 (with a stall of 50 ms made every 5 s) | 19 |
| ... the sound ran out | 4 | 16 |
| ... pictures a second | 66 | 55 |
| now: time let go | 0 | 0 |
| ... the sound ran out | 0 | 1 |
| ... pictures a second, and the spread between two | 81.2, 5.5 ms | 75.0, 2.9 ms |
| ... blanks a refresh late that showed nothing | 25 | 0 |

With `vsync = off` the driver's queue is never waited for at a swap, and
the processor is held up inside the card's batches instead (15 ms at
once, `card`), unevenly: where the processor's work was near a blank,
the time was let go nine times as often. With room to spare `off` comes
out as evenly paced as `on` (the clock holds the rate) but tears every
picture; `on` shows more pictures and never tears. `on` is the default
and the right choice.

**`low_latency = off`** was measured too: the swaps come back at once,
not at a refresh, so the moment each picture is shown is not known and
the pictures between frames are drawn for the wrong moments (a phase
went backwards between two pictures 890 times in the fight against 646
with it on). It stays on.

**`vsync = adaptive`** (`port_SetSwapInterval`: SDL's -1, where the
driver has it; `on` where not, and the log says so) shows a picture that
missed its refresh at once, torn, rather than a refresh late. On this
card in a hidden window it measured as `on` (the driver tears nothing
it does not show); on a shown window it is for the owner's screen.

**The display's rate made up** (`--display-rate N`) now swaps with no
interval of its own, so that a hidden window on a real card is held to
the made-up display and not to its own as well. Measured with it
(`frame_rate display`, the same fight):

| `--display-rate` | pictures a second | between two, median | 95% | time let go | not shown |
| --- | --- | --- | --- | --- | --- |
| 60 | 47.7 | 16.8 ms | 33.6 | 0 | 554 |
| 85, the real display | 81.2 | 11.7 | 22.2 | 0 | 25 |
| 120 | 66.0 | 9.1 | 25.8 | 0 | 24 |
| 144 | 83.9 | 7.0 | 21.2 | 0 | 26 |

The game kept its pace at every rate (no time let go); what gave was the
pictures. At 60 a fight's frame blank, 15 to 17 ms of work, misses its
refresh most times, and the blank is not shown; at 120 and 144 the
pictures are what the processor and the card leave room for, 66 and 84
a second. (These rows were measured while a hidden window was held to the
system's coarse timer, which slept the short waits a refresh and more:
"The step's refresh", below. With the fine timer, 144 misses 8 to 18% of
its refreshes, nearly all at a frame's step, and 3 to 5% with the step's
refresh.)

The card's own time for a picture of the fight is about 9 ms at
2560x1440 with `antialiasing = 2` (the ground's mesh 3.6, the sky 2.7):
**144 a second (6.94 ms) is beyond the card's share alone** until the
renderer's work comes down ("Measuring on the card"), and 85 is at the
edge of it; what the pacing does about that is draw fewer pictures
between frames, never slow the game.

### Sound that does not run out

The game's sound chip (`src/port/spu.c`) stays on the window's thread,
stepped at each blank in lockstep with the game: which voices have
ended and how far each envelope has come are read back by Sony's
library, so they are the game's state, saved and put back by run-ahead
and rollback and held to the emulator by `port_sound`. Its cost is half
a millisecond a blank. What runs on SDL's thread is the copy out of the
queue (`ring.c`), as before. The sound ran out because the queue was
shorter than the time a blank can run late: a blank's samples are made
at its end, so the queue has to hold a blank's (737), the card's gulp,
and whatever a late picture or step adds.

- The card is asked for **512** samples at a time (11.6 ms), not 1,024,
  and the queue is kept **3,584** long (81 ms), not 2,560: 53 ms of a
  stall is held. The sound is about 12 ms later than before (93 ms of
  queue and gulp against 81).
- Where it runs out all the same, the queue is made **256 longer**, up
  to 4,800 (109 ms), unless that was time let go (a stall no queue
  holds) or a load hurried through; the log says so.
- The log says why each time, the first twenty: `sound: ran out before
  blank 458: 1954 samples were queued after the blank before, which took
  29.6 ms, 5.0 of it work: TWEEN 4.3 ...`.

`tests/port/window.c` holds it on made-up time: a fight whose frame's
blank takes 20 ms, or 27, with a stall of 50 ms more every five seconds,
6,000 blanks: nothing runs out and no time is let go; the queue as it
was ran out at every stall; a stall of 70 ms runs out at each. On the
card, a paced fight with `--stall 50,300,1300` (the window stops 50 ms
at every 300th blank) ran out at none of the stalls (once, at the fight's load, as every run with SDL's dummy card does).

SDL's dummy sound card is not a clock (it takes its samples late and
unevenly), and an agent may sound nothing: **what a real card does with
the queue is for the owner's ears**, with the log's lines.

### After a hitch

`PORT_PACE_LOST` is still a tenth of a second: a blank later than that
is not caught up, the time is let go, and the sound has run out by then
whatever the queue. A shorter hitch is caught up at once, as before.
Two things changed:

- **The log says why**: `pace: blank 377 was 106 ms late, more than the
  tenth of a second the game catches up, and the time was let go (1
  times so far); the blank before took 39.7 ms, 27.6 of it work: TWEEN
  5.6 AHEAD 5.4 FINISH 4.1 CLOTH 3.1` (`PortPace.lost_by`,
  `port_LargestParts`).
- **No crawl after it.** A frame's length for the pictures between
  frames is the blanks' moments apart; across a let-go they jumped, and
  the next frame's pictures were spread over the jump, crawling through
  one frame. A frame that the moments say took more than four blanks now
  lasts what the frame before lasted (`FRAME_PERIOD_MOST`).

In the measured fight the hitches that let time go were the fight's
first frames (40 ms blanks, the side channel and the cloth worked out
the first time) and one stall of 115 ms in the swap 24 seconds in,
the same blank in every run: the card waited on something the renderer
sent (a texture or a program used the first time). Both are the
renderer's to find.

### A report (F9)

The parts' times are now counted always (two reads of the clock a
switch), and the card's passes timed always (`gl_bench.h`); the window
keeps the last 600 blanks of both and the last 2,048 swaps in rings
(`src/port/window/sdl_report.c`). **F9** (or `--report-at N`) makes
`logs/report-YYYYMMDD-HHMMSS-N/` beside the session's log: `picture.png`
(the window as F12 keeps it), `profile.txt` (the rings, in `--profile`'s
form, so `tools/port_profile.py` reads it), `session.log` (its last
256 KB), `rebushido.cfg`, and `about.txt` (the version, the system, the
card and its driver, the display and its rate, vsync, the sound card and
queue, the pace's account, the pictures shown and not, the settings that
cost time, the last blank's parts). Tried on the card: a report at blank
2300 of a paced fight is 3,250 lines of times and a picture of 2560 by
1440.

**The build's id** is the commit's (`ReBushido 0.1-dev (<commit>)`);
a build made in a worktree says `unknown`, since `tools/mirror.sh` asks
git from WSL, which cannot read a worktree's `.git` file of Windows
paths. The owner's copy is built in the main checkout.

### Measuring on the card

The owner's target (2026-11-18): **144 pictures a second at 2,560 by
1,440**, with run-ahead, and room for four players online, a larger
stage and richer models, on his Ryzen 9 9950X3D and RTX 3090. That is
**6.94 ms a refresh**. Mesa's software OpenGL says nothing of a real
card, so the measure is now taken on the card itself.

**OpenGL 4.5.** The window asks for a 4.5 core context (compute
shaders, direct state access, persistent mapped buffers:
`GL_WANTED_MAJOR`, `sdl_window.c`), and the provers' EGL for the same
(`gl_main.c`); a PC without it shows the console's own picture and the
log says "It needs OpenGL 4.5". The shaders are still `#version 330`,
which a 4.5 context runs as they are; new ones may be `#version 450`.
Mesa's llvmpipe in WSL gives 4.5 core, so the provers keep working.

**The card's time per pass** (`src/port/gl/gl_bench.h`): each pass of
the remastered picture is bracketed by two of the card's timestamps
(`glQueryCounter(GL_TIMESTAMP)`), `PORT_GPU_BEGIN`/`PORT_GPU_END`: the
game's primitives (`scene`, or `tween` while a picture between frames
draws them again), video memory sent and stretched (`vram`), the ground
as one mesh (`stage`), the HD fighters with their cloth and layer
(`fighters`), the shadows (`shadow`), the sky, the samples averaged down
(`resolve`) and the picture into the window (`present`). A pass inside a
pass counts to the inner one. What lies between passes, while the
processor works out what to draw next, is no pass's, so a blank's sum is
the card's busy time and not its wall time. The answers are read a few
blanks later from a ring of eight blanks, never waited for. A pass the
processor feeds slowly from inside still counts the card's waiting;
`RB_GPU_FLUSH=1` hands the card each pass as it ends (`glFlush`), which
settles whether a pass's time is the card's own (the stage and the sky
were the same with it, so theirs is). Off, a bracket is one test of a
flag.

`--profile FILE` now writes, after its `parts` line, a `gpu_parts` line,
and for each blank the card has finished a `g BLANK` line of
microseconds per pass (a few lines after the blank's own `b` line);
`tools/port_profile.py` reports both. The counter has a third state:
**F3** steps through off, on and **times** (`fps = times` in
`rebushido.cfg`, "Counter: With times" in the menu), which adds three
lines under it, the means of the last half second's frames: the
processor's work and the card's time with their worst, then the four
largest parts of each (`C CLOTH 8.2 AHEAD 6.6 ...`, `G STAGE 4.0 SKY
3.1 ...`). So a picture of the owner's screen says where the time goes.
All the game's work is on the window's thread; SDL's sound thread only
copies from the ring.

**The benchmark** (`tools/port_bench.py`, BUILD.md "The gate"): the
Windows build with `--hidden` on the PC's own card, SDL's dummy sound,
its own settings (`tools/port_budget.py`'s, every default on), no cards,
at 2,560 by 1,440, unpaced (`--fast`, one picture a blank), the five
mods, through port_game's plan with Kannuki against Mikado on Tatara
Shrine (the plan's addresses are the Windows program's own, from its
`.sym`: the Linux build's plan never leaves "fight (loading)"). The
cases: `ra0`, `ra1`, `ra2`, `ra1-plain` (fighter models off) and
`ra1-stress`, `--bench-stress fighters=4,stage=2`: each HD fighter drawn
twice, the copy beside him, and the ground's mesh drawn twice over
itself, for four players on a larger stage. The stress touches nothing
of the game; the copies' cloth is the same pose's, kept out once, and
they cast no shadow, so the processor's share of four players is
understated by about a cloth and a shadow per fighter. Each case gives,
over the fight's blanks, the processor's work (every part but the waits
for the clock and the display) and the card's time: medians, 95th and
99th centiles and the worst, the parts' medians, the hitches (blanks
over 6.94 ms) and the stalls (blanks over three times a frame's median:
a first use). Every result is kept in the main checkout's
`build/bench/<commit>/results.json`; `--against REV` compares with
another commit's without running it again. `--set KEY=VALUE` changes a
setting of every case for a trial (`--set antialiasing=off`), whose
numbers are kept apart, as the commit's name with `+KEY-VALUE`.

**Measured** (2026-11-18, `9f55dce56` with this instrument, RTX 3090,
driver 616.92; another agent's runs were on the machine at the same
time, so a millisecond either way is noise). A blank in which the game
drew a fight frame, milliseconds:

| case | processor median | 95% | worst | card median | 95% | hitches |
| --- | --- | --- | --- | --- | --- | --- |
| `run_ahead 0` | 29.1 | 38.4 | 92.4 | 10.7 | 14.0 | 62% |
| `run_ahead 1` | 27.6 | 32.2 | 65.1 | 9.7 | 10.6 | 50% |
| `run_ahead 2` | 26.3 | 31.2 | 70.8 | 12.8 | 15.8 | 50% |
| `run_ahead 1`, fighter models off | 12.8 | 14.7 | 34.9 | 7.7 | 8.7 | 52% |
| `run_ahead 1`, four fighters, the ground twice | 26.1 | 33.6 | 62.7 | 11.8 | 14.5 | 91% |

Where it goes, `run_ahead 1`, medians of a frame's blank:

- **the processor (27.6 ms)**: the HD cloth 8.2, the frame run ahead
  6.6, the picture between frames 3.8, the software GPU 2.3, the
  shadows 1.9, the side channel 1.2, the game 1.2, the card's batches
  0.8, the sound 0.6. With the fighters' models off, 12.8: the HD
  fighters cost the processor some 15 ms a frame.
- **the card (9.7 ms)**: the ground's mesh 4.0, the sky 3.1, the
  shadows 0.8, video memory 0.7, the fighters 0.6, the resolve 0.4, the
  window 0.1, the game's own primitives under 0.1. The fighters are
  cheap for the card; the ground and the sky are not, both drawn at the
  supersampled size (`antialiasing = 2`), and the stress's second ground
  takes the stage to 6.2.
- **the stalls**: the fight's first frames (90 ms at its second blank,
  most of it the side channel), then every frame whose cloth is new.

So the processor is the limit, by four times the budget, and the card
too, by half again: **neither fits 144 a second today**, and the card
does not fit 85 (11.8 ms) with run-ahead 2. What fails is listed by the
budget below.

**The card's share, cut (2026-11-18, later).** The ground and the sky
were the card's whole problem, and both were fill: with `antialiasing
off` they came to 1.1 and 0.8 ms, a quarter of what they cost
supersampled. Three changes, `run_ahead 1`, card medians in ms (other
agents' work was on the machine, so the processor's numbers here are
not comparable with each other; the card's are):

| | ground | sky | the card, a frame |
| --- | --- | --- | --- |
| before | 4.02 | 3.08 | 11.1 (9.7 measured quiet) |
| the sky's clouds read from a cube worked out once ("The sky") | 3.85 | 1.08 | 8.3 |
| ... and the edges multisampled, not supersampled ("Edges") | 1.25 | 0.27 | 5.2 |

The ground's faces that are left out (turned away, nearer than the
cutoff, opened up for a fighter behind them) are now left out by their
vertices, before the card rasterises them (`gl_stage.c`), where each of
their pixels was thrown away before; it bought little (4.02 to 3.85),
being fill the depth test had mostly stopped already. The picture: the
2D layer is block for block what it was (`--gl-blocks`, multisampled,
`port_game`'s flat screens: no block uneven), and a fight on the card
differs from the supersampled one in 1.1% of its pixels by more than 8
of 255, at edges and inside textures, which multisampling shades once a
pixel; the pair is in the owner's `ReBushido Screenshots\Performance
2026-11-18`.

Every case, with the fighters' agent's work of the same day merged
(`f10dcbdda`; the card's medians and 95th centiles, ms): `run_ahead 0`
4.4 and 7.2 (before, 9.5 and 11.0), `run_ahead 1` 4.0 and 8.5 (11.1 and
20.3), `run_ahead 2` 9.3 and 13.9 (16.8 and 24.0), fighter models off
3.0 and 4.1, four fighters on the ground twice 5.2 and 9.6 (22.0 and
27.5). With `run_ahead 2` the video memory pass is 3.7 ms of it, against
0.6 in the other cases, and every other pass is half again: the card
draws each frame twice, as the first frame ahead of one boundary is the
last of the boundary before (`port_BeforeStep`, `step.c`). Leaving the
first out (`g_port_unseen`, as the step after them is) would halve it,
but what that frame writes into video memory would then not reach the
card's mirror where its input differs from the boundary before's; the
mirror needs to be told of a write without drawing it first.

**Now it is** (`g_port_ahead_kept`, step.c): with two frames ahead and
more, every frame ahead but the last, which the boundary before ran and
the card drew already, is run for the state alone. The card is not
given its primitives, nor its frame's beginning and end; its copies,
fills and loads go into the mirror and are drawn nowhere (the backend's
`kept_vram`). That happens only where the boundary before ran as many
frames ahead in full, so the card has drawn the frame. `run_ahead 2`,
the card's median 8.6 to 4.25 ms a frame (`run_ahead 1`: 3.9), the
video memory pass 3.7 to 0.31. The pictures, every eighth at 640 by 360
against the build before: 410 of 419 the same. The fight's differ in
the HD cloth's folds, now stepped once a frame as with `run_ahead 1`
and not twice. A menu's animated preview is a frame of its own earlier,
being the card's picture of the boundary before. Every other difference
is under 50 pixels, of the size two runs of one build differ by.

**The mirror sent without waiting** (2026-11-18, the performance run).
On Windows the processor's parts are the window thread's wall time, and
a large share of it was the thread waiting in NVIDIA's driver for the
card. It waited at every write of the card's mirror of video memory: a
texture the card may still be reading can only be written once the card
has drawn everything given it before. A fight frame writes video memory
between its draws (the fog's colour tables, three times), and a picture
between frames wrote again what its frame wrote and then put back all
of it, some 400,000 pixels, the frame's cleared screen included.
Three changes, `run_ahead 1`, the bench's fight, medians:

- **Only what changed is sent.** A written rectangle is held against
  the copy the backend keeps of what the card has, and only its lines
  that differ are sent. A frame's clear is the same as two frames
  before, and so are most of its colour tables. The rectangles waiting
  are kept apart (`MIRROR_RECTS`), not as the one rectangle about all of
  them, which, with the screen at one end of video memory and the fog
  at the other, was most of it.
- **A picture between frames puts back what it changed** of that copy
  (`port_GlMirrorHold`, `port_GlMirrorGiveBack`, an undo of its lines),
  not what video memory holds, which is the software GPU's picture where
  the frame was cleared and is no business of the mirror's. With
  run-ahead, the window's sending all of video memory before the frames
  run ahead was there only for that put-back, and now happens only where
  a picture's changes were too many to keep (`port_TweenTakeMirrorBehind`).
- **The mirror is a buffer**, which the card reads as a texture a line
  after another (`PORT_GL_VRAM_SHADER`, gl.h), so a write is a
  `glBufferSubData` the driver queues in its turn among the draws, and
  neither the processor nor the card waits.

The processor's `card` part went from 1.0 to 0.24 ms, the tween from
3.7 to about 2.7, the video memory pass on the card from 0.55 to 0.15 and
the card from 4.8 to 3.4 ms a frame; the ground is unchanged at 1.28. What
was tried and measured worse, all on the card: sending the texture
through a pixel buffer, orphaned or mapped for good (the video memory
pass 6.6 to 9 ms: the card then waits where the driver did); a ring of
mirror textures, copied on the card or brought up to date from the
processor (7 to 8 ms); and the small writes alone in a buffer, looked up
in a table at every texel (the ground 1.3 to 3.3 ms). The pictures: the
GL prover's figures are main's to the pixel, and on the card the budget
fight at 640 by 360, every eighth picture, 378 of 419 are identical to
main's (the counter left out). The other 41 are within 8 of 255, but
for a loading screen's fade a step apart and a few pixels that also
differ between two runs of one build.

All of it against main `a7f754af8`, which has the fighters' cloth on
the card and the window built `-O3`. The two programs were run one after
the other on the 3090 (`port_bench.py --program`), medians of a fight
frame's blank, in ms:

| case | processor, main | this | card, main | this |
| --- | --- | --- | --- | --- |
| `run_ahead 0` | 7.64 | 4.52 | 6.41 | 4.56 |
| `run_ahead 1` | 6.22 | 5.45 | 4.94 | 4.67 |
| `run_ahead 2` | 9.45 | 7.46 | 6.04 | 4.88 |
| `run_ahead 1`, four fighters, the ground twice | 6.64 | 7.03 | 5.23 | 5.62 |

The waits on the driver (`card`) went from 0.6–2.0 ms to 0.13–0.21. The
stress case is slightly worse on both. Its ground read 2.44 ms against
1.80, though nothing here changes how the ground is drawn: probably
noise, not proved.

**The colour buffer's targets are kept for each size**
(`port_GlTakeTargets`): the fight's picture has another scale than the
menus', and making the multisampled targets again at every way into a
fight held the window's thread for some 20 ms of the fight's first
picture.

**The budget** (`port_bench.py --budget`, the gate's step `port-bench`,
run alone after every other with `port-budget`): the cases `ra0`, `ra1`,
`ra2` and `ra1-stress`, each held to 6.94 ms for the 95th centile of a
frame's work on the processor and on the card; with `run_ahead 1` also
any blank's work, at most 1% of blanks over 6.94 ms and no blank over
twice it. The gate runs the step with `--report-only`, so it says what
is over and passes, **until the targets are met: then take
`--report-only` out of `OWN` in `tools/gate.py`**. What is met with room
to spare is held whatever `--report-only` says (`MET_LIMITS`): since
2026-11-18 the card's 95th centile of a fight frame with `run_ahead` 0,
1 and 2 (3.9 to 5.2 ms against 6.94); the stress's (5.6 to 5.9) and
every processor's measure are still reported only. Where there is no card (CI, WSL), no Windows build,
no disc or no hd mod, it says so and passes.

**The worst blanks** (2026-11-18, later): a game is as smooth as its
worst frames, so the budget also holds, after the fight's first second
(its load's end, `SETTLE_BLANKS`), every blank's whole time on the
window's thread but the wait for the clock, the swap included, since a
card waited for is waited for there: `settled_p99` at most 6.94 ms
(`ra1`, `ra2`, `ra1-stress`) and `settled_worst` at most twice it
(`ra1`). Those are reported only, with the rest. **A stall fails now**:
`settled_stalls`, the blanks after the fight's first second over
`STALL_HARD_MS` (50 ms), must be none in every case, whatever
`--report-only` says, and with `--against REV` there may be no more
than that commit had.

### Stalls

**The hunter** (`tools/port_stalls.py`): the Windows build on the card,
hidden, as `port_bench.py` runs it (2560 by 1440, unpaced, the budget's
settings, the five mods), through long sessions: port_game's plan from
the boot through the menus, a fight, its replay, the menus after and a
second fight, with F9's report, the settings menu (F1) and F12's
picture on the way; and fights from `--fight` on Lost Bridge, Castle
Rooftop, Bamboo Thicket, Kabuki Stage, Ferry Dock and Snake Cliff, with
other fighters, all three `run_ahead`s, and one with F2 pressed every
400 blanks. Each runs with `--profile` and with **`--sample FILE`**
(`src/port/window/sampler.c`, the 64-bit Windows program): a thread of
its own stops the window's thread a thousand times a second, keeps its
stack (the system's unwinder, which reads the driver's unwind
information as well as the program's; the game's own files have none,
so a stack ends at the first of them) with its blank and its part, and,
while the window's thread has waited outside the program for three
samples in a row, samples every other thread of the program too. The
report (`build/stalls/report.txt`) lists each blank over 25 ms, a load's
apart (a `(set-up)` or loading screen, a screen's first blanks, a
fight's first second), with its screen, its parts, the card's passes,
the functions it was in (the program's innermost on each stack, behind
the modules under it: `[nvoglv64.dll>KERNELBASE.dll>ntdll.dll]
port_GlUploadMirror < ...` is the window's thread waiting in the
driver) and what the other threads were doing meanwhile.

**What it found** (RTX 3090, driver 616.92, `main` at `77ee9f758`;
another agent's program was on the card, so a few milliseconds either
way are noise):

| where | how long | why | now |
| --- | --- | --- | --- |
| about 30 s after the program starts, whatever is on the screen (a fight, a menu: the "115 ms at blank 1790") | 92 to 180 ms, once | NVIDIA's OpenGL driver holds the drawing thread up, once, at the first thread of the process to end; a thread of CRYPT32 that the system starts as the program opens ends when it has been idle for half a minute. Shown by ending a thread at blank 1000 on purpose: the stall went there, and the CRYPT32 thread's end cost nothing. The samples: the window's thread in `nvoglv64.dll` waiting, no other thread busy, that thread gone at the same blank | `warm.c`: a thread made and ended at the eighth picture, on the loading screen (73 to 85 ms there). At the first picture it was not enough. |
| every blank | about 1.9 ms of waiting a blank on average, a fifth of the window's thread | `port_GlUploadMirror`'s `glTexSubImage2D` from the program's memory into the mirror the card is still reading makes the driver wait for the card | the renderer's: the mirror as a buffer texture written by `glBufferSubData`, only the rows changed (renderer agent's branch). A pixel-unpack buffer moved the wait onto the card instead (7 ms a frame). |
| a fight's first frames | 50 to 130 ms | the shadows' program and mask, the fighters' program and layer, all first used there; the picture's targets made again because the scene's scale differs between the menus and the fight (`port_GlMakeTarget`, 20 ms) | the fighters' warm-up at load (fighters agent's branch); targets kept for each scale (renderer agent's) |
| the top menu, as it opens | 40 to 58 ms, three times | some forty of the `ui` mod's pictures handed to the card in one blank, each a `glTexImage2D` and `glGenerateMipmap` | `port_GlLoadCollect` stops 2 ms after its first picture (fighters agent's branch) |
| F9 and F12 | 61 to 96 ms | the window's PNG of 2560 by 1440 encoded and written on the drawing thread | encoded and written on a thread of its own: F9's blank 38 ms (the window read back, the report's files), F12's 26 |
| the settings menu (F1) opened in a fight | 39 ms | the menu's first picture | not yet |

The fights themselves, past their first second, had no blank over 50 ms
in any session but those of the table; what is over the 144 Hz budget
there is every frame, the averages' work. The benchmark's own fight
(blanks 2915 to 3257, long after the 30 seconds) is the same before
and after these changes, within the noise of the machine's other work
(`ra1`: after the first second 99% 31.4 and 38.4 ms, worst 33.3 and
46.9; no blank over 50 ms in any case, before or after). The window's
pictures (`--tap-window`, 640 by 360, `frame_rate original`, the
counter off, a fight on Lost Bridge) are the same to the byte as
before, as two runs of one program are.

### The software GPU beside the game

**What the worst blanks were** (2026-11-18, main `984661052`, the
bench's fight after its first second, the numbers in the main
checkout's `build/bench/984661052/`): with the game at 30 frames a second and the
bench unpaced, every other blank is a frame's (`F`) and the one between
does a picture and little else. The between-blanks were 1.3 to 2 ms at
their worst; **every blank over 6.94 ms was a frame's**, and none was a
stall, a swap held up by the card or a periodic job: the frame's own
work, in one blank. `run_ahead 1`, a frame's blank: the software GPU
3.7 ms, the picture between frames 2.2 (0.65 of it pairing the two
frames), the frame run ahead 1.3, the side channel 1.1, the game 1.1,
the sound 0.9 (the frame ahead's mix). The swap was under 0.2 ms.

**The painters** (`src/port/paint.c`, `include/port_paint.h`). The
software GPU's primitives are painted on threads of their own in the
window (`RB_PAINTERS=N`, 4 by default, `off` to paint on the game's
thread as before; the tests, the provers and every Linux program paint
as before). `raster.c` takes every word on the game's thread: the
settings, the copies, loads, stores and the fill are done there; the
polygons, rectangles and lines go into a ring that every painter reads,
each painting only its band of each 256 lines (a primitive that is not
in a painter's lines is passed by before anything of it is worked out),
so each pixel is painted by one painter, by every primitive in order.
Each square of video memory, 64 pixels by 8 lines, keeps the last
primitive given that may write it (its corners' rectangle in the
drawing area) and read it (its texture page and colour table). What
reads video memory on the game's thread waits for the primitives that
write what it reads; what writes it, for those that read or write it;
the state's save, load and hash and the console's picture wait for all
of them. A primitive that reads what it draws, or draws over what an
earlier one still reads, and, in a step whose frames were run ahead, a
copy or load of a colour table between the frame's draws, are done by
the first painter alone behind a barrier of all of them, so that the
game's thread does not wait there. The painters' settings are not the
state's: they take the GPU's again after a load. Video memory is held
to the byte: the budget fight's video memory at its end is the same
with the painters as without, in every case and in repeated runs; the
pictures (640 by 360, every eighth, `frame_rate original`, counter off)
differ in 6 to 9 of 419, the same pictures and by as much as two runs
of one program differ (loading screens' fades, the cloth).

**The pairing beside the game** (`gl_tween.c`, `port_TweenSetWorker`):
the pairing of two frames' primitives, 0.65 ms, is worked out on a
thread of the window's as soon as the frame after is whole (with
run-ahead, as the frame ahead ends, while the step itself runs), not in
the first picture between them (`RB_PAIRING=game`: as before).

**What it bought** (`run_ahead 1`, a frame's blank, the same program
with `RB_PAINTERS=off RB_PAIRING=game` against itself, interleaved, on
a machine other agents were also using): the median 7.5 to 6.0 ms, the
99th centile after the first second 10.6 to 7.6, the software GPU's
part 2.3 to 0.6 and the picture's 1.5 to 0.7. The gate's bench at
`f695523ea` against main `984661052`, `run_ahead 1`: after the first
second, the 99th centile 14.7 to 7.4 ms and the worst 17.3 to 8.3 (the
worst now within its limit of 13.9); the frame's median 10.8 to 5.9.
The other cases moved as much in runs on a quiet machine and were lost
in other agents' load in the gate's. What is left of the
frame's blank is the wait at the next boundary: the game draws its
frame's list (DrawOTag) at the very end of its step, right before the
frames are run ahead and the state is saved, so the painters have
little else to overlap and the save waits for them (`ahead` 1.0 to 2.0
ms). With the clock paced (`vsync off`) it is the same: the list is
drawn after the step's last blank.

**Tried, and left out:**

- *The loads and copies of a step drawn ahead given to one painter*,
  never waited for: the wait moved whole to the save at the boundary.
- *Run-ahead saving and putting back all but video memory*, and
  putting back what the frames ahead wrote of it from a log of their
  writes (`port_StateSaveButVram`), so that the boundary does not wait
  for the painters at all: it took the boundary's wait away (the
  painters waited for 1.0 s in all of a run, then 0.02), but video
  memory's top lines differed from the painters-off run's on the
  loading screens in some runs, a race not found, and it was taken out.
- *Lines a band of eight a painter* (each painter every fourth band of
  eight lines): every painter worked out every primitive, so four were
  little faster than one; bands one after another let most primitives
  be one painter's.

**What is left** for the worst blanks: the boundary's wait for the
painters (about 1 ms; a save that needs no painted video memory is the
way, the log above with its race found, or the HANDOFF's painting on
demand); the frame run ahead's own work (its game, its side channel);
the sound mixed for the frame ahead (0.3 ms a frame ahead; the voices
must step, the mixing need not). Since done: "The step itself as the
first frame ahead", below.

### The step itself as the first frame ahead

**What was done twice.** With `run_ahead 1` a boundary saved the state,
ran the step with the input just read and the card drew it, put the
state back, and ran the same step again with the same input, painted and
heard and not drawn (`g_port_unseen`). The two compute the same: a step
ahead differs from the step itself only in what the host does with it
(it is not heard, the window has no turn at its blanks). What the step
ahead was for is the order: the game draws a frame's list at the end of
its step, after the step's blanks, and the window shows a blank's
picture at the blank, so the step's own frame would be shown only at the
next step's blanks.

**Now** (`step.c`, the window's build): where frames are run ahead, the
step itself is the first of them. Its blanks' turns of the window are
held (`g_port_holding`, `clock.c`): at each, the card's frame is ended as
at a frame ahead's blank and the sound's place is noted
(`port_HeldBlankTurn`); the step is drawn by the card and painted, as
any step with nothing run ahead; when it has ended (`port_AfterStep`),
the frames ahead but one are run from the state it left as before, the
display is kept (`port_AheadDone`), the painters finish, and the held
blanks are given their turns one after another, showing it
(`g_port_unseen`). A blank's pace is reckoned from the sound's samples
as they were at it and the queue less what the step mixed after it, so
paced play waits as it did. A load beginning, or a step of more than
four blanks (the movie), ends the holding there: its blanks are shown as
they come, and nothing is run ahead after it. So **with one frame ahead
no state is saved or put back at all**, and a frame's blank costs about
what it costs with none; with two, one frame is run ahead from the
step's state. The picture shown at each blank is the one the frames
ahead showed before, and the input is read at the same moment, so the
lag is what "Input lag" measured. `RB_AHEAD=old` runs every frame ahead
from a saved state, as before (a trial); a host without the held hook
(the headless programs, the tests) still does.

**The save after the step** (`port_StateSaveAhead`, `state.c`), with two
frames ahead: video memory is neither waited for nor kept. The painters
go on painting the step while the frame ahead runs, which paints
nothing; each write of the GPU's side of the game's thread in it (a
copy, a load, a fill, `raster.c`) first waits for what is still to be
painted in its way, as ever, and keeps the pixels it writes over
(`port_StateKeepVram`, the exact rectangle, round video memory's edges
as the GPU's addresses go); putting the state back puts those back, the
last first, and waits for nothing. A step ahead with more to keep than
room (four times video memory) is given up before the write. The
snapshot notes that it holds no video memory (`is_vram_left`), so a
whole save into it takes all of video memory again; a painted primitive
while the pixels are kept fails the run. The race the log of before had
("Tried, and left out", above) was not found; the likeliest cause is
gone with the order: the step itself was `g_port_unseen` then, its
loads and copies given to the painters to do in their turn
(`port_PaintGiveAlone`), and it ran after the frames ahead, so a log
kept at the next boundary began with such writes still to be done. The
step itself is never `g_port_unseen` now, and the pixels kept are the
exact rectangle a write waited for.

**A frame ahead's sound** (`g_port_spu_quiet`, `spu.c`): the chip steps
as ever -- pitch, envelope, place in the waveform, the blocks and their
flags, what the game can read -- and mixes nothing (no volumes, reverb,
capture or disc's sound; silence), but the output of a voice that moves
the next one's pitch. `RB_AHEAD_SOUND=mix` mixes it as before.

**Held to the same game.** The bench's fight on the card (640 by 360,
`--trace`, `--vram` at the end), `run_ahead` 0, 1 and 2 and the old way
with 1 and 2: the traces are the same to the byte (1,786 lines), and so
are the plan's checkpoints and video memory at the end. The pictures
(`--tap-window`, every eighth blank, `frame_rate original`, the counter
off, `RB_TEXTURES_AT_ONCE=1`), against the old way: `run_ahead 1`, 414
of 419 the same; `run_ahead 2`, 411 of 419. The others differ at the
cloth's edges (at most 54 pixels by more than 16 of 255, one picture
385, one at the fight's first second 732).

**Tried, and no faster**: not waiting for the painters before the held
blanks' pictures (`RB_DONE_SYNC=off`: they have finished by then, while
the side channel decoded the step and the card was given it), and the
painters' lines in groups of 32 or 16 a painter in turn
(`RB_PAINT_LINES=N`, `g_port_paint_band_shift`) for an even share of
the fight's middle lines (painter 3 of 4 paints a third of what painter
1 does). Both are kept as switches; neither moved a measure by more
than the noise.

**Measured** (RTX 3090 at 2,560 by 1,440, the bench's fight, one
program run with `RB_AHEAD=old` and without, interleaved, two runs each,
medians; other agents' builds and runs were on the machine throughout,
and moved a case's numbers by 2 ms from one run to the next). The window
thread's own processor time (`RB_PROFILE_CPU=1`, its cycles: other
programs taking the processor do not lengthen it, those sharing its core
still do), a fight frame's blank and every blank after the first second,
ms:

| case | frame's median, old | new | 99% after the first second, old | new |
| --- | --- | --- | --- | --- |
| `run_ahead 1` | 7.92 | **6.83** | 10.11 | **8.37** |
| `run_ahead 2` | 12.72 | **10.10** | 15.70 | **12.63** |
| `run_ahead 1`, four fighters, the ground twice | 7.50 | 7.44 | 10.30 | **9.06** |

and on the clock (the budget's own measure), `run_ahead 1`: the frame's
median 7.39 to 6.09, the 99th centile 9.93 to 8.05. The gate's bench
(`1cea709e0`, two provers running beside it) against main's
(`2319cca13`, measured the same day under other agents' load), every
blank after the fight's first second, 99th centile and worst, ms:

| case | main | now |
| --- | --- | --- |
| `run_ahead 0` | 10.30, 11.68 | 10.97, 12.29 |
| `run_ahead 1` | 14.71, 16.45 | 11.48, 11.97 |
| `run_ahead 2` | 19.25, 19.53 | 11.64, 12.10 |
| `run_ahead 1`, four fighters, the ground twice | 19.12, 20.59 | 11.02, 12.38 |

So a frame run ahead now costs about what a frame with none costs, and
two little more; `run_ahead 0` is not changed by this (its 0.7 is the
load's). With the frame ahead
mixed (`RB_AHEAD_SOUND=mix`), `run_ahead 2`'s frame is 0.3 ms dearer.
Where a frame's blank goes now, `run_ahead 1` (the profile's parts, and
the window's thread sampled, `--sample`): the game 1.7 (its step, and the
card's frame begun, which lays the shadows), the software GPU's side 1.9
(of it about 0.85 waiting for the painters inside the step: a colour
table loaded again, a copy or a fill, waits for the primitives given
before that read or write the place, where the step run after the
frames ahead gave those to the painters and waited for nothing), the
side channel 1.45, the first picture after the frame 0.7 of its own and
**0.75 waiting for the pairing** of the two frames, which began as the
frame ended and could overlap nothing (it overlapped the step itself,
before), the sound 0.4, the card's batches 0.25. The budget's
`settled_p99` is still over 6.94 ms in every case on this machine; on a
quiet one `run_ahead 1` was at 7.4 before this.

**What is left**, in the order it would pay:

- *The tween on the card* (HANDOFF, "What is left of the performance work"): the
  pairing waited for and the replay of the primitives, some 1.4 ms of
  the frame's blank and as much of the blank between.
- *The painters' waits inside a drawn step*: a load in the painters' way
  given to them (`port_PaintGiveAlone`), its pixels kept for the side
  channel and the card's mirror, which read them at once, and the side
  channel's reads of video memory (the textures' keys, a colour table)
  from a view of it as the game's thread has written it, painted pixels
  waited for only where a primitive still to be painted draws there.
  A step the card is given only the writes of (`g_port_ahead_kept`, the
  step itself with `run_ahead 2`) needs only the first half.
- *The side channel* beside the game, as the painters are.

### The tween off video memory

**What the window's thread did for a picture** (2026-11-18, the bench's
fight on the RTX 3090, `run_ahead 1`, main `598fddd73`; sampled with
`--sample` and timed section by section): the picture between frames
(`tween`) was 1.2 to 1.5 ms of a frame's blank and the side channel
(`remaster`) 1.0 to 1.15. Neither was the replay of the primitives
itself:

- **Half the picture was waiting.** A picture wrote the frame on show's
  writes of video memory back into it and put them back after, so it
  held the painters still (`port_PaintHold`) until they had painted the
  step just ended; and the first picture after a frame then waited for
  the pairing, which took 1.1 ms on its thread.
- **The fighters' colour tables were looked for at every primitive.**
  `port_GlFighterFindPlace`, which works out where a model's colour
  tables are from those its primitives read, ran again at every textured
  primitive of every fighter, in the frame and in every picture, while
  the place stayed unknown (two of the file's tables a shift apart, both
  seen): the file's tables by the file's tables by the sightings, some
  1,500 cycles a primitive, 0.3 ms a picture.
- **A square root was forty divisions.** `port_GlFighterRoot`, three a
  bone of each pose, took Newton's forty steps where six come to rest;
  posing the fighters for a picture was 0.11 ms.
- **The frame's clear was looked at pixel by pixel**, 640 by 240, at
  every frame and every picture, to tell whether it is a fill and no
  picture of a mod's (`port_ReplaceIsPlain`).

**Now:**

- **A picture touches neither video memory nor the painters.** Where
  the renderer is the card's (`port_GlBackend`), the frame on show's
  writes are given again from the frame's own pixels into the backend's
  copy of what the mirror holds (`port_GlReplayVram`), and what the
  renderer and the files about it look up in video memory while a
  picture is drawn -- a mod's pictures by their keys, the stage's pages,
  the fighters' colour tables, the clear's colour -- is read from that
  copy (`g_port_gl_vram`, `port_GlReadMirrorCopy`; outside a picture it
  is `g_port_vram` as before). The undo of the mirror's lines puts the
  copy back after. So nothing of a picture between frames reads or
  writes what the step is painting, which is also what a step on a
  thread of its own needs (the frame-loop work of the same day).
- **The pairing:** a moved thing's faces walked by thing
  (`port_TweenPairMoved`, its lists), not the whole frame for every pair
  of a thing that lost its name and a thing that may have it; the points'
  tables emptied by the slots filled, not 3 MB cleared a pair; and the
  frame after's table taken over as the next pair's frame on show's.
  1.16 ms a pair to about 0.3.
- **The fighters:** the colour tables' place not looked for again with
  the same sightings and model (`tried_count`); a loaded model handed
  back without its name made again (`snprintf`) or the loader's stack
  frame probed; the square root stops where the steps come to rest --
  a step that gives what it was given, or what the step before was given
  (back and forth) -- the same number to the bit (fifty million numbers
  against the forty steps, none different).
- **A fill** looked at by its first line and each other line against it
  whole (`memcmp`); **the stage's pieces** not searched for an object
  past the last of them (`port_GlStagePieceOf`); **the painters'
  squares** also kept by blocks of a texture page's height, the latest
  count of any of their squares, so a page read before a textured
  primitive is four blocks and not 128 squares (`paint.c`).

**Measured** (the bench's fight, `run_ahead 1`, main's program and this
one run one after the other twice, `port_bench.py --program`; other
agents' work was on the machine), medians of a fight frame's blank, ms:

| | main | this |
| --- | --- | --- |
| the picture between frames (`tween`) | 1.24, 1.15 | **0.28, 0.31** |
| the side channel (`remaster`) | 1.08, 1.00 | **0.44, 0.43** |
| the card's batches (`card`) | 0.18, 0.16 | 0.09, 0.09 |
| the shadows | 0.12, 0.11 | 0.06, 0.06 |
| the software GPU (`raster`) | 1.55, 1.60 | 1.70, 1.65 |
| the processor, a frame's blank | 6.03, 5.63 | **4.11, 4.03** |
| ... its 99th centile | 8.78, 8.30 | 7.71, 7.00 |

The software GPU's part is a little larger: the painters are no longer
made to finish by the picture between frames, so the next step's waits
for them (a colour table loaded again, `port_PaintSyncWrite`) find more
still to paint. The card's time is unchanged. The other cases, one run
of each program on a machine loaded by other agents' work (main's run
was the more loaded, so only the shares are worth reading), `tween` and
`remaster` in ms: `run_ahead 0` 2.49 and 2.41 against 0.70 and 1.15,
`run_ahead 2` 2.23 and 2.09 against 0.61 and 1.11, four fighters on the
ground twice 2.65 and 2.27 against 0.52 and 0.82.

**The pictures**, held to main's programs run beside them on the card:
`port_gl_prove.py` gives main's numbers to the pixel (623,545, 6,805,770
and 648,625 pixels against the software GPU at scale 1; the flat
screens' 8,609, 8,626 and 8,744 a step out), and on llvmpipe its own
(608,536, 6,786,976 and 634,701; 8,037, 8,037 and 8,154); every line of
`port_fps_prove.py` is main's but its times (on llvmpipe too: every
frame in order, the first picture the game's own, no corner off its
line); `port_stage_prove.py`'s measures of all 38 stages are main's but
the seconds. The traces are the same with the pictures on and off.

**Not done, and next:** the picture between frames is still the frame
on show's primitives given to the backend again every picture (some
950 a fight's picture, 800 of them a fighter's, which the backend leaves
out for the HD model): a list of what the first picture of a pair drew
(batch state and vertex ranges, the blits, the mesh at its event) kept
and replayed, with the corners mixed by the vertex shader, would make a
picture uniforms and a hundred draws. What a picture still decides per
phase (a primitive held where moved it would not be drawn, a face
coming or going drawn while it turns the right way) is per primitive,
so it needs the primitive's corners in each vertex or a geometry
shader. And the side channel still draws each frame on the card while
the step runs; a step that only writes its frame down, drawn by the
window afterwards (`port_GlStepRecords`, `port_GlDrawPending`, agreed
with the frame-loop work) needs a frame's own copy of what the backend
reads live today: the fighters' views (`port_GetFighterView`, read at a
frame's first fighter primitive), a node's kind read from the game's
memory at every fighter primitive (`port_FighterIsCloth`,
`port_FighterIsTinyBody`), the stage's view, the shadows' casters and
the cloth's points. Since done: "The step on a thread of its own",
below.

### The pause and the frames ahead

**What was wrong** (2026-11-18, the owner on `598fddd73`): with
`run_ahead 2` and the remastered picture the pause menu did not show;
the menu worked, the fight's picture stood still under nothing. Since
`9ae3bb306` ("run_ahead 2: the frame the card drew at the boundary
before is not drawn again") a step the card drew already as the last
frame ahead is run again with `g_port_ahead_kept`: its commands reach
the card's mirror of video memory and nothing else, and its blanks end
no card's frame. `port_RemasterPause` still gave the pause's holder
(`gl_pause.c`) its turn in such a step, and the holder draws the
fight's kept picture whole, straight into the colour buffer. The pause
menu's loop draws its panel after its `VSync`, so the panel of the frame
ahead is in the card's frame that the next step's blanks end; that next
step is the kept one, which ended nothing and drew the fight over the
panel. `run_ahead 0` and `1` have no kept step, the original picture no
holder, and `RB_AHEAD=old` and `RB_PAINTERS=off` both still showed it.
**Now** a kept step notes the pause as ever (the table it would draw
again is passed by) but does not call the holder: the frame ahead draws
the pause's picture, panel and all, once.

**Held** by `tools/port_pause_prove.py` (the gate's `port-pause`, on
the card, after the bench): the Windows build straight into a fight,
Start pressed a second in, the window's own pixels every tenth blank;
the remastered picture with `run_ahead` 0, 1 and 2 and the original
with 0 and 2. Each case's last six pictures, all of the pause, are held
to one another and to the case with nothing run ahead; that case's
panel box must be darker than the fight's before it. The fault's
pictures were 9 to 12 apart (of 765), the right ones 0.0. The other
screens shown while the fight is held were looked at the same way,
`run_ahead 2` against 0 through port_game's plan (the options page, the
menus, the winner, the menu after the fight, the next fight): no other
code of the card's draws in a kept step.

### The step's refresh

**What was asked** (2026-11-18): the step on a thread of its own,
pipelined with the window's pictures, so that a refresh costs the more
of the two and not their sum. **What was found** on the RTX 3090
(`run_ahead 1`, the bench's fight):

- *The step calls OpenGL.* The side channel hands every command to the
  card's backend as it comes (`port_RemasterCommand` in `raster.c`,
  `gl_draw.c` and the rest drawing at once), and a held blank's turn
  ends the card's frame (`port_GlFinishFrame`). The backend also reads
  the game's live state as it draws (the fighters' views, the stage's
  view, the colour tables and texture keys in video memory, the scene's
  nodes). So no picture can be drawn while the step runs until the step
  only records what the card is to draw, and the window draws it from
  the record (the tween agent's plan: `port_GlStepRecords()`,
  `port_GlDrawPending()`).
- *The context cannot move to the step and back.* Tried
  (`RB_PIPELINE=trial`, since taken out): pictures drawn on a thread of
  their own, the window's context handed to it before each held step and
  taken back after. Each step paid about 1.8 ms in the window's
  `SDL_GL_MakeCurrent(NULL)` (the driver empties its queue) and 0.7 ms in
  the other three hand-overs, and the game's and side channel's parts ran
  about 1.5 ms slower afterwards. The frame's blank went from 6.5 to
  12.7 ms. So the context stays on the window's thread for good, and the
  step's thread waits for the record.
- *What of a step is free of OpenGL today*: a fight's step (two blanks)
  is about 1.3 ms of the game's logic before its first blank, the two
  blanks' turns by 1.6, the frame's list from 1.8 to the end at 5.3 ms
  (the software GPU's and the side channel's work, interleaved command
  by command). Only the first 1.3 ms could run beside a picture. That
  could be done by entering the game's fiber on a thread of its own and
  moving it to the window's thread at its first need of the window (the
  switch keeps each stack's record of the thread's stack, so a fiber may
  be left on one thread and entered on another).
- *Nothing beside the step makes its frame sooner.* The input is read at
  the boundary and the frame drawn after the step, so the step's first
  picture can come no sooner than the step and the picture together. In
  a paced game the boundary comes just after a refresh (the window shows
  its pictures until then). So what overlapping buys is not a sooner
  frame but a picture for the refresh that falls inside the step, which
  otherwise shows the picture before it again.

That last refresh needs no thread. **Now** (`port_StepRefresh`, the
window's `g_port_held_begin_hook`): when a held step begins, its input
read, and the step and its first picture would miss the display's next
refresh (what they take, on average, from the boundary, and a
millisecond to spare, since a refresh is seen at the swap's return a
little after it comes), a picture between the two frames written down
already is drawn for that refresh. It is then handed over without
waiting for the card (`port_HandPicture`, no `low_latency` wait). The
step runs while the display shows it, and the step's own first picture
is shown at the refresh after, the one it would have made anyway. The
input is read at the same moment as before. Only with the display waited
for and its refreshes known, never with `--fast`, and never for a picture
a test or the player asked for. `RB_STEP_REFRESH=off` restores the old
flow. A peak instead of the average made the miss rarer (0.9%), but half
of the steps it drew for would have been in time, and those were a
refresh later.

**Measured paced at 144 a second** (`port_bench.py --paced`, below; the
bench's fight at 2560 by 1440, `run_ahead 1`, the same program with
`RB_STEP_REFRESH=off` and without, interleaved, three rounds, other
agents' work on the machine throughout):

| | refreshes no new picture reached | a frame's input to its first picture, refreshes: median, mean, 95% |
| --- | --- | --- |
| before | 12.7%, 18.3%, 7.7% | 2, 1.61, 2; 2, 1.89, 2; 1, 1.38, 2 |
| now | 3.1%, 5.3%, 3.4% | 2, 1.59, 2; 2, 1.51, 2; 1, 1.44, 2 |

Of the misses before, all but one in a hundred were within two refreshes
after a frame's boundary: the step's. The lag is the same in every
pair; nothing is later. `--fast` (the gate's bench) does not draw it,
so the gate's numbers are those of before.

**A hidden window keeps the fine timer.** Since Windows 11 a program
none of whose windows can be seen is held to the system's coarse timer
(15.6 ms), whatever resolution it asked for. A `--hidden` run's waits of
a millisecond or two (`SDL_Delay` in `port_WaitFor` and the made-up
display's wait) then slept a refresh and more. Paced at 144 a second, 41%
of the refreshes got no new picture, near enough 1 − 85/144, which made it
look as if the PC's own 85 Hz screen held a hidden window. The window now
asks for the fine timer whatever it shows (`port_KeepFineTimer`:
`SetProcessInformation`, `ProcessPowerThrottling`,
`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`), and the same run
misses 17% (8 to 18% from run to run, the step's misses above). The
`--display-rate` rows of "Pacing on a real display" were measured with
the coarse timer: 144's 83.9 pictures a second was more the timer than
the card. A shown window had the fine timer all along.

**The measures** (`tools/port_bench.py`):

- `--paced`: the cases at the game's pace against a display of 144 a
  second made up by the program (`--display-rate`), kept apart
  (`+paced`): the refreshes after the fight's first second that no new
  picture reached, and the lag from a frame's boundary (the profile's `k`
  line, the input read) to its first picture handed to the display that
  was waited for, in refreshes.
- Unpaced, *picture to picture*: the time from one picture handed to the
  display to the next, less what was waited for the clock in between
  (the `p` line's fourth word), whichever blank it falls in; `--against`
  compares its 95% and 99%. A picture not waited for is `1` in the
  `p` line's third word, and its time is the refresh it was handed over
  for.

**Since done**: the record and the step on a thread of its own ("The
step on a thread of its own", below). The step's refresh is what the
window draws beside a step with `RB_STEP_THREAD=off`.

### The step on a thread of its own

**What was asked** (2026-11-18): make the step only record what the card
draws, with everything the draw reads of the game kept in the record, and
then run the step on a thread of its own beside the window's pictures, so
that a refresh costs the more of the two and not their sum, and a long
step costs no refresh. The input is read at the same moment, and the game
stays the same to the byte.

**The record** (`src/port/gl/gl_record.c`, `gl_record.h`). While a held
step runs (`g_port_holding`: run-ahead on, the window's build), the side
channel's backend (`port_RemasterSetBackend`) is a recorder, and so is the
pause's holder (`port_RemasterSwapPauseHolder`). Every call the backend
would have had -- `begin`, `draw`, `wrote_vram`, `end`, `kept_vram`, the
card's frame ended at a blank (`port_GlFinishOrRecord`, from the held and
ahead blanks' turns and `port_AheadDone`) and the holder's turns -- is
written down in order, into blocks kept from one record to the next. What
the backend reads of the game at a call is written down with it:

- *the pixels of each rectangle of video memory told of*, as they are at
  the call (a write of a frame the card does not draw waits for the
  painters first, as `port_GlUploadVram` did). Drawn, the backend reads
  them from the record (`port_GlTellPixels`, `port_GlVramAt`), and looks
  up what it looks up in video memory -- a mod's pictures, the stage's
  pages, the fighters' colour tables, the clear's colour -- in its own copy
  of what the mirror holds (`g_port_gl_vram`), as a picture between
  frames does;
- *the fighters' views, the tiny model's and the weapons*
  (`port_FighterKeepViews`) and *the side channel's stage view, the
  shadows' casters with what the game's memory says of each face's node,
  and the sun's slope* (`port_RemasterKeepSide`): copied where they have
  changed since the last copy (a version, and a stamp with the sun's slope
  in it), two or three times a step; the getters answer from the copy in
  force (`port_FighterUseViews`, `port_RemasterUseSide`), the casters by
  their place among the faces (`port_RemasterCasterFaceGround`,
  `port_RemasterCasterFaceIsCloth`);
- *what the backend asked of the game about a primitive's node* -- a
  cloth, the tiny model's body, the pause drawing the fight's last table
  again -- is asked as the primitive is decoded and kept in it
  (`PortRemasterPrim.is_cloth_node`, `is_tiny_body`, `is_redrawing`), so
  that neither the record nor a picture between frames reads the game for
  it.

Read live still, as they are only between loads and stage changes: the
stage's tiles, pieces, textures and number, and the meshes they name. The
window draws a record only where the game is still, or, while the step
runs, never past the stage's view of another version (the meshes are
built again then, from the game's memory and with its coprocessor,
`port_GlStageLightBy`): that waits for the step's end.

**The step's thread** (`sdl_window.c`, "the step on a thread of its
own"; `step.c`, `g_port_step_runner`). The window's thread reads the
input at the boundary as ever (`port_ReadInputLate`), then hands the
game's fiber to the step's thread, which enters it, runs the step -- the
game, the software GPU's game-thread part, the side channel, the sound's
stepping -- and the frames ahead after it (`port_AfterStepAhead`), and
says so. The window's thread meanwhile:

- *draws the record as it is written* once the frame's list has begun
  (`port_GlDrawSoFar`; the first primitive written wakes it), up to a hold
  the step's thread writes as the step itself ends (`port_GlRecordHold`):
  what the frames ahead write is drawn only once the step's thread is
  still, before their state is put back (`port_FlushRecord`, the step's
  thread waiting). The rest is drawn once the step has ended
  (`port_GlDrawPending`), and the blanks' turns are given as before;
- *draws a picture for each refresh the step would miss*
  (`port_BesideStep`): a picture between the two frames written down
  already, handed over for that refresh and not waited for, where the
  step still runs with no more time before the refresh than the rest of
  its record and its first picture take after it (on average, and a
  millisecond to spare). The decision is made at the last moment, not
  guessed at the boundary as the step's refresh had to: a step that ends
  in time has its own picture shown. Once the record's frame is being
  drawn no picture can be (the card's frame is under way), so as the
  frame's list begins the next refresh is given one first where the
  list, the rest and the first picture would miss it.

A step that comes to what only the window's thread may do -- a blank's
turn while the step runs, which is a load beginning or a step of more
than four blanks -- is moved there: the step's thread leaves the fiber
where it is (`port_StepToHost`, `g_port_step_to_host`), the window's
thread draws the record and enters it, and the step goes on there as any
step (7 of the bench's 1,781 steps). The program's end from inside the
step goes the same way (`port_CloseFromStep`). The fiber's switch keeps
each thread's record of its stack, so a fiber may be left on one thread
and entered on another.

**What a blank inside the step reads.** The window's events are the
window's thread's to read (SDL), and it reads them at the boundary; a
blank inside the step sends the pads what the keys and controllers held
then (`port_ReadInput` on the step's thread: `port_SendPads` alone). The
game takes its input at the boundary, so nothing it takes moves. A run
with `--inject` (presses made as the PC's own, read at a blank) keeps the
old flow.

**Counted apart.** Each thread's parts are its own (`profile.c`,
`g_port_profile_thread`): the window's `b` lines are the window's thread's
alone, and the step's thread's are `s` lines at the blank that takes them
(`tools/port_bench.py`: "the step's own thread", `step_p50` and the
rest). The window's waiting for the step is `wait`. With the step on its
own thread the window's numbers are a picture's and a record's, and the
step's are the game's; a refresh costs the larger, not the sum.

`RB_STEP_THREAD=off` runs every step on the window's thread as before;
`RB_STEP_STREAM=off` draws the record only once the step has ended. The
headless programs, the tests and netplay never hold a step and are as
they were.

**Held to the same game** (the bench's fight on the card, 640 by 360,
`--trace`, `--vram` at the end, run_ahead 1 and 2, against
`RB_STEP_THREAD=off` of the same program): the traces, the plan's
checkpoints and video memory at the end are the same to the byte, and so
are the pictures (`--tap-window` every eighth blank, `frame_rate
original`, the counter off, `RB_TEXTURES_AT_ONCE=1`): 419 of 419, both
cases. port-rollback and port-net pass; the eleven checks pass.

**Measured** (RTX 3090 at 2,560 by 1,440, the bench's fight; this
program against main's `29bbb3810` run one after the other with
`port_bench.py --program`, interleaved; other agents' work was on the
machine in the unpaced rounds and less of it in the paced ones; medians
of the rounds, ms). Unpaced (`--fast`, the gate's bench), two rounds:

| case | main: window 95%, after 1 s 99% | now: window 95%, after 1 s 99% | now: the step's thread 50%, 95%, 99% | picture to picture 99%, main, now |
| --- | --- | --- | --- | --- |
| `run_ahead 0` | 6.19, 6.48 | 5.38, 6.09 | (not held) | 6.48, 6.08 |
| `run_ahead 1` | 6.54, 6.85 | **1.61, 2.22** | 4.44, 5.33, 6.56 | 6.85, 8.03 |
| `run_ahead 2` | 6.83, 7.20 | **1.39, 1.53** | 6.06, 7.08, 7.88 | 7.19, 8.43 |
| four fighters, the ground twice | 5.90, 6.12 | **1.55, 1.85** | 4.26, 4.95, 5.46 | 6.11, 6.32 |

The window's thread's own work is now a picture and a record: a frame's
blank 1.2 to 1.6 ms at the 95th centile against 6.5, every blank after the
first second under 3.4 ms. Unpaced, nothing is drawn beside the step (no
display to wait for), so the time from one picture to the next is the
step and its record and first picture one after the other, as before,
with the hand-overs: about 1 ms more at the 99th centile.

Paced at 144 a second (`--paced`), three rounds:

| case | refreshes no new picture reached, main, now | a frame's input to its first picture, mean refreshes, main, now | the step's thread 50%, 95% |
| --- | --- | --- | --- |
| `run_ahead 1` | 0.59, 0.15, 0.29 %; 0.73, 0.59, 0.73 % | 1.23, 1.25, 1.21; 1.22, 1.24, 1.24 | 3.35, 3.88 |
| `run_ahead 2` | 3.81, 3.38, 1.60 %; 2.93, 2.20, 2.06 % | 1.39, 1.40, 1.93; 1.62, 1.48, 1.43 | 5.14, 5.92 |

So the lag is what it was (the input is read at the same moment, and the
step's first picture is shown at the first refresh it can make), the
misses at `run_ahead 2`, whose step is the longest, are fewer, and at
`run_ahead 1` they stay under one in a hundred. On a busier machine
(earlier rounds of the same day, the step's thread preempted), main's
`run_ahead 1` missed 3 to 9% and this 2 to 6%. What a miss still is: a
step that ends in time for a refresh by the average, whose record and
first picture then take longer than the average; once the frame's list
is being drawn, no picture can be drawn for a refresh beside it.
`RB_STEP_STREAM=off` (the record drawn only after the step) missed as
many and was no later, within the noise.

**What is left.**

- *`run_ahead 0` is not held*, so its steps stay on the window's thread
  (its blanks are shown as they come, inside the step).
- *The window's part after a step*: the rest of the record (the card's
  frame ended: the shadows, the resolve) and the first picture of the new
  pair, about 1 to 2 ms, which the step's own picture waits for. The step
  and that together still come to about a refresh, so a frame's first
  picture is often the second refresh after its input. The software
  GPU's part of the step (2 ms, much of it the side channel's decoding
  and the waits for the painters) is the largest.
- *Pictures between frames on the card* (HANDOFF, "What is left of the
  performance work"): the record is the natural place for it -- a frame's calls kept,
  with both frames' corners in one vertex buffer and the phase a
  uniform -- but it is not done: a picture is still the frame on show's
  primitives given to the backend again on the processor.
- *A frame drawn and not shown*: with pictures between frames, the
  record's own frame is drawn into the colour buffer and then drawn over
  by the first picture of the pair; the card could be spared it.

### The processors with the larger cache

**What was asked** (2026-11-19): the owner's processor, a Ryzen 9
9950X3D, has its cores on two dies, and only the first (logical
processors 0 to 15) under the 3D V-Cache, 96 MB of L3 against the other's
32. The step's thread pinned there by hand was quicker. So: prefer the
larger cache's processors where a machine has them, and change nothing
on any other.

**What is read** (`src/port/window/cpu_prefer.c`, at the start, before the
painters and the step's thread are made). On Windows,
`GetLogicalProcessorInformationEx(RelationAll)`: every core's processors
and its efficiency class, and every third-level cache (unified or data)
with its size and processors. On Linux, `/sys/devices/system/cpu/online`
and each processor's `cache/index*/` of level 3 (`size`,
`shared_cpu_list`; processors with the same list share the cache), with
`/sys/devices/cpu_atom/` or a `cpu_capacity` that differs as a hybrid.

**What is made of it** (`cpus.c`, `port_ChooseCpus`; no system call, held
to made-up tables in `tests/port/window.c`, "the processors with the
larger cache"): something is asked only where every processor is under
exactly one L3 that was read, the cores are all of one kind, there are
two L3s or more, and the largest is at least 1.5 times every smaller one
(`PORT_CPUS_LARGER_TENTHS`); then its processors (the union, where more
than one cache has the largest size) are preferred. Otherwise nothing:
one L3 (any one-die processor, Intel's client processors), L3s of one size
(a 9950X, two dies alike), hybrid cores (Intel's P and E cores, ARM's big
and little: left to the system's scheduler, which knows them, even where
their caches differ), a processor under no L3 (Meteor Lake's low-power
island) or anything not read. The log says which:

    cpu: L3 96 MB on 0-15, 32 MB on 16-31; preferring 0-15 for the step and the software GPU's painters
    cpu: the step's thread runs on them
    cpu: one L3 (96 MB on 0-23), nothing done
    cpu: L3 32 MB on 0-15, 32 MB on 16-31, none clearly larger, nothing done

The owner's PC reads the first two; the same PC's WSL (24 processors
under one 96 MB L3, as the virtual machine reports it) is the third.

**What is asked**: the step's thread (`port_StepperThread`, before it
says it is ready: the window's thread waits for that and writes its line)
and the software GPU's painters (`port_PainterThread`) set their own
affinity to those processors (`SetThreadGroupAffinity`, only where they
are all in one processor group, and less any the process may not use;
`pthread_setaffinity_np`). The window's thread is left to the system.
`cpu_prefer = off` (`settings_list.h`; the settings menu's window page,
"Larger-cache cores", at the next start) asks nothing.

**The same game.** Where a thread runs changes how soon it is done, never
what it computes or in what order the threads hand the step over: nothing
of the game's state, the record or a frame's result reads it. The
headless programs, the tests, port-rollback and port-net never read
`cpu_prefer` and are as they were.

**Measured** (RTX 3090 at 2,560 by 1,440, `port_bench.py --report-only`,
interleaved runs of the same program with `--set cpu_prefer=off`; other
agents' emulators and builds were on the machine, 8 to 68% of the
processors busy, so single runs vary by a millisecond or more; ms).
First, which threads (two rounds each, then four of the three best):

| asked of | the step's thread 50%, 95% (medians of the runs) | its `game` part | its `raster` part |
| --- | --- | --- | --- |
| nothing (`off`) | 6.1, 7.5 | 1.35 | 2.05 |
| the step's thread alone | 5.9, 6.9 | 0.80 | 2.6 |
| the step's thread and the painters | 5.3, 6.2 | 0.82 | 2.0 |
| ... and the window's thread | 5.2, 6.2 | 0.85 | 1.9 |
| the step's thread's ideal processor alone (`SetThreadIdealProcessorEx`) | 6.3, 8.3 | 1.35 | 1.9 |

The step's own work (`game`, `ahead`) is quicker on the larger cache
whichever way; with the step's thread alone there, its waits for the
painters on the other die (`raster`) grew by about what it saved. The
painters beside it keep both. The window's thread there too was no
better and its pictures were later (picture to picture 95% 7.5 and 7.8
against 6.1 and 6.9), so it is left out. An ideal processor is only a
hint, and was not taken.

Then `auto` (the step's thread and the painters) against `off`, run by
run, with the load line of each (the step's thread 50%, 95%; its `game`
part; picture to picture 95%):

| case, round | `off` | `auto` |
| --- | --- | --- |
| `ra2`, 0 (46%, 22% busy) | 7.41, 10.07; 1.11; 12.64 | 7.04, 9.00; 0.89; 10.68 |
| `ra2`, 1 (40%, 68%) | 7.24, 9.44; 1.47; 10.29 | 6.42, 8.06; 0.79; 10.97 |
| `ra2`, 2 (42%, 35%) | 7.69, 8.82; 1.46; 9.64 | 7.62, 10.81; 0.93; 13.54 |
| `ra2`, 3 (45%, 21%) | 13.75, 21.27; 1.83; 25.57 | 5.79, 6.67; 0.96; 7.54 |
| `ra1`, 0 (26%, 31%) | 4.53, 5.37; 1.27; 8.95 | 3.91, 4.75; 0.88; 13.13 |
| `ra1`, 1 (18%, 10%) | 4.17, 4.96; 1.44; 6.21 | **3.16, 3.79; 0.63; 4.80** |
| `ra1-stress`, 0 (4%, 4%) | 3.86, 4.86; 1.28; 6.00 | **3.24, 3.63; 0.75; 4.79** |
| `ra1-stress`, 1 (3%, 6%) | 3.76, 4.14; 1.24; 5.12 | **3.59, 3.93; 0.88; 5.01** |
| `ra1-stress`, 2 (20%, 33%) | 4.23, 5.29; 1.43; 6.58 | 4.11, 5.28; 1.14; 9.63 |

On a quiet machine the step's thread is up to a quarter quicker at the
95th centile, its game's part a third to a half quicker, and the
pictures no later. The game's part is quicker in every pair, whatever the load. The
runs where `auto` came out later (`ra2` 2, `ra1-stress` 2, and the
budget's `ra1-stress` once: `settled_p99` 9.74) are the ones where a
third of the processors or more were busy with other agents' provers
on the same card; ra1-stress's two quiet pairs are 2.87 and 1.90 `off`
against 2.55 and 2.33 `auto`. The whole budget once with `auto`
(`--budget --report-only`): every measure but ra1-stress's (that run
loaded) within its limit; `step_p95` ra1 4.16, ra2 6.46.

### The window's thread and the game's state

**The crash** (the owner, 2026-11-18, `05b0f524d`, a fight against the
CPU on Lost Bridge, `run_ahead = 2`, about four minutes in): "memory that
is not there was touched" at `0x083E42C4`, which the build's names put at
`g_port_call_depth` (`src/port/plan.c`), written by
`__cyg_profile_func_enter`, called from `resource_GetStage`, called from
`port_GlStageIsTile` through `port_RemasterStageNumber` (the report's own
names were the nearest it knew, `port_TweenStop` and
`sprite_DrawRotated`).

**The cause.** The game's files are compiled with
`-finstrument-functions`, and the hook at the top of each of the game's
functions writes `plan.c`'s variables (the last functions begun, the
depth of calls), which are the game's state, kept by page
(`config/port_state.txt`; NETPLAY.md, "Save, load and hash"). The
window's thread, drawing the record beside the step that runs on its own
thread, asked the stage's number of the game's `resource_GetStage` for
each primitive of a stage's piece it was deciding about
(`port_GlStageIsTile`), some 84,000 times a bench fight; so it wrote the
state's pages beside the step. A page that is watched is read-only and
its first write faults; `port_StateOnWrite` declined a fault whose page
was no longer noted as watched, and a declined write is a crash. The
step's thread changes a page's note under the window's thread's fault:
its own fault on the same page at the same moment (the step writes the
same variables at every function it begins), or a run-ahead's load,
which notes a page writable before it makes it so. Rare, and so it took
minutes: the page is mostly hot (left writable), and watched again every
128 epochs.

**The fix**, three parts:

- *The hook counts the game's own run alone*: a function begun off the
  game's stack (`PORT_STACK_ADDR`: the port's code calling the game's
  from a stack of the host's) writes nothing (`port_OnGameStack`).
- *The window's thread runs none of the game's code while the step runs
  beside it*: `port_RemasterStageNumber` reads `g_resource_stage` as it
  is, and the port's own getters in the game's files that it asks there
  (`stage_GetTiles`, `stage_GetPieces`) are not instrumented
  (`no_instrument_function`). Such a call is told to the window (`g_port_foreign_call_hook`),
  which says the first in the log ("a function of the game was begun
  beside the step, off its thread") and their count at the end, and
  `tools/port_bench.py` fails a run that says so, whatever
  `--report-only`: the gate's bench would have failed on every run of
  the crash's build.
- *A fault on a page of the state that is already writable is taken*:
  `port_StateOnWrite` holds a lock, makes the page writable whatever its
  note says (but video memory's own, never made read-only) and lets the
  write go on.

**Shown** (the bench's plan and fight on the card, `run_ahead 2`, three
runs side by side, with the race made wide in a trial build: a spin at
the top of the fault's handler and one in the load between noting pages
writable and making them so, and pages watched again every 2 epochs, not
128): the build without the fix stopped in all three on the very write of
the owner's report (`0x083E42C4`, `__cyg_profile_func_enter` from
`resource_GetStage`), about 24 seconds in, and counted some 84,000 of the
game's functions begun beside the step a run; with the fix the same
trial ran all three to the end with none.

### The HD fighters on the card

**What it was** (main at `8f09f98d1`, the card's measure above): the HD
body was posed on the processor for the cast shadows once a frame (every
vertex's eight terms, then 44,000 triangles copied, slid onto the ground
and sent to the card again, 3 MB a pair of frames), and posed again for
each cloth, whose every place was then kept out of it on four threads
(`port_GlClothBodyHold`): 2.4 and 5.7 ms of a fight frame with nothing
run ahead, 2.6 and 7.1 with `run_ahead 1`.

**Now** (`src/port/gl/gl_fighter_card.c`) none of it is the
processor's. Once for each set of a fighter's bones, compute shaders
over the model's own vertex buffer (the file's `PortFighterVertex`, read
as words) pose the body as the fighters' shader does, count each vertex
into the same hashed grid of 64-unit cubes, sum the counts into where
each cube's list begins, and put each list in the order of the
vertices' numbers (a group a list, each vertex placed by how many of the
list are below it): the same lists every run, whatever order the card
ran the vertices in. Each smooth cloth of the fighter is then made
through the game's points in three steps, all of his cloths side by
side and one barrier after each: his points kept out of the body, each
place of the surface (its fixed sum of the points, `gl_cloth.h`) kept
out, and each vertex given its place, the normal of the quads about it
and its texture point, into a buffer of `PortGlClothVertex` that the
fighters' layer and the shadows draw from by `gl_VertexID`. Nothing comes
back: the shadows read the body's lowest point (the soles' ground,
"A body the game casts no shadow of") from the card's own buffer.

- **A point kept out** is the processor's arithmetic of before, in single
  precision: the sum of the body's vertices about it, each one's distance
  along its own normal, weighed by (1 - d²/r²)², r one and a half times
  the nearest one's distance and at least 48, up to four steps out. A
  thread a point waited on memory for each of the few thousand vertices
  it walked, ten times over: 34 ms of the card a frame. Now **a group of
  32 threads a point** walks the cubes together and adds up by a fixed
  tree, and **the 27 cubes about the point are copied into shared memory**
  (512 vertices, 12 KB, so that many groups fit on a multiprocessor;
  0.5% of the places' cubes hold more and are walked in the body), kept
  until the point leaves its cube.
- **The cast shadows from the skinned mesh**: a model's caster is its
  vertex array, the 21 bones' world matrices of the frame and its
  ground (`port_GlFighterCastOf`), and the shadows' vertex shader poses
  each vertex by its terms on each of the two frames' bones, slides it
  onto that frame's ground along the sun and mixes the two; a smooth
  cloth casts from the buffer it was made into. Pairing between frames is
  by the caster and its model (it was by the triangle's place in the
  frame's list, and 1,456 of `port_game`'s mixed pictures' triangles found
  no pair; now 8). The mask's box is the model's: the bones' places and,
  for each vertex, its terms' lengths and shares worked out when the file
  loads, so it holds every vertex in any pose.
- **The first frames** (`port_GlFighterPreload`, while the game loads):
  each slot's poses and held cloths given room at the model's size, every
  compute program dispatched once on the bind pose, the fighters' layer
  made at the picture's size and drawn with, and the shadows' programs
  drawn with once and their mask made (`port_GlShadowWarm`). A driver
  readies a program at its first use: the fight's first frame had the
  cloth at 14 ms, the shadows at 15 and the frame ahead at 11 of the
  processor. Asking GL which program was in use waited on NVIDIA's
  driver thread (6 ms a frame); the card's functions leave none in use
  and the callers bind their own.

**Held to main's pictures.** The prover (`port_game`, the hd mod, `shadows
cast`, scale 1, every 300th blank, and scale 2 with three pictures a
frame, every 150th): the cloth's counts are the same but one (53,898
places of the game's cloth inside the body, as before; 1,372 left inside
as drawn against 1,373), and the pictures differ from main's in at most 4
pixels of 1,228,800, by at most 205 of 255, single pixels at the cloth's
edge where a place kept out in single precision lies a hair from where
double put it. On the owner's card (`--tap-window`, the budget's fight,
every eighth blank at 640 by 360, `frame_rate original`,
`RB_TEXTURES_AT_ONCE=1`, the counter off): two runs of this build are the
same to the byte (419 pictures); against main (`77ee9f758`) 367 are the same to the byte and the 52 that
differ do so in at most 7 pixels by more than 16 of 255 (the most 77),
single pixels at the cloth's edge, as in the prover. GPU skinning,
posing and keeping out in single precision cannot give the processor's
double-precision places to the bit; a place kept out a hundredth of a
unit elsewhere moves an edge pixel at most, which is what is seen.

**Measured** on the owner's card (`port_bench.py --against 77ee9f758`,
RTX 3090 at 2,560 by 1,440, `e9d514aa4`; the stage prover was running on
the processor beside it, so the processor's numbers are if anything
high). A fight frame's blank, milliseconds:

| case | processor median | 95% | worst | card median | 95% |
| --- | --- | --- | --- | --- | --- |
| `run_ahead 0`, main | 14.8 | 17.4 | 30.1 | 4.1 | 8.2 |
| `run_ahead 0`, now | 11.0 | 14.7 | 17.2 | 6.5 | 10.2 |
| `run_ahead 1`, main | 20.7 | 25.7 | 40.3 | 5.6 | 8.6 |
| `run_ahead 1`, now | 9.4 | 11.6 | 16.5 | 6.1 | 9.3 |
| `run_ahead 2`, main | 21.7 | 32.8 | 54.7 | 6.7 | 9.1 |
| `run_ahead 2`, now | 14.7 | 17.9 | 36.4 | 8.4 | 12.3 |
| four fighters, the ground twice, main | 16.3 | 18.1 | 39.5 | 5.6 | 7.8 |
| four fighters, the ground twice, now | 12.0 | 18.8 | 28.1 | 10.2 | 15.8 |

The cloth and shadows' parts of the processor went from 2.6 and 7.1 ms
(`run_ahead 1`, `8f09f98d1`) to under 0.1 each, and no blank of the
fight is a stall (`8f09f98d1`'s worst 69 ms, at its first frames). The
card pays for it: the cloth's pass (`cloth`, the bodies posed and the
cloths made) was 1.7 ms a fight frame then, and 1.1 since (one barrier a
walk where a tree took five, and the smaller copy of the cubes). It is
paid once a game frame, not once a picture: at 144 pictures a second
three pictures of four have none of it.

What was left of the fighters on the card then (since cut: "The fighters
cheap on the card"): the cloth's hold, about 1.1 ms
of a frame with two fighters' cloths new (the places' walks; a coarser
body for the first walk, or a distance field of the body made once a
pose, would take most of it), and the shadows' mask and darkening
(0.4 ms, mostly fill). Four fighters cost the processor
nothing per fighter but his bones and his cloths' points; the card about
half a millisecond a fighter whose pose and cloth are new.

### The fighters cheap on the card

**Measured pass by pass.** The card's passes (`gl_bench.h`) now split the
fighters' time: `skin` (the bodies posed, by compute), `cloth` (the cloth
kept out and made), `fighters` (the models drawn into the layer), `flayer`
(the layer cleared and taken down from its samples), `flay` (the layer laid
over the picture and into the shadows' mask), `smask` (the shadows' mask)
and `sdark` (the ground darkened through it). What `shadow` kept besides
was the scene's own batch flushed before the shadows, which now counts to
the scene. With `run_ahead 1` before this work (`a7f754af8`, RTX 3090 at
2,560 by 1,440): cloth 1.29 ms, ground 1.20, the layer's clear and
take-down 0.40, darkening 0.24, composite 0.19, draw 0.16, mask 0.12,
skin 0.08.

**The cloth's hold** (`gl_fighter_card.c`, the comment over
`CARD_BODY_TEXT`). A walk of the body about an item (a point or a place of
the cloth) was a chain of waits on memory: the 27 cubes one after another,
each list read through `order`, and a step's dispatch lasted as long as its
longest walk. Now:

- the body is copied once a pose in its lists' order (`sorted`), so a cube
  is a run of the buffer;
- a cube wholly beyond the reach adds nothing and is not walked; the first
  walk finds the nearest vertex and the sums at the least reach (48) at
  once, culled to 48, which is the whole answer for most of a cloth (the
  nearest within 32); a nearest within 48 is exact without the whole walk;
- the 32 threads find the cubes of a round together and share out their
  vertices as one run, so a walk waits on memory once a round, not once a
  cube; the sums are added by a butterfly through shared memory, the same
  on every card (subgroup shuffles, and four warps a group, were no faster
  on the 3090);
- every fighter's poses and cloths of a picture are made together, each
  pass of all of them before the barrier after it
  (`port_GlFighterCardPoses`, `port_GlFighterCardHolds`,
  `port_GlFighterHoldAll` before the fighters are drawn): the waits overlap.

The cloth went 1.29 → 0.97 (lists' order, one walk for the near) → 0.48
(culled) → 0.46 (a round as one run) → **0.22 ms** (side by side), skin 0.08
→ 0.05. The stress's copies (`--bench-stress fighters=4`) now keep their
own cloth and poses under keys of their own (`g_fighter.copy`), so four
fighters cost the card four fighters' cloth: 0.30 ms, skin 0.07.

**The fighters' layer only where the fighters are** (`port_GlFighterBound`):
each bone's vertices (those of its largest share) lie in a ball about its
place, as large as their terms' vectors and their shares of the other
bones' places from it (worked out when the file loads,
`port_GlFighterCastBox`); with the cloths' points' box and the weapons,
projected as the shader projects, that is the box the layer is taken down
and laid over in. The layer itself keeps the drawing area's numbers (a
layer only as large as the box drew the models a few hundredths of a pixel
elsewhere), the take-down writes nothing-at-the-far-plane outside the box
without reading a sample, and the layer's depth of several samples is no
longer cleared (only a covered sample's depth is read). The clear,
take-down and composite: 0.40 + 0.19 → 0.24 + 0.13 ms.

**The shadows' box** (`port_GlShadowPairBox`): each thing drawn from a model
is bounded by its bones' balls and its cloths' box, slid onto its ground
and projected alone (the box of them all in the world, slid and projected,
was the whole screen through most of a fight), and a pixel with nothing to
darken is discarded rather than blended: darkening 0.24 → 0.10 ms, the mask
0.12 → 0.09.

**The picture held** (on the card, 640 by 360, `frame_rate original`,
`RB_TEXTURES_AT_ONCE=1`, every eighth blank of the budget fight, 419
pictures): two runs of a build are the same to the byte; the bounds of the
layer and the shadows are the same to the byte as before them; against
`a7f754af8` 390 pictures are the same and 29 differ, in at most 25 pixels
by more than 16 of 255 (the most 84), single pixels at a cloth's edge
where a bucket of the hashed grid shared by a cube in reach and one beyond
it is now summed once. The prover's cloth (port_game, the hd mod, `shadows
cast`, scale 1, every 300th blank, llvmpipe): places left inside the body
as drawn 1,372 → **1,356**; places of the game's cloth inside 53,898 →
53,976.

**Measured** (`port_bench.py`, RTX 3090, `a7f754af8` against now, card
medians and 95th centiles of a fight frame's blank, ms):

| case | the card, median | 95% | the fighters' passes (skin, cloth, draw, layer, laid, mask, darkening) |
| --- | --- | --- | --- |
| `run_ahead 0`, before | 5.40 | 11.64 | 2.55 (0.07, 1.42, 0.15, 0.38, 0.18, 0.11, 0.24) |
| `run_ahead 0`, now | 3.03 | 4.25 | **0.92** (0.05, 0.20, 0.13, 0.24, 0.13, 0.08, 0.09) |
| `run_ahead 1`, before | 4.66 | 6.04 | 2.38 (0.07, 1.25, 0.15, 0.38, 0.18, 0.11, 0.24) |
| `run_ahead 1`, now | 4.16 | 6.27 | **0.95** (0.05, 0.21, 0.13, 0.25, 0.13, 0.08, 0.10) |
| `run_ahead 2`, before | 6.61 | 7.76 | 2.97 (0.07, 1.30, 0.22, 0.57, 0.26, 0.18, 0.37) |
| `run_ahead 2`, now | 5.43 | 7.03 | **1.48** (0.06, 0.24, 0.23, 0.42, 0.22, 0.15, 0.16) |
| four fighters, the ground twice, before | 6.03 | 7.29 | 2.67 (0.08, 1.37, 0.24, 0.40, 0.20, 0.12, 0.26), two fighters' cloth |
| four fighters, the ground twice, now | 4.68 | 6.08 | **1.27** (0.08, 0.32, 0.24, 0.28, 0.16, 0.09, 0.10), four fighters' cloth |

Another agent's benchmark ran on the card beside these (the ground's pass
went from 1.1 to 2.2 ms with it), so the totals are noisy; the fighters'
passes held from run to run within a few hundredths. With `run_ahead 2`
the layer is drawn for each frame run ahead the card draws: the renderer's
frames kept from the boundary before (`g_port_ahead_kept`) skip it.

**The budget a fighter.** Four fighters' whole share of the card is 1.27 ms, **0.32 ms a fighter** with every fixed cost of the layer and the shadows in it; a fighter more costs about 0.16 ms (his pose, his cloth, his draw), and the layer's clear, take-down and laying (some 0.4 ms) are paid once whatever the number. What is left: the layer's take-down and laying are fill over the box of all the fighters (two fighters apart make it most of the screen; a box each, laid apart, would take a third of it again), and the cloth's walks wait on memory more than they work (a body as a distance field made once a pose would make each walk a few reads).

### Input lag

**Where a press went** (measured headless: `port_game`'s plan with one
press of Circle added at fight frame 40, every picture handed to SDL
kept and compared with the run without it; Original picture, SDL's dummy
drivers). R is the blank at which the game takes the input (`pad_Update`,
`port_TakeInput`); a fight's frame is two blanks, a menu's one.

| | before | now |
| --- | --- | --- |
| (a) the PC's keys read to the game taking them | at the blank before R: right after the window's wait for blank R-1, so the read was a whole blank (16.7 ms) old when taken in a fight, and a blank less the step's time in a menu; a press waits for the next read, on average 8.4 ms, and in a fight only every second read counts | read again just before the game takes it (`port_ReadInputLate`, `port_ReadDevicesNow`): 0 ms old |
| (b) the game: taking it to putting a frame that shows it on the display | the frame that takes it draws it (no frame of the game's own lag), and the game puts it on the display at the end of the next frame: R+4 in a fight, R+2 in a menu, as the console | the same: nothing of the game changes |
| (c) the window: the display to a picture handed to SDL | a blank later, because the window shows a blank's display before the game's step after that blank changes it: the picture first differs at **R+5** | `run_ahead 1`: **R+3**; `run_ahead 2`: **R+1** |
| then | the swap: the next refresh, and whatever the driver and the compositor hold it | the same, but `low_latency` lets no picture queue behind another |

So in a fight, from a press to the picture handed to the display was on
average 33 ms (a) + 83.6 ms (c) = about 117 ms, against the console's
16.7 + 66.9 = 84 ms (its driver reads at the blank the game takes it
at). Now it is 16.7 + 16.7 = about 33 ms with `run_ahead 2`, 67 ms
with 1 (the default since 2026-11-17: "The HD cloth and run-ahead"), 100 ms with 0: the console's own lag less two
frames. In a menu: 8.4 + about 11 + 50 = about 70 ms before, 8.4 +
16.7 = 25 ms now. Then the display's own share, which only a screen has:
up to a refresh to the next one (11.8 ms at 85 Hz), the driver's queue,
Windows' compositor in a window, and the screen's own processing.

**With pictures between frames** (`frame_rate display`) the frame that
shows a press is first part of a picture where it is the frame after the
one on show: the blank before the full frame's, R+3 before, R+1 with
`run_ahead 1`, a refresh or so into it, the press's movement a third of
the way. With `run_ahead 2` the frame on show at R+1 is the one with the
press, whole; the pictures between it and the next lean towards a frame
that is run with the input held as it was, which is the one guess
run-ahead makes, and only where the input then changes is the next frame
other than what they leaned to.

**The read just before the game takes it.** The host's turn at a
frame's boundary calls `g_port_boundary_hook` (`port_BeforeStep`,
`src/port/step.c`): the window reads SDL's events again and puts what
the keys and controllers hold into the input the pads' driver read at
the blank, with the plan's buttons as the blank found them
(`port_ReadDevicesNow`, `src/port/pad.c`). A plan's press made as the
PC's input (`--inject`) waits for the next blank's read of the events, as
it always came, so a plan's timing is the same.

**Run-ahead** (`run_ahead = 0|1|2`, `port_SetRunAhead`). At each
boundary, after that read: the state is saved (`port_StateSave`), the
next one or two steps run with the input about to be taken
(`g_port_ahead`), and the state is put back; then the step runs as ever.
A step ahead paints video memory and gives the remastered renderer every
command, so the card draws those frames and the pictures between frames
are written down from them; it mixes its sound and gives it to nobody,
the window has no turn at its blanks but the card's frame ended
(`g_port_ahead_blank_hook`), it prints nothing, writes no test's
picture, presses no key for a plan, and it stops (`port_GiveUpAhead`)
where the program would end, where a memory card would be written (the
card's file is outside the state), or past four blanks a frame (a load,
the movie). In the window no frame is run ahead into a load or across
one: an ahead step stops at a blank where the game is loading
(`port_LoadKind`), and none is run at a boundary in a load
(`g_port_ahead_held`), so a load's blanks are always the step's own,
paced or hurried through by `quick_loading` as they would be without
run-ahead. When the steps are done the window keeps the display they
left and its pixels and ends the card's frame with it
(`g_port_ahead_done_hook`), and the step that then runs
(`g_port_unseen`) gives the renderer no primitive, no new frame and no
word of the pause, which it had from the steps ahead, but what it writes
into video memory. Since the game draws a frame at once in the port and
puts it up two frames after it takes the input, the display after two
steps ahead is the frame of the input just read: no frame shown is
guessed. In the window the step itself is now the first of the steps
ahead, its blanks shown after it, which shows the same pictures at the
same blanks for a frame's less work ("The step itself as the first
frame ahead"); the headless programs run ahead as here.

Netplay has a loop of its own (`port_RunNetplay`) and the link cable's
steps reach another machine, so run-ahead is off in both; the rollback
self-test composes with it. With `--fast`, `--tap-frames`,
`--tap-window` or `--shot-at`, which hold the window's pictures to the
blank they were drawn at, it is 0 unless `--run-ahead` says.

**`low_latency`** (on): after each swap with vsync on, the window waits
for the card (`glFinish`, `port_GlWaitForCard`), so the swap has been
made before the next picture is begun and the time a picture is drawn
for (`port_NextRefresh`) is the refresh it went up at.

**What is proved.** `port_game`'s trace is the same to the byte with
`--run-ahead` 0, 1 and 2 on the headless program, as before the change;
the window's (dummy drivers, `--fast`) is the headless program's with
run-ahead 0, 1 and 2, and with keys and pads injected under 2 (the late
read included); `--rollback-test 8 --run-ahead 2` passes. The 64-bit
Windows program without a window (`rebushido-headless.exe`, run
natively) prints the same trace with 0 and 2, and the 32-bit Linux one
with 0, 1 and 2. The first picture handed to SDL that differs because
of the press, the press taken at blank 3023:

| | Original (`--tap-frames`) | Remastered, `frame_rate original` (`--tap-window`, offscreen, software GL) |
| --- | --- | --- |
| `run_ahead 0` | 3028 | 3028 |
| `run_ahead 1` | 3026 | 3026 |
| `run_ahead 2` | 3024 | 3024 |

And every picture with run-ahead is the picture without it from 2 or 4
blanks later: Original, 172 of 172 (blanks 2950 to 3035, both runs);
Remastered with 2, 82 of 82; with 1, 78 of 82, the other four one frame
(blanks 3000 and 3001) where 20 pixels of 307,200 differ by up to 40 of
255 in a channel.

**What it costs.** The headless 64-bit program with `port_game`'s plan
(2,324 boundaries), on this machine with others' builds running (a load
of 15 to 55, so a millisecond either way is noise), the run's processor
time and the time the frames ahead took a boundary, saving and putting
back the state's 5 MB included:

| | processor, whole run | added a boundary | the frames ahead, a boundary |
| --- | --- | --- | --- |
| `-O1`, `run_ahead 0` | 5.2 to 5.8 s | | |
| `-O1`, `run_ahead 1` | 12.1 to 12.3 s | 2.9 ms | 4.1 to 4.5 ms |
| `-O1`, `run_ahead 2` | 16.8 to 17.4 s | 4.9 ms | 7.1 to 8.2 ms |
| `-O2`, `run_ahead 0` | 4.4 to 4.5 s | | |
| `-O2`, `run_ahead 1` | 10.4 to 11.6 s | 2.8 ms | 4.1 ms |
| `-O2`, `run_ahead 2` | 14.2 to 17.1 s | 4.6 ms | 6.1 to 8.6 ms |

The 64-bit Windows program without a window, run natively: 5.1 ms a
boundary with 2. A fight's boundary comes every 33 ms and a menu's every
17, so on this machine 2 fits in both with room; the card's share (the
frames ahead drawn by it, with 2 one of them twice) is for a real card to
say.

**What only a screen shows**: whether the driver queues pictures with
`low_latency` off, what Windows' compositor adds to a window (a
maximized window of 2,560 by 1,369 is one; full screen, F11, lets the
driver flip the picture itself, and SDL 2's OpenGL has no say in DXGI's
flip model), and the screen's own processing. `--profile` with
`--display-rate` measured the program's side only.

### The press to the screen, part by part

**How it is measured** (2026-11-18, ROADMAP item 15; `port_bench.py
--paced`, `--rate N` for the made-up display's refreshes a second, `--rate
0` for the PC's own display, `--set` for a setting). The profile's lines
say when each part of a frame's way ended, in microseconds from the
window's opening: `k`, the game taking its input at the boundary; `l`, the
keys read again late (below); `e`, the window's thread hearing the step's
end, with when the step's thread took the step up and said it was done;
`r`, the step's record drawn; and in each `p` line, when the picture began
to be drawn, was handed over, and had been drawn by the card. For each
frame of the fight after its first second, and apart for the menus (every
blank a frame), the bench reports the mean and 95th centile of each part
and of the read to the screen: the refresh the frame's first picture
waited for (with `vsync = off`, the swap itself, torn in; with the PC's
own display, the swap's return). A press comes at any moment and waits for
the next read: on average the sum of the gaps' squares over twice their
sum (`waiting for the read`; a fight's boundary comes every 33 ms, so
about 16.7 ms; a menu's every 17, about 8.4). The press to the screen is
the two added. The gate's bench runs `ra1-paced` (ra1 at 144 a second)
and fails where a fight frame's read to the screen is over 15 ms on
average or more than 15% of the refreshes get no new picture (two
refreshes are 13.9 ms: a first picture a refresh late is caught, the
noise of a shared machine is not; and where the bench says the machine
was loaded, these two are said and not held, since a paced run's
measures are the clock's, which another program lengthens). Then comes what the game itself takes, which run-ahead
cuts and nothing here touches ("Input lag", above): the frame that shows
a press is the first picture after its step with `run_ahead 2`, the
frame after with 1 (a fight's frame later, 33 ms; with pictures between
frames it is mixed in from the next picture on), and two frames after
with 0.

**The made-up display now counts the card.** With `--display-rate` a
picture was shown at the first made-up refresh after it was handed over,
whether or not the card had drawn it: a refresh of 6.94 ms seemed to be
met by a picture the card took 8 to finish. With `low_latency` (on) the
window now waits for the card before it hands a picture over, on any
display (`port_HandPicture`; a real display shows a picture at the first
refresh after the card has drawn it either way, and the wait after the
swap for the swap itself is as before), and the made-up display is held
to it. So the paced numbers of "The step's refresh" and "The step on a
thread of its own" were the processor's side only: counted with the card,
`run_ahead 1` at 144 a second misses 9 to 16% of its refreshes (0.9% as
measured before) and a frame's first picture is the second refresh after
its input, not the first. `low_latency = off` on the made-up display
says nothing: the swap comes back at once and the card is never waited
for, so what a driver's queue holds only a screen shows.

**Part by part** (RTX 3090 at 2560 by 1440, the bench's fight, main at
`57bc9b315` with the measures alone, every default on, the made-up display
at 144 a second; a quiet machine, means of a fight frame, ms):

| part | `run_ahead 0` | `run_ahead 1` | `run_ahead 2` | `run_ahead 1`, 60 a second | `run_ahead 1`, `frame_rate original` |
| --- | --- | --- | --- | --- | --- |
| hand-over to the step's thread | (one thread) | 0.02 | 0.02 | 0.02 | 0.01 |
| the step (and its frame ahead) | (in the blanks) | 3.55 | 7.51 | 4.50 | 3.48 |
| hand-over back | | 0.05 | 0.03 | 0.02 | 0.01 |
| the record drawn | | 0.48 | 0.00 | 0.26 | 0.17 |
| the blanks' turns, the picture begun | 0.80 | 0.06 | 0.02 | 0.05 | 0.05 |
| the picture drawn | 0.48 | 0.65 | 0.57 | 0.73 | 7.83 (with the wait for the blank's moment) |
| handed over to the card done | 2.80 | 2.52 | 3.81 | 2.87 | 0.35 |
| the card done to the swap back | 6.05 | 5.99 | 4.31 | 8.10 | 4.65 |
| **the read to the screen** | 9.55 | **12.67** | **14.88** | **15.39** | **15.69** |
| waiting for the read | 16.7 | 16.7 | 16.7 | 16.7 | 16.7 |

(`run_ahead 0`'s first picture after the read is a frame older than the
one with the input: the game's own two frames come on top. With
`run_ahead 2` the frame ahead's record is drawn while the step's thread
waits, inside "the step".) With `vsync = off` the read to the swap is
6.2 ms, torn in, and with `low_latency = off` 2.8 ms to a swap that
does not wait for the card, which the made-up display cannot follow.

**Before and after `late_input`** (the same, main's measures against
this, interleaved one run each, other agents' work on the machine at 4 to
46% of the processors; fight frames, and the menus before the fight, ms;
"missed", the refreshes after the fight's first second no new picture
reached):

| case | the read to the screen, before | after | a press to the screen, before | after | missed, before | after | the menus, a press to the screen, before | after |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `run_ahead 1`, 144 a second | 12.4 to 14.5 | **10.6 to 12.1** | 29.1 to 31.3 | **27.3 to 28.8** | 16 to 18% | **0.3 to 9%** | 14.2 | 13.4 to 13.8 |
| `run_ahead 1`, 60 a second | 15.2 to 15.4 | **13.9 to 14.8** | 31.9 to 32.0 | **30.5 to 31.5** | 0 to 1.1% | 0 to 0.4% | 23.5 | 22.8 |
| `run_ahead 2`, 144 a second | 14.9 to 17.6 | **13.0 to 15.4** | 31.7 to 34.3 | **29.7 to 32.2** | 9.5 to 17.5% | 4.2 to 9.3% | 14.3 | 14.8 |
| `run_ahead 2`, 60 a second | 15.5 to 16.1 | **13.8 to 14.7** | 32.1 to 32.8 | **30.4 to 31.3** | 0.4 to 1.8% | 0% | 23.9 | 22.6 |
| `frame_rate original`, 144 | 14.9 to 15.7 | **12.5 to 13.6** | 31.6 to 32.4 | **29.4 to 30.5** | 58% | 58% | 24.2 | 19.8 |
| `frame_rate original`, 60 | 15.3 to 15.4 | **12.6 to 14.3** | 32.0 | **29.3 to 30.9** | 0% | 0 to 0.4% | 23.0 | 22.4 |
| `run_ahead 1`, the PC's own 85 Hz | 11.3 to 11.5 | 12.1 | 27.9 to 28.2 | 28.8 | 2 to 5% | 4.5% | 11.6 | 13.4 |

Blanks shown late, and so not shown, over the whole run: 32 and 39 with
`frame_rate original` at 60, 34 and 45 with `run_ahead 1` at 60, 8 and 7
at 144; time let go and the sound running out as before (1 or 2 times,
the fight's load). The refreshes missed at 144 come down besides: a
refresh that falls in the wait is given its picture before the step
begins, where inside the step it was missed. On the PC's own 85 Hz display
(a hidden window) the late read gains nothing that rounds clear of the
noise; that display's refresh comes about a step after the boundary.

**Tried and taken out on the way**: aiming at the refresh the 90th
centile makes (at 60 a second that was often the refresh after the one
the step makes, every frame's second picture a refresh late, the game
behind and time let go 48 times); reading the blank's moment from the
window's `due` where the blank was late (a late blank's `due` is the
moment it was shown, so the wait took the time the game needed to catch
up: 284 to 729 blanks not shown, against 32); and a millisecond's doubt
about which refresh a blank's moment falls before, taken the wrong way.
Now nothing is waited for where the last blank was more than half a
refresh late, the blank's own moment is the pace's, and a moment within
a millisecond of a refresh is not waited for. Earlier versions gained
more in the menus (at 60 a second 24 to 12 ms) by waiting while the
game was behind; that is the game's pace, and was given back.

### The input read late

**What was left.** Of `run_ahead 1`'s read to the screen at 144 a second
(12.4 ms), the step and its frame ahead took 3 to 4.5 ms, the hand-overs
to and from the step's thread 0.02 and 0.06, the record 0.5, the first
picture 0.9, and the card 2 more after it was handed over: about 8 ms of
work, and then 4 of waiting for the refresh. A frame's first picture goes
up at the first refresh after all of it, so that wait was time the press
had already spent; at 60 a second it is most of a refresh.

**Now** (`late_input`, on; `port_LateRead`, from the held step's begin,
`port_StepRefresh`). Where the display is waited for, the boundary waits
that much first and then reads the keys and controllers again
(`port_ReadEvents`, `port_SendPads`, `port_ReadDevicesNow`), just before
the step begins: the "frame delay" of other programs. The picture goes up
at the same refresh, and a press made meanwhile is in it. What a read to
the card's drawing of the first picture takes is learnt from the last 32
steps (`port_NoteLateDrawn`, less the clock's waits between): the refresh
aimed at is the one their median makes (so no picture of the frame comes
later than it would have, which a first version aiming by the 90th
centile did, and at 60 a second it pushed every frame's second picture a
refresh late and the game fell behind), and the wait leaves the step
their 90th centile and a spare. The spare grows by a millisecond for a
first picture late for its refresh and shrinks by a hundredth of that for
one in time (`port_NoteLateShown`), so about one first picture in a
hundred is late, which costs the refresh before it a picture held once
more. A refresh that falls inside the wait is given a picture between the
two frames already written down, as one inside the step would be. With
`frame_rate original` a frame's picture is shown no sooner than its
blank's own moment, so the wait reaches up to the refresh after it: the
largest share.

**What does not change.** The game takes its input at the boundary as
ever, so the traces are the same: a plan's presses are the game's, read
at their blanks; only the PC's own keys and controllers are read later.
The blanks keep their moments (the pace is the sound's), and the pictures
their refreshes. Only for a step on the step's thread (`run_ahead` 1 or
2), with `vsync` on, its refreshes known and `low_latency` on, whose wait
for the card says when a picture was drawn; never with `--fast`,
`--inject` or for a picture a test or the player asked for; netplay and
`run_ahead 0` never hold a step. The log's last lines say how many steps
read late, by how much on average, how many first pictures were late, and
the spare (`late_input: ...`).

**What was looked at and not cut, and why.**

- *The devices read on the step's thread, or at each refresh for the
  pictures between frames*: the game takes its input only at the boundary,
  so a read later inside the step changes nothing it takes (its blanks
  send the pads what the boundary read), and a picture between frames is
  of two frames already worked out; nothing a read could change is in it.
  The late read is the latest moment the game's step allows.
- *Run-ahead on the rollback's save and load*: `run_ahead 2` already runs
  its second frame from a state saved and put back by the rollback's own
  save and load (by the pages written), and 1 needs none ("The step
  itself as the first frame ahead"). Two frames ahead take away all of
  the game's own lag (the frame shown after them is the frame of the
  input just read); a third would show a frame further on than the
  input, a guess that snaps back where the input changes, which is not
  how the original plays. So no more frames ahead.
- *The fewest pictures queued*: with `low_latency` none is queued (each
  waits for the card and its swap). SDL 2's OpenGL on Windows has no DXGI
  swap chain to make waitable; whether a window's pictures are flipped or
  composed is the driver's (NVIDIA's control panel, "OpenGL present
  method: Prefer layered on DXGI swapchain") and full screen (F11)'s, for
  the owner's screen to say. The "delay the frame start" technique is the
  late read above.
- *What is left on the way*: the card's 2 ms after the first picture is
  handed over is mostly the record's own frame, drawn and then drawn over
  by the first picture between two frames (HANDOFF, "What is left of the
  performance work"); not drawing it is the renderer's, and the next cut.

### Quick loading

The owner (2026-11-17): the loading screens as short as possible, if
not gone. **Where a load's time went.** The port's drive has no seek and
no speed ("The disc"): a read's sectors all arrive the next time the
game asks. What is left is the game's own way of waiting for the drive,
`cdrom_WaitIdle`: the link cable's brackets on either side of it
(`link_BeginWait`, `link_EndWait`, two and three `VSync(2)`s, which the
game does whether or not a cable is in), then the drive's state machine
stepped once a second blank (set the mode, its answer, three blanks to
settle, the place, its answer, the read, done). That is 16 blanks for a
file and 10 for a wait on a drive already idle, which the game asks for
before every file: about 36 blanks, 0.6 seconds, a file, and a fight
reads eleven. All of it falls inside one frame of the game, behind the
loading picture (the map of red splashes and the title, with its streaks
of noise), a black screen, or story mode's card of the next stage. The
game keeps no minimum time of its own on the loading picture (story
mode's card has one, below); its fades are frames of the game, and are
left as they are.

**What quick loading does** (`quick_loading`, on in the window, off with
nothing else said for a run with `--plan` or `--frames`;
`--quick-loading on|off`): the blanks of `cdrom_WaitIdle` are hurried
through. The game marks them (`g_port_load_wait`, `cdrom.c` under
`RB_PORT`), and the window, at such a blank, gives the card the frame and
the log its line as ever, but does not wait for the PC's clock, and shows
a picture only every thirtieth of a second, so the noise still moves and
the window still answers; when the load is over the blanks are due from
then on (`port_PaceRebase`), so none of the game's time it took is
waited for afterwards or counted as time lost. A hurried blank's sound
goes to the sound card only while the card's queue is short: the load's
sound is silence, or the end of a sound that was playing, and the queue
is neither flooded nor run dry. **Nothing the game computes changes**:
every blank is still a blank of the game, with its pads, its sound and
its scan lines, so the traces are the same with it on and off, and
netplay's loads stay points both sides wait at however soon either gets
there (`tools/port_net.py window` runs with it on).

Two loading screens are read: story mode's card of the next stage (its
title, the map with the route, the stage's name and picture) and the
team battle's scores between two matches. Each is shown for two seconds
at the console's pace before its load is hurried (`READ_BLANKS`). The
card has a time of its own as well, the one minimum the game keeps on a
loading screen: a mark of the route is drawn every 240 of the card's
drawings divided by the chapter's number, and the card stays up after
its loads until the last mark has had its 240
(`story_FinishStageCard`, about three seconds after the first fight's
loads on the port). That wait is marked as a load's too, and hurried
after the same two seconds; the rest of the route is then drawn at once.

**The port's own work** in a load is read during it: the stage's mesh
and its HD pages were already built as the stage's file arrives; the
fighters' and weapons' own models (`gl_fighter.c`) were read at the
first frame that drew them, a hitch of about half a second there, and
are now read at the blanks of the load, as soon as the game has them in
memory (`port_GlFighterPreload`, `port_FighterLoadedModel`), behind the
loading picture.

The log says of every load how many blanks it took, how many were
hurried and how long it took (`load:`), and at the end the run's loads
in all.

**A load that reads nothing** is not hurried for ever. A load hurried
through that has read nothing of the disc for `STALL_SECONDS` (3) of
the PC's clock is waiting for something that does not come (a hang of
the game's), and hurrying it only races its blanks and their sound; from
then to its end it is paced as with quick_loading off, and the log says
`load: WARNING: ... nothing read of the disc for 3 seconds ...` once
(`port_LoadStalls`, `g_port_load_stalled`). A read's start and end are
what count (the disc's hook, kinds 0 and 1); a stream of sound is not a
read. Shown on the hang of "A fight's music" built without its fix: the
warning 3 seconds into the stalled load, then 60 blanks a second.

**Measured** (the window at the console's pace with SDL's dummy drivers,
each plan run with `--quick-loading off` and `on`, on a machine with
other builds running; the blanks are the game's and the same both
ways, the milliseconds the PC's):

| load | blanks | off | on |
| --- | --- | --- | --- |
| power on: the options page's file, the boot picture's | 36 + 52 | 1.45 s | 0.03 s |
| the opening movie to the title | 36 | 0.59 s | 0.01 s |
| the top menu, versus select (three) | 154 | 2.65 s | 0.14 s |
| versus select to the fight (eleven files) | 346 | 6.08 s | 0.52 s |
| the fight to the menu after it, and back to the top menu | 34 + 36 | 1.16 s | 0.07 s |
| story mode: select to the stage card | 132 | 2.42 s | 0.29 s |
| story mode: the stage card, its loads and its own hold (read: two seconds) | 170 + 52 + 186 | 7.14 s | 2.40 s |

`port_game`'s whole run, 4,481 blanks, took 76.2 seconds and 65.0 with
it on; its loads, 11.9 seconds and 0.76. The traces of `port_game`,
`port_other_fight` and a run of story mode are the same on and off, line
for line, and `tools/port_rollback.py --program
build/port-sdl/linux/rebushido --with=--quick-loading --with=on` passes.
With the HD mods and Remastered (offscreen, Mesa's `llvmpipe`, `--fast`),
the fight's first frame took 0.73 seconds before and 0.17 with the
models read during the load, where they took 0.22 and 0.28 seconds at
two of its blanks; the load itself, with the stage's mesh and HD pages
built in it, 2.8 seconds there. What is left of a player's wait is that
work of the port's own, and the boot picture (150 blanks, the
publisher's, which is not a load), the fades and the game's own screens
between, all frames of the game, which quick loading leaves alone.

### Straight into a fight

`--fight STAGE FIGHTER FIGHTER` (both programs, `src/port/fight.c`)
starts the fight in place of the options page, the boot image and the
opening, as the PlayStation's quick-boot build does
([`REBUSHIDO.md`](REBUSHIDO.md), "Booting straight into a fight"), but
chosen when the program starts: for the owner, for agents and for a
benchmark. The names are the game's (`enum StageIndex`, `enum RosterId`,
`enum WeaponId`), any case, spaces and dashes left out, the enum's
prefix allowed, or the number: `--fight tatara-shrine kannuki
mikado:nodachi`. A fighter's weapon follows a colon (a katana if none),
the second fighter is the CPU's (`--fight-2p`: the second pad's;
`--fight-cpu`: the first is the CPU's too, a fight that plays itself, as
the select screen's own choice of two CPUs), the mode is versus. With
`--teams cpu` or `pads` it is two on two, each team two of its first
unless `--partners FIGHTER FIGHTER` names the seconds the same way
(`--partners tatsumi:nodachi utsusemi:yari`; "Two on two"). A
stage's name that is no stage's is a mod's arena's (MODS.md, "Custom
arenas": the fight is on the stage it stands in for, with the arena in
play); a fighter's name not known prints the names, and a stage's that no
arena has either says so, and both end with the usage code. `rb_StartFight` (`src/rebushido/quickboot.c`, which the
quick-boot build's `rb_QuickBoot` now calls too) does what the main menu does when it
first opens, the choice into `g_fight_config`, and `fight_StartMode`.
No card is read on the way, so the settings are the game's own until a
card is loaded from the menus. Without `--fight` nothing changes: the
traces of `port_game` and `port_other_fight` are the same, line for line.
The headless program is in the fight by blank 600; the options page
(`rb_RunOptionsPage`) and the boot image (`disp_ShowBootImage`) return at
once under `RB_PORT` where a fight was asked for, and `main` starts it
after choosing the movie's screen (`rb_PortQuickFight`), so `main`'s own
shape is the original's.

### The fighters' own models

The owner: the fighters may be **replaced whole, as long as the hitboxes
stay exactly the same**. They do: no hitbox comes from a model. The 22
body spheres (`g_fighter_body_point_table`, scaled by
`fighter_ScaleBodyPoints`), the weapon's points on bone 18 and the feet
are placed on the 21 bones of the skeleton every model shares
(`g_model_bones`, `g_model_scales`), which the motions pose. A model only
draws on those bones, so **"Graphics: Remastered" can draw a model of its
own in the place of the game's** and nothing the game computes changes:
the traces of `port_game` with a model drawn and with "Graphics:
Original" are the same, 2,333 lines.

**What the game gives up for it** (`include/port_fighter.h`,
`src/port/fighter_view.c`, one line in `port_RemasterNoteFighter`): as
the renderer comes to a fighter's body, each of his 21 bones' world
matrices, the view and the camera, the projection (H, OFX, OFY) and his
lights (the light and colour matrices, the colour of the dark), and where
his model's nodes are, which names his primitives. None of it is the
game's state (`config/port_state.txt`).

**How it is drawn** (`src/port/gl/gl_fighter.c`). A primitive of a
fighter that has a model of his own -- his body or a part of it, not his
cloth, which the game moves itself, nor his shadow -- is left out, as the
stage's tiles are for the ground's mesh. At the first of them in the
frame the fighters are drawn together, with their cloths ("The cloth,
smooth", below), into a layer of their own with a
depth buffer of its own, skinned on the card (a joint's matrix is the
game's world matrix of the bone times the file's inverse bind matrix),
projected exactly as the coprocessor projects (the stage's mesh's
arithmetic), lit as `NCS` lights a normal, and the texture read as the
chip reads a texel, five bits a channel times the colour, 128 being one;
faces are left out from behind as `render_FaceFront` leaves them. The
layer is laid over the picture held against the ground by the ordering
table's slots, as a fighter's own primitives are ("Depth: the scene is
held against the ground"; a fighter's depth goes into the table shifted
by three whatever his scale, `render_DrawSkinned`), and it writes its
depth where the ground's is, so what the scene paints after it is held
against him. A cloth that a model's file does not have (a file made
before `rb_cloth`) is still the game's own faces, painted by the
table after the layer and, as a fighter's own, not held to that depth:
so it was painted over the model wherever it lay, and the back of
Kannuki's hakama showed through his legs from the front. Such a cloth of a
fighter drawn from a model is now held against the layer's own depth,
pixel by pixel (`port_GlFighterHolds`, `u_fighter_depth` in
`gl_draw.c`), taken 8 units nearer than it is, which keeps it where it
is fastened at the belt: hidden where the model is in front of it, over
the model where it is in front. The layer is laid at the first of the
fighters' primitives or of their cloths, so that no cloth is painted
before it (a long robe's farthest faces can come before every face of
the body: 646 of 16,384 cloth faces in `port_game`'s fight of
`ROSTER_VICTIM` against `ROSTER_LAST_2`). In a picture between two frames (`gl_tween.c`) the bones
and the camera are the two frames' mixed, the rotations made rotations
again.

**The cloth, smooth** (`src/port/gl/gl_cloth.c`). The game moves each
cloth's few points itself (8 to 29 of them, `cloth.c`) and draws flat
faces between them (`render_DrawCloth`). A model's file now carries each
cloth's faces, by those points, with their places in its atlas
(`extras.rb_cloth`, written by `fighter_export.py`, whose atlas takes the
cloth's textures too). As the renderer comes to a cloth node
`port_FighterNoteNode` writes its points down (`PortFighterCloth`, in
128ths of a unit, as `ClothPoint` has them); the layer draws the cloth
from them as a **Catmull-Clark surface**: the points are the control
points, the faces are turned to agree, the subdivision is done three
times (a triangle becomes 48 quads, a quad 64; the open hems are
creases), and the surface is **fitted to pass through every point** (the
inverse of the rows of the points' own places, worked out once when the
file loads, so that each vertex of the surface is a fixed sum of the
points). The texture points are carried linearly within each of the
game's faces, so the texture lies as the game lays it, at the hd mod's
resolution and with the colour tables' tints. It is lit as the body is
(`NCS` of the surface's own normal, made each picture), from both sides
as the game lights a cloth (the side that looks away by the normal turned
round), held in the layer's depth with the body, and its depth against the
scene taken nearer by what the game sorts a cloth's face nearer by
(80 of the coprocessor's depth at the fighter's scale,
`render_SidedDepth`), so that a hem on the ground is neither cut by the
ground's slots nor painted over by the shadow under it. Between two frames
each point is mixed between where the two frames have it. The game's own
faces of such a cloth are left out; a cloth whose model has no file, or
whose file has no `rb_cloth`, is the game's as before. `RB_HD_CLOTH=0` in
the environment draws the game's faces for every cloth.

**The cloth kept out of the body.** The game moved its cloth round its
own low body; the model is fuller, and the cloth the game moved went into
its hips and legs (the owner: "cloth can clip into the existing model").
So once a frame the model's body is posed as the fighters' shader poses
it (the terms, and the normals turned by the joints), and every point of
the cloth, and then every place of the subdivided surface, is put out of
it to 6 units outside along the surface's normal there, all on the card
("The HD fighters on the card", `gl_fighter_card.c`): a point's distance
from the body is the sum of
the body's vertices about it, each one's distance along its own normal,
weighed by (1 - d²/r²)², r one and a half times the nearest vertex's
distance and at least 48 units, found through a hashed grid of 64-unit
cubes. The sum is smooth, so a point kept out moves smoothly from frame
to frame. The frame's cloth kept out is written down in the world's units
under what it was made from (the model, its 21 bones and the cloth's
points: `port_GlFighterHeldCloth`, a buffer on the card each, 32 a slot), and the frame's picture,
its shadow, every picture between it and the next frame and a frame run
ahead to the same pose all read it; a picture between two frames mixes
the two frames' kept-out surfaces, place by place and normal by normal.
It was made again for each of those, and with `run_ahead 2` and
`frame_rate = display` that was the most of what the processor did for a
fight ("Where the time goes", "The HD cloth and run-ahead"). Nothing of
it goes back to the game: its points, its collisions and
its hitboxes are its own. `RB_HD_CLOTH_HOLD=0` draws the cloth as the game
moved it, for measuring; the prover's log counts the surface's places, how
many the game's cloth had inside the model's body and how many are inside
it as drawn (`gl run fighters ... cloth_places cloth_inside
cloth_left_inside`, now once a frame and not once a picture; only the
prover looks again for the places left inside, which cost as much as the
hold itself). Over `port_game`'s fight with every cloth wearer
put in it two by two (a `poke` of the roster, eight fights, 22.9 million
places of the surface), **9.8%** of the cloth as the game moved it was
inside the model's body (from 4.4% for Jo against Hongou to 18.6% for the
armoured man against the victim) and **0.26%** is as drawn (0.03% to
1.2%, the most for Nightstalker against Matsumushi: a place squeezed
between two parts of the body cannot be outside both). The surface is drawn by its own points made
again each frame, and mixed between two frames for each picture, so it
moves with the game's cloth and does not pop.

**A joint as the game builds it.** The game makes a vertex of a joint as
the sum of two bones' turns of two vectors, and its place by a third: a
linear blend of bones cannot say that, and missed by up to 4 pixels in
`port_game`'s fight. A file may carry each vertex's own sum, its *terms*
(`_RB_TERM0` to `_RB_TERM7`: for each bone a vector and how much of the
bone's place is in it), and the card adds them up on the bones the game
posed. Held to the game's own projected corners (`--gl-fighter-pose`) at
blank 3300 of `port_game`, every corner of both fighters has a vertex of
the file within **0.31 pixels**; by the blend of the same file, 4.1. A
file without terms is drawn by its blend, made into terms as it loads.

**The weapons.** Each weapon in a hand, each worn at the waist (the
scabbards: `weapon_QueueWorn`) and each lying loose is drawn from
`models/Wnn.glb` where a mod has one, `nn` being the weapon model
(`enum WeaponModel`, 00 to 57), into the same layer: the file's vertices
are on bone 0 and every bone is given the weapon's world matrix, lit by
the weapon's own lights. A weapon's node is made afresh in each frame's
buffer, so its primitives are told by its address within the frame, and
the model that weapon is by its place among the loaded ones
(`g_resource_loaded_weapons`). Between two frames it is mixed as a
fighter's bones are. A mod's weapon with a model of its own (`model =
KEY`, MODS.md, "A mod's weapon on the select screens") is drawn from
`models/KEY.glb`, in the same format, in place of the weapon model in
its fighter's first hand: `weapon_Queue` says which model slot's hand it
put each node in (`port_RemasterNoteHeldWeapon`, under
`RB_REMASTER_VIEW`), the last two of each slot kept since a frame may be
drawn after the next is built (`port_FighterHeldModel`, `fighter_view.c`,
outside the state); `port_GlFighterWeaponOf` (`gl_fighter.c`) keeps a
place for each key and finds the file in the mods' `models/` whatever
`fighter_models` names.

**The tiny model.** In the first-person view the fighter looked through
is drawn again as a small figure in the top left corner (`model_tiny.c`):
a model of its own for each character (`g_model_tiny_ids`, NDATA ids 77
on), fifteen rigid parts of flat lit faces, a colour each and no texture,
each on a bone of the fighter's own model, seen through a matrix of its
own (`g_model_tiny_matrix`) and projected about another point of the
screen (80, 60), after the scene. `models/T00.glb` to `T26.glb` draws it:
`fighter_export.py --tiny` writes the figure as such a file, its colours
as blocks of a small picture (a block being the colour at 255/496, so
that the light the chip's `NCCS` gives a colour is the light a texel is
given), and the hd mod has it subdivided as the fighters are. The port
notes it as the renderer comes to the first of its body's nodes
(`port_GetFighterTinyView`: the fighter's bones, the figure's eye, its
projection), leaves out the primitives of its body and draws the file
into a layer of its own, laid where the first of them would have been and
held to nothing of the scene; its weapon and the thing it holds are the
game's. Between two frames its bones and eye are mixed as a fighter's.
In `port_game`'s fight with the first-person view poked on, the figure
from the file and the game's own differ by more than 16 of 255 in 3.5% of
the pixels about it, at its edges: its colours and its light are the
game's. The fighter looked through is drawn as outlines
(`model_SetOutline`), and a body drawn so is the game's own lines: no
model stands in for it.

**Chanbara's opponents** (`M302` to `M305`) were drawn in a fight by a
`poke` of `g_fighter_roster_models` for the two fighters' roster entries
(58 to 61), two by two: none torn.

**The game's own colours.** The game fades the fighters in and out and
tints chanbara's opponents, the story's henchmen and the practice ninja
by rewriting their colour tables in video memory (`model_TintOpponent`,
the fades of `disp_`), which a PNG cannot follow. A file may carry the
tables its texture was decoded through and a picture, in the texture's
layout, of which entry each texel is (`extras.rb_palette`). Each frame
the drawing adds to the texture the difference between each entry now in
video memory and in the file; nothing is done while they are the same.
Where the tables are now is found from the game's primitives the model
stands in for: the one shift that takes every table they read to one of
the file's (the second slot's are two pages to the right, the fogged
model's in the fog's block, the weapons' where their group went), kept
while the same model is drawn there. `port_game` had such a change in
732 drawings of its first 3,300 blanks (the fight's fade in).

**The shadows from the model.** With `shadows cast` (`gl_shadow.c`), a
fighter or a weapon drawn from a model of its own casts the model: each
triangle of it, posed by its terms on the frame's bones, is slid onto the
ground along the game's sun as the game's faces are, in the place of the
game's faces of that thing (`port_GlFighterCaster`,
`port_GlFighterCastOf`), posed and slid by the card's own vertex shader
from the model's buffers ("The HD fighters on the card"). A cloth drawn
smooth casts with its fighter: `port_GlFighterCaster` gives a cloth node
his slot, and the slot casts the body and then the smooth cloths, from
the buffers the card kept them out of the body into; a cloth that is the
game's own casts its own faces. A shadow between two frames moves each
vertex the way it went: the same thing's model in the frame after it,
posed on that frame's bones.

**The file** (`src/port/gl/gl_fighter_file.h`): glTF 2.0 binary, one skin
whose joints are named `bone00` to `bone20`, vertices in the game's own
units and axes (y down) at the bind pose, PNG textures; the JSON, inflate
and PNG it needs are in `gl_fighter_file.c`. It is a mod's file: `models/M100.glb`
in a mod's folder (docs/MODS.md), by the model's file name on the disc.
`fighter_models = off` in the settings draws the game's own; the prover
takes `--fighter-models FOLDER|on|off`.

**The tools.** Nothing of the game is in the repository; they read the
player's image and write into folders git ignores.

- `tools/fighter_export.py` writes every model (`M000` to `M306`) as such
  a file: the body, read by emulating `render_DrawSkinnedMesh`'s five
  lists of a section and its carries, and the rigid parts of the bones,
  one mesh; the textures decoded through their colour tables from the
  file's TIMs into one atlas, with the tables and the picture of entries
  (`rb_palette`). Every vertex has its terms; for anything else that
  opens the file, a joint's vertex also gets the two bones and the weight
  of a linear blend that come nearest over a spread of poses about the
  bind pose (24 by default; the worst miss is printed, tens of units for
  a pose far from the bind pose). `--weapons N|all` writes the weapons
  (`W00` to `W57`) from the weapons' files. `--pose` takes a bind pose the
  port wrote: `rebushido-gl --gl-fighter-pose FOLDER,BLANK` writes each
  fighter's bones of the next frame, and the game's own projected corners
  of each of his primitives, `prims.txt`. Held to those corners the
  exporter's reading of the body is within **0.4 pixels at every corner**,
  the joints' included, for both fighters of `port_game`.
- `tools/fighter_hd.py` makes the **hd mod**: every fighter and every
  weapon at a higher definition, from the player's image into a folder
  git ignores (`tmp/mods/hd/` by default), in one command and under three
  minutes; the recipe, the numbers of each model, is
  `tools/fighter_hd.json`. A model is welded (points within half a unit
  made one, their terms averaged), creased where faces meet at more than
  75 degrees (35 for a weapon) and along open edges, subdivided twice
  (Catmull-Clark) with every vertex's terms and weights carried by the
  same sums as its place -- the subdivision is linear in them, so the new
  surface is at every pose the subdivision of the game's own posed mesh,
  and needs at most eight bones a vertex -- then fitted, twelve rounds,
  so that it passes through the game's own vertices (within 2.3 units,
  most within a fifth) and its silhouette is the game's and not shrunk
  inside it; its normals are smooth across the surface and hard across a
  crease and where the game does not light a face. The texture is made
  four times larger by Scale2x twice, which keeps an edge between two
  colours sharp along a diagonal, then smoothed within each colour's
  area; the colour tables' picture is kept as it is, so the tints reach
  it. The port draws a texture so made smoothed and from mipmaps. A
  fighter comes to 11,000 to 15,000 vertices and 16,000 to 22,000
  triangles, against the game's 1,000; a weapon to about 1,500 triangles.
- `tools/blender/fighter_view.py` renders a file in Blender, front and
  side, with nothing shown; `tools/blender/fighter_rebuild.py` was the
  first rebuild, in Blender (it cannot carry the terms or the colour
  tables' picture); `tools/fighter_conform.py` makes what Blender wrote,
  for a model changed by hand, a file the port reads (each vertex where
  glTF draws it at the bind pose, back into the game's space, with the
  original's bind pose and colour tables' picture);
  `tools/fighter_texture.py` makes an atlas larger for such a model
  (`tools/blender/README.md`).

**Measured.** The game's own models through this path against the
remastered picture with the game's primitives, `port_game`'s fight at
scale 2, 27 pictures from blank 3000 to 4300: **2.05% of the pixels differ by more than 16 of 255 and 0.71% by more than 48**; all of it on the fighters. The rest is the difference
between Gouraud shading of every face and the game's flat faces, the
joints' linear blend, and edges. `port_gl_prove --quick` is what it was
on `main` (its blocks check fails there too, with the same numbers).
With the terms, the weapons and the colour tables, the game's own models
and weapons through this path at blanks 3000 and 3300: 2.28% and 1.00%;
the weapons and scabbards lie where the game's do to the pixel. Through
the fight's fade in the picture's mean brightness is the game's to 0.4
of 255 at every fourth blank.

**The hd mod, seen.** Every roster entry (`ROSTER_KANNUKI_STORY` to
`ROSTER_LAST_2`, 33 of them, every model but chanbara's four) was put in
`port_game`'s fight two by two (a `poke` of `config_roster_p1` and
`_p2`), with every weapon and scabbard of those fights from the mod, and
looked at at blanks 3300 and 3600: none torn, none with a vertex astray.
With `shadows cast` the shadows are the models' (44,064 triangles a
picture for two fighters and four weapons, against the game's 2,266
faces), and the models are not darkened by them: what a model covers is
marked in the shadows' mask as the game's faces are
(`port_GlFighterDrawOccluders`).

**The flicker over the feet** (the owner, on his card at 85 Hz with
`frame_rate = display`): the mark was made only while the models were
"drawn this picture", and a picture between two frames
(`gl_tween.c`) puts that back before it finishes, so in every such
picture the mask had no model in it and the shadow darkened whatever of
the fighters it fell on -- his feet above all, which stand in it. With
`frame_rate = display` that is every picture, the edge of the darkening
moving with each. The mark now goes by whether the layer was laid over
this picture (`is_laid`), and it is made without the ground's slots,
which near the soles give one answer one picture and the other the next.
Measured on `port_game`'s fight at scale 2, 48 consecutive pictures of
four a frame, both fighters from the `hd` mod and `shadows cast`: before,
16,790 of the fighters' pixels a picture were darkened and 231 of them
toggled between two pictures; after, none of either. The same unmarked
mask is what darkened a fighter lying on the floor with his own shadow
and a fighter with the other's (the owner's two reports after it): over
`port_game`'s kill, 52 pictures of two a frame, 3,466 pixels a picture
of a fighter, his weapon or the HUD lie under a silhouette and are kept
out of the darkening, the body on the floor among them, and the shadow
lies on the floor about him.

**Floors that are not tiles.** The darkening falls on the stage's tiles
(the ground's mesh) and on nothing else of the stage: everything else of
the scene is marked in the mask as something standing in the shadow.
Some stages build floor a fighter stands on as pieces of the scene and
not as tiles -- the kabuki stage's boards, the dojo's, the lost bridge's
-- and there a fighter's shadow vanished wherever it fell on them. A
face of the stage's pieces that lies level, within 30 degrees, is now
ground to the shadows (`port_GlShadowIsFloor`, `gl_shadow.c`: its
corners taken back into the world by the stage's view). Faces taken
for floor over a stage's tour: the Meikyokan dojo 3,656, the lost bridge
2,642, the kabuki stage 1,658; the Tatara shrine, the castle rooftop
and the terraced fields none, their floors being tiles. On the kabuki
stage's tour, 177,045 pixels were darkened and none of them is wholly
something not ground; the darkest is 0.297 of what was there.

**A body the game casts no shadow of.** The game's shadow nodes say what
casts a shadow and onto what ground, and on some floors the nodes of a
fighter's body are not drawn at all, leaving his weapon's shadow and
nothing else -- which is what the owner's picture shows, though the
prover's tours of the stages measured here never came to such a frame.
A fighter drawn from a model whose faces came this frame with no ground
now has his shadow laid on the floor his soles are on, the model's
lowest point (`at_feet` in the prover's line).

**What is not done.** The tiny model's weapon is the game's flat one,
and in grayscale mode the figure is the colour file's; the order of a model against the game's own faces is per pixel,
so a hakama's hem now hides the sandal it was painted under, and a
sandal's sole that lies in the ground is cut by the ground's slots (both
as before the hd mod); a texture a mod's model draws in a layout other
than the atlas's does not take the game's tints; chanbara's opponents
(`M302` to `M305`) were rebuilt and seen only in Blender, not in a
fight, because no roster entry draws them. The rebuild
cannot add what the game's mesh does not have: a face is still a few
painted quads, and a fold of cloth is the texture's.

### The fighters remade

The hd mod is the game's own mesh made smooth: a face is still a few
painted quads, a hand a block, and the smoothing swells it beyond the
game's outline (by up to 42 units at Tatsumi's feet). ROADMAP 19 remakes
the fighters one by one, by a pipeline with checks that fail
(`tools/port_models/`, its README for the commands), into a mod of its
own, `remade` (`tmp/mods/remade/`, order 30, over the hd mod), made from
the player's disc in one command:

    python tools/port_models/build.py --fighter M101

**The game's motions as the port poses them.** `--gl-fighter-motion
F[,FROM[,TO]]` (the headless program) writes each fighter's 21 bones'
world matrices once a frame into `F/<model>.<slot>.motion.txt`; it draws
nothing and changes nothing of the game. `tools/port_models/motions.py`
runs fights straight from `--fight` (`--fight-2p`, both pads pressed by a
script of strikes high, middle and low, stances, guards, runs, jumps
and crouches, each fighter on each side), so that a fighter's motions,
his being struck and his fall are all in it: for Tatsumi four fights,
339 poses at every 12th frame. `tools/blender/fighter_scene.py` keys
them on any model's armature in Blender (a joint's pose is the game's
world matrix times the file's inverse bind matrix, exactly the port's)
and renders models side by side through them.

**The checks** (`tools/port_models/check.py`, limits in `limits.json`;
`tests/rebushido/fighter_check.py` holds them to made-up models): the
skeleton and bind pose exactly the game's; every vertex wholly on the
bones and no bone the game moves left without one; each bone's piece no
longer, wider or deeper than the game's by more than 30 units; the
silhouette through every motion, from the front, the side and above,
within 24 units of the game's outline (at most 40 pixels of 4 units, or
0.4% of the outline, beyond it in any one view; the worst drawn and the
strays told by their nearest bone); no more triangles of one surface
through itself than the game's own model has; and the budgets, 40,000
triangles a fighter (12,000 and 4,000 at the lower levels), ten million
texels, four fighters 160,000 triangles and 216 MB of textures. The
weapons are not remade: their models, lengths and grips are the game's
files, and no hitbox is a model's ("The fighters' own models").

**What a remade fighter is** (`tools/port_models/README.md` for each
part). The owner's rule (2026-11-18): the game's own design, cleaned, not
a new one; a remade fighter reads as the PlayStation's model at a glance.
Two passes went another way first (new hands and a face in relief on the
hd mod's body; then a body painted anew with folds, a head and hands
modelled whole) and were set aside as too far from the game's; they are
in the history (`2dc5ef3a7`).

- **the mesh**: the game's own, each triangle made four, twice, as a
  smooth surface through the game's shape (Loop's subdivision, most of
  its smoothing), each vertex keeping its own bone, so that it bends
  exactly as the game's. Nothing is fitted (the hd mod's fitting swelled
  it, and made the chest bulge), and no vertex is let far inside the
  game's surface; small pieces (a strap, a shoulder's plate) are cut and
  not smoothed, and a joint's stretched triangles are left the game's
  own (no vertex half way along one moves as the game's triangle does);
- **the texture**: the game's own, cleaned and sharp (its colours
  gathered into kinds, the picture of kinds made eight times larger with
  its staircases cut across, the tones of a kind run into one another),
  read into **a layout of its own** (cut where the game's is, laid flat
  in Blender at one scale, no island over another; the head in a
  rectangle of its own with more texels, its skin more still);
- **the face**: the game's own texels, each a square block (eight
  across for Tatsumi), neither cleaned nor smoothed: the owner's rule
  for every fighter (2026-11-19), the face stays pixelated;
- **the maps for the lighting**: beside the colour, in its layout, an
  occlusion-roughness-metal map by what each texel is made of (the
  recipe's materials, by the game's colours) and a normal map of the
  braid's roundness and a fine grain; no fold, no wrinkle;
- **the levels of detail**: the same surface at a quarter and a
  sixteenth of the triangles, in the same layout (one texture for all
  three); the port does not choose between them yet.

The colour tables' picture is in the same layout, each texel the entry
the game's texture had there, so that the fight's fade-in and the tints
reach it as they reach the game's own.

**Tatsumi, measured**: 16,304 triangles (4,076 and 1,019 at the lower
levels), 5.8 million texels; every check passes (the watertight one
among them: no edge left open that the game's is not, no triangle
turned over), with nothing outside
the game's outline and nothing of it uncovered in any of 339 poses. The
renders of the game's and the new model are in the main checkout's
`tmp/models/renders/M101-fable/`, and the passes as the owner saw them
in `tmp/models/versions/` (`tools/model_viewer/`).

**What is left** (the owner's list): the joints, which the renderer now
blends for every model ("The joints blended", below), and which a
remade fighter may still want edge loops of its own for (a joint of the
game's mesh has a ring or two of vertices each side to bend); the clipping (a collar, a
sleeve, the hair through another part), which is the game's own and
would be taken out motion by motion; the other fighters; the port
choosing among the levels of detail.

### The joints blended

The game builds a fighter of rigid parts, one a bone, and joins two parts
by a few vertices of its own (a place on the parent and an offset the
child turns, "A joint as the game builds it") and triangles stretched
between. When the parts turn against each other the seam opens, creases
or passes through itself; the shoulders, the elbows, the waist, the hips
and the knees of every fighter show it. ROADMAP 19 left a choice: each
joint rebuilt by hand per fighter, or the renderer blending them for all.
This is the second (`src/port/gl/gl_joints.c`, `joints = blended|rigid`,
built in `blended`; Remastered, and only where a fighter is drawn from a
model of his own).

- **The bands, worked out once a model as it loads**
  (`port_GlJointsWeigh`). A table of joints of the skeleton every model
  shares: the neck (2-3), each shoulder (4-5, 8-9), each elbow (5-6,
  9-10), the waist (2-12, the chest and the hips on the body's place),
  each hip (12-13, 12-16) and each knee (13-14, 16-17). A joint's place is
  its second bone's place at the bind pose and its way is that bone's x
  axis (the game's bones run along x). Each vertex is given the bone that
  carries it (the turning bone of a game's joint vertex, else its largest
  share of place, as `geometry.py`'s `carry_bone`). For a vertex of the
  joint's two parts: how far along the way it lies, from the first part's
  end of the band (a share of that part's own length from the joint,
  measured from the model's vertices) to the second's, smoothed, is how
  much the second bone carries it; less, back to its own bone, the farther
  it lies from the joint's axis, past 1.25 times the second part's
  thickness there (the 90th centile of its vertices' distances from the
  axis in the band) to nothing at 1.75 times. A vertex near two joints
  takes the one that moves it most; one moved by less than a hundredth
  is left as it was. The result is a buffer of four floats a vertex (the
  two bones and the share), the model's vertex array's attribute 15.
- **Each pose** (`port_GlJointsDuals`): each bone's skinning matrix (its
  world matrix times its inverse bind matrix) as a **dual quaternion**,
  made rigid about the bone's own place, 21 of them, from the same
  matrices the layer is drawn with (a picture between two frames mixes
  its bones first, so the blend follows). A vertex of a band is its two
  bones' dual quaternions mixed by its share (the second turned to the
  first's half of the sphere), made unit and applied to its bind place,
  its normal turned by the same rotation: a turn of the band about the
  joint, which keeps its volume where a linear mix of matrices shrinks it
  and twists a candy wrapper. A vertex in no band is drawn by its terms
  exactly as before.
- **Where it holds**: the fighters' layer (`gl_fighter.c`), the body the
  card poses for the cloth to be kept out of (`gl_fighter_card.c`, whose
  keys and the held cloths' take the setting in), and the shadows cast
  from the models and the sun's maps (`gl_shadow.c`, both frames' dual
  quaternions): the shadow is the body drawn. The reflections and the
  ambient occlusion read the layer. The boxes of the layer and the shadows
  hold a band turned (a vertex stays as far from the joint, which stays as
  far from its bones' places: `port_GlJointsReach`).
- **What stays rigid**: the head and the face (the neck's band is the
  chest's end alone, up to the head's place; a band through the head
  turned the chin and the hair with a head that turns some 120 degrees on
  the chest in a stance), the hands, which hold the weapon on their bone
  (no wrist is in the table, so a weapon and the hand on it never part),
  and the feet on the ground. **The shoulder** is the shoulder's own piece
  (bone 4 or 8) carried toward the upper arm, the arm itself rigid: a band
  across both bent the arm's top toward the shoulder's bone, which turns
  far from the chest, and pushed it out through Kannuki's vest; a band
  into the chest (2-4) dragged the chest's side with a bone that turns up
  to 165 degrees on it.
- **The game's state is not touched.** Over a CPU fight of 3,000 blanks
  (Tatsumi against Utsusemi, every fighter's own model from the disc), the
  traces of `rigid` and `blended` are the same, 1,322 lines, and
  `--rollback-test 8` passes with each (54 rounds, 432 frames compared).

**Seen.** Every fighter's own model (`fighter_export.py --models all`,
the game's mesh), in CPU fights of 3,000 blanks two by two through the
whole roster (`kannuki story` to `last 2`, 33 entries, 17 fights: the
story fighters, the bosses and the henchmen), on the chanbara stage (Tatsumi
against Kaun, Jo against Mikado) and the practice stage (Kannuki against
Sidearm 1), with their stances, strikes, falls and kills, every 150th
blank at four times the console's size, `rigid` beside `blended`; the six pictures of each fight that differ most, and each fighter in them cropped close, were made, and those of Tatsumi, Utsusemi, Kannuki, Gengoro, Kaun, the armoured man, Tsubame, Sazanka and the last boss against the victim were looked at closely. The
pictures, the fight camera and each fighter cropped close, are in the main
checkout's `tmp/joints19/` (`fights/`, `close/`). The hips, knees and
waist bend round instead of folding (the hakama's and the coat's skirt no
longer opens at the hip, a crouching knee no longer shows its inside
corner), the elbows lose their notch and the shoulder's piece stays on
the arm. No vertex astray, no candy wrapper, no hand off its weapon was
found in them. What a band cannot do: a joint of the game's mesh has a
ring or two of vertices a side, so a deep bend is rounded across a few
large triangles, not a smooth curve; a remade model with edge loops at
the joints (`tools/port_models/`) bends smoother by the same blend.

**Cost.** Once a model as it loads, the bands (a few passes over its
vertices: nothing to see beside reading the file). Each pose, 21 dual
quaternions on the processor beside the 21 matrices it already makes,
and a vec4 a vertex more for the card; a vertex of a band is a dual
quaternion mixed and applied in place of its eight terms. Measured with
`port_bench.py` (RTX 3090, 2,560 by 1,440) with every fighter drawn from
the game's own model (a trial: the disc's models as a mod in the bench's
`mods`), `--set joints=rigid` against `blended`, the card's passes, ms
(median, a fight frame):

| case | skin, rigid | skin, blended | fighters, rigid | fighters, blended | cloth, rigid | cloth, blended |
| --- | --- | --- | --- | --- | --- | --- |
| `ra1` | 0.07 | 0.07 | 0.12 | 0.12 | 0.41 | 0.41 |
| `ra1-stress` (four fighters) | 0.10 | 0.10 | 0.17 | 0.17 | 0.62 | 0.63 |
| `ra1-2v2` | 0.12 | 0.12 | 0.14 | 0.14 | 0.73 | 0.75 |

The same to a hundredth: below what the bench can tell. Other agents' runs
loaded the machine through both (the bench says so), so the totals are not
to be trusted, but the passes held from one run to the other. The bench's
own cases draw the game's own fighters (no fighter models: "not hd"), where
nothing of this runs.
### The effects

**How the game draws them** (`src/main/eff.c`, `include/eff.h`). Blood,
sparks, the flashes of a clash, a lock and a hit, dust, splashes,
footprints, the muzzle flash and the stage's flames are all flat
textured quads (`POLY_FT4`) the game builds itself, outside the
renderer's walk, into a buffer of its own and links into the ordering
table at their depth; the surf (`src/main/wave.c`) is a sheet of 8 by 16
quads drawn through the tile emitter. Their textures are on two pages at
x 896 and 960 of video memory, and nearly all of them mix with what is
behind them: added (the sparks, the flashes, the flames, the blood) or
half and half (the dust). They come in two shapes:

- **a sprite**: one point projected (Sony's `RotTransPers`, or the
  macros `gte_RotTransPers` and `gte_stsxy3`), and the quad's corners
  that point plus so many whole pixels each way, the size worked out
  from the depth by a quotient rounded down (`g_eff_scale`) -- the
  flames, the flashes and streaks, the dust, the splashes, and every
  particle of blood, dirt and water, each drawn two or three times along
  the way it moved this frame;
- **four points projected**, one a corner: the muzzle flash
  (`RotTransPers4`) and the footprints, which lie on the ground.

On the console a sprite is therefore placed on whole pixels and sized in
whole pixels: at four times the resolution a spark steps four pixels at
a time as it moves and as it grows, and a drop of blood is a block of
sixteen pixels a texel.

**What the side channel is told** (`port_remaster.h`, "the effects";
`EFF_NOTE_...` in `eff.c`, under `RB_REMASTER_VIEW`). As each quad is
linked, `port_RemasterNoteSprite` says which point it was built about,
the numbers its size was divided from, and which effect it is (the
effect's own record: `&g_eff_flames[i]`, a particle, a splash's slot,
and which of its quads); `port_RemasterNoteEffect` says which four points
a quad's corners are. The decoder then gives a sprite's corners the
point's place as the projection worked it out, moved as far as the game
moved it itself (the water's drops are shaken by up to 20 pixels after
they are projected), plus each corner's own offset made as large as it
was before the quotient was rounded down, all at the point's depth. The
macros' stores are a plain read of the register in the port
(`tools/port_gte_macros.py`), so `port_GteNoteStored` writes down what
they stored; `RotTransPers`, `RotTransPers3` and `RotTransPers4` store
through `port_GteStoreData`, as their `swc2` does. The surf's points go
where the tile emitter looks for them (`port_RemasterCopyPoint`,
`wave.c`). Nothing of the game's is read or written.

**`effects`** (`rebushido.cfg`, the settings menu, `--effects` of the
prover; `port_remaster.h`):

| | |
|---|---|
| `original` | where the console put them, as before: nothing is kept |
| `exact` | each where the projection put it, its size without the rounding, and named, so that a picture between two frames moves it by its name (`gl_tween.c`) |
| `smooth` | `exact`, and its texture read smoothed (`FLAG_SMOOTH_EFFECT`, `gl_draw.c`): the four texels about the point, a clear one counting as how much of the effect is not there, so the edge fades out instead of stopping at a texel's edge |

**A stage's flame is held by the depth** with `exact` and `smooth`
(`port_RemasterNoteStanding`, `port_GlIsHeldFlame`): the owner
(2026-11-19), "the fires on the Shainto Gate stage still seem to be
drawn on top of everything". The console hid a flame behind a pillar by
painting the pillar after it, by the ordering table; the remastered
picture paints the ground's mesh, its pieces and all, where the
farthest tile was, so a flame came after every pillar and wall in front
of it and over them, in the 4:3 middle and the bands alike. A flame
stands on its point, all of it above it and none in front, so its whole
quad is tested at that point's depth less half its width (the game's
own size for its kind: what is nearer by less than that is inside the
fire, the rim and the bars of its basket, which at the point's own depth
cut dark lines through it), and writes none, against what the depth
buffer holds: the ground's mesh and its pieces, the fighters' models'
layer, and the fighters' own polygons, which write their depth as they
are painted (`port_GlMarksDepth`) because the game sorts a flame by its
own depth and a fighter at the renderer's scale, and a flame behind him
can come a slot nearer. The flame's mix is its own (added); nothing of
it is written. Every other effect, which reaches in front of its point
as far as round it, is held as before.

Whatever the setting, **no other effect is hidden by the ground's
depth**, as the console hid none (`port_GlIsOverGround`): a dust cloud
whose lower half is "under" the ground in the world is drawn whole, as
the console draws it. That is also why there is no soft-particle fade
against the depth buffer: it would take away what the console shows,
and an effect has no hard edge against the ground to soften in the
first place. The colours, the mixes (`added` stays `ONE, ONE`; half and
half stays half), the textures and the timing are the game's; `smooth`
fades the edge in whichever way the draw already mixes.

**Measured** (`port_game`'s plan, scale 2): 4,336 polygons of effects,
every one of them placed (`gl run effects smooth faces 4336 unplaced
0`), and the traces of `--effects original` and `--effects smooth` the
same, 2,338 lines. Pictures of a fight side by side: the clash's streak
and glow and the blood are round and soft at the edge instead of
blocks, in the same colours and the same places to a pixel of the
console's; on a picture of blood across the screen 0.29% of its pixels
differ by more than 16 of 255 and no cell of an eight by eight grid
changes its mean colour by more than 2.1 of 255. At scale 4, 35
pictures of the fight: the mean colour of the whole the same to a
hundredth of a unit, the worst cell 2.0. The surf, on the Kimigaura
coast's tour (stage 11): 33,320 more of the run's polygons placed with
`exact` than with `original` (728,011 against 694,691), the traces the
same, and the water's glints drawn in perspective where they were
blocks laid flat; the mean colour the same to a hundredth, the worst
cell 1.2.

### Lighting

**How the game lights** (`src/main/light.c`, `src/main/scene.c`). A
light setup (`LightSetup`) is three directional lights -- a light
matrix of their directions and a colour matrix of their colours -- and
the colour of the dark; `light_SetStage` gives each stage its own from
a table of 38 rows, so a night stage is lit dim and blue and a dusk
stage warm. The coprocessor lights a normal with NCS (each light's turn
on the normal, none below nought, through the colour matrix onto the
colour of the dark) and the colour goes into the primitive. A tile of
the ground has one normal a face, so the ground is lit face by face; a
fighter has one a vertex, and the colour runs evenly between them.

**`smooth_light`** (`src/port/gl/gl_light.h`) works out that same NCS,
with the same lights, at every pixel, from a normal that runs smoothly
across the surface:

- **the ground's mesh** (`gl_stage.c`): as the mesh is built, each of
  its vertices is given the mean of the normals of the faces about it
  that turn by less than 45 degrees from its own face, each weighed by
  its area (`port_GlLightSmoothNormals`), so a slope is shaded as a
  slope and a wall still meets the floor at an edge; the shader lights
  that normal with the tiles' light setup (`port_RemasterTileLights`),
  given it every frame;
- **the fighters' own models** (`gl_fighter.c`): the normal the vertex
  was lit from goes to the pixel, and the fighter's own lights light it
  there.

A face whose corners all have the same normal -- every flat floor --
comes out the colour the coprocessor gave it, to its rounding: so a
stage keeps its palette and its mood, and only what was faceted
changes. What the game draws from primitives (the scenery's models, the
fighters drawn by the game itself) is lit as the game lit it: those
colours are in the primitives.

**Measured** (the stage tour of `tools/port_stage_prove.py`, the mesh on
every stage, the `hd` fighters, scale 2, 4,800 blanks, a picture every
300; `smooth_light` off against on): the traces the same on all three
stages; the mean colour of the whole picture moves by at most 0.14 of
255 (the Meikyokan dojo, stage 1: 65.81, 59.09, 51.47 against 65.95,
59.21, 51.58), and no cell of an eight by eight grid by more than 2.5
(the Meikyokan dojo), 2.4 (the purple cavern, a dark stage, 24) and 1.3
(the terraced fields, 15). Under a hundredth of a percent of the pixels
differ by more than 16 of 255. What changes is where it should: the
cavern's walls and the haystacks' facets run smoothly into each other
instead of stepping face by face, the folds of a fighter's model are
shaded across the fold, and every flat floor is the colour it was. It is
**subtle by design**: the lights are the game's, and the game's ground is
mostly flat. Nothing measurable in the time a frame takes on Mesa's
software renderer (2,228 against 2,226 microseconds).

### Where the lights come from

The renderer's list of a frame's lights (`gl_lights.h`) is filled once a
game frame by `src/port/gl/gl_light_list.c`, from the backend's `begin`
(`gl_draw.c`), out of three sources, none of them the game's state
(`src/port/lights.c` is `outside` in `config/port_state.txt`; nothing
the game computes reads any of it):

- **The stage's light files** (`stage_lights`): every mod's
  `lights/stage_NN.txt`, layered in the mods' order (MODS.md, "Lights";
  `port_ModFindEach`), read once when the stage is first drawn. Each
  light is placed each frame on what it is fastened to: the world, a
  fighter's body or bone (`port_GetFighterView`), a weapon
  (`port_GetFighterWeapons`), a piece of the stage
  (`port_RemasterStagePieces`), or the stage's own flames.
- **The game's flames.** Nine stages have flames of their own -- the
  torches and fires `eff_DrawFlames` draws as sprites, two or four a
  stage, from the stage's sound file (formats/STAGE_BIN.md, "Billboard
  list"). `eff_DrawFlames` tells `port_LightsNoteFlames` the list each
  frame it draws them, so a light file's `on = flames` puts a light at
  each, the program's own files carry no place of the disc's, and a mod
  that moves the flames (a stage's file) moves their lights. A light is
  put half the flame's height above its foot (`FLAME_..._HEIGHT`), a
  fire's reaching half as far again as a torch's.
- **The fight's flashes** (`effect_lights`). The game has a point light
  of its own: a clash, a hit, a lock and a shot ask for `EFF_LIT`, and
  for ten frames `eff_Update` lights the fighters by a light at the
  effect's place in its colour (white for a shot, rose for a hit, pale
  blue for a lock, pale red for a clash: `g_eff_light_color`). The end
  of `eff_Update` tells `port_LightsNoteFlash` where it is, its colour
  and the frames it has left, and the list gets a light of that colour,
  of intensity 1.5 falling to nothing with the game's own count, reaching
  4,500 units, asking to cast shadows: the flash where blades meet
  lights the ground, the walls and both fighters, and is gone when the
  game's own light goes. Each glowing quad `eff_DrawFlash` draws (the
  sparks of a clash, a hit's flash, a lock's star, the streaks) is told
  to `port_LightsNoteGlow` and becomes a small light in its colour at
  its place (intensity 0.4, 1,600 units).

What the game is read for is read where it is already being computed,
under `RB_REMASTER_VIEW` beside the effects' other notes; nothing of the
game's is written. Under rollback the notes of a frame run again are
written again, and the list is whatever the last frame said. A light's
`id`, which pairs it between two frames and seeds its flicker, is the
stage's number and its place in the files for a light of the files
(with the flame's for `on = flames`), the event's count for a flash
and the quad's slot for a glow.

Where no stage has been drawn for 30 frames (the menus) or no effects
have run for 30 (a pause), the lists of those sources are empty. The
GL prover keeps both off unless `--stage-lights on` and
`--effect-lights on` are given, so that the provers hold the console's
lighting.

**Finding them.** `tools/port_lights.py` (MODS.md, "The lights the
program ships") dumps each stage's mesh, video memory and textures as the
program built them (`port_GlLightListDumpMesh`, with `--dump-textures`),
reads every face's texels as they are drawn (the vertex's colour times
the texel, its colour table found where the stage loaded it, not on the
row the fog writes over), and gathers what looks lit from within into
drafts; and takes pictures of each stage's tour with the lights on.
What it found: the texels alone tell little -- the game lit its night
stages by their vertex colours, and a white line on the floor or a
whitewashed wall is as bright as a lamp -- so its drafts are read
beside the pictures. Two of them were lamps: the parking garage's
ceiling fittings (five, 8,000 units apart) and the ferry dock's street
lamp. The other nine stages with lights are the game's own flames
(MODS.md, "The lights the program ships", has the table).

### What the old branch's lighting came to

Branch `worktree-agent-adce6b210aa0a05f8` (10bd277dd) had four lighting
settings for the picture as it was before the ground's mesh. On this
tree:

- **its shadows** were rejected and replaced ("The shadows");
- **`smooth_fog`** worked the fog out per pixel for the game's own tile
  primitives; the ground's mesh already reads the fog's level for each
  pixel's own depth and mixes two levels ("The ground as one mesh"), so
  nothing of it is wanted;
- **`smooth_light`** carried the normal the GTE lit each corner with
  through the side channel, for every primitive, and lit it again in the
  picture's shader. Its own measurement was "correct and all but
  invisible": the game's faces are small and shaded corner by corner
  already. The two places where it is not invisible are the ones that
  are now drawn from meshes of their own -- the ground's flat faces and
  the fighters' models -- and there the normals are the mesh's, so
  `smooth_light` here lights those from them and needs nothing of the
  side channel or of `gte.c`;
- **`ambient_occlusion`** darkened creases from the scene's depth at the
  console's resolution. It drew halos round a fighter against a far
  wall and darkened the far edge of a stage, it needed a depth of the
  whole scene that this tree's picture does not keep (only the ground's
  mesh and the fighters write depth; the scenery is painted in the
  console's order), and it darkens what the console never darkened. Not
  taken.

So nothing of that branch is merged; what was worth keeping is
`smooth_light`'s idea, done again on the meshes.

### What each later step needs

- **Lighting.** The ground's mesh and the fighters' models are lit at
  the pixel with the game's own lights ("Lighting"). The rest of the
  scene -- the scenery's models, drawn from the game's primitives --
  still has its colours in the primitive; lighting it again would need
  the normals carried through the side channel, as the old branch did,
  for a change it measured as all but invisible. (The shadows are done:
  "The shadows". What is still missing there is the shadow of one thing
  on another -- a fighter's shadow across the other fighter, or up a
  wall -- which wants the ground's own depth, or a shadow map from the
  stage's sun.)

### The lighting overhaul

The owner (2026-11-18), on ROADMAP item 14: "a total overhaul as if this
were a modern game": several lights, shadows of everything on
everything, reflections, and the finish a modern renderer has -- but
still Bushido Blade 2, by known techniques, the Remastered picture alone
(`Graphics: Original` is the console's picture to the pixel), and
nothing of it in the game's state or its step.

**The base stays the game's.** Every surface the remastered picture
lights itself -- the ground's mesh, the stage's pieces in it, the
fighters' own models -- is lit first exactly as the coprocessor lights
it ("Lighting"): the stage's three directional lights and its colour of
the dark, from `light_SetStage`'s table of 38, so a night stage stays
dim and blue and a dusk stage warm. What the overhaul adds is laid over
that, in the same units (bytes of light, 128 being one, the texel times
it over sixteen), so where it adds nothing the picture is what it was.
What the game still draws from its own primitives (effects, the HUD, a
piece the game has changed, fighters with no model of their own) keeps
the colours in its primitives.

**Forward, not deferred.** The game paints in the order of its ordering
table, half-transparent primitives mixed in where they fall, and only the
ground's mesh and the fighters' models have a depth of their own; there
is no G-buffer to light afterwards without breaking that order and
every half-transparent face. So each of the three shaders that light
(gl_stage.c, gl_fighter.c, and the cloths through the fighters') reads
what it needs -- the sun's maps, the light list -- and lights its own
pixel, in the place the game paints it. The many lights are culled on
the processor into a grid over the picture (clustered forward shading:
which lights reach each cell of the screen and of depth), handed over in
a buffer, so a pixel loops over the few that reach it.

**The phases**, each a commit that passes the gate:

| | what | setting |
|---|---|---|
| a | the sun's shadow maps, cascaded, soft | `shadows = map` |
| b | lights with a place (point and spot), per pixel, the strongest few with shadows | `lights` |
| c | reflections: on the screen, the sky behind as the fallback | `reflections` |
| d | a tonemap held to the game's look, ambient occlusion, bloom | `tonemap`, `ambient_occlusion`, `bloom` |

All four phases are in, each setting on with nothing said.

**The budget.** 144 a second at 2,560 by 1,440 on the owner's RTX 3090:
6.94 ms a refresh in all, of which the picture was 3.2 to 4.3 ms of the
card at the end of the performance run (HANDOFF). The lighting's own
share is held at **2.0 ms of the card** with two fighters (1.5 before c
and d), and the
stress case (four fighters, the ground twice, many lights) must still
hold the card's 95% under the refresh. `tools/port_bench.py` reads the
card's time of each new pass (`sun`, and those after it) and holds them
(`LIGHT_PASSES`: `sun`, `lights`, `ssr`, `ao`, `bloom`, `post`).

#### a. The sun's shadow maps (`shadows = map`)

`src/port/gl/gl_sun.c`, `gl_sun.h`. The stage's sun gets four depth maps
of 2,048 texels a side. Three are nested about the camera (cascades):
each holds a slice of the view -- 16 to 1,600 world units, to 5,000, to
16,000 -- in the smallest sphere round it, seen along the sun and reaching
6,000 units further towards it (and a caster further still is held to the
map's near end, not cut: `GL_DEPTH_CLAMP`). The fourth is the whole
stage: the box the ground's mesh stands in, seen along the sun, each side
of the map scaled to the box's own (`port_SunWhole`; 31 to 45 units a
texel on the largest stages). It holds everything past 16,000 units, as
far as the stage is drawn (`draw_distance max` draws all of it, 90,000
units corner to corner on the largest), and as it never moves while the
stage and its sun stand it neither crawls nor needs holding still. The
last 5 per cent of each nested map's square is mixed into the next map's,
so no line shows where one gives way to the other; past the whole stage's
box there is nothing to shade.

Before the fourth map (2026-11-19, the owner: "shadows don't seem to have
far draw distance") nothing past 16,000 units had a sun shadow at all,
the third map ending in a hard edge, while the stage is drawn to 90,000;
and of the stage's pieces only those the frame queued cast, so a building
beside or behind the camera, or past the game's own cells, threw no
shadow into view.

- **What casts.** The whole of the ground's mesh and every piece of the
  stage that stands as it was taken down, whether the frame drew it or not
  (`port_GlStageDrawSunCasters`, `PortStageView.standing`; a cut-out texel
  casts nothing, so a fence casts its own shape; a prop cut down is the
  game's own primitives and casts nothing), and the fighters, weapons and
  cloths
  (`port_GlShadowDrawSunCasters`): what `shadows cast` is made of -- the
  fighters' own faces taken back into the world from the coprocessor's
  sums, or their models posed on the card -- unflattened, at the picture's
  own phase between two frames. The direction is the game's own shadow's
  (`light_ProjectShadow`'s slope), so the shadows fall where the game's
  fell.
- **What it falls on.** Everything the remastered picture lights itself:
  the ground's mesh, the pieces in it and the fighters' models, each
  reading the maps at its pixel (`sun_visible`, `PORT_GL_SUN_SHADER`). So
  a fighter's shadow climbs a wall and a step, a pillar's shadow crosses
  the floor and the fighter standing in it, and a fighter's raised arm
  shades his own body. A fighter drawn from the game's own polygons (no
  model: his body, weapon and cloth) takes them too: each such primitive
  is marked (`FLAG_WORLD`, gl_draw.c), the geometry shader puts its three
  corners back into the world from where the projection put them and
  their depths (`sun_screen_world`, the view the ground was drawn with,
  `port_GlSunNoteView`) for its face's normal, and the fragment its own
  pixel, read by the nearest map's four compared reads rather than its
  soft search (`sun_visible_quick`); the game's light of it, its colour,
  loses in the sun's shadow what a shadow takes from the floor (the
  `shadows` strength), before the texture as the colour is. The frame's
  lights light them too, in a program of their own (b, "The fighters'
  own polygons"). What the game still draws
  from its primitives otherwise (effects, the HUD) is as it drew it; with the ground the game's own
  tiles (`stage_mesh off`, or a stage the mesh is not measured good on),
  the picture falls back to `shadows cast`.
- **What a shadow does to the light.** The game lights a surface with its
  stage's three lights and its colour of the dark; the first light is the
  sun. Where the maps hide the sun, its own share (its turn on the normal
  through its column of the colour matrix) is taken away, and of the rest
  as much again as makes a shadow on the stage's sunlit floor take the
  `shadows` strength (68 hundredths, as `cast` took): the stage's own
  ratio of sun to sky decides how much that is (`port_SunGain`, at most
  four times the sun's share). It scales with the sun's own share, so a
  face the sun barely reaches loses little and one turned away from it
  loses nothing -- which is also what keeps a fighter lit by his own
  lights from a side the stage's sun is not on as the game lit him.
- **Soft.** Percentage-closer soft shadows: the casters about a point are
  searched for in its map, how far they are from it sets the penumbra (a
  sun of about two and a half degrees, `SUN_SOFT`), and sixteen compared
  reads of a turned disc average the edge. The disc's turn is the pixel's
  own and the same in every picture, so a still scene's penumbra is still.
- **Held still.** Each map keeps its size whatever the view (a whole
  number of units), its square moves over the ground by whole texels and
  its depths by whole steps of 256 units, so a still caster's shadow does
  not crawl as the camera breathes. The maps are drawn once a picture,
  before the first thing that reads them (the ground's mesh, or the
  fighters' models when they come first: `port_GlStageSunPrepare`), from
  the picture's own camera and casters.
- **Fixed on the way:** the ground's per-vertex normal was never enabled
  as an attribute (`port_GlStageFormat`), so every vertex read nought and
  `smooth_light` never reached the ground (it did reach the pieces' faces
  lit at each corner and the fighters). It is enabled now, for the sun's
  maps and `smooth_light` both.

**The cast shadow's jitter at rest** (ROADMAP 14): the three suspects --
the ground the shadow falls on, the in-between pictures' camera, the
darkening's bounding box -- are all gone with the mask: the shadow is
read in the world, from a map that stays where it is, by the surface
itself. Measured on the Kimigaura coast and the castle rooftop, nobody
pressing anything, each frame drawn as two pictures (`--gl-tween 2`), the
pixels that change by more than 12 of 255 from one picture to the next
in the lower two thirds of the picture: from a frame to the next, `map`
changes as many as `cast` (the fighters breathing: 1,200 to 7,200 a
step on the coast with either); inside a frame, `cast` changes 2 to 380
and `map` 1,100 to 2,600, all of them on the fighters' own lit sides,
where their self-shadow follows the breathing between the two frames.
In the difference of two pictures no pixel of the ground's shadows
changes at all (`tmp` runs, the coast at blank 3620).

`RB_SUN_DEBUG=1` in a run's environment draws, in the place of every
pixel the maps shade, the share of the sun that reaches it (white all of
it, black none): how a self-shadow or a penumbra was looked at.

**Stages under a roof.** A stage whose own ceiling stands between its
floor and the sun would have its whole floor in shadow, which is not the
game's lighting of it: those stages' own geometry does not cast
(`g_sun_stage_casts`, gl_sun.c), and their fighters still cast onto the
floor and onto each other. Found by every stage's picture with the maps
against `shadows cast` (the tour, blank 3,200): the lower half's mean
lightness fell to 0.40, 0.86, 0.70 and 0.32 of cast's on the main Tatara
shrine, the kabuki stage, the building's parking lot and the chanbara
hall, each a ceiling's shadow over the floor; held off, they come to
0.98 to 1.00. Everywhere else what darkens is a wall's, a pillar's or a
terrace's shadow and stays: the abandoned shipyard 0.60, team 1 0.73,
the terraced fields and the building rooftop 0.77, every other stage
0.86 to 1.00.

#### b. Lights with a place (`lights = on`)

`src/port/gl/gl_lights.c`, `gl_lights.h`. The list (`PortGlLight`, up to
128 a frame: a place in the world, a colour and intensity, a reach, a
point or a spot with its cone, a flicker, whether it should cast, an
`id`) is filled once a game frame by whoever knows the lights -- a
stage's file of them and a fight's events, the mods' side -- between
`port_GlLightsBegin` and `port_GlLightsEnd`, which may be called on the
step's thread: End hands the list over under a lock, and the picture
takes it at the next game frame's `begin` (`port_GlLightsTake`). It is
the picture's alone: outside the state, never saved, hashed or read by
the game.

- **Once a picture** (`port_GlLightsPrepare`, before the first thing that
  reads it): each light is moved between its place in the list before
  and this one by its `id` where the picture is between two frames, its
  flicker worked out from its `id` and the frames taken (three waves of
  prime periods, the same at any frame rate and in every run), and the
  lot sent to the card in a buffer.
- **Sorted by where they reach** (a grid of 64 by 64 cells seen from
  above, over the square that holds every light's reach, its cells 256
  world units or a whole number of times that): each cell
  names the lights whose reach overlaps it, so a pixel loops over the
  few lights near it, whatever the list holds. The grid is in the world,
  not the screen, because three different shaders read it with three
  different projections (the ground's, the fighters' layer, the cloths)
  and a stage is a floor: a cell is the same for every picture of a
  frame, and the processor's work is the lights times the cells
  (4,096), a few tens of microseconds for a full list. Until
  2026-11-19 the grid was 16,384 units a side about the camera, which in
  a fight stands some 7,000 units behind the fighters: a light more than
  a couple of thousand units beyond them was in no cell and lit nothing
  (a door 12,000 out on the main Tatara shrine, the street lamps, the
  building rooftop's stairwell). Fitted to the lights, every light is
  in it; where they are spread over a whole stage the cells grow and a
  pixel loops over more of them, which changes the cost, never the
  picture.
- **Lit at the pixel.** The ground, the pieces and the fighters' models
  add each light that reaches them (`lights_add`, `PORT_GL_LIGHTS_SHADER`)
  to what the stage's own lights give, in the game's own bytes of light:
  its colour times its intensity times 128, times its turn on the normal,
  falling off smoothly to nothing at its reach (the square of one less
  the square of the distance over the reach), a spot's cone smoothed
  between its two half-angles.
- **Shadows of the strongest few.** Of the lights that ask, the four
  that are brightest at the camera (intensity times reach squared over
  the distance squared) are given a cube of six depth maps of 512 texels
  (`PORT_GL_LIGHTS_SHADOWED`), drawn with the sun's casters: the stage
  and the fighters, at the picture's phase. A point reads the face its
  direction from the light falls in, four compared reads.
- **Made up, for measuring** (`--bench-stress lights=N`, the prover's
  `--gl-made-up-lights N`): where a frame's list is empty, N lights of
  the renderer's own in a ring about the place the camera looks at, each
  its own colour, a quarter of them spots, a quarter flickering, the
  first four casting.

**The fighters' own polygons** (2026-11-19). The owner plays with the
fighters the game draws itself -- body, weapon and cloth from its own
primitives, the models put aside -- and those had the sun's maps
(`FLAG_WORLD`, a) but no light of the list: a torch lit the floor about
a fighter and not the fighter. They are lit now as the meshes are, in a
program of their own:

- **The scene's twin.** `lights_add` in the scene's own program, per
  pixel or at each corner, cost every batch of the scene about 0.85 ms
  of the card at the bench's 95th centile with no light at all: the
  scene is the most fragments of the picture and its program the
  longest. So the scene's three shaders are compiled twice
  (`port_GlMakeLitProgram`, gl_draw.c): once as they were, and once as
  GLSL 4.3 with `PORT_LIGHTS_LIT` defined, which brings the lights'
  buffers, `lights_add` and the tonemap in. A fighter's own polygon
  that takes the world back (`FLAG_WORLD`, now with the sun's maps or
  the lights on) marks its batch `is_lit` where `lights` is on, a batch
  is all lit or none of it, and the lit batches are drawn by the twin.
  Nothing else pays for the loop; the twin's uniforms are its own
  (`PortGlSceneAt`, one for each program).
- **What it adds.** At the pixel, the place the geometry shader gave
  back for the face (from its depth and the ground's view) and the
  face's own normal: each light of the pixel's cell, its falloff, colour,
  cone, flicker and shadow (the cube maps already hold these fighters:
  the sun's casters are drawn into them, so a torch's light on a fighter
  is shadowed by the scenery and by the other fighter, and by his own
  arm). The game gave his polygons a light at each corner; the list has
  only the face's normal, so a light is let 0.4 of the way past where it
  turns away (`lights_add_wrapped`, `LIT_WRAP`), which keeps a body of
  few polygons from being lit facet by facet. The light added is rolled
  off above the game's own colour by the tonemap as the ground's is, the
  game's own share being the colour times its light over the whole.
- **What it leaves.** With `lights = off`, or no batch of a fighter's
  own, the picture is the one drawn before to the byte (the tour of the
  ferry dock at night, every picture); `Graphics: Original` marks
  nothing. A fighter casting a torch's shadow onto the ground and the
  walls was there before: the cube maps hold him.
- **What it costs.** Nothing the bench can see: `ra1-stress` (sixteen
  made-up lights, four fighters), this build and main's interleaved on
  a quiet machine, the card's median and 95% 3.58 / 4.08 ms against main's
  3.62 / 4.57, the scene's pass 0.06 to 0.07 ms in both; the gate's
  bench, `ra1`'s lighting 0.66 ms against its 2.0.

#### The fighters' material (rb_remade)

A remade model's file has, beside its base colour, a normal map and a
map of ambient occlusion, roughness and metal (`extras.rb_remade`;
`gl_fighter_file.h`). Where a piece has them the fighters' shader turns
its normal by the normal map at the pixel (the file has no tangents:
they are worked out from how the place and the texture point change
across the pixel, as glTF's own sample viewer does), lights that normal
with the stage's own lights and the light list, lets the occlusion darken
the colour of the dark (the light that comes from all about), and gives
the sun a highlight (GGX with Smith's masking and Schlick's Fresnel)
from its roughness and metal, where the sun's maps say it reaches. A
model without the maps is flat, unoccluded, rough (0.8) and not metal:
its highlight is a faint sheen.

#### What painted a pixel, and its depth

The passes of c and d run after the scene, so they have to know, at each
pixel of the finished picture, what is there. The picture is painted in
the console's order with the console's four mixes into an eight-bit
colour buffer that is all of video memory at the scale, and only the
ground's mesh and the fighters' layer have a depth. Two things are kept
for the passes, neither of them read by anything else:

- **The alpha says what painted the pixel last** (`gl_post.h`). Nothing
  read the colour buffer's alpha before (the resolve and the window
  took its colour alone, the read-back forces it opaque), so it is free:
  the ground's mesh writes 0.6 plus a quarter of its face's reflectance,
  the fighters' layer 0.95, and everything else -- the game's opaque
  primitives, the clears, the sky, video memory written -- 1, as it
  always did. A primitive that mixes with what is there keeps the larger
  of the two alphas (`glBlendEquationSeparate(..., GL_MAX)`: its texels
  that mix say 0 or a half), so smoke over the floor leaves the floor's
  code and an opaque texel of a mixed primitive covers it. The colours
  mix exactly as before: a picture with every setting of c and d off is
  the picture main drew, to the byte (the prover's off runs, all 38
  stages).
- **The depth of the ground and the fighters, one a pixel**
  (`PortGlPicture.ground_depth`). The depth buffer is a multisampled
  renderbuffer the next drawing clears, and reading it -- a blit of its
  samples down to one, or a shader's read of it as a texture -- held the
  card for half a millisecond to most of one at 2,560 by 1,440. So the
  mesh writes its depth again, in the world's units over eight, in half
  floats, into a third buffer beside its slots (only where a pass reads
  it), the slots' own take-down turns it into the world's units one a
  pixel, and the fighters' layer, when it is laid over the picture, puts
  its own depth into the same texture where it is nearer. Each drawing
  area of video memory keeps its own: the game draws one frame while it
  shows the last in another part of video memory, so the picture on show
  is read with the depth and the view (`port_GlPostNoteView`) of the
  drawing it is.

#### c. Reflections (`reflections = on`)

`src/port/gl/gl_post.c`. **Which surfaces**: the level faces (within
about 20 degrees) of the ground's mesh and its pieces on the stages whose
floor is polished, lacquered, glazed or wet, each stage's own strength
and roughness in one table (`g_post_floors`), the face's reflectance put
in its vertices when the mesh is built (`port_GlPostReflectance`):
the halls' and dojos' boards (the main Tatara shrine, the Meikyokan, the
Tatara shrine, the team, chanbara and practice halls, 40 to 50
hundredths), the kabuki stage's lacquered stage, the castle rooftop's
glazed tiles under the moon, the parking lot's sealed floor (35), Shainto
HQ's open deck and the water tower's painted deck, the ferry dock's and
the building rooftop's concrete at night (20 to 22). Earth, grass, sand,
gravel, rough stone and weathered planks do not reflect, and neither do
a hall's walls or a roof's slopes. The collision grid's `wet` class
(`SURFACE_WET`) was the hint looked at: it marks where a fighter splashes
(the stream's edge under the lost bridge), not a surface that shines, and
the water that does (the sea beyond the ferry dock and the coast) is no
level ground of the fight's, so the table is by stage.

**The floor's plane, mirrored** (2026-11-19, after the owner on the
Meikyokan: "they don't look quite right. They seem warped and not
detailed in a way that looks unrealistic"). Where the warping came from,
measured (`RB_POST_DEBUG=normal`): the reflection on the screen took each
pixel's normal from the depth about it, at a third of the size, and that
normal was three to ten degrees off the floor's own up on the
Meikyokan's boards, most near the camera, in a checkerboard by tile and
in bands across it. The tiles are the mesh's own doing: it pushes each
tile's depth back by a 256th of its farthest corner's, so that two tiles
in one plane sort as on the console, which makes the floor worked back
from the depth a patchwork of planes each a little off; the bands are the
depth's half floats (a 2048th of it). A ray turns twice what its normal
does, so the reflections bent into streaks; and a third of the size, a
ray of twelve steps jittered by the pixel and smoothed three by three
took the detail away. Neither the low size nor the march would have
mattered as much with the right normal; the normal was the warp.

What it does now: the stages' level floors are level, and the stage's
floor is one plane. While the mesh is built, the reflecting faces that
lie level to a thousandth give their heights, each with its area
(`port_GlPostFloorAdd`); the height with the most is the floor's plane.
Each picture, at half its size, the scene is mirrored in that plane
(screen-space planar reflection, by one pass of a compute shader):
each texel of the picture whose pixel has a depth of its own (the
ground's mesh and its pieces, the fighters' layer, by the alpha's code;
the fighters the game draws itself, below) and stands above the plane
is taken back into the world, mirrored, and put where the camera sees
the mirrored point; at each texel of the mirror the nearest that lands
there wins by an atomic least of one word -- its depth on a scale of
logarithms in the top 14 bits, how much of it is the mirror's in the
next 3, its colour in 5 bits a channel. A pass after reads the words
into colours and works out how far each mirrored point stands from the
floor where it is seen; a texel left empty with at least three of its
eight neighbours filled takes their mean (where the angle stretches what
is mirrored, the texels it lands on are a little apart). The same mirror
drawn as points with a depth test cost the card 0.5 ms more than this,
and as two compute passes (the nearest depth, then the nearest's colour
in a buffer of its own) 0.6 ms more. A
pixel of floor on that plane (within 6 units and a 250th of its depth)
then reads the mirror at its own place: what stands over it, exactly
where it is, as sharp as the picture at half its size, with nothing
walked or jittered, so it is as steady as the picture as the camera
moves. A pixel of floor off the plane (a step, a raised stage, a sloping
face within the 20 degrees) keeps the reflection on the screen, whose
ray now leaves along the world's up and not the worked-out normal.
Where the mirror has nothing (what the picture never showed: the sky,
the far distance, what is off the picture's edges, what has no depth)
the reflection on the screen fills in, the sky behind as before.

**The fighters the game draws itself.** With the HD fighters set aside
(the owner, 2026-11-19), the fighters are the game's own primitives,
which have no depth in the buffer and are not the fighters' layer: the
mirror had nothing of them, and a fighter left no reflection at all
(measured: the mirror of the Tatara shrine had the railings and the
walls, and no fighter). The shadows' casters already take their faces back into
the world each picture (`shadows` map or cast, the first the default;
"a. The sun's shadow maps"), so where the shadows are laid over the
picture they belong to, those faces are drawn once more, depth alone,
through the drawing's own view into a depth buffer of the passes' at
half the buffer's size (`port_GlPostFighterDepth`; one for each of the
two drawings, as the views are kept). A pixel the game's primitives
painted last is mirrored with that depth where it is nearer than the
ground's: the fighter's own colours, where he stands. With `shadows`
original or soft there are no casters, and no fighter in the mirror.

The floor is not glass. The mirror is blurred at a quarter of the
picture's size (nine reads in a disc, 1.5 plus 8 of the mirror's texels
times the floor's roughness wide), and each pixel takes
the blurred one the more the farther what it shows stands from the floor
(the distance from the mirrored point to where its ray meets the floor,
kept with the mirror, times the roughness over 400 units): a fighter's
feet are sharp in it and his head soft, as on a real floor. Its own
grain turns its normal a little (the slope of its brightness in the
picture, the mirror read up to a few pixels aside, more on a rougher
floor). How much shows: the reflectance times 0.6 and 0.4 of Schlick's
Fresnel for a varnish, so a floor seen at a glance shows more than one
seen from above. What is not mirrored: the top fifth of the picture
(where the HUD is, which a mixed primitive leaves with the ground's
code: its digits would be mirrored on the floor), coming in over the next
twentieth, and gradually from the picture's sides, so that where the
mirror ends is no hard edge across the floor. A stage of the wrong kind
for it -- the camera under the plane, no level reflecting face -- has the
reflection on the screen alone.

`RB_POST_DEBUG=normal` (how far the worked-out normal is from the
world's up), `=height` (the height above the plane of the ground and the
fighters), `=mirror` (the mirror) and `=cover` (how much of each pixel
the mirror covers, and each kind's share) are how it was looked at.
Pictures, before and after, of the Meikyokan, the Tatara shrine and the
ferry dock: the main checkout's `tmp/reflections2/`.

**What the mirror costs** (the bench, `ra1` on the Tatara shrine with
every setting of the lighting on, the game's own fighters as the owner
plays, the build before beside it, interleaved, loads of 4 to 13%):
`ssr` (the mirror, its filling and blur, and the reflection on the
screen) 0.47 to 0.50 ms against 0.20, `post` 0.38 to 0.42 against 0.28
(the lay reads the mirror), the fighters' depth 0.04; the lighting's
95% 1.74 to 1.85 ms against 1.34 to 1.54, under the 2.0 held. With the
archived HD fighters 1.89 against 1.65; the stress case 2.01 against
2.27 (a loaded run, 24%). The lay finds the plane from the third's depth,
not the buffer's own, and the ray on the screen is not walked where the
mirror covers the pixel.

**How the reflection on the screen goes** (the fallback): at a third of the picture's size, for each pixel of ground that
reflects, its point from the depth, the view reflected in the world's
up (it was the face's normal from the depth, the warp above), and a ray
walked across the picture in at most
twelve steps (one over the depth interpolated as the picture does,
jittered by the pixel), then four halvings where it passes behind
something no thicker than 40 units and 3% of its depth: what is there in
the picture is what the floor shows -- the fighters, the pillars, the
railings. Where the ray meets nothing, the sky the picture shows in the
reflected direction (the sky is the same seen from anywhere, so its
pixel at that direction's place on the screen is what the floor
reflects), or the last of the open distance it passed over; else
nothing, and the floor keeps its own colour. How much: the reflectance
times 0.6 and 0.4 of Schlick's Fresnel, as the mirror, less towards the
picture's edges and the HUD's band at its top, and towards the end of the
ray. Smoothed by the floor's roughness (three by three texels, spread up
to four, only between pixels of one depth), and laid over the ground's
own colour: `colour * (1 - a) + reflection * a`.

#### d. The finish (`tonemap`, `ambient_occlusion`, `bloom`)

**HDR and the tonemap (`tonemap = on`).** The plan was the colour buffer
in half floats with every clamp moved to a tonemap pass. It is not done
so, and on purpose: the colour buffer is all of video memory, which the
game's own primitives are mixed into with the console's four mixes, and
the chip clamps each of them (an added mix stops at white, a subtracted
one at black); a float buffer does not clamp a blend, so every effect,
fade and HUD mix would change, and `Graphics: Original` would no longer
be the console's picture. So the lighting is worked out unbounded in the
two shaders that light (the ground's mesh and the fighters' models), and
the tonemap is the last thing each does (`PORT_GL_TONE_SHADER`):
`tone_map(base, total)` takes the colour as the game's own lights make it
and with the frame's lights added. Where nothing is added it is the
game's colour clamped, to the byte, so a stage is its own picture with
its lights off and its mood does not move; what the added light takes
above a knee -- the stage's own colour there, or three quarters of white,
the higher -- rolls off towards white instead of clipping flat, so a
torch's pool or a clash's flash on a fighter keeps its hue and its
gradient. The energy above white is the lights' own, and that is what
the bloom draws from (below).

**Ambient occlusion (`ambient_occlusion = on`).** At half the size, in
the same pass as the reflections: six points about each pixel of ground
in a disc of 110 world units (a turned spiral, the pixel's own turn),
each counting by how far it stands above the pixel's face over its
distance (McGuire's alchemy AO, the cheapest of the kind that holds its
size in the world), fading out from 8,000 to 16,000 units away and
nothing where the disc is under three pixels; smoothed with the
reflections. It darkens only the ground's own pixels, and of them only
the share of the stage's light that is not the sun's: the colour of the
dark against the sun's turn on a level floor, from the stage's own light
setup (`port_GlPostAmbient`, at least 0.35), so a sunlit day stage takes
a little and a night stage lit from all about takes more. A fighter is
not darkened by it (he has his own maps where a remade model has them),
and nothing of the HUD or the game's primitives is.

**Bloom (`bloom = on`).** From the lights, not from the picture: the
picture is eight bits and a white wall or a white line is as bright as a
lamp in it (the light files' own finding), so bright pixels are no sign
of light. Each light of the drawing on show (`port_GlLightsCard`: the
lights as they were sent to the card when it was drawn, already moved
between frames and flickered, kept with its view and sent again) is a
soft glow at a quarter of the size where it is: a bright middle and
a wide fall, its size a share of the light's reach and more for a
brighter light, its colour its own, gone where the depth about it says
the scene stands in front -- the whole glow where all about its middle
is nearer, and each pixel of it where what that pixel shows is nearer
than the light by more than half the glow's size: the owner
(2026-11-19) saw a torch behind a fighter glow over his hands, the halo
reaching past his edge laid over him whole. Spread wider at an eighth and a sixteenth and
brought back up added, and added over the picture. So a torch, a lamp of
the parking lot and the flash where blades meet glow, and nothing else
does; with `lights = off` there is no bloom. The depth is the ground's
mesh and the fighters drawn from models: a fighter of the game's own
primitives is not in it and does not hide a glow.

**Only over the ground it was drawn with.** The passes keep the views
of the ground's last two drawings, one for each half of video memory
the game draws into in turn, each with the lights it was lit by. A game
frame that draws no ground's mesh -- a menu, the stage select, the
pause holding the fight's last frame -- forgets the view of the half it
painted (`port_GlPostNewFrame`, the older of the two first): before,
the views stayed, and the passes ran over the stage select with the
glow of the torches of the stage just left (the owner, 2026-11-19), and
over the pause with each glow where the wider picture had it.

**Where it is drawn.** The passes run in `port_GlFinishFrame`, after the
shadows' darkening, over the picture the resolve averaged down (with one
sample a pixel the resolve now runs anyway when a pass is on, so the
picture has a buffer of its own); the last pass lays the lot over that
picture into a buffer of the picture's size, which then becomes the
picture, the old one the next picture's to lay into (no copy; laying it
in place behind a texture barrier held Mesa's software renderer for the
whole of its drawing). The window shows it and the provers read it.
`RB_POST_DEBUG=ao`, `=reflect` or `=depth` in a run's environment draws
the occlusion, the reflections alone, or the depth and the reflectance in
the place of the picture.

**Classic stages** keep all four: they are the renderer's, not the
stage's dressing (`port_StageIsDressed()`); on a Classic stage the stage
lights are off, so the bloom has only the fight's flashes.

#### What it costs (the bench, the RTX 3090 at 2,560 by 1,440)

`tools/port_bench.py`, other agents' work on the machine (loads of 10 to
46%: the numbers are a guide, the medians the steadiest), at 3126201d9:

| case | card, median / 95% | the stage's pass | sun | lights | the lighting's 95% |
|---|---|---|---|---|---|
| ra1, main before (c212be752, quiet) | -- / 3.30 | 1.40 | -- | -- | -- |
| ra1 | 3.61 / 4.32 | 1.53 | 0.32 | -- | 0.71 |
| ra1-stress, 16 made-up lights, 4 casting | 5.73 / 8.55 (46% load) | 2.42 | 0.28 | 0.54 | 3.47 |
| ra1-stress, the lights off | 5.44 / 6.17 | 3.07 (the soft filter on every map, before the cut) | 0.29 | -- | 0.32 |

What brought it there: the sun's soft filter is the near map's alone,
the far two read four compared texels and one; a light's shadow is one
compared read (the card's own two by two); the stage's depth from a
shadowed light is kept while the light stands still, and only the
fighters are drawn over a copy of it. The stress case's card 95%
still goes over the refresh on a loaded machine; its median is under
it. On ra0 the processor's 95% moved between 5.91 and 9.07 ms from one run to the next with everything of the lighting off or on, interleaved (the step on the window's thread, a busy machine): the gate's ra0 and ra2 processor limits failed on that noise, not on the lighting. Left to take: the sun's map of the stage kept the same way (it
moves only with the camera's whole texels), and the stage's shading
done once a pixel where the ground is drawn twice over (the stress
case's own doubling, which no stage has).

**c and d** (2026-11-19, a quiet machine, `ra1`, one picture a blank;
main's own build measured beside it with `--program`, interleaved):

| build | card, median / 95% | stage | ssr | ao | bloom | post | flay |
|---|---|---|---|---|---|---|---|
| main (97474f2d1) | 3.63 / 4.76 | 1.53 | -- | -- | -- | -- | 0.10 |
| this, c and d off | 3.83 / 4.44 | 1.75 | -- | -- | -- | -- | 0.10 |
| this, c and d on | 4.61 / 6.05 | 1.81 | 0.20 | 0.10 | 0.00 | 0.28 | 0.19 |
| ra1-stress, main | 5.95 / 7.06, 5.61 / 7.04 | 2.54, 2.39 | -- | -- | -- | -- | 0.10 |
| ra1-stress, this | 7.17 / 8.22, 6.62 / 7.68 | 3.03, 2.77 | 0.17 | 0.08 | 0.00 | 0.25 | 0.19 |

The gate's bench (`--budget`) on the last commit, a quiet machine: the
card's 95% 5.78, 6.43 and 5.06 ms at `run_ahead` 0, 1 and 2, the
lighting's 1.49 against 2.0; the stress case's card 95% 7.76, over the
refresh, as main's own is (7.04 and 7.06 above, its met limit failing on
main as it stands).

`ssr` is the one pass of a third of the size that works out the
reflections and the occlusion together, `ao` the smoothing of both,
`post` the depth at a third of the size and the lot laid over the
picture, `flay` the fighters' layer laid and its depth added to the
ground's; the mesh's own depth beside its slots is in `stage` (0.06).
The bloom is nothing in `ra1`, which has no lights; it is a draw of a
quad a light and three passes of a sixteenth to a quarter of the size.
The lighting's 95% in all was 1.44 ms against the 2.0 held. What it
took to get there, from 2.6 ms of `post` alone: the multisampled depth
buffer is never read (a blit of it to one sample cost 0.9 ms, a shader's
read of it as a texture 0.5), the mesh's depth buffer beside its slots
is not cleared (where the mesh drew nothing its slot says so), the
passes run at a third of the picture's size and not at half, the
reflections and the occlusion are one pass and their smoothing one, and
the last pass's buffer and the picture's change places instead of a
copy. Left: the stress case's card (the ground drawn twice doubles the
mesh's share), and
`stage` is 0.2 ms above main's with all four off; nothing of c and d
runs then but the tonemap's switch in the shader and the alpha code.

#### What is left of it

- **The floors' table** (`g_post_floors`) is by stage and by what
  lies level; a light file could say it a texture at a time (a stone
  path through grass), which no stage needed yet. The owner's eye on
  each stage's strength is wanted.
- **What the screen does not show** is not reflected (the mirror has only
  what the picture shows: a wall behind the camera, the far side of a
  fighter, what is above the HUD's band are never seen in the floor); a
  true mirrored drawing of the stage and the fighters from below the
  plane would have them, at the price of drawing the ground's mesh again
  (the stage's pass is 1.5 to 2 ms of the card) and the fighters, which
  is over the lighting's budget; a probe of the stage would be the
  cheaper next step, if a stage asks.
- **One plane a stage.** A floor at another height (a raised stage, a
  step) has the reflection on the screen; a second plane would be a
  second pair of passes.
- **The fighters drawn from the game's own primitives** (no model of
  their own) have no depth in the buffer: the mirror has them from the
  shadows' casters (above), but the reflection on the screen does not
  meet them and the occlusion does not see them. An effect painted over
  a fighter is mirrored at the fighter's depth.
- **The light files' placing and strength** are the lights' own side
  (`gl_light_list.c`, the mods' `lights/`): of the stages measured here
  only the parking lot's ceiling spots reach the fight (107,172 pixels
  brighter by more than 8 at blank 3,200); the flames of the Tatara
  shrine, the kabuki stage, the purple cavern and the cemetery are
  thousands of units from where the tour fights.
- **The normal map's green** is OpenGL's convention as glTF says; with
  the world's y running down, the sign of the bitangent the shader works
  out is not yet checked on a remade model in a fight.

## The stage select's kinds of stage

ROADMAP 16 (the owner, 2026-11-18): three kinds of stage at the select
screen. **Classic** is the disc's stage with only what the engine and the
renderer improve; **Remastered Classic** is the same stage and the same
collision, dressed; a **custom arena** is a mod's stage fought in place
of its host (MODS.md, "Custom arenas").

**What the kind switches.** One function, `port_StageIsDressed()`
(`include/port_stage_choice.h`), is the question; each feature of the
dressing asks it where it decides:

| Feature | Classic | Remastered Classic | Where it asks |
|---|---|---|---|
| The stage's own lights (torches, fires, lanterns: the lights files, MODS.md "Lights") | off | as `stage_lights` says | `port_LightsOfStage` (`src/port/lights.c`) |
| The stages' pages made larger (`stages-hd`) and any mod's page pictures of the stage (a fighter's are his on either: MODS.md, "A fighter's pages") | the game's own textures | the mod's | `port_GlSceneFind` (`src/port/gl/gl_scene.c`) |
| The night sky | the game's colour of the distance | as `sky` says | the sky's drawing (`src/port/gl/gl_sky.c`) |
| The dressing (grass, bushes, water, particles, meshes: ROADMAP 17, "The dressing") | off | as `dressing` says | `port_DressingOfStage` (`src/port/dressing.c`) |
| The time of day ("The time of day") | the game's own | as chosen | `port_StageTime` (`src/port/stage_choice.c`) |

Everything else is the engine's and the renderer's and is the same in
both: the resolution and the wide picture, the draw distance, the frame
rate and the pictures between frames, the ground as one mesh and the
pieces as meshes, the walls seen through, the fighters' models, the
shadows, the smooth light, the effects' flashes as lights
(`effect_lights`), and the new lighting of ROADMAP 14 as it lands. F2 to
the Original picture shows the console's picture and the console's
light whatever the kind. An arena counts as dressed: it brings its own
lights (`lights =`) and nothing of the host's is shown.

**Where the choice lives.** `src/port/stage_choice.c` is a state file
(`config/port_state.txt`): `g_port_stage_choice` holds each stage's kind
as the switch left it, the fight's kind, the arena in play, the arena the
stage in memory was loaded with, and the arenas' page with its cursor.
It is saved, put back and hashed with the game's own variables, so a
rollback puts the choice back with the stage the game keeps
(`g_game_settings.stage_index`, `g_fight_config.stage`), and two players
of a session, whose presses are the same on both machines, choose the
same thing on both. What the run gives it from outside -- `--arena`, and
the window's `stage_kind` -- is kept in `src/port/arenas.c`, outside the
state, and taken in by the game's step when it next asks: a change of
the setting reaches the state at a frame of the game, and nothing a
rollback or run-ahead loads can put it back.

The game's hooks, all under `RB_PORT`:

- `select_StageStep` (`select.c`), before the grid reads its presses:
  `port_StagePageStep` with the stage under the cursor. Square switches
  that stage's kind; Select opens the arenas' page, which then has every
  press: six arenas to a page in three rows of two, as a stage page's
  places (an arena's place `row * 2 + column`), up and down a row round
  the page's three, left and right the other column and, past the page's
  side, the next page or the one before (as the grid's arrows go to the
  next map), L1 and R1 a page back or on (round from the last to the
  first), Cross to fight on the arena (the grid's stage index set to its
  host, the page confirmed as the grid's own Cross does), Triangle or
  Select back; each with the game's own sound (`snd_Play`, the cursor's,
  the confirm's, the cancel's). While the page is up
  (`port_StagePageCovers`) the stage page leaves out what the window
  draws the arenas in place of: the map (`SELECT_STAGE_SHEET_UNDER`), its
  marks, the stages' names and the map's number with the arrows
  (`SELECT_STAGE_SHEET_REST`); its title, frame, scroll and the scroll's
  picture are drawn as ever. The grid's own Cross calls
  `port_StageChosen`: the fight's kind is that stage's, and the arena is
  the run's own (`--arena`, or none).
- `select_Update`: off the stage pages, `port_StageLeftPages`.
- `select_Load`: `port_StageChoiceReset`, the fight's kind the default and
  the arena the run's own whenever a mode's select screen opens, so that
  a story's cutscene never loads its stage with an arena versus mode
  chose.
- `fight_LoadStage` (`fight.c`): `port_StageChoiceAtLoad` first. The modes
  that have no stage page (story, chanbara, the group battle, and versus
  the CPU and training when `stage_select_everywhere` did not send them
  through the pages: "Every stage in every mode, and every unlock") take the default kind and the run's arena there; and the
  stage is loaded again when the arena in play is not the one the stage
  in memory was loaded with, though the stage number is the same (an
  arena, then its host from the disc). `port_StageChoiceLoaded` after
  `stage_Load`.

**The arena's ground can change between two fights.** The disc reader
asks `port_ArenaGroundSector` (`src/port/arenas.c`) first for every
sector (`port_OverrideDiscSector`, `src/port/discfiles.c`): with an arena
in play, a sector of its host's ground is the arena's ground (zeros past
its end), and any other sector, or with none in play every sector, is
the disc's or a stage mod's. Each arena's room on the disc is found as
the program starts (`port_FindArenaRoom`); an arena too long for its host
is left out with a line in the log (and ends the run if `--arena` asked
for it). The start table and the CPU's obstacles the game is handed are
made again when the arena in play changes.

**What is drawn, and where.** Nothing of it is in the game's video
memory: the window lays it over the picture, at its own resolution and
in the port's typeface (`src/port/window/stage_page.c`, from
`port_DrawCounter`), the same over the remastered picture and over the
console's. On a stage page a panel under the scroll, laid in the game's
4:3 area so that it keeps its place at any shape of window, has a column
for each choice of the page, its value over the button that changes it
(a dark key with the button's name, or the pad's own symbol drawn in its
colour): **Kind** ("Remastered" in gold, "Classic" in white; Square),
**Time** (L1, R1), **Music** (L2, R2) and, with arenas, **Arenas**
(Select); one size of letters for the values, one for the buttons'
words, a value too long for its column made smaller. The arenas' page
is a page of the stage select's own (the owner, 2026-11-20: not "a
window drawn on top of the emulation"): over the game's page, which
has left its map and names out, the picture of the arena under the
cursor fills the map's frame (the game's window for it, 352 by 74 of
its places; the PNG its file carries shown filling it, its middle kept,
or a card of its name), the page's arenas are on the scroll where the
stages' names are and in their lettering (the port's typeface, bold, a
dark ink; the cursor's white with a dark edge; a name too long for its
place made smaller), `( Arenas )` where the map's number is (`( Arenas
2 )` with more than one page, and the scroll's arrows), and the panel
under the scroll has a column each for the arena chosen (Cross, "Fight
here"), the pages (L1, R1) and the way back (Triangle or Select). It
reads the state and the arenas only, and is drawn again only when what
it shows changes. With the page down the game's video memory is the
console's, so the play tests that hold the port's video memory to the
emulator's (`port_picture`'s `select_stage`) see the console's screen,
and the headless port has the whole choice without any of the drawing.

**Netplay.** The presses on the stage pages are both players', as the
grid's are, so the kind switched, the page and the arena chosen are the
same on both machines. The kind the stages start as is the host's: the
welcome carries it (`MEET_KIND`, since protocol 3) and both hold it for the
session (`port_HoldStageKindDefault`: the settings menu's change does
nothing until the program starts again). The set-up's hash of the
gameplay mods covers every arena the page offers, in its order and by
its file's bytes and key, and the one `--arena` asked for
(`port_ArenaSessionHash`): a guest without the host's arenas is turned
away, told that it is the arenas (`MEET_ARENAS`, `REFUSE_ARENAS`,
protocol 17; NETPLAY.md, section 13), and a session can fight on one
chosen on the page (`make port-net`).

**How it is proved.** `python3 tools/port_stage_select.py` (in WSL, after
`make port`; a step of `tools/gate.py`): a plan presses through versus
mode's select screen on both sizes of the port with the sample arena as
a mod: Square twice on one stage (Classic, then back) and once on
another, Select to the arenas' page, Cross on the courtyard; a fight on
stage 25 with the arena's starts; the pause menu back to the select
screen and Abandoned Station from the grid, which must be loaded again
from the disc with its own starts. Each checkpoint's values are held to
what the presses chose, and the two sizes print the same trace.
The same tool drives versus the CPU and training through the stage pages
("Every stage in every mode, and every unlock"), and each of them
through the arenas' page to a fight on the courtyard (`vscpu_arena`,
`practice_arena`); and seven arenas (`many_arenas`), more than a page:
the d-pad, the page's sides, L1 and R1, the seventh fought.
`--shots FOLDER` keeps the console's pictures of each step, and
`--window FOLDER` the window's (the Linux window's program offscreen, on
software OpenGL), remastered and original, at the stage page and the
arenas' page, each of the page held to differ from the stage page in
the map's frame and on the scroll. `tools/port_replay.py` plays back the
fight on the courtyard as it was recorded, and `make port-net` holds a
session's match on it, chosen on the page by the host's presses, to the
same state hashes on both machines and both machines' replays.
`tests/port/arenas.c` holds the file's picture, the ground read in place
of the host's, and the arena in play changed to another, to none and
back. With no arena and the kind as built in, every play test is as it
was.

## The time of day

ROADMAP 20 (the owner, 2026-11-19: the sun must not be every stage's
only light). Every one of the 38 stages is set at night on the disc
(`light_SetStage`'s table; "The sky"). A dressed stage -- Remastered
Classic, or an arena -- can be fought at **the stage's own** time (the
game's lighting, exactly as before), **dawn**, **day**, **dusk** or
**night**; Classic and the Original picture (F2) keep the game's own.

**The choice** is the game's state, beside the kind
(`include/port_stage_choice.h`, `src/port/stage_choice.c`): each stage's
time as L1 and R1 left it on a stage page (`times[38]`), the fight's
(`fight_time`, taken as the grid's Cross takes the kind; an arena takes
the default), and the one under the cursor for the strip
(`cursor_time`). `port_StageTime()` is the picture's question: the
fight's time, or the stage's own on a stage that is not dressed. The
window's `time_of_day` (`stage`, `dawn`, `day`, `dusk`, `night`;
`settings_list.h`) and `--time-of-day` are what every stage starts at and
what the modes with no stage page fight at, kept outside the state in
`src/port/arenas.c` as the kind's default is. Online the welcome carries
the host's (`MEET_TIME`, protocol 6) and both hold it for the session;
the presses on the pages are both players', so a stage's time chosen on
the page is the same on both machines and rolled back with the stage.
The panel under the stage pages' scroll says the time of the stage
under the cursor (its Time column: "Dusk", gold) over L1 and R1; on a
Classic stage, "The game's light".

**The look** (`src/port/gl/gl_time.c`, `gl_time.h`) is worked out once
for the stage and the time from the stage's own scenery setup
(`LIGHT_SCENERY`), and every reader of the stage's light in the picture
asks `port_GlTimeTileLights()` instead of the game's setup: the ground's
mesh and its pieces (the light matrix and colours `smooth_light` uses;
a face lit as the game lit it -- `smooth_light off`, or a piece's own
colours -- takes the time's light on a floor over the stage's own,
`u_time_floor`, so the mesh is built with the game's light whatever the
time), the sun's maps and their
strength (`port_SunGain`), the sky's moon and ambient, and the
occlusion's share of the light. At the stage's own time it is the game's
setup itself, every multiplier 1 and the fog's amount 0, so nothing of
the picture changes (the provers, the bench, the play tests).

| | dawn | day | dusk | night |
|---|---|---|---|---|
| the sun's height | 14 degrees | 55 | 12 | 35 (the moon) |
| its bearing | the stage's own turned back 50 degrees | the stage's own | turned on 55 | turned on 25 |
| its colour | pale gold | white | deep orange | weak, cool blue |
| the floor's light, of the stage's own | 0.80 | 1.15 | 0.72 | 0.40 |
| the sun's share of it | 0.42 | 0.65 | 0.42 | 0.55 |
| the fog's colour, its most | lilac grey, 0.70 | pale haze, 0.50 | dusky rose, 0.75 | near black blue, 0.85 |
| the sky | gold horizon to blue, a few stars | a blue sky, clouds | orange horizon to violet, stars | the night sky, stars, the moon |
| the sun's penumbra | 1.3 times | 1 | 1.3 | 2.5 |
| the bloom | 1.0 | 0.6 | 1.25 | 1.6 |
| the stage's own lights | 0.7 | 0.4 | 1.2 | 1.6 |

- **The sun.** Its direction is the time's: height and a bearing turned
  from the stage's own sun's, so a stage keeps its sense of where the
  light comes from, and the shadows run long and low at dawn and dusk.
  The sun's maps are drawn along it (`port_SunDirection`): the cascades
  and the whole stage's fourth map are fitted to it each picture as
  before. Its colour and the ambient are the time's hues, scaled so the
  stage's own lit floor (clamped to 60 to 230) keeps its place in the
  time's range of light, the sun's light where it meets a face at most
  `sun_most`; the ambient keeps a quarter of the stage's own hue (the
  cemetery stays a little purple). That scaling is the exposure: the
  picture is eight bits and the lighting a byte scale, so a darker time
  is less light, not a camera's stop.
- **Night** is its own: the moon a weak, cool key (at most 115 of 255
  where it meets a face), its penumbra two and a half times the sun's;
  the ambient dark and blue, so what lies between the lights is dark; the
  stage's own lights (the lights files, the game's flames) 1.6 times as
  bright and glowing 1.6 times as much; the sky the night sky with the
  time's moon. The shadows of the stage's own lights are the scene's then
  (the four strongest cast cube shadows, "b. Lights with a place").
- **The stage's own lights at a time** (`port_GlTimeLightGain`,
  `gl_light_list.c`): a light file's `times =` (MODS.md, "Lights") leaves
  a light out at the times it does not name; the stage's own time counts
  as night (every stage of the disc is night), so a light that says
  `times = night` is lit as it always was. The others are scaled as the
  table says.
- **The fog.** The game's fog is its colour tables copied over each
  other towards the colour of the distance (black on every stage). At a
  time of the port's own the ground's mesh reads the nearest level's
  colours, unfogged, and lays the time's fog over the lit pixel by how far
  into the levels its depth is (`u_time_fog`), on every face of the mesh;
  so the ground runs into the time's horizon.
- **The sky** (`gl_sky.c`): dawn, day and dusk have a sky of their own --
  the time's horizon (the fog's colour, so the ground runs into it)
  rising to its zenith, the sun's disc and glow where its light comes
  from, colouring the horizon on its side, the baked clouds lit by it, a
  few stars at dawn and dusk; night is the night sky with the time's
  horizon and moon. A mod's picture of a sky is the stage's own time's.
  A time of the port's own has its sky whatever `sky` says (a day over a
  black fill is no day); a closed stage has none.
- **Under a roof** (the halls, the dojos, the cave, the garage: the sky's
  closed stages) the sun keeps the stage's own way, the colours move half
  way to the time's and there is no time's fog: light through the doors.
- **The fighters.** The game's own polygons of a fighter (`FLAG_WORLD`,
  now also set when a time is on without the sun's maps) are relit at
  their face's normal by how much the time's light differs from the
  stage's own there, each channel, at most twice (`time_relight`,
  `PORT_GL_TIME_SHADER`); a fighter's model takes the time's light on a
  floor over the stage's own in its colours (its light's way is the
  game's). The stage's own polygons the game lit (props, pieces the mesh
  does not hold) take that floor ratio (`FLAG_TIME`), and a face the game
  drew unlit (a stone, a sign) three quarters of it, on the mesh and off,
  so it is not bright at night while a lamp so drawn still shows.
  Effects and the HUD keep their colours.
- **The bloom** is the lights' glow times the table's (`gl_post.c`).

**What is left, and wants the owner's eye.** Every number of the table
is a judgement on the pictures (`tmp/timeofday/` of the work, not
committed): the hues, how dark night is, the bearing turns. The HUD's
white letters are the game's half-transparent ones and read less well
over a day sky. The surf's sparkles and other effects keep the game's
colours (bluish at day). A fighter's model's light keeps the game's way.
No disc stage is day in the original, so "the stage's own" is night on
all 38; an arena's own light is its file's.

**How it is proved.** `tools/port_stage_select.py`'s versus plan presses
L1 on the first place (its own to night) and R1 on Abandoned Station,
and holds the cursor's and the fight's time (`port_cursor_time`,
`port_time` in `tests/play/state.toml`) on both sizes, the same trace.
Pictures of six stages at the five times (rebushido-gl,
`--time-of-day`, `--smooth-light on` as the window has it). At the
stage's own time `port_gl_prove.py --gpu card` passes (the Original
picture the console's, to the pixel) and `port_stage_prove.py --gpu card
--against main` has every stage's trace the same both ways and the
pictures main's stored ones (2026-11-19).

**What it costs** (the bench, the RTX 3090 at 2,560 by 1,440, a machine
loaded 36 to 46% by other agents' emulators, so a guide): `--budget` at
the stage's own time, ra1's card 95% 4.50 ms and the lighting's 0.76
against 2.0, the card under the refresh in every case; `--case ra1 --set
time_of_day=night`, the card's 95% 4.99 ms and the lighting's 0.35. What
a time adds to a frame is a few uniforms, one more read of the fog's
levels in the mesh's shader and the sky's branch: nothing measurable
above the load's noise. The processor's side is unchanged (the look is
worked out when the stage or the time changes).

## The dressing

ROADMAP 17 (the owner, 2026-11-21: "go ahead"): a Remastered Classic
stage dressed with what nobody touches -- foliage drawn by instancing and
stirred by the wind, the fighters and the slashes; water; dust, leaves,
embers, snow and petals; scenery meshes -- built on item 14's lights and
item 20's time of day. The files are a mod kind of their own
([`MODS.md`](MODS.md), "Dressing"); the program ships all 38 stages' of them
(`src/port/mods/dressing/`). The window's `dressing` (on) turns it on and
off; the prover's `--dressing` is off unless said, so every prover's
picture is the stage as it was.

**Where.** `src/port/dressing.c` (`include/port_dressing.h`) reads and
layers a stage's files, as `lights.c` the light files, and answers
nothing on a Classic stage (`port_StageIsDressed`) or with an arena in
play. `src/port/gl/gl_dressing.c` (`gl_dressing.h`) makes the picture of
it. Both are outside the game's state (`config/port_state.txt`;
`gl/` is the picture's): nothing of either is saved, put back or hashed,
and nothing the game computes reads them.

**Grown once a stage.** When the ground's mesh is built
(`port_GlStageBuild`), its triangles that look up are kept, sorted into
squares of 512 units, with a copy of video memory and the stage's
textures as they were loaded (`port_GlDressingNoteGround`), where the
stage is to be dressed (a stage with nothing to grow pays nothing in
the fight's first frames, where the mesh is built; the dressing turned
on in the menu grows with the next stage loaded). The first
picture that draws the dressing grows it: every place a block asks for,
by a sequence seeded from the stage and the block's name (so a stage
grows the same every time), is looked up on the ground under it -- the
highest face that looks up, its height, how level it lies, the texel its
texture has there -- and kept where the block's rules say ([`MODS.md`]
(MODS.md), "Dressing"), coloured by that texel. The things go to the card
once: 36 bytes a thing of foliage, 28 a vertex of the meshes and the
water. Measured: a stage of 27,000 tufts grows in about 20 ms.

**Drawn in the ground's place.** The dressing is drawn by the mesh's own
draw (`port_GlStageDrawMesh`), with its projection, its depth and its
fog handed over whole (`PortGlDressingView`): so it is in every picture
the ground is in, the pictures between two frames too, with the camera
between the two. Two passes:

- **What stands** -- the foliage and the meshes -- right after the
  ground, while the ground's slots are bound (`port_GlGroundSlotsBegin`):
  it writes the colour, the depth (in the world's units, as the mesh's)
  and the ordering-table slot of its own depth. So a fighter drawn from
  the game's own polygons, who is held against the ground by the slots
  (`port_GlIsOverGround`), is hidden by a blade of grass in front of his
  feet and not by one behind them; a stage's piece the game still draws
  is held by the depth; the reflections and the occlusion see a
  thing's depth as they see the ground's. Its alpha is 1: not the
  ground's, not a reflecting floor (gl_post.h).
- **What mixes** -- the water and the particles -- after the slots, with
  the depth tested and not written, the alpha left as the ground wrote
  it. The water is mixed over the stage's own water (its own colour where
  one looks down into it, the sky where one looks along it, by Fresnel);
  the particles that glow (embers, fireflies) are added.

**The foliage.** One instanced draw a block: a thing's vertices are built
in the vertex shader from the vertex's number -- a tuft of grass seven
blades of two bends and a tip (105 vertices), reeds three blades of three
bends with a head, flowers three blades and a bloom of two crossed quads,
a bush 36 leaves as cards over half an egg (the leaf's shape cut in the
fragment) -- each blade's place, turn, height and lean from a hash of the
thing's seed. A blade bends keeping its length: its top moved by the
lean, the wind and what pushes it, its rise what is left. Farther than
5,250 units from the camera a thing is drawn less often and with fewer
blades (`FOLIAGE_THIN`: five blades at 6,000, three past 10,500, and of
the things fewer the farther, down to a seventh): it is a few pixels
there and most of the vertices.

- **The wind** is the stage's file's (`[wind]`): its way, strength and
  gusts; each thing sways in three waves that run along the wind's way
  over the ground, a gust that crosses the stage now and then, and a
  flutter across it, all of the picture's clock (the frames taken and the
  phase between two, as the lights' flicker: the same at any frame rate).
- **What pushes**: each fighter, an upright cylinder about his bones
  (gl_see.h, `port_GlSeeFightersAlways`), pushes what is within half of
  it at his feet away from his axis, and where he was 8 and 16 frames
  before pushes too, less, so the grass rises again behind him; and a
  weapon whose hand moves more than 45 units in a frame near the ground
  is a slash (port_fighter.h, the weapons drawn), sweeping a 900-unit
  circle ahead of the hand its way. Sixteen at most, read once a game
  frame (`port_GlDressingFrame`).
- **The light** is the ground's own: the stage's three lights (or the time
  of day's setup, `port_GlTimeTileLights`), less the sun where its maps
  hide it (`sun_visible`, `sun_shade`), and the frame's lights
  (`lights_add`), at a normal mostly the ground's up so that a meadow is
  lit as the ground under it, darker towards the roots; then the
  tonemap and the fog of the ground's levels (or the time's).

**The water** is a level quad of each rectangle, or the ground's own faces
of the water's colour lifted a little. Its normal is four waves along
and across the wind (the size `waves` says) and the rings where a fighter
wades: a fighter whose feet are in the water's rectangle at its height
starts a ring every 9 frames while he moves, every 50 at rest, eight at
most at once. What it reflects is the sky's gradient of the time of day
(the night's at the stage's own time), the sun's or the moon's glint
where its maps let it reach, and the frame's lights; its fog is half the
ground's, so that a sea far off still shows its waves.

**The particles** are worked out from their number and the clock alone
where they fall or rise over an area (leaves, snow and petals falling
from `top`, drifting with the wind and turning; fireflies hovering and
blinking; embers rising from the game's own flames, `port_LightsGame`),
so they cost the processor nothing; a step's dust is a ring of 512
events on the processor, each its place, way, time and colour (the
ground's under the fighter), sent each picture.

**The meshes** are the mods' OBJ files, turned, scaled and placed when the
stage grows: at a place, on the ground under it (`sit`), or in a piece's
place, fitted into the piece's box (and refused where the proportions
differ by more than a quarter), the piece then left out of the mesh's
draw and its sun casters (`port_GlDressingHiddenPieces`). A mesh casts
into the sun's maps and the strongest lights' cubes as the stage does.

**What it changes of the rest.** With no dressing file for the stage, or
`dressing off`, or a Classic stage, nothing is drawn and nothing of the
picture differs: the provers (`port_gl_prove.py`, `port_stage_prove.py`,
`port_fps_prove.py`, `--against main`) run with it off and hold every
stage's picture to main's. The card's own timer has a pass of its own,
`dress` (gl_bench.h), inside `stage`.

**The game's state.** `python tools/port_dressing.py hashes` runs a fight
of the CPU on stages 8, 11 and 31, on the window's program with the
dressing on and then off: the game's state at every blank (`--trace`) is
the same line for line, and each fight's replay made with it on, played
with it off (`--replay-check`), has every hash (each 30 frames) the one
recorded. Online, `tools/port_net.py session` and the rollback test
(`make port-rollback`) pass as before: the headless programs they run
read the dressing's files but draw nothing, and the window's programs of
`port_net.py window` draw it.

**What it costs** (the bench, `tools/port_bench.py`, the RTX 3090 at 2,560
by 1,440; `ra1-dressed` is `ra1` on the Meikyokan's lawn with the
shipped dressing, its 27,000 tufts, flowers and fireflies; `-stress` four
fighters):

| stage, kind of dressing | card, median / 95% | `dress`, 95% |
|---|---|---|
| 8 Outside Meikyokan, the dressing off | 2.59 / 3.17 | -- |
| 8 Outside Meikyokan: a lawn of 27,000 tufts, flowers, fireflies (`ra1-dressed`) | 3.10 / 3.85 | 0.75 |
| ... four fighters pushing it (`ra1-dressed-stress`, before the thinning below) | 3.78 / 4.50 | 1.01 |
| 19 Lighthouse: its meadow and flowers, the most grass on screen | 3.54 / 4.36 | 1.30 |
| 31 Shainto cemetery: grass, petals, embers, fireflies | 3.83 / 4.78 | 1.31 |
| 11 Kimigaura coast: the sea, four rocks, the sand's dust | 2.63 / 3.20 | 0.03 |
| 20 Kabuki stage: embers alone | 2.38 / 3.22 | 0.02 |
| *the second pass (2026-11-25), the same card, other agents' work beside it:* | | |
| 8 Outside Meikyokan (`ra1-dressed`), the particles in the ground's depth; main's stored 3.63 / 4.53, 0.80 | 3.65 / 5.66 | 0.78 |
| 29 Building rooftop (`ra1-rain`): 4,500 streaks of rain and their splashes, puddles ringed by it | 4.19 / 6.53 | 1.26 |

The rain is the dressing's dearest: its streaks are lit by the stage's
light alone (no look into the sun's maps or the lights' list, as the other
particles), a puddle's second pass, the post's code, skips the rest of
the shader where there is no foam, and its rings are of the four squares
nearest a place; it was 2.5 ms at the 95th centile with 9,000 streaks and
all of that, 1.6 with 6,000 and none of it, and 1.26 with the 4,500 it
ships with, within `DRESS_BUDGET_MS`. A shaft of light is a box of twelve
triangles whose pixels walk eight steps through it: a hall's three cost
some tenths of a millisecond. The card's whole time of the dressed cases
moved between runs by a millisecond with the other agents' work on the
machine, the lighting's passes' 95th centile with it (0.6 to 2.2 ms on
the Meikyokan's lawn), so the dressing's own pass (`dress`) is the
measure that holds.

So a stage of water and particles costs nothing measurable; a meadow
costs what its blades on screen cost, about a millisecond at the 95th
centile where the grass fills the picture, held to 1.5 (`dress_p95`,
`DRESS_BUDGET_MS`, met with `MET_SLACK`) and the whole card to the
refresh. The grass-heavy stages were 1.6 ms before the foliage began to
thin at 5,250 units and not 6,300 (`FOLIAGE_THIN` 7,500, was 9,000). The
processor's side is the growing, once a stage (some 20 ms), and the
frame's pushers and dust, a few microseconds. With no dressing to draw
(`dressing off`, a Classic stage, a stage with no file, every case of the
bench but the dressed two) nothing of it is asked of the card.

**The second pass** (the owner, 2026-11-25: "work whatever items you can
without my intervention"): the weather, smoke and light, water that reads
as water, particles that are seen, scenery meshes in pieces' places, and
stage 37.

- **What mixes is tested in the ground's depth.** The water wrote its
  depth as the mesh does (the world's units over the mesh's range,
  `gl_FragDepth`), but the particles left the card's own, another scale:
  every particle in front of the ground failed the test, and only those
  against the sky were drawn. That, not their size, is why the leaves,
  the embers and the dust were hard to spot. They now write the mesh's
  depth too; and the leaves, embers and petals of the shipped files are
  half again as many and a fifth to two fifths larger, the dust a little
  thicker, the embers and the fireflies a hot core with a glow about it
  three times its size (their bloom: the post's own is of the lights'
  list alone).
- **The pictures' colours.** `tools/port_dressing.py shots` read the
  window's pixels (`port_GlReadWindow`: ARGB words, blue first in
  memory) as red first, so every picture of the first pass (its
  `tmp/stages17/`) has red and blue swapped: a blue sea looked brown, a
  beige beach blue. It reads them as they are now.
- **The weather.** `rain` particles are streaks along their fall,
  stretched across the line of sight, slanted by the wind, from `top` to
  the mean height of the ground under the area, worked out from their
  number and the clock; a third as many splashes, each a crown of a fifth
  of a second at the ground. A water block's `puddles` lays the ground's
  own faces of its colour 3 units up and keeps a patch of them where a
  noise of the place passes a height (how much, `puddles`), darkening the
  ground about each; `rain` rings them, a drop now and then in each square
  of 240 units, stateless. `mist` is banks of soft quads, flattened,
  drifting with the wind and wrapping within the area, fading near the
  camera and at the area's edges; `spray` bursts of 24 thrown up along an
  area and falling back; `smoke` rises from the game's flames (or the
  ground), growing and thinning over five to eight seconds.
- **Motes and shafts.** `motes` are lit by the frame's lights
  (`lights_add_wrapped`, the stage's spots and their shadow maps, so they
  are seen where a door's or a window's light falls) and a little by the
  stage's light; added. A `shafts` block makes a box of air for each light
  it names (a `DressBeam`: the opening across the light's way, along it to
  the floor or the light's reach), drawn at the picture's end
  (`port_GlDressingDrawLate`, from `port_GlFinishFrame` after the
  shadows), over the walls and pieces the game paints after the ground and
  held by the ground's depth: each pixel of its first face seen gathers
  the light along the line of sight through the box, eight steps, clipped
  at the floor's plane, soft at the box's sides, in blades across the
  opening, fading along the light, stirred by a slow noise; added, at most
  nine tenths of the light's colour. Its motes are particles inside the
  box, brightest by the opening. A shaft is shown at the times its light
  is, by the light's gain (`port_GlTimeLightGain`).
- **The water.** Each water block grows a map, a texel of each place over
  its areas and as far again as the foam reaches (256 a side; a stream's
  512): how deep the ground lies under it (the stage's own water at its
  height counts as deep), where a stream is (the texel under the place of
  `on`'s colour, smoothed), and how near the shore is (a chamfer of the
  distance from the ground standing above it). The fragment colours it
  deep or shallow by it, lit by the light's brightness and not its hue (an
  orange dusk leaves the sea a sea's colour), mirrors the sky tinted by
  the water's own (never more than 0.62 of it, so the water's colour shows
  even far off), with a Fresnel of the fourth power; foam laps along the
  shore and caps rough water; the waves are broken by a noise and evened
  out with distance. A second pass writes, into the alpha alone, the
  post's code of a floor that reflects `reflect` (gl_post.h), so the
  post's reflections -- the mirror of the floor's plane, or the
  reflection on the screen -- show the stage's pieces and the fighters in
  it (with `reflections` on). A stream (`on`) is laid on all the ground's
  faces in its areas and kept by the map to its painted course, texel by
  texel, its ripples carried by `flow`.
- **Meshes in a piece's place.** A piece's box was the box of its faces
  that look up, which leaves out a gravestone's sides under its top: it is
  now of all its faces (`port_GlDressingNoteGround`). The cemetery's three
  bare gravestones (pieces 3, 6 and 8) are stepped stones with chamfered
  edges made by `tools/port_dressing.py meshes`.
- **Stage 37** is the castle rooftop as link mode fights on it
  (`select.c`, `SELECT_STAGE_LINK_TO`); `--fight 37` (`castle rooftop
  link`) reaches it, and it is dressed.

The state is still the picture's alone: `tools/port_dressing.py hashes
--stages 8,11,31,29,0,14` (the rain, the shafts and the stream with the
first pass's three) gives the same state at every blank with the dressing
on and off, and each replay made with it on plays back with it off to the
same hashes.

**What is left.** The foliage takes the sun's shadows and does not cast
any (the meshes cast into the sun's maps and the lights' cubes,
`port_GlDressingDrawSunCasters`). The water's reflections are the
post's, so they are only as good as the picture's own (`reflections`
on; where the post's ray meets nothing, the sky the picture shows); and
they are not yet seen: the pictures of a window never shown
(`--hidden --tap-window`, `port_dressing.py shots`) hold none of the
post's passes -- `reflections` on and off gave the same bytes on the
main Tatara shrine's polished floor -- so the water's code for them is
written and nothing has looked at what it gives. A
shaft is drawn over a fighter standing in front of it (a fighter writes
no depth of his own). The `mist` kind is there, but no shipped stage's
open water lies where the fight camera looks. Gulls over the ship. Only
the cemetery's three plainest gravestones stand in for pieces: a mesh in
a piece's place keeps the piece's box's proportions within a quarter, so
each piece wants a model of its own.

## Every stage in every mode, and every unlock

The owner, 2026-11-19: practise on any stage with no second player, and
a mod for full unlocks to try every fighter. Two gameplay switches of the
window's settings (`settings_list.h`, the Game group), and of the command
line for the program without a window; `include/port_play.h`.

**`stage_select_everywhere`** (on as built; `--stage-select`). Versus
and link mode go from the crossed weapons to the stage pages; the
one-player modes end the select screen there, versus the CPU (and story)
with the first side's clan's stage (`select_GetClanStage`), and the
modes then pick their own stage: versus the CPU each opponent's
(`g_vscpu_stages`, `fight_NextVsCpuOpponent`), training its two halls
(`STAGE_PRACTICE_1` and `_2`, `fight_StartMode`). With the switch on,
`select_WeaponsOutStep` sends versus the CPU and training on to the second
stage page as it does versus mode (`port_PlayGoesToStages`), and their
fights are on the stage chosen there, `g_fight_config.stage`, for every
opponent of a run (`port_PlayStageChosen` in `fight_StartMode` and
`fight_NextVsCpuOpponent`). The page is the game's own,
`select_StageStep`: the one-player modes' file, `SEL1.BIN`, has the
same three pages, their pictures and the same `stage_codes` as
`SEL.BIN`. On a one-player mode's page the presses are the first pad's
alone, as its grid's are. The Classic/Remastered switch and the arenas'
page come with it: `port_StageModeChooses` (`stage_choice.c`) counts a
mode whose pages chose its stage, so the kind and the arena chosen stand
for its fights. `select_Load` forgets the choice (`port_PlayReset`), and
`port_PlayStageChosen` asks the mode as well, so another mode's fights
never take it.

**`unlock_all`** (off as built; `--unlock-all`). The save keeps the
progress in two fields of `SaveRecord` (`include/memcard.h`):
`unlock_mask`, a bit for each character won (an empty save has the six
`SAVE_DEFAULT_UNLOCKS`), and `staff_roll_character_mask`, the stories
finished with the six fighters each clan starts with. Every read of them
that gates something goes through `SAVE_UNLOCKS()` and
`SAVE_STAFF_ROLLS()`, which on the PlayStation are the fields and in the
port `port_PlayUnlocks` and `port_PlayStaffRolls`: with the switch on,
the mask has every bit a save can hold (`PORT_PLAY_ALL_UNLOCKS`: the
twenty named fighters and the two clans' last fighters) and every story
is finished. What reads them:

| Where | What it opens |
|---|---|
| `menu_EnterMode`, `menu_OpenSoloSelect` (`menu_screen.c`) | the fighters each select screen offers, through each mode's own mask: versus all twenty and the two crests, group battle and link mode the twenty, the one-player modes the eighteen without the gunners |
| `fight_BuildVsCpuOrder` (`fight_modes.c`) | versus the CPU's run: every opponent, the gunners at the eleventh place and the clan's last fighter, so the ending after the twentieth |
| `link_Connect` (`link.c`) | what the link cable tells the other console this one has |
| `menu_ResetToTop`, the options page's way back (`menu_screen.c`) | slash mode's row of the top menu |

The weapons are behind no progress: a clan's five, or a gunner's gun,
whatever the save says. **The save is never written by it.**
`g_saved_record` keeps the player's own progress: what a story, a
chanbara run or a staff roll wins is added to it as ever, from the
fields themselves (`staff_Update`, `fight_NextChanbaraOpponent`), so the
"now available" messages are the save's own, and the card's file, its
title's counts and a save made with the switch on are exactly what they
would be with it off.

**The state.** What the run asks for is kept outside the state
(`src/port/play_switches.c`) and taken in by the game's step whenever the
game asks, into `g_port_play` (`src/port/play.c`, a state file), with
whether the select screen went on to the pages. So a rollback puts the
choice back with the rest of the game, and a change in the settings menu
reaches the game at a frame of its own. In a session the welcome carries
the host's two switches (`MEET_PLAY`, since protocol 4) and both programs hold
them (`port_HoldPlaySwitches`), as they hold the kind of stage the stages
start as: a guest plays with the host's unlocks whatever its own settings
say (NETPLAY.md, section 13).

**How it is proved.** `tools/port_stage_select.py` (a step of the gate)
runs three plans besides versus mode's, on both sizes of the port, each
held to its values and the two traces to each other: versus the CPU with
`--stage-select --unlock-all` picks Matsumushi, whom an empty save has
not won, goes to the stage pages (the switch taken in, the pages marked
used, the unlock mask all of it), switches Abandoned Station to Classic
and fights there against the CPU; training with `--stage-select` alone
picks Kannuki with the empty save's own unlocks and fights the ninja in
Purple Cavern; versus mode with `--unlock-all` picks the Narukagami's
last fighter and Utamaru. `--shots` keeps the pictures of the two
one-player modes' stage pages. With both switches off, as the program
without a window starts, every play test is as it was.

## A fight's music

The owner, 2026-11-19: fights have no music in versus the CPU; "allow a
way to select music during stage select, defaulting to the stage's own
music but let me choose any track for any stage." The original's rule is
`snd_StartFightMusic` (`fight.c`): versus starts the stage's piece,
`g_snd_stage_music_tracks[stage]` (one of tracks 0 to 4, `bb2_m1-0` to
`-4`; a mod's arena's own through `port_ArenaMusic`), chanbara a piece by
the player's clan (8 or 9), practice its two (10, 11), and story, versus
the CPU, team battle and link mode nothing. With no music the stage's
ambient sounds play (`snd_AmbienceEnabled`): always in those modes, and
in versus, practice and chanbara when the options turn the music off.
`include/port_fight_music.h`.

**The rule.** The window's **`fight_music`** (`settings_list.h`, the
Mods group; `--fight-music` on the command line): `stage` (built in)
starts the stage's versus piece in every mode, practice and chanbara
keeping their own; `original` is the game's own; `none` starts none. The
program without a window, which reads no settings, plays the game's own
rule unless `--fight-music` says, so every play test that holds the port
to the emulator is as it was.

**The row.** On a stage page, after the kinds of stage have had the
presses (so never while the arenas' page is up, which takes them all),
`port_FightMusicPageStep` (`select_StageStep`) moves a row with L2 and
R2 (L1 and R1 are the stage's time of day; `select_StageStep` hands the
step L2 and R2 as the L1 and R1 it reads; the game takes L2 out of both
pads everywhere, `camera_ReadPad`'s first-person zoom being its only use,
and keeps it under `RB_PORT` while a stage page is up): the rule's own (`PORT_FIGHT_MUSIC_ROW_OWN`), the thirteen pieces of
the game's (the twelve tracks of `cdrom.c`, the two that nothing asks
for, 5 and 6, among them, and the staff roll's stream, `bb2_m2-4`, which
is started as `fight_after.c` starts it, by `cdrom_StartAudio`), the
pieces a mod brings under keys of their own, and None. `select_Load`
starts it again from the setting (`port_FightMusicReset`); the fights of
a mode whose stage the pages chose take what it was left at (versus, link
mode, and versus the CPU and training through `port_PlayStageChosen`, so
every opponent of a versus the CPU run), the others the setting's. The
window draws it as the stage pages' panel's Music column
(`window/stage_page.c`, `port_PageStrip`), the piece's name over L2 and
R2.

**The fight's start.** `snd_StartFightMusic` asks `port_FightMusicStart`
first, which says the track to start, none, or the game's own switch
(the rule's own under `original`, or in practice and chanbara; and the
options' music off, which is then quiet as ever). A piece started here
loops as the game's do (`cdrom_SetAudioLoop`). Ambience follows the
music as versus has it: `snd_AmbienceEnabled` asks
`port_FightMusicAmbience`, off where the port started a piece, on where
the row said None, the game's own rule otherwise.

**A mod's own piece.** A mod's `music/KEY.ogg` whose key names no stream
of the disc's (`port_MusicExtraCount`, `music.c`: no file of the disc's
directory, alone or with a dash and a channel) is offered in the row by
its key. The game cannot play what is not on the disc, and what it plays
is the game's state; so the fight starts the stage's own stream as ever,
and `port_MusicSetFightPiece` tells the music player, which follows the
drive outside the state (MODS.md, "Music"), to put the mod's piece in its
place the next time that stream starts, until another stream does. The
state is then the stage's own piece's, whatever the mods. In a session
the row offers none of them: two peers' mods may differ, and the row is
the state.

**A stage's own piece.** A mod's `music/stage07.ogg` is stage 7's
(`port_MusicStagePiece`, `music.c`; MODS.md, "A stage's own piece"). Where
the row is at the rule's own, the fight's start puts it over the stage's
stream the same way (`port_MusicSetFightPiece`): over the track it starts
under `stage`, and in versus over the track the game itself starts under
`original`; never on an arena (`port_StageArena`). The state is the disc's
piece's still. The stage page's panel reads "Stage's own: mod" where the
stage under the cursor (`PortStageChoice.cursor_stage`) has one. In a
session each machine sends the sum of its stages' pieces in the high half
of `MEET_MUSIC` (the low half is the host's setting, as before); where the
two sums differ both hold the pieces off (`port_MusicHoldStagePieces`,
`session.c`'s `port_HoldSessionStageMusic`), so the two hear one track; a
spectator holds its own to the host's.

**The state.** The row and what the fight's start made of it are
`g_port_fight_music` (`src/port/fight_music.c`, a state file); the run's
setting is outside it (`play_switches.c`) and taken in when the game
asks. The music itself is the drive's XA stream (`cd.c`, `xa.c`, the
chip), all of it state, so a rollback puts back the piece with the rest
(NETPLAY.md, "Sound"); what a mod's piece sounds like is the player's
alone. In a session the welcome carries the host's setting (`MEET_MUSIC`,
protocol 6) and both programs hold it (`port_HoldFightMusic`); the
pages' presses are the same on both machines, so the row is.

**How it is proved.** `tools/port_fight_music.py` runs versus the CPU
through the stage pages to Abandoned Station (track 3) on both sizes,
the two traces held to each other: with nothing said no piece is
started; with `--fight-music stage` track 3 is, and the drive streams
about 370 sectors of XA more than with none over the fight's ten seconds;
R1 six times starts track 5 (`bb2_m1-5`, never heard in the original);
L1 twice the staff roll's piece; L1 once None, the same sectors as with
no music and the ambience on. `tests/port/music.c` holds the mods' own
pieces and the piece over its stream.

**The piece ends with the fight.** A looping stream never goes idle by
itself, and a read waits for an idle drive. The original never reads
behind one: each screen that leaves a mode's music stops it first
(`cdrom_StopAndWait`: versus' menu after the fight, practice's pause
menu, the team battle's next match). The modes the game played no music
in have no such stop. So with `stage`, a win against the CPU went to the
next opponent's load with the piece still looping, and the load waited
for ever (the owner, 2026-11-19: frame 13363 on the loading screen,
quick_loading racing ~2000 blanks a second, the stream restarting, the
loading picture's red streaks piling up as long as it was drawn). Now
the win against the CPU stops the drive before `snd_EndFight`, as versus
does (`fight_WinnerViewUpdate`). For every other way into a read,
`cdrom_StartRead` and `cdrom_WaitIdle` stop a looping stream themselves
(`cdrom_EndLoopingAudio`, under `RB_PORT`) and the log says `game: a
looping stream of music stopped for a read of the disc`: story's fights
after a band of henchmen, its cutscenes and stage cards. In the original
that state was a hang, so nothing the original does changes. A restart
(a rematch, practice's next round) reads nothing and keeps its piece, as
versus' rematch always has. `tools/port_stage_select.py`, plan
`vscpu_next`, holds it: versus the CPU with `--fight-music stage`, the
CPU cut down by low strikes, the next opponent's fight loaded and
started with its own piece, both sizes the same.

## Match rules

ROADMAP 21 (the owner, 2026-11-20): beside the game's single fight, a
**Stock N** match (each side N lives, a fight lost takes one) and
**First to N** (fights until a side has won N), for versus, link,
online versus and versus the CPU (PLAYING.md, "Match rules", for how
they play); and (the owner, the same day) **Crew N**, crews of fighters
and weapons picked in turn (below, "Crew"). `include/port_match.h` has
the rules; all of it is under `RB_PORT`, and the PlayStation builds play
as they did.

**Where the choice is.** With ReBushido's top menu (`menu = rebushido`,
the window's as built), before the first select screen's grid: below,
"The rule first". With `menu = original`, as the original's menus are,
on a stage page: Circle turns the rule and Start
its number (`port_MatchPageStep`, from `select_StageStep` after the
kinds, the time and the music: neither button was the page's). The
page's rule starts at the window's `match_rule` and `match_count`
(`--match single|stock:N|first_to:N` headless), kept outside the state
in `play_switches.c` as the fight's music is, and keeps what the players
left it at until the setting changes; the window's panel shows it as a
column "Match" (`stage_page.c`) where the mode plays matches.

**The game's flow, and where the port steps in.** A mode started from
the menus (`fight_ApplyConfig`) begins a match with the page's rule
(`port_MatchBegin`); a rematch and versus the CPU's next opponent do not
pass there. Each fight runs as the game has it. Where the game counts a
versus win (`fight_AfterDefeat`, two seconds after the first fall: both
fallen is a draw; `cutscene_KaishakuUpdate`, a surrender) the port
counts the fight into the match (`port_MatchFightOver`): a draw takes a
life from both in Stock unless both are on their last (then none, so
the next fight decides and a Stock match always has a winner), and
scores nothing in First to N. The replay and the winner's view follow
as ever; versus the CPU's win goes to the screen after a fight while
its match is not won (`port_MatchNextOpponent`). On the screens after a
fight and after a kaishaku, before their menu, `port_MatchAfterFight`
holds a card up for 75 frames (120 for the match's result; Cross after
20 takes it down), then starts the next fight as Rematch does (where
the fighters are, `fight_FindNearestStartPair`) but without a continue
counted or the CPU made to forget; or, the match decided, goes the way
"Return to Sub Menu" goes (`g_menu_reopen_mode`) with a mark that
`select_Load` reads: the select screen opens on the stage page versus
opens on, each side's pick the settings' `match_picks` and its grid
cursor on its fighter, so Triangle goes back to the weapons
(`select_PortBackToStages`). Versus the CPU's match won goes on to the
game's next opponent, whose match starts afresh; its match lost is
handed to the game's own menu after a defeat, the score reset, so its
Rematch is a new match.

**The state.** The page's rule and the match's score (lives, wins, the
last fight, the card and its frames), the rule page and its row, the
wounds a winner keeps (below) are `g_port_match`
(`src/port/match.c`, a state file): saved, put back and hashed with the
game, so a rollback puts the score back with the fight and the two
machines of a session keep the same. In a session the welcome carries
the host's `match_rule` and `match_count` (`MEET_MATCH`, protocol 8) and
both hold them (`port_HoldMatch`); the pages' presses are the same on
both machines, so the rule is.

**The picture.** None of it is in the game's video memory: the window
lays it over the picture at its own resolution (`match_page.c`, through
the overlay `port_DrawOverlay` that the stage pages' panel uses too).
Over a fight of a match, light (the owner, 2026-11-20: "keep the gui
very light for actually in battle"): each side's label and small pips,
filled for the lives left, the wins or the crew's fighters left, on a
faint band where the game's own count of wins stands (`hud.c`,
`COUNTER_LEFT_X`, `COUNTER_RIGHT_X`), under the CPU's count of opponents
versus the CPU; no rule and no names (the cards between fights have
them); after a fight the card, its title the rule and the fight's
number, each side's name, number and pips, and who took the fight, a
draw, or who won the match. What is shown is `port_MatchShown`
(`gamestate.c`): the card on the screens after a fight while one is up,
the strip on `SCREEN_FIGHT_UPDATE` of the match's mode. The strip
replaces the game's own "WINS 00" counter, which is not drawn while a
match is in play (`fight_UpdateMode`, `port_MatchHidesWins`: any rule but
the single fight, in the match's own mode; it is the game's step, so the
software picture, the remastered one and both menus lose it alike); the
game's single fight keeps it, as do the CPU's count of opponents and
training's practice round.

**How it is proved.** `tools/port_match.py` (run by `make
port-stage-select`) plays a plan of each rule on both sizes, the traces
held to each other: versus Stock 2 (two kills by the first fighter's
low strikes, the score's card 2-1, the next fight on the same stage and
fighters, the result's card, the stage select, Cross there a new match
of the same fighters); versus First to 2, chosen on the stage page with
Circle and Start; versus the CPU Stock 2, which as the port plays today
is a fight won and two lost, the game's menu after it and its Rematch a
new match; versus the CPU Stock 1, the match won and the next opponent
with a fresh one. In the versus plans the game's pictures at each
fight's end have none of the counter's red rules (252 pixels before it
was left out). With ReBushido's top menu (`--menu rebushido`), the rule first:
`rb_vs_crew`, versus, Crew 2 chosen on the rule page (Right three
times, Down, Left three times), the second place's Cross on the first's
fighter refused on each side (nothing picked 90 frames on) and then one
place down; the first fight the first player's, the plan giving both
fighters a leg wound during it (a `poke`): Kannuki stays with his
(`p1_leg_wounds == 1` at the second fight's start, kept by the match),
Kaun comes in unhurt; the second fight his too, the result, and the top
menu, the crews let go; `rb_vs_stock`, Stock 2 on the page, the same
wounds: the winner keeps his into the second fight, the loser of a life
comes back unhurt, then the stage page as Stock has it; `rb_vscpu_crew`,
versus the CPU with no stage page, Crew 3 on the page, the second and
third places refused the crew's fighters first, the CPU's three all
different, the match lost and the top menu, not the game's menu after a
defeat; and two with no poke, every press the pads'
(`wounding_presses`: the second player strikes first, the first then
wins, wounded in the body): `rb_vs_crew_wounds` and `rb_vs_stock_wounds`,
the winner's four counts at the second fight's start the match's, the
other's none. `tools/port_replay.py` plays those two matches' fights back
from their replays, every hash the same (the second fight's winner
wounded from its first frame, which the replay's start holds), and
`port-rollback` rolls them back every 24 frames on both sizes, with
`vs_stock` and `vs_crew_won`. `vs_first_to` gives the first fight's
winner a leg wound too, and its second fight starts with none. Group battle is played under `--match stock:2`,
`first_to:2` and `crew:3` in turn: its first match won, the card of
the scores, the second match begun and 300 frames of it, with no rule
in play and no card of the strip at any of them ("Memory", "A table on
a stack that has gone"). `tools/port_rollback.py`'s Stock plan rolls a match
back every 24 frames; `tools/port_net.py session` plays its session as
a Stock match.

### The rule first

The owner, 2026-11-20: "make sure they can choose their battle type
before they land on the character select screen the first time." In
versus, versus the CPU and online, with ReBushido's top menu, the select
screen opens on a page of the rule, the grid drawn under it and given no
presses (`port_MatchRulePageOpen` from `select_Load`,
`port_MatchRulePageStep` from `select_Update`): two rows, the rule
(Single fight, Stock, First to, Crew) and its number (lives, wins, or
the crew's size; no second row for the single fight); Up and Down the
row, Left and Right the value, round at the ends; Cross takes it and the
grid has the presses; Triangle leaves the screen, as Triangle on the
grid with nothing picked does. Its rows start where the window's
`match_rule` and `match_count` put the stage pages' (the page's rule is
the same `page_rule`, `page_count` and `page_crew`). The window draws
it (`match_page.c`, `PORT_MATCH_SHOWN_RULE`), over a darkened screen,
in the stage pages' panel's colours; the game's video memory has the
grid alone.

Choices made: the page comes each time the select screen opens afresh
(from the top menu, or after a Crew match), not when it opens on the
stage pages after a Stock or First to match, whose rule stays. The stage
page then shows the rule in its panel's column and does not change it
(`rule_first`: Circle and Start are not the match's there), so that crews
picked are always of the rule they were picked for; to change the rule,
Triangle back to the top menu and in again. Triangle on the grid of a
crew's later place, nothing picked, goes back one place (its picks made
again), and on the stage page after the crews back to the last place's
weapons; on the grid of the first place it leaves the screen as the
game's does. Online the host's pad (the first) chooses; the guest's
presses on the page are not the page's, but either player's Triangle
leaves, as on the grid, and both machines show the page as the host
turns it (the page is the game's state). In a room of several the rule
is the room's, as its host set it: no page, the rule first all the same
(the crews picked at the grid), the stage page showing it. With `menu =
original`, none of this: the page is not shown and the rule is chosen on
the stage pages as before, crews after them (below, "Crew").

### Wounds kept

The owner, 2026-11-20: "For crew battle, make sure injuries do persist
between rounds", and "For stock battles, make injuries persist as well.
But leave injuries resetting for first to X fights." The game keeps a
fighter's wounds in four counts (`Fighter.off_arm_ok`, `leg_wounds`,
`arm_wounds`, `body_wounds`: a lost off arm, and the legs', the sword
arm's and the body's wounds, which slow it and take its power;
`fighter_Wound`, `motion_UpdateFrameStep`) and heals them as each fight
sets the fighter up (`fighter_Init`, `fighter_HealWounds`; only story
mode's player keeps his through a chapter). Under Stock and Crew, where
the game counts the fight (`port_MatchFightOver`), the winner's four
counts are kept (`port_MatchKeepWounds`: `wounds_kept`, `wounds`), taken
before the winner's view, which gives the winner his off arm back for
its pose; the loser (Stock's life lost) comes back unhurt, or is out
(Crew: the next of his crew comes in unhurt), and a draw keeps nobody's.
`fighter_Init` puts them back after its healing (`port_MatchPutWounds`),
for the next fight of the match alone: a match begun or restarted, the
next opponent's, a First to N or a single fight have none. Everything
else is fresh as before (Crew's fighters loaded again at the stage's
places; Stock's where they stood). No count kills: a kill is a blow's
(`fighter_TakeHit`), so a fighter who starts wounded is slow, weakened
or one-armed and stands; the counts are kept as they were, large ones
too (the game's tables stop at three). The story mode's wrappings on the
model (`g_fight_story_wounds`) are story mode's and not drawn. The card
between fights says who stays wounded and where.

### Crew

The owner, 2026-11-20: "a crew battle type gamemode where you select 5
fighters and five weapons and those are essentially your five lives.
First to kill all the opponents' fighters wins." `PORT_MATCH_CREW`,
after First to on the stage page's Circle; Start turns its size, 2 to
8, five unless the window's `match_rule = crew` gives `match_count`
(`--match crew` is five, `--match crew:N` N). The page keeps the crew's
size apart from Stock's and First to's number (`page_crew`).

**The pick, on the game's own select screen, the rule first.** With the
rule chosen before the grid ("The rule first"), the crew's size is known
before anything is picked: from the first place on, each place's
fighters and weapons are picked on the grid and the weapon row, and
taken as its weapons cross (`select_WeaponsOutStep`,
`select_PortCrewPick`, `port_MatchCrewWanted`), the grid coming up again
for the next while places are left; after the last the screen goes on
as the game's does, to the stage page or, versus the CPU with no stage
page, to the fight. The strip at the foot shows the crews from the
first place.

**Every pick a different character** (the owner, 2026-11-20: "enforce
choosing different characters"). Within one crew a character is never
picked twice; weapons may repeat. Cross on a character the side's crew
already has is refused as a locked one is, with its sound
(`port_MatchCrewHas`, in `select_MoveFighterCursors`); the grid draws it
as the game draws a locked fighter when every crew picking has it, and
its name dimmed when one crew has it and the other may still pick it
(`select_FighterStep`, `select_PortCrewTaken`). A crew is no bigger than
the characters the grid offers (`crew_most`, counted as the screen
loads, `select_PortOpenFighters`: six with an empty save's versus, eight
and more with `unlock_all`): the rule page's and the stage page's size
stop there. The CPU's crew is all different characters too (below).

**The pick with `menu = original`.** The rule is chosen on
the stage page, which comes after the fighters and weapons, so the
fighter and weapon each side has already picked are its crew's first
place. Where the screen would then be done (Cross on a stage, an
arena's page, or, in a mode with no stage page, the weapons crossed),
`select_PortCrewPick` hands the picks to `port_MatchCrewTake` and, while
places are left, closes the door on the grid instead
(`g_select_next_step`); when the grid comes up `select_PortCrewGrid`
clears the picks, each cursor left where it was, and the grid, the
weapons and their crossing run as the game has them for the next place.
The last place's crossing ends the screen with the stage chosen before,
the settings' first picks put back to the crews' first places
(`select_PortCrewFirst`), so the first fight is theirs. A crew's
characters are all different here too; within one place the two
cursors keep apart, as the game's always do. Triangle on the grid with nothing picked, during
a later place, lets the crews go and goes back to the stage page with
the first place's picks (`select_PortCrewBack`); Triangle on a stage
page lets kept crews go. While a later place is picked the window lays a
strip over the foot of the screen: each crew so far, names and weapons,
and the place being picked (`PORT_MATCH_SHOWN_PICK`).

**The fights.** `port_MatchBegin` takes the crews (a place not picked,
which only a mode that picked none has, is the fighter and weapon the
menus started with). The fighter in play of a side is its crew's place
after those lost (`lives` counts a crew's fighters left). A fight lost
takes the loser's fighter, a draw one from each; when a crew has none
the match is over, its winner the other, or `PORT_MATCH_DRAWN` (2) when
both ran out at once. Between fights the card, then
`port_MatchNextCrewFight`: each side's fighter in play into
`g_fight_config` (roster and weapon), `g_fight_restart` 0, and the
fight is loaded and begun afresh, behind a black screen
(`LOADING_SCREEN_NONE`: the game's loading pictures are not in video
memory once the screen after a fight has drawn its menu's sheets over
them, and either shows scraps of it), as any fight starts, the winner too,
at the stage's own places (where the Stock and First to fights start
where the fighters stood). The winner's fighter and weapon stay, with
the wounds he ended the fight with ("Wounds kept"); the incoming fighter
is unhurt. The game's own wins count goes on counting (the HUD hides it
under any match rule).

**After the match** (the owner, 2026-11-20: "when the crew battle is
over, the session ends and it goes back to mode select"). The result's
card, then the top menu, in versus, versus the CPU and online alike,
won, lost or drawn (`port_MatchAfterFight`: `to_top`, the crews let go,
`g_menu_reopen_mode` 0). In a session of two the top menu is the
session's end on both machines at the same frame, whatever the
session's rules (`port_MatchEndsSession`, `port_SessionInput`): each
goes back to its own game, as when both players go back to the top menu
there. A room of several's match ends where it is decided, at the
result's card, as before, and its players go back to the room. Stock
and First to keep their way: the stage page, the fighters kept.

**Versus the CPU.** A match per opponent, as the other rules. The
player picks his crew; the CPU's is led by the game's own opponent
(`fight_NextVsCpuOpponent` calls `port_MatchOpponentChosen`), its other
places drawn from the run's opponents (its last two left out), each
with a weapon as the game gives an opponent one (the gunners their guns,
a blade otherwise, the second clan the yari for the naginata). Each
place is a character the crew does not have yet: the leader is taken out
of the pool, and each character drawn too; should the pool run dry
before the crew is whole, any named character not in it
(`port_MatchCrewPoolRefill`). The draws are dice of the match's own
(`crew_seed`, seeded from the frame count and the opponent's number),
so the game's `rand()` is drawn on as without a crew. The crew match
against the CPU is the whole run: won or lost, its result's card and
then the top menu, not the next opponent nor the game's menu after a
defeat.

**Online.** The host's `match_rule` and `match_count` hold, as for the
others (protocol 11: the rule may be Crew, which an older program does
not know; protocol 12: the rule page, crews of different characters,
the wounds kept, a Crew match's end the session's), and with
ReBushido's top menu the host's pad chooses on the rule page. Each player picks his own crew with his own pad on the
shared select screen; the crews, the place being picked and the score
are `g_port_match`, in the state, so a rollback, a replay and the two
machines of a session hold them. In a room of several (NETPLAY.md,
section 16) a rotation's match of Crew ends where Stock's does: at the
screen after a fight once the match is over (the result's card), its
winner the room's result, a drawn match nobody's
(`port_MatchIsDecided`, `session.c`).

**The picture** (`match_page.c`). Over a fight the light pips of the
other rules, a pip a fighter of the crew left; no names. The card after
a fight: each crew as a column of
names and weapons in the same three looks, how many are left, and who
took the fight and who comes in, and who stays wounded and where (or the draw, or who won the match, or
that both crews are out). The names are `port_MatchCrewName`
(`gamestate.c`, the character's roster's name).

**How it is proved.** `tools/port_match.py` (`make port-stage-select`),
both sizes, the traces held to each other: the rule-first plans above
(`rb_vs_crew`, `rb_vscpu_crew`); and with `menu = original` `vs_crew`, versus Crew 3 (the
second places picked one place down the grid on each side, the third
another: Kannuki, Mikado, Tatsumi against Gengoro, Kaun, Jo); the first
fight the first player's (Gengoro out, Kannuki stays and meets Kaun),
the second a draw (both out: Mikado meets Jo), the third the first
player's (the second crew out, 2 to 0), the result and the top menu;
`vs_crew_drawn`, Crew 2 of two draws, a drawn match and the top menu;
`vscpu_crew`, versus the CPU Crew 2, the CPU's crew the game's first
opponent and another character drawn, which as the port plays today
takes both of the player's fighters, and the top menu.
The draws are written by the plan (`poke`: the first fighter fallen
once the second has), since nobody plays the second fighter. `tools/
port_replay.py` plays a Crew 2 match's fights back from their replays
(`vs_crew_won`: no draw, which no replay could hold), every hash the
same; `tools/port_net.py session` plays two more sessions of each size,
begun in the running program (`--session-at 1`: with ReBushido's top
menu the opening movie comes at once, and plays through between two of
the program's boundaries) with `--menu rebushido`: Crew 2 and Stock 2,
the host turning the rule page (the guest's Right there not taken),
each player picking his crew at the grid, a refused Cross first; the
fights `wounding_fight`'s (the second player strikes first, the first
then wins, wounded): the first fight's winner kept wounded into the
second (his four counts at its start the match's, the other's none),
and the Crew match's end the top menu, where the session ends on both
at the same frame and each program's own game is put back; the hashes
the same on both, and the fights' replays the same on both machines and
played back with every hash the same, the second fight's winner wounded
from its first frame. `tools/port_net_lobby.py headless`'s `rooms-crew`
plays a room's rotation match as Crew 2, two players and a spectator:
no rule page (the room's rule), the crews picked at the grid of
different characters, `wounding_fight`'s two fights, the first's
winner wounded into the second, the match decided at the result's card
and each player back to the room, the players' hashes alike and the
spectator's theirs.

## Two on two

ROADMAP 24 (the owner, 2026-11-21: "go ahead"): four fighters in one
fight, two teams; a button turns a fighter to the other enemy; online.
`include/port_duo.h` and `src/port/duo.c` (a state file); all of it is
under `RB_PORT`, and with two on two off the fight is the game's own,
frame for frame (`fight_Update` hands its frame to `fight_UpdateDuo`
only while `port_DuoIsOn`).

**How the game holds four.** The game's fight is written for two:
`g_fighters[2]`, each fighter's `other`, the scratchpad's points of two
(`FightScratch`), the pair's globals (how far apart, the paired move,
the blade lock), two model slots, two motion files. Rather than widen
all of it, each fighter has a place of his own in `g_port_duo`, and the
fighters a step is about are put into the game's two slots: a **team is
a slot** (the first pad's team the first slot, the second's the
second), so a fighter is only ever put beside an enemy, and the game's
code, which knows nothing of four, runs as it is. What a fighter has
that is his slot's goes with him (`port_DuoPut`): his `Fighter`, his
model in the slot (`model_PortPut`), his scratchpad points (weapon,
second blade, bodies apart, the 22 of the body) and his body points
(`g_fighter_body_points`), and his frames of the replay's ring. What a
pair has (`g_fight_distance` and its change, the paired move's seven,
the falling pair, the blades met, the lock's five) is kept for each of
the four pairs of enemies (`PortDuoPair`, `port_DuoPairKeep`,
`port_DuoPairTake`).

**Each fighter his own character and weapon.** The select screen picks
each team's second as it picks its first: in a match of two on two the
grid and the weapons come up a second time, the place a Crew's second
place is (port_match.h: `port_MatchPlaces` is two for the teams), each
side's first then his partner (`PortMatch.partner`, `port_MatchPartner`);
with four players the third and fourth pads pick the partners
(`port_MatchPartnerPads`, the pad unplugged leaving it to his first's);
versus the CPU the CPU's partner is drawn from the run's opponents as a
Crew's places are (`port_MatchOpponentChosen`). A fight with no select
screen (`--fight`) takes `--partners FIGHTER FIGHTER`, or has each team
two of its first. What the game holds of two characters is then held of
four:

- **The model.** A second of a model no slot has is loaded beside the
  game's (`port_DuoLoadPartners`, in `fight_Init` before the weapons):
  his file read where the slots' are staged and what it keeps copied out
  of the arena, into a room of the console's RAM that the game leaves
  alone (`PORT_DUO_AREA_ADDR`, `0x80010000` up to the common sound bank:
  the PlayStation's own program is there, and the port's program is
  elsewhere, Memory above), his meshes two tables of the renderer's of
  their own (`PORT_DUO_RELOC_BLOCK`, 10 to 13), skeleton, nodes, cloth and
  shadows made as `model_Load` makes a slot's (`model_PortLoadPartner`,
  model.c). Of the same model as his first, his model is the first's
  copy as before (`port_DuoCloneModel`).
- **The textures: a second bank of video memory.** Video memory holds two
  characters (the slots' pages at x 640 and 768, x 896 up the effects',
  the lower half the stage's), and a model's textures are named by its
  meshes' own pages and colour tables: a third or fourth has nowhere to
  go in the console's 1024 by 512. The port's GPU has a second bank of the
  slots' part (x 640 to 895, lines 0 to 255: `PORT_VRAM_BANK_X`, `_W`,
  `_H`; port_gpu.h), 128 KB at the head of the partners' room of the RAM
  (`PORT_DUO_BANK_ADDR`), so the state saves and hashes it with the RAM
  and the program's own variables are no larger. A partner's textures
  and colour tables are loaded there where his slot's are in the first
  (`port_SetVramBankLoading`: `LoadImage` writes the bank), and his
  meshes' colour tables have their top bit set (`tex_PortMarkBank`,
  through `tex_ShiftBlock`), which the chip never reads: a polygon whose
  table has it reads its texels and table from the bank where the bank
  has them, while the fight has one (`port_SetVramBank`;
  `PortPaint.bank`, raster.c). Nothing draws into it: a face's eyes and
  mouth are copied within it at once (`port_VramBankMove`, model_face.c),
  not by the GPU. With no bank, or a table's top bit clear, everything is
  the console's: one on one is the game's, pixel for pixel.
- **The motions.** Each fighter's motion file, the decoder of its stream
  and his win pose go with him into his slot (`PortDuoMotion`:
  `port_DuoMotionKeep` and `port_DuoMotionTake` beside his `Fighter`,
  `PortDuo.motions_out`), as his model does: the file in the game's two
  rooms where it is one of theirs, else read into his room of the area
  (`port_DuoLoadPartnerMotions`, with WIN.DAT's part of his character),
  and each fighter's `motion_slot` his slot's, so that whoever is stepped
  reads his own file through the slot's decoder. The game's own go back
  as the next fight is set up (`port_DuoMotionsHome`). With the seconds
  their firsts' characters and weapons none of this happens: the fight of
  four is what it was.
- **The weapon.** The second's weapon and what he throws are asked for
  with the firsts' (`port_DuoAskWeapons`), from the one weapons file.

What stays the slot's: the model's sound bank (a partner shouts with his
first's voice) and the weapons' sounds (the file of the two firsts'
kinds), the point light's target, the sounds a thrown thing makes. The
remastered picture draws the second bank too: a polygon of it names its
page and table 1024 lines further down (`PORT_VRAM_BANK_NAMED_Y`,
remaster.c), the card's mirror of video memory has the bank's lines after
the console's, at line 512 and after (`port_GlUploadBank`, each frame the
lines that differ; `vram()` of gl.h reads a line of 1024 or more there),
a fighter's colour tables are read from the bank for his model's tints
(gl_fighter.c, `port_VramBankAt`), and a
partner's meshes are a fighter's to the renderer
(`port_RemasterNoteFighter`, render_asm.c), so **each of the four is
drawn from his own model** (`fighter_models`) where one is given. A mod's
picture of a whole page is not laid over the second bank's (gl_replace.c,
gl_scene.c leave it to the textures as loaded).

The four fighters' primitives overflow the console's buffers: in a
fight of four the port's larger buffers are the game's
(`port_DuoPrimBuffers`, at a frame's start), where the run has the
console's (and the state keeps them, by the pages written).

**A frame** (`fight_UpdateDuo`, fight.c): the side view over all four
(below); the targets (below); each pair measured (`fight_UpdateDistance`,
whose paired moves are not begun between two of whom one is held by a
third, `port_DuoMayPair`); each fighter, 1 to 4, stepped in his slot with
his target in the other (`fighter_Update`), his pad read: the game's
two pads as the game reads them, the third and fourth through the
game's own reader as if they were the slot's half of its pad state
(`port_DuoReadOwnPad`: the button configuration and the mirroring are
the slot's), the CPU's its own; R2 and Select are taken out of every
fighter's pad. Then each pair's blows and bodies (`fight_UpdateFighterPair`):
a pair held together by a paired move or a lock always, one of whom is
held by a third never, the fallen not; team mates kept apart by a push
of their own (`port_DuoKeepMatesApart`). Then the pair the fight rests
on is put in the slots (`port_DuoEndFrame`) and the frame goes on as
the game's: the loose objects, the stage's cells for all four, the
scene, the mode, the fall.

**Targets.** Each fighter targets an enemy, his `other` while he is
stepped. A press of **R2** (`PADR2`, which the side view's fight never
reads: only the first person's zoom took it, with L2, and ReBushido has
no first person) turns him to the other enemy, if he stands and is not
held by a third; the button is a pad button like any other, so it is
remapped as every other is (`p1.r2 = ...`, the settings menu's
Controls). A fighter whose target falls turns to the one left. The CPU
turns to the other enemy when he is nearer by a third, looking every 30
of its frames (`port_DuoChooseTargets`). A paired move or a blade lock
is between two: from its start (`MOVE_EVENT_LOCK` on both, a paired
move's start asked for in a step, or a throw begun at its distance by
the pair's measure, `port_DuoMeasured`) until neither is in one, the
two are held to
each other (`PortDuo.bound`), each other's target, and neither strikes
or is struck by anyone else.

**Blows.** A pair's meeting is the game's: either fighter's blade on
the other's body, guards, clashes, deflections, wounds, the one-hit
kills, a gun's shot, a thrown thing (`object_HitFighters`, by the slot
that threw it), all of it between enemies only. Team mates never meet,
so there is no friendly fire: their blades pass through each other.
A swing meets whichever enemy it reaches, the target or not (the third
pad's plan strikes at 2 and its blow lands on 4, who stands beside him).
No one gives up: the kaishaku (Select) is a scene of two, and Select is
taken out of every fighter's pad.

**The fall and the end.** A fallen fighter lies and is stepped as the
game steps one (his death throes, his weapon dropped); the fight goes
on. `g_fight_winner` is held at nobody while both teams stand. Between
steps the fight rests on a pair (`PortDuo.rest`): the living, the first
of each team before the second; once a team has none, the last of it to
fall and the enemy who has him as his target. So the game's own fall
(`fight_UpdateDefeat`: two seconds, then the mode), its count of the
win, the match's (`port_MatchFightOver`: a team out is a side's fight
lost), the replay and the winner's view all run as they do for two: the
replay is of that pair (each fighter's frames are kept in a ring of his
own, and the game's ring is given the resting pair's, all of it as the
fight is decided), the winner's view of the winner. Both teams out
within the two seconds is the game's draw (both fallen in the slots).

**The camera** (`port_DuoCameraPoints`) gives the side view
(`camera_UpdateSideView`) two points and two fighters: along the line
from the first team's middle to the second's (so the first team stays
on the left, where the game's pad directions want it), the two ends of
the fighters to be seen (the living; all four once the fight is
decided), each end pushed out by half the widest of them off the line,
so that the distance the view eases to takes in all four; the fighters
whose ground it looks over are the two at the ends.

**The match.** The rule page has a third row, Fighters: one on one,
two on two with the CPU's partners, or with four players
(`PortMatch.page_teams`, `teams`; the window's `teams = one|cpu|pads`,
`--teams`). Single fight, Stock and First to take it; Crew does not (its
fighters change between fights; the row is not shown). Two on two's
fighters start every fight unhurt: no wounds are kept. Stock and First to
count a team's fight as a side's. A fight's second fighters stand beside
their firsts, square to the line between the teams, at 1,600 (or 900,
or behind), where the way is clear and the ground level
(`port_DuoFindPlace`), the two seconds facing each other as the firsts
do. Versus the CPU, the CPU's team is its opponent twice, the player's
partner the CPU's or the third pad's.

**The pads.** The input has four pads (`PORT_INPUT_PADS`): the game's
two, which its reads see, and the third and fourth, which only two on
two reads. A replay keeps them (format 2: `CHANGED_THIRD`,
`CHANGED_FOURTH`; a file of format 1 is read as before). The window
binds four (`PORT_WINDOW_PADS`): `p3.*` and `p4.*` in rebushido.cfg,
the third and fourth controllers plugged in as built in, and a page of
each in the settings menu's Controls. A plan presses `P3` and `P4`.

**The picture.** Remastered draws all four: the fighters' own models
for each (the views are five, the game's three slots and the two
copies: `port_DuoViewModel`, fighter_view.c). Over a fight of two on two
the window lays, as light as the match's strip (match_page.c), a band
under the game's count of wins (or the match's strip) with each team's
fighters, `1P`/`3P` and `2P`/`4P` or `CPU`, a pip each filled while he
stands; and over each standing fighter's head a small tag of the same in
his team's colour, under it the numbers of the players who have him as
their target (`port_DuoTagPixels`, laid where the head projects:
`port_DuoShown`, the renderer's view squashed to half height as the
frame is). The game's video memory has none of it.

**What it costs.** A fight frame of four steps the fight about twice
over: `port-rollback`'s fights of four run a step of a fight in 4 to 5
ms on Linux in WSL (two fighters' about 2 to 3). On the Windows
program, by `tools/port_bench.py --rollback` (the program without a
window, port_game's plan, `--rollback-test 8 --rollback-quiet`): a
frame of the fight run again quietly 0.71 ms one on one
and 0.98 ms two on two, a state put back 0.08 to 0.09 ms;
**a rollback of netplay's whole window of 8 frames 5.8 ms one
on one and 7.9 ms two on two** (the worst 6.8
and 8.5). The window (`port_bench.py`, the case
`ra1-2v2`, the fight of four with the CPU's partners, RTX 3090 at 2,560
by 1,440): beside ra1 run straight after it, on a machine loaded by other
checkouts' emulators (38 and 40% of the processors busy, so both
higher than on a quiet one): the window's processor 4.04 ms median and
7.50 at 95% (ra1 2.00 and 3.36), the card 3.10 and 4.25 (ra1 2.86 and
4.16), the step's own thread 3.91 (ra1 3.10), 6.6% of the fight's
blanks over 6.94 ms. The band and the four tags are five overlays a
picture: drawn through one texture of the card, each written while the
card still drew from it, they cost the card 4.5 ms a picture, so the
host's pixels have textures of their own now, none written again for
three pictures (`port_GlShowPixels`, gl_draw.c: the card 7.45 ms
median before, 3.10 after).

**How it is proved.** `tools/port_duo.py` (`make port-stage-select`),
both sizes, the traces the same line for line:

| Plan | What it holds |
| --- | --- |
| `retarget` | `--fight` Kannuki against Mikado, every fighter a person's, only the first played: 1 targets 2 as the fight starts, R2 turns him to 4, his blow lands on 4 (counted against 4, by 1, none on 2), 4 falls and 1 is turned to 2 by himself, 2 falls: the second team out, the winner the first, the fight resting on 1 and 2 (the last to fall), the game's fall, replay and menu |
| `pads` | the third pad: 3 turns from 4 to 2 with his R2 and his blows land (on 4, beside 2) |
| `menu` | ReBushido's main menu: the rule page to Stock 2 and two on two with the CPU's partners, the grid, the grid again for the partners (the first's one place down, the second's his first's), the stage; fights of four characters (Kannuki and Mikado, Gengoro and Kaun), the score's card between, the result's; the second player strikes too, so that no fight stands off |
| `menu_pads` | the same with four players: the partners picked by the third and fourth pads (`P3`, `P4`), one place and two down, their weapons one along; a first fight of four characters |
| `cpu` | all four the CPU's: a fight that plays itself to a team out |
| `second_wins` | the firsts written down (a `poke`), the fight goes on with the seconds, then 3: the second team's win |
| `draw` | all four written down at once: both teams out, the game's draw |
| `stock` | `--match stock:2`, the CPU's partners: two fights, the score's card, the next fight with all four up, the result |
| `four` | `--fight` Kannuki against Mikado, `--partners tatsumi:nodachi utsusemi:yari`, all four the CPU's: four characters and four weapons (`port_duo_character_1` to `_4`), a fight to one team out, the game's own fall, replay and winner's view |

`retarget`, `pads`, `cpu` and `four` are recorded as replays and played
back by both sizes with `--replay-check`, every hash the same (and `pads`'
input of the third pad with them). `make port-rollback` rolls back
`retarget`, `pads`, `stock` and `four` every 24 frames on both sizes,
plain and quietly, with no difference. Online: NETPLAY.md, section 18.

**Four players online** (NETPLAY.md, section 18): a session of four pads
(`--players 4`, four PCs, or `2x2`, two on each of two) is two on two
with every fighter a pad's (`port_DuoPartnerIsCpu`: a session's partners
are the CPU's only where it has two pads, `port_NetplayPads`), the
session's match held at `PORT_TEAMS_TWO_PADS` (`port_MatchBitsTeams`).
The four pads are netplay's (`g_np_input`), the third and fourth read as
a local fight reads them; a replay of the fight keeps all four, as a
local one does. A rollback at four remote pads is a rollback at four
fighters: `tools/port_bench.py --rollback`'s `rollback-4p` (`--teams
pads`, every fighter a pad's), on the RTX 3090's PC on 2026-11-25 while three other agents loaded it: a frame of the fight run again 1.70 ms (worst 4.25), a state put back 0.38 ms, a rollback of the whole window of 8 frames 14.0 ms on average and 37.4 at most, against 13.8 and 42.9 for two on two with the CPU's partners and 10.5 and 30.0 for one on one in the same run (the loaded machine doubles them all against the quiet run's 7.9 and 5.8 above): four remote pads cost what four fighters cost, a few hundredths of a millisecond a frame more than the CPU's partners, nothing for the pads themselves.

**A player who leaves** (NETPLAY.md, section 19): a pad of the input
whose `present` is `PORT_PAD_CPU` (`port_platform.h`) answers and holds
nothing; at the first step of a fighter whose pad says so,
`port_DuoPutFighter` makes him the CPU's (`port_DuoTakeOver`: `is_cpu`,
the match's CPU level, `cpu_Init` against the one he faces, as a CPU
partner is set up), and a fight of the same match after it does the same
at its first step. It is the input's, so a rollback, a spectator and a
replay (whose `present` byte keeps the CPU's pads in its high four bits,
`port_ReplayPresent`) make him the CPU's at the same frame.

**What is left.** Teams of two different characters, or different
weapons, need video memory for a third and fourth character (a model's
textures are addressed by its meshes' own pages) and two more motion
files; a wider video memory is a change of the GPU, the side channel and
the remastered picture alike. A team mate's face and point light are his slot's. A
fighter left lying by his player on a roof below (the rooftop) may be
out of his enemies' CPU partners' reach. The owner's eye on the band,
the tags and the page.

## ReBushido's main menu

The owner, 2026-11-19: drop the options page at boot, put every setting
in the menu's Options, drop the first person's view, Link Mode becomes
Online Play, and Group Battle, Slash, Vs. CPU and Story go under one
Singleplayer row; Training stays on the top menu. The window's setting
`menu` (`rebushido`, as built, or `original`; `--menu` on the command
line), `include/port_top_menu.h`; docs/PLAYING.md, "The main menu".

**What the game does.** `src/port/top_menu.c` (a state file) holds the
two pages' rows, the page and the cursor, and a count of the times the
settings menu was asked for. At boot `main` asks `port_TopMenuBoot`
before the options page: with the switch on the page is not run, the
Bug fixes are the window's `bugfixes` and `g_rb_page_state` is done.
`menu_UpdateTop` (`menu_pages.c`) hands the frame to
`menu_UpdatePortTop`, which moves the cursor with the game's own nudge,
sets the game's row (`TOP_ROW`) to the one the cursor's row stands for
-- its picture, its caption, and what `menu_ChooseTopRow`, the game's
own cross and triangle, chooses for a mode's row, the second pad's
rules and the sounds included -- and draws the page as
`menu_DrawTopRows` does, its five rows in the places of the game's
first four and its last, without their names. Cross on Singleplayer,
Back, Online Play or Options is the port's: a page, or the count and
the page asked for (`port_TopMenuAsk`). The view is the side view
(`first_person` held at 0 there and in `fight_ApplyConfig`). The
game's Difficulty, Music and Monitor are the window's `difficulty`,
`music`, `monitor`, written into `g_game_settings` and the save's flags
at each frame of the top menu and as the options are fetched
(`port_TopMenuTakeOptions`); the save found at boot is loaded without
the question (`menu_Update`, `MENU_PAGE_CARD_CHECK`).

**What the window does.** `top_page.c` lays the rows' names, and the
two lines of the rows the game has no caption for, over the game's
page at the window's resolution, as `stage_page.c` does the stage
pages', from what `gamestate.c` reads out (`port_ReadTopMenu`); its
letters are drawn by one function, `port_TopPageText`. `sdl_menu.c`
follows the count of asks and opens the settings menu on its first page
(Options), or the online pages (Online Play: `online_pages.c`; a
session begins and ends there in the running program, `NETPLAY.md`,
section 15), both beside the running game, which draws its Option
Menu's backdrop under them (below, "The window's pages in the game's
look"). `top_page.c` letters the title's MAIN MENU too, and the pause's
and the after-fight menu's renamed rows.

**The tests.** The switch is off in the program without a window, and
in every run of a plan or `--frames` unless `--menu rebushido` comes
after: the play tests, port-rollback, port-net, the bench and the
provers press through the original's menus, options page included, as
before. In a session the host's switches hold (`PORT_PLAY_BIT_TOP_MENU`,
`_BUGFIXES` and the three options from `PORT_PLAY_OPTION_SHIFT`), and
the session's own presses to versus select find Versus as they found
Vs. Mode (`menu_GetTopRowMode`). Proved: a headless run of the window
boots to the top menu and reaches Singleplayer then Vs. CPU's select
screen, Training's page, Online Play's page and Options; port-rollback
of that plan with `--with=--menu --with=rebushido` is the same with and
without rollbacks.

## The window's pages in the game's look

The owner, 2026-11-20: "For the Online play menu, make sure sound
effects and music persist and try to standardize the GUI to be more
similar to the base game, not obviously a window drawn on top of the
emulation. Same for the options menu, integrate it into the game itself
like you would any other. Lets also rename 'Top Menu' and 'Sub Menu'
across the game to be 'Main Menu' and 'Character Select'."

**Beside the game, not over it stopped.** The settings menu
(`menu.c`, `sdl_menu.c`) and the online pages (`online_pages.c`) opened
over ReBushido's main menu or over a fight's pause are no longer a loop
of the window's with the game stopped: they open *beside* the game
(`port_PageCanBeBeside`: the main menu with `menu = rebushido`, or the
pause), which goes on running its own frames under them. So its music
goes on as the game plays it, a piece that ends starts again as the
game starts it, and nothing is silent or begins again on the way in or
out. While a page is beside the game:

- every event of SDL's is the page's (`port_PageTakesEvent`, from
  `port_ReadEvents`) but the window's own F2, F3, F9 and F12; what the
  keys and controllers hold is followed all the same, and nothing is
  given to the game (`port_MenuTakesPad` holds it all back until let
  go), so the game's frames under it take no press;
- the page has a turn at each blank (`port_MaybeRunMenu`): a test's word,
  the room's turn, the cursor's glow and the fade;
- it is drawn at every picture the window shows, as the stage pages and
  the match rules are (`port_DrawOverlay`, `port_PagePixels`), so it is
  the same over the console's picture and the remastered one, and with
  more pictures a second than the game's frames.

Anywhere else -- F1 in the middle of a fight, on a load, at the boot (the
tests' `--menu-at` and `--online-at`) -- the menu and the pages run as
before, with the game stopped at a blank (`port_RunMenu`,
`port_RunPages`), drawn the same way over its last picture
(`port_ShowPage`): there a fight would go on under them, and a replay
being recorded would keep frames no input made.

**The main menu covered.** Beside ReBushido's main menu, the page covers
it: the window writes `g_port_top_menu.covered` (`port_SetTopMenuCovered`,
at a blank's turn, where the game is still, as it writes the Bug fixes),
and the game's main menu (`menu_UpdatePortTop`) then draws the backdrop
and the wide panel of its own Option Menu (`menu_DrawPageFrame`,
`MENU_PANEL_WIDE`: the night sky, the clouds, the moon and the slate
panel) and nothing else, takes no press, and does not count the time to
its movie (`menu_Update`, `MENU_IDLE_FRAMES`, held at 1 as a held button
holds it). The window's rows over the main menu (`top_page.c`) are not
drawn while it is covered. The flag is cleared as the last page closes,
and at any blank where no page is up and no session is being set up.

**The look** (`page_look.c`, menu.h's `PORT_LOOK_...`), followed from
the game's Option Menu (`menu_DrawOptions`), the page the main menu's
Options row opened in the original:

- the title above the panel in white, centred, over the game's grey rule
  (`menu_DrawTitleRule`: a light line and two fainter);
- the rows in the panel in the dull gold the game's rows have laid half
  over its panel, (174, 174, 74); the cursor's row in yellow, (255, 255,
  49), swelling and fading as the game's cursor does (`MENU_PULSE`: once
  round in 32 frames, 47 either side of the plain tint, from an eighth of
  a turn, started again at each move); no bar under the cursor, as the
  game has none;
- a value that is set in white, as the game's chosen value is; one that
  is not the one built in in gold (the settings menu's own rule, kept);
- the help line under the rows in white, notes in grey, the keys under
  the panel in grey;
- a page comes up with a fade of eight frames (`PORT_LOOK_FADE_FRAMES`):
  the game's own pages change with a cut and a music fade
  (`MENU_PAGE_FADE_TO_MODE`), which a page of rows that change often
  does not want; the fade is the nearest to a page of the game's coming
  up, and the music is not faded at all, as the owner asked;
- the port's one typeface (Shippori Mincho; "The port's lettering"),
  the titles in ExtraBold, the rows in Bold, the rest in Regular, with
  the dark shadow down and to the right the game's lettering has.

Over the main menu the game draws the backdrop and the panel itself.
Over the pause (and wherever the game is stopped) the window draws them:
the picture darkened (0xD0 of black; the game's renamed rows of the
panel under it are left out) and the same panel, slate blue (0x4C,
0x4C, 0x6C) laid half over it, at the game's place for it (63 to
577 across, 48 to 224 down, of the game's 640 by 240). The online pages'
keyboard, a room's page and the meeting are laid out in the same panel.

**The sounds.** A press on a page plays the game's own menu sound: the
cursor's as the cursor moves or a value changes, the confirm's as a row
is taken, the cancel's on a way back (Back, Escape, Circle, F1's close)
-- `MENU_SOUND_CURSOR`, `_CONFIRM`, `_CANCEL`, played by `snd_Play` as
the game's menus play them (`port_TopMenuPlaySound`). The window asks for
one (`port_PageSound`, `port_AskMenuSound`, outside the state in
`play_switches.c`); the game plays it as it takes its next input
(`port_TakeInput`, `port_PlayAskedSound`), so it is one of that input's
effects: a frame run ahead or again plays none, and a replay being
recorded keeps it with the input (`SOND`, below).

**The meeting.** While two players meet (`online.c`, section 15 of
NETPLAY.md) the game is stopped at its boundary and the pages are drawn
by the session's waits. There, if the pages were beside the main menu,
the game's sound goes on by itself: `port_PageKeepSound` runs, for each
blank of the PC's time since the last (at most twelve at once; one a call
in a test's run), what a stopped console still does
(`port_RunStoppedSound`): the sound asked for, the music's step of
`cdrom_Update` alone every other blank (`cdrom_KeepMusicGoing`, so a
piece that ends starts again; a read of the disc is left for the
frames), the game's queue of sounds started (`snd_FlushQueue`) and a
blank of the chip and the drive (`port_RunSoundBlank`). It changes the
player's own state as their frames would have; that state is saved as
the session begins and put back as it ends, so the music goes on from
where the meeting left it, and the pages come back beside it. The
session's own game is from power on and plays its own sound.

**A replay of a fight paused with a page over it.** The pause's frames
are the fight's, and a page's sounds change the sound chip's state,
which the replay's hashes hold: a file without them would not play back
the same. So each input's sound is kept (`ReplayFrame.sound`) and
written as a chunk of its own, `SOND` (a count, then each frame's
number and its sound plus one, as words), which a file without one
does not have; the playback plays each as its frame's input is taken.
The format's version is unchanged: an older file has no `SOND` and no
sound, and an older program skips the chunk.

**Main Menu and Character Select.** What a player sees says Main Menu
where it said top menu, and Character Select where the game says Sub
Menu (the owner, 2026-11-20):

- our own: the main page's Back row's caption ("To the main menu"), the
  online pages' and the session's words, the Updates page, the
  settings' labels and help (`menu`, `english`: "English main menu"),
  `--help`, the English mod's name, the players' README and
  docs/PLAYING.md;
- the game's own lettering, with ReBushido's menus (`menu = rebushido`;
  `original` keeps the game's words, as it keeps its menus): the main
  menu's title draws its big line alone (`menu_DrawTopTitle`, the
  sheet's top 17 rows, which are textures of their own keys: the English
  mod has its "Select a Trial" for them too, `tools/port_english.py`,
  `TITLE_LINE`) and the window letters "MAIN MENU" under it in the
  game's teal capitals with their dark edge, spaced over the span of the
  game's "TOP MENU" (`top_page.c`, `port_TopPageSmall`); the pause's and
  the after-fight menu's last two rows, "Return to Sub Menu" and "Return
  to Top Menu", are left out by the game (`hud_DrawPanel`) and their
  places noted (`port_TopMenuNotePanelRow`: the frame, the panel, the
  cursor, each row's y), and the window letters "Return to Character
  Select" and "Return to Main Menu" there, centred, white where the
  cursor is and half laid over the panel where it is not, as the game's
  semi-transparent rows are, narrowed to the panel where the line is
  long (`port_PanelRowsPixels`). The training page's and the Group Battle
  page's "Return to Top Menu" the same way (`menu_DrawPractice`,
  `select_DrawTeamRules`, `port_TopMenuNoteLine`; `port_PageLinesPixels`):
  "Return to Main Menu" at the left of the game's line, in each page's
  look (the training page's yellow with a shadow, the Group Battle
  page's with a dark edge), dimmed and half laid over where the game's is
  dim. All over either picture. In Training, Return to Character Select
  still goes back to training's own page first, as the game's Sub Menu
  did;
- not renamed: the code's names (`port_TopMenu...`, `g_port_top_menu`,
  `PORT_TOP_...`, `top_page.c`, the game's `menu_...`), the session log's
  lines that tests read, and the game's options page's "Return to Top
  Menu", which ReBushido's main menu never shows. Renaming the port's
  names would touch every file of the main menu, the match rules and the
  online flow beside another agent's work for little.

**The battle type's page** (the match rules' page before the first
character select, `match_page.c`, `port_MatchRulePage`) is drawn in the
same look: the select screen under it darkened twice over (its lettering
is bright), the wide panel, the title "Battle type" over the rule, the
rows' names in the gold and the row under the cursor in the yellow, each
value in white with `<` and `>` beside the cursor's, the rule's two lines
in white and the keys under the panel. Its presses were the game's own
menu sounds already (`port_MatchRulePageStep`, played by the game).

**A new page in this look**: drawn by the window, it is a `PortLook`
(`port_LookBegin`, `port_LookPanel` with the backdrop its own unless the
game draws it, `port_LookTitle`, `port_LookText` with `PORT_LOOK_ROW`
and `port_LookPulse` for the cursor's row, `port_LookFade`), laid over
the 4:3 area as the others are (`port_DrawOverlay`), with
`port_PageSound` at each press; opened beside the game where it waits
for the player, given no press, as `port_PageCanBeBeside` and
`port_MenuTakesPad` do for these. Drawn by the game, the page asks
`menu_DrawPageFrame(ctx, MENU_PANEL_WIDE, db)` for the Option Menu's
backdrop and plays `snd_Play(MENU_SOUND_...)` as its menus do, and the
window letters it in the same `PORT_LOOK_` colours.

**Proved** (`tools/port_menus_test.py`, a step of the gate; Windows, or
the Linux window from WSL), the window hidden with SDL's dummy drivers,
`--fast`, `--wav`:

| Case | What runs | Held |
| --- | --- | --- |
| `menu` | the main menu: Singleplayer and back, down to Online Play; the online pages beside it (down, down, up, Host a room, back, back); Options: the settings menu beside it (down, down, up, a group, back, close); Online Play again, Join by address, Host: the meeting, 300 of its turns, given up | the game's own confirm, cancel and cursor sounds noted from its own presses (vab 0, program 0, tones 1, 2 and 0); the pages and the menu open beside the game for 59 blanks each, each playing exactly 3 cursors, 1 confirm and 2 cancels, the game's own, and nothing else; the music under them as loud as before them (7,340 and 8,058 RMS against 5,271); not one gap of 0.1 s from the main menu's music on to the meeting's end (the longest: none); 301 blanks of sound made while the game stood still in the meeting |
| `pause` | a versus fight (port_game's), paused at its 120th frame; the settings menu over the pause (down, down, up, a group, back, close); Escape, and the fight on to its kill | 6 sounds over the pause; the fight's replay keeps them (`SOND`, 6) and `--replay-check` plays it back with every hash the same; the same file with `SOND` taken out does not (it ends 1): the sounds are what keeps it the same |

The pictures, before and after, of every online page, the settings menu
from the main menu and from the pause, and the renamed rows, in both
pictures: the main checkout's `tmp/menus/`, kept for the owner and not
committed.

## Updates

The Windows package updates itself from GitHub Releases of the public
repository (`src/port/window/update.c`, `update_core.c`,
`update_apply.c`, `update.h`; for players [PLAYING.md](PLAYING.md),
"Updates"; for the owner [BUILD.md](../BUILD.md), "Publishing a
release"). Only the build with a window has it, and only the one for
Windows asks the network; none of it is the game's state.

**Where updates come from.** `GET
https://api.github.com/repos/tgg12321/ReBushido/releases/latest` (never a
prerelease or a draft), or `/releases?per_page=10` with `update_channel
= beta`, through WinHTTP (the system's own TLS and proxy, no OpenSSL),
with a five-second timeout. The answer's `browser_download_url`s name
the release's assets: `update-manifest.txt`, its signature
`update-manifest.txt.sig`, and the zip of `make port-release`.

**The manifest** (`tools/release_sign.py` writes it), text, a line each:

```
rebushido-update 1
version = 0.2
commit = abc1234
channel = release
updater = 1                      the least PORT_UPDATER_VERSION that installs it
zip = ReBushido-PC-0.2-abc1234.zip
zip_size = 28123456
zip_sha256 = <hex>
folder = ReBushido-PC-0.2-abc1234
note = a line of the release notes        (any number)
file = <sha256> <size> <path>             (every file of the zip's folder)
```

**Security.** The `.sig` is the hex of an Ed25519 signature (RFC 8032) of
the manifest's bytes, made with the release key, whose public half is
built in from `config/release_public_key.txt` (`tools/port_sdl_build.py`
writes `update_key.c`; `none` builds a program that never updates).
The program checks it with Monocypher 4.0.2's `crypto_ed25519_check`
(`src/port/vendor/monocypher/`, CC0 or BSD-2, as shipped, cross-checked
against the tag's sources) before it reads anything else of the
manifest; then the zip's size and SHA-256 as it downloads, then each
file's SHA-256 and size as it unpacks (the port's own inflate,
`port_Inflate` of `src/port/png.c`), refusing a file the manifest does
not name, one it names twice, one missing, and any path that is not a
package's (`..`, a drive, `\`, a setting, card, log, disc, `logs/`,
`screenshots/`, `update/`, `made-from-disc.txt`:
`port_UpdatePathIsOurs`). The step that installs checks the signature
and every staged file again. HTTPS is not what is trusted. A trial
build (`-dev`) takes `--update-url URL` and `--update-key HEX` for the
tests; a release refuses both, and its install step uses the built-in
key whatever it is given. `make port-release RB_RELEASE=1` refuses a
`none` key, and `tools/release_publish.py` refuses to sign with a key
whose public half is not the config's.

**When.** At a start, if it is a day since the last answer
(`update/state.txt`), on a thread of its own at a low priority, after
the window has started: the start never waits for it. Not at all in a
program started into a session (`--host`, `--join`, `--lobby`), in a
test's run (`--frames`, `--plan`) unless it names `--update-url`, with
`--prepare-only`, or by itself in a trial build. `updates = off` asks
only when the menu's **Check for updates now** is chosen. The download
waits while a fight is on (`port_UpdateWatch`, from the window's watch of
the game) or online play's pages are up (`port_UpdateSetOnline`, which
the settings menu's Play online page calls and online play's own pages
are to call), and is given up if one begins; nothing is shown or
installed meanwhile.

**Installing** (`update_apply.c`). The zip is unpacked into
`update/staged/`, and `update/ready.txt` and `ready.sig` (the manifest)
are written last. At the next start (`updates = auto`), or at **Restart
and install** (`ask`), the program copies itself and `SDL2.dll` into
`update/apply/` and starts the copy as `--update-apply PID KEY -- WORDS`
and ends. The copy waits for PID to end, checks everything again, and
for each file of the manifest: leaves it if the folder's is already that
file; otherwise moves the folder's into `update/old/` (a running
program's file can be renamed, never written over) and the staged one
in, `MoveFileEx`, retried for a few seconds where a scanner holds one.
A file the old version's `package-files.txt` names and the new manifest
does not is set aside too, if it is still the old version's bytes. Then
it starts the new program with WORDS and `--update-started`, which writes
`update/started.txt` once its settings and disc are read (before the
disc's mods are made, which may take minutes). If a move fails, the
program does not start, ends without that file, or says nothing in a
minute, every move is undone in reverse and the old program is started
with `--update-rolled-back VERSION`, which keeps `failed = VERSION`
(that version is not downloaded by itself again) and says so on the top
menu. Nothing else of the folder is ever touched: the player's files are
named by no manifest. The disc's mods are made again by the new version
at its first start (their key has the version, `prepare.c`).

**Tests** (`tools/port_update_test.py`, Windows, the disc, no network):
a server of its own on 127.0.0.1 answering as the API does, a key made
for the run, the build's program installed in `build/tests/port_update/`
with a player's files beside it, run with SDL's dummy drivers,
`--hidden`, `--no-sound`, `--no-cards` and `--frames`. The newer version
is the same program with its version text one more, a README changed, a
file added and one dropped. Ten cases: up to date, newer (installed and
started, player's files intact, the dropped file gone, nothing left in
`update/`), bad signature, bad zip hash, bad file hash, truncated,
offline, timeout, rollback, off (no request at all).

## Replays

ROADMAP 23; the owner, 2026-11-20: "automatic replay saving, ideally in
a format that is lightweight, like saving inputs and re-creating replays
on the fly". How a player uses them: [`PLAYING.md`](PLAYING.md),
"Replays". The code: `src/port/replay.c` and `include/port_replay.h`
(outside the state), the compressor `src/port/deflate.c`, the window's
part `src/port/window/sdl_replay.c` and the menu's page
`menu_replays.c`.

**What a replay is.** The port is deterministic (the play tests,
port-rollback, netplay's hashes), so a fight is its first state and its
inputs. A fight is from the first frame's boundary whose screen is the
fight (`SCREEN_FIGHT_UPDATE`) to the first that is not; the pause menu is
inside it. Its file begins one boundary sooner, at the last frame out of
a fight, before the step that loads the fight: the state at every
boundary out of a fight is saved (`port_StateSave`, NETPLAY.md section 7:
the pages written since the last) with its hash, and where the next is a
fight's first, that one is the start. So the playback loads the stage
and the fighters itself, and everything the picture makes of a stage as
it loads (the ground's mesh, the stage's textures noted for the HD ones,
`port_RemasterStageChanged`) is made for the replay's stage. From there
every input the game takes
(`port_TakeInput`, after the take hook: the pads, a network's, a
session's own presses) is kept, with what the game read of the picture
for that frame, and the state's hash every 30 frames (once a second of a
fight). As the fight ends the file is written. Playing it back puts the
start in at a boundary, gives the game the inputs one by one through the
take hook, holds every hash to the recorded one and ends at the last
frame, where the state the game had before the playback is put back.

**Why a start state and not the inputs from power on.** A fight's state
hangs on everything before it in the session (the cards, the CPU's
record, every menu frame), so inputs from power on would mean running a
whole session again, minutes of it, to watch its last fight; the start
state makes each file whole by itself and quick to begin (well under a
second to read and put in). The online agent's pristine boot snapshot
(for sessions with no restart) is a run's own and is not needed here.

**The size.** The state is 4.8 MB (7.2 MB in the window, with the port's
primitive buffers). Three things make a fight's file about 215 KB:

- *What the disc holds is not written.* Most of a fight's start is what
  the game read from the disc as it loaded the fight: the stage's and
  the fighters' files in RAM, the sounds in the sound chip's memory
  (measured: 188 of 214 probes of RAM's loaded part, and 84 of 85 of the
  sound memory, are bytes of the disc). The last 4,096 sectors the game
  read (`port_ReplayNoteSector`, `cd.c`) are indexed by a hash of each
  64 bytes, the snapshot is looked through four bytes at a time with a
  rolling hash, and each run of the disc found of 256 bytes or more is
  cleared and noted (DISC: where, which sector, which byte, how long).
  Playing back reads those from the player's own disc, which the disc's
  sum holds to be the same. The disc as it is: a mod's sectors follow the
  game's state (the arena in play) and are written with the rest.
- *What is dead is cleared*: the samples of the drive's queue that are
  not waiting to be played (`port_XaClearPlayed`), and the `run` group's
  variables (below).
- *Deflate* (zlib's stream, RFC 1950/1951), written by the port itself
  (`deflate.c`: hash chains, a code of each block's own, a block stored
  where shorter), since the program with no C library cannot link zlib.
  `png.c`'s inflate reads it back (`port_Inflate`). In the window the
  writing (the index, the look through, deflate) is done in pieces of
  about a millisecond, as many at each boundary after the fight as fit
  in 2.5 ms, so that no frame waits on it; the program with no window
  does it at once. Measured in the window, port_game's fight: 57
  boundaries, 188 ms in all, 3 ms the most at one. The folder's total
  for `replays_megabytes` is read once a run and kept up after.

Measured, `port_game`'s versus fight (568 frames with its load, 19
seconds): 4,804,204 bytes of state, 1,449,480 of them in 581 runs of the
disc, the start 205,859 bytes compressed (with the start at the fight's
first frame and nothing taken out for the disc it was 1,369,159; zlib -9
of that whole snapshot, 1,579,787), the inputs 271 bytes, the hashes 160:
**216 KB**. The window's (7,163,744 bytes of state): 211,179. The fights
of `tools/port_stage_select.py`'s plans: 215 to 217 KB, and 463 KB for
the second fight of `vscpu_next`, whose start is the first fight's end,
less of it bytes of the disc as they are.

**The file** (`.rbr`): `RBREPLAY`, the format's version, then chunks of
a tag of four letters, a size and the bytes, little-endian. META (text,
a line `key value` each: the date, the version, the mode, the stage, each
fighter's roster number, weapon and whether the CPU's, the kind of stage,
the arena, the time of day, the fight's music, online and whose machine,
Bug fixes, the winner, the frames, the frame of the first cut, the hash's
period, the input's number at the start, the start's hash, and the
build's, the disc's and the gameplay mods' sums), PIEC (the snapshot's
pieces, a name and a size each), STRT (the start's size and its zlib
stream), DISC, VIEW (each kind of game view once), INPT (runs of frames
alike: which of the two pads' buttons, which pads answered and the view
changed, the new values, then how many frames, a varint), HASH, END. A
fight's inputs are a few hundred bytes.

**Held to the program.** A replay plays only where the build's sum
(netplay's, session.c, but for the state's size without the primitive
buffers), the disc's and the gameplay mods' are the ones recorded, and
the snapshot's pieces are laid out alike; the port's primitive buffers
(a draw distance past the console's at the run's start) are needed where
the fight had them, and kept as they are where it had none. Otherwise the
page, `--replay` and `--replay-check` say why. The start's hash is checked
as it is put in: a start that is not the one recorded stops the playback
there.

A replay is watched from a menu, never in the middle of a fight
(`port_PlayReplay` says so): the picture's stage is the one loaded last
until the next load, so the fight that was being played would go on with
the replay's.

**Another run's state.** NETPLAY.md, section 7, said a snapshot is never
written to a file: the host's pointers among the variables are a run's.
A replay is put in by another run of the same build, so what is a run's
own is kept out of the load and put back after it:

- the files of the new group `run` of `config/port_state.txt` (a play
  test's plan, `plan.c`), state as ever for rollback, but linked last in
  each kind between the marks `g_port_run_data` / `g_port_run_bss` and
  the state's end (`tools/port_build.py`, `port_sdl_build.py`); the file
  has them cleared;
- the hooks the host set (`g_port_take_hook`, the device's, the blank's,
  the sound's, the disc's and its override, the plan's press, the drive's
  unmixed samples);
- the memory cards: another run's slots hold its paths and file handles,
  so each slot that had a file is kept in memory from the start's header
  block (`port_CardsForReplay`), and nothing is read from or written to
  a card file until the game's own state is put back;
- the blank limit of a run with no plan, which the fight's clock is past
  (`PORT_NO_BLANK_LIMIT`).

**What the game reads of the picture.** The draw distance and the left
margin are the two things of the picture the game's own computation
reads (NETPLAY.md, section 13), and the player may change them in a
fight (F2). The game now reads them as they were at the step's boundary
(`port_RemasterHoldGameView`, every boundary): a change at a blank in the
middle of a step counts from the next. A replay keeps each frame's
(VIEW) and holds it while playing, so a fight played in Remastered at a
far draw distance plays back as it was in Original, and the picture is
the player's own.

**Online.** Netplay's frames call the boundary too, reruns included: the
start is taken again where a rollback goes back past it, every input and
hash is written again as a frame is run again, and the file is written
only when every input to the fight's end is known and nothing is left to
run again (`port_NetplayFinalFrames`). Both machines' files of a fight
have the same inputs and hashes. A fight a player leaves in the middle of
is not kept.

**Not kept**: a fight in which the settings menu's Bug fixes was changed
(`port_SetBugfixes`: a change no input made), a fight longer than
2^18 frames, a link cable's fight (the other console is not an input),
a fight while a replay plays. A fight the program ends in the middle of
is written as far as it went (`port_ReplayAtQuit`, `unfinished 1`).

**Playing back.** In the window the settings menu's Replays page asks
for one (`port_PlayReplay`); it begins at the next boundary in main's
loop: the state is saved to come back to, the start put in, the take
hook set. Each boundary holds the frame's view and hash; pause and the
slower speeds wait in the window's hook (its events read and its picture
shown again a blank at a time), the faster ones and the skips (from the
start, to 90 frames before the first cut) run frames quietly as netplay
runs a rollback's (`g_port_rerun`). `--replay FILE` plays one from the
first boundary and ends; `--replay-check FILE` plays it quietly, with a
restart from its middle and a skip to before the first cut on the way
(as the keys ask for them), and ends with 0 if every hash, each time,
was the one recorded. Headless runs record nothing
unless `--replays FOLDER` says where (`--replays-keep N`); the window
records into `replays/` beside the program unless it is a test's run
(`--plan`, `--frames`). `replays_keep` and `replays_megabytes` take the
oldest away after each one written.

**Proved** by `python3 tools/port_replay.py` (WSL, the disc; no
emulator): port_game's plan and four of `tools/port_stage_select.py`'s
(the stage select's arena and rematch, versus the CPU through two fights,
training, versus the CPU with `--stage-select --unlock-all`) recorded by
both sizes, every file played back by both: the size that recorded it
to the end with every hash the same, the other refusing it as another
build's; the two sizes' files of a fight have the same inputs. And
`tools/port_net.py session`: both machines of a session keep the fight,
their files have the same inputs, hashes and views, and each plays back
headlessly with every hash the same. The window's program recorded
port_game's fight with `--replays` and played it back with
`--replay-check`, every hash the same.

**A replay's start for a room's spectators** (ROADMAP 22; NETPLAY.md,
section 16). The host of a room of several's match serves its
spectators the start of the match's latest fight (`port_ReplayServe`):
the fight is recorded whether fights are written to files or not, and
its start is compressed as soon as no rollback can take it back
(`port_ReplayIsFinal` of its first input), a slice at each boundary as a
file's writing is, into the replay's own chunks with a `SERV` chunk of
its input's number, netplay's frame and the start's hash. The file
written after the fight takes the same work (`WORK_DONE`: deflate is not
run twice). A spectator puts it in as a playback puts its file's start
in (`port_ReplayLoadServed`: this run's own kept, the hash held to the
host's) and runs the match on from there (netplay.c, `port_RunWatch`).

## Rooms of several

ROADMAP 22; how they work on the network, NETPLAY.md, section 16; how a
player uses them, PLAYING.md, "Rooms of several". The code:
`src/port/room.c` (the room's client: its own socket, the roster, what a
member asks), `src/port/watch.c` (a match's feed and a spectator's side),
`include/port_room.h`, all outside the state; the room's matches as
sessions in `online.c` (`port_OnlineRoomRun`, `port_OnlineWatchRun`),
the spectator's frames in `netplay.c` (`port_RunWatch`), the match's end
in `session.c` (`PORT_SESSION_RULE_ONE_MATCH`) and the room's page in
`src/port/window/online_pages.c`.

- **A room's match is a session** begun in the program (section 15 of
  NETPLAY.md), met through the lobby under the match's code and let in
  by its ticket, with no lobby before the fight. Its host's rule
  `PORT_SESSION_RULE_ONE_MATCH` ends it where the match is decided: the
  game's menu after the fight (`SCREEN_AFTER_FIGHT_UPDATE`), of a single
  fight or of a Stock or First to match's last (`g_port_match.over`).
  The session's presses note the frame and who won in the state
  (`g_port_top_menu.session_left`, `.session_result`), so that a rollback
  takes them back and both machines end at the same frame
  (`PORT_NETPLAY_END_MATCH`); each player tells the room the result.
- **Leaving a fight**: the window's F1, Escape in a fight (where it is
  Start, nobody's online) or the guide button ask to leave
  (`port_OnlineLeave`, `port_NetplayAskLeave`); the line says so, and a
  second ask within three seconds leaves (`PORT_NETPLAY_END_QUIT`): the
  other is told, its session ends as one whose other player left, and
  both are back at their own games and pages. `--leave-at N:F` does it
  for a test.
- **A spectator** runs the match's frames with its players' inputs, no
  guess and no rollback: every frame as it comes, `WATCH_LAG` frames
  behind the newest it has, and quietly (`g_port_rerun`) while it is more
  than `WATCH_CATCH_UP` behind, as a member who came in the middle of a
  fight is. Its own game is kept and put back after, as a session's
  player's is; it holds the match's settings as its players do
  (`port_WatchSession`: the host's switches, rule and margin).
- **The command line** (`--room-open NAME`, `--room-enter CODE`,
  `--room-rule`, `--room-watcher`, `--room-no-watch`, `--room-matches N`,
  `--room-side open|join`, `--net-plan-pads`) makes a program a room's
  member with no pages, for the tests; its own game idles between
  matches. `--room-size 4` opens a room whose matches are of four.
- **A match of four** (NETPLAY.md, section 18) is a session of four PCs:
  its first player hosts it, each other meets him at the seat of its
  place in the match (`port_SetRelaySeat`, the ask's `seat`); its result
  is its teams' (the first and third players against the second and
  fourth, `port_OnlineMatchEnds`), and a player who leaves it loses it for
  his team (`port_NetplayLeftPad`).

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

### Where the program's variables are

**An ordering table's link is the low 24 bits of an address** (a
primitive's first word, as on the console), so every place a primitive
may be has to have low 24 bits no other such place has: the port follows
a link by finding the one place it falls in (`port_AddLinkRegion`,
`src/port/gpu.c`), and stops the program, saying so, where it falls in
two or none. Four places are fixed (`include/port_platform.h`): the
console's RAM (`0x80000000`, 2 MB: low bits `0x000000` to `0x1FFFFF`),
the scratchpad (`0x1F800000`, 1 KB: `0x800000`), the game's stack
(`0x7FC00000`, 1 MB: `0xC00000`) and the port's own primitive buffers
(`0x7ED00000`, 2 MB: `0xD00000`). The others are **the game's state's two
runs of variables**, its data and its bss (from `g_port_game_data` to
`g_port_state_data_end`, from `g_port_game_bss` to
`g_port_state_bss_end`: `include/port_state.h`), which are also what a
snapshot saves (`NETPLAY.md`, section 7). Nothing else of the program
holds a primitive: what cannot be written cannot be linked, and what is
outside the state (`config/port_state.txt`: the remastered side channel,
the window's, SDL's) is the picture's and the host's.

So only the state's variables are held to the 24-bit space, and between
the RAM and the scratchpad they have 6 MB of it. Every program is linked
at `0x08200000` (`PROGRAM_BASE`, `tools/port_build.py`; the window's
builds take it from there), and its state begins past the RAM's 2 MB.
Each build works out where the two runs fall against the four places
(`port_build.check_link_space`) and **fails, naming the run, the place
and by how much**, where they would meet; where they do not it says how
much room is left. In November 2026 that was 2.8 MB for the headless
programs, 2.4 MB for the window's on Linux and about the same on
Windows, where the MinGW object puts the zeroed variables among the
data. A state that outgrows it: put a large buffer that holds no
primitive outside the state, or in memory the system gives, or move
`PROGRAM_BASE` (the band from the scratchpad to the stack has 4 MB
more, but a run may not cross the scratchpad's kilobyte).

Until then the whole of the program's variables was one place, with the
remastered side channel's 2.4 MB in it, and the window's builds were held
to `0x08200000` to `0x08800000` with 70 KB to spare; the headless
programs, at `0x08400000`, had already reached past the scratchpad's
place with variables that hold no primitive, and nothing said so.
Nothing else depends on where the variables are: below 4 GB is all the
64-bit program needs ("The design"), the 32-bit one the same, and the
snapshot saves the two runs wherever they lie.

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
