# Handoff, 2026-11-20

Where ReBushido stands after the session of 2026-11-18 to 20 (lighting,
models, menus, online, match rules, replays, releases). The standing
rules are in [`CLAUDE.md`](../CLAUDE.md) and [`AGENTS.md`](../AGENTS.md);
the plan is [`ROADMAP.md`](ROADMAP.md) (items 14 to 25 are this
session's); how to play is [`PLAYING.md`](PLAYING.md); online play is
[`NETPLAY.md`](NETPLAY.md) (sections 13 to 15); mods are
[`MODS.md`](MODS.md); how each piece works is in [`PORT.md`](PORT.md).

## The state

- `main` is pushed at `ab47df77e`; the gate passed on it (one step,
  port-update, failed on a test's menu path and passed after its fix).
  Every agent branch of the session is merged.
- **The owner's copy** at the root is `ab47df77e`, with `prepare/` (the
  first start rebuilds `mods/art` and `mods/stages-hd` from the disc,
  about 3.5 minutes; the old ones had been emptied by a cleanup on
  2026-11-19 and are gone) and `mods/fighters` (Tatsumi's geometry-only
  model, geo-01).
- **Releases** are the owner's, on GitHub Releases (2026-11-20): players
  and testers get builds from there; no hand-made test zips. None is
  published yet. The lobby on the VPS is version 2 (rooms of several,
  2026-11-20); keep it matching the latest published release.
- **The owner's cfg** still has `time_of_day = day` (the stage pages now
  ignore it and start at the stage's own) and `shadows` as the owner set it.

## Waiting on the owner

1. **The first real online test** with a friend, from a GitHub release
   (steps: PLAYING.md, "Playing online"; the friend needs only the US
   disc image, SLUS-00663, as .cue/.bin). Afterwards, read both
   players' `logs/`: the connection's path (direct, router port, punch,
   relay) and the round trip.
2. **The release key** is made (2026-11-20): the secret in
   `%USERPROFILE%\.rebushido-release-key\`, a copy on the VPS in
   `/root/.rebushido-release-key/` (mode 700/600), the public half in
   `config/release_public_key.txt`. Each release is `python
   tools/release_publish.py --notes NOTES.txt` (BUILD.md). Publishing is
   the owner's. The test zip of `ab47df77e` was built with `none` and
   never updates itself.
3. **Looks to judge in the game**: each time of day's colours
   (PORT.md, "The time of day"), each stage's floor reflections
   (`g_post_floors`), the stage lights placed (`tmp/stagelights/`; is
   the Kabuki eaves' band lanterns?), the font (Shippori Mincho), the
   match strip and cards (`tmp/matchrules/`), the online pages
   (`tmp/online-ui/`).
4. **Tatsumi's shoulders, knees and neck** (geo-01 left them as the
   game has them): a hand rebuild of the joints in Blender, or the
   renderer blending joints by rotation (every fighter at once).

## The VPS (the lobby, rendezvous and relay)

`ssh -i ~/.ssh/id_ed25519 root@135.181.108.74` (Git Bash; PowerShell has
no ssh). Hetzner, Ubuntu 26.04; also runs the owner's PokeBowl, an IPsec
VPN and AdGuard: leave those alone. The service is `rebushido-relay`
(systemd, DynamicUser, `/opt/rebushido-relay/`), UDP 7845, opened in
Hetzner's cloud firewall by the owner. Uninstall:
`bash /root/rebushido-relay-src/deploy.sh uninstall`. The internet test
(`wsl python3 tools/port_net_lobby.py internet --lobby
135.181.108.74:7845`) passed: punch and relay, 82 hashes alike; the
relay's own cost about 290 ms round trip from here (Helsinki).

## What the session built (all merged)

- **Lighting** (ROADMAP 14, 20): sun cascades plus a whole-stage map,
  every standing piece casting, scenery shadows on the game's own
  fighters; stage lights (mods/lights, 26 more, `times =`); fighters lit
  and shadowed by them (a twin shader program); planar floor
  reflections; AO, bloom (depth-tested), tonemap; flames held by depth;
  a time of day per stage (L1/R1 on the stage page) and night's own
  light.
- **Fighters**: the HD and Fable-remade models scrapped (archived in
  `tmp/models/archive-2026-11-19/`); geometry-only passes from the
  game's own texture (`tools/port_models/geometry.py`); the fighters'
  near plane at 16; the model viewer (`tools/model_viewer/serve.py`).
- **Menus**: ReBushido's top menu (Singleplayer, Versus, Training,
  Online Play, Options), no boot options page, no POV, the settings menu
  grouped (`menu = original` brings the old ones back; tests use it);
  one typeface everywhere; the stage page's panel (Style, Time, Music
  L2/R2, Match Circle/Start, Arenas).
- **Gameplay**: stage select in every mode, `unlock_all`, fight music
  in every mode (`fight_music`), match rules Stock N / First to N.
- **Online**: the lobby with rooms and passwords, the ways between two
  PCs (IPv6, UPnP/NAT-PMP/PCP, punch, relay), setup codes
  (`--check-online`), refusals on screen, sessions begun and ended in
  the running program (no restart), the online pages in the game's look
  (Quick Match, Host, Browse, Join by code or address).
- **Replays**: every fight to `replays/` (~215 KB: the start state,
  inputs, hashes), a Replays page, `--replay`, `--replay-check`.
- **Releases**: `make port-release` (refuses disc-made files), the
  first-start builder of the disc-made mods (a bundled Python, byte-
  identical output), the signed auto-updater (GitHub Releases,
  Ed25519/Monocypher, install on restart with a rollback).
- **Fixes**: the rooftop crash (a call short of an argument; `make port`
  now fails on any such call), the Vs. CPU next-fight hang (a looping
  piece before a read; a hurried load that reads nothing is paced),
  hidden windows never opening the PC's sound device, `cpu_prefer` (the
  step and painters on the larger-cache CCD of an X3D, nothing
  elsewhere), the bench's held measures with 25% slack (the owner: good
  performance in general, not strict).

## Next, in the owner's order

1. Whatever the online test shows.
2. ROADMAP 22: lobbies of more than two, spectators, the rotation of
   three (A+B, B+C, C+A). It builds on the no-restart sessions.
3. ROADMAP 25: a ladder and Elo through the lobby server (replays as the
   check of a result).
4. ROADMAP 24: 2v2 (four fighters in one fight, retargeting): the
   largest; the game's fight logic is for two.
5. Smaller: the room list's ping; leaving an online fight without
   closing the window; the group battle card's names still in the
   game's letters; the game's "WINS 00" beside the match strip.

## Lessons of the session (for agents too)

- **Disk**: an agent's worktree is ~4 GB; remove each after its merge
  with `tmp/cleanup/worktrees.ps1 -Do` (it now skips locked, dirty and
  recent trees: an earlier version removed a running agent's tree). C:
  filled once and made WSL's disk read-only until `wsl --shutdown`.
- **Junctions**: never delete through one (`build/bench/mods/*` point
  at `tmp/mods/*`; something emptied `tmp/mods/art` and `stages-hd`
  that way). Remove a junction alone with `cmd /c rmdir`.
- **Sound**: a Windows run must pass `--hidden` (now forces SDL's dummy
  audio) or `--no-sound`; the environment variable alone did not stop
  the Windows build from opening the real device.
- **Merges**: parallel branches collide in `session.c` (protocol and
  MEET fields), `menu.c` (rows; tests navigate by position), the build
  lists of `tools/port_sdl_build.py`, and `gl_draw.c` (FLAG/INFO bits:
  two branches took the same bit). Check each after a merge.
- **Agents forget `python tools/format.py --check --all`**; the gate's
  format step catches it.
- **Python heredocs through the Bash tool** turn `\\n` inside C string
  literals into newlines: write such scripts with the Write tool.

---

# Earlier: the handoff of 2026-11-18

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
  reached the ground. 2026-11-19: a fourth map holds the whole stage
  past 16,000 units (before, nothing past it had a sun shadow), every
  standing piece casts whether the frame drew it or not, the nested
  maps blend into each other at their edges, and the fighters drawn from
  the game's own polygons take the sun's shadows too (`FLAG_WORLD`).
  The frame's lights on those polygons came after (b). Still wanting, of
  a modern shadow stack: contact shadows (the fighters' own polygons are not in the kept
  depth), shadows of stage pieces left to the game's primitives, and the
  four roofed stages' sun.
- **b, lights with a place** (`lights = on`): the light list
  (`gl_lights.h`, filled by `gl_light_list.c` from the mods' `lights/` and
  the clash effect) per pixel on the ground, pieces and fighters, sorted
  into a world grid, the strongest four with cube shadows; a remade
  fighter's normal and ORM maps read and lit (GGX highlight from the
  sun). 2026-11-19: the fighters the game draws itself (what the owner
  plays) lit and shadowed by the list too, in a twin of the scene's
  program that only their batches use (PORT.md, "The fighters' own
  polygons"); pictures in the main checkout's `tmp/fighterlights/`.
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

## The time of day, 2026-11-19 (ROADMAP 20)

- **In:** L1 and R1 on a stage page turn a dressed stage's time (its own,
  dawn, day, dusk, night); `time_of_day` in the cfg and the menu for the
  modes with no page; the host's online (`MEET_TIME`, protocol 6). The
  look is `src/port/gl/gl_time.c`: the sun's way and colour (the maps
  along it), the ambient, the fog laid over the mesh, a day sky, the
  game's own fighters and props relit, the bloom, the light files'
  `times` and the stage's lights brighter at night (PORT.md, "The time
  of day"). At the stage's own time nothing changes.
- **Waits on the owner:** his eye on each time's numbers (the table in
  PORT.md is judged on pictures: the main checkout's `tmp/timeofday/`,
  `sheet_stageNN.png` the five times side by side, and
  `stage_page_dusk.png` the strip); the HUD's letters over a day sky.

## What is left, in the order I would take it

1. **The lighting work** (ROADMAP 14), above; give it a budget in
   `port_bench.py` from its first commit.
2. **The owner plays this build**: the HD fighters with vsync on at
   run_ahead 2, the pause, Escape, shadows (the jitter at rest is known).
3. **Online between two real PCs.** The ways through routers are built
   and proved on one PC (NETPLAY.md, section 14: the lobby with rooms
   and passwords, IPv6, the router's mapping, hole punching, the relay;
   Windows sessions, the menu's restart and the refusals on screen, all
   in `port-net-windows`). The lobby runs on the owner's server
   (`tools/relay/README.md`); UDP 7845 still has to be let in by its
   cloud firewall before the internet test (`tools/port_net_lobby.py
   internet --lobby 135.181.108.74:7845`) and a first play with a
   friend. The players' package is `make port-release` (BUILD.md, "The
   port's package").
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
- Online: the draw distance (NAT and the lobby are built, section 14).
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
