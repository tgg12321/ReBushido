# Handoff, 2026-11-16

Where ReBushido stands at the end of the session that made the native
port playable and started its remastered picture, and what is left. The
standing rules are in [`CLAUDE.md`](../CLAUDE.md) and
[`AGENTS.md`](../AGENTS.md); the plan is [`ROADMAP.md`](ROADMAP.md); how
to play is [`PLAYING.md`](PLAYING.md).

## What there is

- `main` holds everything. No agent is running, and every agent's branch
  is merged except one, kept on purpose (below).
- The PlayStation ReBushido builds, fits and passes its play tests; the
  switches-off build still matches the retail bytes.
- The native port is a 64-bit program for Windows and Linux (SDL 2):
  window, sound, pads and keyboard, memory cards, movies with sound,
  logs in `logs/`, settings in `rebushido.cfg` beside the program.
  `wsl bash tools/build.sh port-sdl-windows` builds it into
  `build/port-sdl/windows/`; the owner's copy is `rebushido.exe` at the
  repository's root (not committed).
- The remastered picture (OpenGL 3.3, F2): high resolution,
  antialiasing, the whole stage drawn (`draw_distance max`), 16:9 with
  the HUD anchored, more pictures a second (`frame_rate`), the ground of
  all 38 stages as one mesh (`stage_mesh`, on by default), and soft
  shadows (`shadows`, off by default: `soft` darkens under the game's
  own shadow mesh, `cast` under the fighters' own bodies). F3 the counter, F11 full screen,
  F12 a screenshot.

## What is left, in the order I would take it

1. **The owner looks at this build.** Nothing of the remastered picture
   has been seen on a real graphics card by anyone else: every proof is
   Mesa's software renderer with no window. To look at: practice mode on
   the mesh (it was on tiles and flickered when the camera turned);
   fighters hidden by terraces again; the fog on long floors; flicker of
   fighters and shadows at `frame_rate = display`.
2. **Shadows: look at `cast`, then a default.** `shadows = cast` is the
   answer to "blocky and polygonal, and jitter when still": the shadow
   is cast from every face of the fighters' bodies, weapons and
   scabbards in floating point instead of the game's coarse shadow
   mesh rounded to four units, and darkens by 68 hundredths
   (`cast:N` for another strength; why 68 is in `PORT.md`, "The shadows
   cast from the fighters' own faces"). `shadows = soft` (55, the game's
   mesh) is still there. Not yet seen on a real card. Known limits: the
   shadow lies on the plane of the fighter's ground and not on steps or
   slopes, a wall in front of a shadow is darkened with the ground, the
   soft edge does not widen with height.
3. **Pictures between frames on what is not ground.** The ground no
   longer needs pairing, but fighters, shadows and stage pieces still
   go through `gl_tween.c`. The owner's "always flickering and in
   motion even when they should be still" was measured and three causes
   mended (`PORT.md`, "What made a fighter standing still move": slivers
   held, weapons' and shadows' nodes renamed every frame, faces one
   frame has not); it wants looking at on the owner's 85 Hz screen. Left:
   `port_other_fight` has 20 frames whose pictures in between are out
   of order, on the cave stage where rails against the camera change
   colour from frame to frame in the game's own pictures, and two
   frames (blanks 3,260 and 3,374) whose phase 0 lacks a near face of
   the cave wall the game's own picture has. On the tile path
   (`stage_mesh off`) hairline gaps between tiles remain.
4. **The other lighting candidates.** Branch
   `worktree-agent-adce6b210aa0a05f8` (10bd277dd, not merged) has
   `smooth_fog`, `ambient_occlusion` and `smooth_light`. Its shadows
   were rejected and are replaced; the mesh now does the fog's job on
   the ground. Take `smooth_light` from it if wanted, then delete it.
5. **Make the high frame rate the default**, once 3 is settled
   (`frame_rate = original` with nothing said today).
6. **The port's older open items**: the music about a tenth louder than
   the emulator's; the GPU's rounding of ties; the link cable is a
   stub; chanbara's best times were never reached by a test; the sweep
   plans are not kept as a test; `src/port/window/` is outside
   `tools/lint.py` and `tools/count.py`; `port_gl_prove.py`'s `blocks`
   verdict says NO on `main` (it did before the last two merges too).
7. **The English top menu and the settings menu** (roadmap 11;
   [`MODS.md`](MODS.md), "The English menu"; [`PLAYING.md`](PLAYING.md),
   "The settings menu"). Both proved headless only. To look at: the
   English rows' lettering at the player's own window size, and "Group
   Battle Mode", the longest, drawn smaller to fit its 64 texels; "Setup
   Mode" for 支度の間, since every row shares its second piece, "Mode";
   the menu's look and its keys on a real controller. The English is
   drawn by Graphics: Remastered alone, and the window starts as the
   options page says, Original unless it is changed, so a player sees it
   once Remastered is on (F2, or the menu).
8. **The original's bugs left alone** ([`REBUSHIDO.md`](REBUSHIDO.md),
   "Left as the original has it"). My recommendation: the three that
   only change the picture under Bugfixes, and the CPU's and the
   fight's under a separate setting, since they change how the game
   plays.

## The owner's decisions

- Publishing release 0.1 (tagging and publishing are the owner's).
- Which of the original's bugs to fix (8).
- The English rows' names and look, and whether the window should start
  in Remastered so that they are seen (7).
- Which shadows by default (`original`, `soft` or `cast`), and how
  dark (2).
- The high frame rate by default (5).
- `tmp\sdl-dl` can be deleted by hand; `tmp\pcsx-redux` and
  `tmp\redux-portable` are the emulator the play tests use and stay.

## Picking the work up

- Before a push: `wsl bash tools/build.sh clean-check`, `wsl bash
  tools/build.sh checks`, `python tools/format.py --check --all` (run it
  yourself after every merge: agents report it clean when it is not),
  and `wsl bash tools/build.sh playtest` on a quiet machine, reading
  its result before pushing.
- A change to `src/port/gl/`: `python tools/port_gl_prove.py --quick`,
  `--gl-tween 5`, `python3 tools/port_fps_prove.py`,
  `python3 tools/port_stage_prove.py`.
- The owner's screen stays at 85 Hz (144 Hz flickers through its
  cable); the window is 2560 by 1369, which the provers take as
  `--window 2560x1369`.
- Pictures for the owner go in `Desktop\ReBushido Screenshots`.
- The old agents' worktrees under `.claude/worktrees/` are all merged
  (but the one of 4) and can be removed with `git worktree remove`.
