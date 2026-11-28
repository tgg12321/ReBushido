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
window programs on one PC (section 13). Then the lobby and its ways
(section 14), sessions begun in the running program (15), rooms of several
(16), the ranked ladder (17), two on two online, four players on four
PCs among them (18), the CPU playing on for one of four who leaves
(19), teams of four characters (20), a resync where the machines part (21),
and a tournament in a room (22). What is left
is play between two real PCs,
and what section 13 leaves to the owner.

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
run, or for a program whose game lies where this one's does. A replay's
start is the one snapshot written to a file (and a room's spectator puts
the host's in), for another run of a build of the same **core**, which
keeps its own hooks, cards and `run` group out of the load ([`PORT.md`](PORT.md),
"Replays").

**What lies below the stack pointer** (2026-11-26). A function of the
game's that reads a local it never set reads what was last left there:
before, whatever the host had run on the game's stack (a take hook, a
replay's or a plan's) and what a snapshot's save had cleared, so two runs
of the same game with other hooks, or saving at other boundaries, could
read otherwise. It is now cleared as each input is taken
(`port_FiberClearDeadStack`), in every run alike (PORT.md, "Across
builds: the lead-in").

**The core and the build's sum** (2026-11-25). A snapshot holds pointers
into the program's code and variables, and the game's stack, whose
frames return into the code (section 8): a program that puts the state
of another in has to have the same code at the same addresses. The
window's build links the game, Sony's code and the port's own into one
object first (`tools/port_sdl_build.py`); the part of it that is not the
window's files (`src/port/window/`'s arithmetic) nor the version's is
its **core**, first in each kind of that object, its code one section
(`g_port_core_text` to `g_port_core_text_end`, and the same of the
constants, the variables and the zeroed), and the switch of stacks is in
it (`src/port/fiber.c`). The window's hosted files keep all their code
in `.text`, after the core's (Linux: `-fno-reorder-functions
-fno-reorder-blocks-and-partition`). So a window that differs moves its
own code and none of the core's, nor, while it grows within a page, the
core's variables. `port_StateBuildSum` (`state.c`) sums where the core
lies (each kind's two ends, the state's runs), the size of a pointer,
the system and `g_port_code_sum`, which the build takes from the object
before the program's link: the core's bytes, how large its zeroed
variables are, and every relocation with the name it reaches
(`port_sdl_build.code_sum`, into `code_sum.c` beside the object; 0 in
the program with no window, whose version stands for its code). Before,
the sums held the place of `port_Main` and of all the program's
variables: on Windows the game's `main` (`.text.startup`) and its cold
code were put after the window's code and SDL's, a window that differed
by a few bytes moved them and passed, and a replay's start, put in,
returned into the wrong code ("memory that is not there", exit 70).

The session's build (`session.c`) is the game's version (with its
commit), the state's size and `port_StateBuildSum`. Two programs whose
window alone differs keep their states' hashes equal (the hash reads no
address of the window's) and may take each other's start (a spectator):
they would be safe to meet. The version keeps them apart all the same,
as it keeps two releases apart, which the lobby and the relay pair
players by; that is the owner's to loosen.

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
processor and system (`include/port_fiber.h`, `src/port/fiber.c`): it keeps the registers a
called function must keep, and on Windows the thread's record of its
stack's two ends (and, in 32 bits, its chain of fault handlers), since
Windows hands a fault to nobody while the stack pointer is outside the
stack it knows. `fiber.c` gives `port_HostEnterGame` and
`port_HostLeaveGame`, and the three hosts (`host_linux.c`,
`host_windows.c`, `window/host_posix.c`) `port_HostStartGame` on
`port_FiberStartGame`, in place of the old `port_HostCallOnStack`. The
switch and the game's first frame are in the game's object, not the
host's file, so that the addresses the game's stack returns to are the
core's (section 7, "The core and the build's sum").

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
  output), sends it with its inputs, and stopped with `netplay: DESYNC`
  when the other's hash of a frame was not its own; since section 21 the
  session goes on, parted, until a resync puts the host's state on both.
  The two are compared only once both are final: the other's is kept
  until this program's own is (below, "Under load").
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
  say and the guest's screen shows. The arenas' sum goes in the hello
  and the welcome on its own as well (`MEET_ARENAS`, protocol 17), so a
  guest whose gameplay mods differ in their arenas -- one without the
  host's, or with another, or another order -- is told that it is the
  arenas (`REFUSE_ARENAS`: "The other player's custom arenas are not the
  same as yours: you both need the same arenas, from the same mods."),
  whether the host has one in play (`--arena`) or only offers them on
  the stage select's arenas' page;
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
(`PORT.md`, "ReBushido's main menu"). The **versus select screen is
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
program ends instead, as before. A desync ended both; since section 21
the host's state is put on both (a resync) and the session goes on.

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
  same frame. 64-bit: Crew 76 hashes the same on both, the last at
  frame 2,280, Stock 67 (32-bit: 80 and 58), both fights' replays alike
  on both machines and played back;
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
`--lobby` name another). It keeps rooms in memory, nothing of them on disk
(the ranked ladder's store, section 17, is the one thing it writes): a
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
| 48 `HELLO` | client | 208 (232 in version 4: a room of four's seat key, its players, the seat; section 18) | role and flags; the room's code, name, the player's name, the setup code, the salt, the verifier (host) or proof (guest), up to four candidates |
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
ends the program (and since section 21 only where a resync cannot be
made). `port_RunNetplay` returns why
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

**The pages** (`online_pages.c`): Online Play on the main menu opens
them (the count of `port_TopMenuAsk`, `sdl_menu.c`) beside the game,
which runs on under them with its music and draws its Option Menu's
backdrop, drawn at the window's resolution in the look of the game's
own pages (`page_look.c`, the port's typeface; none of it in the game's
video memory; PORT.md, "The window's pages in the game's look"); opened
anywhere else (a test's `--online-at`), over the game stopped at a
blank, as before: Quick Match, Host a room,
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
count of asks follows the state's. While two players meet, with the
pages opened beside the main menu, the player's own game is stopped at
its boundary but its sound goes on, as a waiting console's does: its
music, a piece started again as it ends, and the pages' menu sounds
(`port_RunStoppedSound`; PORT.md, "The window's pages in the game's
look"). That changes the player's own state, which is saved as the
session begins and put back as it ends: the music comes back where the
meeting left it.

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
  frame it wants, eight hashes to a datagram of 240 frames (in version 4
  four pads a frame, four hashes to a datagram of 120: section 18). A host's
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
| 59 `FEED` | a match's host | to 1,200 | the match's code and ticket; its settings, a chunk of the start (its frame, size, place, count), inputs (the first frame, how many, up to eight hashes, four bytes a frame; version 4: four, eight), or the end (the frame, who won) |
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
inputs a feed. A feed is about the start's size and 4 bytes a frame (8 in version 4): a
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
| `win-room` | three Windows windows: the host opens a room of several from its pages (Host a room, Room for: several), two windows of the command line come in; the host plays the first match and leaves it in its fight with Escape twice (`--escape-at`); back on its pages, in the room, it watches the second | each match's hashes the same on its two players and its spectator (one of them the host's window from its pages); the host left at frame 1,665, in the fight (the fight is over at 1,887); 100 seconds |

**What is left.** Two players' PCs, and a room of real players: the
feed's and the watch's pacing over a real network (on one PC the
spectators keep up at once). A spectator of a match's menus before its
first fight sees nothing until the fight begins. Matches across builds
are refused by the setup code as before. The ranked ladder (ROADMAP 25)
is section 17.


## 17. The ranked ladder

ROADMAP 25 (the owner, 2026-11-21): ranked matches through the lobby
server, a rating per player, a ladder page; identities, results checked
by both players and by the replays, and the server's own care for what
it keeps. How a player uses it: [`PLAYING.md`](PLAYING.md), "Ranked". The
code: the server's half `tools/relay/ladder.py` (imported by
`rebushido_relay.py`), the program's `src/port/ranked.c` and
`include/port_ranked.h`, the pages in `src/port/window/online_pages.c`.

**The design in short.** No accounts and no email: a player is an
Ed25519 key their program made, and the server knows them by its public
half and the name they chose. A ranked match is a session of two, met as
a room's match is (section 16: a code and a ticket the server gives its
two players alone), played under a fixed rule, and reported by both
programs, each report signed and naming the match's **digest**. Reports
that agree are rated by Glicko-2; any other outcome is held, unrated,
for the owner to look at, with the replays of the match's fights if the
server asked for them. The server never runs the game (it has no disc):
it compares, it does not judge play.

**Identity.** On the first use of Ranked the program draws 32 bytes
from the system's own source of random numbers (`port_HostRandom`:
`getrandom` on Linux, `RtlGenRandom` on Windows) and keeps them as the
seed of an Ed25519 key pair (Monocypher 4.0.2, now in every program of
the port, `include/port_monocypher.h`) in a small text file beside the
settings, `rebushido-identity.key` (`--identity FILE` names another): the
seed, the public key, and a few lines saying what the file is. It is
written beside and renamed, so a half-written file is never the
identity, and a damaged one is said, never replaced. Losing the file is
losing the identity: the Ranked page's *Back up your identity* writes a
dated copy beside it (PLAYING.md says to keep one elsewhere).

**Signatures.** Every request is a `RANK` datagram of 240 bytes; the
three that change what the server keeps (`NAME`, `QUEUE`, `REPORT`) are
signed over the string `ReBushido ranked 1` and the datagram's first 176
bytes, which hold the key and a **nonce** the server gave: the first 16
bytes of HMAC-SHA256(the server's secret, key ‖ the minute), good for
that minute and the next. Every answer carries the current nonce, and a
request with an old one is answered `STALE` with a fresh one, which the
program signs again with. A signature heard on the wire is so of no use
two minutes later, and the server keeps no per-key counter to forget.
The server checks Ed25519 in Python (`ladder.verify`, the RFC's
arithmetic; about 2 ms each), at most 2 a second per address and per key.
The other requests are not signed: `HELLO` (the profile and the nonce),
`LADDER` (a page of the ladder), and `STAY`, `LEAVE` and `UPLOAD`, which
carry the 16-byte token a signed `QUEUE` was given.

**Names.** 3 to 15 of the letters, digits, space, `.`, `_` and `-` (what
the port's typeface and every field of the lobby have), beginning with a
letter or a digit, no two spaces together and none at the end. A name is
refused if its **fold** is another key's: the fold lowercases it, takes
away the separators and maps the look-alikes (`0` o, `1` `i` `|` l, `5`
s, `rn` m, `vv` w, ...), so `Ayame`, `AYAME`, `Ay_ame`, `4yame` and
`Ayarne` are one name. A name is held while its player has been seen in
the last 180 days. A few names are kept (`ReBushido`, `admin`, `server`,
`moderator`, `SquareSoft`, ...). A rename frees the old name. A new
identity may be registered from one address at most 3 times a day.

**The queue and the pairing.** A signed `QUEUE` puts the player in the
queue with their rating, the setup code (section 14: only players of the
same build, disc and gameplay mods are paired) and their round trip to
the server; a `STAY` every second keeps them there (10 seconds unheard
and they are out). Twice a second the server pairs, longest waiting first:
with the player of the same setup code, never of the same address (an
IPv4 address or IPv6 /64), not already paired with them 3 times in the
last 24 hours, whose rating is within 150 points and whose round trip
added to theirs is within 250 ms, both widening as the first waits (10
points and 10 ms a second, to 800 and 600), taking the nearest by both.
The round trips added are the most the two can be apart through the
server, so a direct way is never worse. The one who waited longer hosts
the match (player 1).

**The match.** The server makes it a row of its store at once (so that a
report after a restart is still taken), draws its **stage**, one of the
18 places of the three stage pages (`--ranked-stages` narrows the
draw), and gives both players a `MATCHED`:
the match's number, the code and ticket to meet by, the role, the stage,
the rule and the other's name and rating. It is played as a room's match
is (section 16, `PORT_SESSION_RULE_ONE_MATCH`), with two differences:
the host's program holds the ladder's rule, **First to 3** (`--ranked-
first-to`), as its session's match rule whatever its own setting
(`port_HoldMatch`, and the player's own put back after), and the stage
pages, as they open, take the server's stage before anyone chooses
(`port_RankedStagePlace`, `select.c`; the same on both machines, outside
the state, so a rollback over those frames chooses it again). Every fight
of the match is on that stage. Who leaves loses: a player who leaves in
the middle (F1 or Escape twice, section 16) reports that they left, the
other that the other left, and both name the same winner.

**The digest.** As netplay makes each frame's two inputs final it gives
them to `ranked.c` (`port_RankedFrame`, the frame and both pads, as a
room's match's feed has them), and each final state hash
(`port_RankedHash`). At the match's end, the frame the session ended at
(`port_SessionLeftAt`, the same on both machines), the digest is
BLAKE2b-256 of the BLAKE2b of every input before that frame, the BLAKE2b
of every hash before it, and the frame: the same inputs and hashes a
replay keeps of each fight, of the whole session. Two honest programs of a
match played to its end make the same digest; a program that played
another game, or reports another, does not.

**Results.** Each program sends a signed `REPORT`: the match, why it
ended (played to its end, the other left, I left, a desync, never met),
the winner it saw, the rule its session held, the frames and the digest;
said again until the server has it, then a `STAY` naming the match until
it is decided. The server keeps the signed report as it came, and decides:

| The reports | Decided |
| --- | --- |
| both played to the end, the same winner, digest and frames | **rated** |
| one left, the other says the other left, the same winner | **rated**: the leaver loses |
| one left in the match's last frames, the other saw it decided | **rated** as the leaver's own report concedes |
| one report, the other player unheard for 20 s (left, crashed, lost the network) | **rated** as the one report says: the leaver loses |
| one report, the other still there and not reporting after 3 minutes | disputed |
| different winners, different digests, both saying the other left, a rule not the ladder's | **disputed** |
| both never met, or a desync on both | void |
| nobody reports within an hour | void |

A disputed or void match is not rated. For a disputed one the server asks
both players for the **replays** of its fights (section 15's replays,
which every online fight writes): their files in one upload (`RBRANKUP`,
each file's name and bytes), sent in chunks of 1 KB with the token, at
most 4 MB a side and 1 GB of all disputes, kept 60 days as
`disputes/match-N-a.rbu` and `-b.rbu`. The owner settles it with
`tools/relay/disputes.py` (`tools/relay/README.md`, "Disputes"): it lists
the disputes, plays both sides' replays back with the port's
`--replay-check` (on the VPS, which has no disc, by an export checked on
the owner's PC), and says for each fight whether the replay's own hashes
hold and who won it by the game's state as the playback leaves it (the
port prints it; never what the file says of itself), what each side's
replays show of the match, whether the fights both sides sent are alike,
and which report they bear out; then, on the owner's word, rates the match
as the replays show (or as the owner says) or voids it, the decision and
its reason written into the store (`settled`, the store's schema 2, kept
as long as the match).

**The rating: Glicko-2** (Glickman, "Example of the Glicko-2 system",
2013), over Elo because a new player's rating is uncertain and should
move fast and settle (its RD, the rating deviation, says how far), and
because a player back after months should move fast again. The
constants: a new player 1500, RD 350, volatility 0.06; the system
constant τ 0.5; RD kept between 30 and 350. Each match is a rating period
of its own for both players; the step that widens RD for time without
play counts that time in days (a fraction of one too), so that ten
matches in an evening do not widen it ten times. Each is rated against
the other's rating with the other's RD as widened to now. A draw (a
First to 3 has none, but the rule may change) scores a half. A rating
whose RD is above 110 is **provisional**, shown with `?` (about the first
ten matches). The ladder orders the rated players by rating. The test
holds the code to the paper's worked example (1500/200/0.06 against
1400/30 won, 1550/100 and 1700/300 lost: 1464.06, 151.52, 0.05999).

**The ladder page** (Online Play, Ranked, The ladder): the top eight and
on, eight around the player, the player's last eight matches, everyone's
last eight, each match with its date, who beat whom and the two ratings'
change; the player's own rating, place, RD and record on the Ranked page
above its rows. `python3 tools/relay/ladder.py --show STATE_DIR` prints
the same as JSON on the server (no web page: the server keeps UDP alone).

**The server's care for its data.** The ladder is the one thing the
lobby writes to disk, in the folder `--state` names (none: ranked play
is off, `DISABLED`); on the VPS the service's own
`/var/lib/private/rebushido-relay`, made by systemd for its
`DynamicUser` (`StateDirectory=`), readable by that user and root alone,
and kept across restarts and redeploys. What it holds:

| What | Kept |
| --- | --- |
| a player: the public key, the name and its fold, the rating, RD, volatility, the record, when made, last seen (to the hour), last rated | while seen in the last two years; the name held 180 days after the last time seen |
| a match: the two keys, when, the state, the winner and why, the rule and the stage, both signed reports as they came, the ratings before and after | two years |
| a disputed match's replays | 60 days, at most 4 MB a side and 1 GB in all |
| a dispute the owner settled: when, the decision, its reason, what the dispute was, what the replays said | as long as its match |
| the nonces' secret | for good (a new one only with a new store) |

No address of anyone's is written: the limits by address are in memory
alone. The file is SQLite (Python's standard library) in write-ahead
mode with full synchronisation; each change is one transaction, so a
crash or a power cut leaves the last change whole or not at all, and a
match being played when the server stops is reported after it starts
again. A replay upload is written beside and renamed. Once a day the
server copies the store with SQLite's online backup into
`backups/ladder-YYYY-MM-DD.sqlite` and keeps the last seven; the copies
are the store's, the disputed replays are not in them. A failure of the
ladder's (a full disk) is logged and never stops the lobby and relay.

**Limits.** Per address: 20 ranked requests a second, 2 signed a second
(and 2 a second per key), 3 new identities a day, an upload's chunks at
the spectators' rate; 2,000 in the queue; the same pair at most 3
matches in 24 hours. Every number is an option of the server
(`--help`).

**The wire** (lobby version 3; a client of version 1 or 2 is answered
`VERSION`, and its screen says "The lobby server is of another version").
Little-endian; text zero-padded ASCII.

| Kind | Who | Bytes | What |
| --- | --- | --- | --- |
| 63 `RANK` | client | 240 (an `UPLOAD` 176 and a chunk of 1,024) | an action (`HELLO`, `NAME`, `QUEUE`, `STAY`, `LEAVE`, `REPORT`, `UPLOAD`, `LADDER`); the key, the nonce, the name, the setup code, a stamp (echoed: the round trip), the round trip, why a match ended, the match, the token, the winner, the rule and its count, the frames, the digest, two arguments (a list's kind and first place, an upload's place and size); the signature |
| 54 `RANKED` | server | 168 and up to eight rows of 48 | a status (OK or an error: `VERSION`, `RATE_LIMITED`, `BAD_SIGNATURE`, `STALE`, `NAME_TAKEN`, `NAME_BAD`, `NO_MATCH`, `DISABLED`, `TOO_MANY`, `FULL`) and what it is (a profile, queued, matched, reported, a list, uploaded); the nonce, the player's name, rating, RD, place and record, the token, the queue, the match (number, role, stage, count, code, ticket, the other's name and rating), its result (state, winner, the ratings before and after), an upload's progress, a list's rows (a place: rank, name, rating, RD, record, provisional; a match: when, the two names, the winner, the state, the two changes) |

`tools/relay/relay_client.py` builds and reads both, and
`tools/relay/test_relay.py` holds the server to them.

**Session protocol.** Unchanged (12): a ranked match is a room's match
to the session; the rule comes in the host's welcome as ever, and the
stage from the server to both. A guest's report names the rule its
session held, so a host that played another is disputed.

**Proved.**

`tools/relay/test_relay.py` (Windows and Linux, about 45 seconds with
the rest): identities and signatures (a new key, its name; a NAME signed
by another key under Ayame's, one with a byte changed after signing, an
unsigned QUEUE: each `BAD_SIGNATURE` and logged; an old nonce `STALE`
with a fresh one; version 2 refused); names (too short or long, a
character, a space first or two, a separator first, kept names; seven
spellings of a name held refused, a name of its own taken, a rename freeing
the old); the queue (1500 and 1560 paired, 1900 not; 400 ms of round
trip not; the longer waiting hosts; each told the other's name and rating,
the rule and one stage; the match met by its ticket, a wrong ticket
refused); agreeing reports rated to Glicko-2's own numbers, the record
counted, a report said twice not counted twice, a stranger's refused;
different digests disputed, unrated, its replays uploaded in chunks,
kept whole and unpacked, an upload over 4 MB refused; the same pair not
paired a third time; a leaver who says so, one who goes quiet, a player
who stays and never reports, both saying the other left, a match never
met, a match of another rule; the ladder's top and recent pages; the
store across a stop and a start (a name, a rating, a match reported after
the start, a name still held); the backup (made, at most the kept number,
a whole SQLite file holding the players); the limits (new identities a
day, 60 requests at once, 12 signed at once, two of one address never
paired); a server with no store; `disputes.py` beside the running server
(two disputes checked, one settled rated as its replays show and its
players told, one settled void from an export, neither twice; a store of
schema 1 migrated by the server, its copy kept, and settled after); and
`ladder.py --self-test`: RFC 8032's cases and Glicko-2's worked example.

`tools/port_net_lobby.py`'s `ranked` case (Linux, in `make port-net`) and
`ranked-win` (Windows, in the gate's `port-net-windows`), headless
programs through a local server with a store, 60 ms each way and 5% lost:

| Match | What runs | Result |
| --- | --- | --- |
| 1 | Ayame and Bunta queue, are paired and play (First to 1 on this test server, a fight each match, and the stage drawn from the first place alone, the one the scripted fight is written for: on another its presses may never cut the other down) | the lobby's stage taken on both, the host's rule First to 1 held on both; both digests the same, every hash alike; rated: the winner 1500 to 1662, the loser 1500 to 1338; Bunta's program prints the ladder with both at those ratings |
| 2 | Chie and Dai; Dai's program reports the other winner (`--ranked-lie`) | disputed, unrated; both sides' replays uploaded and kept; one unpacked and played back by `--replay-check`: 570 frames, 30 hashes the same as recorded; then `disputes.py check` beside the running lobby: both sides' replays hold, their one fight alike on both, Chie the winner by the game's state, her report borne out and Dai's not; `resolve --winner replays`: rated, Chie 1500 to 1662, Dai 1500 to 1338 |
| 3 | Eiko and Fumi; Fumi leaves at frame 1,150, in the fight | Fumi reports she left, Eiko that the other did; rated, Fumi's loss |
| | a report of match 1 forged under Ayame's key | `BAD_SIGNATURE`, in the server's log; her rating unchanged |

About 30 seconds. `win-ranked` (Windows, the gate's `port-net-windows`):
a hidden window opens Online Play, Ranked, *Find a ranked match*; a second
window of the command line (`--ranked`) queues; the two are paired, the
first window's pages begin the match in the running program, it is played
and rated alike on both, and the first window's Ranked page shows the
result and its ladder page the two. Pictures of every page: the case's
`shots/`, and the owner's copies in `tmp/ladder/`.

**What is left.** Two players' PCs on two networks, and the server under
real players (the pairing's widening and limits are guesses until then).
A dispute whose two sides' replays show different winners, or none, is
still the owner's to look at (`disputes.py` says so and settles it as the
owner says). A player who
loses their identity file and its copies cannot get their rating back.
The ladder has no seasons and no decay of a rating but RD's.

## 18. Two on two online

ROADMAP 24 (docs/PORT.md, "Two on two"): four fighters in one fight.
What a session already is holds them: the four fighters, the copies of
the two models, the four pairs' globals and each fighter's ring of the
replay are a state file's (`src/port/duo.c`), saved, put back and run
again with the game, and the fight of four is as deterministic as the
fight of two (`port-rollback` holds it, both sizes, plain and quiet).

**What a session carries.** The host's `teams` rides in the welcome's
match word (`port_MatchBits`, over the rule and its number), held by the
guest as the rule is (`port_HoldMatch`; the log says `session: the
host's teams are two on two`). A session of two players' input is one pad
from each PC, so there both teams' partners are the CPU's, whatever the
host's choice (`port_DuoPartnerIsCpu`: `port_IsNetplayOn` and two pads);
a session of four players (below) has a player for every fighter.
Protocol 13 (setup codes `P13-...`); 15 with four players.

**Rollback at four.** A guess is never more than 8 frames ahead
(section 12), so the most a rollback runs again is 8 frames. On the
Windows program without a window (`tools/port_bench.py --rollback`,
port_game's plan, each frame run again quietly), RTX 3090's PC: one on
one 5.8 ms for the whole window (0.71 ms a
frame), two on two 7.9 ms (0.98 ms a frame); the
state put back 0.08 to 0.09 ms. A frame of four costs about 40% more
than a frame of two: the fight's two steps of fighters and two of pairs
over, the drawing the same. The worst rollback at four, 8.5 ms, is
within half of the game's frame of 16.7 ms.

**Proved** on this PC: `tools/port_net.py session` plays, after its
other sessions, a Stock match of two on two (`64-bit-duo`,
`32-bit-duo`): the host's `--match stock:2 --teams cpu`, the guest given
neither, over 100 ms each way and 5% lost; both fights to a team out,
the score's card, the rematch, every state hash the same on both
machines (142 hashes on each size, the same on both), and the fight's replays the same on both and
played back with every hash the same. `tools/port_net_lobby.py
headless`'s `rooms-duo`: a room of several whose rotation's first match
is a fight of four, Ayame and Bunta its players and Chie its spectator,
who runs all four from the fight's start: 63 hashes the same on both players, 17 of them Chie's as she watched. The session's
stage is the stage page's second (on the rooftop a fighter left lying by
the plan may lie on a roof below, out of the CPU partners' reach, and
the fight never ends).

**Four players on four PCs** (protocol 15, lobby version 4; the owner,
2026-11-25: "work whatever items you can without my intervention"). The
host is player 1 and the middle of a star: each guest has one link, to
the host, and the host one to each guest; the host passes each guest's
inputs on to the others with its own. Two on each of two PCs is the same
session with one link, each PC playing two pads (a team).

*Why the host in the middle, not every PC to every other.* Each guest
needs one way, to the host, and finds it as a guest of two does: the
host's link is its own socket, so each pair is exactly a session of
two's meeting at the lobby (IPv6, the host's mapped port, a punched
hole, the relay), section 14's ladder unchanged three times over. A mesh
needs six ways, the three between guests among them: behind a symmetric
NAT a guest that reaches the host may well not reach another guest, and
every pair of the six must come through, or relay, for the session to
begin. The price is delay between two guests, whose inputs go by way of
the host: guest to host and host to guest, at most about twice a direct
way's one way for players of one region, all of it within the window a
rollback hides (a guess is never more than 8 frames ahead). The host is
also where everything that must be one is decided: who is in, the go,
the end, the hashes held to each other (the host holds each guest's to
its own, a guest the host's: every program's to every other's by way of
the host), the feed to the spectators. Its upload is three guests'
datagrams of a few hundred bytes, 60 a second each.

*The input* (`netplay.c`): a frame is every pad of the session, each
with its own count of frames known, its own guesses (the last that came)
and its own wrong guess, any of which rolls the session back to its
frame; a hash is final once every pad's inputs before it are known. A
datagram (`KIND_INPUT`) says how many of each pad's inputs its sender
has (so each sender sends a link only what its program lacks), the
sender's last final hash and its time, and a block of inputs for each
pad that the receiving program does not play: to a guest, the host's and
the other guests', to the host a guest's own. Time sync is kept with
each link and the hold is the most any link asks for. The local pads
are the PC's: a PC of one player plays its pad with either of its pads,
a PC of two its first pad and its second. A rollback at four remote pads costs what one at four fighters does (PORT.md, "Two on two": `port_bench.py --rollback`'s `rollback-4p`, 14.0 ms for the whole window on a loaded machine against 13.8 with the CPU's partners in the same run).

*The meeting* (`session.c`): `--players 4` (or `2x2`) on the host. The
host meets its guests one after another, each on its own link: by
address at its port and the two after it (a guest that says hello to a
port whose link has its guest is sent on to the next that waits,
`KIND_MOVE`), or at the lobby a seat each of a room of four. Every
guest's protocol, build, disc and gameplay mods are held to the host's,
and each is sent the cards. While the host meets one, those already in
are kept in (`port_SessionKeepOthers`: their datagrams read, a leave or a
silence of 15 seconds letting that link wait for another, the lobby told
them every 100 ms). The welcome says the session's pads and PCs and the
guest's own pads (`MEET_SEATS`); the host's lobby (`KIND_LOBBY`) says
how many are here of how many and, once all are, the shape every
program cuts faces off at (the widest of the four pictures, section 13),
which each guest must have before it says it is ready. The host says go
to all once every guest is ready and its own player is. A session of
four players is two on two with every fighter a player's: pad 1 and 3
the first team, 2 and 4 the second (on two PCs, each PC a team); the
host's rule, its match and its stage page hold as in a session of two.

*A guest who gives up in the meeting* (2026-11-27). A guest the lobby
let into a seat, whose way to the host was found, and who then went (his
program closed or killed, his player giving up, before his first hello
reached the host) had the host wait for that hello for ever, its other
guests kept in and the lobby telling a newcomer that the room had all
its players; and as nothing more was said of the seat, the lobby closed
the whole room 30 s on as a meeting done (`--direct-timeout`). Now,
while it waits for a guest the lobby sent, the host says the seat's
hello again every second (`port_RelayStillHere`, no reset: the lobby
keeps the room), and if no hello of that guest has come in 45 s
(`MET_GIVE_UP_MS`) it lets the seat go: its log says `session: the
player the lobby sent to link N never said hello in 45 seconds; letting
the seat go and waiting for another`, its status *A player who came to
the room gave up before joining. Still waiting: ...*, and its next
hello for the seat is the reset one a leave in the meeting already sent
(the lobby forgets that guest and the seat is free for the next). This
is what a leave or a silence in the meeting already did (the seat waits
for another; nothing is decided for the CPU before the match has begun,
section 19); the guests already in are kept in meanwhile. 45 s, not
less: a guest whose way was found directly said so a moment before and
says hello at once, but one relayed may be a program held up opening its
window inside its meeting, which took up to 30 s on a loaded machine.
Nothing new is sent (a hello of the meeting's own kind) and nothing of
the session changes: protocol 21 and the lobby's version 4 hold, and a
host of before and a guest of now, or the other way, meet as before.
The same holds for a host of two at the lobby.

*Leaving.* A guest of four PCs who leaves a fight of four (the window's
close, F1 twice, the plan's end) or stops answering gives his fighter to
the CPU, and the others play on (protocol 16, section 19). Otherwise a
player who leaves ends the session for all: the host's player, a guest
outside the fight itself (before it, between fights, after it), the last
guest but one, either PC of two on each of
two. The host, told by that guest's goodbye (which says the pad), runs
to the last frame it has of every pad and ends, telling each other guest
who left; each runs to its own last and ends. In a room's match the
leaver's team loses it.

*The lobby* (`tools/relay/`, version 4; `README.md` beside it): a
**room of four** is a listed room with three seats. The host's first link
opens the room as a room of two is opened, with a seat key; its other two
links each say hello for their seat with the room's code and the key, and
each seat is a two-player room of that link and one guest, punched,
decided and relayed on its own. A guest asking for the room's code is
given the first seat that is open and has nobody, or a seat it asks for
(a room's match: its place in the match), or is told to wait while its
seat is not open yet; the list counts the host and every seat's guest,
and marks the room as of four. The room is kept while the host of any
of its seats still says hello (the host meets them in turn). A **room of
several** may make matches of four (`--room-size 4`, *Room for: several,
matches of four*): the rotation takes the first four free, in order,
their teams the first and third against the second and fourth; a fixed
turn sends the match's first to the end, the winner staying keeps both
winners first; a ticket goes to all four, and each guest meets the
match's host at the seat of its place. Matches beside the room stay of
two. **The spectators' feed** carries four pads a frame (8 bytes, 120
frames and 4 hashes a datagram), and the settings say the session's
pads. A client of version 3 is answered `VERSION` (before its size is
looked at), and says "The lobby server is of another version".

*The window*: the online pages' *Host a room*, *Room for*: two players;
four players, a PC each; four players, two on each of two PCs; several;
several, matches of four. *Quick Match* joins a room of the kind the page
says (two, or four on four PCs) or opens one. *Browse rooms* marks a
room of four `[four]`, `2/4`. The line says `Online: player 3 of 4`, or
`player 1 and 3 of 4` on a PC of two players, with the worst link's
ping. The settings menu's Play online page hosts two only.

**Proved** on this PC, every program headless unless said:

| Case | Programs | What runs | Result |
| --- | --- | --- | --- |
| `port_net.py four`: `four` | four headless, each size | `--host 127.0.0.1:P --players 4`, three `--join` at P (the second and third sent on to P+1 and P+2); 100 ms each way and 5% lost; versus select, the fight of four to a team out, the menu after it | 64-bit: 5,502 frames, 183 hashes the same on all four, the last `13BE7CD212C83E90` at frame 5,490; 1,035 rollbacks on the host (three links' pads to be wrong), 733, 705 and 745 on the guests, the longest 8; the fight's replay the same on all four machines (all four pads) and played back by each with every hash the same. 32-bit: 183 the same on all four, the last `FA0367574C81715F`; 1,001, 720, 668 and 736 rollbacks |
| `couch` | two headless, each size | `--players 2x2`: each PC two pads, a team | the same 183 hashes as `four`, to the last (the same presses at the same frames: the state does not depend on which PC plays which pad); 650 and 664 rollbacks (32-bit 576 and 635) |
| `four-leave` (protocol 15; since 16 the CPU plays on: section 19) | four headless | as `four`, the fourth player's program ending 400 frames into the fight | each of the others told `the other program ended after frame 1,799 (player 4)`, all four ending well; 59 hashes the same on all |
| `port_net_lobby.py`: `four-punch` | four headless | a locked room of four at a local lobby, three guests by its code and its password, each at its own seat; 60 ms each way and 5% lost | every guest's way a punched hole to the host (the host's three); 183 hashes the same on all four, the last `13BE7CD212C83E90` (that of the session by address: the network decides when an input is known, never what it is); 1,030, 719, 681 and 741 rollbacks; 100 s |
| `four-relay` | four headless | the same, `--net-paths relay`: each seat's pair passed on by the lobby | each the relay; the same 183 hashes, the same last; 109 s |
| `four-gives-up`, `four-gives-up-win` (2026-11-27) | five headless | `four-punch`'s room; Bunta and Chie in, then Dai, killed the moment his program says the host is in the room (no hello to the host ever, no goodbye); Eiji asks for the room's code once the host has let the seat go | before the fix the host waited for ever (`3 of 4 players here`), Eiji was told the room had all its players, and the lobby closed the room 30 s on. Now: 45 s after the kill the host let the seat go, met Eiji there (link 3), and the session of four played with him: 143 hashes the same on all four, the last `A6A8B211828A9212` at frame 4,290; 611, 432, 397 and 437 rollbacks; 112 s (142 s in a loaded gate; Windows' headless programs 115 s) |
| `rooms-four` (protocol 15; match 2 since 16: section 19) | five headless | a room of several, matches of four (`--room-size 4`, a fixed turn): Ayame, Bunta, Chie and Dai play, Sora watches; Dai leaves the second match at its frame 1,900, in the fight | match 1, Ayame and Chie against Bunta and Dai, each at the place the rotation gave: 182 hashes the same on all four, Sora holding 134 of them as she watched (from the fight's start), the second team's win told to the room. Match 2, the order turned (Bunta first, Bunta and Dai against Chie and Ayame): Dai's leave ended it on all, the others told `player 3` left, his team's loss the room's result, 63 hashes the same on all four, Sora 17; all back to their own games and the room; 169 s |
| `pages-four` | four windows (hidden) | the host window's online pages: Host a room, *Room for: four players, a PC each*, Open; three windows of the command line join by its code; the host's Ready; at versus select the guests' Triangle (player 2's takes all back to the top menu) | the host met all three in turn and said go once all were ready; the session from power on to the select screen, where it ended on all (the host back at its own game, never started again); 17 hashes the same on all four; the Linux window, 19 s |
| `four-punch-win` | four, Windows' headless program | `four-punch` on Windows | 183 hashes the same on all four, the last `6D260E838DC11965` (the Windows build's own, section 7); 1,049, 767, 710 and 735 rollbacks; 91 s |
| `four-relay-win` | four, Windows' headless | `four-relay` on Windows | each the relay; 183 hashes the same on all four, the last `6D260E838DC11965` (as `four-punch-win`); 1,049, 726, 747 and 687 rollbacks |
| `rooms-four-win` (protocol 15) | five, Windows' headless | `rooms-four` on Windows | match 1: 182 hashes the same on all four, Sora holding 136; match 2: Dai's leave, his team's loss, 63 hashes the same on all four, Sora 17; 127 s |
| `win-pages-four` | four Windows windows (hidden) | `pages-four` on Windows | the same: all met, go, the session's end at the select screen on all; 17 hashes the same on all four; 18 s |
| `tools/relay/test_relay.py` | the server | rooms of four, their seats, a room of several's matches of four, the feed of four pads, version 3 refused | every case passes (OK, about 70 s), with the rest of the server's |
| `tests/port/netplay.c` | netplay alone | the host of four: each pad's inputs taken from its own link only, each link sent every pad but its own (the other guests' passed on) from what it has, a wrong guess of one guest's pad rolled back, a hash final only once all four pads have come, a guest's goodbye with its pad | 85 checks, both sizes |

The Windows program without a C library read at most 32 words of its
command line (`host_windows.c`), which a host of four's line with a
lobby's and a test's words passed; it reads 128 now. `port_order` and
`port_casts` (`make port`) had kept a dump of a source edited since it was
made, as nothing they read named the source itself; they look at it now
(`port_build.made_since`).

**What is left.** Four real PCs on four networks, and players: the
delay between two guests through the host, and the host's upload. Teams of different characters (video memory for
four). The settings menu's Play online page hosts two only; the online
pages have the rest. The window asks its router to map the host's first
port alone: by address, the second and third guests' ports must be open
(at the lobby every seat finds its own way). The owner's eye on the pages'
new rows.

## 19. The CPU for a player who leaves

ROADMAP 24's gap (2026-11-25): in a fight of four on four PCs, a guest
who leaves, or stops answering, no longer ends the session for all. His
fighter is the CPU's from one frame, the same on every remaining machine,
and the fight and its match go on for the other three; the session ends
for all at the match's end. Protocol 16 (setup codes `P17-...` since the arenas' protocol 17); the
lobby is unchanged (version 4).

**Decided by the host, carried by the input.** The host is the middle of
the star (section 18) and the only program that hears every guest, so it
decides. When a guest's goodbye comes (`KIND_BYE`, which says his pad),
or nothing has come from him for 15 seconds, the host gives his pad to
the CPU (`port_NetplayTakeOver`): from the first frame of that pad's
inputs it does not have, the pad's input is `INPUT_CPU` (0xFFFF, every
button at once, which no pad holds: a local pad that did is let go of
Select), and the host plays it on with its own pad, frame after frame
(`SOURCE_CPU`). Those inputs go to the other guests as any pad's do,
passed on by the host, so nothing new is sent and nothing can be lost
that the resends do not bring again. The frame is a pad's input like any
other: a program that ran it on a guess (the leaver's last buttons) finds
the guess wrong and runs it again (`port_NetplayTakePad`: a guess and an
input that differ in whose the pad is roll back, whatever the frame's
input depended on); the host runs its own frames from it again the same
way. A guess is never more than 8 frames ahead of what is known, and the
frame is no earlier than the last known, so the rollback is one of the
window's. The rest of the leaver's goodbyes (five are sent) are let be
once his pad is the CPU's, and no frame is run past the window whatever
cut a wait short (the first gate's run found a leave after the fight
whose later goodbyes kept cutting the host's waits short, and its frames
ran past the window). In practice the frame is the leaver's own last: his goodbye
follows his last inputs, and the host had all of them (`four-leave`: he
left after frame 1,799, the CPU's from 1,799; `rooms-four`: at 1,900 with
a delay of 2, the CPU's from 1,903).

**What the game does with it.** Netplay hands the game a pad whose input
is `INPUT_CPU` as answering and holding nothing, its `present`
`PORT_PAD_CPU` (`port_NetplayPutPad`): no unplugged pad's pause, no
press. At the step of a fighter whose pad says so, two on two makes him
the CPU's (`port_DuoTakeOver`, `duo.c`): `is_cpu`, the match's CPU
level, `cpu_Init` against the one he faces, as a CPU partner is set up.
Which CPU (docs/CPU.md, section 2): the host's `cpu` setting, which the
welcome's gameplay switches carry and every machine holds, so the port's
CPU at the host's level, with a memory of its own for that fighter, or
the game's own where the host's is Original.
`is_cpu` and the CPU's own fields are the fighter's, in the state, so a
rollback puts them back with the rest and the step that comes to the
frame again makes him the CPU's again; a later fight of the match does
the same at its first step. The tags over the heads say `CPU` over him.
With a pad the CPU's, the match's end (the game's menu after its last
fight) is the session's, as a room's match's is (`port_SessionInput`,
`port_SessionHasCpu`): on every machine at the same frame, the result in
the state, as section 16 has it; a headless program ends there. The rest
of the session would need the leaver at the select screen.

**Between the fights of a match** (2026-11-25). A match of several fights
(Stock N, First to N) has its fall, replay and winner's view after each
fight, then the card between two fights, then the next one's load. None
of it waits on one player: the card goes on by itself, or on anyone's
Cross (`port_MatchAfterFight`), and a pad the CPU's holds nothing. So a
guest who leaves there, or goes silent there, is given to the CPU as in
a fight (`port_NetplayInFightsOfFour`: the screens of a fight and of what
follows it, in a match of two on two of a versus mode, while the
session's end is not yet set), and his fighter is the CPU's from the next
fight's first step, the same frame on every machine, as any input is.
The host says so (`netplay: player 4 left between the fights of the
match: his fighter is the CPU's from the next fight's first step`); the
guest's F1 says the CPU plays his fighter on there too. Before a match's
first fight (the select screen, the partners' grid, the stage page) and
on the menus after its last, the session still ends for all: those pages
need their players. A leave after the last fight of a match that is
decided but whose session has not ended (a session of four by address,
the game's menu after its last fight) gives the pad to the CPU too, and
the session ends at once there with the match's result in the state,
where before it ended at the leave. Nothing new is sent and the game's
state is what it was, so the session protocol is unchanged (18): a guest
of before follows a host of now, which alone decides.

**Spectators and replays.** The feed carries every pad's input as
netplay keeps it (section 16), so a spectator is sent `INPUT_CPU` too and
makes the fighter the CPU's at the frame its players did
(`port_RunWatch`). A replay keeps which pads answered in its frames'
`present` byte; the CPU's pads are its high four bits now
(`port_ReplayPresent`), so a replay of such a fight plays back the same,
every hash held (`--replay-check`); one of before has none of them set.
Since 2026-11-26 an online fight's replay carries its session's run from
power on, the CPU's pads among its inputs, and plays on a build of
another core from there (PORT.md, "Across builds: the lead-in", *An
online fight's*).

**When the session still ends for all.** The host's own player leaving
(nobody else hears everyone), a leave outside a match's fights (the
host's game before the first, on the select screen, the partners' grid
or the stage page, or on the menus after the last: they need their
players), the last
guest of all leaving (nobody would be left to play with), a goodbye that
says a desync, two on each of two PCs (either leaving leaves one PC: the
window plays on alone as before), and a session of two with the CPU's
partners (likewise). A guest of four still waits for another guest's
inputs by way of the host; it gives up on the session only when the host
itself has been silent for 15 seconds, so that the host's 15 seconds for
the silent one come first.

**The room's result.** In a room's match of four the result is the
match's as it was played out: the leaver's partner and the CPU may still
win it. The leaver's own program tells the room nothing (`room: match 2
left; its end is the other players' to tell`, `port_OnlineMatchEnds`);
the three who stayed tell it at the match's end, and the lobby takes the
first. Why: the match goes on, and a partner who stayed and won should
not lose for a leaver; a leaver who wanted his team to lose gains
nothing, and one who leaves while losing still loses with his team. The
lobby counts a win for both members of the winning team, the leaver
among them if they win (it counts by teams; to leave him out would need
the lobby to be told who left: version 5). Where the session ends for
all (above), the leaver's team loses as before. The ranked ladder is one
on one, which this never reaches.

**What a remaining player sees.** The tag over the leaver's fighter turns
to `CPU`, and the line at the top says *Player 4 has left: the CPU plays
his fighter* for eight seconds (`sdl_net.c`); the guest's first F1 says
*Press F1 or Escape again to leave the fight: the CPU plays your fighter
on*.

**Proved** on this PC, headless:

| Case | Programs | What runs | Result |
| --- | --- | --- | --- |
| `port_net.py four`: `four-leave` | four headless, each size | as `four` (section 18), the fourth player's program ending 400 frames into the fight; 100 ms each way and 5% lost | 64-bit: each of the three told `player 4 has left the session: the CPU plays his fighter from frame 1799 on`; the fight played to a team out and the match decided at frame 5,474, where the session ended on all three; 182 hashes the same on the three, 123 of them after the CPU took over, the last `9A3285E775C3B4BE` at frame 5,460 (the same in three runs); the fight's replay the same on the three machines and played back by each with every hash the same; 783 rollbacks on the host, 462 and 442 on the guests. 32-bit: the same frames, 182 hashes the same on the three, the last `FD80F749253F7CE2`, its replays the same and played back |
| `four-host-leave` | four headless, each size | the host's player leaving 400 frames into the fight | as before protocol 16: each guest told `the other program ended after frame 1,789` (or 1,791) `(player 1)`, all ending well; 59 hashes the same on all four, each size |
| `port_net_lobby.py`: `rooms-four`, `rooms-four-win` | five headless, Linux and Windows | section 18's: Dai leaves the second match at its frame 1,900, in the fight | match 2: the CPU played Dai's fighter from frame 1,903 on Bunta, Chie, Ayame and Sora alike; the match to its end at frame 5,465, the second team's win (Chie and Ayame) told to the room by those who stayed, Dai's program telling nothing; 186 hashes the same on all four (Dai's before the CPU's frame), Sora holding 140; 165 s, on Windows the same, 174 s |
| `four-punch`, `four-relay` | four headless | section 18's, again with protocol 16 | 183 hashes the same on all four, as before |
| `tests/port/netplay.c` | netplay alone | the host given a guest's goodbye: with no fight of four on, the session's end as before; in one, the pad the CPU's from the first frame the host lacked, the frames past it run again, `INPUT_CPU` passed on; a guest told so from a frame it ran on a guess, rolled back where the frame's input depended on no button; the rest of a taken guest's goodbyes changing nothing; on the menus of a match of four no take-over, between its fights (the card, the next fight's load) one | 116 checks, both sizes |
| `port_net_lobby.py`: `four-silent`, `four-silent-win` (2026-11-25) | four headless at a local lobby, Linux and Windows | `four-punch`'s room; Dai's program killed (SIGKILL, TerminateProcess: no goodbye) four seconds into the fight | the host heard nothing from him for 15 seconds and the CPU played his fighter on, from one frame on all three who stayed (Linux: from frame 2,188, his last hash at 2,160; Windows: 2,200, his last at 2,190); the fight to a team out and the match's end on all three, 95 hashes the same on them (23 after the CPU's frame), Dai's 72 before it the same too; the fight's replay the same on the three and played back with every hash the same; 85 s, on Windows 64 s. Nothing to fix: the silence path held as written |
| `port_net_lobby.py`: `four-between`, `four-between-win` (2026-11-25) | four headless at a local lobby, Linux and Windows | the same room, the host's `--match stock:2`; the first fight's rounds, then Dai leaves (a goodbye) 30 frames into the card between the fights, the others pressing on in the second | the host gave player 4's pad to the CPU from frame 4,212, in the card (`left between the fights of the match`); the card went on, the second fight began with all four up and his fighter the CPU's at its first step, the match decided at frame 5,009 and the session ended there on the three; 167 hashes the same on them (27 after the CPU's frame), Dai's 140 the same too; both fights' replays the same on the three and played back; 131 s, on Windows 119 s (the same hashes but the build's own: section 7) |

| `port_net_lobby.py`: `rooms-four-stock`, `rooms-four-stock-win` (2026-11-26) | five headless at a local lobby, Linux and Windows | `rooms-four`'s room, every member's `--match stock:2` (a match's rule is its host's); each match two fights or more, the card between them; Dai leaves match 2 at its frame 4,440 (`--leave-at 2:4440`), in the card between its first fight and its second (whose start is frame 4,472) | match 1, Stock 2 of four players: two fights, 189 hashes the same on all four, Sora holding 120. Match 2: the host gave player 3's pad to the CPU from frame 4,443 (`left between the fights of the match`), the card went on, the second fight began with his fighter the CPU's at its first step; the match took a third fight and was decided at frame 5,910, the second team's win, told to the room by those who stayed; 197 hashes the same on all four (Dai's before the CPU's frame), Sora holding 128; the five fights' replays the same on the three who played both matches out, each played back, and from its session's power on by both sizes (PORT.md, *An online fight's*); 471 s, on Windows the same, 266 s |

The case was first written with the leave in the middle of match 2's
first fight, the CPU playing on through the card into the next: it never
ended, and nothing was wrong with the session. The plan's presses had run
out, and the two left standing, an idle player and the CPU's fighter with
both legs cut, 440 apart, stood in their stances for 48,000 frames (the
CPU's choice, which a player one on one would end by coming in). A leave
in the card instead leaves the next fight to three who
press and a CPU who is whole, as `four-between` does.

**What is left.** Four real PCs.

## 20. Teams of four characters

ROADMAP 24's last piece of the game's (docs/PORT.md, "Two on two":
"Each fighter his own character and weapon"): each team's second is a
character and weapon of his own, picked at the select screen after his
first. Nothing of it is carried beside the session's inputs, because all
of it is the game's state and its inputs:

- **The picks** are the select screen's, which every machine runs from
  the same pads: the grid comes up a second time for the partners
  (`PortMatch.crew`, its second place; `PortMatch.partner` as the match
  begins), picked by the first two pads in a session of two players (the
  partners the CPU's) and by the third and fourth in a session of four
  players (`port_MatchPartnerPads`, the third and fourth pads of
  `g_port_input`, which netplay fills). Versus the CPU the CPU's partner is
  drawn by the crew's dice, in the state. A room's rule is chosen before
  its grid, so its partners are picked before its stage page, a
  session's after it.
- **What is loaded for them** is loaded by the game's own step on every
  machine alike (`fight_Init`): a partner's model and what it keeps into
  the console's RAM below the common sound bank (`PORT_DUO_AREA_ADDR`),
  saved and hashed with the rest of the RAM; his textures into the second
  bank of the port's video memory, which is the head of the same room
  (port_gpu.h), so saved and hashed with it; his motion file and win pose
  into the same room. Each fighter's motion file, its
  decoder and his win pose are put into his slot with him
  (`PortDuoMotion`, a state file's). A rollback puts all of it back as it
  puts back the fight.

**Protocol 18** (`SESSION_PROTOCOL`, setup codes `P18-...`): a program
before it plays a fight of four with each team two of one character, and
its select screen has no second grid, so the two would part at the first
partner's pick; 17 and 18 do not meet. The lobby is unchanged (version 4).

**Protocol 20** (setup codes `P20-...`; 19 is another change's): a
partner of his own sounds as himself (docs/PORT.md, "Two on two", "The
sounds"). His banks of sounds are loaded by `fight_Init` on every machine
alike, their heads into his room of the RAM and their samples into the
port's second bank of sound memory, a part of the chip's state (spu.c,
a state file), so saved, put back and hashed with it (the weapon file his
sounds may need is read at once from each machine's image, taking no
blank). The state is not 18's, so 18 and 20 do not meet.

**How it is proved.**

| Case | Programs | What runs | Result |
| --- | --- | --- | --- |
| `port_net.py session`: `duo` | two headless, each size | two players, the CPU's partners: after the stage page the grid again, the first player's partner one place down, the second's his own character again | 131 hashes the same on both (64-bit, the last `898E901344900095` at frame 3,930; 32-bit `C3AAB0DB4E6670FD`), the fight's replay the same on both and played back |
| `port_net.py four`: `four`, `couch`, `four-leave`, `four-host-leave` | four (or two) headless, each size | the third and fourth pads pick the partners, each one place down: four characters at the fight's start (`port_duo_character_1` to `_4`, held by the test) | 143 hashes the same on all (64-bit, the last `8F736001F9937252` at frame 4,290; 32-bit `94C5E39E7CC96A86`); `four-leave` 147 the same on the three who stayed, the CPU taking player 4's fighter at frame 2,357; the replays the same on all and played back |
| `port_net_lobby.py`: `rooms-duo`, `four-punch`, `four-relay`, `rooms-four` | headless, a local lobby | a room's rule before its grid: the partners picked before the stage page; `rooms-four`'s leave moved to frame 3,600 (the fight starts near 3,000 now) | all ok; `rooms-four`: match 1 147 hashes the same on all four, Sora 78; match 2 199, Sora 130, the CPU playing Dai's fighter from frame 3,603 |

## 21. A resync

ROADMAP 28 (the owner, 2026-11-26, instead of a desync report): where an
online session finds the machines' hashes parted, the session no longer
ends. The host's state is put on every machine, at a press of any
player's or by itself a moment after, and the fight goes on from there.
The code: `src/port/netplay.c` ("a resync"), `src/port/replay.c` ("a
resync"), `watch.c` (a spectator's), `sdl_net.c` (the line) and F5 in
`sdl_window.c`.

**What happened before.** Every 30 frames each program's final hash goes
to the others (section 12). One that was not this program's own ended
the session: the headless program quit with a failure, a session in the
running program ended with `PORT_NETPLAY_END_DESYNC` (the pages said
*The two games parted (a desync)*), a spectator stopped watching. Since
the host holds every guest's hashes to its own and each guest the
host's, the host finds every parting a guest does.

**Parted.** Now a hash that is not this program's own still prints
`netplay: DESYNC at frame F` (every test of a session still fails on
it), and the program is *parted*: it holds no more hashes, and the
session goes on. The window's line says *The game fell out of step with
the other player's | F5 or hold Select: resync now | by itself in 3 s*
(or *resync asked*, once asked). F5 is the window's own key; a
controller's is Select held a second (RESYNC_HOLD_FRAMES, 60 of the
local pad's frames, in `port_NetplayKeepLocal`). Both are heard only
while parted. Parted, Select is the resync's and not the game's: every
local pad's inputs are kept and sent without it (a Select pressed in a
fight is the game's, a kaishaku among others), so that holding it cannot
do anything in the fight, and it cannot be hit by accident where the
machines have not parted. It is the pad's Select, so the remapping
(`p1.select = ...`, PLAYING.md) moves it with the rest: Back on a
controller, Right Shift on the keyboard, or whatever the player made it.
Training's Select is training's own and never online.

**When.** The host resyncs at the first of: a press of its own player
(F5 or Select held), a press of any guest's (each input datagram says
so, `INPUT_ASK`), or 180 frames, three seconds, after it found the
parting (`--resync-auto N`; 0 for only at a press). Recommended and the
default both by itself and at a press: a parted fight is no fight, each
player seeing his own (a cut on one screen is a miss on the other); a
second's hold of Select is long in the middle of a fight; and three
seconds is long enough to read the line and short enough that
little is played on a game that will be thrown away. A resync again
within 1,200 frames (20 s) of the last is not made by itself, only at a
press, so that a fault that parts the machines again at once does not
stall the fight every three seconds; the player sees the line and
chooses.

**The frame.** The host does not resync at the frame it is at: a guest
can be further on, having run up to 8 frames past the host's last input
it has on a guess. It says a frame `RESYNC_LEAD` (20) ahead of its own,
in every input datagram from then (`INPUT_RESYNC_AT`), past every frame a
guest can have run on the inputs the host sent before it said so (the
most delay, 8, the most frames run on a guess, 8, and 4 more). Every
machine runs to that frame's boundary and no further: each has its own
state and its own plan there (a play test's plan is the run's own, kept
across the load: config/port_state.txt's `run`). The host waits there
for every input before the frame and puts right a wrong guess among
them, so that its state there is final, and makes final its hashes
before it.

**The state.** `port_ReplayResyncPack` packs the host's state as a
room's spectator is given a fight's start (section 16): this run's own
cleared, the runs the disc holds pointed at (the sectors the game read
last), the rest deflated, in a replay's chunks (`SERV` with the input's
number, the frame and the state's hash, `PIEC`, `STRT`, `DISC`, `END`).
A fight's file or a spectators' start being written is finished first
(the compressor is one stream at a time), and the runs found for the
fight's start are kept aside meanwhile. In a fight of two the 7.6 MB of
the state are 0.69 MB (1.05 MB of them the disc's, in about 520 runs);
in a fight of four, 1.09 MB. Packing it takes the host about 0.6 s (a
loaded machine), which the players see as part of the resync's pause.

**Over the link** (`KIND_RESYNC`, `KIND_RESYNC_ACK`). The host sends it
to each guest in chunks of 1 KB, the header's number the chunk's, each
with the resync's number, its frame, the last frame all held the same
before it, the state's bytes and the host's build
(`port_StateBuildSum`). A guest acknowledges every 8 chunks or 20 ms: the
first chunk it lacks and a bit for each of the 256 after it. The host
keeps at most 64 chunks unacknowledged on a link, sends one again after
twice the round trip and 40 ms, and no faster than 3,000 chunks a second
on a direct way or 150 through the lobby's relay, which passes 240
datagrams a second of a pair on (`port_RelayIsRelayed`); every 100 ms
meanwhile each side sends its inputs again, to say it is there. A chunk
of a resync a guest has had already is acknowledged again (the host may
not have heard the last). Nothing heard of it for 15 seconds, or a
state of another build, ends the session on the desync as before
(`netplay: DESYNC: no resync could be made`).

**Put in.** A guest, the state whole, waits for every input before the
frame and puts right a wrong guess among them (the fight's replay keeps
the frames before it as they were, not as they were guessed), then puts
the state in (`port_ReplayResyncLoad`: this run's own and the hooks
kept, the host's hash held), and goes on from the frame. Each side's
hashes are then of the resync's number (`INPUT_EPOCH`): one of the
state before it, or one not had yet, is not held to another's, and no
hash before the frame is. The log says `netplay: resync N: every machine
has the host's state of frame R; its hashes are held again from there
(the last the same before it, frame A)` on every machine alike.

**A replay of a resynced fight** (`RSYN`, replay.c). Each machine's
recording of the fight keeps the resync: the fight's frame it was put in
at, the frame of the last hash all held the same before it, and the
state put in, the same bytes on every machine. A playback puts it in at
that frame, as the session did, holds it to the host's hash, and does
not hold the hashes after the last the same and before the resync: the
frames where the machines had parted, which no playback can be (a
machine that parted ran a state its inputs alone do not make). Every
other hash is held, and the playback reaches the fight's end. A
resynced fight has no lead-in from power on (the session's run from
power on is not whole from the resync), so it plays on a build of the
one that recorded it only; `resyncs N` in its META.

**Spectators.** A spectator holds its own hashes to the host's, as the
feed gives them. Where the host's state parted from what its inputs make
(the host's own fault), the spectator parts too: it says so (*Watching:
the game fell out of step with the players'; waiting for their
resync*), and asks the lobby for a start later than the hash that
parted (`port_WatchAskStart`). The host gives the lobby the state of
every resync it makes as the match's newest start (the feed's `PART_START`
with the resync's frame), as it gives each fight's: the lobby, unchanged,
replaces its copy and sends it to the spectator who asks, who puts it in
at its frame and watches on, every hash held again. A spectator who had
not parted (a guest's fault) needs nothing. One whose state does not
come for 30 seconds, or by 5 seconds after the match's end, stops on the
desync as before.

**A fight of four's, told as a difference.** The lobby keeps a start of
1 MB at most (1,024 chunks of 1 KB; `PORT_REPLAY_SERVED_MOST`), and a
fight of four's resync is more (1.06 to 1.09 MB measured). Where the
whole is more, the host gives the lobby instead the difference from its
state of the frame every machine last held the same (the resync's
*agreed*), which a spectator who parted has too: its hash agreed with
the host's there, and it stopped running at the parting. Each program
keeps for it the states of its last four hashed frames
(`port_ReplayKeepBase`, replay.c): the host as each hash is made final
(a copy of netplay's own save of that frame, while it feeds a room's
spectators), a spectator as it runs each; the one of the agreed frame
is held from the parting to the resync (`port_ReplayHoldBase`), the
other three go round. The difference (`port_ReplayDiffMake`) is made of
the host's state at the resync and the kept one, both with this run's
own cleared, word by word: runs of so many words the same and so many
new, with the new words' bytes (a run of the same shorter than eight
words is among the new), deflated. It goes in a replay's chunks as the
whole does: `SERV` (the input, the frame and the state's hash), `PIEC`,
and `BASE` in place of `STRT` and `DISC` (the base's frame and hash, the
difference's size, the state's size, then the deflated difference). A
spectator puts it in (`port_ReplayUnpackDiff`) over its own kept state
of that frame and hash, its own cleared as the host's was, and holds the
result to the host's hash as it holds a whole one; the pieces must be
this build's, as a resync's are. In the case below the whole was
1.06 MB and the difference 320 KB (900 KB before deflating), 248 frames
after the agreed frame. The guests are sent the whole state, as before
(the link has no such limit). A spectator who comes into the match after
the resync, and so has not the base, is not given a way in by it
(`port_ReplayServedNeedsBase`): it waits for the next fight's start, as
one who comes between two fights does. A spectator who keeps replays
packs the state put in whole again for its fight's `RSYN` (a playback
has no base of its own). The lobby is unchanged: what it keeps and
sends is a start of a frame, of 1 MB at most, as before.

**Ranked.** A ranked match's digest (section 17) is made of each final
hash; on a resync both machines leave out those after the last the same
(`port_RankedResynced`), so that the two digests agree.

**Protocol 21** (`SESSION_PROTOCOL`, setup codes `P21-...`): netplay's
input datagram carries the resyncs its sender has had, a resync asked
for and the frame of the host's next, and the two kinds are new; 20 and
21 do not meet. The lobby, its relay and its feed are unchanged (version
4), and no build of it before is told apart. A controller's press (Select
held) and a fight of four's state for its spectators (`BASE`) changed
neither: Select held is a resync asked, as F5's, in the same word; a
start told as a difference is a start of the lobby's like any other,
and only spectators of the same build put either in.

**For tests.** `--net-part-at F` makes this program's state another's at
its frame F: a word of the RAM the game never reads (below the stack's
top the port gives the game and does not use) has a number added, after
the frame's state is saved, so that a rollback past it makes it again;
the hash parts there and nothing of the game does. `--resync-ask-at F`
asks for a resync at frame F, once parted, as F5 does; a play test's
plan holds Select (`SELECT hold 75f`) as a controller's player does.

**How it is proved.** At 60 ms each way and 5% lost (the lobby's cases
as the lobby's are):

| Case | Programs | What runs | Result |
| --- | --- | --- | --- |
| `tests/port/netplay.c` | none (the network made in the file) | the host parted at frame 30, a resync by itself said 20 frames on and sent in three chunks, one acknowledgement in two lost; a guest stopping at the host's frame, the chunks in another order and one twice, put in; another build's refused; the host parted, its player holding Select and Cross | the resync's frame, number and bytes; no hash of the state before it or of before its frame held, one after it held; the guest's acknowledgements, again for a chunk sent again; the session ending on the desync for another build's; no resync asked for 59 of the pad's frames of Select held, Cross alone in the inputs sent, the 60th asking |
| `port_net.py resync`: `resync-guest` | two headless, each size, a session's Stock match | the guest parted at frame 1,450, in the fight; the host resyncs by itself | the desync found at 1,470; the resync at frame 1,687 (64-bit), 690 KB had in 2.5 s; 75 hashes the same on both but frames 1,470 to 1,686, 19 of them after; the fight's replay the same on both (inputs, views, the resync's state) and played back through the resync, every hash held |
| `resync-host` | the same | the host parted at 1,450; neither resyncs by itself; the guest's player holds Select (the plan's `SELECT hold 75f`, from near 1,637) | the hold asks at 1,697 (64-bit), the resync at 1,728, the guest given the host's parted state; 18 hashes the same after it; both replays played back |
| `resync-four` | four headless, each size | four players on four PCs, the third parted at 2,400 | the resync at 2,642 (64-bit), 1.09 MB had by the three guests in 3.6 s; 55 hashes the same on all four after it; the fight's replay the same on all four and played back |
| `port_net_lobby.py`: `relay-resync` (`-win` on Windows) | two headless, the lobby's relay | the guest parted at 1,450 | 693 KB at the relay's pace, had in 5.0 s; 26 hashes the same after |
| `rooms-resync` (`-win`) | three headless, a room of several | Ayame hosts the match and parts at 1,550; Bunta plays; Chie watches from the fight's start | Bunta and Chie part from Ayame; the resync at 1,652 on Bunta, and on Chie by the lobby; Chie runs 12 hashes after it, 17 held to the players' in all |
| `rooms-four-resync` (`-win`) | five headless, a room of several | Ayame hosts a match of four with Bunta, Chie and Dai, parts at 2,700 and resyncs by itself; Sora watches from the fight's start, keeping replays | the resync at 2,948, 1.06 MB whole to the three guests; the lobby given it as the difference from frame 2,700's, 320 KB (896 KB raw); Sora parts, puts it in, runs 49 hashes after it, 70 held in all; 49 hashes the same on the four after it; Sora's replay keeps the state whole and plays back through it |
| `win-resync` | two Windows windows (dummy drivers) | the guest parted at 1,450, neither resyncs by itself, the guest holds Select (the plan's) | the hold asks at 1,697, the resync at 1,719; 25 hashes the same after; the window's replays the same on both |

`port_net.py resync` is part of `make port-net` (`port_net.py all`) and
so of the gate.

**What is left.** Two real PCs over the internet, and a desync that is
one (no desync has been found between two machines of one build since
section 12's "Under load"; the resync is proved on desyncs made on
purpose). A resync's three seconds and Select's one are guesses to try
with players. A spectator who comes into a fight of four after its
resync waits for the next fight.


## 22. A tournament in a room

ROADMAP 31's room half: a tournament of a room of several (section 16),
its bracket run by the room's host, each match played by the two
entrants whose turn it is while the rest of the room watches, CPUs among
the entrants. The code and how a match's state carries it: PORT.md, "A
tournament in a room"; how a player uses it: PLAYING.md, "A tournament
in a room".

**Nothing of the lobby changed**: it is version 4, as deployed. The
bracket goes by what rooms already carry.

- *The board.* The host opens a match beside the room's (`SIDE_OPEN`)
  that nobody joins, and sends as its feed's settings (`FEED`, its
  `SETTINGS` part, the 64 bytes a spectator is given of a match) the
  tournament as it stands: a mark (`RBTN`), the tournament's number and
  the board's version, the host's id, whether it is over, the entrants'
  count, single or double, the rule and its number, the dice, the
  entrant of each seed, each entrant (a member's id, or `0x80` and the
  CPU's level; its fighter and weapon), the winners of the matches
  decided in their order (a bit each) and which were walkovers, the
  entrants who left, the two members who are to play next and the
  room's match the pair comes after. The server keeps the settings as
  last sent, and every member asks for them (`WATCH`, no start, no
  frame) about once a second while the host has a match beside that
  nobody joined; the newest version is taken and the bracket built again
  from it. The server lets a match beside nobody joins go after two
  minutes (`--side-timeout`): the host opens another, and the members ask
  that one. A member's page offers no one the board to join.
- *The rotation follows the bracket.* The host sets a fixed turn once
  every member has read the first board (it names nobody: a player comes
  into a room playing), and each member says it plays (the `ROOM`'s flag
  that puts it in the rotation) only while the board names it, and only
  once the board is newer than the last room's match it was in. The
  server's first two free players are then always the board's two, and
  it makes their match (its code, its ticket) as any rotation's. The two
  who have just played say they do not before the host's next board, so
  they are never paired again by mistake. Where the host is one of the
  two, it lets its board go (`SIDE_DONE`) once the other says he plays: a
  member in a match beside the room's is never free.
- *A match* is a room's match (`ONE_MATCH`, its players telling the
  result). Its session's host holds the tournament's rule and sends the
  match's two entrants in its welcome (protocol 22, below); the host of
  the room reads the result in the roster's last result, by the two who
  played, and reports it into the bracket. A match not met, or a draw,
  is played again.
- *The CPU's side* is carried by a member: the room's host, or where the
  host is the CPU's opponent another member; two CPUs by the host and
  another. Both machines of the session compute the CPU (it is the
  game's own, in the state); the carrier's pad plays nothing. Two CPUs
  are judged after three minutes, as on one machine, and here a player
  against the CPU as well (the CPU circles a player as it circles
  another CPU; the room waits on the match); the session ends there. With nobody to carry the second CPU, the dice decide.
- *A leaver* loses the match he is in (the server gives it to the other,
  as in any room's match), and once he is out of the room each of his
  later matches is a walkover. The CPU playing on for him is not chosen:
  a room's match is a session of two PCs, and section 19's CPU for a
  leaver is the host's of four. A match whose CPU's carrier left is
  played again. The host's leave ends the tournament.

**Protocol 22** (`SESSION_PROTOCOL`, setup codes `P22-...`): the welcome
carries a room's tournament's match (`MEET_TOURNAMENT`, four words: on,
the rule and its number; each entrant's pad, the host's 0 or the
guest's 1 or the CPU, and the CPU's level; their fighters and weapons;
the dice), put in the state of both machines as the session begins, and
the session's result is by entrant. 21 and 22 do not meet; the lobby,
its relay and its feed are unchanged.

**Proved** on this PC, the server run here, every program headless
(`tools/port_net_lobby.py`'s `rooms-tournament` and
`rooms-tournament-leave`, in `make port-net` and the gate's
`port-net-windows` as `-win`) or a hidden window with SDL's dummy drivers
(`win-tournament`), 60 ms each way and 5% lost on every match's session:

| Case | What runs | Result |
| --- | --- | --- |
| `rooms-tournament` | five headless programs: Ayame opens the room and begins the tournament (`--room-tournament single,order,one,seed:7,@Ayame,@Bunta,@Chie,@Dai`) once the four are in; Sora a spectator | the semifinals Ayame beat Dai and Bunta beat Chie, the final Ayame beat Bunta, each the room's rotation's match of the board's two (room matches 1 to 3), 61 hashes on its players, watched by the other three (18 or 19 of them each held to the players' as they came), the same on all; the champion Ayame on all five, the places 1 Ayame, 2 Bunta, 3 Chie and Dai; about two minutes |
| `rooms-tournament-leave` | five headless programs: Bunta, Chie, Dai and a CPU (Master, Kannuki with a katana) in a double elimination run by Ayame, who is no entrant and carries the CPU's side; Sora watches; Dai leaves his first match (room match 2, against Chie) in its fight at frame 1,700, and the room (`--leave-at 2:1700 --room-quit-left`) | Bunta beat the CPU, Chie beat Dai (his leave), Bunta beat Chie in the winners' final, the CPU had Dai's losers' match as a walkover, Chie beat the CPU in the losers' final, then won the grand final, and Bunta the reset: seven matches of the bracket in six room matches, each the same on all who ran it (220 hashes for the two against the CPU, judged after three minutes, decided at frame 6,608; 56 to 61 for the others); the champion Bunta on the four who stayed; under six minutes |
| `win-tournament` | four Windows windows (SDL's dummy drivers): the host opens a room of several from its pages, three windows of the command line come in, and the host begins the tournament on the room's page (its fifth row, the four players, single, one fight) | the host's page began it (seeded at random: Hostess against Chie and Bunta against Dai), the bracket's page up at once; the three room matches each played by its two and watched by the other two windows (the host from its pages), the hashes the same on all four; the champion Hostess on all four; the pages' pictures (the room, the bracket at the start, at the end) in the main checkout's `tmp/room-tournament/`; under three minutes |

**What is left.** A room of real players on real networks. The board's version is the host's own count:
a member who comes in after a tournament began reads it whole, but a
member who comes in playing may be paired by the server with one of the
board's two before his program has read the board (a second at most);
the host then counts nothing of that match.
