# Handoff, 2026-11-18

Where ReBushido stands at the end of the performance run. The standing
rules are in [`CLAUDE.md`](../CLAUDE.md) and [`AGENTS.md`](../AGENTS.md);
the plan is [`ROADMAP.md`](ROADMAP.md); how to play is
[`PLAYING.md`](PLAYING.md); online play is [`NETPLAY.md`](NETPLAY.md);
mods are [`MODS.md`](MODS.md). How each piece below works, with what was
measured and what was tried and dropped, is in [`PORT.md`](PORT.md).

**The next session is the lighting work** (the owner, 2026-11-18):
ROADMAP item 14, shadow maps for the sun first, then lights of a stage's
own that a mod can add, casting on walls, steps and the fighters
themselves. The cast shadow's jitter at rest was left to it on purpose
(item 14 lists the suspects); check it is gone when it lands.

## What there is

- `main` holds everything and is pushed; every agent branch is merged.
- **The gate** is `python tools/gate.py` (about 11 minutes: the eleven
  checks, the port's builds, port-rollback, port-net, port-budget,
  port-bench, port-pause and the 17 play tests). The play tests run with
  `--strict`: a missing emulator or a skipped test fails the gate.
- **The owner's copy** (`rebushido.exe`, `SDL2.dll`, `mods/` at the root)
  is refreshed after every verified merge (`tools/owner_copy.py`).
- **The owner's cfg** now has `vsync = on` and `fighter_models = on` (he
  asked, 2026-11-18); `run_ahead = 2`, `shadows = cast`.

## The performance run, 2026-11-18

The target (the owner): 144 a second at 2560 by 1440 on his RTX 3090 and
Ryzen 9 9950X3D, 6.94 ms a refresh, with headroom for four players online
on a large modded stage with much richer lighting and models.

**Where it ended** (`port_bench.py` at `c212be752`, a quiet machine, ms;
the window's thread draws the pictures, the step's thread runs the game):

| case | window, 95% | window, 99% after 1 s | step, 95% / 99% | picture to picture, 99% | card, 95% |
|---|---|---|---|---|---|
| ra0 | 3.78 | 4.09 | (one thread) | 4.08 | 3.19 |
| ra1 | 0.95 | 1.38 | 3.56 / 3.83 | 4.37 | 3.30 |
| ra2 | 0.79 | 1.15 | 4.66 / 5.07 | 5.51 | 3.54 |
| ra1, four fighters, the ground twice | 1.05 | 1.53 | 3.47 / 4.02 | 4.30 | 4.28 |

At the start of the day ra1 was about 28 ms of processor and 10 ms of
card a frame. No blank after a fight's first second goes over 6.94 ms in
any case; the one slow blank left is the fight's very first (16 to 18 ms,
the card's or the shadows' first use), inside the first second.

**What did it** (each a section of PORT.md):

- The software GPU paints on four threads beside the game ("The software
  GPU beside the game").
- The step itself is the first frame ahead: run-ahead 1 no longer saves
  and loads, run-ahead 2 runs one frame ahead, frames ahead mix no sound.
- The pictures between frames no longer touch video memory or hold the
  painters; the pairing of frames 1.16 to 0.3 ms; a fighter's colour
  tables no longer searched at every primitive ("The tween off video
  memory").
- The step records what the card draws and runs on a thread of its own;
  the window's thread draws the record and the pictures beside it ("The
  step on a thread of its own"). `RB_STEP_THREAD=off` restores the old
  flow.
- The refresh inside a step gets a picture of its own ("The step's
  refresh"); a hidden window keeps Windows' fine timer.
- The HD fighters on the card: two fighters 2.5 to 0.9 ms, four 1.27 ms
  ("The fighters cheap on the card").
- Earlier in the day: only changed video memory uploaded, MSAA for
  supersampling, compute skinning and cloth, state saved by pages
  written, the driver's first-thread-exit stall moved to the load, mod
  textures loaded off the drawing thread, the orange flashes (effects
  near the camera) fixed.

**What the gate holds** (`tools/port_bench.py`): the card's 95% (ra0,
ra1, ra2, stress), the window's 95% and settled 99% (ra1, ra2, stress),
ra0's processor 95%, ra1's worst settled blank, the step's 95% (ra1,
ra2), and no blank over 50 ms after the first second, fail the gate
whatever `--report-only` says. Still reported only: the fight's worst
blank including its first second, the hitches share and the picture to
picture gap. The bench takes the card lock and records the machine's
load; a loaded run warns.

**Fixed along the way:** the pause menu invisible at run_ahead 2 with the
remastered picture (now `port-pause` in the gate); Escape pauses and goes
back and never ends the program (Quit is the settings menu's last row);
the cave railing's shimmer (an in-between camera rounded to whole units);
the port-rollback flake (the build mirror copying old files back); the
quick-boot disc flake (WSL's clock stepping backwards).

**What is left of the performance work**, none of it blocking:

- The picture to picture 99% has the least room (4.4 to 5.5 ms): the
  step's end, its record and the first picture come one after another.
  Interpolating on the card (a persistent vertex buffer, a shader mixing
  two frames) and not drawing the record's own frame under the first
  picture would cut it.
- The fight's first blank (16 to 18 ms): warm the shadows' and the
  card's first draws at the load.
- A heavier made-up scene for the bench: the stress case only doubles
  the ground; four HD fighters on a large modded stage with many lights
  is what the owner's goal needs measured, and the lighting work will
  add to it.
- A backward step of WSL's clock could in principle leave an object
  stale after a source edit (BUILD.md, "Where the build runs").

## The lighting overhaul, 2026-11-18 (ROADMAP 14)

The owner: a total overhaul as a modern game would have it, still Bushido
Blade 2, the Remastered picture alone. How it works: PORT.md, "The
lighting overhaul".

- **a, the sun's shadow maps** (`shadows = map`, built in): three
  cascades of 2,048 along the stage's own shadow direction; the ground,
  the pieces and the fighters' models read them at the pixel; soft
  (PCSS), held still by whole texels and depth steps. The cast shadow's
  jitter at rest is gone with the mask. Four stages under a roof keep
  their ceiling from casting (`g_sun_stage_casts`). Fixed on the way: the
  ground's normal attribute was never enabled, so `smooth_light` never
  reached the ground.
- **b, lights with a place** (`lights = on`): the light list
  (`gl_lights.h`, filled by `gl_light_list.c` from the mods' `lights/` and
  the clash effect) per pixel on the ground, pieces and fighters, sorted
  into a world grid, the strongest four with cube shadows; a remade
  fighter's normal and ORM maps read and lit (GGX highlight from the
  sun).
- **c and d** (2026-11-19, `src/port/gl/gl_post.c`): reflections on the
  screen (`reflections`), ambient occlusion (`ambient_occlusion`), the
  lights' bloom (`bloom`) and a tonemap that rolls off only the light the
  frame's lights add (`tonemap`), all on; the colour buffer's alpha says
  what painted a pixel, the mesh writes its depth beside its slots. About
  0.7 ms of the card at 2560x1440 (PORT.md, "What it costs").
- **The floor's plane mirrored** (2026-11-19, the owner: the dojo's
  reflections "warped and not detailed"): the warp was the normal worked
  out from the depth, 3 to 10 degrees off by tile and in bands (the
  mesh's per-tile depth push, the half floats). Level floors now reflect
  in the stage's floor plane, mirrored on the screen by a compute shader
  at half the size (exact, steady, blurred by roughness and distance);
  the fighters the game draws itself mirrored too, their depth from the
  shadows' casters; the screen's ray kept for what is off the plane.
  The lighting's 95% 1.74 to 1.85 ms against 2.0 (PORT.md, "c.
  Reflections"). Pictures: the main checkout's `tmp/reflections2/`.
- **Left:** the light files' placing (only the parking lot's reach the
  fight in the tour); the normal map's bitangent sign on a remade model
  in a fight; the stage pass 0.2 ms above main's with c and d off; the
  stress case's card over the refresh (main's too).
- **Waits on the owner:** his own cfg says `shadows = cast`, so he sees
  the sun's maps only after setting `shadows = map` (or Shadows: Sun in
  the menu); whether a stage's own geometry should cast at all (the
  shipyard, team 1, the terraced fields and the building rooftop darken
  by a quarter to two fifths in the lower half of the picture, PORT.md);
  pictures in the main checkout's `tmp/lighting/`.

## What is left, in the order I would take it

1. **The lighting work** (ROADMAP 14), above; give it a budget in
   `port_bench.py` from its first commit.
2. **The owner plays this build**: the HD fighters with vsync on at
   run_ahead 2, the pause, Escape, shadows (the jitter at rest is known).
3. **Online between two real PCs**, and on Windows (NETPLAY.md, section
   13: the listen address, routers, the draw distance online).
4. **Input lag** (ROADMAP 15): measured part by part and cut where safe
   on 2026-11-18 (`late_input`; PORT.md, "The press to the screen, part
   by part" and "The input read late"). The paced bench now counts the
   card before a made-up refresh, so its older paced numbers (0.9% of
   refreshes missed at 144) were the processor's alone: with the card,
   `run_ahead 1` at 2560x1440 and 144 a second misses 9 to 16% and its
   first picture is the second refresh after its input. Next cut: not
   drawing the record's own frame under the first picture (the
   renderer's). For the owner's screen: whether `late_input`'s
   one-in-a-hundred late first picture is ever seen, and full screen
   against a window.
5. **Faces and hands** of the HD fighters (ROADMAP 19: the pipeline and
   its checks are in, `tools/port_models/`, and Tatsumi is remade; the
   other fighters are left); the chanbara, henchman and practice tints
   never reached in a fight.
6. **New weapons**, **2D still only upscaled**, and the older items: as
   in ROADMAP.md.

## The owner's decisions

- Release 0.1 (tagging and publishing are the owner's).
- `-O2` for the players' build: 10–35% faster, traces identical.
- Online: listen address, NAT, draw distance.
- Which of the original's bugs to fix (REBUSHIDO.md).
- A modern control layout; `READ_BLANKS` (quick loading's hold).

## Picking the work up

- New agents are Opus, in their own worktrees, up to five at once (the
  owner, 2026-11-18). Brief them with the rules of CLAUDE.md, the
  worktree's junctions (`tools\gcc-2.7.2`, `tools\sdl2-mingw`, `.venv`)
  and hardlinks of the disc, `REBUSHIDO_EMULATOR` for play tests, and a
  private `TMPDIR` for WSL builds (WSL's `/tmp` gets wiped). A junction is
  removed alone (`cmd /c rmdir LINK`), never through a recursive delete:
  one wiped the shared `.venv` on 2026-11-18.
- **Provers:** `--gpu card` while iterating (93 to 527 s), llvmpipe once
  on the final commit; compare with `--against main` against the stored
  baseline instead of re-running main.
- **After each verified merge:** `wsl bash tools/build.sh
  port-sdl-windows`, `python tools/owner_copy.py`, push; and
  `python tools/baseline.py --store` (about an hour; `--only bench` six
  minutes) so the next agents have main's numbers. The stored provers'
  records are of `5c0eead12`; the bench's of `c212be752`.
- **Timings:** only a run holding the card lock (`tools/card_lock.py`) on
  a quiet machine is a number; with several agents at work, judge by
  interleaved A/B runs. Five agents at once once put the processors at
  73% and doubled every time.
- An agent's report can go to another agent that messaged it last: ask
  it to report to the coordinator.
- If every `wsl` call fails with `E_UNEXPECTED`, `wsl --shutdown` brings
  it back (it ends every WSL process: tell the agents to rerun).
- One empty folder, `.claude/worktrees/agent-ac424a93b83bf5773`, was held
  open when the session closed; delete it.
