# Architecture

How the game program is put together: what runs each frame, which subsystem
does what, where the big data lives, and the words the source uses for all of
it. For the toolchain and the build see [`AGENTS.md`](../AGENTS.md); for file
formats see [`formats/`](formats/).

Some of this is a reading of the code, not something seen in the running
game. [`STATUS.md`](STATUS.md) says what was checked in the running game
and what was not.

## The program

One executable (`SLUS_006.63`, loaded at `0x80010000`) holds the whole game
except full-motion video, which a small overlay (`MOVOVL.EXE`) plays. The
engine called itself *Marionation* (it says so in its error strings). It is a
single loop with no threads: one frame of game logic, then wait for the
vertical blank, then show the frame.

Everything the player sees is one of two things:

- **the 3D scene**: a stage, two fighters, loose weapons, effects, seen by a
  camera. Used by the fight and by every screen that shows fighters (replays,
  cutscenes, win poses, the pause and after-fight menus drawn over them);
- **a 2D screen**: sprites cut from a sheet, loaded from one resource file per
  screen (main menu, character select, narration, staff roll).

## The main loop

`main` (`main.c`) boots (`sys_Init`, `sys_InitGame`), shows the boot image,
and then loops forever. Each frame it:

1. clears this frame's ordering table and picks this frame's primitive buffer
   (there are two of each, used on alternate frames: `GpuDb`);
2. reads the pads (`pad_Update`), steps the CD state machine (`cdrom_Update`)
   and keys on the sounds queued last frame (`snd_FlushQueue`);
3. calls **one handler**, `g_sys_screen_handlers[g_sys_screen]`;
4. waits out the frame period, waits for drawing to finish and for the
   vertical blank, flips the display and draws the ordering table;
5. during a fight, runs the pause menu if Start was pressed.

The handler is the current *screen*. A screen changes the game's state by
storing another screen in `g_sys_screen`; nothing else decides what runs.
Screens come in pairs, an `_INIT` that runs for one frame and stores its
`_UPDATE`, which runs until the screen is over. The screens are `enum Screen`
in `sys.h`; the table of handlers is in `main.c`:

| Index | `SCREEN_...` | Handlers | What the screen is |
|---|---|---|---|
| 0x00, 0x01 | `FIGHT` | `fight_Init`, `fight_Update` | The fight: load the stage and fighters, then one frame of fighting per call |
| 0x02, 0x03 | `DEBUG_MENU` | `menu_DebugInit`, `menu_DebugUpdate` | A text-only debug menu that picks fighters, stage and mode |
| 0x04, 0x05 | `REPLAY` | `replay_Start`, `replay_PlayFrame` | Replay of the fight just finished |
| 0x06, 0x07 | `CUTSCENE` | `cutscene_Init`, `cutscene_Update` | A story cutscene: recorded motion for both fighters and the camera, with a voice track |
| 0x08, 0x09 | `MENU` | `menu_Init`, `menu_Update` | The main menu and its pages (options, memory card, character select, narration); `menu_screen.c` |
| 0x0A, 0x0B | `STAFF_ROLL` | `staff_Init`, `staff_Update` | The staff roll |
| 0x0C, 0x0D | `AFTER_FIGHT` | `fight_AfterFightInit`, `fight_AfterFightUpdate` | After a fight: a low view of the loser lying and the winner standing by, and a three-item menu (Rematch, Return to Sub Menu, Return to Top Menu) |
| 0x0E | `UNUSED` | `sys_UnusedScreen` | Empty; nothing selects it |
| 0x0F | `MOVIE` | `sys_MovieScreen` | Run the movie overlay, then set the engine up again. The loop starts here |
| 0x10, 0x11 | `WINNER_VIEW` | `fight_WinnerViewInit`, `fight_WinnerViewUpdate` | The moment after the last blow: the camera swings to the winner |
| 0x12, 0x13 | `ENDING` | `cutscene_EndingInit`, `cutscene_EndingUpdate` | An ending: a cutscene with a still picture wiped in over it. Also the team-battle winner and newly unlocked fighters |
| 0x14, 0x15 | `TEAM_RESULT` | `fight_TeamResultInit`, `fight_TeamResultUpdate` | The team battle's table of results |
| 0x16, 0x17 | `STAGE_CARD` | `story_StageCardInit`, `story_StageCardUpdate` | Story mode's stage card, loaded when the story script asks |
| 0x18, 0x19 | `AFTER_MATCH` | `fight_AfterMatchInit`, `fight_AfterMatchUpdate` | The same three-item menu over a slowly turning view of the stage: after a team battle or a kaishaku |
| 0x1A, 0x1B | `SAVE` | `menu_SaveInit`, `menu_SaveUpdate` | The save prompts, then back to the main menu |
| 0x1C, 0x1D | `KAISHAKU` | `cutscene_KaishakuInit`, `cutscene_KaishakuUpdate` | The cutscene of a kaishaku: a fighter that has given up kneels and the other strikes it down |
| 0x1E, 0x1F | `BEST_TIMES` | `fight_BestTimesInit`, `fight_BestTimesUpdate` | The best-times table after a chanbara run |
| 0x20, 0x21 | `KILL_COUNT` | `fight_KillCountInit`, `fight_KillCountUpdate` | How many opponents were beaten, when a run against the CPU has beaten every opponent the save offers and they were fewer than twenty |

The screens from 0x10 down have all been seen in the running game.

While something loads, the loaders call `disp_DrawLoadingScreen`, which draws
`g_disp_loading_screen` (`enum LoadingScreen`: a plain picture, the next
opponent, the team scores, story mode's stage card) straight to the display.

### A frame of the fight

`fight_Update` (`fight.c`) is the heart of the game. In order:

1. the camera works out this frame's view (`camera_`);
2. each fighter gets its input: the pad, a recorded replay frame, the link
   cable, or the CPU's decision (`fighter_ReadPad`);
3. each fighter is stepped (`fighter_Update`): the current move advances, the
   motion data is decoded into a pose, the model's bones are placed, the body
   and weapon points are worked out in the world and left on the scratchpad
   (`FightScratch`);
4. the two fighters are tested against each other: weapons against bodies
   and against each other, pushing apart, paired moves (grapples, clashes);
5. loose objects and projectiles fly, bounce and hit;
6. the replay records the frame;
7. the scene is drawn: the stage's visible cells and the fighters' models
   are queued as nodes, the queue is flushed into GPU primitives, effects and
   the overlays (timer, counters, pause panel) are added;
8. stage sounds, the round's progress and the end of the round are checked;
   when the fight is over the handler index moves on.

## Subsystems

From the hardware up. A subsystem calls those above it in this list and,
with few exceptions, not those below.

| Layer | Subsystem | What it does | Where |
|---|---|---|---|
| Sony | PsyQ libraries | GPU, GTE, CD, SPU, pads, cards, serial | `src/main/psxsdk/` |
| Platform | CD and streaming | Reads files by index from a table of disc positions (`g_cdrom_files`: a file is a group, `enum CdromGroup`, and a number in it, `enum CdromDataFile`, `enum CdromAudioFile`); plays CD and XA audio; one state machine stepped every frame | `cdrom.c`, `include/cdrom.h` |
| | Sound | Sound banks (VAB, ids in `enum SndVabId`), a queue of sounds keyed on once a frame, positional and stage sounds | `snd.c`, `include/snd.h`, `fight_event.c` (a move's sounds; a stage's props and ambience), parts of `fight.c`, `main.c` and `resource_weapon.c` |
| | Pad | Both pads into one `PadState`: held, pressed, released; the button configuration | `pad.c`, `include/pad.h` |
| | Memory card | Save and load of unlocks, best times and options | `memcard*.c` |
| | Link cable | Two consoles exchange pads every frame | `link.c` |
| | Display | The two display buffers, fades, full-screen images, grayscale mode | `main.c`, `include/disp.h`, `tex.c` (the black and white option) |
| Engine | Memory arena | One 0x45000-byte buffer cut into up to ten movable blocks (`enum ArenaBlockId`) | `arena.c`, `include/arena.h` |
| | NDATA archive | The big data file: models, stages, weapons, motions, by entry id (`enum NdataId`) | `ndata.c`, `include/ndata.h` |
| | Resources | Loads the models, the weapons, the stage and the tiny model into arena blocks and fixes up their pointers; works out which weapon models and sounds the coming fight needs | `resource.c`, `resource_weapon.c`, `include/resource.h`, `render.c` (`reloc_`) |
| | Textures | TIM loading into VRAM, CLUT tints, scrolling textures, moving a mesh's textures (`tex_Shift`) | `tex.c`, `tex_scroll.c`, `include/tex.h`, `render.c` (`tex_Shift`) |
| | Scene graph | Nodes with a transform, a parent and a mesh; the two view roots; the light setups, the point light and each stage's lighting; shadows; beginning and ending a frame of the scene (`scene_Begin`, `scene_End`) | `include/node.h`, `include/light.h`, `include/scene.h`, `scene.c`, `scene_frame.c`, `light.c`, `model.c` (`node_UpdateWorldMatrix`) |
| | Models | A fighter's skeleton of nodes, built from the loaded model data and posed from a motion frame; its face, extra textures, tints and outline; the tiny model | `model.c`, `model_face.c`, `model_tiny.c`, `include/model.h` |
| | Renderer | Turns queued nodes into GPU primitives: projection, lighting, depth sorting, subdivision. The inner loops were assembly | `render.c`, `include/render.h`, `include/mesh.h`, `render_asm.c`, `render_skin_asm.c` |
| | Maths | The sine table, rotation matrices in each order of axes, colour conversion | `math.c`, `math_sin.c`, `include/math.h` |
| | Stage | The 32x32 grid of cells: which the camera sees, the tiles and pieces on them, distance fog (`stage.c`); what single stages have of their own (`stage_hooks.c`): props that fall when cut (`stage_props.c`), the surf (`wave.c`, `include/wave.h`), scrolling textures, a backdrop | `stage.c`, `stage_hooks.c`, `stage_props.c`, `wave.c`, `include/stage.h` |
| | Collision | Casts a segment through the stage's cells; reports the hit, its normal and the kind of surface (`enum Surface`) | `collision.c`, `include/collision.h` |
| | Motion | Motion files (the common one and one for each motion set: a weapon class with three stances), finding a move in them (`fighter.c`), the bit-packed frame decoder (`motion_decode.c`), a bit reader and the tracks read with it (`bitstream.c`); bending a decoded pose is with the fighter's code (`fighter_pose.c`). Cutscenes: a camera track and a motion stream for each fighter, played on a stage (`cutscene.c`) | `fighter.c`, `motion_decode.c`, `bitstream.c`, `include/motion.h`, parts of `fighter_pose.c` and `fighter_hit.c`; `cutscene.c`, `include/cutscene.h` |
| | Cloth | The parts of a fighter that hang and swing (skirts, sleeves, long hair): a mesh's vertices as points joined by springs, pinned to a bone, kept out of the ground and the limbs | `cloth.c`, `cloth_model.c`, `include/cloth.h` |
| | Effects | 28 effects drawn as flat quads, started by the fight and run each frame until over: blood, sparks, streaks and flashes, a gun's muzzle flash, dust, splashes, footprints; and the stage's flames | `eff.c`, `eff.h` |
| | Sprites and text | Sprite sheets drawn as cells; the built-in text font | `select.c`, `include/sprite.h`, `font.c`, `include/font.h` |
| Game | Fighter | One of the two fighters: its pad, the move it is in, how the move carries it through the stage, its model's pose, its wounds (`fighter.c`: set-up and the frame; `fighter_pose.c`: aiming the pose, turning, where the body's and the weapons' points are). The two against each other: blade against blade, blade and bullet against body, bodies kept apart, what a hit does (`fighter_hit.c`), with the geometry those tests use (`hit_test.c`). A move's marks, and the sounds, footprints and effects of a fight (`fight_event.c`) | `fighter.c`, `fighter_pose.c`, `fighter_hit.c`, `hit_test.c`, `fight_event.c`, `include/fighter.h`; `fighter_pad.c` (the fighter's pad) |
| | CPU opponent | Plays a fighter by making up its pad: sees threats and openings, walks round the stage's obstacles, guards, and picks attacks (pad scripts from the motion files) by a profile for each character | `cpu.c`, `include/cpu.h` |
| | Loose objects | The weapons out of a fighter's hand: the sub-weapons a fighter carries, throws and picks up, a second sword let go of, a defeated fighter's weapon; they fly, bounce, lie, stick in a fighter or glance off (twelve at most, kinds in `enum ObjectKindId`). Also the handfuls of dirt or water thrown at the other fighter. The numbers weapons go by (`enum WeaponId`, `enum WeaponModel`) are in `weapon.h` | `object.c`, `include/object.h`, `include/weapon.h` |
| | Camera | The fight's side view of both fighters, the first-person view, the views of the winner, the replay's two views (`camera.c`); choosing the frame's view and beginning the scene from it (`camera_BeginFightScene`, in `fight.c`) | `camera.c`, `include/camera.h`, part of `fight.c` |
| | Fight flow | What the next fight is to be and starting a mode (`fight_start.c`); the fight's screens: set-up, a frame, the winner's view, the menu after (`fight.c`); what each mode keeps between fights: the order of opponents against the CPU, chanbara's hundred opponents and best times, a team battle's matches (`fight_modes.c`); the screens that follow: endings, results, the kaishaku (`fight_after.c`); the blade lock (`fighter.c`) | `include/fight.h` and those files |
| | Story | A script of commands for each character: stages (chapters), bands of henchmen, named opponents, cutscenes, interludes played as a guest, the victim; the stage card and the cutscene screen | `story.c`, `include/story.h` |
| | Replay | Records each frame's inputs and events; plays them back | `replay.c` |
| | Overlays | Timer, counters, pause panel, captions, stage card, result tables, the loading picture: drawn over the scene | `hud.c`, `include/hud.h` |
| | Menus | The main menu screen and its pages: the top menu, options, memory card menu, key configuration, training and slash mode's first pages, the message box (`menu_screen.c`, `menu_pages.c`, `include/menu.h`); character select: fighter grid, weapon row, stage pages (`select.c`, `select_data.c`, `include/select.h`); the debug menu (`fight_modes.c`, `fight_start.c`); team battle's select (`select_team.c`); the steps between the screen and its pages (`menu_run.c`); the tables that are not small data (`menu_data.c`); the narration (`narration.c`, `include/narration.h`) and the staff roll (`staff.c`, `include/staff.h`) | `menu_screen.c`, `menu_run.c`, `menu_pages.c`, `fight_start.c`, `menu_data.c`, `select.c`, `select_data.c`, `select_team.c`, `narration.c`, `staff.c` |
| | Boot and main loop | Start-up, the loop and its screens, the pause menu, the settings that pass from menus to the fight | `main.c`, `include/sys.h`, `fight_start.c`, `menu_screen.c` |

## The source files and the original's

The game is 71 files in `src/main/`: 57 of C code, 3 of the authors'
assembly (`render_asm.c`, `math_asm.c`, `render_skin_asm.c`: listings
included from `asm/funcs/`), and 11 of data (nine `*_data.c`, `fight_sdata.c`,
`globals.c`). How far they are the original's files
([`FAKES.md`](FAKES.md), "The zeroed variables" and "The original's source files"; [`STATUS.md`](STATUS.md)):

- Every file of the game is compiled with a small-data threshold of 8
  (`-G8`), as the original was: a variable of eight bytes or fewer is reached
  through `$gp` by the file that defines it and by full address from any
  other, and a file's strings lie ahead of its jump tables. Those two facts,
  with the padding at a file's end, are what mark a boundary between two of
  the original's files. They prove 24 boundaries; 41 of the 59 boundaries
  between our code files have no mark and are where the subject changes.
- A variable with no initial value that one file alone uses is `static`
  there where the original's layout shows it was: the file's own storage
  holds it, in the order the file declares it. The engine's files
  (`font.c` to `menu_pages.c` in link order) keep their state that way; the
  fight, the menus and the modes keep theirs in variables any file can
  reach, which the original's linker placed in an order made from their
  names. Those names are not known, so `bb2.ld` lists such variables one by
  one (300 lines).
- The small shared variables are in `globals.c` (probably `main.c`'s in the
  original). The data files hold tables that have to stay apart from the code
  that reads them; `fight_data.c` is several of the original's files' data
  run together.
- The bytes cannot show whether a function was `static` in the original.
  In the source, a function or a table that only its own file uses is
  `static` there and in no header ([`STYLE.md`](STYLE.md), rule 10).

## Where the big data lives

| What | Where | Notes |
|---|---|---|
| Program: constants, code, data, zeroed data | `0x80010000` up | Ends at `0x801078E0`. Must end below `0x8010DB00` (`sys_InitGame` prints the limit), so a changed build has 0x6220 bytes to grow by; `make shift` reports the figure |
| The arena | `g_arena`, in the program's zeroed data | 0x45000 bytes; models and stage data |
| Common sound bank | `0x8010DB00` | At most 0xD00 bytes |
| Effect textures and tables | `0x8010E800` | At most 0xA000 bytes |
| Movie work area | `0x80118000` | The overlay's, while the opening movie plays. Its stack is the first 0x800 bytes of the game's shared buffer (`memcard_GetSharedBuffer`) |
| Load area | `0x80118800` up | In a fight: two decoded-frame buffers, the common motion file (`0x80120000`), one motion file per fighter (`0x80148800`, `0x80190800`). In a 2D screen: the screen's resource file, then its primitive pools. `0x80190800` is also where files are staged before being copied or uploaded |
| Primitive buffers | `0x801D8800`, `0x801EBC00` | 0x13400 bytes each, one per frame parity. The movie overlay loads over them |
| Stack | below `0x801FFF00` | |
| Display buffers | `g_disp_db[2]` | Draw and display environment and a 0x1008-entry ordering table each |
| Fighters | `g_fighters[2]` | `Fighter`, 0x44C bytes each |
| Scratchpad | `0x1F800000`, 1 KB | Shared by turns: the fight (`FightScratch`), the effects (`EffScratch`), the collision cast, the renderer |

## Vocabulary

Names are `subsystem_VerbNoun` for functions and `g_subsystem_noun` for
globals. This table is the list of subsystem prefixes, with the words and
name patterns each subsystem uses.

| Prefix | Means | Notes |
|---|---|---|
| `sys_` | Boot, the main loop's own state, panic, running the overlay | `pcdrv_` is the two development-host file loaders |
| `disp_` | Display buffers, video mode, fades, full-screen images, grayscale | `disp_Blank`, `disp_Show`, `disp_Reset`. The frame's ordering table is `g_disp_ot`, its primitive cursor `g_disp_prim_cursor`. The link cable's picture stayed with the menu (`menu_ShowLinkPicture`): it uses the menu's memory |
| `pad_` | Pads | |
| `cdrom_` | CD files and CD audio | |
| `snd_` | Sound | |
| `memcard_` | Memory card | |
| `link_` | Link cable | |
| `arena_` | The block allocator only: open, close, find, shift blocks | |
| `ndata_` | The NDATA archive: its directory and reading entries | |
| `resource_` | Which entries a fight needs and getting them loaded | Not a second word for arena or archive. `reloc_` stays for the generic "turn file offsets into pointers" helpers |
| `tex_` | Textures in VRAM: TIMs, CLUTs, tints, texture scrolling, re-basing texture coordinates in loaded primitives | `tex_LoadGroup`, `tex_TintClut`, `tex_QueueScroll`, `tex_Shift*` |
| `node_` | Scene nodes: transforms, parents, the view roots | |
| `weapon_` | Queueing the weapon models to be drawn: in a hand, or worn | `weapon_Queue`, `weapon_QueueWorn` (`include/weapon.h`) |
| `light_` | Light setups | `light_CalcMatrix` works out a setup's light matrix |
| `model_` | A fighter's model: loading, building the skeleton, per-frame update, face, tints, the tiny model | A "slot" is the index of a model |
| `scene_` | A frame of the 3D scene: `scene_Begin`, `scene_End`, the view squashed to half height |  |
| `render_` | The 3D renderer: the draw queue, projection, the primitive emitters | The emitters are `render_Emit` + the variant + Sony's letters for the primitive (`render_EmitTwoSidedGT4`); the walks are `render_Draw` + what is drawn (`render_DrawQueue`, `render_DrawStageTiles`, `render_DrawCloth`, `render_DrawSkinnedMesh`); `gte_` stays for the bare GTE helpers |
| `stage_` | The stage grid: cells, visibility, fog, the stages' own hooks | `wave_` is the water surface |
| `collision_` | Segment casts against the stage | |
| `motion_` | Motion files, frame decoding, moves and stances | A fighter has three **stances** (`Fighter.stance`), of kinds 1 to 5; a **motion set** is a weapon class with three stance kinds, and names a file. The frames at which things happen in a motion are its **marks** (`enum MoveMark`) |
| `cutscene_` | Playback of recorded motion and camera | The story's cutscenes are four bytes of its script (`STORY_CUTSCENE_...`). The camera is a **track** (`bitstream_OpenTrack`), a fighter's changing face a **face track** |
| `cloth_` | Cloth | The only soft bodies are the fighters' clothes and hair |
| `eff_` | Effects | |
| `sprite_` | Sprite sheets, the resource files 2D screens load and the primitive pools they draw from | `sprite_Draw`, `sprite_DrawScaled`, `sprite_DrawRotated`, `sprite_GetTPage`; `sprite_LoadFile`, `sprite_RelocateFile`, `sprite_RelocateList`, `sprite_InitPools` |
| `font_` | The built-in text font | |
| `fighter_` | Anything about **one** fighter: takes a `Fighter` or a fighter index | `fighter_Init`, `fighter_Update`, `fighter_StartMove`, `fighter_TakeHit`, `fighter_PlacePoints`. The tests between the two are `fight_` (`fight_UpdateFighterPair`, `fight_TestBlades`, `fight_TestBladesOnBodies`, `fight_PushBodiesApart`), and what happens in a fight is raised with `fight_RaiseEvent` (`enum FightEvent`) |
| `cpu_` | The CPU opponent | `cpu_Init`, `cpu_BuildPad`, `cpu_Decide`, `cpu_StepScript`; the CPU's members of `Fighter` are `cpu_...` |
| `object_` | Loose objects (`LooseObject`) and thrown dirt (`ThrownDirt`) | A fighter's **sub-weapons** (`Fighter.sub_weapons`, `sub_weapon_kind`) are object kinds; an object is **launched**, **sticks in** a bone or **glances off**, comes to **rest**; `object_ThrowSubWeapon`, `object_PickUp`, `object_DropWeapon`, `object_Update`, `object_HitFighters` |
| `camera_` | Cameras | `g_camera_main`, `g_camera_second`, `enum CameraMode` |
| `fight_` | The match around the two fighters: set-up, the interactions between them, the modes, results, and the screens that follow a fight | `fighter_` vs `fight_`: one fighter vs both or neither. The modes' variables are `g_team_`, `g_chanbara_`, `g_vscpu_`; the kaishaku's and the endings' `g_kaishaku_`, `g_ending_` |
| `story_` | Story mode's script and what it sets up | `story_RunScript`, `enum StoryCommand`, `g_story_chapter`, `g_story_interlude` |
| `replay_` | Replay | |
| `hud_` | What is drawn over the scene: timer, counters, pause panel, stage card, result tables | |
| `menu_` | The main menu: the screen, its pages, the message box, the debug menu, the settings (`GameSettings`, `menu_GetSettings`) | A page has `menu_Draw<Page>`, `menu_Handle<Page>` (the pad) and `menu_Update<Page>` (a frame); `menu_Run<Page>` is the step `menu_screen.c` calls |
| `select_` | Character and weapon select (SEL.BIN, D_SEL.BIN) | `select_Load`, `select_Update`, a `select_<Step>Step` per step. Team battle's select (`select_team.c`) has `Team` in every name: `select_LoadTeam`, `select_UpdateTeam`, and for each step `select_Draw<Step>` and `select_Handle<Step>` (`select_HandleTeamFighters`); its constants are `TEAM_` |
| `narration_`, `staff_` | The narration screen (NAR.BIN) and the staff roll (STAFF.BIN) | `narration_Load`, `narration_Update`; `staff_LoadRoll`, `staff_UpdateRoll` under the screen's `staff_Init`, `staff_Update` |
| `math_`, `gte_`, `rng_`, `bitstream_`, `mem_` | Small helpers | The sine table is `g_math_sin_table`. `reloc_` is the helpers that turn a loaded file's offsets into pointers. The plain geometry of `hit_test.c` is `math_` too (`math_PointInTriangleXZ`, `math_SegmentMeetsCircle`) |
| `hit_` | The hit tests that work in the fight's scratch (`hit_test.c`) | `hit_FrameTriangle`, `hit_SphereMeetsTriangle`, `hit_TrianglesMeet`; declared with the scratch in `include/fighter.h` |

`gpu_` is not used: it is Sony's word. The helpers that were
written in assembly (`math_asm.c`) are `math_` where they work something out
(`math_MulMatrix`, `math_Transform`, `math_Sqrt`, `math_LerpRotation`) and
`gte_` where they only load the GTE or read it back (`gte_LoadMatrix`,
`gte_ReadFarColor`, `gte_NormalizeIR`). Every function of the game has one
of these prefixes but `main`; an empty function is named for the table slot
or callback it fills.

Other words:

- **screen**: one entry (or init/update pair) of the main-loop table.
- **fighter**: one of the two characters fighting; `Fighter`. "Player" means
  a human at a pad (player 1, player 2), which a fighter may or may not have.
- **move**: one entry of a character's move script (`MoveScript`): an
  animation with its timing windows and what it can lead to. **Motion** is
  the animation data itself.
- **model**: a fighter's drawable body. **Node**: one transform in the scene
  graph; a model's bones are nodes. **Mesh**: what a node draws, one entry of
  a loaded file's pointer table (a **block**).
- **side view**: the fight's usual view, from beside the two fighters
  (`g_camera_main`).
- **first-person view**: the fight seen through one fighter's eyes
  (`g_camera_second`; `g_fight_viewer` is the fighter). That fighter's model is drawn as outlines, and again as
  a small figure in the corner (the **tiny model**).
- **prop**: a thing on a stage that can be cut and falls.
- **stage**: the place fought in, a grid of **cells** 2000 units square.
  There are 38 (`enum StageIndex`, `include/stage.h`). Stages 0 to 31 are
  the ones a fight can be on and have the names the game's stage cards
  show (`STAGE_MAIN_TATARA_SHRINE` to `STAGE_SHAINTO_CEMETERY`); 32 to 36
  are the modes' own (`STAGE_TEAM_1`, `STAGE_TEAM_2`, `STAGE_CHANBARA`,
  `STAGE_PRACTICE_1`, `STAGE_PRACTICE_2`); stage 37 has no name: link mode
  uses it in place of the castle rooftop.
- **sprite sheet**, **cell**: a 2D image set and one rectangle of it.
- **page**: one of the main menu's screens-within-the-screen (top menu,
  options, key configuration...). **nudge**: the short push a menu gives the
  thing under the cursor before the cursor moves.
- **side**: one of the two fighters of a fight as character select picks
  them; a **pick** is a side's fighter and weapon. **step**: a stage of
  character select (fighter, weapon, crossed weapons, stage pages).
  **clan**: which of the two groups a fighter belongs to; it decides the
  weapons on offer. In team battle a **team** is one clan's fighters, one
  for each **match**, each in a **place** of the team; the **rules** are
  the number of matches, the points that win one and the time limit.
- **mode**: one of the seven ways to play (`enum FightMode`), by the game's
  own names: STORY, VSCPU, PRACTICE, CHANBARA (the menu's Slash), VS, TEAM
  (Group Battle), NETWORK (Link).
- **chapter**: a stage of the story, opened by the **stage card** on which
  the **route** so far is marked. **henchmen**: the clan's nameless
  opponents, who come in a band, one after another. **interlude**: a part
  of the story played as another character, the **guest**. **victim**: the
  unarmed last opponent of some stories.
- **blade lock**: the two fighters pushing blade against blade. **paired
  move**: one motion for both fighters, begun when one catches the other.
- **warning**, **point**: a team battle's match is scored like a kendo bout:
  a kill is a point, stepping off the ground a warning (the screen's word),
  two warnings a point to the other side.
- **kaishaku**: the cutscene in which a fighter that has given up is
  finished by the other.
- **event**: something that happens to a fighter (`Fighter.pending_event`,
  `enum MoveEvent`): its move has a table of the moves that answer each one.
  Not to be confused with the fight's events (`enum FightEvent`), which are
  what is heard and seen, or a motion's frame events (its sounds).
- **body point**: one of the 22 spheres of a fighter that a blade or a
  bullet can hit (`BodyPoint`, `enum BodyPointId`); a blow that also reaches
  a point's **core** kills. **wound**: what a blow that does not kill
  leaves: the **off arm** is lost, the **sword arm** and the legs slow the
  fighter down.
- **sweep**: where a blade has been since last frame, which is what is
  tested for a hit. **blade lock**: the two blades pressed together
  (`MOVE_IS_LOCK`), decided by button presses. **paired move**: one fighter
  catches the other and both play one motion. **clash**: two attacks that
  meet.
- **roster**: the list fighters are picked from (`Fighter.roster_id`,
  `enum RosterId`, 33 entries); several entries can be one **character**
  (`Fighter.character`, `enum CharacterId`, 27), the row of the
  per-character tables. Twenty characters have the name the game's
  pictures give them (`CHARACTER_KANNUKI` to `CHARACTER_MATSUMUSHI` for the
  Narukagami, `CHARACTER_GENGORO` to `CHARACTER_ISOHACHI` for the Shainto);
  the six that start the story have a second roster entry for it
  (`ROSTER_KANNUKI_STORY` and the like). Seven characters have no name on
  the disc and are named for what they do or where they come:
  `CHARACTER_VANISHER`, `CHARACTER_SIDEARM_1` and `CHARACTER_SIDEARM_2`
  (the two clans' ninja), `CHARACTER_ARMORED`, `CHARACTER_VICTIM`, and
  `CHARACTER_LAST_1` and `CHARACTER_LAST_2` (the two crests beside the
  select grid). The fighters of the select screen are `enum SelectFighter`
  (`include/select.h`).
- **profile**, **level**: how a CPU fighter plays, and how well
  (`CpuProfile`, `Fighter.cpu_level`). **pad script**: a short sequence of
  presses the CPU plays; its attacks are pad scripts.
- **loose object**: a weapon that has left a fighter's hand. **sub-weapon**:
  what a fighter carries besides its weapon and can throw (the game's own
  word: the move is `SUBWEP`); for five characters it is a second sword,
  held in the off hand in the two-sword stance. **weapon model**: one of
  the 58 models of things held or thrown (`enum WeaponModel`).

## Core types

Each is in its subsystem's header. The right-hand column is the name the
type had before the readability pass, for reading old notes (the full
list is in the private archive).

| Type | Is | Was |
|---|---|---|
| `Fighter` | One of the two fighters | `Unk80101EC8Record` |
| `Vec3` | A world point or vector, three `s32` | `Vec3i32`, `Unk80107850Rec` and three effect records |
| `FightScratch` | The scratchpad during a fight: weapon and body points, then the hit tests' scratch (`WeaponHitScratch`, `ObjectCastScratch`) | `ScrPad`, `Unk1F8002B8Rec`... |
| `EffScratch`, `EffRequest`, `EffWork` | The scratchpad as the effects use it; a request to start or run an effect | `Unk1F800000Rec`..., `EffCommand` |
| `SegmentCast`, `GridCell` | A stage-collision cast's work area; a stage cell | `Work_80053E9C`, `Cell_80052D00` |
| `SceneNode` | A scene-graph node (`include/node.h`) | `Unk80101DF0Record` |
| `Model`, `ModelNode`, `ModelBone` | A fighter's model, its nodes, a bone of the shared skeleton (`include/model.h`) | `Unk80045878Obj`, `Unk80045878Node` |
| `StageTile`, `StageNode`, `StageProp`, `StageProps` | A cell's piece of ground, the stage's other pieces, and the props that fall when cut (`include/stage.h`) | `Unk800A4750Rec`, `Unk800A6690Rec`, `Unk800A9CF8Entry`, `Unk800A9CF8Rec` |
| `LightSetup`, `LightDir` | A lighting setup and one of its lights (`include/light.h`) | `Unk800F62E0Rec` |
| `CameraView` | A view of the camera's; what the player's zoom adds to it, `g_camera_offset`, is one too (`include/camera.h`) | `Rec44`, `Unk800FF5C8Rec` |
| `LooseObject`, `LooseObjectKind`, `ThrownDirt` | A thrown or dropped weapon, what its kind does, a handful of dirt in the air (`include/object.h`) | `Obj80106A78`, `Tbl8008E194`, `Unk80104E88Rec` |
| `SpriteSheet`, `SpriteCell`, `SpriteDraw` | A sprite sheet, a cell, a request to draw one (`include/sprite.h`) | `Unk8009B0E0Record`, `Unk8009B400Record`, `Unk8007352CRec` |
| `PrimPool`, `PrimCursors`, `MenuDrawCtx`, `MenuCtx` | A 2D screen's primitive memory and draw context (`include/sprite.h`, `select.h`, `menu.h`) | `Unk8006E49CRec`, `Unk800788B0Rec`, `Unk8006EACCRec` |
| `ScreenPoint`, `ScreenSize` | A screen position; a size in pixels | four identical records |
| `SpriteFileHead`, `SoundVab` | The start of a 2D screen's resource file; its sound bank | `Unk8006E950Head`, `Unk8005C2A8Pack` |
| `GameSettings`, `RoundPick`, `BestTime` | What the menus decide and the fight reads (`include/menu.h`) | `Unk8009BD24Block`... |
| `MenuFile`, `SelectFile` | The resource files of the main menu (MOD.BIN) and of character select (SEL*.BIN) | `Unk8006919CRec`, `Unk8006EA28Rec` |
| `TeamSelectFile`, `TeamSelect`, `NarrationFile`, `StaffFile` | The resource files of team battle's select (D_SEL.BIN), the narration (NAR.BIN) and the staff roll (STAFF.BIN), and the team select's work area | `Unk80076FF8Rec`, `SelWork`, `Ctx77D94`, `Unk80078824Rec` |
| `ClockTime` | Minutes, seconds, hundredths for display | `Unk8001CD68Rec` |
| `BodyPoint`, `WeaponHitScratch` | A point of a fighter's body that can be hit; the hit tests' scratch (`include/fighter.h`) | `BoneHitRec` |
| `CpuProfile`, `CpuStoryLimits`, `CpuRoute`, `NavChain`, `NavSet` | How a CPU fighter plays, a story boss's plan, the CPU's route and a stage's obstacles (`include/cpu.h`) | `Unk80099D88Rec`, `Unk8009A8C8Entry`, `NavVertexChain`, `NavVertexChainSet` |

Sony's `SVECTOR` is used where the game had an identical type of its own. Types that
kept their names: `PadState`, `MoveScript`, `MotionFrame`, `CpuRoute`,
`TexRec`, `GpuDb`, `SaveRecord`.
