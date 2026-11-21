# Online play with rollback

What the native port ([`PORT.md`](PORT.md)) has and lacks for online play
with rollback netcode ([`ROADMAP.md`](ROADMAP.md), item 8), measured on the
port as it is, and the design that follows. Built so far: the save,
load and hash of the state with a self-test (section 7), the game as a
step the host calls (section 8), the state's files apart from the
picture's (section 9), a step run again quietly (section 10), the game's
own link cable over UDP (section 11), rollback netplay of a whole
session between two programs (section 12), and netplay as a player has
it: in the window, paced, with a line on the screen, set up by `--host`
and `--join` or the settings menu's Play online page, proved between two
window programs on one PC (section 13). What is left is play between two
real PCs, and what section 13 leaves to the owner.

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

A mod's music ([`MODS.md`](MODS.md), "Music") is outside the state: it
follows the drive by the sound's clock, a step run again quietly hears
none of it and moves nothing of it, and the samples heard with it are
made beside the mixer's own, which stay the state's. So two peers may
have different music mods. A mod's stage files are the game's own data,
loaded into its state: the peers must have the same. The piece a fight
starts with, chosen on the stage pages or by `fight_music` (PORT.md, "A
fight's music"), is a stream of the drive's like any other: the row is
`g_port_fight_music` (`fight_music.c`, a state file), the stream the
drive's and the chip's, so a rollback over a fight's start starts the
same piece on the same sample; the piece of a mod's own that the row can
offer is heard over the stage's own stream, outside the state, and a
session does not offer it.

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
In the window the keys and controllers are also read again at the
boundary, just before the game takes the input (`port_ReadDevicesNow`;
[`PORT.md`](PORT.md), "Input lag"); a plan's presses keep their blank.
Run-ahead, which saves, runs ahead and loads at the same boundary, is
off in netplay (its own loop) and over the link cable.

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
the serial driver under it is a stub (`src/port/api.c:34`), and the
link's packets go over UDP instead (section 11). The mode makes
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
- *A first milestone that needs no rollback* (built: section 11): the
  game's own link protocol over UDP between two ports. That is delay-based, it works only where the round trip is
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
- **The architecture** (built: section 8). The game runs on a stack of
  its own already. Make it a coroutine: a
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

1. **The step as a call** (done: section 8; the coroutine, and the
   boundary where each of the game's loops reads the pads). It also gives the window, the frame rate
   and optimisation one place where a frame begins and ends, and it is
   where remapping reads its input.
2. **One input point** (done: section 4, "The one input point"). The
   window's keys and controllers become a 16-bit word a pad, read into
   one `PortInput` that the game reads from (`port_TakeInput`), with a
   hook where a replay or the other peer's input goes in. Remapping
   (item 9) is the layout the window reads the PC's devices through
   (`rebushido.cfg`, `docs/PLAYING.md`). A recorded run of such words
   is a replay and a test; the recording itself is not written yet.
3. **State apart from presentation** (done by file: section 9). The
   program's variables go in two
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
5. **A step that can run with nothing drawn** (done: section 10), and
   the sound out of the rollback (section 3). After that, **a build for players at `-O2`
   without `-finstrument-functions`**, and the sound chip made faster,
   which is item 12's anyway. A frame run again costs about 4 ms today,
   so 8 frames is too slow and 3 is not. One to two weeks.
6. **The session:** the handshake checks the build, the disc's hash and
   the settings. The host sends its card data and settings, both seed
   `rand()` and `rng_Next`, and both start from the versus select screen
   with the pause menu turned off and both pads present. One to two weeks.
7. **The netplay itself** (the core is built: section 12): the library
   or the C core, UDP, delay and rollback settings, a line on the screen
   for ping and rollbacks, the desync report, and spectators later.

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
| The top of the game's stack, and its stack pointer (section 8) | 65,536 and 8 (4) | between two steps the game is a few calls deep where it reads the pads; what lies below the stack pointer is cleared first, being nobody's |
| The port's own primitive buffers, `PORT_PRIMS_ADDR`, where a run has them | 2,097,152, or nothing | the pause menu draws the fight's last frame from them again every frame |
| The program's variables, but those of the files outside the state (`config/port_state.txt`) | 2,636,352 (2,619,712) | the game's and Sony's, then the port's emulation of the console |

That is **4.80 MB** in both sizes of the headless program (4,804,204 and
4,787,552 bytes), and 6.9 MB in the program with a window, which has the
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
`config/port_state.txt` puts outside the state after everything else of
their kind, so that what the state saves of the program's variables is
two runs of memory, one of initialised data and one of zeroed, whose ends
are marks of the link (`g_port_state_data_end`, `g_port_state_bss_end`).
The window's build does the same inside the game's object
(`tools/port_sdl_build.py`). This is change 3's division in a first form,
by file and not by variable, and with no check that a new variable is in
the right group. (Change 3 made the division a file of its own with a
check: section 9.)

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
run: it is never given to another build. A replay's start is the one
snapshot written to a file, for another run of the same build, which
keeps its own hooks, cards and `run` group out of the load ([`PORT.md`](PORT.md),
"Replays").

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

It runs between two steps of the game (section 8): the host's loop
(`src/port/main.c`) calls `port_OnRollbackFrame` after each, which does
nothing unless the option is given. The game's stack is in the state,
so a load can be made at any boundary. (Before change 1 the hook was a
call after main's `ResetRCnt`, where main's loop keeps `index` and
`frame_db`, and a load could only be made where the frame's buffer was
the save's.)

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

**Only the pages written** (2026-11-18). A save and a load no longer
copy every byte: of a snapshot taken a frame or two ago, little has
changed (about a hundred of the state's 1,700 pages in a fight's two
frames, most of them video memory's). The four big pieces -- the RAM,
the primitive buffers and the two runs of variables -- are kept by page
(4 KB); the rest (the scratchpad, the I/O page, the stack, the stack
pointer and the bytes of a piece before its first page bound and after
its last) is copied whole every time, some 80 KB.

- *Epochs.* Every save and every load ends an epoch. Each page notes the
  epoch it was last written in, each snapshot the epoch it was taken in.
  A save into a snapshot copies the pages written after the snapshot's
  epoch, a load copies them back; a snapshot never taken here (or one
  whose buffer is new) copies all. A page put back from the newest
  snapshot is noted as of that snapshot's epoch, so a save into it again
  (run-ahead's one buffer) leaves it; from an older one, as of the epoch
  now. So netplay's ring may load any of its snapshots, the self-test
  its two, and run-ahead composes with both: what holds is that a page
  not written since a snapshot's epoch is what that snapshot holds.
- *What writes a page.* Video memory is written only by the software
  GPU (`raster.c`), which marks each line it writes
  (`g_port_vram_lines_written`, kept in `state.c`, outside the state).
  Every other page is made read-only at the end of each epoch
  (`port_HostProtect`): the first write to it faults, and
  `port_StateOnWrite` notes it and makes it writable for the rest of the
  epoch (`port_HostWatchWrites`: SIGSEGV on Linux, a vectored handler on
  Windows; any other fault is still a crash). A fault costs about 2
  microseconds on Windows and 5.5 in WSL, a page's copy a tenth of that,
  so a page written in two epochs running is left writable and taken for
  written in every epoch (*hot*), and is watched again every 128 epochs.
  A fight's run-ahead boundary has about 3 faults.
- *What the system writes.* A read of a file, or a datagram, into a page
  that is read-only would fail in the system, which does not fault: the
  hosts touch each page of where it goes first (`port_HostTouchPages`).
  The game's stack is never watched: a fault's handler may run on it.
  The picture between frames (`gl_tween.c`) writes video memory without
  the GPU's marks, and gives back every byte before the next save or
  load, as it has to anyway.
- `--whole-state` copies every byte, as before, for measuring.

The hash is unchanged: it reads everything, as before.

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

**What failed in the gate, and was not the state** (2026-11-18). Now
and then `python tools/gate.py` failed at `port-rollback` with "the run
with rollbacks wrote CP.fighters.snap otherwise than the plain run", in
the 64-bit program only, while the run's own hashes passed; it never
failed alone. The game was not at fault: the files were not the ones
the plain run had written. `wsl bash tools/build.sh port-rollback` runs
in the build's copy of the checkout (`tools/mirror.sh`, BUILD.md, "Where
the build runs"), so that copy kept a `build/rollback/` of its own, from
whatever program it last ran. The gate runs `port-rollback` in place,
and each of its build steps, when it ends, brings the copy's `build/`
back by what is newer or missing here (rsync `--update`): a file that
`port_rollback.py` had just cleared away to run again was missing, and
the copy's old one came back, or replaced the new one as it was written.
It was the 64-bit run because it runs first, while the window's builds
and the discs are still ending; by the 32-bit run they have ended.
Shown by running `port-rollback` in place beside `tools/mirror.sh out`
every 0.3 s, with an old `build/rollback/` in the copy: 4 runs of 4
failed so, with the same words; with the fix, 30 of 30 passed. The 64-bit
program itself, run from a copy of its own under the gate's load, passed
80 of 80; two of the six copies on the owner's machine held snapshots of
another branch's program, which differ in 21 of 23 files, and the
others the same program's, which do no harm: hence now and then. The fix: the copy brings back no
folder that a run in place writes (`rollback`, `net`, `budget`, `bench`,
`gate`), `tools/build.sh` runs `port-rollback` and `port-net`, which
build nothing, in place, and `port_rollback.py` fails a file older than
its run with a word on where it came from.

**What the later changes need from this.**

- *Change 1* (done) moved the hook between two steps, and put the
  game's stack in the state.
- *Change 2, the input record.* The game step reads the pads from the
  port's pad driver (`pad.c`: the held buttons and the packets), which is
  in the state, as is the plan. A word a pad a game frame that the step
  reads must be in the state too, or be set again before every step,
  including a step run again; what the window keeps of the keys and the
  remapping (`src/port/window/`) is outside it and must stay so. Netplay
  then sets the record before each step and does not use the plan.
- *Change 3* (done, by file: section 9) gives every file of the port
  one of the link's two groups, with a check.
- *Change 5* (done: section 10). A frame run again quietly
  (`g_port_rerun`) still mixes, but hands nothing to the sound card or
  the file, and paints nothing.
- *The picture work.* The side channel is outside the state now, so a
  load no longer rewinds its epoch. What is left for that work: slots
  written by a frame that was taken back stay, under an epoch the
  corrected frame does not use again; after a load the frame that the
  GPU is given next was built by the game before the save, and meets
  the side channel's notes of the last frame run before the load (at
  change 1's boundary that frame is drawn before the save, and this goes
  away); and the tween must take
  its frames only from steps that are shown (section 3).

## 8. The step

Preparatory change 1 is built. The game's main never returns; it runs on
its own stack (`PORT_STACK_ADDR`) as a fiber, and the host runs it a step
at a time (`src/port/step.c`, declared in `include/port_platform.h`):
`port_StartGame` makes the fiber ready, `port_StepGame` runs the game to
its next frame boundary, and the host's loop in `src/port/main.c` has the
turn between two steps. The switch is a few lines of assembly a
processor and system (`include/port_fiber.h`): it keeps the registers a
called function must keep, and on Windows the thread's record of its
stack's two ends (and, in 32 bits, its chain of fault handlers), since
Windows hands a fault to nobody while the stack pointer is outside the
stack it knows. The three hosts (`host_linux.c`, `host_windows.c`,
`window/host_posix.c`) give it as `port_HostStartGame`,
`port_HostEnterGame` and `port_HostLeaveGame` in place of the old
`port_HostCallOnStack`.

**The boundary is where the game reads the pads.** `port_TakeInput`
(`src/port/pad.c`), the one input point, ends the step before it takes
the input. Main's loop (in `pad_Update`, after its fresh ordering table),
the pause menu's loop, `link_Connect`'s and the options page's each read
the pads once a frame, so each of their frames is a step, and nothing in
`src/main/` had to change for it: the old hook after main's `ResetRCnt`
is gone. A frame that reads no pad (a load, the movie, the boot picture)
is part of the step it falls in, which suits netplay: one step, one
input. Between two steps the host may save the state, put back an
earlier one, or set the coming frame's input before the game takes it.

The boundary is not at the top of main's loop with nothing live, as
section 6 first proposed: the pause menu and `link_Connect` are loops of
their own, a few calls down. So the top 64 KB of the game's stack and its
stack pointer are in the state (section 7), and a load can be made at any
boundary, in any of those loops. At a boundary the game is a few calls
deep. What lies below the stack pointer is cleared before a save or a
comparison: it is whatever the deepest calls last left there, and that
differs between a frame's first run and its second (the self-test's tap
prints one time and compares the other), as it will in netplay, where a
frame run again does not draw.

**Measured.** With `port_game`'s plan, the plain run's trace (2,338
lines) is the same to the byte as before the change in both sizes, and
as the 64-bit Windows program's (`rebushido-headless.exe`, run natively).
`make port-rollback` passes in both sizes (96 rounds of 8 frames: the
rule of the save's parity no longer loses any), and so does the window's
program on Linux (`--fast`, dummy drivers) and the Windows program. One
frame every frame: 2,324 rounds of the 2,325 steps in each size, every
one the same.

## 9. State apart from presentation

Preparatory change 3 is built by file. `config/port_state.txt` puts every
file of `src/port/`, and every file of `src/port/window/` that is linked
into the game's object, in one of the link's groups, with a line
saying why: **state** (the console's machine as the game sees it: the
clock, drive, disc reader, XA, sound chip and mixer, pads, card, events,
GPU and video memory, GTE, the movie's decoder, the stubs' counts),
**run** (state as well, but the run's own and never carried into another
run: a play test's plan, linked last in the state between
`g_port_run_data` / `g_port_run_bss` and the state's end, which a
replay's start keeps this run's own of; [`PORT.md`](PORT.md), "Replays")
or **outside** (the remastered side channel, the run's
output, pictures and sound file, the crash report, the program's
arguments, the host, the step's own count, the snapshot's bookkeeping,
and the window's pacing, sound queue, keys, counter and log). The game's
own files are state, always. `tools/port_build.py` and
`tools/port_sdl_build.py` read it for the link (the outside group goes
after the rest of each kind) and stop the build on a file of the port in
neither group, or on a line naming a file that is not built. The rest of
the window and `src/port/gl/` are outside by being outside the game's
object, so the picture's work there needs no line.

It is by file, not by variable: a variable of a state file that must not
be put back by a load goes in a file that is outside. None had to move.
What was looked at for rollback's sake:

- **The side channel's epoch only goes forward.** `g_epoch`
  (`remaster.c`) is moved on by `port_RemasterNewFrame`, which the game
  calls as each frame begins, a frame run again included, and
  `remaster.c` is outside the state, so no load takes it back. After a
  rollback the frame that is shown finds its own notes under a new epoch
  and those of the frame before under the one before: the last frame run
  again, which is that frame on the corrected course, as in a run with no
  rollback. The slots of frames taken back keep epochs that are never
  used again.
- **Hooks set once** (`g_port_blank_hook`, `g_port_sound_hook`,
  `g_port_take_hook` and the rest) are in state files but are set before
  the game starts and never change, so a load leaves them as they are.
- **The memory card's file** (`card.c`) is the one thing in a state file
  that writes outside the program: a frame run again that saves writes
  the same bytes again, but a frame taken back that saved would have
  written its own. Netplay runs with `--no-cards` until the session sends
  the host's card (change 6).
- **The window's pacing, the sound card's queue, the tween** are outside;
  a frame run again must not reach them (change 5, section 10).

## 10. A step run again, quietly

Preparatory change 5 is built, in its first form. The host sets
`g_port_rerun` (`include/port_platform.h`, `src/port/step.c`, outside
the state) while it runs a step again after a load. The step then
computes all of the game's state as it would and nothing of what is seen
or heard of it:

- **the picture:** the software GPU (`raster.c`) runs every command, so
  its registers, the drawing mode a textured polygon brings, the clip and
  the offset are what they would be, and it takes loads into video memory
  and copies within it; but it paints no pixel of a polygon, line,
  rectangle or fill, and the remastered renderer is given no command
  (its note of a load into video memory still goes, so its textures stay
  true). The side channel's own notes are taken as ever: its epoch goes
  forward (section 9);
- **the sound:** mixed, as section 3 recommends first (the chip is in the
  state and a load takes it back with the rest), but neither the window's
  sound card nor `--wav` is given the samples. A sound that a corrected
  frame adds is heard from the next frame that is shown, a few frames in;
  one that the frame taken back started stops short;
- **the window** has no turn at a blank (its input and blank hooks:
  pacing, presenting, the keys), a run writes no picture (`--shots`, a
  checkpoint's picture, snapshot, video memory or sound chip) and prints
  nothing; a play test's plan still runs, being in the state.

Video memory is then not what a run without rollbacks would hold: what a
frame run again would have painted is missing until a shown frame paints
over it. In a fight every frame paints the whole screen, and no fight
frame reads video memory back (section 2). A load or a menu that did
(`StoreImage`, `MoveImage` of what a frame painted) after a quiet run
again would read the frame taken back's pixels; none does in the
measured runs, and the hash (section 5) would name it as a desync.

**The self-test** has it: `--rollback-quiet` runs each round's frames
again quietly and compares every page of the state but video memory and
the game's stack. The stack is left out because the registers a function
keeps for its caller can hold what a painting function last left in them
and nobody reads (the 32-bit program showed one such word); the run that
is not quiet still holds the stack to the byte. `make port-rollback` now
makes three runs of each size: plain, with rollbacks, and with rollbacks
run again quietly, and all three print and write the same. It reports
how long a step took between rounds, run first and run again, over the
run and in the fight's frames alone.

**What a frame run again costs.** The headless 64-bit program, `port_game`'s
plan, 8 frames every 24, on the machine of section 1 with other builds
running (so the means wander by a millisecond between runs; the least is
steadier):

| A step of the fight | `-O1` mean (least) | `-O2` mean (least) |
| --- | --- | --- |
| run first, drawn and sounded | 5.2 ms (2.7) | 4.1 ms (2.3) |
| run again quietly | 2.9 to 3.2 ms (0.90) | 2.0 to 2.1 ms (0.71) |

So a quiet frame is about half a drawn one, and most of what is left is
the sound chip mixing two blanks (section 3); eight of them at `-O2` are
16 ms, half a frame of 30 a second, and three are 6 ms. Keeping the sound
out of the rollback (section 3, "later") would take most of the rest.

**`-O2`.** `PORT_OPT=2 make port` builds the headless programs at `-O2`
into `build/port-O2/` and `build/port32-O2/`, and `PORT_OPT=2 make
port-sdl` the window's into `build/port-sdl/linux-O2/`. With
`port_game`'s plan all three print the same 2,338-line trace as at `-O1`
to the byte, and pass the rollback self-test, quiet or not. It buys about
a fifth of a fight frame drawn and a third of one run again. The checks
and the play tests keep `-O1`, and so does the window's build for now:
the play tests hold only the headless program to the emulator, so the
switch of the players' build is the owner's.

## 11. The link cable over UDP

The first online milestone is built: the game's own link-cable versus
mode (`FIGHT_MODE_NETWORK`, `src/main/link.c`) between two programs of
the port, in lockstep with one frame of delay, as on two consoles.

**The network** (`src/port/net.c`, `include/port_net.h`) is one UDP
socket to one other program: `--net-bind ADDRESS:PORT --net-peer
ADDRESS:PORT`, IPv4 numbers only. The hosts give it as three calls of
`include/port_host.h` (`port_HostUdpOpen`, `Send`, `Receive` with a
wait): the kernel's own calls in `host_linux.c` (`socketcall` in 32
bits), the C library's in `window/host_posix.c`, Winsock in
`host_windows.c` (the Windows programs link `ws2_32`). No library is
vendored: SDL_net is not linked, and ENet would only add what the next
section's few datagrams do not need. A run binds where it is told and
answers only its peer; the tests bind to 127.0.0.1. For tests a run can
make the network worse: `--net-latency MS` holds every datagram back
that long and `--net-loss PERCENT` drops so many, by a sequence of
random numbers of the run's own.

**The link** (`--link-side 0|1`). The serial driver's lines and
interrupts are not imitated: their timing is the console's, and a busy
wait on them moves the port's clock (each `GetRCnt` is a scan line), so
two programs would part by however long the network took. Instead,
under `RB_PORT`, link.c asks the port at the three places it touches
the cable, and its own protocol, checksums and all, is left as it is:

- `link_PickSide`: the side is the one the program was given;
- the handshake's two waits: `port_LinkMeet`, where each program says it
  is here until each has heard the other, waited for in the PC's time
  and none of the game's, so that both take the same frames to connect;
- `link_SendPacket` and `link_ReceivePacket`: the packet's two words are
  a datagram, numbered; a wait sends its own last one again every 50 ms,
  and a program that is sent one it already had sends its own last
  again, so a lost datagram costs a resend and nothing else. After 15
  seconds of silence the other console has gone and the game aborts the
  link, as it does on the console.

A program that ends still answers for a moment, so that the packets the
other is waiting for are not lost with it.

**Proved** by `tools/port_net.py link`, which runs two headless programs
of one build at once, bound to 127.0.0.1, each with a plan of its own:
both go from power on to the top menu's link row as `port_game`'s plan
goes to versus, connect, and in the select screen and the fight each
console's pad plays its player's part of `port_game`'s presses. Over the
link the first stage cell is another stage, where the two start far
apart (`SELECT_STAGE_LINK_TO`), so the first console takes the cell
beside it; and the view is each fighter's own, so both walk forward
before they strike. The first player is cut down at frame 687.

| Run | Packets each way | Sent again | Result |
| --- | --- | --- | --- |
| 64-bit, 5 s | 2,132 | 0 | the same 2,369 lines on both consoles, to the end of the fight |
| 32-bit, 10 s | 2,132 | 0 | the same |
| 64-bit, `--latency 5 --loss 10`, 45 s | 2,132 | 254 | the same 2,369 lines |

The two consoles' traces are the same line for line although their
cameras differ: the traced values are the fighters', the screen's and
the random numbers, which the two compute alike. What this mode costs is
the round trip: each frame waits for the other's packet, so it plays
only where that is well under a frame. Rollback (section 12) is for the
rest.

## 12. Rollback netplay

The netplay of section 6 is built in its core, GGPO's design written in
C (`src/port/netplay.c`): `--netplay 1|2` with `--net-bind` and
`--net-peer`, and `--net-delay N` (2 if nothing says). Neither GGPO (MIT)
nor GekkoNet was vendored: both are C++, and the headless program, which
the tests run, has no C or C++ library to link them against. The
algorithm is a few hundred lines.

**The session is the whole run**, as section 5 recommends: both programs
start from power on with the same build, disc and options (`--no-cards`
for now), and each frame's input is both pads, the one this program
plays and the other's, sent over the network. It goes in by the input's
hook (`g_port_take_hook`), with both pads present.

- **Frames.** A frame is a step (section 8). The host's loop
  (`port_RunNetplay`) saves the state at each frame's boundary into a
  ring of ten, then runs the step with the frame's input.
- **The local pad** is what the pads' driver read at the last blank
  (`port_DriverInput`: a plan's presses or the window's keys), kept for
  the frame `--net-delay` on; the first frames of the delay have none.
  Every input the other has not acknowledged is sent in each datagram,
  with this program's count of the other's inputs and its last final
  hash, so a lost datagram is made up by the next.
- **The other pad**, until its input comes, is taken to be what it last
  was. One that comes and is not what was taken puts back the state of
  its frame and runs every frame from there to now again, quietly
  (section 10), with the inputs now known. Neither program runs more
  than 8 frames past the other's last input it has: it waits, sending
  its inputs again every 16 ms.
- **Desyncs.** Every 30 frames, once both pads' inputs before it are
  known, the state's hash at that frame (`port_StateHash`, section 7) is
  final. Each program prints it (`netplay: frame F hash H`, on the error
  output), sends it with its inputs, and stops with `netplay: DESYNC`
  when the other's hash of a frame is not its own. The two are compared
  only once both are final: the other's is kept until this program's
  own is (below, "Under load").
- **The end.** A program that ends (its plan's end, or a failure) tells
  the other, and answers for a moment longer; the other runs to the last
  input it was given and ends there.

**Proved** by `tools/port_net.py rollback` (`make port-net`), two headless
programs of one build on 127.0.0.1, both following `port_game`'s plan,
each pressing only its own pad's part of it: power on, the menus, versus
select, a fight to the kill, the replay and back to the menu, 2,332
frames. Each frame's presses count two frames later than in the plan's
own run, so this is not `port_game`'s fight to the frame, but it is a
fight to a kill. 64-bit, on the machine of section 1:

| Network | Rollbacks (pad 1, pad 2) | Frames run again | Longest | Result |
| --- | --- | --- | --- | --- |
| none (0 ms, no loss) | 1, 12 | 2, 23 | 6 | 77 hashes the same on both, the last `30925357EE23889C` at frame 2,310 |
| 100 ms each way, 5% lost | 44, 66 | 267, 427 | 8 | the same 77 hashes, the same last one |
| the same, 32-bit | 44, 65 | 261, 419 | 8 | 77 hashes the same on both, the last `531D54044C49431A` |

The two runs of the 64-bit program end in the same state whatever the
network did, as they must: the network only decides when an input is
known, never what it is. A 64-bit program against a 32-bit one, whose
hashes differ (section 7), stops on both sides with a desync at frame 30,
the first hash. With 100 ms each way the headless pair, which nothing
paces, runs about 70 frames a second, waiting about ten thousand times on
the other for its inputs. A frame run again averaged 7 to 11 ms there,
far more than a fight frame's 2 to 3 ms (section 10), because the session
is rolled back in the menus and loads too, where one step can read
megabytes from the disc.

With `--netplay` both programs must be started alike by hand, from power
on, and both must skip the opening alike: the movie player reads the
pads its own way (section 4). Netplay now skips every movie where it
begins (section 13), so this run's presses count otherwise than when the
table above was measured; it still ends on the same hashes on both, the
last now `A73BDA6AD7613D4C` (64-bit, 100 ms each way, 5% lost; a
hash follows where the program's variables lie, which changed with
PORT.md's "Where the program's variables are").

**Under load.** A busy PC made `make port-net` stop now and then on a
desync near a session's end, and a rerun pass. It was not one: the
program that stopped had compared the other's final hash of a frame with
its own hash of that frame made on a guess, before the rollback the same
burst of datagrams asked for. A program that has run past a hashed frame
on a guess holds a hash of it that a rollback will make again; the
other's final hash of that frame comes in the very datagram that brings
the inputs which show the guess wrong, or a few behind it in the same
burst, and each datagram's hash was compared as it was read. A program
the system had held up a moment, waking to a queue of datagrams, met
that often. In every failure the program that stopped had not yet made
the frame's hash final (its last final hash was the one before), and
the other's final hash of the frame was the one both had printed in the
runs that passed. Now the other's hashes are kept, a ring of them, and
each is compared once this program's hash of the same frame is final
(`port_NetplayCompareHash`); `tests/port/netplay.c` holds it to that
with the three orders the datagrams can come in, the hash with the
inputs, after them in one burst, or before them.

`tools/port_net.py --stress N` runs a check N times over and counts the
runs that failed and found a desync; `--pause MS` stops one program or
the other for MS milliseconds every second or so, `--burn K` starts K
programs that burn a processor, and `--port-base` keeps the runs' ports
apart from another's (tests/README.md). Before and after the change, on
the machine of section 1:

| Run | Runs that stopped on a desync |
| --- | --- |
| before: `rollback`, 64-bit, `--pause 300` | 2 of 5 (both at frame 2,160) |
| before: `rollback`, both sizes, `--burn 40` (32 processors) | 2 of 10 (64-bit, both at frame 2,280) |
| before: `session`, both sizes, `--pause 300` | 1 of 4 (32-bit, frame 1,680) |
| before: `window`, `--pause 300` | 0 of 4 |
| after: `rollback`, both sizes, `--pause 300` | 0 of 20 |
| after: `rollback`, both sizes, `--burn 40` | 0 of 10 |
| after: `session`, both sizes, `--pause 300 --burn 8` | 0 of 20 |
| after: `window`, `--pause 300` | 0 of 5 |

A 64-bit program against a 32-bit one still stops on both sides at
frame 30, as it must, and `make port-rollback` under 32 programs that
burn a processor passes.

What was left here, pacing, the session's set-up and the line on the
screen, is section 13.

## 13. A session in the window

Netplay as a player has it: the window's program, paced against the
other, with a line on the screen, set up from the command line or the
settings menu. How to play it is in [`PLAYING.md`](PLAYING.md),
"Playing online".

**The session** (`src/port/session.c`, preparatory change 6). The host
waits at `--host ADDRESS:PORT`; the guest goes there with `--join
ADDRESS:PORT` (its own socket at `--net-bind`, or any address of the PC
and a port the system chooses). Nothing of the game runs until the two
have met:

- the guest says hello every 100 ms; the host takes the first program
  that does as its peer (`port_SetNetListen`, `port_FixNetPeer`,
  `net.c`), and answers nobody else from then on;
- each holds the other to the same **protocol**, the same **build** (a
  sum of the version with its commit, the size of a pointer, the
  system, where the program's variables lie and the state's size: two
  programs that agree on these lay the game's memory out alike, which is
  what makes their hashes comparable, section 7), the same **disc** (a
  sum of its size, its volume's description and every 997th sector) and
  the same **gameplay mods** (`port_WeaponsSessionHash`: every byte of
  the mods' `stages/`, `weapons/` and `motions/` files with their keys,
  and the `--weapon`s each was given; MODS.md, "Weapons and motions";
  and every arena the stage select offers and the one `--arena` asks
  for, their files and keys, `port_ArenaSessionHash`: MODS.md, "Custom
  arenas";
  textures, models, skies and music are not in it, and may differ). A
  guest that differs is turned away with the reason, which both logs
  say;
- the host sends its **two memory cards**, whole, 1 KB a datagram, the
  guest saying which parts it has every 20 ms, and a **seed** taken from
  its clock. Both then play on copies of the host's cards kept in memory
  (`port_SetCardImage`, `card.c`): the unlocks, the button layouts, the
  records and ReBushido's settings, **Bugfixes among them**, are the
  host's on both, and neither writes a save of the session to a file.
  The copies are part of the state (256 KB), so a save that a rollback
  takes back is taken back too. With `--no-cards` the host sends none;
- the welcome also carries the kind of stage the host's stages start as
  (`MEET_KIND`) and the host's two gameplay switches, `unlock_all` and
  `stage_select_everywhere` (`MEET_PLAY`, protocol 4; PORT.md, "Every
  stage in every mode, and every unlock"), the host's `fight_music`
  (`MEET_MUSIC`; PORT.md, "A fight's music") and the time of day the
  stages start at (`MEET_TIME`; PORT.md, "The time of day"), protocol 6,
  and the host's `match_rule` and `match_count` (`MEET_MATCH`, protocol
  8; PORT.md, "Match rules"; protocol 11 when the rule may be Crew;
  12 with the rule page before the first select, crews of different
  characters, the winner's wounds kept and a Crew match's end the
  session's);
  both programs hold them for the session,
  so a guest plays with the host's unlocks, the host's fights' music
  and the host's match rule whatever its own settings say, and its
  settings menu changes nothing until it starts again;
- when the guest has the cards it says it is ready; the host says go and
  starts, and the guest starts on that or on the host's first inputs.
  The host is the first pad, the guest the second, and the host's
  `--net-delay` is both players'.

With two programs started on one PC the meeting and the cards take well
under a second; with 100 ms each way and 5% lost, about two.

**From power on to versus select the session presses for both**
(`port_SessionInput`): the same buttons at the same frames on both
machines, worked out from the game's state, so that no guess about them
is ever wrong. Start on the options page (the host's settings, from its
card), Start and Cross on the title and the memory card's messages
(Cross loads the host's save), and on the top menu Down until the row's
mode is versus (`menu_GetTopRowMode`, `src/main/menu_pages.c`, under
`RB_PORT`), then Cross: about ten seconds. With the host's `menu =
rebushido` there is no options page, the save is loaded without the
question, and the row is Versus, which stands for the game's own
(`PORT.md`, "ReBushido's top menu"). The **versus select screen is
the match's set-up**, both players choosing at once as on a couch, and
the host takes the stage. After a fight the game's own menu offers
Rematch, the select screen, or the main menu; a way back to the top menu
comes straight back to versus. A Stock, First to or Crew match
(PORT.md, "Match rules") goes from fight to fight by itself, with a card
of the score, and back to the stage pages (a Crew match back to the top
menu, which ends the session on both: protocol 12); with the host's
`menu = rebushido` the host's pad chooses the rule on the page before
the first select. Its score (and a Crew's crews, each player's picked
with his own pad, and the wounds a Stock or Crew winner keeps) is the
game's state
(`g_port_match`, `src/port/match.c`), so a rollback puts it back with
the fight and both machines keep the same. **In a fight Start is nobody's**: the
pause menu would stop the other player's game too. Where the frame's
input does not depend on the other's buttons (the session's presses, or
Start in a fight), a guess of them that turns out otherwise rolls
nothing back (`g_np_used`).

**Each machine keeps the session's fights as replays** ([`PORT.md`](PORT.md),
"Replays"): netplay's frames call the replay's boundary, a frame run
again included, so that a fight's start and inputs are those of the
frames as they were last run, and a file is written once every input to
the fight's end is known and nothing is left to run again
(`port_NetplayFinalFrames`). The two machines' files of a fight have the
same inputs, views and hashes (`tools/port_net.py session`), and either
plays back on either program of the build.

**Movies are skipped where they begin, on both machines** (`PAD_dr`,
`pad.c`, `g_port_movies_skipped`). The movie player reads the pads its
own way, at moments no other machine shares (section 4); rather than
make its reads frames of the session, every movie in netplay is given
Cross held from its first read, and ends there. Versus has no movie
but the opening.

**What the game computes must not follow either player's picture.** Two
things of the remastered picture reach into the game's own computation,
and both followed the picture, so that one player in Remastered and the
other in Original, or F2 in the middle of a fight, parted the two
machines (a desync at frame 1,500 of a session, the first time it was
tried):

- *the draw distance* (`src/port/main.c`): how much of the stage the
  game walks, and which primitive buffers it is given. In netplay it is
  the console's, whatever each player's picture; the remastered picture
  is still drawn, at that distance. The alternative, both computing the
  host's distance whatever their picture, would keep `max` online but
  change the Original picture with it: the owner's call;
- *the left margin* (`port_RemasterFixMargin`, `remaster.c`): a wider
  picture keeps faces the console would cut off at its screen's left
  edge (`render_FaceOffScreen`), which changes the primitives the game
  builds. In a session each program sends the shape of its picture and
  both cut faces off at the wider of the two, whether Remastered is on
  or not; with `--netplay`, at the program's own shape. Neither player
  loses a face of a wider picture, and the Original picture draws a few
  faces left of its screen that nobody sees.

With both fixed, the window's session of `make port-net` (below), with
40 ms each way and 3% lost, the host in Remastered on a software OpenGL
driver (`SDL_VIDEODRIVER=offscreen`, `LIBGL_ALWAYS_SOFTWARE=1`),
`draw_distance = max` in both settings, F2 every 300 blanks on the host
(17 changes of picture), and the guest in Original, ends on the same 82
hashes on both. The host, drawing in software, could not keep the
console's rate, and time sync held the guest back 62 times, 4.6 seconds
in all, to stay with it.

**Pacing** (`netplay.c`, `src/port/window/sdl_net.c`, `pace.c`). The
window's program paces each blank against the sound card as before; on
top of that:

- *time sync*, as GGPO has it: every datagram says the frame its sender
  is at and how far ahead it reckons itself (the other's last frame and
  half the round trip, against its own). Over 32 frames each side
  averages both; where this one is further ahead than the other is
  behind, it holds its window back half the difference, at least a
  frame and at most four, no oftener than every 30 frames
  (`g_port_net_hold_hook`). The window takes a hold a quarter of a
  blank's time a blank (`port_PaceHold`), so the sound's queue is drawn
  down a little and never runs dry. The headless programs, with no
  time to keep, run as fast as they go;
- *a load is a point both wait at*: a step that read more than 64
  sectors loaded something, and the next frame waits until the other's
  inputs up to it are in, so that no later guess can roll the load
  back. The step that loads is run once, as the other's; a guess before
  it that the other's inputs then correct still runs it again. Each
  window hurries the blanks of its loads through (`quick_loading`,
  PORT.md, "Quick loading"), which changes no frame and no input: the
  one there first waits, as it would for a slower PC;
- *the waits*: while netplay waits on the other (for its inputs past
  the eight frames it may guess, or at a load), the window reads its
  events, so closing the window still ends the program, and shows its picture again
  with the line saying what it waits for (`g_port_net_wait_hook`).

**What a rollback does to the picture and the sound.** A frame run
again is drawn by nobody and heard by nobody (section 10). The picture
jumps to the corrected course at the next frame shown, as every rollback
game's does; `gl_tween` draws its pictures between frames that were
shown, never ones run again, so its next frame may be one a correction
has replaced, and its picture jumps with the game's. The window's
session of `make port-net` with the host drawing Remastered at
`--frame-rate 120` (software OpenGL) ran through it: 6,443 pictures, 1,136
of them between the game's frames, 946 not drawn because two frames were
unlike, the same 82 hashes on both. **The sound is
kept out of the rollback by the sound card's queue**, a cheaper form of
section 3's "later" that leaves the chip in the state: the window
remembers how many samples it queued at each blank, and when a rollback
begins it takes back what the queue still holds of the blanks to be run
again, all but 23 ms of it (`port_TakeBackRing`); the frames run again
are mixed as before, and when they are done their newest samples, as
many as were taken back, go into the queue in their place
(`g_port_rerun_sound_hook`, `port_SoundRerunEnds`). Nothing is heard
twice and the queue keeps its length; what the card had already played
of the frames taken back stays played, so a sound the correction adds
is heard from where the queue's 58 ms reach back, not from its start.
Section 3's whole design (key-ons replayed when frames become final)
would take the chip out of the state, and the game reads which voices
are free: the two machines' sound libraries would then choose voices
differently, and their states would differ.

**The line** (`port_DrawNetLine`, `sdl_net.c`): at the top of the
window, in the settings menu's font at the window's own resolution, as
crisp in 4K as at 960 by 720: `Online: player 1 | ping 48 ms | rollback 3
| delay 2`, the rollback being the longest of the last three seconds;
or `Waiting for the other player 1.2 s`, `Loading: waiting for the other
player`; or for eight seconds `The other player has left: you play on
alone`. Before the game starts a panel says how the session's set-up is
going. None of it is in the game's video memory or in a test's picture.

**Leaving.** The window's close ends the program (Escape is the game's
pause and back, and never ends it); the other
is told (five datagrams), answered for a moment so that it has every
input, and **plays on alone** from the last frame it had both pads for:
its pads are both its own again and the settings menu opens again. A
program that hears nothing for 15 seconds does the same. The headless
program ends instead, as before. A desync still ends both.

**The settings menu's Play online page** (`src/port/window/menu.c`):
host or join; the port to wait at, or the host's address and port
(typed: digits, dots, a colon); whether the program is found through
every network of the PC or on this PC alone; the delay. They are kept in
`rebushido.cfg` (`net_role`, `net_port`, `net_address`, `net_listen`,
`net_delay`). *Host now* or *Join now* begins the session in the
running program (section 15), as the online pages do. Starting the
program again as the session's (`port_StartOnline`, `sdl_main.c`: the
same command line without the network's options, with `--host` or
`--join`; `execv` on Linux, `_spawnv` on Windows) is kept only as the
fallback where the program has no state of the game's start, and the
log says so. The menu is refused during netplay, as before.

**The bind address.** The Play online
page's `net_listen` is `all` with nothing said: a host is found through
every network of the PC (`0.0.0.0`), which is what play between two PCs
needs, and opens a port to the network while it waits. `local` keeps it
on 127.0.0.1. On the command line `--host` always names its address, and
`--join` binds `0.0.0.0` and a port of the system's unless `--net-bind`
says otherwise; a bind of `0.0.0.0` is of IPv6 and IPv4 at once where
the system has IPv6. How the two get through their routers (the
router's own mapping, hole punching, a relay, all built) and the lobby
that finds them each other is section 14; a platform's networking
(Steam's) is not needed for any of it.

**Proved** by `make port-net` (`tools/port_net.py`), on one PC, every
socket on 127.0.0.1:

- `session`: two headless programs of each size, `--host` and `--join`
  with 100 ms each way and 5% lost: the meeting, the host's two new
  cards sent, the session's presses to versus select, both players'
  picks, `port_game`'s fight to the kill, played as a Stock match of two
  lives (the host's `--match stock:2`, the guest given none): the
  score's card in place of the menu after it, the next fight started by
  itself, the fight again to the kill, and then the guest's plan ends:
  the host is told and ends there. It passes if both end well, neither
  finds a desync, both hold the host's rule (the guest's log says so)
  with three lives left of four at the second fight's start, at least 40
  state hashes are the same on both and no card file was written. (The
  `window` pair below keeps the game's own menu and its Rematch.) 64-bit: about 2,490 frames, 82 hashes the same on both;
  67 and 79 rollbacks, the longest 8 frames; 32-bit: 69 and 77, the same
  82 hashes on both (each size's own, section 7). Then two more
  sessions of each size, begun in the running program with the host's
  `menu = rebushido` (PORT.md, "Match rules", "The rule first"), play a
  **Crew** match of two and a **Stock** match of two, the rule turned on
  the page by the host's pad: the crews picked at the grid, of different
  characters; the first fight's winner wounded into the second; the
  Crew match's end the top menu, where the session ends on both at the
  same frame. 64-bit Crew: about 4,200 frames, 139 hashes the same on
  both, both fights' replays alike and played back;
- `window`: the same between two of the window's programs, headless
  (dummy drivers), in real time with their sound card and pacing, 60 ms
  each way and 5% lost; the host must play on alone after the guest
  goes. 90 seconds; 82 hashes the same on both; the two were 0.8 and
  0.9 frames apart on average (the headless pairs, which nothing paces,
  4.2 to 4.8) and held back 2 and 4 times (85 and 286 ms in all); 25
  and 59 rollbacks; the sound's queue had 11,633 and 45,360 samples
  taken back and as many corrected ones queued, while 27,436 and 87,326
  had been played already;
- `window`, the menu: two window programs started as a player starts
  them open the settings menu (`--menu-at`), go to Play online and
  start, the host on 127.0.0.1 and the guest typing the host's address
  (`--menu-keys ... type:127.0.0.1:47022`); each begins the session in
  its own process (section 15), and the two play on with the same
  hashes.

The whole of `make port-net` takes about eight minutes.

`link` and `rollback` are as before (sections 11 and 12).

**What is left.**

- *Two real PCs*, which settle the delay, the window of rollback and
  the pacing's numbers, and the first play by people: the line, the
  feel of a rollback, the sound. On one PC the pacing has nothing to
  keep apart: both programs read one clock.
- *A Windows session* is run now (section 14: Windows programs headless
  and with a window, by address, through the lobby, from the menu by
  section 15); not yet between a Windows and a Linux one (which the
  build's sum keeps apart until it is proved they agree).
- The desync report's snapshots, spectators; section 14's own list.
- *The view*: computing the host's draw distance on both rather than
  the console's (above).
- *The play tests owed*: none changes; the port's play tests run with
  netplay off, and nothing they run reads what changed (the pads'
  driver gives the movie player its pads as before, the cards are files
  as before).

## 14. The lobby and the ladder of ways

Section 13's session needs the two programs to reach each other. Here
is how they do with nothing but this program on either side: no VPN,
no tool of anyone else's.

**The ladder.** The host's program first asks its own router to open
its port (*the router's mapping*, below). The two then meet at the
**lobby** (`tools/relay/`), which tells each the other's addresses, and
both try every way at once, taking the best that works:

1. **IPv6, directly.** Each socket is of both kinds where the system
   has IPv6 (a bind of `0.0.0.0` opens `[::]` with `IPV6_V6ONLY` off,
   `net.c`; `--host [x::y]:P` and `--join [x::y]:P` name IPv6 too), and
   each offers its global IPv6 address (found by a socket connected to
   a public address, which sends nothing: `port_HostOwnAddress`). There
   is no NAT there; the routers' stateful firewalls are opened by the
   two sending to each other at once.
2. **The host's mapped port** (UPnP, NAT-PMP or PCP).
3. **A punched hole**: the address the lobby saw each at, and each one's
   address on its own network; a punch sent to each opens the sender's
   router to the other, so the other's punch comes through.
4. **The lobby's relay**: the server passes the two's datagrams on, so
   that a session is had however the routers are.

Each way is a **candidate**. A punch answered (`PUNCH_ACK`) proves a
way works in both directions, and each tells the server which kinds it
has had answered (`PUNCHED4`, `PUNCHED6`). The server decides for both
alike, so that the two never take different ways: IPv6 if both have it,
else IPv4 (waiting a second for IPv6 where either offered it), else
after three seconds the relay. Each program's peer is then the address
its answer came from, or the server; the session's own checks of
section 13 (protocol, build, disc, gameplay mods, the cards) run over
that way unchanged. The window's line says which way it is and its round
trip (`Online: player 1 | IPv6, direct (12 ms) | ping 24 ms | ...`).

With no lobby, the advanced ways stay: `--host` and `--join` by
address, the host's port mapped by its router, and **a punch by
address** (`--punch-to`): each player reads their own address off the
Play online page (*Your address for the other player*: the router's
mapping, or what the lobby sees), sends it to the other, the host types
the joiner's, the joiner keeps the port it said (`net_port`), and both
start at once; the host sends to the joiner while it waits, as the
joiner's hellos do the other way.

**The lobby** (`tools/relay/rebushido_relay.py`, `README.md` beside it)
is one process of standard-library Python, UDP alone, on the owner's
server (`PORT_NET_LOBBY_DEFAULT`, `135.181.108.74:7845`; `net_lobby` and
`--lobby` name another). It keeps rooms in memory, nothing on disk: a
room's code, its name, its host's name and setup code, its password's
salt and verifier, and the two addresses. It reads no game datagram:
it passes on those of a relayed room, between that room's two addresses
alone, at most 240 a second and 300 KB a second each room. It logs
counts only (`--verbose` for more). Its limits: 500 rooms, 3 a host's
address, 6 made a minute, 30 hellos a second, 10 lists a second, and 5
wrong passwords a minute before an address is shut out for a minute.

**Rooms.** A host's hello opens a room, with a name and its host's
setup code; the server gives it a code of six letters and digits. The
list (`LIST`, `ROOMS`) shows each room's name, its host, its setup code,
whether it is locked and has one player or two. A room is gone when its
host has said nothing for 20 seconds while it waits, 30 after a direct
session began, or 60 after a relayed one's last datagram. A guest asks
for a room by its code; one whose setup code is not the host's is
turned away by the server itself (`INCOMPATIBLE`, with the host's code,
so the guest's screen says which part differs).

**Passwords** never go over the network as typed. The host's program
makes a salt and gives the server the salt and `SHA-256(salt ||
password)`; the server keeps only those, for the room's life. A guest
of a locked room is sent the salt and a fresh random challenge and
answers `HMAC-SHA256(SHA-256(salt || password), challenge)`; the server
compares it with its own (`hmac.compare_digest`). The page keeps a
password in memory alone, never in `rebushido.cfg`, and starts the
session again with the key made of it (`--room-key SALT:KEY`), not the
word. `relay.c` has its own SHA-256 and HMAC, as the program without a C
library has no other.

**The wire.** Every datagram has the port's header (`RB`, a kind, a
detail byte, a number); kinds 48 and up are the server's, all below it
the game's. Words are little-endian; text fields are fixed, zero-padded
printable ASCII.

| Kind | Who | Bytes | What |
| --- | --- | --- | --- |
| 48 `HELLO` | client | 208 | role and flags; the room's code, name, the player's name, the setup code, the salt, the verifier (host) or proof (guest), up to four candidates |
| 49 `STATE` | server | 208 | waiting, password (salt, challenge), punch (the other's candidates, the token), direct4, direct6, relayed, or an error; the code, the address seen, the other's |
| 50, 51 `PUNCH`, `PUNCH_ACK` | client to client | 16 | the token, the candidate aimed at, the sender's clock (echoed: the round trip) |
| 52, 53 `LIST`, `ROOMS` | client, server | 16, 8 + 92 each | up to twelve rooms a datagram (and rooms of several, section 16) |
| 55, 56 `WHOAMI`, `YOUARE` | client, server | 16, 28 | the address the server sees |

An address or a candidate is 20 bytes: its family or kind, the port,
and 16 bytes of address (IPv4 in the first four). The full layout is in
`relay.c`'s enums and `tools/relay/relay_client.py`, which the server's
test (`tools/relay/test_relay.py`) drives: every state, error, limit and
expiry, IPv4 and IPv6, the relay's forwarding and its refusal to pass on
a third address's datagrams.

**The router's mapping** (`src/port/window/portmap.c`, the window's
build alone: it speaks HTTP, through the C library): PCP (RFC 6887),
then NAT-PMP (RFC 6886), then UPnP's Internet Gateway Device (SSDP's
search, the device's description, `GetExternalIPAddress` and
`AddPortMapping`, falling back to a lease of 0 where a router takes
only permanent ones, and to the next port where the one asked is
taken), within four seconds; a lease of two hours, asked again when half
has gone (`port_PortMapTick`, each blank of netplay), and given back as
the program ends (`port_EndSession`). A mapping whose outside address is
private or the providers' shared space (another router stands before
this one) is not offered as a way. What it found is said where the host
waits ("UPnP opened your router's port: the other player joins
203.0.113.7:7845") and in the log. `tools/relay/fake_router.py` is a
router of all three for the tests, and `tools/relay/test_portmap.py`
holds `portmap.c` to it on Linux and Windows.

**Checking two players' setups.** `rebushido --check-online` prints the
build's, the disc's and the gameplay mods' sums and the protocol, and
the **setup code** made of them, `P10-BBBB-DDDD-MMMM` (each sum folded to
20 bits, four digits of Crockford's base 32); the Play online page's
last row shows it, the lobby's list shows each room's, and a refusal
shows the player's own. Two players with the same code meet.

**The disc's sum, redumped.** Section 13's sum read the image's raw
sectors and its size. Two dumps of one disc differ in what a dumping
program writes of its own: an image whose form 2 sectors' EDC was
written as zero (as some programs write it, and as the disc may hold
it) and one with 150 sectors of run-out after the volume had other sums
than the owner's Redump image, though the game reads the same from all
three. The sum (protocol 7) now takes the volume's size from its own
description (sector 16), not the file's, and each sector's subheader
and data (2048 bytes of form 1, 2324 of form 2), not its sync, address
or error codes; and it reads the disc with no stage mod's files laid
over it (those are the gameplay mods' sum). Tried on images made from
the owner's (scratch, deleted after):

| Image | Old sum | New sum |
| --- | --- | --- |
| the Redump image | `fe20bc34015c8494` | `8defc7e1a5290e7f` |
| its form 2 EDC zeroed (153,577 sectors) | `0ee491bce942bc7c` | `8defc7e1a5290e7f` |
| 150 sectors of run-out added | `50274db961e28307` | `8defc7e1a5290e7f` |
| 2 seconds of pregap before it (`INDEX 01 00:02:00`) | the port stopped (below) | `8defc7e1a5290e7f` |
| the same image under another name or cue | the same | the same |

What changes the sum is what changes the game's data: another region,
a hacked or damaged image, a missing sector. The port reads `.cue` and
`.bin` of raw 2352-byte sectors alone; `.iso` (2048-byte sectors, which
lose the XA sound and the movies), `.chd`, `.ecm` and `.pbp` it does not
read at all. The pregap image stopped the port where it began: the cue
sheet's `INDEX 01` was not read, so every sector was looked for 150
sectors early, the disc had no directory, and `port_FindStageFile` read
a table it had not found. The cue's first `INDEX 01` is now where the
data begins (`PortDisc.first`, `disc.c`), and a disc with no directory
no longer stops it.

**Refusals on the screen.** A guest turned away, a host not found, a
lobby that does not answer, a wrong password, a full, closed or
incompatible room, or a host that goes quiet once the guest is in
(after `SILENT_GIVE_UP_MS`, 15 seconds) is said on the guest's screen in
plain words (`g_port_net_fail_hook`, `sdl_net.c`: a panel until Enter
or Escape, then the program ends; in a session begun from the online pages they say it instead, and the program goes on: section 15) with the guest's setup code to
compare; a guest that finds the host is not its kind tells the host
too. The host's panel says it turned a player away and why, or that its
guest stopped answering, and waits for another (its room's guest let go
at the lobby: `HELLO_RESET`). Section 13's refusal texts stay in the
log.

**Windows.** A UDP socket of Windows fails its next receive when
something it sent was answered "port unreachable", as a hello sent
before its host is up, or a punch to an address not yet open, is: the
port's sockets turn that off (`SIO_UDP_CONNRESET`, `host_windows.c`).
The fallback start again (section 15, `_spawnv`) says the new
process's number, which the tests wait on.

**Proved** on this PC, every socket on loopback (127.0.0.x and ::1),
with the server run here, by `tools/port_net_lobby.py` (and its
`headless` half in `make port-net`), each case a session of section
13's plan, a fight to the kill and a rematch, with 60 ms each way and 5%
lost unless it says otherwise:

| Case | Programs | The way | Result |
| --- | --- | --- | --- |
| `punch` | headless | a locked room, the right password; a punched hole between 127.0.0.2 and 127.0.0.3 | 82 hashes the same on both; 61 and 77 rollbacks |
| `relay` | headless | `--net-paths relay`: the lobby passing every datagram on | 82 the same; 63 and 80 |
| `mapped` | headless | the host's mapped port (`--mapped`), the only way the guest tries | 82 the same; 70 and 75 |
| `v6` | headless | IPv6 on ::1, the lobby too | 82 the same; 70 and 72 |
| `direct6` | headless | no lobby: `--host [::1]:P`, `--join [::1]:P` | 82 the same |
| `punchto` | headless | no lobby: `--punch-to` and a guest of a fixed port | 82 the same |
| `list` | headless | the lobby's list while a host waits | its name, host, 1/2, locked, its setup code |
| `refused` | headless | a wrong password, none, an incompatible disc, no such room, a lobby that does not answer | each turned away, and said why |
| `win-headless` | Windows, headless | section 13's session, 100 ms each way, 5% lost | 82 the same; 72 and 83 rollbacks, the longest 8 |
| `win-window` | Windows, window (hidden) | the same in real time, 60 ms, 5% | 82 the same; 0.7 frames apart on average; the host plays on alone |
| `win-menu` | Windows, window | the Play online page by address | both began the session in their own processes (section 15) and played on, the hashes the same |
| `win-rooms` | Windows, window | *Host a room* with a password, *Browse rooms*, *Look for rooms*, the room chosen and its password typed | both began it in their own processes, met through the lobby (a punched hole), the hashes the same |
| `win-portmap` | Windows, window | the fake router's UPnP: `AddPortMapping` as the host began, `DeletePortMapping` as it ended; the guest came by the mapped port | 82 the same |
| `win-refused` | Windows, window | another build, disc, mods and protocol (`--session-differ`), nobody at the address | each said on the guest's screen and the host's |
| `win-quiet` | Windows, window | the host stopped (`NtSuspendProcess`) once the guest was in | the guest's screen: "The host stopped answering"; the host, woken, waits for another |
| `win-lobby-refused` | Windows, window | a wrong password and an incompatible build at the lobby | each said on the guest's screen |

The `headless` half (on Linux, `make port-net`; on Windows the same with
the Windows headless program) takes under half a minute, the whole of
`windows` about a minute and a half with its cases side by side
(`python tools/port_net_lobby.py windows`, the gate's `port-net-windows`).
The window's programs are run with `--hidden` (SDL's dummy sound device,
whatever the environment says), SDL's dummy video and every controller
backend off (`NO_CONTROLLERS`, `tools/port_net.py`: a controller plugged
into the PC was read by the hidden windows and its drift pressed into
their fights); the harnesses refuse to start one otherwise, and fail a
run whose log names a sound driver other than the dummy.

**Through the owner's server, across the internet**, two Windows
programs of this PC (`tools/port_net_lobby.py internet --lobby
135.181.108.74:7845`):

INTERNET_TABLE

**What is left.** Two players' PCs on two networks, behind two real
routers: the hole punching and the router's mapping have met only the
fake router and the loopback here. A symmetric NAT on both sides takes
the relay, as it must. The lobby's room is gone from its list once a
direct session is under way (the host says nothing to it then). Names of
servers (`--lobby` takes numbers only: the port without a C library has
no resolver).

## 15. A session begun in the running program

Sections 13 and 14 began every session with a program started for it:
the Play online page started the program again (`execv`, `_spawnv`)
with `--host` or `--join`, so that both peers began from the same
fresh state, the host's cards in both slots. Now a session begins and
ends in the program the player is playing (the owner, 2026-11-20:
"In-game menus and lobbies, no 'restart game' requirement to host or
join"). `src/port/online.c`, `include/port_online.h`; the pages,
`src/port/window/online_pages.c`.

**The state of the game's start.** Section 7's snapshot holds the
whole state: the console's memory, the game's stack with its stack
pointer, every variable of the game's, Sony's and the port's console.
As the program begins, after `port_StartGame` has made the game's fiber
and before its first step, the window's program (and the headless one
with `--session-at`) saves it once (`port_OnlineKeepBoot`): the game's
start, not yet run. A program started for a session has, at the same
moment, the same state but for what the session puts in before the
game starts: nothing of the meeting is the state's (`session.c`,
`net.c`, `relay.c`, `netplay.c` are outside it, `config/port_state.txt`),
and what `port_StartSession` puts in after the meeting -- the host's
cards in both slots (`card.c`), its switches held (`play_switches.c`,
`arenas.c`), the margin fixed (`remaster.c`) -- is put in again here.
So `port_StartSession` is now two halves, `port_MeetSession` (the sums,
the lobby, the way, the checks, the cards and the seed) and
`port_ApplySession`, and a session begun in the program runs, at a
boundary of the player's own game (`port_OnlineBoundary`, `main.c`):

1. the meeting, its socket opened now and the window's router asked
   (`g_port_online_map_hook`), the online pages showing it;
2. the player's own state saved (`port_StateSave`, a second buffer), and
   their switches and stage defaults kept;
3. the state of the game's start put back (`port_StateLoad`), the
   session applied, the draw distance the console's, netplay told it is
   in the program (`port_SetNetplayInProcess`);
4. `port_RunNetplay` from there, as in a program started for it: its
   first `port_StepGame` begins the game's main on the fiber as it was
   made, since the fiber's frame is in the stack the snapshot keeps.

The paged snapshots (section 7, "Only the pages written") make the two
loads cheap and need nothing new: a load from a snapshot older than the
newest marks its pages as of the epoch now, so the netplay ring's next
saves copy them.

**Proved.** `port_net.py inprocess` (part of `make port-net`) holds it to
a program started for the session, in both sizes, with a fixed seed
(`--net-seed`) so that the two runs can be compared hash for hash:

| Case | What runs | Result, 64-bit and 32-bit |
| --- | --- | --- |
| `restart` | the pair of section 13's session, `--host` and `--join` from the start, port_net's plan (a fight to the kill, a rematch) | the reference: the first frame's hash, 82 hashes |
| `inprocess` | the same pair, each playing its own game first (the host 240 frames, the guest 120) and then beginning the session (`--session-at`) | the first frame's hash and all 82 the same as the reference, on both sides |
| `again` | one host process, two sessions one after the other (`--sessions 2`), two guest processes one after the other | both sessions' 82 hashes the same as the reference: nothing of the first is left in the second |
| `leave` | a session to the versus select screen, where the guest's Triangle takes both back to the top menu | ended on both at the same frame, each back to its own game; the programs end well |
| `refused` | a guest of another build (`--session-differ build`) | turned away at the meeting, and back to its own game in the same process |

The first frame's hash (`netplay: the session's first frame, hash H`,
printed by both) is `AC3FE9C01064D55B` in the 64-bit program and
`97C379FEA8862E86` in the 32-bit one, the same in the restarted pair and
in every session begun in a running program, whatever that program had
played before.

**The end.** A session begun in the program ends, on both sides, where
both players go back to the top menu (the host's rule, below), where
the other program has gone (its window closed, its plan ended), where
nothing is heard of it for 15 seconds, or at a desync, which no longer
ends the program. `port_RunNetplay` returns why
(`PORT_NETPLAY_END_...`); the other is told and answered for a moment
(`port_EndNetplay`), as before; then everything of the session is let
go (`port_StopNet`, `port_ResetNetplay`, `port_ResetSession`,
`port_ResetRelay`, the router's port given back), the player's switches,
stage defaults, margin and picture are theirs again, and their own
state is put back. The game goes on at the boundary it was at, with
their cards and unlocks, and the window's pages say why it ended.

**Leaving by the game's own menus.** A session begun from the pages
carries the host's rule `PORT_SESSION_RULE_TOP_MENU_LEAVES` in its
welcome (protocol 8). Under it, the session's presses no longer take a
way back to the top menu straight on to versus: once the game has been
at the select screen, its top menu again -- the main menu after a fight,
or Triangle on the select screen before either player has chosen -- is
the session's end. `port_SessionInput` notes the frame
(`g_port_top_menu.session_left`, in the state, so that a rollback takes
it back) and presses nothing more; netplay ends the session at the first
turn where every input up to that frame is the other's own, not a
guess, so that both end at the same frame of the game. The game's own
after-fight menu keeps its Rematch and select screen.

**The lobby before the fight.** Protocol 8 also carries each player's
name in the hello and the welcome, and adds two datagrams: the host's
`KIND_LOBBY`, every 100 ms once the guest has the cards, saying whether
its player is ready, and either's `KIND_LEAVE`. The guest says it is
ready (`KIND_READY`) only when its player is, and the host says go only
when both are (`g_port_session_ready_hook`; with none, as in the
headless program and a session from the command line, at once). A
guest that leaves the lobby lets the host wait for another; a host that
leaves tells the guest, whose pages say so. The pages' Back gives up a
meeting at any point (`port_CancelSession`, which the meeting's loops
and the lobby's client look at every turn).

**The lobby server and a second room.** The server keeps a room of a
host's address for a while after a direct session began, its old guest
in it. A host's first hello of a meeting now says `HELLO_RESET`, so
that the server lets that guest go and the room waits again: the
second room of a running program is the first's, under its old code.

**The pages** (`online_pages.c`): Online Play on the top menu opens
them (the count of `port_TopMenuAsk`, `sdl_menu.c`) over the game,
stopped at a blank as the settings menu stops it, drawn at the window's
resolution in the top menu's look (the port's typeface, `port_TextDraw`;
none of it in the game's video memory): Quick Match, Host a room,
Browse rooms, Join by code, Join by address, Your name, Back; a
keyboard drawn on the page for a pad; and, while the two meet (drawn by
the session's waits at the boundary), the host's *Waiting for a player*
with the room's code and what the router said, the cards' progress, and
*Ready to fight* with both names and the setup code, Ready and Leave. A
refusal, a failure or the session's end comes back to them in a
player's words (`g_port_net_fail_hook` is the pages' while they meet,
and the program goes on). Quick Match lists the lobby's rooms and joins
the first of the same setup code, unlocked and not full, or opens a room
of the player's own. While they or a session are up the updater neither
downloads nor asks (`port_UpdateSetOnline`). The settings menu's Play
online page asks the same way (`port_StartOnline`), with its advanced
rows. The window's own state is told a load happened
(`port_WindowStateReplaced`): nothing is drawn between frames until two
more, video memory goes to the card again, and the settings menu's
count of asks follows the state's.

**Proved in the window** by `tools/port_net_lobby.py` (`win-pages` and
`win-pages-refused` on Windows; the same cases through the Linux window
in `port_net.py window`), the windows hidden, with dummy drivers:

| Case | What runs | Result |
| --- | --- | --- |
| `win-pages` | a host window opens a locked room from its pages (Host a room, a password typed on the keyboard page); a guest window finds it in Browse rooms, types its password and joins; both say Ready; at the select screen the guest's Triangle ends the session on both; the host, back at its own game, takes Quick Match, and a second guest window's Quick Match finds its room | two sessions in one host process, never started again; 17 and 17 state hashes the same on both sides of each; Windows and Linux |
| `win-pages-refused` | a guest window of another build joins a room by its code, typed on the keyboard page | the lobby turns it away; the pages say which part of the setup code differs, and the program goes on |
| `win-menu`, `win-rooms`, port-net's `menu` | the settings menu's Play online page, by address and by a browsed room with its password, with Ready in the lobby | both programs begin the session in their own processes; 35 state hashes the same |

The whole of `python tools/port_net_lobby.py windows` passes, the earlier
cases with them (each session's checks, the refusals, the router's
mapping, the host gone quiet), and `make port-net` on Linux.

Pictures of every page are in the cases' `shots/` folders
(`--online-keys ... shot`, `--menu-shots`).

**The fallback.** Where no state of the game's start was kept (no
memory for it, or a program that never keeps one), the pages' ask
starts the program again as before, and the log says why.

**What is left.** Two players' PCs. (The lobby's list shows a room's
ping, and a player leaves the middle of a fight with F1 twice: section
16.)

## 16. Rooms of several, spectators and a rotation

ROADMAP 22: a room at the lobby that holds several players and
spectators; spectators who watch a match live, from its players'
inputs, with no rollback of their own; a rotation of three (A+B, B+C,
C+A), the winner staying or a fixed turn as the room's host sets; and
matches beside the room's. Built on section 15's sessions in the running
program. The code: PORT.md, "Rooms of several".

**The design.** A room of several is the lobby's; each match in it is a
session of two, as every session before.

- *The room* (lobby version 2) holds up to eight members, each known by
  a token the server gives it, not by its address, so that a member's
  sessions may come from other sockets. A member says it is there every
  second (`ROOM` with `STAY`) from whatever it is doing (its pages, its
  own game, a meeting's loops, netplay's frames, a spectator's); the
  server answers every word with the room as it has it (`ROSTER`) and
  lets go of a member it has not heard from for 20 seconds.
- *The rotation is the server's.* It keeps the members in the order
  they came and, where the host has a rotation on and two who play are
  free, makes the next match of the first two free: with **a fixed
  turn** the match's first player then goes to the end of the order
  (A+B, B+C, C+A, A+B ...); with **the winner staying** the winner goes
  first and the loser last (a draw: both last). A player who leaves a
  match, or the room, in the middle of it loses it. The server alone
  decides, so every member sees the same order, and it is held by
  `test_relay.py`.
- *A match is met as a two-player room is*, under a code the server
  gives it (not listed), and let in by a **ticket**: sixteen random
  bytes the roster gives its two players alone, in the hello's secret
  (`HELLO_TICKET`), in place of a password. The match's first player
  hosts it (player 1, its cards and switches); every way of section 14
  is open to it. A session of a room's match has no lobby before the
  fight and ends where the match is decided (`ONE_MATCH`, below); each
  player tells the room who won (`RESULT`), said again with each word
  until the room shows it taken, and the room makes the next.
- *Matches beside the room's* are the same, but asked for: a member
  opens one (`SIDE_OPEN`) and another free member takes it
  (`SIDE_JOIN`); the rotation goes on meanwhile without the two. They
  are numbered from 1001, the rotation's from 1.
- *Spectators pull from the lobby, not from the players.* The match's
  host sends the server the match's **feed** once, whatever the
  spectators' number: the session's settings (the host's switches, rule,
  margin, 64 bytes, `port_SessionSettings`), the **start of its latest
  fight** as a replay keeps it (PORT.md, "Replays": the state before the
  fight's load, what the disc holds left out, deflated, about 215 KB),
  and from that start's frame on every frame's two pads, once both are
  known (never a guess), with every final hash. The server keeps the
  feed in memory while the match lasts and 30 seconds after, and gives
  every member who asks (`WATCH`) what it lacks (`SCENE`): the
  settings, the start's chunks it has not, then the inputs from the
  frame it wants, eight hashes to a datagram of 240 frames. A host's
  upload does not grow with its spectators; the server's is about 215
  KB a spectator as it comes in, then a few KB a second.
- *A spectator* waits for a fight's start, keeps its own game (as a
  session's player does), puts the start in (`port_ReplayLoadServed`,
  the start's hash held to the host's), holds the session's settings
  (`port_WatchSession`) and runs netplay's frame with the players' two
  pads: the session's own presses, the replay's boundary, the step. It
  never guesses, so it never rolls back; it holds each hash it makes to
  the players' as they come, and stops at the first that differs. It
  keeps six frames in hand behind the newest it has, so that inputs
  coming in bursts are not seen; one who comes in the middle of a fight
  begins at that fight's start and runs quietly (`g_port_rerun`, as a
  rollback's frames) until it is with the players. The match decided
  in its state, or its end said by the feed, it puts its own game back.
  The way in is the fight's start rather than the session's power on:
  the latter needs the host's cards sent and every menu frame run
  again, minutes of a Stock match, where a fight's start is a few
  seconds of frames at most; before the first fight a spectator waits
  ("waiting for their fight to begin").

**The match's end.** `PORT_SESSION_RULE_ONE_MATCH`, in the welcome of a
room's match: where the game reaches its menu after a fight, of a single
fight or of a Stock or First to match's last (PORT.md, "Match rules"),
the session's presses (`port_SessionInput`) note the frame and the
winner in the state (`g_port_top_menu.session_left`, `.session_result`)
and press nothing more; netplay ends the session there on both machines
once the frame's inputs are both the other's own (`END_MATCH`), as
section 15's way back to the top menu does. A program now looks at the
session's own end before at the other's goodbye: the other, ending at
the same frame, may have said goodbye first, and the ending was a
"peer left" with the wrong winner (found by the rooms case below).

**Leaving a fight.** F1, Escape in a fight (Start, nobody's online) or
the guide button ask to leave; the window's line says *Press F1 or
Escape again*, and a second ask within three seconds leaves at the next
frame (`END_QUIT`): the other is told (`BYE`), its session ends as one
whose other player left, and both are back at their own games and the
pages, in a room on its page. A spectator leaves its watching the same
way.

**The list's ping.** Each hello and each room's word carries the
sender's round trip to the server, measured by its own hellos (the
hello's two bytes after its count of candidates; a room's `STAY` its
stamp, echoed). The list gives each room's host's, in the two bytes
after its count of players, and the browsing program adds its own round
trip to the server, measured by the list's ask: *ping ~N ms*, the way
through the server, about what a direct way costs at most. The host's
address is never given.

**The wire, version 2.** Everything of section 14 stays, with three
changes: `RELAY_VERSION` 2 (a version 1 client is answered `VERSION`, and
a version 2 client a version 1 server's `VERSION`: each says "The lobby
server is of another version"); `HELLO_TICKET` (0x20); the hello's and
the list's round trips. The list's locked byte has a bit for a room of
several (0x02), whose players are its members. New kinds, little-endian:

| Kind | Who | Bytes | What |
| --- | --- | --- | --- |
| 57 `ROOM` | member | 160 | an action (`OPEN`, `ENTER`, `STAY`, `LEAVE`, `RULE`, `RESULT`, `SIDE_OPEN`, `SIDE_JOIN`, `SIDE_DONE`): the room's code, name, player, setup code, salt and verifier (open) or proof (enter), the member's token, whether it plays, the rule, an argument, a stamp, its round trip, a match's number |
| 58 `ROSTER` | server | 472 | OK, `PASSWORD` (salt, challenge) or an error; the code, name, the member's token and id, the rule, the host, the setup code, the rotation's match (number, players, code, ticket to its players), the last result, eight members (id, flags, wins, round trip, name), four matches beside it |
| 59 `FEED` | a match's host | to 1,200 | the match's code and ticket; its settings, a chunk of the start (its frame, size, place, count), inputs (the first frame, how many, up to eight hashes, four bytes a frame), or the end (the frame, who won) |
| 60 `FEED_ACK` | server | 160 | the start's frame and its chunks had, the settings and the end had, the next frame wanted |
| 61 `WATCH` | spectator | 192 | the room's code, the token, the match's code, the start it has and its chunks, the frame it wants |
| 62 `SCENE` | server | to 1,144 | the settings and the two players' names, a chunk of the start, inputs with their hashes, the end, or nothing yet / no such match / not a member |

`tools/relay/relay_client.py` builds and reads every one, and
`tools/relay/test_relay.py` holds the server to them: the rotation's
order with a fixed turn (four matches) and with the winner staying
(through a draw), a result told twice or by one not in the match, the
host's rule, a match's meeting by its ticket (before and after its host,
a wrong ticket, none), matches beside it, a member leaving in the middle
and the host leaving, a feed served to a spectator who comes in the
middle (the start whole, the inputs from its frame with their hashes,
inputs that overlap), a feed of a wrong ticket, a stranger's watch, a
locked room's password, an incompatible setup, a ninth member, version
1, and a silent member let go.

**Limits and memory.** A room of several counts as a room against the
server's 500 and a host's three; its matches do not count against their
hosts' addresses. Per source address, `--watch-rate` (400 a second) for
`WATCH` and `FEED` together; a start of at most 1 MB and 2^18 frames of
inputs a feed. A feed is about the start's size and 4 bytes a frame: a
few hundred KB a match, kept while it lasts and `--feed-linger` (30 s)
after. `--member-timeout` (20 s) and `--side-timeout` (120 s) are the
rest.

**The protocol of the session**, 10 (setup codes `P10-...`): the rule
`ONE_MATCH`, the result in the state and the leave. The setup code's
protocol is written in full now, two digits past nine.

**Proved** on this PC, the server run here, every program headless
(`tools/port_net_lobby.py`'s `rooms`, in `make port-net` and the gate's
`port-net-windows`) or a hidden window with SDL's dummy drivers
(`win-room`, and `room` in `port_net.py window`), 60 ms each way and 5%
lost on every match's session:

| Case | What runs | Result |
| --- | --- | --- |
| `rooms` | six headless programs in one room: Ayame opens it (a fixed turn), Bunta and Chie play, Sora is a spectator who comes in once the first match's fight has begun, Dai and Eiko play a match beside the room's; Bunta leaves the second match in its fight (frame 1,700) | the rotation Ayame+Bunta, Bunta+Chie, Chie+Ayame; each match watched by the third player and by Sora; every hash of a match the same on its two players and its spectators: 67, 56 and 67 hashes, the spectators holding 21, 10 and 21 of them to the players' as they came (every one from the fight's start); Sora ran the first match from its fight's start, frame 1,387; the match beside the room's, 67 hashes, at the same time; Bunta's leave ended the second match on both, Chie its winner, both back to their own games and the room; about a minute |
| `win-room` | three Windows windows: the host opens a room of several from its pages (Host a room, Room for: several), two windows of the command line come in; the host plays the first match and leaves it in its fight with Escape twice (`--escape-at`); back on its pages, in the room, it watches the second | each match's hashes the same on its two players and its spectator (one of them the host's window from its pages); the host left at frame 1,950, in the fight; 100 seconds |

**What is left.** Two players' PCs, and a room of real players: the
feed's and the watch's pacing over a real network (on one PC the
spectators keep up at once). A spectator of a match's menus before its
first fight sees nothing until the fight begins. Matches across builds
are refused by the setup code as before. A ladder and Elo (ROADMAP 25)
can take a room's results and the replays' starts as their check.
