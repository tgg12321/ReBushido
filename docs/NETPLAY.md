# Online play with rollback

What the native port ([`PORT.md`](PORT.md)) has and lacks for online play
with rollback netcode ([`ROADMAP.md`](ROADMAP.md), item 8), measured on the
port as it is, and the design that follows. Of the design, the save, load
and hash of the state are built, with a self-test that runs on demand
(section 7); the rest is not.

**In short:** the game step is already deterministic and repeatable from its
inputs alone, and a whole snapshot of the machine is small and quick to
take. A probe saved the state in a fight, ran eight frames, put the state
back and ran them again, eleven times through a fight to the kill. All 88
frames run again came out the same to the byte, in both sizes of the
program, and the run's trace was the same as an untouched build's. What is
missing is control and separation, not determinism. The game owns its loop,
the step draws and plays sound as it goes, and the state is not yet told
apart from the port's picture and its other output. The game also has
online play of its own, the link cable's mode, which settles most of how a
match is set up.

## How it was measured

A probe was built for this, and then thrown away: a file in `src/port/` and
a call after main's `ResetRCnt` (`src/main/main.c:663`). It ran with
`port_game`'s plan, headless (`build/port/rebushido --plan … --no-cards
--trace`), through the menus, character select and a versus fight of 479
frames to the kill. At every frame of the fight it compared memory with the
frame before. Every 40 frames it copied a whole snapshot, let 8 frames run
while it noted a hash of every 4 KB page, put the snapshot back at the
ninth frame's boundary and let the same 8 frames run again. The machine was
a Ryzen 9 9950X3D (TSC at 4.3 GHz) under WSL, with other builds running, so
the times are rough. The port is compiled at `-O1`, with every function of
the game instrumented and no vector instructions (`tools/port_build.py`),
so they are also pessimistic.

## 1. Determinism

**What the step depends on.** The game reads nothing but its own memory,
the disc image and the pads:

- **Time** is the port's own count of scan lines, moved only by the game
  (`src/port/clock.c`). Nothing reads the PC's clock. The wait at the end
  of main's frame, which stirs `rand()` on the console for as long as the
  frame had left, is a fixed 384 calls in a fight frame (`main.c:654`).
  The window paces blanks against the sound card (`src/port/window/
  pace.c`) only by sleeping after a blank has been computed.
- **The disc** answers at once: a read's sectors all arrive the next time
  the game asks the drive (`src/port/cd.c`; PORT.md, "The disc"), so a load
  takes the same frames on every PC. XA music moves the drive's position by
  the sound's clock, which is itself a count of blanks.
- **Random numbers:** `rng_Next` (`main.c:575`) and Sony's `rand()` are
  both plain state in memory. In a versus fight `rand()` feeds only
  effects and the HUD's noise (`eff.c`, `hud.c`); the CPU and a new
  opponent's place use it in the other modes (`cpu.c`, `fighter.c:705`,
  `fight_modes.c:66`).
- **Memory at address 0:** every place that reads or writes through a
  pointer the original never set has been given a fixed answer (PORT.md,
  "Memory"). A new one stops the program instead of reading whatever
  happens to be there, which is the right failure for netplay.
- **Floating point:** none in the step. The game is integer code, the
  port's own files have no `float` or `double` outside `src/port/gl/` and
  `src/port/window/`, and the port is built with `-mno-sse -mno-mmx -fwrapv`.
  The order in which C works out an expression's parts is pinned where it
  matters (`tools/port_order.py`).
- **32 and 64 bits:** the same plan gave the same 2,338-line trace in two
  64-bit runs, one 32-bit run, and the unprobed build of the main checkout.
  That held with eleven rollbacks in the probed runs, so going back and
  running again changes nothing a trace sees.

**What would differ between two PCs.** Not the arithmetic. What differs is
what each player brings:

- the **build**: the 64-bit and 32-bit programs put different addresses
  in RAM (a `PTR32` slot holds a variable's address, and the variables are
  laid out differently), so two peers must run the same program. A Windows
  and a Linux build of the same source should agree on values but would
  have to be proved to before they are allowed to meet;
- the **disc image**: it must be the same US disc, checked by hash;
- the **memory card**: unlocks, the button configuration
  (`g_saved_record.button_config`) and records are read at boot;
- **ReBushido's settings**: "Bugfixes" changes how a fight plays
  (`g_rb_settings.bugfixes`), and so may later settings;
- **anything uninitialised**: the original reads some locals it never set
  (`main`'s `skipped` before `RB_FIX_FIRST_FRAME_PAD`). Such a read takes
  whatever an earlier call left on the stack or in a register, which
  depends on the compiler. It is a risk between two different builds, not
  between two runs of one. No difference was seen in 88 frames run again.

## 2. State

The state is all in four places, every one at a fixed address in one
process:

| Piece | Size | In a fight |
| --- | --- | --- |
| The console's RAM, `0x80000000` (`src/port/memory.c`) | 2 MB | 116 KB written: the two primitive buffers (`SYS_PRIM_BUFFER_0/1`, 104 KB) and the effects' area (`SYS_EFFECTS_ADDR`, 11.6 KB). The stage, the models and the motions are loaded before the fight and only read during it |
| Scratchpad, `0x1F800000` | 1 KB | used every frame |
| The sound chip's registers, `0x1F801C00` (`include/port_spu.h`) | 512 bytes (a 4 KB page) | written by libspu every frame |
| The program's variables, `g_port_data_start` to `g_port_data_end` (`tools/port_build.py`) | 3.73 MB (64-bit; 3.56 MB in 32-bit) | 881 KB written, below |

The game's stack (`PORT_STACK_ADDR`) holds no state between frames at
main's loop. The port's remastered primitive buffers (`PORT_PRIMS_ADDR`) are
the frame's output.

What the fight wrote in the program's variables, in 64-byte blocks: the
software GPU's video memory 647 KB (`g_port_vram`), the XA reader's buffer
131 KB (`g_port_xa`), the display buffers and ordering tables 26 KB
(`g_disp_db`), the output buffer 16 KB, and 9 KB of the 276 KB arena
(`g_arena`). After those come 6.6 KB of the 515 KB sound chip
(`g_port_spu`: voices, not samples), motion decoders 3.7 KB, fighters
1.8 KB, and 534 other small variables with 30 KB between them. A frame
changes on average 34 KB of RAM in 12 pages and 115 KB of variables in 151
pages. Not one `LoadImage`, `StoreImage` or `MoveImage` was called in a
fight frame (356, 4 and 2 in the whole run, all in menus and loads), so the
game never reads video memory back during a fight.

**A full snapshot** is RAM, scratchpad, the sound registers and the
variables: **6.0 MB**. The probe copied it in 0.7 to 1.2 ms and put it back
in 0.8 to 1.2 ms. That was a plain loop of 8-byte copies at `-O1`, while
other builds were running. A library `memcpy` should be several times
faster. Eight frames of snapshots are 48 MB.

**The first test failed, and showed what counts.** Before the sound
registers' page was in the snapshot, 48 of the 88 frames run again
differed, all in `g_port_spu` and the mixer's buffers in `sound.c`. With
that page saved, nothing differed. A snapshot that leaves out a piece the
emulation keeps outside the variables fails in this way, and the same test
finds it.

**The minimum for a fight** leaves out video memory (1 MB), the remastered
side channel (`g_face_slots`, `g_point_slots`, `g_anchor_slots`: 1.2 MB),
the plan and trace (`g_port_checkpoints`, `g_port_presses`,
`g_port_values`: 0.35 MB), the output buffer and the movie's buffers. That
is about 3.4 MB, of which RAM is 2 MB that barely changes. A snapshot
of what changes since the fight's first frame is under 300 KB, but that
only helps if copying 3.4 MB ever turns out to be too slow, and on this
machine it is not.

## 3. Separation

**Where the step draws, sounds and reads.** One turn of main's loop
(`main.c:624`) is a frame:

1. `pad_Update` reads the pads; `cdrom_Update` steps the drive's state
   machine; `snd_FlushQueue` keys on the sounds queued last frame
   (`snd.c:598`);
2. the screen's handler runs the fight and builds the frame's primitives
   (`fight.c`, the renderer), noting the side channel's identities on the
   way (`port_RemasterNote*`);
3. the wait, `VSync`, `ResetRCnt` (`main.c:663`): each blank on the way
   fills the pads (`clock.c:70`), takes the card's turn, mixes a blank of
   sound and runs libsnd's tick (`sound.c:471`), then calls the window's
   hooks, which pace and present;
4. `DrawOTag` hands the frame to the GPU (`main.c:700`), and Start opens
   the pause menu, a loop of its own (`main.c:706`, `fight_RunPauseMenu`).

Per frame of the fight the probe measured roughly 1.1 to 1.4 ms in the
handler, 2.1 to 2.2 ms rasterising (`DrawOTag`), and 2.6 to 3.1 ms mixing
the two blanks' sound. In 32-bit the sound took 1.7 ms. A frame run again
with nothing drawn therefore costs about 4 ms today. Most of that is the
sound chip, and all of it is at `-O1` with instrumentation.

**What a frame run again must not do:** draw (`DrawOTag`, the software GPU,
the OpenGL backend, `gl_tween`'s recording), write the side channel, send
samples to the sound card, sleep in the window's pacing, read SDL's keys,
or write the trace and the WAV. It must still build the primitives (step 2
cannot be split from the fight without study: cloth and stage fog live in
the renderer's state), still step the drive and the sound chip if the sound
is rolled back, and still let the blanks pass on the port's clock. Each of
those is either a hook the port already calls through a pointer
(`g_port_input_hook`, `g_port_blank_hook`, `g_port_sound_hook`) or one
flag in `gpu.c` and `remaster.c`.

**Sound.** The game never reads the sound chip for anything but which
voice is free (`SpuGetAllKeysStatus` in `snd_FlushQueue`), so what is heard
does not feed back into the fight. Two ways, in order:

- *First: roll the sound back with everything else.* This is what an
  emulator's rollback does. A frame run again mixes and throws the samples
  away; the card hears the corrected state from the next blank on. A
  sound started in a frame that is taken back stops short, and one the
  correction adds starts a few frames in. Correct by construction, and
  the cost is the mixing.
- *Later: keep sound out of the rollback.* `snd_FlushQueue` is the one
  place a frame's sounds become key-ons. Record its queue per frame. In a
  frame run again, empty the queue without keying anything on. When the
  frames are final, key on what the correction added and was never
  played, and let what was wrongly played fade. The chip, libsnd's and
  libspu's variables and `sound.c` then leave the snapshot, and a frame
  run again stops paying for the mixer. Music needs nothing: it does not
  depend on input, so a rollback ends where the first run did.

**`gl_tween` and the side channel.** The tween draws between the frame on
show (F-1) and the next frame (F), which is worked out before F-1 leaves
the screen (PORT.md, "More pictures a second"). Rollback keeps that rule,
with one change. A correction that arrives while F-1 is on show
recomputes F, so the tween's next frame can be replaced half way. The
picture then jumps to the corrected F, as every rollback game's picture
does. The tween must take its frames only from steps that are shown, never
from frames run again, and must accept a new next frame for the one it is
moving towards. Its pairing by identity is safe across a rollback: an
identity is the address of a scene node or stage tile, and those arrays do
not move.

The side channel is the risk. Its epoch (`g_epoch`, `remaster.c:71`) and
slots are among the program's variables, so a snapshot of the whole region
rewinds them. A slot written during a frame that was taken back keeps an
epoch the corrected frame will use again. The probe saw exactly that:
`g_point_slots` differed after some rollbacks while nothing of the game
did. **The side channel's epoch must only ever go forward, and its slots
must stay out of the snapshot.** The same holds for any per-frame counter
the picture keeps.

## 4. Input

The pads are read once a frame, in one place: `pad_Update` (`pad.c:66`),
called at the top of main's loop. It reads the two packets that the port's
driver fills (from the input it read at the last blank: "The one input
point", below) and keeps the result in `g_pad_state`. It works out the edges there and only
there: `pressed` is held and not held the frame before, and `released` and
`unheld` likewise (`pad.c:142`). Each fighter then takes its half of those
words once a frame (`fighter_ReadPad`, `fight.c:1095`), mapped through the
button configuration. There is no buffer of past presses outside
`g_pad_state` and the fighters' own state, which the snapshot holds. A
fight frame spans two blanks, and only the second blank's buttons count.
So netplay's input is **16 bits a pad a game frame**, set just before
`pad_Update`.

Two other readers have to be dealt with: the pause menu runs its own loop
with its own `pad_Update` (`main.c:494`), and `link_Connect` does as well
(`link.c:522`). A pad that is not plugged in counts as Start in a fight
(`pad.c:116`). Netplay must always report both pads present, and turn off
the pause menu, as online fighting games do.

**The one input point (done).** Everything the game reads of the pads
now comes from one value, a `PortInput` (`include/port_platform.h`): the
sixteen buttons of each pad and whether it answered, six bytes. The
console's driver reads the pads at a blank, and so does the port's
(`port_FillPads`, `src/port/pad.c`): the run's script
(`port_SetPadButton`) and the window's keys and controllers, through the
layout (`g_port_device_hook`, `src/port/window/`), are read there into
the driver's input. When the game reads the pads, `pad_Update` and the
options page's own read call `port_TakeInput`, which takes that input,
gives it to `g_port_take_hook` (a replay, or the network, puts its own
there), keeps it in `g_port_input` and only then writes the two packets
the game reads. The packets are no longer written at a blank. Reading
the devices at the blank and not at the read keeps the timing exact: a
plan's press made as a frame begins (at `ResetRCnt`) reaches the game a
frame later, as on the console and in the emulator. The three plans of
`port_game`, `port_other_fight` and `port_bugfixes`, measured: the game
read the pads 2,325, 2,445 and 2,355 times, never twice at one blank; the
pause menu's loop and `link_Connect` take an input for each of their own
frames. The movie player alone reads the pads its own way (`PAD_dr`), and
is given them as they are at that moment (`port_SampleInput`).

## 5. Scope and setting up a match

**The game has online play already: the link cable.** `FIGHT_MODE_NETWORK`
(`src/main/link.c`) exchanges an 8-byte packet with the other console every
frame, in every screen: the pad, the pad's kind, and a checksum
(`link_ExchangePad`, `link.c:299`). Each console acts on the pad it sent
the frame before, so it is lockstep with one frame of delay. Connecting
agrees a seed for `rng_Next` (`link.c:552`, `link.c:566`, then
`fight.c:958`), each side's button configuration
(`pad_SetRemoteButtonConfig`) and unlocks (`g_link_peer_unlock_mask`).
Loads are bracketed so the other console waits (`link_BeginWait`), and the
select screen has a mode for two consoles (`select.c:2035`). In the port
the serial driver under it is a stub (`src/port/api.c:34`). The mode makes
each console follow its own fighter in first person (`fight_start.c:57`):
the two consoles' cameras differ, and their memory differs with them.

**What follows for the port:**

- *Netplay of the whole session, not only fights.* Both peers start from
  the same state (the same build, disc, settings and card contents, the
  host's sent to the guest) and from then on exchange inputs every frame.
  The menus, select screens, fight, replay and the screens after it then
  run on both as they would on one machine with two pads. That is how
  emulator netplay works. It needs no network menu of the game's own, and
  the versus select screen is the match's set-up, with both players
  choosing at once as on a couch. Rollback covers every frame. A load is a
  point both peers wait at, as `link_BeginWait` makes it: a load in the
  port reads megabytes in one frame, which is no frame to run again.
- *Versus mode, not the link mode, as the default.* In versus both peers
  compute the same camera, so their whole state can be compared. The link
  mode's first-person view can come back as a choice once the viewer is
  made presentation only.
- *A first milestone that needs no rollback:* `AddCOMB` and
  `_comb_control` over UDP, so the game's own link protocol runs between
  two ports. That is delay-based, it works only where the round trip is
  under a frame or two, and it plays the link mode, but it is a few days'
  work and proves the transport and the handshake.

**Desyncs.** A hash per frame of the state that is compared, sent with the
inputs a few frames late and checked against the peer's for the same
confirmed frame. With the same build on both sides it can be a hash of
the bytes: RAM's mutable ranges, the game's variables and the emulation's
statics. The probe hashed all 6 MB a frame without trouble. On a
mismatch, both peers write their snapshots and the frame's inputs. The
probe's tools already name a differing page by the variables in it, which
turns a desync report into a diff. The same hash, with the same inputs
fed twice in one process, is the self-test of section 7.

## 6. Recommendation

**The approach: GGPO's.** The inputs are exchanged over UDP, a frame's
missing remote input is predicted as the one before, and a late input that
differs from the prediction restores the snapshot of its frame and runs to
now again. The peers keep in step by slowing the one that is ahead. Input
delay can be set from 0 to 3 frames (at 30 a second one frame is 33 ms, so
little delay is needed). Up to 7 or 8 frames can be rolled back.

- **The library.** GGPO (MIT, C++, since 2019) is the reference, but its
  upstream builds for Windows only. GekkoNet (C++, BSD-2-Clause, by the
  licence its listing gives) is a newer peer-to-peer rollback SDK and
  worth evaluating: check its licence file, its C interface, and MinGW and
  Linux builds first. Either one asks to call back into the game to "run
  one frame", which the port cannot do yet (below). If neither fits, the
  same algorithm in C in `src/port/` is a few hundred lines. That is the
  known design, not an invented one, and it keeps the port free of C++.
- **The transport.** Plain UDP through SDL_net, or ENet (MIT, C) for the
  handshake's reliable messages beside the unreliable, redundant inputs.
  Joining someone means an address and a forwarded port at first. Getting
  through NAT (a STUN-style exchange, or a relay) comes after, because the
  rest does not depend on it. Steam's networking only matters if the port
  is ever on Steam.
- **The architecture.** The game runs on a stack of its own already
  (`port_HostCallOnStack`, `src/port/main.c:288`). Make it a coroutine: a
  `port_StepFrame()` that switches to the game's stack, runs to the next
  frame boundary and switches back, a few lines of assembly a CPU or a
  Windows fiber. The window's loop, or the netplay library, then owns
  time and calls the step. The boundary is the top of main's loop, where
  no local is live: `skipped` becomes a variable of the port's, in the
  snapshot. The probe restored at `ResetRCnt` instead, which works only
  for an even number of frames, because `index` and `frame_db` are live
  there. A step run again sets one flag, which turns off drawing, the side
  channel, the card's sound, pacing, the keys and the trace.

**Preparatory changes, in order.** Each helps one of the later goals too.

1. **The step as a call** (the coroutine and the boundary at the top of
   main's loop), about a week. It also gives the window, the frame rate
   and optimisation one place where a frame begins and ends, and it is
   where remapping reads its input.
2. **One input point** (done: section 4, "The one input point"). The
   window's keys and controllers become a 16-bit word a pad, read into
   one `PortInput` that the game reads from (`port_TakeInput`), with a
   hook where a replay or the other peer's input goes in. Remapping
   (item 9) is the layout the window reads the PC's devices through
   (`rebushido.cfg`, `docs/PLAYING.md`). A recorded run of such words
   is a replay and a test; the recording itself is not written yet.
3. **State apart from presentation.** The program's variables go in two
   groups at the link (`tools/port_build.py`, `tools/port_sdl_build.py`):
   the game's, Sony's and the emulation's (clock, drive, XA, chip, pads,
   card, GPU registers) in one, and the picture, side channel, plan, trace
   and output in the other. A check like `port_layout` fails the build on
   a variable in neither. The side channel's epoch must only go forward.
   This is also the boundary a mod framework (item 10) must keep out of
   the game's state. About a week.
4. **Save, load and hash** (`port_StateSave`, `port_StateLoad`,
   `port_StateHash`), with the probe's test as a check. It runs a plan
   headless, saves, rolls back and runs again many times, and fails on
   any page that differs, both sizes. This is what keeps every later
   change honest about determinism. **Built: section 7.** The check needs
   the disc, so it is `make port-rollback`, not one of the checks of every
   push.
5. **A step that can run with nothing drawn**, and the sound out of the
   rollback (section 3). After that, **a build for players at `-O2`
   without `-finstrument-functions`**, and the sound chip made faster,
   which is item 12's anyway. A frame run again costs about 4 ms today,
   so 8 frames is too slow and 3 is not. One to two weeks.
6. **The session:** the handshake checks the build, the disc's hash and
   the settings. The host sends its card data and settings, both seed
   `rand()` and `rng_Next`, and both start from the versus select screen
   with the pause menu turned off and both pads present. One to two weeks.
7. **The netplay itself:** the library or the C core, UDP, delay and
   rollback settings, a line on the screen for ping and rollbacks, the
   desync report, and spectators later. Two to three weeks.

So about two months of work for one person to a first online match that
rolls back, after which tests between two real PCs settle the delay, the
window of rollback and the sound.

**For the picture work now.** Keep everything the picture remembers from
frame to frame (the tween's three frames, the side channel's epoch and
slots, the ground mesh's caches) out of the game's state, with counters
that only go forward. Let the tween's next frame be replaced. Do not let
anything in `src/port/gl/` write a variable the game reads. Any new port
variable belongs in one of the two groups of change 3, said when it is
written.

## 7. Save, load and hash

Preparatory change 4 is built: `src/port/state.c`, declared in
`include/port_state.h`. `port_StateSize()`, `port_StateSave(buf)` and
`port_StateLoad(buf)` copy the state out and back; `port_StateHash()`
hashes the part of it a console would hold. The state is whole from the
game's first frame on (the sound chip's page is mapped as the game
starts its sound).

**What is in.** The pieces, in the snapshot's order:

| Piece | Bytes, 64-bit (32-bit) | Why |
| --- | --- | --- |
| The console's RAM | 2,097,152 | the game's own memory: the primitive buffers, the effects' area, everything loaded |
| The scratchpad | 1,024 | used every frame |
| The I/O page of the sound chip's registers, `0x1F801000` | 4,096 | libspu writes the chip's registers and its DMA's there; leaving it out was the probe's first failure, and is the self-test's (below) |
| The port's own primitive buffers, `PORT_PRIMS_ADDR`, where a run has them | 2,097,152, or nothing | at the frame's boundary the self-test uses they hold the frame not yet given to the GPU (below) |
| The program's variables, but those of `OUTSIDE_STATE` | 2,636,352 (2,619,712) | the game's and Sony's, then the port's emulation of the console |

That is **4.74 MB** in both sizes of the headless program (4,738,624 and
4,721,984 bytes), and 6.84 MB in the program with a window, which has the
port's primitive buffers. Among the variables, after the game's and
Sony's: the clock (`clock.c`), the drive and the XA reader with its
128 KB buffer (`cd.c`, `xa.c`), the sound chip with its 512 KB of sound
memory and the mixer (`spu.c`, `sound.c`), the pads (`pad.c`), the
memory cards' images (`card.c`), the events, the GPU's registers and its
megabyte of video memory (`raster.c`, `gpu.c`), the GTE, the movie's
decoder (`mdec.c`, `stream.c`), the counts of the stubs, and a play
test's plan (`plan.c`).

- *Video memory* is in although no fight frame reads it: the menus call
  `StoreImage` and `MoveImage`, the session is rolled back as a whole
  (section 5), and the copy of a megabyte is a tenth of a millisecond.
- *The plan* is in because it is the run's input: it schedules presses
  from checkpoints and counts the game's frames. Rolled back with the
  game, it presses the same buttons the second time. In netplay the
  input comes from outside instead (below).

**What is out, and why.** The link puts the variables of the files
`OUTSIDE_STATE` names (`tools/port_build.py`) after everything else of
their kind, so that what the state saves of the program's variables is
two runs of memory, one of initialised data and one of zeroed, whose ends
are marks of the link (`g_port_state_data_end`, `g_port_state_bss_end`).
The window's build does the same inside the game's object
(`tools/port_sdl_build.py`). This is change 3's division in a first form,
by file and not by variable, and with no check that a new variable is in
the right group.

- the remastered picture's side channel (`remaster.c`, 1.2 MB): it is
  the picture's, and a load must not rewind its epoch (section 3);
- what the run prints and writes: the output buffer (`text.c`), the
  sound file (`wav.c`), the pictures' buffer (`picture.c`). What was
  printed is not taken back, and the self-test compares what is printed
  again instead (below);
- the program's own: its arguments and the disc's file (`main.c`), the
  host's file handles and signal stack (`host_linux.c`,
  `host_windows.c`), the crash report (`crash.c`), and the snapshot's own
  bookkeeping (`state.c`), which must survive a load;
- in the program with a window, the window's files (`src/port/window/`:
  the pacing, the sound card's queue, the keys, the counter, the log) and
  everything outside the game's object: `gl_tween` and the rest of
  `src/port/gl/`, SDL and the C library;
- the game's stack: at main's loop no frame of it holds anything. A
  function that reads a local it never set reads the stack, though, and
  one place did (below);
- what the port allocates (the plan's text, the table of names): set as
  the program starts and never changed.

**Host pointers.** In the 64-bit program a pointer in the state is one of
three kinds, and each survives a load. It points into the program's own
variables or code, which are at fixed addresses (the program has no
position independence) and are themselves in the state or never move;
or it was set as the program started, before any save, and is the same
in every snapshot (a hook, a path, the plan's text); or it lies in a file
outside the state. So a snapshot is for the same program in the same
run: it is never written to a file or given to another build.

**The hash** reads the console's RAM, the scratchpad, the sound chip's
registers and its sound memory, the GTE's registers, and the game's and
Sony's own variables, which the link puts first in each kind (on the
console they are in its RAM): 3.1 MB. It leaves out the port's
bookkeeping, where a pointer can be the host's own (`sound.c` keeps one
to the program's arguments, the plan several to memory the system gave
it), so two runs of one program give the same hash for the same state:
two runs of each size gave the same hashes at all 95 saves. It is
MurmurHash3's mixing in two lanes of 32 bits, as quick in the 32-bit
program as in the 64-bit one: 0.38 ms. **The two sizes' hashes differ**,
and nothing cheap makes them agree. Measured piece by piece at the same
95 saves of the two programs: the sound chip's registers and memory were
the same at every one, the GTE at 74, the scratchpad at 63, the RAM and
the variables at none. The variables are laid out otherwise in the
64-bit program, and the console's RAM holds their addresses (an ordering
table's links into `g_disp_db`, the `PTR32` slots). Two peers must run
the same program (section 1), and compare hashes only between the same
build.

**The self-test.** `--rollback-test N` (and `--rollback-every M`, 24 if
nothing says) makes the headless program save the state every M frames,
run N frames while it notes a hash of every 4 KB page of the state, the
state's hash and a hash of what each frame printed, load the state and
run the same frames again, and stop at the first page, hash or line that
comes out otherwise, naming the pages and, in the first frame after the
save, which is kept whole, the words. What a run prints goes through a
tap (`g_port_print_tap`, `text.c`): the first time it is printed and
noted, the second time only compared, so a run with rollbacks prints
exactly what the same run without prints. At the end it reports the
rounds and the times on the error output.

It is hooked in as the probe was: one call after main's `ResetRCnt`
(`main.c`, `port_OnRollbackFrame`, under `RB_PORT`), which does nothing
unless the option is given. Main's loop keeps `index` and `frame_db`
there, so a load is made only where the frame's buffer is the save's
(the hook is given `index`); a first run that does not come back to it
within 16 frames is given up. Change 1 moves the boundary to the top of
the loop, where that rule and the primitive buffers' piece go away.

`make port-rollback` (`tools/port_rollback.py`, `tests/README.md`) runs
it in both sizes with `port_game`'s plan, from power on through the
menus, a versus fight to the kill, the replay and back to the main menu:
once plainly and once with `--rollback-test 8 --rollback-every 24`, and
it fails as well if the two runs printed or wrote anything differently.
`--program` runs it on another build, such as the window's. It needs the
disc, so it is not one of the checks of every push; about half a minute.
On the machine of section 1 (other builds running):

| Run | Rounds | Frames run again and compared | Result |
| --- | --- | --- | --- |
| 8 frames every 24, 64-bit and 32-bit | 95 each | 760 each | the same, and the same output and files as the plain run |
| 8 frames every 8 (every frame twice), both | 285 each | 2,280 each | the same |
| 7 frames every 7, 64-bit | 285 | 2,280 | the same |
| 1 frame every frame, both | 1,145 each | 2,290 each | the same, after the fix below |
| 8 every 24, the window's program (`--fast`, dummy drivers) | 95 | 760 | the same |

| Time (8 every 24) | 64-bit | 32-bit |
| --- | --- | --- |
| save, mean (least to most) | 0.53 ms (0.10 to 2.0) | 0.55 ms (0.31 to 2.1) |
| load | 0.41 ms (0.21 to 2.3) | 0.35 ms (0.20 to 2.4) |
| `port_StateHash` | 0.38 ms (0.36 to 0.79) | 0.38 ms (0.36 to 1.1) |

The copy is the processor's own (`rep movsb`), so a frame's snapshot
costs about a twentieth of a frame. Eight snapshots are 38 MB.

The test was shown to fail where it should: with the I/O page left out
of the snapshot it stopped at the first round (the state's hash
differed), and with the port's own variables left out it stopped at the
first frame, naming the pages (the XA reader's buffer, the sound
library's variables).

**What it found.** One frame every frame failed in the replay after the
fight: the first frame after a save differed in ten words of
`g_motion_decoders`. The motion decoder keeps each frame it decodes in a
cache with the rates of change of its values, and some rates it never
works out (the root's three, a value coded as fixed, all of them for the
first frame of a group of eight); those are kept as the caller's work
area held them. In a fight the work area is the scratchpad, which is in
the state; in the replay it is a local array on the stack
(`replay.c`), so the cache kept whatever an earlier call had left
there, and the hook's own calls had left something else the second
time. Nothing reads those rates, so the game played the same, but the
state did not, and netplay's hash would have called it a desync. The
port now starts them at zero (`motion_decode.c`, under `RB_PORT`). The
plain run's trace is unchanged. Any other read of what was never written
that reaches the state will show the same way: the test finds it where
the stack's garbage differs between the two runs of a frame.

**What the later changes need from this.**

- *Change 1* moves the hook to the top of main's loop.
- *Change 2, the input record.* The game step reads the pads from the
  port's pad driver (`pad.c`: the held buttons and the packets), which is
  in the state, as is the plan. A word a pad a game frame that the step
  reads must be in the state too, or be set again before every step,
  including a step run again; what the window keeps of the keys and the
  remapping (`src/port/window/`) is outside it and must stay so. Netplay
  then sets the record before each step and does not use the plan.
- *Change 3* turns `OUTSIDE_STATE` into the link's two groups by
  variable, with a check that every variable is in one.
- *Change 5.* A frame run again still draws, mixes and hands its samples
  to the window's sound card; the self-test is for the headless program
  (with `--wav`, or in the window, a round's frames are heard twice).
- *The picture work.* The side channel is outside the state now, so a
  load no longer rewinds its epoch. What is left for that work: slots
  written by a frame that was taken back stay, under an epoch the
  corrected frame does not use again; after a load the frame that the
  GPU is given next was built by the game before the save, and meets
  the side channel's notes of the last frame run before the load (at
  change 1's boundary that frame is drawn before the save, and this goes
  away); and the tween must take
  its frames only from steps that are shown (section 3).
