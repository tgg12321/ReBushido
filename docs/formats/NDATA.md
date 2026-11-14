# NDATA — Packed game-data archive

Files: `disc/NDATA/NDATA.DAT` (58,114,048 bytes) and `disc/NDATA/NDATA.INF` (3,604 bytes).

NDATA.DAT is the primary bulk-data archive for Bushido Blade 2. It stores
character animations, motion tables, stage geometry, audio sample banks, and
texture/CLUT pages in a single sector-aligned blob. NDATA.INF is a flat sparse
directory mapping a fixed file-ID (0..900) to a (start-sector, length-sectors)
pair inside NDATA.DAT.

## File layout

```
NDATA.INF :  901 x 4 byte directory entries  (3,604 bytes)
NDATA.DAT :  28,376 x 2,048-byte sectors     (58,114,048 bytes)
```

The sector size is 0x800 (2,048) — the same data sector size used by the PSX
CD-ROM filesystem. NDATA.DAT is read sector-aligned via the same disc-I/O path
used by PSX CdRead.

## NDATA.INF entry format

Each of the 901 entries is 4 bytes, little-endian:

| Offset | Size | Field           | Meaning                                          |
|--------|------|-----------------|--------------------------------------------------|
| 0      | u16  | start_sector    | Offset into NDATA.DAT in 2,048-byte sectors      |
| 2      | u16  | length_sectors  | Length in 2,048-byte sectors (rounded up)        |

Byte offset into NDATA.DAT = `start_sector * 0x800`.
Byte length on disc      = `length_sectors * 0x800`.

Unused / reserved indices are filled with the sentinel pair `0xFFFF, 0xFFFF`.
Of the 901 indices, **762** are non-sentinel entries, of which 760 pack the 28,376
sectors; 139 are sentinels: named files the build left out (138 trailing motions of
the `DATA7` sets, plus `HAND`; see the pool-to-INF rule below).

Of the 762 non-sentinel entries, 760 pack the 28,376 sectors = the entire DAT file. There
are no gaps in the actual on-disc layout; each active entry begins where the
previous active entry ended.

### Trailing entries 898..900 (sentinel + leftovers)

Indices 898, 899, 900 lie at the very end of the INF; 899 and 900 run past the
end of NDATA.DAT (sector 0x6ED8):

```
[898] start=0xFFFF  len=0xFFFF        (sentinel)
[899] start=0x9800  len=0x0308        (would address byte 0x4C00000 — past DAT end)
[900] start=0x1900  len=0x5956        (bytes 0xC80000..0x392B000: overlaps live entries, ends past DAT end)
```

These three slots appear to be footer-padding / abandoned indices left over from
the build pipeline (DAT was finalised earlier in development than INF was
trimmed). The runtime never indexes past 897 in the address-walking code paths
observed in the source. Neither `ndata_Read` nor `ndata_ReadChecked` range-checks
the id (the mirror has 1024 slots). Most callers compute the id at run time from a
base plus an offset (e.g. `number + NDATA_MOTIONS` in `cutscene_Start`,
`tiny_model + NDATA_TINY_MODELS` in `ndata_ReadTinyModel`); that every id lands on one of the 760 entries addressing NDATA.DAT
is not established from the code.

## In-RAM mirror

`g_ndata_inf` (`NdataInfEntry[1024]`, 0x800963EC, `.data`, defined at
`src/main/ndata.c`) holds the INF: 1024 4-byte
`{s16 start_sector; s16 length_sectors;}` slots. The EXE ships it pre-filled (its
bytes equal NDATA.INF except entries 899-900, then zeros), and `ndata_StartReadInf`
re-reads the file over it at init. The original reads each length halfword at +2, which
the function listings spell `g_ndata_inf + 0x2`.

NDATA.INF first 8 bytes: `00 00 49 00 49 00 46 00`, i.e. entry 0 = (0x0000, 0x0049),
entry 1 = (0x0049, 0x0046).

### Loader (`ndata_StartReadInf`)

`cdrom_StartReadAt(1, g_ndata_inf, 0, 2)`: file 1 of `g_cdrom_files` (3,604
bytes, NDATA.INF), 2 sectors (the INF rounded up), into the mirror. It only starts
the read; the next `cdrom_WaitIdle` (e.g. `ndata_Read`'s) waits for it.

### Reader (`ndata_Read`)

`ndata_Read(id, dst)`: `cdrom_WaitIdle`; `cdrom_StartReadAt(0, dst,
g_ndata_inf[id].start_sector, g_ndata_inf[id].length_sectors)` (file 0 =
NDATA.DAT, 58,114,048 bytes); `cdrom_WaitIdle`. `ndata_ReadChecked` first checks
that an arena destination has room ("Marionation over flow" otherwise);
`ndata_GetArenaRoomAfter(id)` is the arena's free bytes minus the entry's size.

### Pack cache

`ndata_LoadPack(n, dst)` reads entry `0x25 + n` into `dst`, keeps `dst` in
`g_ndata_pack` and sets `g_ndata_pack_loaded`. While it is set,
`ndata_ReadFromPack(id, dst)` serves ids 0x1E..0x24 from that block:
`g_ndata_pack_sections[id - 0x1E]` (s16 {0, 1, 2, 3, 4, -1, 0}; -1 = not in the pack)
picks sub-block k; the block must start with the count word 5 (otherwise the
flag is cleared), and sub-block k lies at `block + (block[1 + k] & ~3)`, length
`block[2 + k] - block[1 + k]`, copied with `mem_MoveWords`. It returns 1 when it
served the read. `ndata_InvalidatePack` clears the flag (memory untouched) and
`ndata_IsPackLoaded` returns it. `ndata_ReadModel` remaps ids >= 0x1F to `id - 0x1B`,
tries the pack, and falls back to `ndata_Read`.

## File-ID → name mapping (source-side filename tables)

The executable holds the names of the 899 files the archive was built from: `.rodata` at
`0x80010DEC..0x8001528C` is a contiguous pool of 899 `DATA<n>\<NAME>.DAT` strings, each
NUL-padded to a 4-byte boundary, followed by the `Marionation over flow` format string, and
`.data` at 0x800955E0 holds 899 pointers to them. In the source this is one table of string
literals, `g_ndata_file_names` at the top of `src/main/ndata.c`, in INF order:
`g_ndata_file_names[i]` is slot `i`'s name. (The compiler writes a table's strings out
last-first, which is why the pool is in reverse.) No code reads the table: the shipped game
does no name lookup, and every caller passes a numeric id.

### Pool-to-INF rule: one name per slot, in reverse

**INF slot `i` (0..898) is named by pool string `898 - i`.** The pool is written
last-id-first, so walking it backwards walks the INF forwards. Every slot has a name,
sentinels included (a sentinel is a named file the DAT does not carry); slots 899 and 900
have none. Evidence (read from NDATA.INF, NDATA.DAT and the EXE pool):

* Each of the 139 sentinels falls on a name the build left out: 138 are the
  highest-numbered motions of one `MARcc` set, each run ending exactly on that
  set's last slot (slots 328..336 = `MAR00_23..MAR00_31`, 361..368 =
  `MAR01_24..MAR01_31`, and so on); the 139th is slot 898 = `DATA1\HAND.DAT`.
* Group boundaries match the header shapes in the DAT (the 38 stage files carry the
  multi-section header `resource_LoadStage` parses, the 54 tiny models are
  12,288-byte files with a 1-entry header), and the group byte totals in the table below
  sum to exactly 58,114,048, the DAT size.
* The code's base offsets (`enum NdataId`, `include/ndata.h`) land on group
  starts: `0x25` (`NDATA_CHANBARA_PACKS`; `ndata_LoadPack`,
  `src/main/ndata.c`) on `CHANBARA0`, `0x27` (`NDATA_STAGES`) on `S00`,
  `0x4D` (`NDATA_TINY_MODELS`) on `T00`, `0x4D + 0x1B`
  (`NDATA_TINY_MODELS_GRAY`) on `TINYM00`, `0x83` (`NDATA_WEAPONS`) and
  `0x10C` (`NDATA_WEAPONS_SET_1`) on the two `WEP0000`s, `0x131`
  (`NDATA_MOTIONS`) on `MAR00_00`.

In the source the 899 names and the directory are laid out in these groups
(27 of them: each set of `MARcc` files is one), each under a comment with
its first entry's number and its `NdataId`.

[`ndata_filemap.csv`](ndata_filemap.csv) applies this
rule: one row per slot 0..900, sentinels flagged. The script that wrote it refused to write unless the
pool holds 899 strings, the id bases above land on their names, and the EXE's pointer
table `g_ndata_file_names` agrees with every slot's name.

### File-ID layout (by ID range)

| File IDs (hex) | Group | Names | Live / slots | Bytes | Read by |
|----------------|-------|-------|-------------:|------:|---------|
| `0x000..0x026` | DATA0 | `M000..M306`, `CHANBARA0/1` | 39 / 39 | 5,410,816 | model files (`ndata_ReadModel`, `src/main/ndata.c`, blocks 3..5), served from the pack for ids `0x1E..0x24`; packs `0x25 + n` (`ndata_LoadPack`) |
| `0x027..0x04C` | DATA2 | `S00..S37` (stages) | 38 / 38 | 5,048,320 | `ndata_ReadStage` (`0x27 + selector`) |
| `0x04D..0x067` | DATA5 | `T00..T26` | 27 / 27 | 331,776 | `ndata_ReadTinyModel` (`0x4D + id`) |
| `0x068..0x082` | DATA6 | `TINYM00..TINYM26` | 27 / 27 | 331,776 | `ndata_ReadTinyModel` (`0x4D + id + 0x1B`, grayscale mode) |
| `0x083..0x10B` | DATA8 | `WEP0000..WEP1515`, `WEPNULL` | 137 / 137 | 31,516,672 | `ndata_ReadWeapons(0, k)` (`0x83 + k`) |
| `0x10C..0x130` | DATA9 | `WEP0000..WEP0707`, `WEPNULL` | 37 / 37 | 3,182,592 | `ndata_ReadWeapons(!0, k)` (`0x10C + k`) |
| `0x131..0x381` | DATA7 | `MAR00_00..MAR22_13` | 455 / 593 | 12,292,096 | `cutscene_Start` (`0x131 + index`) |
| `0x382` (898) | DATA1 | `HAND` | 0 / 1 | 0 | sentinel |
| `0x383..0x384` | — | (no name) | leftovers | — | never |

Within a group the ids run in name order (`S00` = `0x27`, `MAR00_00` = `0x131`). A stage
file's number is the stage's (`enum StageIndex`, `include/stage.h`; the names are listed in
[STAGE_BIN.md](STAGE_BIN.md)): `S00` is `STAGE_MAIN_TATARA_SHRINE`, `S31`
`STAGE_SHAINTO_CEMETERY`, `S32` to `S36` the modes' own stages, and `S37` the stage without a
name that link mode uses in place of the castle rooftop. DATA0's
order is `M000..M002`, `M100..M111`, `M003..M005`, `M200..M211`, `M301..M305`, `M300`,
`M306`, `CHANBARA0`, `CHANBARA1`. Ids 0 to 30 are the models of the roster entries of the same
number (`enum RosterId`, `g_fighter_roster_models` in `src/main/fight_data.c`): `M000..M002`
are `ROSTER_KANNUKI_STORY`, `ROSTER_TATSUMI_STORY` and `ROSTER_MIKADO_STORY`, `M100..M111`
`ROSTER_KANNUKI` to `ROSTER_SIDEARM_1`, `M003..M005` `ROSTER_GENGORO_STORY` to
`ROSTER_JO_STORY`, `M200..M211` `ROSTER_GENGORO` to `ROSTER_SIDEARM_2`, and `M301`
`ROSTER_LAST_1`. `M302..M305` are chanbara's opponents (models 58 to 61), `M300` is
`ROSTER_VICTIM`'s (model 62) and `M306` `ROSTER_LAST_2`'s (model 63). The pack ids
`0x1E..0x24` are the `M3xx` set, and the two
pack blocks `0x25`/`0x26` (`CHANBARA0/1`) have header count word 5, the value
`ndata_ReadFromPack` requires (so do `M302..M305`, ids `0x1F..0x22`). A pack's
sections 1 to 4 are `M302..M305`, byte for byte their own entries, in both
packs. Section 0 is the last opponent, `M301` in `CHANBARA0` and `M306` in
`CHANBARA1` (`g_ndata_pack_sections` sends both models to section 0, so the
pack loaded has to be the right one), cut down: five sections where the
model's own entry has six (no sound bank) and fewer textures, 78,972 and
81,228 bytes against 167,768 and 154,520 (`tests/ndata.py` checks all of
this). Each `T<n>`/`TINYM<n>` pair has the same
header and size and differs in a few hundred bytes (colour vs grayscale). DATA8 names are
the 136 unordered weapon pairs `WEPaabb` (`aa <= bb`, both 00..15) plus `WEPNULL`; DATA9 the
36 pairs over 00..07 plus `WEPNULL`.

DATA7 by set (19 sets, `cc` of `MARcc_mm`). Sets 00 to 21 are the cutscenes of the
character of that number (`enum CharacterId`, `include/fighter.h`): the eighteen the story
can be played with, 00 KANNUKI to 09 MATSUMUSHI without 05 (TSUBAME), and 12 GENGORO to
21 ISOHACHI without 16 (KATZE); 10 and 11 (`CHARACTER_VANISHER`, `CHARACTER_SIDEARM_1`,
two of the characters that have no name on the disc) have none either. Set 22
(`CUTSCENE_SET_SHARED`) is the one every fight shares. The sets' first ids are
`g_cutscene_set_first_file` (`src/main/cutscene.c`). Each block lists motions
`_00` upwards and ends with its sentinels:

| Set | IDs (dec) | Live | Sentinel tail | Set | IDs (dec) | Live | Sentinel tail |
|-----:|-----------|-----:|--------------:|-----:|-----------|-----:|--------------:|
| 00 | 305..336 | 23 | 9 | 13 | 612..646 | 27 | 8 |
| 01 | 337..368 | 24 | 8 | 14 | 647..681 | 27 | 8 |
| 02 | 369..401 | 23 | 10 | 15 | 682..716 | 27 | 8 |
| 03 | 402..428 | 20 | 7 | 17 | 717..751 | 27 | 8 |
| 04 | 429..458 | 24 | 6 | 18 | 752..782 | 23 | 8 |
| 06 | 459..485 | 19 | 8 | 19 | 783..813 | 23 | 8 |
| 07 | 486..515 | 24 | 6 | 20 | 814..848 | 27 | 8 |
| 08 | 516..545 | 24 | 6 | 21 | 849..883 | 27 | 8 |
| 09 | 546..576 | 24 | 7 | 22 | 884..897 | 14 | 0 |
| 12 | 577..611 | 28 | 7 | | | | |

### Runtime selectors and the arena blocks they fill

The arena block numbers are `enum ArenaBlockId` (`include/arena.h`), the entry-id bases
`enum NdataId` (`include/ndata.h`); the loaders are in `src/main/resource.c`. "Selector"
below is the stage number (`resource_GetStage`).

| Reader | NDATA ids | Group | Arena block | Evidence |
|--------|-----------|-------|-------------|----------|
| `ndata_ReadStage(selector, dst)` = `ndata_Read(selector + 0x27)` | `0x27..0x4C` for selector 0..37 | DATA2 stage `S<selector>` | 7 (stage) | `src/main/ndata.c`; `resource_LoadStage` opens block 7 and reads into it or into the staging buffer (`src/main/resource.c`); selector = `g_fight_stage` (`src/main/fight.c`) or `(control_word & 0x3F) - 1` of a motion block (`src/main/cutscene.c`) |
| `ndata_ReadTinyModel(id, dst)` = `ndata_Read(id + 0x4D)` | `0x4D..0x67`, or `0x68..0x82` in grayscale mode | DATA5 `T<id>` / DATA6 `TINYM<id>` (tiny models) | 8 (tiny model) | `src/main/ndata.c`; caller `src/main/resource.c` adds `0x1B` when `tex_IsGrayscale()`; ids from `g_model_tiny_ids` (`src/main/resource_data.c`: 0x00..0x1A, `MODEL_TINY_NONE` = none; read in `src/main/model_tiny.c`); `cutscene_Start` frees block 8 with "Destruction tiny model." when the arena is short (`src/main/cutscene.c`) |
| `cutscene_LoadBlock(ndata_id)` = `ndata_ReadChecked(ndata_id)` | `0x131..0x381` (`NDATA_MOTIONS` + the cutscene's number) | DATA7 `MARcc_mm` | 0xA (motion playback) | `src/main/resource.c`, caller `src/main/cutscene.c`; move callback `resource_OnCutsceneMoved` -> `cutscene_Rebase` (`src/main/resource.c`) |

The selector ranges are data facts, not code bounds: no reader range-checks its argument.
Stage selectors 0..37 are exactly the 38 files whose header the stage loader parses. Its
per-selector cases read header words past the common five only for selectors 3
(`STAGE_TATARA_SHRINE`), 4 (`STAGE_BUS_STOP`), 7 (`STAGE_BAMBOO_THICKET`), 13
(`STAGE_CASTLE_ROOFTOP`), 18 (`STAGE_LUMBERYARD`) and 34 (`STAGE_CHANBARA`)
(`src/main/resource.c`), and those six files (`0x2A`, `0x2B`, `0x2E`,
`0x34`, `0x39`, `0x49`) are the only stage files whose header count word exceeds 6 (8, 8,
8, 9, 8, 21). Selector 38 or more would read `T00` and the later tiny models, whose 1-word
header the stage loader does not fit.

**Arena block 9 has no reader and no caller.** Its three accessors, `resource_OpenSpareBlock`
(`arena_OpenBlock(9)`), `resource_CloseSpareBlock` (peak, end, no-op move callback `resource_OnSpareBlockMoved`) and
`resource_ResizeSpareBlock` (`arena_Resize(9, n)`) (`src/main/resource.c`), are referenced by no
`jal`, `j`, data word or `%hi/%lo` pair in `SLUS_006.63` or `MOVOVL.EXE` (word scan of
both images for the call encodings and the addresses; `%hi/%lo` and
named references searched in `asm/funcs/` and `src/`). No
other arena call passes 9: every `arena_*` call in `asm/funcs/` and `src/` takes a constant
6, 7, 8 or 0xA, or slot `n` / `n + 3` for slots 0..2. No `ndata_Read*` reads into it. The
shipped game never fills block 9, so this executable doesn't show what it held.

Arena blocks in use: 0..2 slot objects (`ARENA_MODEL` + slot), 3..5 their asset blocks
(`ARENA_MODEL_DATA` + slot; `src/main/resource.c`), 6 weapons (`ARENA_WEAPONS`,
`resource_LoadWeaponFile`), 7 stage (`ARENA_STAGE`), 8 tiny model (`ARENA_TINY_MODEL`),
9 unused (`ARENA_SPARE`), 0xA motion playback (`ARENA_CUTSCENE`).

**Stage header section 3 (word 3 up to word 4) is a counted list of 20-byte records that
nothing in the shipped game reads.** `resource_LoadStage` stores its address in
`g_resource_stage_unused_list` (`src/main/resource.c`); the stage-area path (`resource_SelectStageArea`) stores block 7's
base plus the third offset word of that subblock's record instead (`resource_SelectStageArea`) and the
move callback rebases it. Layout, in
all 38 files and in `S34`'s four stage areas (third words `0x4FD8`, `0x9A14`,
`0xD6EC`, `0x110E4`): one u32 count, then count records of five s32, and the section is
exactly `4 + 20 * count` bytes. 25 files (and all four `S34` subblocks) hold count 0; 13 hold
records: `S03` 7, `S06` 1, `S07` 3, `S10` 1, `S12` 3, `S15` 5, `S16` 1, `S19` 3, `S23` 2,
`S24` 1, `S25` 1, `S27` 1, `S31` 4. Words 0-1 and 2-3 of a record are shaped like two
coordinate pairs (shape only, no reader; values within ±24000; records share endpoints in
`S03`, `S07`, `S19` and `S31`); word 4 takes only the values -4000, -2100, -2000, 0, 499
and 1999; `S15` repeats its last record. Example `S06`: `(-1999, -5000) (-1999, 4999) 0`.
No reader exists: in `SLUS_006.63` and `MOVOVL.EXE` the only accesses to
`%gp_rel(g_resource_stage_unused_list)` (`0x2E4($gp)`) are the two stores, the move callback's rebase (load,
add, store) and the getter `resource_GetStageUnusedList` (`src/main/resource.c`), which no `jal`/`j`,
data word or `%hi/%lo` pair references; neither image holds the word
`0x800A33B0` (word scan of both images). The records' use is therefore not shown by this
executable.

**DATA7 header word 0 is the count of section offsets before the end offset.** In all 455
live `MARcc_mm` files the header is word 0 = `n`, then `n` ascending file-relative section
offsets, then the end offset (word `n + 1`, word-aligned, within the file and less than one
sector short of its length); the first section starts at `4 * (n + 2)`, right after the header.
`cutscene_LoadBlock` reads word `n + 1` as the block end (`src/main/resource.c`). `n`
is 7 in 443 files, 5 in 11 (ids `0x152 0x15C 0x165 0x168 0x1A5 0x1C4 0x1FD 0x21B 0x239
0x25C 0x264`) and 6 in one (`0x1A2`). With `n = 5` there are no draw-set tracks and with `n = 6`
only track 0, so `CutsceneFile`'s last one or two offsets then read the end offset or the
first data word. The control word agrees in every file: the bits that make
`cutscene_Advance` read draw-set track offsets 0 / 1 (`0x80` / `0x40`,
`src/main/cutscene.c`) are clear in all `n = 5` files, only `0x80` is set in the `n = 6`
file, and the `n = 7` files set `0xC0` (231), `0x80` (179) or `0x40` (33).
The model files (DATA0) use the same shape, with the TIM-group section last (word `n`) and the
end at word `n + 1` (`src/main/model.c`).

**The six slot-1 assets with VRAM row lists are the Ninja and the Slash Mode opponents.**
`g_model_tint_clut_list_ids` (`src/main/model.c`) gives a list only to model file
indices 14, 29 and 58..61. The slot loader's remap (`a0 >= 0x1F` -> `a0 - 0x1B`,
`src/main/ndata.c`) turns them into NDATA ids:

| Asset | List | NDATA id | File | Size | Used as |
|------:|------|---------:|------|-----:|---------|
| 14 | 0 `g_model_tint_cluts_14` | 14 | `M111` | 143,360 | the Ninja: roster entry 14, `ROSTER_SIDEARM_1` (`g_fighter_roster_models`, `src/main/fight_data.c`) |
| 29 | 1 `g_model_tint_cluts_29` | 29 | `M211` | 157,696 | the Ninja: roster entry 29, `ROSTER_SIDEARM_2` |
| 58 | 2 `g_model_tint_cluts_58` | 31 | `M302` | 38,912 | Slash Mode opponent, step column 0 |
| 59 | 3 `g_model_tint_cluts_59` | 32 | `M303` | 53,248 | Slash Mode opponent, column 1 |
| 60 | 4 `g_model_tint_cluts_60` | 33 | `M304` | 47,104 | Slash Mode opponent, column 2 |
| 61 | 5 `g_model_tint_cluts_61` | 34 | `M305` | 51,200 | Slash Mode opponent, column 3 |

All six are fighter object assets (slot header, node templates, TIM groups). Roster
entries `ROSTER_SIDEARM_1` and `ROSTER_SIDEARM_2` are the Ninja (two of the entries that
have no name on the disc and are named for what they do: they draw a pistol with the off
hand). Practice mode sets its opponent to one or the other when `g_fight_practice_round` is 0,
picked by the player's `g_fighter_clans` (`fight_StartMode`, `src/main/fight_start.c`; the
`training-ninja` scenario's "fight against the Ninja"). Story command 16, `STORY_HENCHMEN`
(`story_SetHenchmen`, `src/main/story.c`) picks between the same two (`ROSTER_SIDEARM_2` when `g_story_chapter` is 1), sets a defeats-left count and tint operands, and the tint is applied
(`model_TintOpponent`) only when the story's opponent is a band of henchmen (`fight_Init`,
`src/main/fight.c`; `cutscene_Init`, `src/main/story.c`). Kannuki's stream (`g_story_script_kannuki`) opens with stage command `01 08` (stage 8, `STAGE_OUTSIDE_MEIKYOKAN`) and then this command (`10 07 10 03 33 24` at
`0x8008F222`: 7 defeats, the opening ninja run of `story-ninja-cross`). Assets 58..61 are
`g_chanbara_models` columns 0..3 (`src/main/fight_data.c`), the second fighter's model
only in `FIGHT_MODE_CHANBARA` (Slash Mode; `fight_NextChanbaraOpponent`,
`src/main/fight_modes.c`) and tinted with the opponent's three colour bytes
for kinds below the last (`fight_Init` and `fight_AfterDefeat`, `src/main/fight.c`). They play
the moves of roster entries `ROSTER_RED_SHADOW`, `ROSTER_MIKADO_STORY`, `ROSTER_SUMINAGASHI`
and `ROSTER_KANNUKI_STORY` (7, 2, 10, 0:
`g_chanbara_rosters`, `src/main/fight_data.c`). Column 4, the last opponent, is asset 30
(`M301`, `ROSTER_LAST_1`'s model) or 63 (`M306`, `ROSTER_LAST_2`'s), with no list. `M302..M305` are the only model files (ids 0..36) with header count 5: they
have no VAB section, so `model_FinishLoad` skips
`snd_LoadVab` (`src/main/model.c`). They are served from the Slash Mode pack
(`CHANBARA0/1`, `g_ndata_pack_sections` entries 1..4, `src/main/ndata.c`). The lists' VRAM
positions are not in the files: the TIM CLUT headers of all six store x = y = 0, so the
positions are assigned at load.

### Model file section 3: the cloth stream

`model_FinishLoad` stores `block_base + word 3` as
`model->cloth_params` (`src/main/model.c`); `cloth_RebaseModel` moves it with
the block (`src/main/cloth_model.c`). Section 1 (word 1) is the mesh table relocated as
`mesh_block` (`reloc_RelocateBlock`, `src/main/render.c`: count word, then
section-relative offsets); section 2 (word 2) is `(mesh id, node index)` u16 pairs ending `0xFFFF`,
mesh id -> `bones[node + 1]` (`model_AssignMeshes`, `src/main/model.c`).

Layout (s16 stream, read by `cloth_SetupModel`, `src/main/cloth_model.c`; the markers are the
`CLOTH_FILE_` values of `include/cloth.h`): one record per
mesh id from 0, `-3` ends the stream (a leading `-3`: no cloth). A record is
`-2` (mesh not cloth) or: groups `count, value, count point indices` repeated while `count >= 0`
(`cloth_ReadPins`, after setting every point's `SVECTOR.pad` to -256, `CLOTH_PIN_LOOSE`); then `-1` and collider records of 8 s16 `node, x0, y0, z0, x1, y1, z1, bound` (node as
in section 2, `bones[node + 1]`; `cloth_ReadColliders`) up to `-2`, or `-2`
directly. Points index the mesh's vertex list (section-1 entry: u16 count, u16, count × s16 xyz,
node-local; copied by `cloth_CreatePiece`). `cloth_SetupModel` skips the groups by
scanning to the first negative halfword, so a negative group value stops the scan early; the two
meshes with negative values (M002, M102 below) have no collider list, so the record parses the same.
The stream is word-padded with 0.

All 37 model files (ids `0x00..0x24`) carry a section 3; 16 hold only `-3`: ids `0x06 0x08 0x0E 0x12
0x13 0x18..0x1B 0x1D..0x22 0x24`. The values present are 0 (every soft mesh), -170, -60, -20 (two
meshes); no file stores a positive value or an explicit -256. Node 11 is `bones[12]`
(parent of both leg chains; its +x runs toward the legs, thigh nodes at x = 180), node 1 is
`bones[2]` (parent of the head and arm chains; +x toward the head at 468, shoulders at
289), node 2 is `bones[3]` (the head, no children) (`g_model_bones`,
`src/main/model.c`); colliders use these plus nodes 12 / 15 (the two thighs). Distances
are node-local units; "rows" are free points at the same distance from the pinned points. The
body-part words (head, neck, shoulder, chest, waist, hip, thigh, legs) are readings of the
template's shape, not names from code: node 2 is a childless leaf 468 out on node 1, which also
parents two 4-link chains; node 11 parents two 3-link chains.

| Model (id) | Mesh / node | Value | Points | Part of model (vertices) |
|---|---|---:|---|---|
| M000 (0x00) | 5 / 11 | 0 | 8-13 | 6-point cluster at waist height (x -38..-4); the other 8 (default) are two 4-point flaps at z ±49..115 hanging to x 308 (rows 150, 327 from the pins) |
| M001 (0x01) | 3 / 2 | 0 | 12-17 | 6 points within 121 of the head node; 12 free in rows 272, 604, 970 out (a tail to ~940 from the head) |
| M001 | 4 / 11 | 0 | 8-13 | 6 at the waist (x -47..-14, y -110..-123); 8 free on the -y side to x 420 (rows 234, 457) |
| M002 (0x02) | 4 / 2 | 0 | 1 3 6 10-18 | 12-point cluster 145..220 from the head node (the root) |
| M002 | 4 / 2 | -170 | 4 8 9 | first segment, 159..179 from the root cluster |
| M002 | 4 / 2 | -60 | 2 5 7 | second segment, 359..374 out |
| M002 | 4 / 2 | -20 | 0 | single tip point, 623 out (all 19 points listed: none stays -256) |
| M002 | 5 / 11 | 0 | 1 6 7 9 10 | 5-point row across the waist (x ≈ 0, z ±119); 6 free on the +y side to x 406 |
| M002 | 6 / 11 | 0 | 1 6 7 9 10 | same row (x 16..36); 6 free on the -y side to x 419 |
| M002 | 9 / 1 | 0 | 0 1 | 2 points at x ≈ 404 (below the head node, the neck); 6 free in pairs 237..656 out, down to x -131, y -445 |
| M100 (0x03) | 9 / 11 | 0 | 0 1 2 | 3-point row at x 52, z ±140, -y side; 6 free in two rows to x 460 |
| M100 | 11 / 11 | 0 | 0 1 2 | same row on the +y side; 6 free to x 471 |
| M101 (0x04) | 5 / 1 | 0 | 12-17 | 6 at chest height (x 227..271, y ≈ -127); 12 free on the -y side in rows 296, 587, 920, down to x -679 (below the hips), z ±138 |
| M102 (0x05) | 6 / 2 | 0 / -170 / -60 / -20 | 22 points (1 3 6 10-28) / 4 8 9 / 2 5 7 / 0 | as M002 mesh 4: root cluster, then segments 158..180, 382..394, 647 out |
| M102 | 7 / 11 | 0 | 0 1 2 | 3-point waist row (z ±137), +y side; 6 free to x 396 |
| M102 | 8 / 11 | 0 | 0 1 2 | same, -y side; 6 free to x 427 |
| M104 (0x07) | 3 / 2 | 0 | 0-5 | 6-point row across the head (y ±113, z ≈ 0); 11 free fanning to z 270 (≤ 279 from the row) |
| M106 (0x09) | 4 / 1 | 0 | 0-5 | 6-point row at shoulder height (x 161..237) spanning z ±347, y ≈ -155; 8 free in rows 233..297 and 591..612 to x -375, flaring to z ±487, y -423 |
| M107 (0x0A) | 3 / 2 | 0 | 12-17 | 6 within 98 of the head node; 12 free in rows 271, 612, 1015 (tail to ~1000) |
| M108 (0x0B) | 6 / 1 | 0 | 0 1 | 2 points at x ≈ 380 (neck); 6 free in pairs 306..1122 out, to x -510, y -673 |
| M109 (0x0C) | 3 / 2 | 0 | 0 1 2 4 6 7 | 6-point row across the head (y ±94); 13 free fanning to z 251 (≤ 280) |
| M110 (0x0D) | 3 / 2 | 0 | 0-3 | 4 points 167..194 from the head node; 6 free in rows 132, 257, 416 |
| M110 | 4 / 2 | 0 | 0-4 | 5 points ≤ 111 from the head node; 10 free within 41..167 (a short flap) |
| M110 | 5 / 11 | 0 | 0 1 2 | 3-point waist row (z ±87), -y side; 9 free in rows 210, 386, 552 to x 560 |
| M003 (0x0F) | 3 / 11 | 0 | 8-13 | 6 at the waist; 8 free in two flaps (z ±105) to x 317 (rows 125, 285) |
| M004 (0x10) | 3 / 2 | 0 | 12-17 | 6 at the head node; 12 free in rows 168, 385, 601 |
| M004 | 4 / 11 | 0 | 8-13 | as M003 (flaps to x 310) |
| M005 (0x11) | 4 / 2 | 0 | 12-17 | 6 at the head node; 12 free in rows 150, 358, 578 |
| M202 (0x14) | 5 / 11 | 0 | 4-9 | 6 at the waist; 8 free (0-3, 10-13) in two flaps (z ±142) to x 250 |
| M203 (0x15) | 6 / 2 | 0 | 12-17 | 6 at the head node; 12 free in rows 296, 580, 892 |
| M204 (0x16) | 7 / 11 | 0 | 8-13 | 6 at one hip (z 96..142, the node-12 thigh side); 8 free to x 431 on that side |
| M205 (0x17) | 5 / 1 | 0 | 0-4 | 5-point row at shoulder height (x ≈ 236) spanning z ±335; 12 free in rows 327, 647, 955 to x -720, flaring to z ±544 |
| M210 (0x1C) | 9 / 11 | 0 | 0 1 2 | 3-point waist row (z ±125), +y side; 9 free in rows 295, 555, 818 to x 727 |
| M210 | 10 / 11 | 0 | 0 1 2 | same, -y side; 9 free to x 709 |
| M300 (0x23) | 3 / 11 | 0 | 0 1 2 | 3-point waist row (z ±80), -y side; 6 free in rows 238, 581, 875 to x 861 |
| M300 | 4 / 11 | 0 | 0-8 | 9-point ring around the waist (x -9, radius ~130, open on the -y side); 18 free in rings at x 170, 313, 466 flaring to z ±607, y 714 |
| M300 | 10 / 2 | 0 | 1 3 9 11 | 4 points 139 from the head node; 12 free in rows 387, 787, 1232 |

How the values are read (`ClothPoint.pin`, copied from `SVECTOR.pad` by
`cloth_Build`, `src/main/cloth.c`):
- `>= 0`: counted in the build centroid; no spring between two such points
  (`cloth_AddSpring`). In `cloth_Step`, 0 puts the point
  on its transformed rest vertex every step; > 0 adds `(anchor - pos) * w / 256` to a
  halved velocity, with `(0x100 - w) * 25` added to the y term before the `/ 256`.
- `< 0`: integrated with forces, ground, colliders and damping; -1..-255 first
  move the point `|w| / 256` of the way to its rest vertex; -256 (the default) skips that.
- The over-stretch correction moves the `to` end of a spring when its value is `< 0`, else the `from` end (`cloth_SolveSprings`).

## Contents of a typical entry

Entries hold opaque data of various formats. The first 64 bytes of entry 0:

```
06 00 00 00  20 00 00 00  bc 61 00 00  e0 61 00 00
10 62 00 00  54 67 00 00  1c 62 01 00  f8 46 02 00
09 00 00 00  2c 00 00 00  f0 47 00 00  a0 4a 00 00
50 4d 00 00  b8 52 00 00  fc 55 00 00  80 57 00 00
```

This looks like a small TOC: a u32 record count (6), a u32 header size (0x20),
then 6 u32 offsets growing monotonically. Most NDATA entries appear to be small
nested archives of this kind — the inspector tool below reports the TOC where
the leading word is plausibly a record count, otherwise dumps a hex preview.

Decoding individual record types (geometry, animations, audio) is out of scope
for this archive document; see the per-format docs for BBM (motion), STAGE_BIN
(stage geometry), etc.

## Inspector tools

### `tools/inspect_ndata.py` — list / extract NDATA entries

```
python tools/inspect_ndata.py <NDATA.INF> (--list | --extract <ID> [--out FILE]
                                           | --dump-all <OUTDIR>
                                           | --hex <ID> [--bytes N]
                                           | --stats)
```

(Exactly one of `--list` / `--extract` / `--hex` / `--dump-all` / `--stats` is
required. The output filename for `--extract` is given via `--out`, and the
`--hex` byte count via `--bytes` — they are NOT bare positional arguments.)

Examples:

```
# Print every directory entry with start/length/preview
python tools/inspect_ndata.py disc/NDATA/NDATA.INF --list | head

# Extract entry 0 to a named file (default name: ndata_0000.bin)
python tools/inspect_ndata.py disc/NDATA/NDATA.INF --extract 0 --out entry000.bin

# Dump first 128 bytes of entry 42
python tools/inspect_ndata.py disc/NDATA/NDATA.INF --hex 42 --bytes 128

# Extract every entry into a directory
python tools/inspect_ndata.py disc/NDATA/NDATA.INF --dump-all extracted/

# Directory-level summary statistics
python tools/inspect_ndata.py disc/NDATA/NDATA.INF --stats
```

The DAT file is auto-located alongside the INF (same directory, `.DAT`
extension).

### `tools/repack_ndata.py` — rebuild INF + DAT from entries

```
python tools/repack_ndata.py --template disc/NDATA/NDATA.INF --entries extracted/ --out rebuilt/
python tools/repack_ndata.py --roundtrip disc/NDATA/NDATA.INF   # extract+repack in memory, exit 1 unless identical
```

The inverse of `--dump-all`. The template INF supplies the 901 slots. The
760 chained slots (every non-sentinel slot except the leftovers 899/900; the
template must have them back to back from sector 0) are laid out again in id
order, each `ndata_NNNN.bin` rounded up to whole sectors with a zero-padded
tail, so an entry can change size and every later entry moves. Sentinels and
the leftovers keep their INF words verbatim; leftover entry files are ignored.
A file for a sentinel slot, an empty file for a chained slot, or any file in
the directory not named `ndata_NNNN.bin` for a slot is an error. Repacking the
unmodified dump reproduces both files byte-identically, and a repacked INF
works as the next template. A resized NDATA.DAT also needs the disc's ISO
directory and the EXE's static `g_cdrom_files` entry 0 (disc position +
size; declared and defined in `src/main/cdrom.c`) updated, and
every file after it moved (`tools/build_disc.py` does all of this);
the EXE's pre-filled `g_ndata_inf` copy is overwritten by `ndata_StartReadInf`
at init.

### `ndata_filemap.csv`: the file-ID -> name map

One row per INF slot, written by a script that parsed the name strings out of
`disc/SLUS_006.63` (by a script that is not kept; the table does not change). Columns: `file_id` (INF slot),
`name_vaddr`, `group`, `filename`, `content_guess`, `sentinel` (1 = `0xFFFF,0xFFFF`, a
named file the build left out; no byte range), `start_sector`, `len_sectors`,
`byte_offset`, `byte_length`. Slots 899 and 900 have no name.

## Verified by

* Hex-dump of all 3,604 bytes of `disc/NDATA/NDATA.INF` (901 little-endian (u16,
  u16) pairs).
* Cross-reference with the in-RAM mirror `g_ndata_inf` (`src/main/ndata.c`,
  same byte sequence).
* Loader `ndata_StartReadInf`, which CD-reads 2 sectors of NDATA.INF into it.
* Reader `ndata_Read`, which passes `g_ndata_inf[id]` (start_sector,
  length_sectors) to `cdrom_StartReadAt` on NDATA.DAT.
* Filename pool `0x80010DEC..0x8001528C` (the strings of `g_ndata_file_names` in
  `src/main/ndata.c`) — 899 `DATA<n>\<NAME>.DAT`
  strings, string `898 - i` naming slot `i`: the 139 sentinels fall exactly on
  the left-out names, and the code's id bases (`0x25`, `0x27`, `0x4D`, `0x83`,
  `0x10C`, `0x131`) land on group starts.
* Cumulative-sum sanity check: starting at sector 0, walking only the non-
  sentinel entries in INF order, the running total exactly reaches sector
  0x6ED8 = 28,376 = `len(NDATA.DAT) / 0x800`. No gaps.
