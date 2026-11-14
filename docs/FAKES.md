# `FAKE` notes

A `FAKE` note marks code that is shaped to give the original's bytes and is
not what its authors would have written (`docs/STYLE.md`). This page sorts
every one of them. It is the record of the accuracy pass, which went
through the source in rounds, and the page names them as they went: the
first sweep (before the pass, which dropped notes and made frame pads into
locals), the SDK audit (Sony's assembly), the kind A round (which wrote this
page), rounds 1 and 2 over kinds B and C (their results are under "Kinds B
and C, round 1" and "Kinds B and C, round 2" and in the last column of the
two tables, where "round 2:" marks what round 2 built), the `goto` round,
the layout round (the zeroed variables and the frame's unused room: "The
zeroed variables"), the types round ("Types and casts"), the files round
("The original's source files"), the names round ("Names against outside
evidence"), the last naming round (Japanese words made English) and the
last clean-up. The working notes of each round are not in the repository
any more (`docs/HISTORY.md`).
`python tools/count.py` counts the notes and the `goto`s in the
source and checks them against the tables of this page that say what is
left; a note added or removed needs its row here.

| Kind | What it is |
|---|---|
| A | the code says something untrue: a wrapper struct, a wrong type or size, a function called at the wrong type |
| B | an extra or reused local, a declaration order, an odd expression, that only moves registers or instructions |
| C | control flow written unnaturally to match (not a `goto` itself) |
| D | not fake after all; the note is gone, and the reason is here |

## Counts

225 notes when the kind A round began: 199 in the game, 23 in the SDK, 3 in
`movovl/`. 197 when round 1 began (176, 19, 2), 149 when round 2 began
(133, 15, 1), 137 when the `goto` round began. 140 now: 126 in the game, 12 in the
SDK, 1 in `movovl/` and the overlay's listing. The `goto` round took two away (one
fixed, one Sony's own) and put five on `goto`s that are ours.
The types round took two away by a type (below, "Types and casts"): 138 now, 125 in the
game, 11 in the SDK, 1 in `movovl/` and the listing.
The last clean-up took one away that stood over nothing (`cutscene.c`, kind D
below): 137 now, 124 in the game. It also brought six notes' words up to
the names the code has now (`MATH_SQRT_SHIFT` for `0x16`, `foot_pos`,
`length`, `b_y - a_y`, the roster's two constants, `index`).

| Kind | Notes | |
|---|---|---|
| A, fixed | 12 | the kind A round: the code changed and the note went; the types round: the reverb call (`snd_SetStageReverb`'s result is a 16-bit variable) |
| A, left | 16 | each with what was tried, below; round 1 tried the four that had not been |
| B, fixed | 9 | round 1 (7), the `goto` round (`model_SetOutline`'s walk of the extra nodes) and the types round (`PutDispEnv`: the saved environment is a `volatile` member) |
| B, the original's own | 45 | round 1 (35) and round 2 (10): the variable or the `volatile` is in the bytes; the word dropped, the reason below |
| B, left | 101 | all tried: 38 in round 1, 63 in round 2 |
| C, fixed | 6 | round 1 |
| C, the original's own | 3 | round 2 (2): the tests are in the bytes; the `goto` round: Sony's `goto` out of a loop in `_spu_gcSPU` |
| C, left | 14 | all tried: 7 in round 1, 7 in round 2 |
| a `goto` that is ours | 5 | the `goto` round: nine `goto` in five places, each built in its plain forms |
| D | 18 | the kind A round: the word dropped, the reason below; the last clean-up: the note over nothing in `cutscene.c` |
| a listing | 1 | `dma_execute` in the overlay, the SDK audit's |

## What the compiler does, found while working the A's

Four facts decided most of the verdicts below, and later rounds can use
them. Each was measured with this build's compiler, not assumed.

1. **A structure or array local starts on an 8-byte boundary of the frame.**
   So a function with a 0x14-byte structure and 8 unexplained bytes after
   it has one more local of one to eight bytes, not twelve bytes of padding
   in the structure. `cloth_CreatePiece` was the known hard case: as
   `ClothDesc desc;` and twelve unused bytes it did not match, as
   `ClothDesc desc;` and eight it does. A variable that is not a structure
   or an array is in the frame only if its address is taken or it is
   `volatile`, and then it takes four bytes.
2. **The address of a structure or an array is kept in a register between
   uses of its first element; a plain variable's is formed again at every
   use.** (An element or member at a nonzero offset is addressed directly
   either way.) So code that names a variable and forms its address each
   time is naming a variable, and code that reaches a table at a constant
   distance from the register that holds another table is reaching one
   object. Sony's four CD command tables are one object by this, and the
   game's win counts are two variables.
3. **The original's linker laid the zeroed variables out in an order that
   follows their names.** Sony's own separately named variables stand side
   by side where their names differ by one in a letter: `_svm_okon1` and
   `_svm_okon2`, `_svm_okof1` and `_svm_okof2`, `_snd_seq_s_max` and
   `_snd_seq_t_max` (but `_svm_orev1` and `_svm_orev2` have one variable
   between them). So two of the game's variables being neighbours does not
   show that they are one array: names like `x`, `y`, `z` or `1`, `2` at
   the end would put them there. The layout round worked the rule out: the order
   is that of the sum of the name's characters (under "The zeroed variables").
4. **A call to a function declared to return `int` is compiled differently
   from a call to a `void` one** (which register holds a value across the
   call, the order of the instructions before it), also when the result is
   not used. Two game functions that return nothing are `int` functions by
   their callers' code: `snd_Play` (the first sweep) and
   `fighter_TryDropSecondSword` (here).

Round 1 added four more, measured the same way.

5. **A value that the compiler makes for itself dies once, and its register
   is then free for what is computed from it; a variable of the source that
   is set in several places is given a register of its own.** (The compiler
   ties a result to its operand's register only when the operand is set
   once and dies there: `local-alloc.c` takes a pseudo-register only if
   `reg_n_deaths` is 1.) So where the original computes `x + 12` into a
   second register though `x` is dead, the sum went into a variable that
   the function sets more than once. An inline function does not do it:
   each call gets fresh temporaries.
6. **A constant gets no saved register of its own.** A literal is loaded
   where it is used (and 0 is `$zero`); a loop may lift one out, and then
   the literal in the source gives the same code. So a constant that sits
   in `$s0`-`$s7` from the top of a function to several uses was a variable
   in the source. In the same way a division by a register that holds a
   constant was a division by a variable: a literal divisor is compiled as
   a multiplication.
7. **After a store through a plain pointer the compiler reads again every
   variable it had read; after a store to a member of a structure or an
   element of an array it does not** (it takes such a store to be unable to
   change a plain variable). So a global read twice around a store says the
   store was through a pointer.
8. **A loop whose counter is used only to count is turned round to count
   down**, and one that indexes an array with it walks a pointer down. So
   `for (i = 0; i < n; i++) a[i] = 0;` is what gives a loop that clears
   from the top down; written counting down by hand it gives other code.
   Round 2 tried every loop in the tree that counted down to 0 over a
   constant number of turns: fifteen more are forward loops (below). A loop
   whose count is a variable is not turned round: those that end on
   `!= -1` are something else and were left.

The `goto` round added five about jumps, measured the same way.

9. **A `?:` whose arms are tests, used as a condition, gives its first arm
   a conditional branch and then a jump**: "if so go there, if not go
   there". (The compiler hands each arm both of its destinations; an `if`
   inside an `if` gets one and falls through.) So where the bytes test a
   sign or an order and then have a branch followed at once by a jump, the
   source had `a ? b : c` in an `if` or under `&&`:
   `x >= 0 ? x < n : -x < n`. Written `(x < 0 ? -x : x) < n` it becomes the
   compiler's absolute value, which is other code. Sixteen `goto` were
   this (`light_ApplyPointLight`, `stage_QueueFarCells`, `cpu_Decide`).
10. **A loop that begins `if (c) break;` has that test moved to its foot; a
    loop that begins `if (c) return;` or `if (c) goto out;` keeps it at the
    top and jumps back from the foot.** (The compiler rolls round a
    conditional jump to the loop's own end and no other.) So a loop in the
    bytes that tests at the top and ends in an unconditional jump back was
    left by a `return`, in a function expanded inline, or by a `goto`.
11. **A loop made with `goto` is not a loop to the compiler**: nothing is
    lifted out of it, no pointer is stepped in place of an index, and its
    variables get no extra weight when the registers are given out. A
    loop statement is a loop to it whatever its form and however it is
    left. Four loops of the game give the original's code only as a `goto`
    (under "The `goto`s").
12. **Two copies of the same statements are merged when they end at the
    same place and compile to the same instructions in the same
    registers**, branches inside them and all: ten lines in two arms of an
    `if` in `fighter_Update`, three copies of four lines in
    `select_HandleTeamRules`. They are not merged in `eff_DrawFlames`,
    `eff_DrawFlash`, `fighter_ResolveBladeContact` or Sony's two `printf`s,
    where the second copy stays as more code. So a `goto` to a shared end
    that goes away when the end is written twice says nothing of what the
    authors wrote, and one that does not go away shows that the original
    had one copy and a jump.
13. **An inline function's `return` is a jump to the end of the call**, so
    early returns give what `goto end` gives; and **an inline function's
    locals are variables of their own**, so the function that calls it
    cannot keep one of them and one of its own in the same register.
    `fight_UpdateDefeat` matches either way; `select_StageStep` matches
    only with the `goto`, because its `i` and `stage` are used on both
    sides of it.

Two more that matter for inline functions: **an inline function's argument
is copied into a variable of the function**, so when the function assigns
to its parameter the caller's value needs a second register only if the
caller passed a variable it uses again; and **the calls of an inline
function share the frame slot of a local whose address is taken**, where a
block-scope local of a macro gets a slot at each use.

## Kinds B and C, round 1

178 notes (155 B, 23 C). 48 are gone: 13 by a change to the code, 35
because the thing the note called a stand-in is in the bytes. 46 more were
tried and are left (the last column of the tables below says what was
tried), and 84 were not tried (round 2 built all of those: below). No
note was dropped only because the code looked natural, and none was traded
for another.

### Fixed (13 notes)

| Notes | Where | Before | Now |
|---|---|---|---|
| 3 B | `model_update.c` `model_Update`, `resource_weapon.c` `weapon_Queue`, `weapon_QueueWorn` | `{ s32 *extra = &node->extra; *extra = x; }` | `NODE_EXTRA(node) = x;` (`model.h`): the word after the scene node, stored through a pointer. Fact 7: the original reads `g_render_queue_end` again after the store, which it would not after a store to a member. The other five stores to `extra` come out the same either way, so nothing shows that `extra` is a member at all (the types round). |
| 5 C | `tex.c`: the five group loaders | `if (count != 0) { while (count-- != 0) { ... } }` | `if (count == 0) return;` and the plain `while`. The test ahead of the loop is in the bytes (the plain `while` alone jumps to the loop's test; an unsigned count, a `for` and a `do` were tried too). |
| 2 B | `cloth.c` `cloth_FreeAll`, `snd_stage.c` `snd_ResetStageSounds` | loops written counting down, one by byte offset and one with a cursor | `for (i = 0; i < n; i++) a[i] = 0;`. Fact 8. The loop above it in `snd_ResetStageSounds`, which had no note, is forward too. |
| 1 B | `hit_test.c` `hit_FrameTriangleByNormal` | the normal's z shifted in a local that was free | `scr->normal.vector.z >>= 6` and the root of the two members, once the root is the inline function (below) |
| 1 B | `model_update.c` `model_SaveTintCluts` | the model id read again through a pointer | the plain `model->model_id`; the note was stale |
| 1 C | `fighter_hit.c` `fighter_TestShot` | `do { point = ...; } while (0);` | the plain assignment; the note was stale |

### The square root is an inline function

The table's square root (`MATH_SQRT`, `math.h`) was a macro in eleven
places and written out in some fifteen more, most of those with a temporary that
"holds a number, then the byte of the table it leads to". That temporary is
the parameter of an inline function, assigned to inside it:

```c
static inline s32 math_TableSqrt(s32 x) {   /* math.h */
    ...
    x = g_math_sqrt8_table[(u32)x >> shift];
    return (u32)(x << 16) >> (0x13 - ((u32)shift >> 1));
```

Before, in `math_MatrixToAngles` (and the same in `math_TurnAngles`):

```c
root_byte = i0 * i0 + i1 * i1;
if ((u32)root_byte < 0x400) { var_7 = ...; } else { ... 13 lines ... }
```

Now: `var_7 = math_TableSqrt(i0 * i0 + i1 * i1);`. It matches wherever the
number rooted is an expression: `math_MatrixToAngles`, `math_TurnAngles`,
`hit_FrameSegment`, `hit_FrameTriangleByNormal`, `math_ScaleAlong`,
`object_DirtHitFighters`, `camera_UpdateSideView` (seven places; the
variable that only carried the sum to the macro is gone from each). It does
not match where the caller roots a variable it uses again or puts the root
back in the variable rooted (`hit_SphereMeetsSliver`,
`hit_SphereMeetsSegment`, `motion_AimPose`), nor in `hit_FrameTriangle`,
whose three roots keep three frame slots where three calls would share
one; those keep the macro. Two written-out copies with a second assignment
of the number (`math_SegmentMeetsCircle`, `hit_SphereMeetsTriangle`) fit
neither. `fighter_hit.c` has the same function without the test of the
sign (`math_TableSqrtPositive`, as `cloth_Sqrt` in `cloth.c`); it is called
once, and `fighter_TestShot`'s two roots are one instruction's place away
from being two more calls of it.

So the authors had at least two inline functions for this, and the places
that match neither are probably a third form or the same function seen
through a difference not found yet. None of this removed a `FAKE` note by
itself (the temporaries were not marked), but it took the stand-ins out of
seven functions and one note went with it.

### The root of an unsigned number: not found

Five places root a `u32` with no test of the sign (`fight_HoldDistance`,
`fight_HoldLockDistance`, `snd_PlayAt`, `camera_FindHidingCell`,
`math_ReverseUnit`), and four have the note `shift = 0x16 - (lz & ~1)`: the
stand-in keeps -2 in a variable and ands the count into it. Written plainly
the count is loaded into `$v0` and -2 into `$v1`; the original has them the
other way round. By fact 5 the result of the `and` is a variable that is
set elsewhere too. Tried in `fight_HoldDistance`: `MATH_SQRT`; six inline
functions (the count from an inline of its own, with and without a test of
the sign, the root through a result variable); some twenty spellings of the
expression; and the permuter for nine minutes, whose best (a score of 10) keeps
the even count in the function's result variable, `distance = lz & ~1;
shift = 0x16 - distance;`, which leaves one register different. Left.

### The original's own (35 notes)

Each of these said "read it as" something simpler. The simpler form was
built, and what goes missing from the code shows that the original had
what the note called a stand-in. The word `FAKE` is dropped and the code
is as it was (two locals got better names).

| Notes | Where | The note said | What the bytes show |
|---|---|---|---|
| 19 | `menu_pages.c` (9 functions), `select.c` (6), `select_team.c` (4) | `draw.table = draw.header->cells`, through a local `cells` | Fact 5. The original puts the cells' address in a second register and makes it ahead of the store of the header: `lw v0; addiu v1,v0,12; sw v0,header; sw v1,table`. Written directly, and in eleven other ways (a cast of `header + 1`, three inline functions, a variable for the header, comma forms), the sum stays in `$v0`. Only a variable that the function sets more than once does it, and each of the 19 functions sets `cells` two to six times; some go on to use it (`cells + 1`). Functions that draw one sheet have no such variable and match with the direct form. |
| 10 | `select_team.c` (3), `staff.c`, `narration.c`, `select.c` `select_WeaponStep`, `menu_pages.c` `menu_DrawKeyConfig`, `menu_DrawPractice` (2), `menu_DrawChanbara` | a constant kept in a local; read it as the constant | Fact 6. The original loads the constant into a saved register at the top (`move s5,zero`, `li s7,0x20`, `li s4,0x60`, `li s2,0x40`) and passes that register. As a literal the register, its save and its restore are gone (three instructions fewer). It is the blending mode handed to `sprite_GetTPage` in nine of them: `zero` is now `abr` in two. |
| 1 | `fighter_hit.c` `fight_HoldLockDistance` | `/ 1000`, written in each line | Fact 6. The original divides by a register four times; the literal is compiled as a multiplication (109 instructions for 131). A local for the 1100 with `fight_HoldDistance`'s body was tried and is not it. |
| 1 | `fight_modes.c` `fight_BuildVsCpuOrder` | `1 << i` | The original shifts a register that holds 1 (`li a2,1; sllv`). With the literal the compiler shifts the mask down and tests bit 0 instead. |
| 4 | `psxsdk/libcd/bios.c` `getintr`, `CD_getsector2`; `libetc/vsync.c` `v_wait`; `libspu/s_crwa.c` `SpuClearReverbWorkArea` | the local is not `volatile` | As the first sweep's two. Without `volatile` the frame slot and its loads and stores go: `getintr`'s `nReg` is stored at 16(sp) and read back at every use, and a byte of `buf` is read back after it is stored; `v_wait`'s count is loaded, decremented and stored on every turn; the callback pointer is in the frame; `CD_getsector2` stores what it read from the register in an 8-byte frame it has for nothing else. These are the three the kind A round asked about and one more. |

## Kinds B and C, round 2: the 84 that round 1 did not try

All 84 were built in their plain form (the 81 marked "not tried" and the
three roots marked "not built here"). None matched as it stood, and none
was fixed by a change to the code. 12 are gone because the plain form
loses something the original has; 72 are left, each with what was built
and how the code then differed (the entries that begin "round 2:" in the two
tables below). No note was traded for another.

### The original's own (12 notes)

The word `FAKE` is dropped and the code is as it was.

| File | Function | The note said | Built in its place, and what went missing |
|---|---|---|---|
| `camera.c` | `camera_UpdateFirstPerson` | g_camera_second itself, at each use | `g_camera_second` named at each use: 145 instructions for 134. The saved register that holds the view's address from the top of the function is gone with its save, and the address is formed at every use. The pointer is in the bytes. |
| `fight_lock.c` | `fight_UpdateLock` | first is g_fighters already | without the assignment: 340 instructions for 342 (the second load of the fighters' address is gone) and `$s0` and `$s1` change places. `first` is set again in the original. |
| `fighter_hit.c` | `fighter_TakeHit` | object_kind itself, for both | `object_kind` at every use: 565 instructions for 574, the frame 8 bytes smaller and a saved register fewer. The original keeps the parameter in two variables (an inline function's parameter does that, but none was found). |
| `light.c` | `light_ApplyPointLight` | g_light_setups, held in a local | `g_light_setups` named: 133 instructions for 135 and the frame 8 bytes smaller. `$s8` holds the array's address from the top in the original; without the variable it takes the first parameter, which the original keeps in the frame. |
| `menu_pages.c` | `menu_StepNudge` | steps++, as a byte offset that is 0 the first time | `steps++` at the foot of the loop: 65 instructions for 67. The register that starts at 0, grows by 2 a turn and is added to `steps` at the top of each turn is gone. (`steps += axis`: 66, a shift each turn.) The offset is a variable of the original; what the authors called it is not known. |
| `psxsdk/libsnd/vm_no1.c` | `vmNoiseOn` | vc at each use | `vc` at each use: 306 instructions for 308, the frame 8 bytes smaller and a saved register fewer. The copy is in the bytes. |
| `psxsdk/libspu/s_gvv.c` | `assign` | uval - 0x8000, without the local | `uval - 0x8000`: the compiler adds (`li 0x8000; addu`), the original subtracts a register (`subu`). A constant subtracted is compiled as an addition, so the 0x8000 was in a variable (fact 6). |
| `resource.c` | `resource_LoadStage` | s7, the "nothing to read" flag above, reused for the block id | the block's id as a variable of its own, and as the literal: both 243 instructions for 248 and the frame 8 bytes smaller. `li s7,7` and the saved register are gone: a constant is kept in a saved register only for a variable set in several places (facts 5 and 6), and this one is set to 1, to 0 and to the block's id. |
| `resource_weapon.c` | `snd_LoadWeaponVabs` | copies -1 over -1 | `if (slot1 != slot0) slot1 = 0;`: 127 instructions for 126, and the `move s0,s2` is gone. The assignment is in the bytes. |
| `story.c` | `story_RunScript` | g_fight_config.roster[1] and .weapon[1] | both named: 177 instructions for 179 and the frame 8 bytes smaller. `$s1`, which holds `&g_fight_config.roster[1]` from the top of the function, is gone with its save and restore. The pointer is in the bytes. |
| `cpu.c` | `cpu_Decide` | the test of the goal is written twice | with one test: 2989 instructions for 2991; the second `li v0,1; beq` is gone. The goal is tested twice in the bytes. |
| `cpu.c` | `cpu_Decide` | only the goto reaches the leap | without the two tests that nothing passes: 2974 instructions for 2991. The tests are in the bytes; the `goto` into the block is the `goto` round's. |

What these have in common is fact 5 and fact 6 again: a pointer or a
constant that sits in a saved register from the top of a function
(`camera_UpdateFirstPerson`, `light_ApplyPointLight`, `story_RunScript`,
`resource_LoadStage`), a second copy of a value (`fighter_TakeHit`,
`vmNoiseOn`), and an assignment or a test that changes nothing but is
there as instructions (`fight_UpdateLock`, `snd_LoadWeaponVabs`, the two
in `cpu_Decide`).

### What the 72 that are left have in common

- **The plain form gives the same instructions in other registers** (about
  40). A local used for two things, or a copy of a parameter, decides which
  register each value gets and in what order the saved registers are
  stored; giving each thing a local of its own moves them. Nothing was
  found that says which variables the original shared.
- **The plain form is a few instructions shorter or longer** (about 25),
  mostly because an address is formed in another order or a value is read
  again. Several of these show that something is there without showing
  what: `fighter_TestBodyInSweep`'s flag has a frame slot, `cpu_Decide`'s
  best score is a 16-bit variable, `menu_DrawMessageText` keeps
  `message * 8` in the frame, `model_Update` holds the root in a variable.
- **The root of an unsigned number** (seven places with the two in
  `cloth.c` and `math_TableSqrtPositive`): all now built plainly, all with
  the one difference round 1 found (the count in `$v0` and -2 in `$v1`,
  or the -2 loaded ahead of the count).
- `MATH_SQRT`'s six places and the written-out roots were not tried again
  as the inline function: round 1 built each of them that way and says
  above what differs.

### A frame slot nothing uses

`cloth_CreatePiece`'s second loop is written `for (n = (s16)*src++, count =
n; n != 0; ...)` for the original's `while ((count = *src++) != 0)`. The
plain `while` gives **the same 263 instructions** and a frame 16 bytes
larger; without the function's `s32 unused[2]` it is 8 bytes larger than
the original. Ten spellings of the loop were built (in the table). What
they show: the compiler leaves a frame slot of 8 bytes for a value it
made and then merged into another instruction (its count of uses is not
brought up to date, so the value is given a place in the frame that
nothing reads or writes: `reload1.c`, `alter_reg`). The plain `while`
leaves two such slots, the written-out forms none, and the original's
frame has room for exactly one besides the description. So the 8 bytes
that `unused[2]` stands for are probably one of these slots and not a
local the authors declared, and the line that leaves exactly one was not
found (the permuter, scoring stack differences, tried about 28,900
candidates in ten minutes and none was better than the plain `while`). This is
measured for this function only; it is likely to be what several of the
"unused locals that size a frame" at the end of this page are, and it is
why so many of the plain forms above change the frame by 8 bytes.

### Fifteen more forward loops

Fact 8, tried on the loops that carry no note. Each of these counted down
to 0 over a constant number of turns, some of them walking a pointer down
beside the counter; each is now `for (i = 0; i < n; i++)` and matches:
`arena_Reset`, `sys_PollHost`, the two clears in
`memcard_BuildFileHeader`, the two in `model_AssignMeshes`,
`model_Update`'s marking of the body's matrices,
`reloc_ClearCounts`, the two in `replay_Reset`,
`resource_ClearWeaponRequests` and `resource_ForgetWeapons` (whose local
that held -1 is gone too), `stage_ResetCellFlags`, Sony's `_SpuInit`
(`s_ini.c`) and the overlay's `movie_Start`. Not forward loops:
`model_RebaseNodeParents`, `reloc_RebaseBlock`, `tex_GrayscalePixels` and
`resource_OnWeaponsMoved` (a variable count, ended on `!= -1`; a `for`
upward and `while (count-- != 0)` both differ), and the loops that really
do go backwards (`cpu.c`'s route, `sprite.c`, `fight_modes.c`).

## The `goto`s

84 `goto` statements when the round began: 59 in the game, 25 in Sony's
code, none in the overlay. (An earlier 85 and 60 counted a comment in
`cpu.c`.) 47 now: 22 in the game, 25 in Sony's. Every one was sorted, and
every one of the game's that was not plainly a jump of the authors' was
built in at least one plain form; none is left untried.

| Verdict | `goto` | |
|---|---|---|
| removed by a rewrite that matches | 37 | game: the table below |
| kept: a jump the authors could well have written | 6 | game: a shared way out, a retry, leaving a loop past what follows it; each built once in a plain form all the same |
| kept: the bytes have one copy of the code and a jump to it | 7 | game: the second copy was written out and the compiler did not merge it. Probably the authors' |
| kept, ours | 9 | game: four loops and one test that only a `goto` gives; what the authors wrote was not found. Five new `FAKE` notes |
| kept, Sony's | 25 | each built once in its plain form; none matches |
| not tried | 0 | |

What the compiler does with jumps is facts 9 to 13 at the top of this page.

### Removed (37)

| File, function | `goto` | Before | Now |
|---|---|---|---|
| `light.c` `light_ApplyPointLight` | 13 of 15 | each axis tested with four jumps (`if (dx < 0) goto neg_dx; if (dx < BOX) goto check_dy; goto on; neg_dx: if (-dx >= BOX) goto on;`), and two tests ahead that each `goto off` | `if (WITHIN(dx, BOX) && WITHIN(dy, BOX) && WITHIN(dz, BOX) && math_LengthSquared(dx, dy, dz) > RANGE) goto off;` with `WITHIN(d, n)` `((d) >= 0 ? (d) < (n) : -(d) < (n))`, and `if (pos == 0 \|\| g_light_point_color[0] < 0) goto off;`. Fact 9. Built on the way: `(dx >= 0 ? dx : -dx) >= BOX`, 123 instructions for 135 (the compiler's absolute value); the arms as `>=` tests under `\|\|`, 124 to 126; `if (dx >= 0) { if (dx >= BOX) goto on; } else if (-dx >= BOX) goto on;`, 124. |
| `stage.c` `stage_QueueFarCells` | 2 | `if (col >= 0) { mask = -1; if (col >= CELLS) goto skip; goto calc; } else { mask = -1; if (-col < CELLS) { calc: ... } } skip:` | `if (col >= 0 ? col >= CELLS : -col >= CELLS) mask = -1; else if (col < 0) ... else if (col == 0) ... else ...`. Fact 9. Built on the way: the absolute value, 137 for 143; `mask = -1;` ahead and the test the other way round, 144; the three-way choice written in both arms, 140. |
| `cpu.c` `cpu_Decide` | 3 of 8 | `goto record` into the block of the other arm; `goto after_nav` over the rest of a block; `goto pick2` into a block behind two tests nothing passes | `if (goal_kind == CPU_GOAL_OPPONENT ? (...) : (...)) { ... }` (fact 9); an `else`; and `if ((work2 == DROP && d < reach) \|\| (work2 == BLOCKED_STEP && reach < d)) { if (d < reach) { ... } ... }`. The two tests round 2 found "in the bytes, passed by nothing" are the second arm of that `\|\|`: a blocked step never leaps. |
| `fight.c` `fight_UpdateDefeat` | 7 | `if (frames <= N) goto end;` and six `goto end` out of the `switch`, past the `g_sys_screen = SCREEN_REPLAY_INIT` that follows it, to the capping of the win counts | `if (frames > N) fight_AfterDefeat();`: the `switch` is an inline function and the six are `return`s (fact 13). **The same bytes come from the six `goto end` inside the `if`;** nothing shows which the authors had. The inline function's two locals are used nowhere else, so its registers say nothing. |
| `fight_modes.c` `fight_BuildVsCpuOrder` | 4 | `goto insert` over an `if`; `last = X; goto append_last;` in two arms, `goto set_count` past the store | `else if`; and `g_vscpu_order[count] = X; count++;` written in each arm, which the compiler merges (fact 12). |
| `select_team.c` `select_HandleTeamRules` | 4 of 5 | three `goto to_ok` into the last of three cases, and `goto rule_sound` from each of the two nudges | the test of the cross button is written in each of the three cases (merged, fact 12); the second nudge falls into `rule_sound`, which is inside the inner `switch` and ends in `break`. One `goto rule_sound` is left (below). |
| `model_update.c` `model_SetOutline` | 2 of 3 | `loop2: if (...) goto after2; ...; goto loop2; after2:` over the extra nodes, walked by the address of their `draw_flags` | `model_OutlineExtraNodes(model, on)`, an inline function: `for (node = model->extra_nodes;; node++) { if (node->extra == 0) return; ... }`. Fact 10: the loop tests at the top and jumps back from the foot, which a `break` does not give and a `return` does. The `FAKE` note on the raw offsets went with it, because `SceneNode.draw_flags` is an unsigned byte: with `s8` the mask `~NODE_DRAW_OUTLINE` is loaded into a register and lifted out of the loop, the original masks with `andi 0xfe` in place. The whole game builds the same with `u8`. Probably an inline function; a `goto` out of a `for (;;)` gives the same bytes. |
| `resource_weapon.c` `resource_LoadWeapons` | 1 | `goto end` over the call that looks the file up | an `else`, and the call in both arms |
| `fighter.c` `fighter_Update` | 1 | `if (a) { ... } else if (b) { ... } else { goto no_scabbards; }` and ten lines after it | the ten lines are an inline function, `fighter_MarkSheathed`, called in the two arms. Written out twice they match too: the compiler merges them, branches and all (fact 12). |

### Kept: a jump the authors could well have written (6)

| File, function | `goto` | What it is | Built in its place |
|---|---|---|---|
| `light.c` `light_ApplyPointLight` | 2 | `goto off`: no position or no light, and too far away, leave by one way that turns the light off | `if (...) { off } else { ...; if (...) { off } else { on } }` and the same with `continue`: 136 for 135, `dz` and `setup` change registers (the second `off` counts as a use). An inline function that returns whether the light was aimed: 138, the frame 8 bytes smaller. |
| `eff.c` `eff_DrawFlash` | 1 | `goto again`: a clash draws its streak and goes round again for the glow, from inside a `switch` where a `break` cannot leave a loop. Backwards, but over no declaration | `for (;;)` round it with `continue`: 1419 for 1455: the loop lifts addresses out that the original forms each time round (fact 11). |
| `select.c` `select_DrawPortraits` | 1 | `goto draw`: the `default` of a `switch` leaves past the load that follows the switch | `pixels = 0; break;` and a test of it: 220 for 209 |
| `select.c` `select_StageStep` | 1 | `goto end`: triangle leaves the loop over the pads and passes the test of the cross button after it | `break` and `if (i >= n && ...)`: 696 for 690. An inline function with a `return`: 690, other registers; `i` and `stage` are the function's own (fact 13), so it was not that. |
| `motion_decode.c` `motion_DecodeFrame` | 1 | `goto decode`: the first frame of a group starts the loop from step 0 and passes the start made from the frame before | `if (neither cache is the frame before) { first } else { resume }`: 750 for 749, other registers |

### Kept: the bytes have one copy and a jump (7)

In each of these the code jumped to was written out a second time and the
compiler did not merge the two (fact 12), so the original had one copy
reached from two places. That is a `goto` unless the authors had an
arrangement of the tests that was not found. Probably the authors'.

| File, function | `goto` | What it is | Built in its place |
|---|---|---|---|
| `eff.c` `eff_DrawFlames` | 2 | `fire`, `torch`: two cases set their sizes, the first jumps into the second for the rest | the rest written in both cases: 534 for 501 |
| `eff.c` `eff_DrawFlash` | 3 | `streak_size` (one), `centred` (two): the same between cases of a `switch` | written in each case: 1589 for 1455 |
| `select_team.c` `select_HandleTeamRules` | 1 | `rule_sound`: both nudges set the refused sound and share the test that changes it | written in both: 205 for 208; the sound is then picked without a branch, and the original branches (round 2 found the same from the other side) |
| `fighter_hit.c` `fighter_ResolveBladeContact` | 1 | `goto clang`, backwards into the block of an earlier `if`, for the spark and the clang | the two calls written again, plainly and in a `do {} while (0)` like the copy beside it: 227 for 215. The reason for calling a jump into a block the authors': the two calls are in the bytes once and reached from two places with the pending event set differently in each, and the compiler, given a second copy, keeps it. |

### Kept, ours (9 `goto`, 5 new `FAKE` notes)

| File, function | `goto` | The note | Built in its place |
|---|---|---|---|
| `cpu.c` `cpu_Decide` | 5 | for (footwork = 0; footwork < 7; footwork++), and continue for goto pick_next | the `for` with four `continue`s; `do { } while (++footwork < 7)` with them; `for (;;)` left by a `goto`: each 2991 instructions with 121 lines in other registers. Fact 11. |
| `wave.c` `wave_Init` | 1 | for (column = 0; column < WAVE_COLUMNS + 1; column++) | 45 forms (a `for`, a `do`, a `for` with the pointer stepped in it; the counter `column`, a variable of its own, or one of three others; indexed or through `phase` or `p`): 170 for 170 every time. With `column`, 24 lines in other registers, back to the first loop; with a counter of its own, 8: the counter and the pointer of this loop change places. |
| `model.c` `model_BlendPoses` | 1 | do { ... } while (i < MODEL_BODY_BONES + 2); | `do`, `while`, `for (;;)` left by a `goto`: 134 for 132, the compiler steps a second pointer through the hands |
| `model.c` `model_SetOutline` | 1 | for (i = 1; i < MODEL_BODY_BONES; i++) | `for`, `do`, with and without the local `b`: 83 for 82, the compiler steps a pointer to `draw_flags` |
| `hit_test.c` `math_SegmentMeetsCircle` | 1 | if (threshold < z1) return 0; as for x above | seven forms: `if (z2 < -threshold \|\| z1 > threshold) return 0;` as the test of x is written, two `if`s, an `else if`: 146 for 149 (the last test of the first arm is merged with the second arm's); the two arms as one `?:` three ways: 149 with a branch the other way round, or 146. The test of x has the same shape in the bytes and is plain, so what differs is what follows it. |

The four loops are the open question of this round. Fact 11 says the
original's compiler did not see a loop in any of them, and two more
(`model_SetOutline`'s other loop and Sony's `_spu_gcSPU`) turned out to be
loops left by a `return` or a `goto` (fact 10). Loops left that way were
tried for these five and do not match either. A programmer does not write
four loops with `goto` in three files, so the authors' form is something
not found.

### Sony's (25)

A `goto` in a compiled module of Sony's is Sony's if the plain form does
not match. Each was built once.

| File, function | `goto` | What it is | Built in its place |
|---|---|---|---|
| `psxsdk/libc2/prnt.c` `prnt` | 17 | 11 `goto rflag` (a flag is read and the next character looked at), 2 `goto pforw`, 1 `goto number`, 3 `goto nosign`, into the shared end of the conversions: the labels of the BSD `printf` that this is | `for (;;)` round the `switch` and `continue`: 411 for 418. The shared ends written out in every case: 1120 for 418. |
| `psxsdk/libc2/sprintf.c` `sprintf` | 6 | 3 `goto loop_30` (after `h`, `l`, `L`), `goto printDec`, `goto printHex`, `goto end` | a loop of `if`s ahead of the `switch`: 543 for 535; `for (;;)` and `continue`: 535, 179 lines differ; the two ends written out: 627; `out[written] = 0; return written;` for `goto end`: the same instructions but the branch goes one instruction further and takes that instruction into its delay slot |
| `psxsdk/libspu/s_n2p.c` `_spu_pitch2note` | 1 | `goto found` leaves two loops | two `break`s and two tests: 87 for 74 |
| `psxsdk/libspu/s_m_int.c` `_spu_gcSPU` | 1 | `goto scanned` out of a `for (;;)`, to the line after it | `break`: 202 for 194, the test is moved to the foot of the loop and written twice (fact 10); round 1 built a `for` with the test in it, a `while` and a `do`. The `FAKE` note said "break"; the word is dropped: by fact 10 the original left this loop by a `goto` or a `return`. |

## The zeroed variables

Not about `FAKE` notes, but it is where facts 1 to 3 led, and it adds two
facts about the compiler (14 and 15, at the end). 383 lines in `bb2.ld`
placed a variable each, with 23 `. += n` gaps and 41 `ALIGN` lines among
them. Now 300 lines place a variable, with 21 `. += n` and 5 `ALIGN`
lines: every space the two rules below do not give is written as a gap
now, and each has a verdict.

### How the original laid them out

Four runs, in this order, after the last file's initialised small data:

| Run | From | What is in it | Order |
|---|---|---|---|
| each file's small `static`s | `0x800A3308` | a file's own variables of eight bytes or fewer (`.sbss`) | the files' link order; in a file, the order declared. Sony's assembly modules come last: sixteen bytes each |
| the small common variables | `0x800A3670` | every variable of eight bytes or fewer that is not `static` and has no initial value, whichever file defines it | the linker's (below) |
| each file's larger `static`s | `0x800A3930` | a file's own variables of more than eight bytes (`.bss`); for Sony's modules, built without small data, all of them | the files' link order, each file starting on an 8-byte boundary; in a file, the order declared, each on a 4-byte boundary |
| the larger common variables | `0x800F1B10` | the rest | the linker's |

The compiler writes a `static` without an initial value as `.lcomm` and
any other as `.comm`; the first is the file's own storage, the second is
the linker's to place. That is the whole difference between the runs, and
it was tested: the 57 larger variables of 19 of the game's files and the
15 of 7 of Sony's modules are now `static`, in the order of their
addresses, and each file's `.bss` puts them where the original has them
with no line each in `bb2.ld`. Each is used by one file only, which is
what `static` needs, with one exception: `g_models` is used by `model.c`
and `model_update.c` and sits between their variables, so those two are
one file of the original's (the files round's to join; it has a line of its own
until then). The compiler writes out a `static` that nothing uses (built:
`static char unused_one[0x80];` between two that are used comes out
between them), so a gap in a file's run is a variable the file declares
and never uses, or uses only in code that was removed.

No file before `font.c` in link order has a larger `static`: the fight,
the menus and the modes keep their state in common variables, and the
engine's files (`font.c` to `menu_pages.c`) keep theirs `static`. Two
hands, or two habits.

### The linker's order: found

**A common variable is placed in the order of a number made from its
name: the sum of the name's characters plus its length, modulo 512** (or
the plain sum modulo 507, 508 or 509: the evidence fits both and nothing
else that was tried). Each run of common variables is one walk from 0 up.

The test, on names that are certainly the original's: the larger run has
40 of Sony's variables among 42 of the game's, and Sony's names are known
from its libraries. In the order of their addresses those 40 names give
6, 7, 11, 12, 47, 52, 75, 95, 142, 149, 152, 153, 192, 206, 227, 232, 242,
249, 253, 273, 348, 351, 358, 362, 366, 369, 386, 407, 430, 437, 457, 460,
465, 466, 478, 483, 488, 506, 510, 511: rising without an exception, from
`_svm_okon1` to `_svm_okof2`. 40 things have 8 x 10^47 orders. Tried and
excluded, as having an inversion at every modulus from 2 to 2048: the sum
of the upper-case or lower-case letters, the sum without the first
character, the exclusive-or of the characters, a multiply-by-31 hash. The
plain sum fits only at 507 to 509 and the sum plus the length only at 512
to 515; 512 with the length is the likelier (a table of 512, and the name
as the object file keeps it, a length byte and the characters).

What it says about our names: nothing directly. The rule depends on the
name, the game's original names are not known, and ours do not follow it
(108 of 207 neighbours in the small run rise by our names, 17 of 41 in
the larger: chance). It is not evidence for any name of ours and was not
used as such. What it gives is a test for a candidate original name, for
the names round: a name proposed for a larger variable has to give a number in
the window its Sony neighbours leave, and the numbers of all the game's
names in a run have to rise in the order of the addresses.

| The game's variables, in address order | Number, with the length, mod 512 | Plain sum mod 509 |
|---|---|---|
| `g_motion_decoders` | 11 to 12 | 7 to 8 |
| `g_render_skin_colors` | 12 to 47 | 8 to 43 |
| `g_shared_buffer` | 47 to 52 | 43 to 48 |
| `g_camera_second`, `g_cloth_scratch_save` | 52 to 75 | 48 to 71 |
| `g_render_tiny_ot_db`, `g_fighter_body_points`, `g_light_setups` | 75 to 95 | 71 to 89 |
| `g_team_scores`, `g_camera_main`, `g_scene_settings` | 95 to 142 | 89 to 137 |
| `g_math_rot_matrix_funcs` | 142 to 149 | 137 to 144 |
| `g_cloths`, `g_replay_object_draws` | 153 to 192 | 148 to 188 |
| `g_disp_db`, `g_model_tiny_matrix` | 192 to 206 | 188 to 199 |
| `g_pad_packets`, `g_camera_offset`, `g_render_view_matrix` | 206 to 227 | 199 to 221 |
| `g_node_identity_root` | 232 to 242 | 226 to 238 |
| `g_snd_stage_vab_buffer` | 242 to 249 | 238 to 245 |
| `g_render_tilt_matrix`, `g_replay_events`, `g_node_camera_root`, `g_cdrom`, `g_fighters`, `g_motion_common_sections`, `g_fight_config`, `g_pad_state`, `g_team_rosters`, `g_motion_fighter_sections`, `g_team_weapons` | 273 to 348 | 266 to 341 |
| `g_memcard_file_list` | 358 to 362 | 353 to 355 |
| `g_render_queue` | 369 to 386 | 364 to 377 |
| `g_reloc_tables`, `g_reloc_counts` | 407 to 430 | 400 to 426 |
| `g_object_dirt`, `g_snd_mode_vab_buffer`, `g_saved_record`, `g_object_pool` | 437 to 457 | 428 to 451 |
| `g_vscpu_order` | 466 to 478 | 458 to 470 |
| `g_snd_prop_sound_positions` | 488 to 506 | 480 to 498 |

The small run has no name of Sony's in it (Sony's libraries were built
without small data), so it has no windows, only the order.

What the rule explains: why neighbours are not one array (fact 3: two
names that differ by one in their last character give numbers one apart);
why a variable nothing names can sit anywhere (it has a name, and the
name has a number); and why the game's variables and Sony's are shuffled
together.

### The linker's alignment: found

**A common variable is put on a boundary that goes by its size: 8 for
eight bytes or more, 4 for four to seven, 2 for two or three.** With that
rule in `maspsx` (it had 8 for exactly eight bytes) the larger run needs
no `ALIGN` line at all (82 variables, three gaps left) and the small run
needs three, each a link packet (below). Before, the two runs had 36.
Five to seven bytes go on 4, not 8: `g_team_results`, five bytes, is at
`0x800A377C`.

### The gaps

Of the 23 `. += n` there were: 6 are gone because a type was too small
or two variables were one, 2 because the variable is declared now, 1 was
the end's rounding; 4 are the storage of Sony's assembly modules; 10 are
left as "a variable, or several, that no code names" (6 among the files'
own, 4 among the common ones), two of them smaller than they were. Seven
more spaces that were `ALIGN` lines are gaps now, because the rule does
not give them: 2 of Sony's and 5 unnamed small variables. "Unnamed" means
a variable that no instruction names: it has a name in the original and
a place by that name. The table has every space the old list had, `ALIGN`
lines too, except those that are only the size rule.

| Address | Bytes | Was | Verdict | Evidence |
|---|---|---|---|---|
| `0x800A3308` | 8 | gap | left: one or more small `static`s of `main.c`, `cloth.c` or `pad.c` that no code names | it is in the files' own run ahead of `motion_decode.c`'s, and those three are the files before it; an unused `static` is written out |
| `0x800A338C` | 12 | gap | left: small `static`s of `light.c`, `math.c`, `render.c` or `stage_props.c` | between `light.c`'s and `math_sin.c`'s; probably the same file as the `0x1C0` below |
| `0x800A361C` | 4 | `ALIGN` | Sony's: `PATCH`'s storage is 16 bytes | the overlay has the module with the same space |
| `0x800A3630`, `..3C`, `..4C`, `..5C` | 8, 12, 12, 12 | gaps | Sony's: each assembly module (`SENDPAD`, `CHCLRPAD`, the card's and the GTE's patches) has 16 bytes, of which its code names one or two words | the overlay has the first two with the same spaces |
| `0x800A366C` | 4 | `ALIGN` | the same, or the run of common variables starts on 8 | cannot be told apart |
| `0x800A3672` | 6 | `ALIGN` | gone: `g_render_view_tilt` is 8 bytes, not 6 | on an 8-byte boundary, and the byte variable after it starts 8 later; three angles and room for a fourth, an `SVECTOR`'s shape |
| `0x800A3691`, `0x800A36BC` | 7, 4 | `ALIGN` | kept as `ALIGN(8)`: a link packet is one object of 8 bytes | two of the three packets are on a boundary only an 8-byte object gets. This is the layout's word on a kind A note (the packets' second words are variables of their own in our source): the note is right that it is ours |
| `0x800A36A6` | 2 | `ALIGN` | left: unnamed, one variable of 2 bytes or two of 1 | the next is a byte and needs no boundary |
| `0x800A36FE` | 10 | gap of 8 | left: unnamed, 8 bytes from `0x800A3700` (one of 8, or two of 4), perhaps 2 before | a 4-byte variable follows and would have gone at `..3700` |
| `0x800A3741` | 3 | `ALIGN` | gone: `g_sys_unused_boot_flags` (was `D_800A3744`) is 4 bytes, not 3 | three bytes would have been put on 2, at `..3742` |
| `0x800A37B1`, `0x800A38EA` | 3, 2 | `ALIGN` | gone: the henchmen's tint and chanbara's are each one 4-byte object (`CVECTOR`), not three bytes | each starts on a 4-byte boundary and is followed by one unused byte, twice; as three variables each would need unnamed bytes before it of just the right size. The code is the same either way. Probably |
| `0x800A37CA` | 6 | `ALIGN` | left: unnamed (a 4-byte one at `..37CC`, or smaller ones) | |
| `0x800A37D1` | 1 | `ALIGN` | left: unnamed byte, ahead of the two win counts | the counts as `u8[2]` would explain it and do not match (below) |
| `0x800A37E2` | 6 | `ALIGN` | left: unnamed (a 4-byte one at `..37E4`), ahead of the blow's direction | the direction as an `SVECTOR` would explain it and does not match (below) |
| `0x800A38A5` | 3 | `ALIGN` | left: unnamed, 1 to 3 bytes | the next is 2 bytes and would have gone at `..38A6` |
| `0x800A46D0` | 0x80 | gap | a `static` of `stage.c` that nothing uses, between the fog's two tables and the tiles: `g_stage_unused_block` (was `D_800A46D0`) | it is inside `stage.c`'s run; not a longer table (0x80 is not a multiple of a level's 0x30) |
| `0x800A93C8` | 0x468 | gap | left: unused `static`s at the end of `cloth_model.c` or the head of `model.c` | a multiple of 8, so the file boundary cannot be placed |
| `0x800A9B24` | 4 | `ALIGN` | a file's start: `light.c` begins on 8 | every one of the 19 files does; inside a file the boundary is 4 |
| `0x800A9B38` | 0x1C0 | gap | left: unused `static`s of `light.c`, `math.c`, `render.c` or `stage_props.c` | `math.c` and `render.c` have no other larger `static`, and the 12 small bytes above are in the same place |
| `0x800EEE48` | 0x228 | gap | 0x10 gone (the backdrop's node is 0x68 bytes); 0x218 left: unused `static`s of `stage_hooks.c` or `wave.c` | |
| `0x800EF0C8`, `0x800FF690`, `0x80101E48` | 0x10 each | gaps | gone: the four nodes that stand alone are 0x68 bytes | every node in the game is: `ModelNode`, `StageNode` and `StageProp` are a `SceneNode` and 0x10 more. `PlainNode` in `node.h` |
| `0x800EF160`, `0x800EF1F0` | 8, 0x368 | gaps | gone: the surf's two rows of vertices are rows 0 and 1 of one array of 8 rows of 18 | `wave_Draw` reads 18 vertices of a row, three at a time, and writes 17; 8 x 18 x 8 bytes is exactly the room; 8 is `WAVE_ROWS` |
| `0x800EF97C`, `0x800EFAE4`, `0x800EFB34` | 4 each | `ALIGN` | a file's start (`resource_weapon.c`, `cutscene.c`, `snd.c`) | as above |
| `0x800F184C`, `0x800F1854` | 4 each | `ALIGN` | a module's start (`PUTCHAR`, `SYS`) | Sony's modules start on 8 too |
| `0x800F1898` | 4 | gap | a `static` of libgpu's `SYS` that none of its code names, after `clr_prims`: `clr_prims_unused` | the overlay has the module with the same 4 bytes |
| `0x800F19C4` | 12 | gap | 4 are the next module's start; 8 left: a `static` of a module after `BIOS` (to `VM_INIT`) that no code names | the overlay has `BIOS` and not the 8 bytes |
| `0x800F62D8` | 8 | gap | left: probably `g_fighter_body_points` is 8 bytes longer than the 0x370 the code reaches | an unnamed common variable of 8 bytes or fewer would be in the small run |
| `0x800F664C` | 4, then seven variables | `ALIGN` | gone: the seven 2-byte variables at `0x800F6650` are one object of 16 bytes, `g_scene_settings` | a 2-byte common variable of the game's is in the small run; these are among the larger ones, in a row, set one after another by `sys_InitEngineState`, with 2 bytes spare. `CameraView` is not 4 short: the 4 bytes after it are this object's boundary |
| `0x800FF5E4` | 0x2C | gap | gone: `g_camera_offset` is a `CameraView` (0x44) | it had its first 0x1C bytes declared, "laid out like the start of a `CameraView`"; the room is a whole one |
| `0x80101BC0` | 4 | gap | left: probably `g_snd_stage_vab_buffer` is 4 bytes longer than the 0x2518 the code allows | as for `0x800F62D8` |
| `0x80101EA8` | 0x20 | gap | left: an unnamed variable of 0x20 bytes (a `MATRIX`'s size), or `CdromState` is 0x70 | its number would be between `g_cdrom`'s and `g_fighters`' |
| `0x801078DC` | 4 | gap | the end of the zeroed part is rounded up to 8 | `ALIGN(8)` now |

Left unexplained, plainly: 15 spaces (the 10 and the 5) are known only
as far as "a variable, or several, that no code names", and for three of
them "or the variable before is longer". Which file owns the three large
ones (0x468, 0x1C0, 0x218) and the two small ones (8, 12) is the files round's
question. Nothing reads any of them.

### The pairs the game indexes from the first

| Variables | By the layout | By the code | Verdict |
|---|---|---|---|
| win counts, `g_fight_wins_p1` / `_p2` | on an even address after a byte: one 2-byte object, or an unnamed byte before them | as `u8 g_fight_wins[2]` `cpu_Init` keeps the address in a register (fact 2): 106 bytes differ | two variables. The code decides |
| the blow's direction, x / y / z | on an 8-byte boundary with 2 bytes after: an `SVECTOR`, or an unnamed 4-byte variable before them | as an `SVECTOR`, `fighter_TakeHit` reads `vx` through the register that holds the address, where the original forms the address again | three variables |
| the story's wounds, the team's points and fouls | nothing: each follows a variable that leaves it where it would be anyway | two variables each (the kind A round) | two variables each |
| the two tints | each a 4-byte object | the same either way | one `CVECTOR` each, probably |
| `g_motion_loaded_sets` | one 4-byte object: on a 4-byte boundary with 2 bytes free before it; two 2-byte variables would be at `..38C2` and `..38C4` | two notes reach the second element through a pointer, which is what an array's address in a register looks like (fact 2) | one array of two, as declared. The two `FAKE` notes stay: the plain form was built in round 2 |
| the link packets' second words | one 8-byte object each | (the kind A round: as one struct `link_ExchangePad` changes) | one object by the layout; the note stays |

### `SceneNode`, `CameraView`, `NODE_EXTRA`

- Every node is 0x68 bytes. The scene's own code reads the first 0x58;
  the last 0x10 are the owner's. So `SceneNode` stays 0x58 as the part
  every node shares, and the four nodes with no owner are `PlainNode`.
- Is the word after a model's scene node a member? The layout cannot say
  more than that the room is part of every node. The code says: three
  functions store it through a plain pointer (fact 7), and for the other
  stores and every read the two forms give the same code. So it is
  probably reached by a pointer past the node everywhere, as `StageNode`
  and `StageProp` suggest too (each owner lays its own sixteen bytes over
  the same room); `ModelNode.extra` is left a member where it compiles
  the same. For the types round.
- `CameraView` is 0x44 or 0x48: all three are followed by something on
  an 8-byte boundary. Left at 0x44.

### Two more facts about the compiler

14. **A signed 16-bit variable that is given a value from memory inside a
    condition, and used afterwards, costs 8 bytes of frame that nothing
    reads or writes: once for each copy of the condition.** `if ((c = *p)
    != 0)`, `?:` and `&&` cost 8; `while ((c = *p++) != 0)` and `for`
    cost 16, the condition being written at the head and the foot; `for
    (;;) { if (!(c = *p++)) return ...; }`, the same test under a `do`,
    and a loop made with a label cost 8. None is left by `c = *p; if (c
    != 0)`, by an unsigned or 8-bit or 32-bit variable, by a call's result,
    or when the variable is not used after the test. (13 statements by 8
    types were built; the slot is the one `reload` gives a 16-bit value
    that was also used as a 32-bit one and then merged away.)
15. **Such a slot is above every local the function declares**; declared
    locals fill the frame from the bottom in the order declared.

By 15, of the locals named `unused` at the end of this page:

| Could be such a slot (it is the last thing in the frame) | Cannot be (a declared local is above it): a local the authors declared |
|---|---|
| `cloth_CreatePiece` (8), `hud_DrawTeamScores` (8), `menu_DrawPovChoice` and `menu_DrawSideViewChoice` (16), `stage_BuildFogLevels` (8), `cutscene_KaishakuUpdate`'s second (8), `_Pad1` (8), the overlay's `movie_DecodeFrame` (8) | `camera_BeginFightScene` and `camera_BeginReplayScene` (8, below the view), `model_SetOutline` (a `MATRIX`), `object_Launch` (a `VECTOR`), the four texture loaders (0x20), `cutscene_KaishakuUpdate`'s first (16), `menu_DrawChanbara` (8), `motion_LoadFighterFiles` (256) |

Round 2's reading of `cloth_CreatePiece` holds as far as this: the
slot is real, it is where a slot would be, and the function has a signed
16-bit count read in a loop's condition. The line that leaves one slot
and not two was looked for again and not found: `for (;;) { if ((count =
*src++) == 0) break; ... }` gives the original's instructions and a frame
8 bytes too large (two slots); the assignment moved into the test after
the loop (`if ((count = *src) != 0)`) leaves no slot and reads the word
signed; the `do` form of fact 14 is not this loop (the count is run down
inside it). No `unused` local was removed. One was tried in a function of
the other column, to check 15: `camera_BeginFightScene` with a 16-bit
copy of the screen number assigned in its test and no `unused` has a
frame 8 bytes too small.

### The overlay

`movovl/movovl.ld` had 10 gaps. Seven are the rules above (a module
starts on 8; a variable of 8 bytes or more is on 8) and are `ALIGN(8)`
now, or gone into the module's own `.bss`; three are the 16 bytes of
Sony's assembly modules. Its common variables looked like two runs, one
before the game code's larger variables and one after, in no order our
names gave. The names round explains it ("Names against outside evidence", below): with Sony's names
it is one walk in the order of the names, and the game's two variables
in the middle of it are common variables too.

## Types and casts

A cast is often a type that is wrong somewhere else. The casts of the
game's files (`src/main/*.c`, `include/*.h`) and of the overlay's own
(`movovl/src`, `movovl/include`) were counted by kind, the compiler was
asked about each, and the declarations the answers pointed at were
changed. `tools/casts.py` counts; a second script (not kept) took
each cast out and compared the compiler's output.

| Kind | What it is | Game, before | Game, now | Overlay |
|---|---|---|---|---|
| int-call | `(s16)f(...)`: an integer cast on a call's result | 17 | 8 | 0 |
| int-member | `(s8)p->m`: an integer cast on a member | 139 | 16 | 0 |
| int-other | `(s32)x`, `(u32)(a - b)`: any other integer cast | 727 | 283 | 0 |
| ptr-struct | `(Fighter *)p`: to a pointer to a structure | 188 | 178 | 0 |
| ptr-scalar | `(u8 *)p`, `(s32 *)p`: to a pointer to a number or `void` | 473 | 401 | 8 |
| ptr-arith | `(T *)((u8 *)p + n)`: something reached by arithmetic | 44 | 43 | 1 |
| ptr-hw | `(T *)0x1F80...`: a hardware register or the scratchpad | 23 | 24 | 0 |
| fnptr | a cast to a function pointer | 4 | 3 | 0 |
| **all** | | **1,615** | **956** | **9** |

The overlay's nine are unchanged: each hands a typed buffer to a
parameter Sony declared `u_long *` or `u_char *` (`LoadImage`, `CdRead`,
`CdControl`, `StGetNext`, `DecDCTout`).

### What the compiler was asked

Each test compiles a changed copy and compares the assembly the compiler
writes with the original copy's; a warning counts as a failure, as in the
build.

1. **Each cast taken out by itself.** Of the 1,459 in the game's `.c`
   files, 315 gave the same code with no warning. 313 are gone (the other
   two are in the `va_arg` macro of `font.c` and `render.c`, left as Sony's
   `stdarg.h` has it). Of the 919 there now, 674 cannot go because the
   types differ without them, and 243 change the code.
2. **A local given the type its casts name** (`s32 x` with `(s16)x` at its
   uses becomes `s16 x`). 38 locals took it with the same code (eight of
   them are now written with the plain comparison of fact 17 instead); 12
   did not
   (`cpu_Decide`'s `work2`, the roots' temporaries, `hud_DrawBestTimes`'s
   two counters, `scene_frame.c`'s two shears among them). The same test on
   locals that are pointers: about 70 candidates, none passes; each is used
   as two types.
3. **A parameter given the type its function casts it to**, in the
   definition and every prototype, compiling every file that names the
   function. 20 took it: `du`, `dv` and `dcx` of the four `tex_ShiftPoly`
   functions (`s16`), `select_DrawTeamHeading`'s `heading`, the group index
   of `tex_LoadStageGroup` and `tex_LoadGroupCluts`, and five that are
   unsigned (`menu_SetEnabledRows`, `menu_UpdateTop`, `stage_QueueFogMoves`
   twice, `math_TableSqrtPositive`). Four did not, and that is evidence:
   with `s16` in the prototype of `cdrom_StartRead`, `tex_LoadGroup`,
   `tex_LoadGroupAt` or `tex_LoadGroupAtSplit` the *callers* change (fact
   16). So their prototypes said `int` and the function narrows the value
   itself: the cast, or an old-style definition like `tex_LoadTim`'s, is
   the authors'.
4. **The signedness of every 8- and 16-bit member** declared in the game's
   headers, flipped one at a time (535 members). 325 change the code and
   17 no longer compile (a comparison becomes constant): 342 are proved by
   the code. 138 give the same code either way, so nothing decides them;
   55 are named by no code. `SceneNode`: `draw_flags`, `mesh`,
   `mesh_block`, `matrix_state` and `rotation_order` are proved; `kind` and
   `light_setup_index` are not decidable.

   | Header | Proved | Either way | Not named |
   |---|---|---|---|
   | `fighter.h` | 144 | 20 | 24 |
   | `eff.h` | 9 | 42 | 15 |
   | `motion.h`, `replay.h` | 42 | 15 | 2 |
   | `cpu.h`, `cloth.h`, `collision.h` | 42 | 14 | 5 |
   | `sprite.h`, `select.h`, `menu.h`, `hud.h` | 44 | 16 | 1 |
   | `node.h`, `model.h`, `mesh.h`, `stage.h`, `scene.h`, `light.h` | 32 | 26 | 7 |
   | `fight.h`, `snd.h`, `memcard.h`, `arena.h`, `narration.h`, `ndata.h`, `pad.h`, `tex.h` | 29 | 5 | 1 |

5. **Integer functions that return no value.** Of 264, four never returned
   one: `fight_UpdateDistance` is `void` now (the same
   code everywhere); `snd_Play`, `fighter_TryDropSecondSword` and
   `eff_SetUpParticles` are integer functions by their callers' code (fact
   4). 93 return a value that no caller uses directly (32 of them are the
   effects' handlers, called through the table); the value is in the bytes.

Three facts about the compiler came out of it, measured the same way as
the others.

16. **A narrow parameter in a prototype makes the callers extend the
    argument as well as the function.** So a function whose callers pass a
    value unextended was declared to take an `int`, whatever it does with
    it inside.
17. **`x >= a && x <= b` is compiled as `(unsigned)(x - a) <= b - a`, `x <
    a || x > b` as the opposite, and a signed `x / 2` as `(x + (x >>> 31))
    >> 1`.** Every `(u32)(x - 7) < 2`, `(u32)x >= n` and `x + ((u32)x >>
    31)` that was rewritten as the plain comparison or division gave the
    original's code: about 40 places in 14 files, none failed.
18. **`x = *((u16 *)p)++` reads and then steps the pointer by two, one
    instruction each**: the same code as `x = *(u16 *)p; p = (u32
    *)((s32)p + 2);`. A structure read through one pointer gives loads at
    offsets and a single step, which is not what `tex.c`'s loaders have.

### Declarations changed

| What | Before | Now | What went |
|---|---|---|---|
| `FightConfig`: `roster`, `weapon`, `is_cpu`, `stage`, `mode`, `first_person`, `every_hit_kills` | `u8` | `s8` | 56 `(s8)` casts; one `(u8)` came, where `fight_LoadFighter` tests the weapon against 0xFF |
| `g_eff_handlers` | `s32[62]`, each entry `(s32)eff_...` | `void *[62]` | 62 casts. The handlers have three return types by their own code (`u8`, `s32`, none), so no function type fits them all; the two calls still cast to `EffHandler` |
| `tex.c`'s five loaders | `p = (u32 *)(((s32)p) + 2); x = *(u16 *)p;` | `x = *((u16 *)p)++;` | the file's 145 casts are 65 (fact 18, and the casts that changed nothing) |
| `gpu_info.disp_env` (Sony's `SYS`) | `DISPENV`, read through eight `(volatile s16 *)` | `volatile DISPENV` | 8 casts and a `FAKE` note; both programs match |
| `arena_SetMoveCallback` | `s32 on_move` | `ArenaMoveFn on_move`; the type's first parameter is `s32` like the callbacks' | 8 casts |
| `math_RotMatrixZXY`, `YXZ`, `XYZ` | `u16 *angles` | `SVECTOR *angles`, like `ZYX`; the angles are read into `u16` locals | 6 casts: three in the table of the four and three at calls; `g_render_view_tilt` is an `SVECTOR` (it was `u16[4]`) |
| the stage casts' work area (`collision_CastWith`, `collision_CastMoveWith`) | `s32 work_addr` | `SegmentCast *`; `FIGHT_CAST_WORK` and `SideViewScratch.cast_work` have the type | 7 casts; two more were retargeted and one is in the macro |
| `model_GetBoneMatrices`, `model_GetBones`, `stage_GetNodes`, `menu_SetPanelColor`, `menu_SetBackdropColor`, `math_InvertMatrix` | `void *` in and out, with a typed copy inside | `MATRIX **`, `ModelNode *`, `StageNode *`, `TILE *`, `POLY_F4 *`, `MATRIX *` | the copies; `fighter_StartBleeding`'s `s32 mat[8]` is a `MATRIX` |
| `narration_RelocateFile`, `select_RelocateTeamFile` | `s32 *file`, lists named `file[6]` | `NarrationFile *`, `TeamSelectFile *`, lists named by member | 22 casts, with `sprite_RelocateList(void *file, void *list)`: it serves four file types |
| `cloth_Distance`, `cloth_DistanceFar`, `math_LerpFramePoint`, `math_LerpFramePointPolar` | `s32 in[3]` passed as `(VECTOR *)` | `VECTOR` locals | 10 casts |
| `math_SegmentMeetsCircle` | `s32 *p1, s32 *p2` | `Vec3 *` | 6 casts: every caller had one |
| `g_hud_best_times_bar`, `g_hud_best_times_row_bar`, `g_fight_lock_frames`, `g_camera_replay_down_frames` | `u16` | `s16` | 4 casts |
| `g_hud_noise_seed`, `g_eff_flame_frame` | `s32` | `u32` | 7 casts |
| `snd_SetStageReverb`'s `result` | `s32`, and `SsUtSetReverbType` called through a cast | `s16`, called plainly | the kind A note "called as if it returned an int" |
| `fight_UpdateDistance` | `s32`, returning nothing | `void` | |
| `g_sys_data_start` (was `D_8008D070`) | `s32[8]` in `main.c`, `u8[]` in Sony's `snmain.c` | `s32` in both | the only object the tree declared with two types |

### What the earlier rounds handed over

| Item | What was done | Verdict |
|---|---|---|
| `NODE_EXTRA`: is the word after a model's scene node a member; should the owners' sixteen bytes be one node type | `ModelNode.extra` built as a union member and `NODE_EXTRA` as that member: `weapon_QueueWorn` is 8 bytes shorter. A union member's store is a member's store (fact 7) | not a member in the three functions, and not a union laid over one node type. The cast is the authors'. `extra` stays a member where both forms match |
| `SceneNode`'s other byte and short members | test 4 above | five proved, two not decidable |
| `SceneSettings.unk0A`, `unk0E` | every listing searched for the object's offsets: +0xA is written once, by `sys_InitEngineState`; nothing names +0xE | still not known; the names stay |
| the two tints as `CVECTOR` | three bytes of four are used; a `u8[4]` gives the same code and the same place | still probably |
| `_spu_memList` `volatile` | declared a pointer to `volatile`: the main program no longer matches | not `volatile` as a whole; the one read in `SpuMalloc` is, and the note stays |
| `PutDispEnv`'s saved environment | the member declared `volatile` | fixed: both programs match with no cast |
| `fighter_TakeHit`'s `object_kind` in two variables | the parameter and one local: 2,260 bytes for 2,296, the copy is merged | left: two locals, as now |
| `cpu_Decide`'s 16-bit best score | test 2: `work2` as `s16` differs | left (round 2's result stands) |
| `story_RunScript`'s pointer to the roster place | the pointer is `s8 *` now, as the bytes it points at are | left: the pointer is in the bytes, and no layout of `FightConfig` puts the weapon two bytes after the roster place by a name |
| the 18 `unkNN` uses and 30 `D_` names | 8 uses of `unkNN` members now and 57 declared; every `D_` name searched in every listing: 20 are named by no function, 9 by one or two that only clear them (`cloth_Init`, `cloth_ClearSceneColliders`, `sys_InitEngineState`, `snd_Init`, `menu_Load`, `select_Load`, `staff_LoadRoll`, `sys_InitGame`, `cutscene_Init`), `D_8008D070` by Sony's `__main` | none renamed then: nothing reads any. Since: 25 named for what is known (`tools/maps/unnamed.tsv`), and 3 gone: `D_800A33FC` (`collision.c`) and `D_800A34F4` (`eff.c`) were the hole before the next file's start on 8, an `ALIGN(8)` each now; `D_800A31FD` (`memcard_save.c`) was the byte skipped ahead of the halfword after it |
| prototypes | tests 3 and 5, and the table above | 20 parameters, 6 `void *` interfaces, one `void` return; the casts to a function pointer are 3: `AddPrim` (the kind A round's), `menu_UpdateWhitePage` called with no arguments, and `font_Printf` called with one |
| `font_Printf` | as `font_Printf(const u8 *, ...)`: 572 bytes for 584. The original stores its second parameter back to its place in the frame, so it has a name | left: the definition is the authors', and a call with the text alone had no prototype in sight |
| the kind B notes about a type | read again with the types in view | none went (see "Kind B notes that are about a type") |

### The casts that are left

Looked at by group, not one by one.

- **An address kept as an integer** (most of the 674 that cannot go). The
  engine holds the addresses of file images, primitive buffers and work
  areas in integers and adds byte counts and file offsets to them:
  `hud.c`'s `prims` and the `SpriteDraw` and `PrimCursors` members
  `sprt_out`, `ft4_out`, `sprt`, `ft4` (about 170 uses as a number, 5 as a
  pointer), `resource.c`'s section offsets (24 of the 43 `ptr-arith`),
  the cutscene's `block_base`, the sound banks' load addresses. One side
  of each use has to cast; the integer is kept where the arithmetic is.
  `hud_Draw...`'s `prims` (19 callers cast `g_disp_prim_cursor` to it) was
  not changed for that reason. Not built the other way.
- **A primitive written a word at a time** (`eff.c`, 44; `font.c`):
  `*(s32 *)&quad->x0 = sxy[0]`. The word stores are in the bytes. A
  structure of words would need casts for `code` and the colour bytes
  instead. Not built.
- **Sony's parameter types**: `LoadImage`'s `u_long *` (28), `StoreImage`,
  `CdControl`'s `u_char *`, `SetTransMatrix` handed "the matrix 0x14 below"
  a vector (3; `eff.h` says why).
- **Two types in the original**: `menu_DrawTitleRule((MenuCtx *)ctx, ...)`
  from the select screens (4), whose context is a word shorter (the
  comment at the call says what that does); `fighter_TakeHit`'s blade
  number passed as an object's kind.
- **A store through a plain pointer** (fact 7): `NODE_EXTRA`, `hud.c`'s
  `TILE_` macros (round 2: as members, 190 instructions for 192).
- **A value narrowed where it is used**: the four parameters of test 3, the
  roots' `(u32)` (rounds 1 and 2), `(u16)g_link_..._pad_word`.
- **Unions** (15): seven are two halves that are also written as one word
  (`Model.load_flags`, `TeamPair`, `KeyConfigCounts`, `SelectPicks`,
  `key_cursors`, the cloth spring's pair, the effect's id); the word store
  is in the bytes, and whether the authors had a union or a cast is not
  known. The rest are the scratchpad's work areas and a file type with two
  layouts. Not tried.

### What was not looked at

- The 243 casts that change the code and the 674 that the types need were
  not gone through one at a time; `ptr-scalar` (401) least of all.
- Casts in the headers' macros and inline functions (13) were counted and
  not tested; Sony's library code was not counted, and was touched only
  where it meets the game (`libgpu/sys.c`, `libsn/snmain.c`, and
  `libspu`'s list, tried and left).
- The signedness test was run on members only. Variables outside
  structures were tried where they carried a cast (seven; six changed,
  `g_menu_cursors` did not); 32-bit members and variables not at all.
- No member that test 4 calls "either way" was changed: nothing says
  which it is.
- The tests for locals, parameters, members and return types were scripts
  in the round's scratch folder and are gone with it; what each did is
  above.

## The 31 notes the first sweep dropped as wrong

Commits `7de72e452` and `1b7930484` dropped the word from 31 notes and
left the code alone. All 31 were looked at again. All were right to go;
none is restored. One (`fighter_TryDropSecondSword`) had moved to another
file and is now fixed properly.

| Notes | Where | Why it is not fake |
|---|---|---|
| 10 | `hud.c`: the draw functions' `end_addr` (`hud_DrawTimeAndCount`, `hud_DrawWinCounts`, `hud_DrawPointsAndTime`, `hud_DrawEndingPicture`, `hud_DrawKillCount`, `hud_DrawStageCardMark`, `hud_DrawStageCard`, `hud_DrawBestTimes`, `hud_DrawTeamScores`, `hud_DrawCaption`) | Each ends in a `subu` of the end address less the start: the subtraction is in the bytes, where a constant size would be a `li`. Checked in all ten listings. |
| 5 | `menu_pages.c` (`menu_DrawTopRows`, `menu_DrawOptions`, `menu_DrawKeyConfig`), `select.c` (`select_MoveWeaponCursors`, twice): "loads again what it already holds" | The load is in the bytes and the source is a plain assignment; there is no construct to call fake. |
| 2 | `eff.c`: `eff_SetUpParticles`, `eff_UpdateParticles`, the work area's members through pointers | Every effect function reaches the scratchpad through a pointer for each member (the globals `eff_BindScratch` sets are the same thing); the pointers are in the bytes as registers and frame slots. |
| 2 | `s_sva.c` `SpuSetVoiceAttr`, `spu.c` `_spu_Fw1ts`: `volatile` counters | The listing loads and stores both counters in the frame on every turn of the loop; in C only `volatile` does that. |
| 1 | `cpu.c` `cpu_Decide`: `(fighter->cpu_level * (ANGLE_TURN / 16)) >> 12` | `cpu_level` is a 4.12 fixed-point number (`cpu_Init` caps it at 0x1000); this is a fixed-point product. |
| 1 | `cpu.c` `cpu_Decide`: `attack_head` | A second note on one construct: the note at the assignment is still there (kind B). |
| 1 | `menu_pages.c` `menu_LoadLinkPicture`: `memory` | A local initialised where it is declared. |
| 1 | `resource_weapon.c` `weapon_Queue`: `ground_y = 0` | The initial value of a variable that one branch reads. |
| 1 | `select_team.c` `select_DrawTeamPanel`: `ot_base` set again | The same `if`/`else` written twice; the stores are in the bytes. |
| 1 | `snd.c` `snd_RebaseVab`: `s16 ret` | A result variable with one `return`; nothing unnatural. |
| 1 | `sprite.c` `sprite_DrawRotated`: offsets that are always 0 | The body of `sprite_DrawScaled` reused, where a mirrored mode changes them. |
| 1 | `stage.c` `stage_ReadCells`: `i` counts the list | A counter used again. |
| 1 | `pad.h`: `pad_Update()` without a prototype | The pause menu calls it with no argument and the main loop with one; only a declaration without a prototype allows both. |
| 1 | `snd.h`: `snd_Play` returns `int` | Fact 4 above; `fighter_TryDropSecondSword` is the same. |
| 1 | `snd.c` `snd_CompactSpuMemory`: the loop that only sets `addr` | The loop is in the bytes. |
| 1 | `fighter.h`: `fighter_TryDropSecondSword` has no declaration | The note had moved to `fight_lock.c`; fixed in this round (below). |

The first sweep also turned 19 frame pads into plain unused locals. Those are
listed at the end of this page: they are honest about being unused, but
their types are guesses.

## Kind A, fixed (11 notes)

| Notes | Where | Before | Now, and the evidence |
|---|---|---|---|
| 1 | `cloth_model.c` `cloth_CreatePiece` | a wrapper `struct { ClothDesc desc; s32 tail[3]; } init;` | `ClothDesc desc;` and `s32 unused[2];`. Fact 1: the frame has 0x20 bytes of locals, the description takes 0x14 and the next local starts 8-aligned at 0x18. `ClothDesc` is 0x14 bytes, as `cloth_Create` and `cloth_Build` read it. |
| 3 | `fight.c`, `fight_modes.c`, `globals.c`: the team battle's points and fouls | `fight.c` declared `u8 g_team_points[2]`, `fight_modes.c` four single bytes, `globals.c` defined four | One declaration in `fight.h`: four variables. `fight.c` reaches the second from the first, `(&g_team_fouls)[side]`, as the win counts do (see D). Fact 2: `fight_modes.c` forms each one's address at every use. |
| 1 | `fight_event.c` `fight_RaiseEvent` | `((void (*)())replay_RecordEvent)(...)`, as if undeclared | `replay_RecordEvent(s32 variant, s32 kind, ...)`: with `int` parameters the caller passes the words as they are and the function's own bytes are the same. They were declared `u8`. |
| 1 | `main.c` `disp_DrawLoadingScreen` | `((s32(*)())hud_DrawStageCardMark)(...)` | `hud_DrawStageCardMark(s32 place, ...)`: the same; it was `s16`. |
| 1 | `fight_lock.c` | a private `int fighter_TryDropSecondSword();` against the `void` definition | The function is `s32` in `fighter_hit.c` and `fighter.h` and returns nothing. Fact 4: a prototype with `int` matches, so it was the result type and not the missing prototype. All 13 calls in `fighter_hit.c` still match. |
| 1 | `motion.c` `motion_LoadFighterFiles` | `u16 sets_to_load[130]`, two used | `u16 sets_to_load[2];` and `char unused[256];`. Fact 1 gives the same frame; a 256-byte buffer nothing uses (a file name's, at a guess) is likelier than 128 spare halfwords. |
| 1 | `stage.c` `stage_BuildFogLevels` | `u8 far_color[0x10]`, three used | `CVECTOR far_color;` (`gte_ReadFarColor` writes three bytes; it now takes a `CVECTOR *`) and `s32 unused[2];`. |
| 1 | `psxsdk/libapi/pad.c` `_Pad1` | `volatile s32 i[3]` | `volatile s32 i;` and `s32 unused[2];`. |
| 1 | `psxsdk/libcd/bios.c` `CD_cw` | `cd_com_clears_ready[com + 0x40]`: one table indexed to reach the one two after it | The four tables are one object, `static struct { ... } cd_com`, and the loop reads `cd_com.nparam[com]`. Fact 2: the original reaches `nparam` at 0x100 from the register that holds `clears_ready`, which it can only do inside one object. (A two-dimensional table would do the same; the struct keeps the four names.) |

The three locals named `unused` that these added are in the list at the
end, with the first sweep's.

## Kind D (18 notes dropped)

| Notes | Where | Why it is not fake |
|---|---|---|
| 3 | `fight.c` `fight_UpdateDefeat` (twice), `fight_after.c` `cutscene_KaishakuUpdate`: "the two win counts are not an array" | They are not, and the code is the original's. Tried as `u8 g_fight_wins[2]`: `fight_UpdateDefeat` and `cpu_Init` then keep the array's address in a register, where the original forms `g_fight_wins_p1`'s at each use (fact 2). So the counts are two variables and the original reaches the second from the first; fact 3 is why that could work. What the authors wrote for it is not known; `(&g_fight_wins_p1)[winner]` is the plain reading. Probably. |
| 1 | `fighter_hit.c` `fighter_MarkStoryWound` | The same. The first sweep built it as an array and the code that names each byte came out different; not built again here. Probably. |
| 3 | `fighter_hit.c` `fighter_TakeHit`, `object.c` `object_HitFighters` (twice): the blow's direction as one vector | The same. The first sweep built it as one `SVECTOR` and as an array and `fighter_TakeHit`, which names x, y and z, came out different; not built again here. Probably. |
| 4 | `eff.c` `eff_DrawFlames`, `eff_DrawSplashes`, `eff_DrawDust`, `eff_UpdateParticles`: `SetTransMatrix` handed "the matrix 0x14 below" a vector | The original does exactly that: `addiu $a0, <the vector's register>, -0x14` in each. What lies 0x14 below differs from one function's layout to the next (the work area's start in two, 4 bytes in for the dust), so there is no matrix the authors could have named. Probably their own trick for setting a translation from a vector. |
| 1 | `cdrom.c` `sys_ExecMovieOverlay`: `s32 argv[8]`, six passed | The frame has 32 bytes there. By fact 1 an `argv[6]` would need another local after it; an array of eight with six used is the simpler reading and an ordinary thing to write. Probably. |
| 1 | `snd.c` `snd_FlushQueue`: `(s32)g_snd_vabs[...] < 0` for "is open" | The original tests the sign; `!= 0` would be a different instruction. |
| 1 | `fighter_hit.c` `fight_TestBladesOnBodies`: the blade's number passed as `object_kind` | The parameter has two meanings in the original: `fighter_TakeHit` tests it against 0 when no object struck, and uses it as a pointer when one did. The cast says so. |
| 2 | `psxsdk/libetc/intr.c` `startIntr`: `_96_remove(pCallbacks)` | The listing loads `pCallbacks` into `$a0` just before the call, which in C is an argument. The BIOS routine takes none; Sony's call passes one all the same. |
| 1 | `movovl/src/movie.c` `movie_DecodeFrame`: `u8 unused[8]` | An unused local like the others; listed with them at the end, where the tree's convention is that they carry no `FAKE`. |
| 1 | `cutscene.c`, over the file's first constants: "the literal in the printf below, padded to whole words" | The note was left behind when the string went back into `cutscene_Start`'s `printf` as a literal, which is how it matches; nothing there is shaped. The last clean-up took the note away. |

## Kind A, left (16 notes)

| File | Function | The note | What was tried, and the verdict |
|---|---|---|---|
| `camera.c` | `camera_SwingToFighter` | the x, y and z of foot_pos, copied as one | Three member assignments: the original loads the three words and then stores them, a 12-byte structure copy out of a 16-byte `VECTOR`. `foot_pos` is copied whole as a `VECTOR` in `camera.c` too, so both types are in the bytes; the cast is the plainest thing that says so. Left, probably the authors' own. |
| `cloth.c` | `cloth_Sqrt` | s32 lz; the array gives the caller its stack frame | Round 1: `s32 lz[1]`, `lz[2]`, and one word with `s32 unused[2]` change `cloth_BeginStep`; `lz[3]` matches as `lz[4]` does. So the function has 9 to 16 bytes of locals whose address is taken, in one object (fact 1), and only the first word is known. Left. |
| `cloth.c` | `cloth_SolveSprings` | s32 lz; the array gives the function its stack frame | Round 1: one word, one word with `unused[4]`, two words with `unused[2]` all differ; `lz[5]` matches as `lz[6]` does: 17 to 24 bytes in one object. Left. |
| `cloth.c` | `cloth_Step` | s32 lz[2]; the longer array gives the function its stack frame | Round 1: `lz[2]`, and `lz[2]` with `unused[2]`, differ; `lz[5]` matches: 17 to 24 bytes in one object. Left. |
| `cpu.c` | `cpu_BuildPad` | u8 bits[4] = {0xd, 0xf, 0xc, 0xe}, a local's initialiser | The first sweep: as a local with an initialiser the four bytes go to `.rodata`; the original has them in small data. The compiler stage of this build runs `-G0`, so it cannot be said in C here. Left. |
| `cpu.c` | `cpu_Decide` | u8 walks[4] = {...}, a local's initialiser | As `cpu_BuildPad`. Left. |
| `cpu.c` | `cpu_Decide` | u8 steps[4] = {...}, a local's initialiser | As `cpu_BuildPad`. Left. |
| `fight_start.c` | `(file)` | "TEAM", "VS" and so on, without the padding. | The short names are in small data, where this build's compiler stage (`-G0`) puts no literal; as literals they go to `.rodata`. Left. |
| `globals.c` | `(file)` | no variables of their own; reached from the first words | As `link.c`: these are the second words. |
| `hud.c` | `hud_DrawPanel` | (s16)cursor_and_options | `selected_row = cursor_and_options;`: the argument then lives in a register and the frame is 8 bytes larger. The original has it in memory from the start, as when its address is taken. Left. |
| `hud.c` | `hud_DrawTeamScores` | side_scores[0] = side_scores[1] = 0 | `side_scores[1] = side_scores[0] = 0;`: two halfword stores, the original has one word store. Left. |
| `hud.c` | `hud_DrawBestTimes` | AddPrim(...), not called as if it were undeclared | The plain call: the compiler then uses `$v0` for the cursor before the result is formed. Only a call to an `int` function gives the original, and Sony's `AddPrim` is `void`. Left. |
| `link.c` | `(file)` | the second word reached from the first, not named | One `s32[2]` for each packet (and `bb2.ld` to match): the address of an array is kept in a register between uses, the original forms the first word's at every use and reaches it through `$gp`, the second by its full address. So the first word is a variable of its own that `link.c` defines, and the word after it is reached from it. What the second word is declared as is not known. Left. |
| `menu_pages.c` | `(file)` | the top menu's locals, laid out as its stack frame has them | Separate locals: `MenuCtx ctx`, eight unused bytes and `pressed` as a variable come out where they are; the word at +0x48 does not: as a local of its own its store is deleted, so it belongs to something whose address is passed on. The frame is: the context, 8 bytes, then 12 bytes (`pressed`, a word nothing touches, the last row). Left. |
| `psxsdk/libapi/pad.c` | `SetPatchPad` | one record of four words, patch0 | `u32 patch0[4]`: the stores to the first and last words then go through the register that holds the second's address; the original names each. Left. |
| `psxsdk/libc2/sprintf.c` | `sprintf` | the end of buf, reached from the locals that follow it | Round 1: `buf + sizeof(buf)`, `&buf[0x200]`, `&buf[sizeof(buf) - 1] + 1` and `(char *)&info` all give 510 instructions for 535: what the stand-in does is take the address of `args`, which puts the argument pointer in the frame, where the original has it. So the original takes that address somewhere (its `va_arg`, at a guess); where is not known. Left. |

## Kind B, left (101 notes)

The last column is what was built in place of the stand-in and how the
code then differed: round 1's, or round 2's where it begins "round 2:".

| File | Function | The note | What was built |
|---|---|---|---|
| `camera.c` | `camera_FindHidingCell` | shift_a = MATH_SQRT_SHIFT - (lzc & ~1) | round 2: the plain line built: the count is loaded into `$v0` and -2 into `$v1`, as in `fight_HoldDistance` |
| `cloth.c` | `cloth_SolveSprings` | shift = MATH_SQRT_SHIFT - (lz[0] & ~1) | round 2: `shift = 0x16 - (lz[0] & ~1)`: the same instructions, the sum in `$a1` for `$a0` and the -2 loaded ahead of the count |
| `cloth.c` | `cloth_SolveSprings` | the shift is held in `len` and the table's root in `sum` | round 2: locals `shift` and `root` of the block's own: the root in `$a0` for `$v1`, the count loaded later |
| `cloth_model.c` | `cloth_CreatePiece` | kind = MESH_FACE_KIND(flags), and the tests are on `kind` | round 2: `kind = MESH_FACE_KIND(flags)` and the tests on `kind`: 262 instructions for 263, other registers |
| `cpu.c` | `cpu_Decide` | some thirty locals of their own, not four shared ones | round 2: `work4` alone as three locals (the follow-up, the waypoint, the script): 2989 instructions for 2991, the frame 8 bytes smaller, `fighter` in `$s1`. `work1` to `work3` were not split |
| `cpu.c` | `cpu_Decide` | work2 < pick_score | round 2: without the cast the `sll`/`sra` pair is gone (2989 for 2991), so the best score is a 16-bit variable in the original; as an `s16` local of its own: 2989, the frame 8 bytes smaller |
| `cpu.c` | `cpu_Decide` | 1, in a local | the literal: the same instructions, the registers of 1 and -1 change places |
| `cpu.c` | `cpu_Decide` | entry_cursor itself | round 2: `entry_cursor` at both uses: 2990 for 2991, two saved registers change places |
| `cutscene.c` | `cutscene_Advance` | one local for the camera's turned z and each fighter's | round 2: a local of the macro's own block: 402 for 403, the multiplications in another order |
| `eff.c` | `eff_LoadTextures` | `buffer`, copied | round 2: `buffer` itself: the saves of `$s1` and `$s2` change places |
| `eff.c` | `eff_DrawMuzzleFlashes` | the work area, held in the quad pointer | round 2: a `MuzzleFlashWork *` local: 419 for 420, the work area in `$v0` and not in the quad pointer's `$s1` |
| `eff.c` | `eff_DrawFootprints` | the work area, held in the quad pointer | round 2: a `FootprintWork *` local: 436 for 437, in `$v0` and not `$s2` |
| `eff.c` | `eff_DrawFootprints` | `bit` is not used again | without `bit`: two instructions fewer and a saved register less. So a variable does hold the bit; why nothing reads it is not known |
| `eff.c` | `eff_BuildFootprintMatrices` | mask = 1 << i | in one step: the frame is 8 bytes larger |
| `fight.c` | `story_HenchmanShout` | &g_fighters[1].world_pos.x | round 2: named: the address is formed anew and `$s0` is gone (46 for 47); the original makes it from the register that holds `&g_fighters[0].character` |
| `fight_event.c` | `snd_PlayAt` | shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) | round 2: the plain line built: as `fight_HoldDistance` |
| `fighter.c` | `fight_UpdateLock` | locals of their own for the runner and the other | round 2: block locals `winner` and `loser`: the same count, other registers |
| `fight_modes.c` | `fight_InsertBestTime` | g_saved_record.times, not through a pointer | named directly: 53 instructions for 47 |
| `fighter.c` | `fighter_UpdateRun` | work and speed, reused for the drag, the size, the step | round 2: locals `drag` and `step`, the size read in the product: 160 for 159, the drag in `$v1` |
| `fighter.c` | `fighter_Update` | four locals: a turn limit, an angle, a turn, the pad's side | round 2: four locals: 2982 for 2983, a branch turned round, other registers |
| `fighter.c` | `fighter_Update` | the kind, read ahead of the test | read in the test: two more `nop`s |
| `fighter_hit.c` | `fighter_ResolveBladeContact` | through a local | assigned directly: the branch is the other way round |
| `fighter_hit.c` | `math_TableSqrt` | the leading zeros, then the shift made from them | round 2: `0x16 - (lzc_out & ~1)` in one step: in `fight_PushBodiesApart` the count goes to `$a0` and the -2 is loaded ahead of it |
| `fighter_hit.c` | `fighter_TestShot` | a copy of length, then a byte of the table of roots | `math_TableSqrt` and `math_TableSqrtPositive` at both roots: the same instructions, but the copy of the number is made ahead of the test and the original makes it after |
| `fighter_hit.c` | `fight_TestBladesOnBodies` | &g_fight_blow_dir_x, written where it is used | written at its use: the address goes to a saved register |
| `fighter_hit.c` | `fight_HoldDistance` | shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) | see "The root of an unsigned number" above: some 35 forms and one permuter run; the best leaves one register different |
| `fighter_hit.c` | `fight_HoldLockDistance` | shift = MATH_SQRT_SHIFT - (leading_zeros & ~1) | round 2: the plain line built: as `fight_HoldDistance` |
| `fighter_hit.c` | `fighter_TestBodyInSweep` | kept in the local the test's result goes in | round 2: three `continue`s: 170 for 179 and the frame 8 bytes smaller, so the flag is in the bytes; a local `outside` of its own: the same instructions, the flag in `$a1` and not in `hit`'s `$s0` |
| `fighter_pose.c` | `math_LobVelocity` | kept in dy, which is free | round 2: the difference written in the expression: other registers |
| `hit_test.c` | `hit_FrameTriangle` | a number, then the byte of the table of roots it leads to | `math_TableSqrt` at the three roots: the frame is 8 bytes smaller (the calls share one slot for the count); at two of the three: the slots change places |
| `hit_test.c` | `math_SegmentMeetsCircle` | stored again | `disc_root = math_TableSqrt(disc_root)`: differs |
| `hit_test.c` | `hit_SphereMeetsTriangle` | b_y - a_y kept in the parameter, which is free | round 2: a local `dz`: the multiplications in another order |
| `hit_test.c` | `hit_SphereMeetsTriangle` | stored again | `sqrt_val = math_TableSqrt(circle_radius_sq)`: differs |
| `hit_test.c` | `math_MatrixToAngles` | the determinant, then a square root | two variables; the root as `ratan2`'s argument: a division is lost |
| `hit_test.c` | `math_TurnAngles` | the determinant, then a square root | as `math_MatrixToAngles` |
| `hud.c` | `hud_DrawLoadingNoise` | a second name for the value on its way into draw | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | as long_work | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | SPRITE_SCALE_ONE | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | 1 | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | &g_hud_loading_sheets[0][LOADING_SHEET_LONG_STREAK] | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | the long streak's sheet of this set | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | long_work holds the y base, then the cursor | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `hud_DrawLoadingNoise` | as long_work | the whole function written plainly (a `for`, no temporaries, the constants as literals): 178 instructions for 176. The two constants alone: other registers |
| `hud.c` | `(file)` | ((TILE *)(tile))->r0 and so on | round 2: `((TILE *)(tile))->r0` and so on: 190 for 192 in `hud_DrawBestTimes`, the stores in another order |
| `hud.c` | `hud_DrawBestTimes` | the bar grows by one, and its width needs no row_work | round 2: `bar = row_bar + 1` and the width in one expression: the test after it uses the register where the original reads the variable again |
| `hud.c` | `hud_DrawBestTimes` | as row_work | round 2: built; as the first |
| `link.c` | `link_ExchangePad` | one variable for the local buttons and, later, the outgoing ones | round 2: a local `sent` for the outgoing buttons: 199 for 200, the saved registers change |
| `link.c` | `link_ExchangePad` | 0 held in a variable | the literal: other registers |
| `link.c` | `link_ExchangePad` | type = word >> 16, read through a pointer instead | round 2: `word >> 16`: 196 for 200; the original reads the halfword at +2 (`lh`) |
| `main.c` | `fight_RunPauseMenu` | a copy of the parameter; use paused_frame_db | the parameter itself: it is saved in another order |
| `main.c` | `disp_DrawLoadingScreen` | one local for the number of noise streaks and the stage's mark | round 2: a block local for the streaks: 134 for 136, the stage's mark then in `$a0` and not `$s1` |
| `memcard.c` | `memcard_CountFreeBlocks` | used / CARD_BLOCK_SIZE, written out with the shift in a local | `used / CARD_BLOCK_SIZE`: the instructions come in another order |
| `memcard_save.c` | `memcard_PackSaveData` | assigned here and not where it is declared | round 2: initialised at its declaration: the copy is made at the top and not after the first loop |
| `menu_pages.c` | `menu_Load` | the work area's members, through pointers | round 2: the members named: 109 for 108, other registers |
| `menu_pages.c` | `menu_StepNudge` | 1 in a local | the literal: an instruction moves |
| `menu_pages.c` | `menu_DrawRowUnderline` | draw->header, through a pointer | round 2: stored directly: 196 for 199 |
| `menu_pages.c` | `menu_DrawTopRows` | ctx->prims.offset++, not as a member | round 2: `ctx->prims.offset++`: 551 for 552, a load moves ahead |
| `menu_pages.c` | `menu_DrawMessageText` | sheets[message * 2 + i], added in another order | round 2: `sheets[message * 2 + i]`: 100 for 103, the frame 8 bytes smaller (the original keeps `message * 8` in the frame) |
| `menu_pages.c` | `menu_DrawKeyConfig` | a constant kept in a local | round 2: 0 and 0x80 written at each use: the same count, the 0x80 in `$t0` and its first store earlier |
| `menu_pages.c` | `menu_DrawKeyConfig` | zeroes kept in 16-bit locals | as literals: the frame is 8 bytes smaller, so something is there; but one of them is passed as a `RECT *`, so 16-bit variables are not it |
| `menu_screen.c` | `menu_StartFight` | the two constants in locals; read them as the constants | the literals: two instructions change places |
| `model.c` | `model_BuildShadows` | &model->bones[i + 1].node and g_model_bones.bones[i].shadow_mesh | round 2: both named: 38 for 36, the mesh indexed and not walked |
| `model_tiny.c` | `model_LoadTiny` | `i`, the tiny model's number above, is reused to count | round 2: a local `id`: `$a0` and `$a2` change places |
| `model.c` | `model_Update` | `bone` is the root here, and each extra node's bone further down | round 2: the root named where it is used: 270 for 272 and the frame 8 bytes smaller, so a variable holds the root; a variable `root` of its own: `$s5` and `$s6` change places |
| `model.c` | `model_Update` | the frame is read through this copy of the parameter | round 2: `frame[row * 3 + n]`: the same count, saved registers change |
| `model.c` | `model_BlendPoses` | named here, where the loop above writes scratch + 1 | round 2: `scratch + 1` at each use: `$s0` and `$s3` change places |
| `model.c` | `model_SetOutline` | the test goes through `b`, which holds blue further down | round 2: `if (bone->node.mesh >= 0)`: the load of blue moves behind red and green |
| `fighter.c` | `motion_ForgetFiles` | g_motion_loaded_sets[1], through a pointer | the array named plainly: its address goes to a saved register. As two `u16` variables every function of `fighter.c` gives the original's instructions with plain names (fact 2), but the two then lie at 0x800A38C2 and C4, and the original has them at C4 and C6 with two bytes free ahead, as one 4-byte object would be. Not split; the layout round |
| `fighter.c` | `motion_DropChanbaraPack` | g_motion_loaded_sets[1], through a pointer | as `motion_ForgetFiles` |
| `fighter.c` | `motion_LoadFighterFiles` | two locals: a character, then a place in g_motion_set_files | round 2: a local `file` for the place in the table: it is in `$a1` and not `$s0` |
| `narration.c` | `narration_Draw` | g_narration_file->picture_2_pixels[i], by address | round 2: indexed: 467 for 468; the original walks an offset |
| `narration.c` | `narration_Draw` | &g_narration_file->lines[i], summed as integers | round 2: `&g_narration_file->lines[i]`: the operands of one `addu` change places |
| `object.c` | `object_ThrowDirt` | the constant, kept in a local | the literal: other registers |
| `psxsdk/libapi/pad.c` | `_IsVSync` | ret = 0 | `ret = 0` alone: three instructions fewer |
| `psxsdk/libcomb/comb.c` | `_comb_control` | ret = CombWaitCallback; CombWaitCallback = param; | named plainly: the address is formed twice. The original keeps it in a register, as for a structure or an array (fact 2), though the file's other uses form it each time |
| `psxsdk/libgpu/sys.c` | `SetDrawEnv` | out itself | round 2: `out` itself: the saves of `$s0` and `$s1` change places |
| `psxsdk/libgpu/sys.c` | `SetDrawEnv2` | out itself | round 2: as `SetDrawEnv`, built |
| `psxsdk/libsnd/vs_vh.c` | `SsVabOpenHeadWithMode` | _svm_vab_used[vabid], not through p | indexed plainly: differs |
| `psxsdk/libspu/s_m_m.c` | `SpuMalloc` | not volatile | the plain read: one load fewer. The second load is in the bytes; whether the list is `volatile` is the types round's |
| `psxsdk/libspu/spu.c` | `_spu_init` | _spu_RXX->rxx.spucnt = 0xC000 | the plain member store: another register |
| `render.c` | `reloc_UnrelocateBlock` | entry++, with a step forward and back before it | `entry++` alone: two registers change places |
| `render.c` | `reloc_KeepSelectedEntries` | a copy of the parameter | round 2: `file` itself: the saves of `$s3` and `$s4` change places |
| `replay.c` | `replay_PlayFrame` | two locals, the move's section and the frames left to play | round 2: a local `section`: 525 for 526, the frame 8 bytes smaller |
| `resource.c` | `resource_OnModelMoved` | `slot` reused for the bank id | round 2: the id as the argument: `$s0` and `$s1` change places |
| `resource.c` | `resource_LoadStage` | block_end = cells | round 2: `block_end = cells`: saved registers change |
| `resource_weapon.c` | `resource_LoadWeapons` | `i` counts both loops and then holds the file number | round 2: a local `file`: the counter goes to `$a2` |
| `resource_weapon.c` | `weapon_Queue` | `where` loses its HELD bit here and gets it back below | round 2: a local for `where` without the bit: 196 for 197 |
| `resource_weapon.c` | `snd_LoadWeaponVabs` | tail_addr = pack_table_addr + tail_off, in two steps | round 2: in one step: `$s7` and `$s8` change places |
| `resource_weapon.c` | `snd_LoadWeaponVabs` | `~x >> 31` is `x >= 0`. | round 2: `slot0_selection >= 0`: 124 for 126 |
| `select.c` | `select_MoveWeaponCursors` | weapon_rects + side * 8, in two steps | round 2: in one step, here and where it is worked out again: 809 for 810, `rects` and `pad` change registers |
| `select.c` | `select_MoveWeaponCursors` | the address of the picture, through a local | round 2: the picture as the argument, and an inline function for the load (all three): 806 for 810 |
| `select.c` | `select_MoveWeaponCursors` | the address of the picture, through a local | round 2: the picture as the argument, and an inline function for the load (all three): 806 for 810 |
| `select.c` | `select_MoveWeaponCursors` | the address of the picture, through a local | round 2: the picture as the argument, and an inline function for the load (all three): 806 for 810 |
| `select_team.c` | `select_LoadTeam` | a store nothing reads | without it: another register |
| `snd.c` | `snd_SetStageReverb` | `reverb` is g_snd_stage_reverbs[stage] and `depth` stage * 2 + 1. | round 2: `g_snd_stage_reverbs[stage]` and `stage * 2 + 1`: 47 for 46; the original doubles the stage ahead of the test |
| `stage.c` | `stage_QueueFog` | g_stage_fog_settings[stage][1], with no index | round 2: `g_stage_fog_settings[stage][1]`: read once and ahead of a store |
| `stage.c` | `stage_BuildFogLevels` | each colour blended in one expression | round 2: one expression each: other registers, the multiplications in another order |
| `stage_props.c` | `stage_UpdateProps` | a StageNode pointer of its own for the stage piece | round 2: a `StageNode *` block local: 206 for 203, the frame 8 bytes larger |
| `stage_props.c` | `stage_CutProp` | a StageNode pointer of its own for the stage piece | round 2: a `StageNode *` local: the prop in `$a2` and not `$s0` |
| `tex.c` | `(file)` | block = tim + 2, after the two stores | round 2: `p = tim + 2` after the stores: 67 for 68 |
| `tex_scroll.c` | `tex_QueueScroll` | rect.h = scroll_phase below, with no h2 | round 2: without `h2`: 231 for 232 |

## Kind C, left (14 notes)

| File | Function | The note | What was built |
|---|---|---|---|
| `movovl/sdk/libcd/c_004.c` | `data_ready_callback` | without the empty loop the load of StRingIdx1 comes before | round 2: built without it: the load of `StRingIdx1` (then `st_write_slot`) comes first and the two stores change places, as the note says |
| `cloth_model.c` | `cloth_CreatePiece` | while ((count = *src++) != 0) | round 2: the plain `while`: **the same instructions**, and a frame 16 bytes larger (8 without `unused`). `count = *src++` ahead and at the foot, `for (;;)` with a `break`, a `for`: `lhu` for `lh`. See "A frame slot nothing uses" above |
| `cpu.c` | `cpu_Decide` | no do-while(0) round it | without it: two registers change |
| `fighter.c` | `fighter_StartMove` | an empty statement | without it: two registers change places |
| `fighter.c` | `fighter_StartMove` | fighter->move_kind = head[MOTION_HEAD_KIND] | without the `do`: two registers change places |
| `fighter_hit.c` | `fighter_ResolveBladeContact` | the loop | without it: two saved registers change places |
| `main.c` | `fight_RunPauseMenu` | do-while(0); read as two plain calls | without it: a saved register less |
| `memcard_save.c` | `memcard_SaveScreen` | this arm and the next are empty; read it without them | without the two arms: four instructions fewer, so the two tests are in the bytes |
| `menu_pages.c` | `menu_Load` | an empty block; read it as not there | round 2: without both: `li v0,5` moves in three places |
| `menu_pages.c` | `menu_Load` | an empty block; read it as not there | round 2: without both: `li v0,5` moves in three places |
| `menu_pages.c` | `menu_StepNudge` | the loop, wrapped in a do {} while (0) | round 2: without it: two registers change places |
| `psxsdk/libc2/sprintf.c` | `sprintf` | a plain if, with no do-while(0) around it | round 2: a plain `if`: 531 for 535 |
| `psxsdk/libgpu/sys.c` | `PutDispEnv` | if (env->disp.h > ...) mode \|= 0x24; | `if (!(a <= b))`, a `?:` in each arm, `mode \|= ... ? 0 : 0x24`: four to nine instructions more |
| `select_team.c` | `select_HandleTeamRules` | set in both cases; read it as set once at rule_sound | round 2: set once at `rule_sound`: 205 for 208, the sound is then picked without a branch |

## A listing (1 note)

`movovl/sdk/libcd/c_011.c`, `dma_execute`: The SDK audit: within about 70 instructions of 100 in C; see the note.
## Kind B notes that are about a type

These are sorted B because the bytes they stand for are right and only the
spelling is odd, but each reaches something through a cast or a raw offset
and the types round (types) should look at them with the A's that were left:
`eff.c` `eff_DrawMuzzleFlashes` and `eff_DrawFootprints` (the work area held
in the quad pointer), `fight.c` `story_HenchmanShout` (the second fighter's
place at 0x536 from the first's `character`), `hud.c` the `TILE_` macros,
`link.c` `link_ExchangePad` (the pad type read through an `s16 *`),
`menu_pages.c` `menu_DrawTopRows` (`*(s32 *)&ctx->prims.offset`),
`narration.c` `narration_Draw` (a pointer summed as integers), `stage.c`
`stage_QueueFog`, `stage_props.c` (a `StageProp *` used for a `StageNode`),
`story.c` `story_RunScript` (the weapon reached from the roster's pointer),
and the SDK's `volatile` casts in `libgpu/sys.c` and `libspu/s_m_m.c`.
Round 1 adds `NODE_EXTRA` (`model.h`): is the word after a model node's
scene node a member? The `goto` round made `SceneNode.draw_flags` an unsigned byte
(see `model_SetOutline` under "The `goto`s"); the other byte members of the
scene's types were not looked at that way. And `fighter.c`'s two loaded sets, which the code names
as two variables and the layout places as one object (the layout round).

The types round looked at each with the types in view; the verdicts are in its
table ("What the earlier rounds handed over"). One went: `PutDispEnv`'s
`volatile` casts, by declaring the member `volatile`. In each of the
others the odd spelling stands for something the bytes have (a store
through a plain pointer, a pointer kept in a register, a halfword read)
and no declaration was found that gives it.

## Unused locals that size a frame (not `FAKE` notes)

A local named `unused` says truthfully that nothing uses it. Its size is
known only within the 8 bytes of fact 1, and its type is a guess. The first sweep
made 19 of these out of frame pads; the kind A round added four and took the word
`FAKE` off one. The layout round sorted them into those that can be room the
compiler left for a value it merged away and those that are locals the
authors declared (facts 14 and 15, under "The zeroed variables"). For the types round.

| File | Function | The local |
|---|---|---|
| `cloth_model.c` | `cloth_CreatePiece` | `s32 unused[2]` (this round) |
| `fight.c` | `camera_BeginFightScene`, `camera_BeginReplayScene` | `s32 unused[2]` |
| `fight_after.c` | `cutscene_KaishakuUpdate` | `s32 unused[4]` and, after the blood's position, `s32 unused_2[2]` |
| `hud.c` | `hud_DrawTeamScores` | `s16 unused[3]` |
| `menu_pages.c` | `menu_DrawPovChoice`, `menu_DrawSideViewChoice` | `s32 unused[4]` |
| `menu_pages.c` | `menu_DrawChanbara` | `s32 unused[2]` |
| `menu_pages.c` | `menu_DrawBestTimes` | `s16 digits[6]`, two used |
| `model.c` | `model_SetOutline` | `MATRIX unused` |
| `fighter.c` | `motion_LoadFighterFiles` | `char unused[256]` (this round) |
| `object.c` | `object_Launch` | `VECTOR unused` |
| `resource_weapon.c` | `weapon_QueueWorn` | `SVECTOR offset` |
| `stage.c` | `stage_BuildFogLevels` | `s32 unused[2]` (this round) |
| `tex.c` | `tex_LoadGroup`, `tex_LoadGroupAt`, `tex_LoadGroupAtSplit`, `tex_LoadStageGroup` | `u32 unused[8]` |
| `psxsdk/libapi/pad.c` | `_Pad1` | `s32 unused[2]` (this round) |
| `movovl/src/movie.c` | `movie_DecodeFrame` | `u8 unused[8]` |

The list is what a search for `unused` finds, with `menu_DrawBestTimes`
from the first sweep's notes; the first sweep's other pads were turned into locals with a
use-shaped name (`SpriteDraw s;`) and are not marked.

## The original's source files

Not about `FAKE` notes either: where one of the authors' files ended and
the next began, as far as the bytes show it. Six rules mark a file; each
was tested by building. The game's code was in 66 files here and is in 60
(57 of C, 3 of assembly). The marks prove 24 boundaries (18 between two
files of ours that are neighbours, 6 somewhere in a run of them), so at
least 25 files; they deny none of the 59 boundaries that are left; and 41
of the 59 have no mark of their own.

### What marks a file

1. **A jump table is on an 8-byte boundary counted from the start of its
   file's `.rodata`, and a file's `.rodata` starts on 4.** So all the jump
   tables of one file are on the same grid (their addresses are all 0 or
   all 4 modulo 8), two tables on different grids are in different files,
   and a padding word before a table means the thing before it is the same
   file's (a new file would have started where the padding is). Known
   since the build first matched; what is new is reading it backwards.
   Three padding words were written out here as a constant or a line in
   `bb2.ld` because the file boundary was in the wrong place; all three are
   gone with the joins below, and the build still matches, which is the
   test.
2. **The whole game was compiled `-G8`, and at `-G8` the compiler writes
   all of a file's strings before all of its jump tables.** (It holds the
   functions' code back until the end of the file; the strings go out as
   they are met.) So a string after a jump table begins a new file, and a
   string used by a later function than a table's, lying before that
   table, puts the two functions in one file. Five instances, each of them
   built: `memcard_Format`'s `"bu%1d%1d:"` before `memcard_ProbeCard`'s
   table; the debug menu's `"%sCR%3d\n"` and `"%sCO%3d\n"` before
   `fight_StartMode`'s; `fighter_TestShot`'s `"ILLEGAL GUN MOTION : %d\n"`
   before the tables of three earlier functions; the save file's name
   (defined at the end of `memcard_save.c`) before that file's nine
   tables; `sprite_Draw`'s `"warning\n"` before `select_Load`'s. The test
   of "the whole game": every file of the game's C was compiled both ways
   and compared function by function. At `-G8` all 57 give the original's
   code; at `-G0` eight do not (`cdrom.c`, `fight_start.c`, `fighter_hit.c`,
   `light.c`, `memcard.c`, `select.c`, `stage_hooks.c`, `memcard_save.c`
   by its strings). The `Makefile`'s list of fifteen `-G8` files is gone.
   One array said otherwise and is explained: `snd.c`'s code reaches the
   four bytes of `g_snd_stage_vab_ids` as it would a large variable, and
   the four bytes are in `.data`, not in the small data. Both follow if
   the array is another file's, compiled without small data, and `snd.c`
   saw `extern u8 g_snd_stage_vab_ids[];` with no size (built: with the
   size the code changes, without it it matches). So `snd_data.c` is a
   file of the original's, and the other data files probably are (below).
3. **A string of eight bytes or fewer is small data**, like any object of
   that size; the others are `.rodata`. **Identical strings in one file are
   one string.** The debug menu prints `"%s%3d\n"` three times and
   `"%s%5d\n"` twice and has one of each; `memcard.c` uses `"bu%1d%1d:%s"`
   twice and has one. No string of the game's `.rodata` occurs twice, so
   pooling marks no boundary. `tools/maspsx` moves a small string constant
   to `.sdata` now (the compiler here does not), and ends a file's
   `.rodata` on 4.
4. **A file reaches a small variable through `gp` only if it defines it**
   (known; `globals.c` exists because of it), and only in the plain form:
   an indexed access, an access at an offset and taking the address go by
   the full address even in the defining file. So a variable two functions
   reach through `gp` puts them in one file, and one reached through `gp`
   here and plainly by full address there puts a boundary between.
   `g_memcard_timeout_counter` is reached through `gp` from `memcard.c` and
   from `memcard_probe.c` (both defined it): one file.
5. **A file's `.sbss` and its `.bss` start on 8; inside, a variable is on
   4** (the layout round, for `.bss`). For `.sbss` it holds for all 22 files that
   have one, and 10 of them needed padding to get there, which chance
   would give once in a thousand (12 with `snd.c` and `menu_pages.c`,
   whose holes were written as an unused word of the file before). So where a file's statics end off 8 and
   the next begin on 8 with a hole between, there is a boundary; a hole of
   8 or more is a variable no code names, of one file or the other, and
   proves nothing.
6. **`.text`, `.data` and `.sdata` carry no mark of their own.** Code is on
   4 everywhere. A file's `.data` and `.sdata` start on 4 and so does every
   object in them. What they give is order: every section lists the files
   in the same order, so a mark in one section is a mark in all of them
   between the same two functions, and that is how the table below is
   read.

**`static` functions leave no mark.** Built: `font_DrawChar`,
`font_DrawString`, `cdrom_Reset`, `cdrom_ReadyCallback` and
`cdrom_UpdateAudio` made `static` give the same instructions, in the same
order; only the object file's relocations differ (against the section
instead of the name), and the linked bytes are the same. 331 of the game's
1,032 functions are called only from inside the file they are in now, and
could be `static`; nothing shows which were. None was marked. The 18
`static inline` functions were already there, each shown by its body being
inside its callers with no copy of its own. No function was found both
inlined and written out; that was not searched for beyond those 18.

### The files, and where each one's sections begin

Addresses without the leading `80`. A file with no entry in a column has
nothing in that section.

| File | `.rodata` | `.text` | `.data` | `.sdata` | `.sbss` | `.bss` |
|---|---|---|---|---|---|---|
| `main.c` | 10000 | 164F8 | 8D070 | A30CC |  |  |
| `cloth.c` |  | 17714 |  | A30E8 |  |  |
| `pad.c` | 10068 | 1945C |  |  |  |  |
| `motion_decode.c` | 100A4 | 1979C |  | A30EC |  |  |
| `camera.c` |  | 1A4F0 |  | A30F0 | A3310 |  |
| `fighter_pad.c` |  | 1BE08 |  |  |  |  |
| `fight.c` | 100C4 | 1C444 |  | A30FC |  |  |
| `fighter_pose.c` |  | 1F1C4 |  |  |  |  |
| `fighter.c` | 100FC | 20CDC |  |  |  |  |
| `fighter_hit.c` | 10478 | 272FC |  | A3140 |  |  |
| `hit_test.c` |  | 2CD58 |  | A314C |  |  |
| `object.c` |  | 2FF20 |  |  |  |  |
| `fight_event.c` | 105A0 | 324D0 |  |  |  |  |
| `fight_modes.c` |  | 33898 |  |  |  |  |
| `fight_start.c` | 1081C | 343F0 |  | A3150 |  |  |
| `menu_screen.c` | 108CC | 34F88 |  | A31D8 |  |  |
| `cdrom.c` | 10938 | 35F30 | 8EC34 | A31E4 |  |  |
| `memcard.c` | 109B0 | 375EC |  | A31E8 |  |  |
| `memcard_save.c` | 109EC | 37F40 | 8F19C | A31F0 | A3318 |  |
| `replay.c` |  | 392C8 |  | A3208 |  |  |
| `link.c` | 10CA4 | 3A174 |  | A320C |  |  |
| `story.c` | 10CC4 | 3AE5C | 8F220 |  |  |  |
| `fight_after.c` |  | 3BE10 | 9016C |  |  |  |
| `font.c` |  | 3D2C4 | 90178 | A3218 | A3358 | A3930 |
| `bitstream.c` |  | 3D774 |  |  |  | A3D40 |
| `stage.c` |  | 3D91C |  | A3228 | A3368 | A3D70 |
| `cloth_model.c` | 10D8C | 3F568 |  |  | A3370 | A93B0 |
| `model_face.c` | 10D9C | 401CC | 94AF4 | A3234 | A3378 | A9830 |
| `model.c` | 10DB8 | 40400 | 94B88 |  |  | A9A10 |
| `light.c` |  | 41E10 | 94E48 | A3238 | A3380 | A9B28 |
| `math.c` | 10DD4 | 42504 |  |  |  |  |
| `render.c` |  | 430E4 | 951D8 |  |  |  |
| `stage_props.c` |  | 44670 |  |  |  | A9CF8 |
| `math_sin.c` |  | 44CCC | 973FC |  |  |  |
| `ndata.c` | 10DEC | 44E64 | 955E0 | A3240 | A3398 |  |
| `arena.c` |  | 451D0 |  |  | A33A0 | A9D10 |
| `resource.c` | 152B4 | 45878 | 993FC | A3248 | A33B0 |  |
| `scene_frame.c` |  | 46AE8 |  |  | A33B8 | EEDB0 |
| `stage_hooks.c` |  | 47384 |  |  | A33C8 | EEDF0 |
| `wave.c` |  | 477DC |  |  | A33D0 | EF070 |
| `tex.c` |  | 47EE8 |  |  |  |  |
| `model_tiny.c` |  | 48AD0 |  |  | A33E0 |  |
| `tex_scroll.c` |  | 48F58 |  |  |  | EF848 |
| `resource_weapon.c` |  | 4939C |  | A324C | A33E8 | EF980 |
| `scene.c` | 153F0 | 49E4C |  |  |  |  |
| `render_asm.c` | 1541C | 4A348 |  |  |  |  |
| `math_asm.c` | 154A0 | 520B8 |  |  |  |  |
| `collision.c` |  | 52D00 |  |  | A33F0 | EF9F8 |
| `render_skin_asm.c` | 15820 | 54440 |  |  |  |  |
| `cutscene.c` | 15840 | 54604 | 99D54 | A3250 |  | EFAE8 |
| `cpu.c` | 1585C | 5508C | 99D88 | A3258 |  |  |
| `snd.c` | 158B4 | 5B43C | 9AD1C |  | A3400 | EFB38 |
| `hud.c` |  | 5C8A8 | 9ADB4 | A3264 | A3418 |  |
| `eff.c` | 158E0 | 60A68 |  | A32B8 | A3420 | EFC78 |
| `menu_pages.c` | 15988 | 68ECC | 9BC04 | A32C0 | A34F8 | F11E0 |
| `select.c` | 159A0 | 6E534 | 9BCD4 | A32E8 | A3530 |  |
| `select_team.c` | 15A0C | 74220 |  |  | A35D0 |  |
| `menu_run.c` | 15A3C | 77820 |  |  | A35E0 |  |
| `narration.c` | 15A54 | 77D10 |  | A32FC | A35F0 |  |
| `staff.c` |  | 78628 |  | A3304 | A3608 |  |

### What lies between each file and the next

"none": nothing in the bytes puts a boundary there or denies one.

| Between | Marks |
|---|---|
| `main` / `cloth` | none |
| `cloth` / `pad` | none |
| `pad` / `motion_decode` | a string (`0x800100A4`) after a jump table (`pad_Update`) |
| `motion_decode` / `camera` | none |
| `camera` / `fighter_pad` | none |
| `fighter_pad` / `fight` | none |
| `fight` / `fighter_pose` | none |
| `fighter_pose` / `fighter` | none |
| `fighter` / `fighter_hit` | a string (`0x80010478`) after a jump table (`fight_UpdateLock`); jump tables on different 8-byte grids (`fight_UpdateLock` at `0x8001042C`, `fighter_MarkStoryWound` at `0x80010498`) |
| `fighter_hit` / `hit_test` | none |
| `hit_test` / `object` | none |
| `object` / `fight_event` | none |
| `fight_event` / `fight_modes` | none |
| `fight_modes` / `fight_start` | none |
| `fight_start` / `menu_screen` | none |
| `menu_screen` / `cdrom` | jump tables on different 8-byte grids (`menu_EnterMode` at `0x800108CC`, `cdrom_UpdateAudio` at `0x80010938`) |
| `cdrom` / `memcard` | a string (`0x800109B0`) after a jump table (`cdrom_Update`) |
| `memcard` / `memcard_save` | a string (`0x800109EC`) after a jump table (`memcard_ProbeCard`); jump tables on different 8-byte grids (`memcard_ProbeCard` at `0x800109D8`, `memcard_BootCheckScreen` at `0x80010AC4`); g_memcard_fd: reached through gp in one, by full address in the other |
| `memcard_save` / `replay` | none |
| `replay` / `link` | none |
| `link` / `story` | none |
| `story` / `fight_after` | none |
| `fight_after` / `font` | none |
| `font` / `bitstream` | none |
| `bitstream` / `stage` | none |
| `stage` / `cloth_model` | none |
| `cloth_model` / `model_face` | `.bss`: 468 bytes no code names between `0x800A93C8` and `0x800A9830` |
| `model_face` / `model` | jump tables on different 8-byte grids (`model_SetFace` at `0x80010D9C`, `model_Update` at `0x80010DB8`) |
| `model` / `light` | `.bss` starts on 8 at `0x800A9B28`, the one before ends at `0x800A9B24` |
| `light` / `math` | none |
| `math` / `render` | none |
| `render` / `stage_props` | `g_render_queue_end`: reached through gp in one, by full address in the other |
| `stage_props` / `math_sin` | none |
| `math_sin` / `ndata` | none |
| `ndata` / `arena` | `.sbss` starts on 8 at `0x800A33A0`, the one before ends at `0x800A339C` |
| `arena` / `resource` | none |
| `resource` / `scene_frame` | none |
| `scene_frame` / `stage_hooks` | `.sbss` starts on 8 at `0x800A33C8`, the one before ends at `0x800A33C4` |
| `stage_hooks` / `wave` | `.sbss` starts on 8 at `0x800A33D0`, the one before ends at `0x800A33CC`; `.bss`: 218 bytes no code names between `0x800EEE58` and `0x800EF070` |
| `wave` / `tex` | none |
| `tex` / `model_tiny` | none |
| `model_tiny` / `tex_scroll` | none |
| `tex_scroll` / `resource_weapon` | `.bss` starts on 8 at `0x800EF980`, the one before ends at `0x800EF97C` |
| `resource_weapon` / `scene` | none |
| `scene` / `render_asm` | none |
| `render_asm` / `math_asm` | none |
| `math_asm` / `collision` | none |
| `collision` / `render_skin_asm` | none |
| `render_skin_asm` / `cutscene` | none |
| `cutscene` / `cpu` | none |
| `cpu` / `snd` | a string (`0x800158B4`) after a jump table (`cpu_Decide`); `.sbss` starts on 8 at `0x800A3400`, the one before (`collision`'s) ends at `0x800A33FC` |
| `snd` / `hud` | none |
| `hud` / `eff` | `.sbss` starts on 8 at `0x800A3420`, the one before ends at `0x800A341C` |
| `eff` / `menu_pages` | `.sbss` starts on 8 at `0x800A34F8`, the one before ends at `0x800A34F4` |
| `menu_pages` / `select` | a string (`0x800159A0`) after a jump table (`menu_HandleOptions`) |
| `select` / `select_team` | jump tables on different 8-byte grids (`select_DrawPortraits` at `0x800159D0`, `select_HandleTeamRules` at `0x80015A0C`); `.sbss` starts on 8 at `0x800A35D0`, the one before ends at `0x800A35CC` |
| `select_team` / `menu_run` | none |
| `menu_run` / `narration` | `.sbss` starts on 8 at `0x800A35F0`, the one before ends at `0x800A35EC` |
| `narration` / `staff` | `.sbss` starts on 8 at `0x800A3608`, the one before ends at `0x800A3604` |

A boundary somewhere between two files that are not neighbours (only those
with no marked pair inside them):

- `main` .. `pad`: `g_fight_mode`: reached through gp in one, by full address in the other
- `fight` .. `fighter`: a string (`0x800100FC`) after a jump table (`snd_StartFightMusic`)
- `fighter_hit` .. `fight_start`: `g_fight_every_hit_kills`: reached through gp in one, by full address in the other
- `fight_event` .. `fight_start`: a string (`0x8001081C`) after a jump table (`snd_GetStagePropKind`)
- `fight_event` .. `fight_start`: jump tables on different 8-byte grids (`snd_GetStagePropKind` at `0x800107C0`, `fight_StartMode` at `0x8001084C`)
- `story` .. `cloth_model`: a string (`0x80010D8C`) after a jump table (`cutscene_Update`)
- `wave` .. `model_tiny`: `.sbss` starts on 8 at `0x800A33E0`, the one before ends at `0x800A33DC`
- `collision` .. `cutscene`: `.bss` starts on 8 at `0x800EFAE8`, the one before ends at `0x800EFAE4`

`cloth_model.c` / `model_face.c` is not counted among the 18: a hole of
variables no code names is not a boundary's mark (rule 5).

"Strong" in the sense the round asked for: the 18 marked pairs are each
marked in one section, and the other sections agree in the only way they
can, by having the same files in the same order on both sides. No boundary
is marked independently in two sections except `memcard.c` /
`memcard_save.c` (strings, grid and `gp`), `fighter.c` / `fighter_hit.c`
(strings and grid), `select.c` / `select_team.c` (grid and `.sbss`) and
`stage_hooks.c` / `wave.c`.

A mark between two files says a boundary lies between the two functions
that carry it, not that it lies exactly where ours is. `pad.c` /
`motion_decode.c`, for one, is between `pad_Update` and
`motion_PrintFrame`.

### Joined and split

| Was | Is | By what |
|---|---|---|
| `model.c` (the face), `model.c` (the rest), `model_update.c` | `model_face.c`, `model.c` | `model_SetFace`'s table and `model_Update`'s are on different grids, so the layout round's "one file" was two; `g_models` lies after the face's variables and before the rest's, and is `static` now with no line in `bb2.ld`. The cut is after `model_SetFace`; the three small functions after it use nothing that says which side they are on, and went with the model |
| `memcard.c`, `memcard_probe.c`, `memcard_Format` of `memcard_save.c` | `memcard.c` | rule 4 (`g_memcard_timeout_counter`), rule 2 (`memcard_Format`'s string before `memcard_ProbeCard`'s table). `memcard_PackSaveData`, next, has no mark and stayed |
| `motion.c`, `fighter.c`, `fight_lock.c` | `fighter.c` | rule 1: a padding word between `motion_GetMove`'s table and `fight_UpdateLock`'s. `D_80010428` was that word |
| `fight_event.c`, `snd_stage.c` | `fight_event.c` | rule 1: a padding word between `fighter_PlayFrameEvent`'s table and `snd_GetStagePropKind`'s. An `ALIGN(8)` in `bb2.ld` was that word |
| `fight_start.c`, `menu_debug.c` | `fight_start.c` | rules 1 and 2: the debug menu's long strings before `fight_StartMode`'s table, and a padding word (`. += 4` in `bb2.ld`) after it. The debug menu's thirteen strings are literals now: eleven are small data, in the order they are used |
| `select.c`, `sprite.c` | `select.c` | rule 2: `sprite_Draw`'s string before `select_Load`'s table. `g_str_warning[16]` in `menu_pages.c` was the string and the padding after it |

Besides, with the files right: `g_str_empty` (`fighter_hit.c`) and
`D_80015A20` (`select_team.c`) were padding words too and are gone; the
messages of `main.c`, `motion_decode.c`, `cutscene.c`, `snd.c`,
`fighter_hit.c`, `memcard.c` and `memcard_save.c` are string literals
where they are printed, not named arrays (17 arrays gone); one `ALIGN(4)`
is gone from `bb2.ld`'s `.rodata` list.

### Found and not acted on

- **Where the joined files begin and end.** `fighter.c`'s original began
  after `snd_StartFightMusic` (in `fight.c`) and no later than
  `motion_GetMove`: `fighter_pose.c` and the tail of `fight.c` are in it or
  in a file between. `fight_event.c`'s began after `fighter_TakeHit`:
  `hit_test.c` and `object.c` may be part of it. `fight_start.c`'s began
  after `snd_GetStagePropKind`: `fight_modes.c`, which begins with
  `menu_DebugInit`, is probably part of it, and `menu_screen.c` (same grid,
  no string between) may be. `select.c`'s began after `menu_HandleOptions`.
  Nothing marks the place, so nothing was moved.
- **The boundaries with no mark** are ours, by subject. The longest
  runs with none: `memcard_save.c` to `fight_after.c` (five files),
  `fight_after.c` to `cloth_model.c`, `light.c` to `ndata.c`, `wave.c` to
  `collision.c`. Each run is at least one file of the original's; no more
  is known.
- **The data files** (`fight_data.c`, `stage_data.c`, `resource_data.c`,
  `cpu_data.c`, `snd_data.c`, `hud_data.c`, `eff_data.c`, `select_data.c`,
  `menu_data.c`). Small objects in them are in `.data`, which a file
  compiled `-G8` would not do, and `snd.c` saw one without its size: files
  of data compiled without small data, as they are built here. But
  `fight_data.c` is not one such file: its tables of names
  (`g_fighter_move_kind_names`, `g_motion_set_files`, `g_fight_mode_names`)
  point at strings that lie in `fighter.c`'s and `fight_start.c`'s
  `.rodata` and small data, and a table and its strings are one file's. So
  those tables were `fighter.c`'s and `fight_start.c`'s own, with the
  strings as literals, and `fight_data.c` is several files' data run
  together. It was not taken apart: its small objects that are not in the
  small data (`g_kaishaku_cues.swing_frame` and the seven after it, 24 bytes
  that may be one structure; `g_team_time_limits`) do not fit a `-G8` file
  either, and which piece is whose is not shown. `fight_sdata.c` is the
  same in the small data: the three motion file names in it are
  `g_motion_set_files`' short strings. `FAKE` notes on `fight_start.c`'s
  mode names stay for the same reason.
- **`globals.c`** stays. `main.c` reaches 27 small variables through `gp`
  that belong to the fight, the story, the camera, the effects, the sound
  and the link cable, so `main.c` is where the authors defined the game's
  globals, and the 120 in `globals.c`, which no file reaches through `gp`,
  were most likely defined there too (moving them would build the same).
  Probably; nothing shows it.
- **The assembly.** At least two objects: the run from
  `math_RotMatrixZYX` to `collision_ReadProjection`
  (`0x8004A348`-`0x80052D00`: `render_asm.c` and `math_asm.c`), and
  `render_LightSkin` with the outline templates (`0x80054440`-`0x80054604`:
  `render_skin_asm.c`), with `collision.c`'s compiled code between them,
  which no single object could span. Whether the first run was one object
  or several nothing shows: assembled code has no alignment between
  routines and its tables are not on a grid. Ours is cut at `0x800520B8`
  by subject.
- **The `.sbss` list's `ALIGN(8)` lines** are rule 5 and could be the
  list's own alignment; they are in the loaded section with everything
  else, which is on 2, and were left as lines.

### The overlay

`movovl/src/main.c` and `movie.c` are two files of the original's:
`g_movie_still_reached` is reached through `gp` in `main` and by its full
address in `movie.c` (rule 4). Its `Makefile` already compiles both `-G8`.
Nothing else was looked at there.

## Names against outside evidence

Not about `FAKE` notes. Four sources were put beside the names: the
linker's order of the zeroed variables (found by the layout round), the game's own
text, Sony's headers, and the sister game, *Kengo*. 205 names changed
(20 in the overlay, 185 in the game),
eight variables became one structure, two of the overlay's variables lost
their `static`. No name of the game's own variables was recovered.

### What the linker's order is over

The rule (under "The zeroed variables"): a variable that is not `static` and has no
initial value is placed in the order of the sum of its name's characters
plus its length, modulo 512. Tested here on what else it might order,
with Sony's names, which are certain:

| What | Sony's names, in address order | Adjacent pairs that rise | Verdict |
|---|---|---|---|
| functions | 661 | 340 of 660 | chance: not ordered by name |
| the first function of each module | 199 | 109 of 198 | chance: the modules are not either |
| initialised data | 156 | 84 of 155 | chance |
| the larger common variables, main program | 40 | 39 of 39 | the rule (the layout round) |
| the common variables, overlay | 21 | 20 of 20 | the rule (below) |

So it covers the common variables and nothing else: a function or an
initialised variable is where its file puts it. It cannot test a function's
name.

**The small common run** has no name of Sony's in it in either program
(the libraries were built without small data), so it cannot be tested on a
name that is certainly original. What there is: nine families of variables
that the code treats as siblings lie side by side in it, in the code's
order (the two win counts; the blow's direction x, y, z; the team's points
and its fouls; the story's wounds; the link cable's pad word and
check word, three times; the three remapped buttons). That is what names
differing in their last character give under the rule, and 11 of 206
adjacent pairs being siblings is not chance. Consistent with the same
rule; not proof.

### The overlay: Sony's names, and one walk

The layout round left the overlay's common variables as two runs in no order, and
a recollection that Sony's names would fall in order. Sony's names for
them are on this machine in one place: a scratch of `StCdInterrupt` from
another game's copy of the library, kept from the old process in
`tmp/decomp_me_corpus/` (`gcc2.7.2-psx__ShFCc.json`; not in git), whose
`extern` list names the variables and whose statements are ours one for
one. From it:

| Ours was | Sony's | Number |
|---|---|---|
| `st_frame_no` | `Stframe_no` | 24 |
| `st_sector_no` | `Stsector_offset` | 76 |
| `st_rgb24` | `StRgb24` | 79 |
| `st_no_sector_header` | `StMode` | 82 |
| `st_end_callback` | `StFunc2` | 140 |
| (already Sony's) | `StCdIntrFlag` | 145 |
| `st_next_channel` | `StCHANNEL` | 201 |
| `st_start_frame` | `StStartFrame` | 204 |
| `st_channel` | `CChannel` | 260 |
| (already Sony's) | `PadIdentifier` | 293 |
| `st_frame_done` | `StFinalSector` | 302 |
| `st_mem_sector` | `StEmu_Idx` | 379 |
| (already Sony's) | `_que` | 430 |
| `st_write_slot` | `StRingIdx1` | 439 |
| `st_frame_slot` | `StRingIdx2` | 440 |
| `st_mem_addr` | `StEmu_Addr` | 466 |
| `st_end_frame` | `StEndFrame` | 467 |
| `st_mask` | `StSTART_FLAG` | 474 |
| `st_body` | `StRingBase` | 476 |
| `st_ring` | `StRingAddr` | 476 |
| `st_ring_size` | `StRingSize` | 508 |

The rows are in address order and the numbers rise without an exception:
21 names, which by chance happens once in 5 x 10^19. The names were taken
from the scratch before the numbers were worked out, so this is a second,
independent test of the rule as well as of the names. It also narrows
nothing: the sum with the length fits moduli 511 to 515 here and the plain
sum 507 to 512, and with the main program's the answer is still 512 to 515
or 507 to 509.

- **It is one walk, not two runs.** The first "run" ends at `StFinalSector`
  (302) exactly where the file ends, and the second begins after the game's
  two movie variables. The names before them are all below 303 and the
  names after all above 378, so the game's two (`g_dec_storage`,
  `g_movie_file`: 0x40 bytes) are inside the walk: they are common
  variables, not `static`, with names whose numbers are between 302 and
  379. Built: without `static`, placed by a line each, the overlay
  matches. A `static` would have been with the modules' own variables,
  ahead of the walk.
- The two names with the same number (476) are in the order `StRingBase`,
  `StRingAddr`; nothing else is known of how a tie falls.
- Renamed as well, from the same scratch: the module's own `debug_cause`
  (was `st_result`) and `HeaderArea` (was `st_header`), which is a `static`
  inside `StCdInterrupt` there (`HeaderArea.2`) and is now here; it builds
  the same.
- **Not renamed, because nothing here has their names:** `st_read_slot`
  and `st_frame_callback`. `StRingIdx3` and `StFunc1` would continue Sony's
  pattern (`StSetStream`'s parameters are `func1` and `func2` in Sony's
  header) and their numbers, 441 and 139, fall in their windows (440 to
  466, 82 to 140). That is a guess that fits, not a name seen.
  `ds_callbacks` (libds) is ours too: its number has to be between the
  movie variables' and 379.
- What the game's two variables were called is not known. `dec`, 303,
  would fit the first and is what Sony's sample player calls its
  structure as far as memory goes; nothing on this machine shows the
  sample, so it is worth nothing.

### The game's variables: nothing confirmed

The windows are in the table under "The zeroed variables". A candidate has to come
from somewhere other than the window. The sources of candidates were:

1. **The game's messages.** None prints a variable's name. `eff_init:`
   is printed by the function we call `eff_Init`; the others name things,
   not identifiers (`common_vab`, `vab id`, `eff prim`, `common prim`,
   `tiny model`, `GUN MOTION`, `SOUND ID`).
2. **Kengo's executable, `SLUS_200.21`**, which is not stripped: 2,482 function
   names and about 1,000 variable names of Lightweight's next game. Its
   names for the things both games have were tested, as candidates only:

   | Ours | Kengo's | Number | Window | |
   |---|---|---|---|---|
   | `g_fighters` | `Rob` | 294 | 273 to 348 | fits alone |
   | `g_pad_state` | `Pad` | 280 | 273 to 348, after `g_fighters`' | fits alone; not with `Rob` (280 is below 294) |
   | `g_camera_main` | `Camera` | 79 | 95 to 142 | rejected (nor `g_camera_second`, 52 to 75) |
   | `g_light_setups` | `Light`, `StageLight` | 509, 502 | 75 to 95 | rejected |
   | `g_replay_events` | `Replay` | 115 | 273 to 348 | rejected |
   | `g_saved_record` | `UserWork` | 330 | 437 to 457 | rejected |
   | `g_fight_config` | `DebugOption` | 107 | 273 to 348 | rejected |

   Two of seven fit a window that takes one name in seven by chance, and
   those two cannot both be right. **Kengo's variable names are not this
   game's**, and none is used. (Every Kengo name that falls in each
   window was listed too; with windows this wide the lists are long and
   say nothing without a reason to pick one.) This is the hash doing what
   it can do: it rejects.
3. **Sony's samples**, which the game's main loop follows
   (`g_disp_db[2]` is the samples' structure of a `DRAWENV`, a `DISPENV`
   and what goes with them, with the members named `draw` and `disp`).
   The samples call it `db`; `db` gives 200 and the window is 192 to 206
   (15 numbers of 512). Suggestive, and from memory of the samples, which
   are not on this machine: recorded, not relied on. The samples' names
   for the memory card's events (`ev0` to `ev3`, `ev10` to `ev13`) are
   rejected: they would be eight neighbours in the small run, and the
   events are not.

No candidate at all was found for the other windows, the narrow ones
included (`g_motion_decoders`, 11 to 12; `g_memcard_file_list`, 358 to
362; `g_shared_buffer`, 47 to 52).

### The game's own words

The executable's text is thin: 52 names of move kinds, seven of modes, the
debug menu's letters, a dozen messages, the file names. The menus' words
are pictures (some were read off the screen of the running game).
Where the game has an English word for a thing and the source used
another, the source has the game's now, inside our convention. The three
Japanese words in the table (`seri`, `karami`, `suna`) were put into 110
names here and taken out again in the last naming round: the source is in English, and
a Japanese word of the game's that is not a proper noun is given in the
comment at the definition (`STYLE.md`, "Names"). Their rows stay as the
record of the evidence:

| Was | Is | Names | The game's text | Strength |
|---|---|---|---|---|
| `lock` (`g_fight_lock_frames`, `fight_UpdateLock`, `LOCK_EVENT_WIN`, `eff_StartLockFlash`...) | `seri` (reversed, the last naming round) | 46 | the move kinds `SERI` and `SERIEXIT` (tsubazeriai) | the game's word for the state; that the authors' identifiers used it is likely, not shown |
| `paired move` (`g_fight_paired_move`, `fight_RunPairedMove`, `PairedMoveEntry`...) | `karami` (reversed, the last naming round) | 34 | the move kinds `KARAMI`, `KARAMI_ED` | the same |
| `dirt` (`ThrownDirt`, `object_ThrowDirt`, `PARTICLES_DIRT`...) | `suna` (reversed, the last naming round) | 30 | the move kind `SUNA`; Kengo has `efc_rob_set_sand_smoke` | the same; "suna" is sand, and it is thrown on earth and in water too |
| `bank` (`g_snd_banks`, `snd_LoadCommonBank`, `SoundBank`, `SND_BANK_COMMON`...) | `vab` | 47 | `common_vab start:%08x`, `vab id:%d mistake` | good: `snd_LoadCommonVab` prints the first |
| the team's sounds (`enum TeamSound`, `SND_TEAM_IPPON`, `snd_LoadTeamBank`, `SND_FILE_TEAM`) | `judge` | 22 | the file is `JADGE.SE`; Kengo has `judge.c`, `judge_Exec`, `Judge` | good |
| the effects' `quad` buffers (`g_eff_quads`, `g_eff_next_quad`, `EFF_QUAD_MAX`...) | `prim` | 6 | `eff prim over :%d`, printed when they run out | good |

Comments keep the English ("blade lock", "paired move", "dirt", "bank");
each definition says which word is the game's. The judge is the judge in
comments too, since the last clean-up (eight said "referee").

Kept, with what the text says:

| Name of ours | The game's text | Why kept |
|---|---|---|
| `cutscene_...`, `Cutscene` | the files are `MARcc_mm.DAT` and `MAR_XAn.XA`; Kengo spells it out: `marionation_Init`, `nm_mario.c`, `MarioCam` | `MAR` is three letters of a file name, not a word in this game's text, and the expansion is only the sister game's. The file's comment says it. The owner may prefer `mar_`: it is one map |
| `eff_Init` | `eff_init:%08x size:%08x`, printed by it | agrees |
| `chanbara`, `practice`, `team`, `vscpu`, `story`, `FIGHT_MODE_NETWORK` | the mode names in the debug menu's table | agree. The menu's pictures say "Slash Mode"; the disc says `GAMEDAN`, `D_SEL` (dantai) for the team battle and `GAMETRA` for practice |
| `link_...` | `LINK.TIM`, `LINK_M.TIM`; the mode is `NETWORK` | both are the game's |
| `tiny` model | `Destruction tiny model.`, `TINYMnn.DAT` | agrees |
| `common` (sound, motion) | `common_vab`, `COMMON.BBM` | agrees |
| `g_disp_prim_buffers` | `common prim over flow` | "prim" is there; the comment has "common" |
| `MOVE_KIND_...` | the 52 strings | 24 are the strings; the 28 that are Japanese words have English names since the last naming round, the string in the comment at each |
| `henchmen` | none (`NINJA_C.SE` is chanbara's ninja class) | no word of the game's for them |
| `g_sys_build_date_ref` | the date string; Kengo: `MakeDate`, `common/date.c` | the sister game's identifier only |
| the debug menu's rows | `C`, `CL`, `O`, `CR`, `CO`, `L`, `M`, `D` | letters; `CO` for the colour option and `CL` for the CPU's level fit ours, the rest say nothing |

### The kaishaku's cues are one structure; the time limits are not explained

The files round left two things in `fight_data.c` that a file compiled `-G8`
would have put in the small data and the original did not.
- `g_kaishaku_swing_frame` and the seven after it are `g_kaishaku_cues`
  now, one structure of 24 bytes (five frames, then the blood's x, y, z).
  Built: the same bytes. One object of 24 bytes is not small data in a
  `-G8` file, which eight variables of two and four bytes would be; the
  members' addresses are what a structure gives (five at a stride of two,
  two bytes of padding, three words). Probably: the code is the same
  either way.
- `g_team_time_limits`, four bytes, is the last thing of the fight's data
  ahead of the disc's file table. With `g_chanbara_models` before it (ten
  bytes and two of padding) it makes no structure that means anything.
  Left as it is.

### Sony's headers

The game's code already has Sony's spelling where it uses Sony's things:
looked at here were the events (`SwCARD`, `HwCARD`, `EvSpIOE`...,
`EvMdNOINTR`), the CD commands and modes (`CdlSetloc`, `CdlReadS`...), the
pad's buttons (`PADLup`..., 216 uses, none of ours for a button), the
serial attributes (`SS_SERIAL_A`), the primitives' macros, and every call
of a library function with a bare number for an argument (they are the
arguments Sony's samples write as numbers: `VSync(0)`, `DrawSync(0)`,
`SetSemiTrans(p, 1)`). Nothing was changed. Not gone through member by
member: the structures in `include/psxsdk/` against Sony's own headers,
which are not on this machine.

### What was not gone through

- **Bushido Blade 1.** Nothing of it is on this machine: no disc, and the
  archive branch has no notes on it (searched `docs/` and `memory/` there).
- **The archive branch's naming material** (`docs/naming/`, 5,216 files)
  beyond its `README.md` and `MISNOMERS.md`. What those say bears on this:
  names carried over from Kengo by matching function sizes were wrong often
  enough that the owner had them all reset (2026-08-07). Hence Kengo's
  names were used here only as candidates for the hash to reject, and its
  function names not at all.
- **Kengo's data**: `FID.BIN` was not unpacked, so the file names inside
  it were not read. Its source file names are in the executable (assert
  strings: `common/judge.c`, `common/date.c`, `ishito/is_font.c`,
  `numata/nm_mario.c`, `suzuki/su_card.c`... 107 of the game's) and
  were read but not set against our files: a lead for anyone taking the files
  round's "only suggestive" list further. Its font takes the same `~cNNN`
  colour codes as `font_Printf`.
- **The words in the pictures** (the menus, the select screens) beyond
  what the checks in the running game recorded; nothing was looked at in an
  emulator or an image viewer.
- **The disc's file names** `LAS_*.TIM`, `SEA2_16.TIM` and `U_PIC/` were
  not traced to the code that loads them. The others (`*.SE`, `MOTION/`,
  the `NDATA` groups, `MOD`, `SEL`, `D_SEL`, `NAR`, `STAFF`, `LINK`) agree
  with the names of what loads them.
- Local names and members were not gone through against anything: there
  is no outside evidence for them.
