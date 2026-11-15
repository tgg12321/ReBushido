# Project status

- **Build:** `decomp` reproduces the original executable (SHA1
  `62efab4f73f992798c43e8c730aa43baa10bb4fa`): `wsl bash tools/build.sh check`.
  The movie overlay, a separate program, is rebuilt too (`disc/STR/MOVOVL.EXE`, SHA1
  `a1307dbebefca0b057e02509207d00f6225e13e4`): `wsl bash tools/build.sh movovl`.
- **Decompilation:** complete. 1,484 functions: 1,304 in C and 180 in assembly because the original's
  were (71 of the game's own, hand-written; 109 of Sony's and SN Systems': 67 listings, 40 jumps into
  the BIOS, 2 whole-function blocks). The game is 1,024 of them (953 in C), Sony's libraries 460 (351 in C).
- **Three passes went over the finished decompilation:**
  - the readability pass: each subsystem read whole, given its own files and header, and
    named for what it does in the game.
  - the accuracy pass: every line asked whether it is shown to be the authors' or only
    compiles to the same bytes. What it settled is in [`FAKES.md`](FAKES.md); what it did not is
    ["What is still a stand-in"](#what-is-still-a-stand-in) below.
  - the polish pass: the source held to the checklist of [`STYLE.md`](STYLE.md) (names,
    numbers, tables, declarations, comments), which `python tools/lint.py` counts: 144 findings are
    left of 3,753. The stages and the fighters got their names from the game's own pictures.
- **After the passes** the documents and tools that only served the work (the plan with each step's notes,
  the listings of the functions that are C, the emulator scripts) were taken out. They are
  kept in a private archive ([`HISTORY.md`](HISTORY.md)).
- **Checks:** seven, and on the `main` branch three more for the ReBushido build
  ([`REBUSHIDO.md`](REBUSHIDO.md)), run by GitHub Actions on every push and by `wsl bash tools/build.sh checks`
  locally ([`BUILD.md`](../BUILD.md), "What is checked"). What they show and what they do not is
  under ["What was tested and what was not"](#what-was-tested-and-what-was-not).
- **Names:** every name of the game's is ours; none of the original's was recovered. Many are a
  reading of the code that nobody has seen or heard confirmed in the game
  (["Open questions"](#open-questions)).
- **By subsystem** (standard: [`STYLE.md`](STYLE.md)):

| Subsystem | Files | State |
|---|---|---|
| Memory card | `memcard.c`, `memcard_save.c`, `include/memcard.h` | done |
| Replay | `replay.c`, `include/replay.h` | done |
| Link cable | `link.c`, `include/link.h` | done |
| Domain model | `docs/ARCHITECTURE.md` (main loop, subsystems, vocabulary), the core types in the subsystems' headers | done |
| CD and streaming | `cdrom.c`, `include/cdrom.h` | done |
| Sound | `snd.c`, `include/snd.h`, the `snd_` functions of `fight.c`, `fight_event.c` (a stage's props and ambience), `main.c`, `resource_weapon.c` | done |
| Boot, main loop, pad | `main.c`, `pad.c`, `fight_start.c`, `menu_screen.c`, `include/sys.h`, `disp.h`, `pad.h`, `menu.h`; every screen handler named | done |
| Memory arena, NDATA archive, resources | `arena.c`, `ndata.c`, `resource.c`, `resource_weapon.c`, the surf (`wave.c`); `include/arena.h`, `ndata.h`, `resource.h`, `wave.h` | done |
| Scene graph: nodes, models, lights | `model.c`, `model_face.c`, `light.c`, `scene.c`, `stage_props.c`, `math.c`, `math_sin.c`, `render.c` (the renderer's C side: draw queue, mesh tables, `tex_Shift`), the shadows (`scene_frame.c`), stage hooks (`stage_hooks.c`) and tiny model (`model_tiny.c`), the weapon nodes in `resource_weapon.c`; `include/node.h`, `model.h`, `light.h`, `math.h`, `render.h`, `stage.h` | done |
| Collision | `collision.c`, `include/collision.h` (`enum Surface`); starting a cutscene: `cutscene.c`, `include/cutscene.h` | done |
| Cloth (the soft bodies) | `cloth.c`, `cloth_model.c`, `include/cloth.h` | done |
| Effects | `eff.c`, `include/eff.h` (`enum EffectId`; what the fight asks for is `eff_StartBlood`, `eff_StartClashSpark`...) | done |
| Rendering core | `scene_frame.c` (a frame of the scene), `stage.c` (cells, fog), `stage_hooks.c`, `tex.c`, `tex_scroll.c`, `select.c`, `font.c`, `hud.c` (what is drawn over the scene), `render.c`'s handler tables; `include/scene.h`, `stage.h`, `tex.h`, `sprite.h`, `font.h`, `hud.h`, `mesh.h` | done |
| Menus: main menu and character select | `menu_screen.c`, `menu_pages.c` (the top menu, options, memory card menu, key configuration, training and slash mode's pages, the message box), `fight_start.c`, `select.c`, `select_data.c`, the pages' steps in `menu_run.c`; `include/menu.h`, `select.h`, the 2D screens' file and pool types in `sprite.h` | done |
| Menus: team battle's select, narration, staff roll | `select_team.c` (rules, clan, fighters, weapons; declared in `select.h`), `menu_data.c` (its tables, the settings, the top menu's rows), `narration.c`, `staff.c`; `include/narration.h`, `staff.h` | done |
| Camera | `camera.c` (the side view, the first-person view, the winner's and the replay's views), `include/camera.h`; the three functions that begin a frame's scene, in `fight.c` | done |
| Motion and cutscenes | `fighter.c` (the motion files, moves, stances), `motion_decode.c` (the frame stream), `bitstream.c`, `cutscene.c` (starting a cutscene and playing it), `include/motion.h`, `cutscene.h`; the functions that bend a pose, in `fighter_pose.c`, `fighter.c` and `fighter_hit.c`; `docs/formats/BBM.md` | done |
| Loose objects and weapons | `object.c` (sub-weapons thrown, dropped and picked up, a defeated fighter's weapon, thrown dirt), `include/object.h` (`enum ObjectKindId`), `include/weapon.h` (`enum WeaponId`, `enum WeaponModel`) | done |
| Fighter | `fighter.c` (set-up and the frame), `fighter_pose.c` (aiming the pose, turning, the body's points), `fighter_hit.c` (the two fighters against each other: blades, bullets, bodies, what a hit does), `hit_test.c` (the hit tests' geometry), `fight_event.c` (a move's marks and sounds, the fight's events), `fighter_pad.c` (the fighter's pad); `include/fighter.h` (`Fighter`, `enum MoveEvent`, `enum RosterId`), the move kinds in `include/motion.h` | done |
| CPU opponent | `cpu.c`, `include/cpu.h` (`CpuProfile`, `enum CpuFlag`, the pad scripts) | done |
| Fight flow | `fight.c` (the fight's screens), `fight_modes.c` (opponents against the CPU, chanbara, team battle), `fight_after.c` (endings, results, the kaishaku), `fighter.c` (the blade lock), `fight_start.c`, `fight_event.c`; `include/fight.h` (`FightConfig`, `enum FightMode`, `enum FightEvent`, `enum JudgeSound`) | done |
| Story | `story.c` (the script reader, the scripts written as commands, the stage card and cutscene screens), `include/story.h` (`enum StoryCommand`, the interlude and the victim) | done |
| Hand-written assembly | `render_asm.c` (the renderer's inner loops), `render_skin_asm.c` (lighting a fighter's body, the outline colour), `math_asm.c` (memory move, roots, matrix helpers, the collision test's two helpers); declared in `include/render.h`, `math.h` and `collision.h`; the listings in `asm/funcs/` commented. Audited function by function: all 71 are the authors' assembly, none the compiler's, and each says by what above its `INCLUDE_ASM` | done |
| Data files | `fight_data.c`, `stage_data.c`, `resource_data.c`, `cpu_data.c`, `snd_data.c`, `hud_data.c`, `eff_data.c`, `select_data.c`, `menu_data.c`: tables that have to stay apart from the code that reads them, each declared in its owner's header; the read-only tables and the screen table are in their owners' files | done |
| Headers | `include/bb2.h` includes Sony's libraries and every subsystem's header and declares nothing; `include/common.h` has the basic types and `Vec3`; `game.h` and `bb2_const.h` are gone; no `.c` file declares another C file's object | done |
| Sony SDK | `src/main/psxsdk/` (205 files, one per PsyQ module, 11 libraries), `include/psxsdk/` (the headers follow Sony's 4.0 files: `kernel.h`, `sys/file.h`, `libapi.h`, `libc.h`, `libcd.h`, `libcomb.h`, `libetc.h`, `libgpu.h`, `libgte.h`, `libsn.h`, `libsnd.h`, `libspu.h`, and Sony's GTE macro files as shipped). Its assembly audited module by module: 88 modules are Sony's or SN Systems' assembly and say by what, 117 are C; no C function has inline assembly | done |
| Accuracy sweep | the whole tree: 137 `FAKE` notes left of 293 (one line each, each sorted by kind in [`FAKES.md`](FAKES.md)), 47 `goto` of 214 (each sorted there too), 897 casts of 1,626 (counted by kind there too), no `volatile` in game code, every file built with warnings on but `libspu/spu.c`; the numbers are below | done |
| Symbol tables and listings | `asm/funcs/` (the 139 listings the source includes: 138 functions and the outline's four template words), `config/symbol_addrs.txt` (every symbol, written from the ELF, the functions marked), `config/named_syms.txt` (the three addresses the linker is told); `tools/listings.py` checks the listings against the build and writes the listing of any other function on demand, `tools/symbol_addrs.py` writes the table | done |
| Movie overlay | `movovl/` (`src/main.c`, `src/movie.c`, `include/movie.h`; of its 59 Sony library modules 43 are built from the main build's source and 14 are C of its own in `sdk/`; `DecDCTvlc` and a BIOS stub are Sony's assembly; one library function, `dma_execute`, is a listing that would not match), built and checked apart from the main executable | done |
| Tools, documents, repository | `tools/` (what each is for: `tools/README.md`), `README.md`, `BUILD.md`, `docs/`, `.github/workflows/build.yml` (builds and checks both executables on every push) | done |
| Variables and loose data | every variable is defined in C: small shared ones in `globals.c`, the rest in their owners' files, Sony's in their modules; `fight_sdata.c`; a file's own (`static`) are placed by the file, the others are listed in `bb2.ld` in the original linker's order, which is by name (the sum of its characters) with alignment by size: both rules found and tested ([`FAKES.md`](FAKES.md), "The zeroed variables"); `asm/data/` is gone | done |
| Names checked in the running game | scripted emulator runs, in two rounds, confirmed some names and corrected others (below, "What was tested and what was not") | done; what needs a person playing is under "Open questions" |
| Last read-through | the whole tree: every game function has its subsystem's prefix (the sine table is `g_math_sin_table`), every `unkNN` member and `D_` name says what is known of it, headers declare arrays with their counts, `make shift` passes, the documents describe the tree as it is | done |
| The original's source files | 60 files of code (57 of C, 3 of assembly) and 11 of data, cut where the bytes mark a boundary and by subject elsewhere; every file compiled `-G8`; a file's own variables `static` and placed by the file ([`FAKES.md`](FAKES.md), "The zeroed variables" and "The original's source files") | done; what the bytes do not show is below |
| Recount | `python tools/count.py` counts everything this page reports, and checks the `FAKE` notes and `goto`s of the source against [`FAKES.md`](FAKES.md) | done |
| Polish | the whole tree against the checklist of [`STYLE.md`](STYLE.md): names, numbers, tables, declarations, comments, one spelling for one thing; `python tools/lint.py` counts it | done; 144 findings stay, each with its reason |

## The counts

Counted by `python tools/count.py` at the end of the readability pass, at the end of the accuracy pass
and on this tree, the same way each time. "Game" is `src/main/*.c` and `include/*.h`, "SDK" is
`src/main/psxsdk/` and `include/psxsdk/`, "overlay" is `movovl/`.

| | At 1.0 | After the accuracy pass | Now |
|---|---|---|---|
| Functions in C / in assembly | 1,306 / 178 | 1,304 / 180 | 1,304 / 180 |
| Listings the source includes: the game's, Sony's, the overlay's | 71 and one of data, 66, 0 | 71 and one of data, 67, 1 | the same |
| `FAKE` notes: game, SDK, overlay | 223: 199, 23, 1 | 138: 125, 11, 2 | 137: 124, 11, 2 |
| `goto`: game, SDK | 84: 59, 25 | 47: 22, 25 | 47: 22, 25 |
| Casts in the game's files | 1,626 | 947 | 897 |
| `bb2.ld`: lines that place one zeroed variable; `. += n` among them; `ALIGN` among them | 384; 23; 40 | 300; 21; 5 | 300; 21; 5 |
| Files of the game: code, assembly, data | 62, 4, 11 | 57, 3, 11 | 57, 3, 11 |
| Assembly files of the overlay | 65 | 6 | 6 |
| `D_` names (uses) | 30 (48) | 0 | 0 |
| Members named `unkNN` / `unk_NN`, declared | 54 / 41 | 57 / 39 | 18 / 20 |
| Locals named `unused` | 17 | 21 | 26 |
| The checklist's findings (`python tools/lint.py`) | - | 3,753 | 144 |
| Hex literals in the game's files | 14,479 | 14,479 | 7,467 |
| `extern` lines in the game's `.c` files | 128 | 107 | 22, all labels of listings |
| The game's C functions defined `static` (of 953) | 1 | 1 | 338 |

The polish pass took 39 `unkNN` and 19 `unk_NN` members out: alignment the compiler leaves anyway,
bytes that only fill up (now `padNN`), and two that could be named. The 144 findings that stay are 47
prototypes with `()` (the renderer's routines that only the assembly calls, and seven functions
called with other arguments than they are defined with), the 39 `unk` members and their uses, 39
numbers nobody understands (sounds nobody has listened to, one pose's angles) or that a comment
explains where the lint does not look, 9 old-style definitions and 3 return types the bytes need,
and 7 locals of the CPU that the bytes give many jobs (`python tools/lint.py --by-file`).

The `FAKE` notes now, by kind ([`FAKES.md`](FAKES.md) has a row for each, and the script checks the rows
against the source):

| Kind | Game | SDK | Overlay | All |
|---|---|---|---|---|
| A: the code says something untrue | 14 | 2 | 0 | 16 |
| B: an odd local or expression that only steers the compiler | 94 | 7 | 0 | 101 |
| C: control flow shaped to match | 11 | 2 | 1 | 14 |
| on a `goto` that is ours | 5 | 0 | 0 | 5 |
| a listing that would not match in C | 0 | 0 | 1 | 1 |

The `goto`s by verdict: 6 a jump the authors could well have written, 7 probably the authors' (the
bytes have one copy of the code and a jump to it), 9 ours, 25 Sony's.

## What is still a stand-in

Everything here builds the original's bytes. This is what is not shown to be the authors' own, class by
class, hardest first inside each. "Not shown" does not mean wrong.

### `FAKE` notes: 137

Each marks a line shaped to give the bytes. Every one has been built in its plain form at least once;
what each plain form gave is in [`FAKES.md`](FAKES.md).

- **Kind B, 101: the registers.** In about 40 the plain form gives the same instructions with values in
  other registers or the saved registers stored in another order; in about 25 it is a few instructions
  shorter or longer; the rest are the two groups below. The stand-in is nearly always a local used for
  two things, a copy of a parameter, or a constant held in a local. GCC 2.7.2 gives a variable its
  register by where it is first used and how much it is used, so the question in each is which variables
  the authors declared and in what order, and the bytes hold many answers. The hardest: `cpu_Decide`
  (four shared locals standing for some thirty, in 2,991 instructions; four notes), `select_MoveWeaponCursors`
  (4), `fighter_Update`, `model_Update`, `link_ExchangePad` (3), `menu_pages.c` (7 over six functions).
  To settle them: the original's source or a build of it with symbols, or a long search by machine;
  decomp-permuter was run on two functions for about nine minutes each and found nothing.
- **The root of an unsigned number, 5 notes of the 101 and seven places** (`fight_HoldDistance`,
  `fight_HoldLockDistance`, `snd_PlayAt`, `camera_FindHidingCell`, `cloth_SolveSprings`; `math_TableSqrt`
  has the same line). `shift = 0x16 - (lz & ~1)` written plainly loads the count and
  the -2 into each other's registers. About 35 spellings, six inline functions and one permuter run
  left one register different. The seven are surely one macro or inline function of the authors'; its
  form is not found. Beside it the table's root is spelled three ways in our source (`math_TableSqrt`
  in seven places, `MATH_SQRT` in six, written out in ten): the authors had one.
- **`hud_DrawLoadingNoise`, 8 notes.** The whole function written plainly is 178 instructions for 176.
  Its two temporaries and two constants in locals are all stand-ins; no part of it is known to be right.
- **Kind C, 14: empty `do {} while (0)`s, empty blocks and a test made twice** (`menu_Load` 2,
  `fighter_StartMove` 2, `memcard_SaveScreen`, `sprintf`, `PutDispEnv`, the overlay's
  `data_ready_callback`...). Without them two registers change places or a few instructions go. An
  empty `do {} while (0)` is usually what is left of a macro or an inline function; which, is not known.
- **Kind A, 16: the code says something untrue.** The link packets' second words, which are variables
  of their own in our source and one 8-byte object in the layout (`link.c`, `globals.c`); the top menu's
  locals written as one structure; `cloth.c`'s three count arrays, whose size is known only to within
  8 bytes; three local initialisers and `fight_start.c`'s short names, which the original has in small data
  (our compiler stage runs without small data and `maspsx` moves things afterwards, so C cannot say
  it: four notes that a compiler with a working `-G8` would remove); `cutscene.c`'s padded message; `AddPrim` called as if it returned
  `int`; two in `hud.c`; `camera_SwingToFighter`'s 12-byte copy (probably the authors'); Sony's
  `patch0` and `sprintf`.
- **On a `goto`, 5 notes: four loops and one test** (`cpu_Decide`'s pick of a footwork, `wave_Init`,
  `model_BlendPoses`, `model_SetOutline`; `math_SegmentMeetsCircle`'s test of z). Each loop was built
  as `for`, `do`, `while` and `for (;;)`, `wave_Init` in 45 forms: the instruction count is right and
  the registers are not, because the original's compiler did not see a loop there. Nobody writes four
  loops with `goto`; the loop statement that hides from the compiler was not found.
- **`dma_execute`** (the overlay, Sony's libcd): compiled in the original, a listing here. In C it came
  to about 70 of 100 instructions differing. The other 37 functions of the overlay's own Sony modules
  are C.

### `goto`: 47

25 are Sony's (17 of them the labels of the BSD `printf`); each was built plainly once and none matches.
Of the game's 22: 6 are jumps a programmer writes; 7 are kept because the bytes have one copy of some
code reached from two places and the compiler would not merge a second copy (probably the authors'
`goto`, unless an arrangement of the tests does it that was not found: `eff_DrawFlames`, `eff_DrawFlash`,
`select_HandleTeamRules`, `fighter_ResolveBladeContact`); 9 are ours (above).

### Verdicts that are "probably"

- **`SetBackColor` and `SetFarColor`** are listings, taken as Sony's assembly because their four
  neighbours are; the bytes are the same from the assembler and from Sony's macros in C. Sony's
  library objects would settle it.
- **Rewrites that match both ways**: `fight_UpdateDefeat`'s inline function against six `goto end`,
  `model_SetOutline`'s inline walk against a `goto` out of a `for (;;)`, `fighter_MarkSheathed` against
  ten lines written twice. A choice, not a finding.
- **Notes dropped on what a build showed**: 48 `FAKE` notes went because the plain form loses a
  register, a frame slot or an instruction the original has, so the variable or the test is the
  original's. That the thing exists is shown; how the authors spelled it is not. 17 more went as "what
  a programmer of 1997 would write" (kind D), and 31 before them in an earlier sweep.
- **Pairs the layout and the code disagree on**: the two win counts and the blow's three coordinates
  are separate variables by the code and look like one object by where they lie (an unnamed variable
  ahead of each explains it); `g_motion_loaded_sets` is one array by the layout and two variables by
  the code (two `FAKE` notes). The henchmen's and chanbara's tints are `CVECTOR`s, and the kaishaku's
  eight values one structure (`g_kaishaku_cues`), by their places alone: the code is the same either
  way.
- **Locals named `unused`, 26.** Each only sizes a frame; its size is known to within 8 bytes and its
  type is a guess. Eight could be a slot the compiler leaves and no local at all; the line that
  leaves exactly one slot in `cloth_CreatePiece` was not found, so none was removed.
- **Sizes and signs**: 138 of the 535 byte and short members compile the same signed or unsigned and
  55 are named by no code. 18 members are `unkNN` and 20 of `Fighter`'s are `unk_NN`; 25 variables are
  named `g_<subsystem>_unused_...` (and one `g_sys_data_start`): nothing reads any of them, or only
  code that clears them, and the name says what is known and no more. `sys_ExecMovieOverlay`'s `argv[8]`
  with six used. Nine functions defined in the old style (`f(a) int a; {`) because only that matches.
- **The assembly**: whether the authors' 71 routines were an assembler source or `__asm__` in C, the
  bytes cannot say; the three empty C functions among them in `render_asm.c` were probably assembly.

### The zeroed variables

- **21 spaces (`. += n`, 2,262 bytes).** 6 are the storage of Sony's assembly modules (52 bytes). The
  other 15 (2,210 bytes) are known only as "a variable, or several, that no code names": three large
  ones between files' own variables (0x468, 0x1C0, 0x218 bytes: unused `static`s of `cloth_model.c` or
  `model.c`, of `light.c` to `stage_props.c`, of `stage_hooks.c` or `wave.c`), and twelve small ones.
  For three, the variable before may simply be longer (`g_fighter_body_points`, `g_snd_stage_vab_buffer`,
  `g_cdrom`). Nothing reads any of them, so nothing in the program can settle them.
- **300 variables are placed by a line each in `bb2.ld`**, and 32 in the overlay's script. The original's
  linker placed them by a number made from each one's name; our names do not give that order, so the
  list has to. Only the original names would remove it.
- **12 `ALIGN(8)` lines in the list of the files' own small variables** are taken to be "a file starts
  on 8"; that is not tested.

### Source files

- **41 of the 59 boundaries between our 60 code files have no mark of their own**: they are where the
  subject changes. The marks (a file's strings ahead of its jump tables, padding at a file's end, a
  variable reached through `$gp` only by the file that defines it, a file's `static`s) prove 24
  boundaries and deny none of ours. The longest runs with no mark: `memcard_save.c` to `fight_after.c`,
  `fight_after.c` to `cloth_model.c`, `light.c` to `ndata.c`, `wave.c` to `collision.c`.
- **`fight_data.c` is several of the original's files' data run together**: its name tables belong to
  `fighter.c` and `fight_start.c` by where their strings lie. It was not taken apart; which piece is
  whose is not shown. `fight_sdata.c` is the same in the small data. Of the other eight data files one
  (`snd_data.c`) is shown to be a file of the original's.
- **`globals.c`**: its 120 variables were most likely defined in `main.c`. Nothing shows it.
- **The assembly** was at least two objects; ours is three files, cut by subject.
- **`static` functions**: `static` changes no instruction, so the bytes cannot show which functions
  were. 338 are `static` now and 19 `static inline`: every function, and every table with an initial
  value, that only its own file uses. That is the rule of the source, not a
  finding about the original. The 18 variables without an initial value that one file alone uses are
  not `static`: `bb2.ld` places them by name.

### Names

- **No original identifier of the game's has been recovered.** Every function, variable, type and
  constant name of the game is ours. The game's messages print none, and Kengo's names for the same
  things do not fit the linker's order (five of seven rejected, the other two cannot both be right).
- **What the linker's order can do**: reject a candidate. A proposed original name for one of the 42
  larger common variables has to give a number in the window its Sony neighbours leave
  ([`FAKES.md`](FAKES.md), "The zeroed variables"); the narrowest are `g_motion_decoders` (11 to 12),
  `g_memcard_file_list` (358 to 362), `g_shared_buffer` (47 to 52). No candidate has been found for
  any. `db` for `g_disp_db`, Sony's samples' word, falls in its window; that is suggestive and no more.
- **The overlay**: 18 of its streaming variables have Sony's names, from one scratch copy of
  `StCdInterrupt` in another project, and with them the linker's order holds. Three keep ours
  (`st_read_slot`, `st_frame_callback`, `ds_callbacks`).
- **Names that are a reading of the code and have not been seen or heard in the game** are listed
  under ["Open questions"](#open-questions). All of them need a person playing.

### Types and casts

897 casts are left in the game's files. When last sorted (at 956) 674 were needed by the types and 243
changed the code; they were worked by group, not one by one, and the 376 to a scalar pointer least of
all. Most are an address the engine keeps as an integer (primitive buffers, file images, the
scratchpad): one side of every use has to cast, and which side the authors cast is not known. Of the
unions, seven are two halves also written as one word: whether the authors had a union or a cast there
is not known.

## Suspected bugs and oddities of the original

Collected while the source was read, and marked in it with "the original's behaviour" or "the
original's data". Each is how the original's code or data reads; none was changed, and none was
watched in the running game unless it says so. Two that were suspected and turned out not to be bugs
are at the end, and what a modern compiler found is marked "found by `make modern`".
On the `main` branch ReBushido fixes fifteen of them when its "Bugfixes" setting is on, and
says which and why not the others ([`REBUSHIDO.md`](REBUSHIDO.md), "The fixes"); the notes here
and in the source describe the original.

**The fight**
- `fighter_Vanish` reads the start position with half the table's stride (12 halfwords a stage, not
  24): off the first stage the vanisher comes back at another stage's place. Watched in an
  emulator (a versus fight set up by writing to memory): on the castle rooftop it vanished and then fell for ever. A player
  cannot meet this: the vanisher is only in the second clan's stories, on the Tatara Shrine's
  stage, which has a branch of its own.
- `fight_TestBladesOnBodies` gives `fighter_TakeHit` no place for `*clashed_out`, which it clears
  first: a word written at address 0. It also takes the blade's direction from the last sweep tested,
  not the blade that touched.
- `cpu_Decide` tests its own motion's class where the opponent's step away would be expected;
  `cpu_AttackReach` picks the opponent's reach table by the CPU's own move. Its two tables
  (`g_cpu_weapon_reaches`, `g_cpu_weapon_ranges`) say the same of the nodachi, the broadsword and
  the naginata and not of the long sword (418 against -88) or the yari (36 against 936); which is
  meant is not known.
- `cpu_Decide`, mirroring an opponent that is neither walking nor stepping, takes its footwork from
  a variable that nothing has set for that: whatever it last held higher up in the function (found
  by `make modern`).
- The leaping attack is aimed for a fall of 31 a frame where gravity is 21.
- **Watched** in an emulator: a fighter's weapon points are kept three a frame whatever the weapon
  (`fight_UpdateFighterPair`), and a new fighter's first frame freshens only the first
  (`fighter_Update`). A naginata or yari that follows a two-point weapon in the same place sweeps
  its shaft, on its first frame, from whatever the scratchpad held: an address among it. Whether
  that sweep nudges the other fighter depends on where the program is linked; it is the one place
  where a moved build was seen to play differently from the retail game.
- `fighter_UpdateBleeding` clears its timer every frame, so a wound bleeds once and not every eighth
  frame for 150; and it bleeds from the body point with the bone's number.
- When the main weapon's blade goes live, `fighter_Update` resets the second weapon's last places
  again and not the main weapon's.
- `fighter_ScaleBodyPoints` scales a body point's squared reach by the fighter's size once, not twice.
- The ending's picture and the count of opponents beaten move the primitive cursor on four times as
  far as they draw (bytes added to a pointer to words).
- `g_fighter_run_classes` is there twice, the second copy unread; the jump tables and
  `g_kaishaku_voice_frames` have a place for every roster entry and one more but are read by
  character; chanbara's last opponent has the roster entry of the player's clan's crest and the
  model of the other's.

**Effects, objects, cloth, surf**
- `eff_SpawnSplash` writes a thirteenth splash over the next table when twelve are alive;
  `object_Launch` writes a thirteenth object after the pool; `cloth_Create`'s -1 (all eight in use)
  is not looked for.
- `eff_DrawMuzzleFlashes` keeps six ordering-table indices, two flashes' quads, with the size of
  the flash being made straight after them. Three flashes alive do no harm; with four, the third's
  quads are linked into the ordering table wherever the fourth's size says. Whether four can be
  alive within the seven frames a flash lasts was not worked out.
- `object_Update` reads the table of kinds one entry ahead of its first for an object that chanbara
  has just let go on the same frame (found by `make modern`).
- The splash, the dust and the hit flashes take the projection distance times 1000 and then shift
  by 8, where the flames shift it up by 8: they are drawn 1000/256, about 3.9, times the size their
  constants say.
- A splash's drops are black, so unseen, until they begin to fade, and the fade begins a frame
  early, where its blue is more than a byte holds.
- The dirt puff's texture is a row taller than its frame.
- `eff_UpdateParticles` tests "past a third of the reach" before "past half", so the second never
  passes.
- The rated lock flash chooses its texture by the hit flash's timer, which never gets that far.
- A cloth point more than 100 units under the ground is pushed up less than one just short of that;
  the last spring of a cloth is never pulled in when over-stretched.
- `cloth_CreatePiece` ends its list of a mesh's faces after every group of them, so only the first
  group's faces are joined by springs. 14 of the game's 34 cloth meshes have a second group; in
  each its faces join no two points the first group's do not, so nothing is lost (found by
  `make test`).
- `cloth_AddSpring` counts nothing against the eight springs a point has room for, from it or to
  it: a ninth from a point would be written over the first to it, a ninth to it over what follows
  the point. It cannot happen with the game's models: in all 34 cloths at most eight springs meet
  at a point, both ways together (`make test`, `tests/cloth.py`).
- `wave_GetEdgeAt` (which nothing calls) weighs its two points by the low bit of a remainder and
  lets through two columns past the row's last point.

**Menus and what is drawn over a fight**
- `menu_DrawTitleRule` sets its first line white and at once gray; `hud_DrawPanel` sets a tint it
  never turns on; `hud_DrawTimeAndCount` takes the tens' digit `% 100` after it is under ten;
  `hud_DrawPointsAndTime` goes three times round for two digits and draws the last twice.
- `hud_DrawPointsAndTime` and `hud_DrawTeamScores` each link a second drawing mode at the address
  just past the size they return, where the caller builds whatever it draws next.
- `link_Connect` goes through its three trades after the handshake gave up: the link is shut, so
  the exchanges do nothing, and the other side's unlocks, its buttons and the fight's seed are set
  from whatever was received last. The caller then leaves for the menu.
- Coming back to chanbara's page from a fight starts the pages' music twice.
- `select_AddBannerShade` and `link_SendPacket` each take a parameter and read the same thing from a
  variable; `select_StageStep` works out a glow before the page's title and draws the title without
  it; a page of stages has sheets for four rows and a cursor that reaches three; `select_WeaponStep`
  sets the draw's place twice; the weapon step's "take it back" reads the side's own pad where the
  rest reads the pad that picks for it; the select screens draw the title rule through a context of
  the wrong type.

**Models, textures, stages, resources**
- `model_LoadExtraTextures` loads a fogged model's extra colour tables over the rows the fog
  rewrites, not the plain ones under them (if the reading of the two blocks is right).
- `resource_LoadWeaponFile` marks the texture groups to load by each model's number and then
  looks a kept model's group up by its place in the set. For the second set (the practice weapons)
  that is the wrong entry of the table, and it holds the same group: nothing goes wrong.
- `tex_SetGrayscale` sets what the next `tex_IsGrayscale` overwrites; `tex_TintClut`'s gray mode
  (which nothing asks for) scales green and blue from the red it has just scaled.
- `render_Flush` and `render_FlushCutscene` are the same function twice; `light_SetStageColor` takes
  a stage it does not use.
- The tiny model's camera takes its y and z from the next two bones' matrices, not the root's.
- `stage_ReadCells` compares a piece's third angle with whether its first two are equal, where "all
  three are zero" was meant.
- `resource_OnStageMoved` moves the castle rooftop's backdrop meshes without passing how far.
- `STAGE_SHAINTO_GATE`'s third ambient slot has a volume and no sound; 26 of the side view's 65
  masks have something in their 32nd row, which is not read.
- Called by nothing: `stage_SetDetailAround`, `stage_ClearFarViewBit1`, `collision_CastMove`,
  `motion_PrintFrame`, the spare block's four functions, `math_Pow2Band`, the colour conversions
  of `math.c`, the host loaders of `main.c`. Those are some of 61: `make modern` lists every one
  (`build/modern/report.txt`, under `unused-function`).

**The system**
- `snd_SetStageReverb` sets the reverb's depth from the stage's number and not from the table's
  second column.
- The boot image's fade adds the TIM header's size twice: it starts 20 bytes into the pixels and
  runs 20 bytes past them.
- `memcard_FileExists` compares one byte past a name's twenty.
- The host loaders close a file whose read failed with the console's `close()`, not `PCclose()`.
- `snd_CompactSpuMemory` works out the same address once for every open bank before it uses it, and
  takes a bank's id it does not use; `cdrom_Init`'s `g_cdrom_init_done` is set and never read.
- The main loop passes `skipped` to `pad_Update` on the first frame before anything has set it,
  and `main` gives `disp_ShowBootImage` two locals nothing has set, which it does not use.
- `sprite_Draw` calls `font_Printf` with its warning's text alone, one argument short.
- In `g_unlock_bit_faction` the victim's bit, which nothing sets, counts with the gunners and the
  crests' bits count for nothing.
- The movie player's wait for a frame never ends on its timeout: nothing marks the frame done.
- A save file carries a table of 16-bit values and RAM addresses that the loader writes, and a flag
  (`SAVE_FLAG_PATCHES_ONLY`) to apply only that: a development leftover the game never sets. On the
  native port the memory card hands the game an empty table, so that no save file from anywhere can
  write memory there ([`PORT.md`](PORT.md), "The memory card"); the PlayStation builds are as they
  were.

**Suspected, not settled**
- `fight_TestBladesOnBodies` (by reading): with only the sub-weapon live its one
  sweep is the main blade's; with a pole arm and a live sub-weapon the third sweep is the
  sub-weapon's again and the shaft is not tested. Whether either can happen was not worked out.

**Suspected once, read again and not bugs**
- `fight_UpdateLock`'s `flash` is always set: its one caller calls it only when the first
  fighter's move is one of the seven kinds its switch has.
- The select banner's stripes are not uneven: each is a pixel thinner than the one outside it, so
  their outer edges are 33 apart and their inner edges 32, on both sides, mirrored about the
  middle of the screen.

## What was tested and what was not

**Tested**

- **The bytes.** Both executables are built and their SHA1 compared on every push. That shows the
  source compiles to the original; it does not show that a name or a comment is right.
- **Nothing depends on an address** (`make shift`): the objects are relinked with padding in front
  of the code and the data, and the check fails if anything in the result still points at an old
  place. It passes. Not moved: the movie overlay (linked at a fixed address) and the fixed buffers
  above the program (48 uses, `include/sys.h`), which leave a changed build 0x6220 bytes to grow by.
- **The shifted build in the running game.** A disc built with everything moved was run beside the
  original in an emulator by script and compared byte for byte (screenshots, memory, call logs): the
  opening movie, a story stage and the start of the next, a card save and load, a versus fight on
  every one of the 38 stages with every roster entry and weapon, sub-weapons thrown and guns fired,
  and eleven opponents of Vs. CPU Mode. One fight differed and was traced to the original reading
  leftover memory, not to the link (the list above, "The fight", the entry marked watched). The
  sound was off in those runs. The shifted disc was then played by hand for one sitting and nothing
  wrong was seen; not every mode was played, and which were was not written down.
- **Names in the running game.** Over 200 scripted emulator runs, and 77 more in a second round,
  set up fights by writing to memory and logged calls and screenshots: they confirmed most of what
  can be seen (the screens, the effects, the stages' props, the seven characters with no name) and
  corrected about fifteen names. The scripts and their record are not in the repository any more
  ([`HISTORY.md`](HISTORY.md)).
- **A modern compiler's reading** (`make modern`, `tools/modern/check.py`): the game's C given to
  the host's `gcc` as 32-bit code for its warnings and its analyser. 1,038 findings, 22 of a serious
  kind left in the baseline (something read before it is set, an index past an array, a call or a
  return that does not fit). Every serious finding was read: they are the compiler being wrong, how
  the bytes need the source written, or the original's own (four places, in the list above). None
  was a mistake of ours in a type or a declaration. The check fails when a change adds a serious
  finding.
- **What the C computes** (`make test`, [`tests/README.md`](../tests/README.md)): the game's pure
  functions built for the host from the game's own `.c` files and run. 13 tests, about 600,000
  comparisons: against real maths (the sine and root tables, the rotation matrices, the colour
  conversions, the point in a triangle), against cases made by hand (the arena, the save record,
  the bit reader, the random numbers) and against the disc (every frame of every motion file
  decoded two ways, the CD table against the disc's directory, the archive's directory and packs,
  every model's cloth). All pass; two are skipped without the disc.

- **ReBushido's build in the running game** (`make playtest`,
  [`tests/play/README.md`](../tests/play/README.md); on the `main` branch, on the developer's
  machine): fourteen scripted tests in an emulator with no window (nine of the game; the tenth holds
  the native port's C to the emulator's GTE and to the assembly, the next three the port's
  program to the emulator's game from power on through three whole fights, and the last the port's
  picture to the emulator's, pixel for pixel, [`PORT.md`](PORT.md)). The retail
  disc reaches its top menu;
  the ReBushido disc, left at its options page with nothing turned on, is in the same state as the
  retail disc at eleven checkpoints from the opening through the menus and a versus fight to the
  menu after it (the watched values, the vertical blanks between them, the picture to the pixel); a
  quick-boot disc starts the fight it was built for, and that fight runs to a kill, the menu after
  and a rematch without a crash. With "Bugfixes" on, a versus fight from the options page to the
  menu after it runs without a crash, and two of the fixes are shown with the setting off and on:
  a naginata's first frame, and the vanisher struck off the shrine's stage. Versus fights only,
  with no sound.
- **ReBushido's options page and its file on the memory card** (the same play tests): the page
  comes up, the pad changes a setting, the file is written, the game goes on to its top menu, and a
  second boot with that card has the setting; with no card in either slot the page says so and the
  game goes on. In the emulator with its own BIOS only: not on a console, and not with an
  unformatted card, a full one or one pulled out during the write
  ([`REBUSHIDO.md`](REBUSHIDO.md), "The options page"). The file's format is held by host tests.

**Not tested, or not read**

- **Modes never run on a moved build by script**: team battle, chanbara, practice, link play, the
  kaishaku, the story past its second stage, the endings and the staff roll, first person, a long
  run. A table typed as the wrong thing that only an unplayed mode reads would not have shown.
- **Sources never gone through**: Bushido Blade 1 (it was not to hand; the same engine a
  year earlier is the likeliest place for an original name or macro); Sony's real headers, member by
  member against `include/psxsdk/` (not to hand; the two macro headers were once checked by
  hash); the PsyQ library objects (not to hand: which of Sony's modules were assembly was told from the listings);
  the archive branch's `docs/naming/` (5,216 files) beyond its README; Kengo's source file paths (107
  in its executable, such as `common/judge.c`, read and not set against our files) and its data file;
  the words in the menus' pictures, but for the stages' cards and the fighters' names, which were
  read.
- **Not looked at in the accuracy pass**: Sony's modules' file boundaries and casts; the order of
  declarations inside a file's `.data`; local names and members (there is no outside evidence for
  them); 32-bit members' signs.
- **The polish pass ran nothing.** It changed names, numbers' spelling, comments, layout and linkage,
  and every step was held to the bytes; nothing it wrote was watched in the game. The stages' and
  fighters' names are read off pictures on the disc (the stage cards, the select screen's sheets, the
  HUD's name cells), each by one look at an extracted picture. The comments it wrote
  about what a number means (an effect's sizes, a screen's places, a table's columns) are readings
  of the code. In the last read-through the numbers of `math_sin.c` and the view masks of
  `stage_data.c` were skimmed, not read.
- **What `make modern` cannot see**: the functions that are assembly; what a GTE
  instruction reads; anything that depends on what a caller in another file passes or on what the
  data holds (it found neither the muzzle flashes' indices nor the cloth's ninth spring); and the
  700 places, in 86 of the larger functions, where the analyser gave up. Its 60 findings set aside
  as wrong were each read once.
- **What `make test` does not test**: anything that is assembly (`math_Sqrt`,
  `math_Length`, the matrix products, the renderer) or that works through the GTE, which is most
  of the fight's geometry (the blade tests, `math_MatrixToAngles`, `math_ScaleAlong`): the tests
  have a stand-in only for the GTE's count of leading zeros. Nothing that draws, plays a sound or
  reads a pad. The tests are compiled by the host's `gcc` for x86, so they show what the C means,
  not what GCC 2.7.2 made of it: that is what the SHA1 is for. The motion decoder's second decoder
  was written by whoever had read the first, from `docs/formats/BBM.md`: it is
  independent in how it works, not in who understood the format. Sony's routines that are assembly
  are stood in for by exact arithmetic (`tests/stubs/`), which the real ones, reading tables, are
  not quite.
- **Sony's library code was not polished** (`src/main/psxsdk/`, `include/psxsdk/`, `movovl/sdk/`): it
  keeps Sony's names and shapes, and the lint does not read it.

## Open questions

1. **Seven characters have no name anywhere on the disc** and are named for what the code does with
   them (`enum CharacterId`, `include/fighter.h`). Anyone who knows what the game, its manual or a
   guide calls them is asked to say:
   - `CHARACTER_VANISHER` (10): a figure in a long white robe with long pale hair, at the Tatara
     Shrine in the second clan's stories; a blow makes it vanish and come back elsewhere.
   - `CHARACTER_SIDEARM_1` (11) and `CHARACTER_SIDEARM_2` (23): the ninja a story begins with, one in
     grey and one in black, each with a pistol in the off hand; the black one is practice's partner.
   - `CHARACTER_ARMORED` (22): a broad figure in dark brown armour, the last opponent of the first
     clan's stories; blows spark off the armour.
   - `CHARACTER_VICTIM` (25): a shrine maiden in white who kneels and does not fight, at the end of
     the second clan's stories.
   - `CHARACTER_LAST_1` (24) and `CHARACTER_LAST_2` (26): the two kabuki figures (purple and yellow,
     red and white) behind the clans' crests on the select screen, won in Slash Mode.

   Stage 37 has no name either: link mode's stand-in for the castle rooftop
   (`SELECT_STAGE_LINK_TO`), its surroundings by night without the roof.
2. **Names nobody has heard or seen confirmed.** These are readings of the code, and need a person
   playing, with the sound on:
   - everything heard: the sound banks (`SND_VAB_...`), the judge's calls (`enum JudgeSound`), the
     fighters' forty sounds (`SND_FIGHTER_...`), sixteen sounds of `fighter_PlayFrameEvent` that are
     still a bank and a number, the reverb's depth, what `KAISYA_1.XA` and `KAISYA_2.XA` hold;
   - the story: an interlude won by the guest, the guest turning up later with its wounds, the staff
     roll after a continue;
   - the fight: `enum MoveEvent` beyond the lock and the parry, body points 6 to 13, the blade lock
     begun by running, a team battle's break and its time running out, what the CPU's profiles feel
     like;
   - what is drawn: the cloth pieces and the face parts, the render flags, the rarer camera views,
     `enum Surface` kinds 0 and 5, the Lost Bridge's flowing texture, the dust and the flame kinds;
   - the comments on the tables of the data files.

   Beside these, 21 places in the source are marked as guesses (`grep -rn -i guess src include`).

- **Earlier material:** everything removed in the clean-up, and the rules, tooling, naming evidence
  and runtime captures of the process used until 2026-11-10, are kept in a private archive and are
  not published. [`HISTORY.md`](HISTORY.md).
