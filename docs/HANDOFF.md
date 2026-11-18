# Handoff, 2026-11-17

Where ReBushido stands at the end of the session that took the native port
from "playable with a remastered picture" through most of the owner's
longer roadmap. The standing rules are in [`CLAUDE.md`](../CLAUDE.md) and
[`AGENTS.md`](../AGENTS.md); the plan is [`ROADMAP.md`](ROADMAP.md); how to
play is [`PLAYING.md`](PLAYING.md); online play is
[`NETPLAY.md`](NETPLAY.md); mods are [`MODS.md`](MODS.md).

## What there is

- `main` holds everything and is pushed. Every agent branch is merged.
- **The gate** is `python tools/gate.py` (about 9 minutes: every check,
  the port's builds, rollback, netplay, the 17 play tests four at a time;
  `--owner` refreshes the owner's copy, `--push` pushes; `--changed REV`
  picks the play tests a change reaches). Play tests may run side by side
  (a lock per run, about eight emulators at most); a test fails on wrong
  results, never on slowness ([`BUILD.md`](../BUILD.md), "The gate").
- **The owner's copy** (`rebushido.exe`, `SDL2.dll`, `mods/` at the
  repository's root, not committed) is refreshed after every verified
  merge by `python tools/owner_copy.py` (`CLAUDE.md`). The mods made from
  the disc live in `tmp/mods/` (`hd`, `art`, `stages-hd`); rebuild one
  there when its tool changes.
- **Defaults:** every new feature is on in the window build (the owner,
  2026-11-17): Remastered, cast shadows, `frame_rate = display`,
  `smooth_light`, `effects = smooth`, sky, see-through, the ground and the
  stage's pieces as meshes, the fighters' own models, quick loading,
  `run_ahead = 2`, `low_latency`, the English and sharp menus. Settings
  that change how fights play (Bugfixes, gameplay mods) and build options
  (`-O2`) are still asked.

### Built this session

| Area | What | Where it is written down |
|---|---|---|
| Picture fixes | fighters' joint gaps (16.7% of body faces lost their exact place), floor z-fighting on tile seams, fighters hidden by terraces (the console's slot rule), movies and the title black under Remastered, the pause dropping to the old look, full screen at start | PORT.md |
| Shadows | `cast`: from the fighters', weapons' and HD models' own faces; never on a fighter (also lying down, also between frames); on floors built of stage pieces; at the feet where the game casts none | PORT.md, "The shadows" |
| Fighters | all 37 models and 58 weapons rebuilt in HD from the disc (`tools/fighter_hd.py`), skinned on the game's bones, colour tables and tints, exact joints; HD cloth, a smooth surface through the game's own cloth points, pushed 6 units outside the HD body (cloth inside it: 9.2% to 0.23%); the first-person tiny figure; hitboxes untouched | PORT.md, "The fighters' own models" |
| Scene | night sky on 28 open stages, walls thinned before a fighter, the stage's static pieces (railings, fences, walls, props: 412 on 34 stages) built as meshes, HD stage textures by page, smooth per-pixel light, effects placed exactly | PORT.md |
| 2D | the `ui` mod (lettering in OFL fonts, Archivo and Marcellus), the English top menu, the `art` mod (every 2D texture 6x from the disc), the title picture, fades kept remastered | MODS.md |
| Mods | `mods/` with manifests; textures, models, skies, music (`.ogg`/`.wav`), a stage's files, weapons and motions (`weapons/NAME.txt`, `motions/`) | MODS.md |
| Controls | one input point; remapping per player in `rebushido.cfg` and the settings menu (F1) | PLAYING.md |
| Online | rollback netplay (GGPO's design in C) in the window: Play online page, host/join, build and disc checks, the host's cards, versus select over the net, rematch, disconnect; a false-desync alarm under load found and fixed; stress options in `tools/port_net.py` | NETPLAY.md |
| Speed | batched video-memory uploads, pacing at 85 Hz, `--profile`; quick loading (11.9 s of loads to 0.76 s); input lag about 117 ms to about 33 ms (late input read, run-ahead, `low_latency`) | PORT.md, "Where the time goes", "Quick loading", "Input lag" |

## What is left, in the order I would take it

1. **The owner plays this build.** Every proof is headless (Mesa's
   software OpenGL, scripted input). Most worth looking at: the HD
   fighters and their cloth, shadows on every kind of floor and on a
   fallen fighter, railings and fences now meshes, the building
   rooftop's shimmer (the city-lights backdrop, not the railing: it wants
   a filter of its own), smoothness at 85 Hz with `run_ahead = 2` (F3: if
   under 100%, `run_ahead = 1`), input feel in true full screen (F11),
   quick loading's two-second holds on screens you read.
2. **Online between two real PCs**, and on Windows (the Play online
   page's restart uses `_spawnv`, never run). The owner's calls in
   NETPLAY.md, section 13: the address a host listens at, getting through
   routers (port forwarding, hole punching or a relay), the draw distance
   online.
3. **Shadow maps and a stage's own lights** (ROADMAP 14): the cast shadow
   is one sun flattened onto the ground; walls, steps, self-shadowing and
   torches need shadow maps. The HD meshes and the stage's meshes are
   what they need.
4. **Faces and hands** of the HD fighters (`tools/fighter_hd.py` rounds
   them only); the chanbara, henchman and practice tints were never
   reached in a fight.
5. **New weapons**: a slot on the select screens, a motion set of a
   mod's own, a tool to add one motion to a BBM, sounds in the weapon file
   (MODS.md, "Weapons and motions").
6. **2D still only upscaled**: setup mode's pages, slash mode, the
   endings, other modes' results, "Waiting for Challenger", portraits,
   the loading maps (each a table row in `tools/port_ui.py`).
7. **Older open items**: `port_fps_prove.py` still fails on
   `port_other_fight` alone (14 to 20 out-of-order pictures between
   frames on the cave stage, where rails before the camera change colour
   from frame to frame in the game's own pictures); music per stage (the game shares five pieces);
   `src/port/window/` outside lint and count; the music a tenth louder
   than the emulator's; the original's bugs left alone (REBUSHIDO.md).

## Faster iteration, for the start of the next session

The owner's ask (2026-11-17): make development faster. In the order of
what they buy for what they cost:

1. **Build in WSL's own file system, not on `/mnt/c`.** Every build
   compiles through the Windows drive, which is slow for thousands of
   small files: a large part of the 3–4 minutes a port build takes. A
   Linux-side copy synced from the repository typically builds 3–10
   times faster.
2. **A compiler cache (ccache)** for the Linux and the Windows cross
   builds: most of a rebuild is files that did not change.
3. **The owner's RTX 3090 for the picture provers.** Every prover and
   headless screenshot runs on Mesa's software renderer, often 10–50
   times slower than a card. WSL can reach the card through Mesa's D3D12
   driver (`GALLIUM_DRIVER=d3d12`), or the Windows build can draw into a
   hidden window on the card: prover runs in minutes, and numbers that
   match what the owner sees.
4. **No more merge conflicts in the same three places.** Nearly every
   merge collided in the settings table (`src/port/window/choices.c` and
   its groups), its built-in test list (`tests/port/settings.c`) and the
   defaults in `keymap.c`. Each feature registering its own settings in
   its own file ends most "merge main into your branch" round trips.
5. **A "report a bug" key**: one press keeps the screenshot, the log's
   last seconds, `rebushido.cfg`, the frame-time profile and the build
   in one folder, so each report comes with what an agent needs.
6. **Quick-boot in the window build** (`--fight STAGE FIGHTER FIGHTER`,
   as the PlayStation build's quick-boot): straight into a fight, for
   the owner and for agents.
7. **A frame-time budget in the gate**, so a slowdown like run-ahead's
   on 2026-11-17 is caught before it reaches the owner's copy (being
   added with the run-ahead fix).
8. **Main's baseline numbers cached per commit** (provers, profiles):
   agents stop re-running main to compare against it.
9. **Fewer heavy agents at once**: five agents building and running
   provers on one machine starve each other; three focused ones may
   finish sooner.

## The owner's decisions

- Release 0.1 (tagging and publishing are the owner's).
- `-O2` for the players' build: 10–35% faster, traces identical everywhere
  checked.
- Online: listen address, NAT, draw distance (above).
- Which of the original's bugs to fix (REBUSHIDO.md, "Left as the original
  has it").
- A modern control layout (one more table in `src/port/window/keymap.c`).
- `READ_BLANKS` (quick loading's two-second hold on screens one reads).

## Picking the work up

- New agents are Opus, in their own worktrees, up to five at once; give
  their WSL builds a private `TMPDIR`; disc-made mods go in the main
  checkout's `tmp/mods/`, never only in a worktree.
- Merge an agent's branch, then `python tools/gate.py --owner --push`.
  A branch that conflicts goes back to its agent to merge `main`.
- The picture provers use `build/port-gl/rebushido-gl`, built only by
  `python3 tools/port_gl_build.py`: rebuild it before
  `tools/port_gl_prove.py` / `port_fps_prove.py` / `port_stage_prove.py`.
- Pictures for the owner go in `Desktop\ReBushido Screenshots`; this
  session's are in its `Session 2026-11-17` and `Investigation
  2026-11-16` folders.
- Six empty folders `.claude/worktrees/agent-*` were held open by this
  session's agents when it closed; delete them (they are no worktrees and
  hold nothing). `tmp\sdl-dl` can go too; `tmp\pcsx-redux`,
  `tmp\redux-portable`, `tmp\tools` (Blender) and `tmp\mods` stay.
