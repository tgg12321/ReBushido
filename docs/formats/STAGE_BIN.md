# STAGE_BIN — Stage sound-bank and stage-card container files

Files (three related but distinct flavours):

| Directory  | Pattern        | Count | Per-file size      | Role                                       |
|------------|----------------|-------|--------------------|--------------------------------------------|
| `LOADSE/`  | `STAGE00..09`  | 10    | 128 KB .. 242 KB   | 6 VAB packs + billboard list               |
| `LOADSE1/` | `STAGE10..31`  | 22    | 127 KB .. 259 KB   | Same (continuation)                        |
| `disc/LOADSE1/STAGE32..37.BIN` | 6 | 132–222 KB | Continuation (the modes' own stages, and stage 37) |
| `U_PIC/`   | `STG00..29`    | 30    | exactly 19,240 B   | Story mode's stage cards: the stage's name and a view of it |
| `TIM2D/`   | `STG30..31`    | 2     | 19,240 B           | Same, for the last two stages              |

(Total: 38 STAGE\*.BIN + 32 STG\*.BIN = 70 stage-related BIN files.)

The two formats share no on-disc structure. STAGE\*.BIN carry the sound banks and
billboard list for one playable arena; STG\*.BIN carry the two pictures of
the stage card that story mode shows before a chapter.

## The stages

The number in a file's name is the stage's (`enum StageIndex`,
`include/stage.h`; `S00..S37` in the archive, [NDATA.md](NDATA.md), are
numbered the same). The names of stages 0 to 31 are read off the stage
cards, whose first picture is the name written out; the table gives them
as the enum has them:

| Stage | `enum StageIndex` | | Stage | `enum StageIndex` |
|---:|---|---|---:|---|
| 0 | `STAGE_MAIN_TATARA_SHRINE` | | 16 | `STAGE_UNDER_RAILWAY` |
| 1 | `STAGE_MEIKYOKAN_DOJO` | | 17 | `STAGE_WATER_TOWER_DAM` |
| 2 | `STAGE_SHAINTO_HQ` | | 18 | `STAGE_LUMBERYARD` |
| 3 | `STAGE_TATARA_SHRINE` | | 19 | `STAGE_LIGHTHOUSE` |
| 4 | `STAGE_BUS_STOP` | | 20 | `STAGE_KABUKI_STAGE` |
| 5 | `STAGE_LOST_BRIDGE` | | 21 | `STAGE_ABANDONED_SHIP` |
| 6 | `STAGE_TATARA_SHRINE_PATH` | | 22 | `STAGE_FERRY_DOCK` |
| 7 | `STAGE_BAMBOO_THICKET` | | 23 | `STAGE_SHAINTO_GATE` |
| 8 | `STAGE_OUTSIDE_MEIKYOKAN` | | 24 | `STAGE_PURPLE_CAVERN` |
| 9 | `STAGE_FUTURE_DUMP_SITE` | | 25 | `STAGE_ABANDONED_STATION` |
| 10 | `STAGE_SNAKE_CLIFF` | | 26 | `STAGE_RAILWAY` |
| 11 | `STAGE_KIMIGAURA_COAST` | | 27 | `STAGE_ABANDONED_TUNNEL` |
| 12 | `STAGE_ORANGE_TREE_FIELDS` | | 28 | `STAGE_ABANDONED_SHIPYARD` |
| 13 | `STAGE_CASTLE_ROOFTOP` | | 29 | `STAGE_BUILDING_ROOFTOP` |
| 14 | `STAGE_UNDER_LOST_BRIDGE` | | 30 | `STAGE_BUILDING_PARKING_LOT` |
| 15 | `STAGE_TERRACED_FIELDS` | | 31 | `STAGE_SHAINTO_CEMETERY` |

Stage 4's card is the one in Japanese only ("... basutei-mae", in front of
a bus stop); stage 8's reads "Outside the Meikyokan" and stage 14's "Under
the Lost Bridge".

Stages 32 to 36 are the modes' own and have no card: team battle's two
courts (`STAGE_TEAM_1`, `STAGE_TEAM_2`), chanbara's hall (`STAGE_CHANBARA`),
the hall of practice with the ninja (`STAGE_PRACTICE_1`) and a practice
round's (`STAGE_PRACTICE_2`). Stage 37 has no name: link mode uses it in
place of the castle rooftop, and nothing says how it differs.

---

## 1. STG\*.BIN — the stage card's two TIMs (U_PIC + TIM2D)

A trivial 8-byte wrapper around two inline TIM images:

```
+0x00  u32   off1   absolute offset to TIM #1 (always 0x00000008)
+0x04  u32   off2   absolute offset to TIM #2 (always 0x00000B48)
+0x08  TIM image #1
+0xB48 TIM image #2
+....  optional trailing raw 4-bit pixel block (unaligned, no TIM header)
```

Verified across `STG00.BIN`, `STG01.BIN`, `STG29.BIN`, `STG30.BIN`,
`STG31.BIN`. All files are exactly the same size (19,240 bytes) and have
identical wrapper bytes for the first 32 bytes. Internal TIM content differs
per stage.

For `STG00.BIN` (stage 0, `STAGE_MAIN_TATARA_SHRINE`):

```
TIM #1 at 0x008:   pmode=0 (4-bit), CLUT 1×16 @ VRAM (0,0),
                   image 256×22 px @ VRAM (0,0)
TIM #2 at 0xB48:   pmode=0 (4-bit), CLUT 2×16 @ VRAM (0,480),
                   image 256×127 px @ VRAM (0,0)
```

TIM #1 is the stage's name written out, TIM #2 a view of the stage.
`hud_LoadStageCard` (`src/main/hud.c`) reads file `CDROM_DATA_STG_BIN +
stage` of the disc's data group, makes the two offsets addresses, and
uploads each picture and its colour table with `LoadImage` to places of
its own (the name to (960, 384) with its colours at (960, 511), the view to
(896, 383) with its 32 colours at (896, 511)); the positions in the file
are not used.

TIM #2's image `block_length` is 8,140, half of what its 64 × 127 words
need: the ~8 KB after the block's stated end is the second half of the
view's pixels, not a block of its own. The loader uploads 64 × 127 words
from the block's start, which takes the file to its last byte.

Use `inspect_stage.py STG00.BIN` to enumerate and `tools/inspect_tim.py` to
PNG-decode the embedded TIMs (extract them first via inspect_stage.py
`--dump`).

---

## 2. STAGE\*.BIN — per-stage sound banks + billboard list (LOADSE + LOADSE1)

A 52-byte header (13 little-endian u32 words), six VAB packs back to back,
then a billboard list. No geometry or textures: every record is a VAB pack
with exactly the shape of an `.SE` file ([SE.md](SE.md)).

### Loader (`snd_LoadStageVabs`, `src/main/snd.c`)

The stage load in `src/main/fight.c` (`fight_LoadStage`) calls
`snd_LoadStageVabs(buf, stage, a, b)` with `a` and `b` the classes of the
two fighters' roster entries (`g_fighter_roster_classes`, one of five). It
reads file `stage + CDROM_DATA_STAGE_BIN` of the disc's data group
(`cdrom_GetFileIndex(CDROM_GROUP_DATA, stage + 9)`) into
`buf`, rebases header word 12 and passes it to `eff_SetFlames`
(`src/main/eff.c`), then hands record 0, record `a + 1` and record `b + 1`
(only two packs when `a == b`) to `snd_LoadVab` for the three VAB ids in
`g_snd_stage_vab_ids`, compacting the loaded packs to the start of `buf`.

### Header (offset 0x00, 52 bytes)

| Offset | Size     | Field          | Meaning                                       |
|--------|----------|----------------|-----------------------------------------------|
| 0x00   | u32 × 2  | rec0 off, size | record 0 (offset always 0x34)                 |
| 0x08   | u32 × 2  | rec1 off, size | record 1                                      |
| 0x10   | u32 × 2  | rec2 off, size | record 2                                      |
| 0x18   | u32 × 2  | rec3 off, size | record 3                                      |
| 0x20   | u32 × 2  | rec4 off, size | record 4                                      |
| 0x28   | u32 × 2  | rec5 off, size | record 5                                      |
| 0x30   | u32      | billboards     | offset of the billboard list                  |

In all 38 files the records are contiguous from 0x34 and the billboard list
starts where record 5 ends.

For `STAGE00.BIN` (128,988 bytes):

```
record 0: off=0x000034  size=0xE424  VH v6, ps 1 / ts 11 / vs 7
record 1: off=0x00E458  size=0x37A8  VH v6, ps 1 / ts 10 / vs 4
record 2: off=0x011C00  size=0x36F8
record 3: off=0x0152F8  size=0x3AE8
record 4: off=0x018DE0  size=0x34F8
record 5: off=0x01C2D8  size=0x34F8
billboards (word 0x30) = 0x1F7D0  → 12 bytes: the terminator alone
```

### Records

Each record is a VAB pack: 16-byte head (event table, VH and VB offsets, VB
size), event table, VH (`pBAV`, version 6 in all 228 records), 4 zero bytes,
VB, 4 zero bytes. Layout: [SE.md](SE.md). Record 0 is the stage's own bank
(`SND_VAB_STAGE`); records 1..5 are the banks of the five classes of roster
entry, picked by `a` / `b` above (`SND_VAB_CLASS_1`, `SND_VAB_CLASS_2`).
The class also picks the footprint a fighter leaves, which is why the
source guesses that it is footwear and the banks footsteps.

### Billboard list

12-byte entries (three s32, `Flame`, `include/eff.h`).
`eff_SetFlames` copies entries into `g_eff_flames` while word 0
has bit 0 set; the first entry with bit 0 clear ends the list. On disc the
list ends with 12 zero bytes at end of file. 29 stages have no billboards
(the terminator alone); the others hold 2 or 4 entries (24 or 48 bytes)
before it. `eff_DrawFlames` (`src/main/eff.c`) decodes an entry as:

```
word 0   bit 0 = present, x = word0 / 2
word 1   type = word1 & 7 (0..3 pick sprite size and frames; 4..7 keep
         the previous entry's), y = word1 / 8
word 2   z
```

`g_eff_flames` (`src/main/eff.c`) is six entries, so the copy holds at
most 5 entries plus the terminator (the disc files use at most 4). `repack_stage.py`
warns when a list holds more than 5.

### Repack

`tools/repack_stage.py` splits a file into one `repack_vab.py` directory per
record plus `billboards.bin`, and rebuilds it with the header recomputed, so
a record may change size (whole 4-byte words; VAG rules in [SE.md](SE.md)):

```
python tools/repack_stage.py unpack disc/LOADSE/STAGE00.BIN out/   # rec0/..rec5/, billboards.bin
python tools/repack_stage.py pack out/ STAGE00.BIN                 # rebuild
python tools/repack_stage.py --roundtrip disc/LOADSE1              # exit 1 unless every STAGE*.BIN is byte-identical
```

All 38 files round-trip byte-identically; both directions check the
billboard list's shape. Size limits: `fight_LoadStage` (`src/main/fight.c`)
panics when the compacted size `snd_LoadStageVabs` returns reaches 0x2519
(`SND_STAGE_VAB_ROOM` is 0x2518). That
size is the bytes ahead of the VB in the three loaded packs
(`snd_LoadVab` returns the VB offset), so it grows with the event table
and VH, not with VAGs. A file that grows also has to fit its disc slot (see
`tools/build_disc.py`).

---

## 3. Related multi-record container BIN files (`wrap5` family)

Other `.BIN` files in `disc/TIM2D/` open with a 5-u32 outer TOC instead of
the STAGE\*.BIN 13-u32 layout. The first 5 u32s are segment-start offsets;
duplicated values mean "this segment is empty / zero-length" (the value
between two equal pointers occupies zero bytes).

### Per-file findings

| File         | Size    | Outer 5-u32 TOC                                  | Embedded resources                                                        |
|--------------|--------:|--------------------------------------------------|---------------------------------------------------------------------------|
| `SEL.BIN`    | 774,388 | `0x1E33C, 0x49574, 0x49574, 0xA90F4, 0xB30F4`    | 1× VAB-VH at 0x1E3BC (v7, 2 progs / 28 tones / 28 VAGs, fsize=0x2B1B0)    |
| `SEL1.BIN`   | 774,364 | `0x1E324, 0x4955C, 0x4955C, 0xA90DC, 0xB30DC`    | 1× VAB-VH at 0x1E3A4 (same VAB schema as SEL.BIN; offsets shifted by -24) |
| `SEL2.BIN`   | 774,364 | (TOC matches SEL1.BIN)                           | 1× VAB-VH at 0x1E3A4 (same TOC + sound as SEL1; graphics segment differs) |
| `D_SEL.BIN`  | 477,936 | `0x00F70, 0x00F70, 0x00F70, 0x60AF0, 0x6AAF0`    | No TIM/VAB headers found — segments are raw VRAM-ready pages              |
| `NAR.BIN`    | 596,544 | `0x1DEC0, 0x1DEC0, 0x1DEC0, 0x7DA40, 0x87A40`    | 5× TIMs (128×184 8bpp+CLUT, 0x5E20 bytes each) at 0x820 + i×0x5E20        |
| `MOD.BIN`    | 656,904 | `0x15850, 0x40A88, 0x40A88, 0x01850, 0x0B850`    | 1× VAB-VH at 0x158D0 (same VAB schema as SEL/SEL1/SEL2)                   |
| `STAFF.BIN`  | 434,504 | `0x005C8, 0x005C8, 0x005C8, 0x60148, 0x60148`    | No TIM/VAB headers found — segments are raw VRAM-ready pages              |

The same VAB-VH schema (`v7, 2 progs, 28 tones, 28 VAGs, fsize=0x2B1B0`)
appears in `SEL/SEL1/SEL2.BIN` and `MOD.BIN`, suggesting these select-screen
containers share a common UI/menu sound bank.

`NAR.BIN`'s 5 TIMs are evenly stride'd at exactly 0x5E20 bytes (= 12 bytes
TIM header + 12 bytes CLUT sub-header + 0x200 CLUT bytes + 12 bytes pixel
sub-header + 128 × 184 = 23552 pixel bytes). The 5 portraits live in
segment 0 (before the first outer-TOC offset at 0x1DEC0); segments 3 and 4
hold large raw blocks (0x5FB80 + 0xA000 bytes) consistent with pre-rendered
VRAM upload pages.

### Outer-TOC interpretation

Common pattern across this family — segment boundaries come from the 5 outer
u32 values plus an implicit "header-end" at 0x14 (just after the TOC) and
an implicit "file-end" at EOF. Segment `i` runs from `boundary[i]` to
`boundary[i+1]`:

```
seg 0 :  0x14 .. outer[0]
seg 1 :  outer[0] .. outer[1]
seg 2 :  outer[1] .. outer[2]
seg 3 :  outer[2] .. outer[3]
seg 4 :  outer[3] .. outer[4]
seg 5 :  outer[4] .. EOF
```

Duplicated outer values collapse adjacent segments to zero length. The
typical layout these files use is **segment 0 = inner TOC + early payload,
segments 1–2 = empty markers, segment 3 = bulk graphics, segment 4 = small
trailing CLUT/page**.

### Inner secondary table

The 9 u32s following the outer-5 (at file offsets 0x14..0x37) form a
secondary table whose semantics are file-specific:
- For `D_SEL` and `NAR.BIN` the values are small (`0x44..0x378`) and look
  like sizes of fixed-format sub-records.
- For `SEL/SEL1/SEL2` the values are larger (`0xB0FC..0x18324`) and look
  like sub-segment sizes within the bulk graphics segment.
- For `STAFF.BIN` they are an ascending sequence (`0x44, 0xF8, 0x1AC, …`)
  consistent with an inner offset table for the credits text-page strip.

These have not been fully cross-checked against runtime loaders — the
schema is "one secondary u32 table per file, but the unit (size vs offset)
varies by file."

### MOD.BIN's +0x1C sheet list

In `MOD.BIN` the words after the head are the root's list offsets
(`MenuFile`, `include/menu.h`). The list at +0x1C (`top_sheets`, file 0x7F8)
holds 13 sheet-header offsets, 0-terminated. Decoded by loading the image the
way `sprite_LoadFile` does (`select.c`:
`image` 384x476 at VRAM (640,0), +0x59400 368x36 at (640,476),
`bottom_strip` 640x32 at (0,480)), then drawing each header's cells
through its tpage `(tp0 & 0xFE1F) + (tp1 << 7)` and CLUT `(cx, cy)`. All 13
are 4-bit. Readers are in `menu_pages.c`.

| Entry | Header | tpage / CLUT | Cells | Shows | Read by |
|---|---|---|---|---|---|
| 0 | 0x1094 | 0x1F / (16,481) | 2, 100x40 | first-person picture: a swordsman's back on a checker floor | `menu_DrawPovChoice` |
| 1 | 0x10CC | 0x1F / (32,481) | 2, 100x12 | caption "POV" | `menu_DrawPovChoice` |
| 2 | 0x1078 | 0x1F / (0,481) | 2, 100x40 | side-on picture: two duelists on the same floor | `menu_DrawSideViewChoice` |
| 3 | 0x10B0 | 0x1F / (32,481) | 2, 100x12 | caption "Side View" | `menu_DrawSideViewChoice` |
| 6 | 0x10E8 | 0x1F / (48,481) | 1 (+1 after it), 16x16 | arrow: cells[0] points left, cells[1] (32 px to its right) points right | Bit0 from cells[0], Bit1 from cells[1] |
| 9 | 0x1104 | 0x1F / (64,481) | 4, 210x26 | screen title: kanji banner over "TOP MENU" | `menu_DrawTopTitle` (from `menu_DrawTopRows`) |
| 10 | 0x1130 | 0x1F / (80,481) | 1, 50x1 | one-pixel line, dark to light gradient | `menu_DrawRowUnderline` |
| 11 | 0x1144 | 0x1F / (96,481) | 1, 45x1 at x+50 | one-pixel line, light gradient (continues 10) | `menu_DrawRowUnderline` |
| 4, 5, 7, 12 | 0x1158 | 0x1D / (0,489) | 8, 256x72 | the top 72 rows of the English mode-caption page: "Story Mode" .. "Group Battle Mode" and a sliver of "Link Mode" | [4]: `menu_DrawModeCaptions`, which nothing calls; 5, 7, 12: no reader |
| 8 | 0x1414 | 0x0A / (48,484) | 3, 38x148 | no picture of its own: its rect (u 0..37, v 92..239 on the key-config page) cuts across other sheets' texels (the pad picture, the EXIT / OK / N/A bars, the 1P / 2P divider, button icons) | no reader |

Header 0x1158 is also `white_page_sheets[0]`. Entries 0 to 3 and 6 are
the camera-view chooser of the mode menu (`menu_UpdateTop`, which calls
both drawers with the cursor row's `views`): `MENU_VIEW_POV` alone draws only the POV pair
and forces `first_person` to 1, `MENU_VIEW_SIDE` alone only the Side View pair and forces 0;
both draws both, plus the arrows, toggles `first_person` with left / right,
and draws the unchosen picture at half scale, dimmed (RGB 0x70). Entries 9 to
11 are fixed screen furniture of the same menu (title, row underline).
`menu_DrawTopRows` on that screen also reads `row_sheets` (the eight
kanji row names "決戦の間" .. "支度の間") and `mode_caption_sheets` (the English
caption of the cursor row), so the screen is not what sets +0x1C apart (other
lists serve other screens: `key_config_sheets`, `option_sheets`,
`card_menu_sheets`, ...). Common
subject: none. The read entries are two kinds of art (the view chooser and
screen furniture), and the other five are copies of another list's sheet or a
rect with no picture of its own.

### MOD.BIN's other lists

Decoded the same way (2026-11-11), to name the pages in `menu_pages.c`:

- `message_sheets` (+0x20): two entries a message, the second -1 where one
  sheet is enough; `message_sizes` (+0x48) gives each message's box. In
  order (`enum MenuMessage`): 0 "Checking Memory Card / Please wait / Do not
  remove Memory Card", 1 "No Bushido Blade 2 data found on Memory Card",
  2 "Loading ...", 3 "Load completed", 4 "Load failed", 5 "Not enough empty
  block space to save", 6 "Overwrite data? Previous data will be erased",
  7 "Saving ...", 8 "Save completed", 9 "Save failed", 10 "Memory Card has
  been removed", 11 "This data is destroyed / Cannot load", 12 "Memory Card
  is unformatted / Format?", 13 "Formatting Memory Card ...", 14 "Formatting
  completed", 15 "Formatting failed", 16 and 17 "Save current data on Memory
  Card?", 18 the answers (the first cell reads "YES"), 20 "Memory Card is
  unformatted". 19 (the question asked at boot, by its use) drew nothing
  with this decoding and was not read.
- `option_sheets` (+0x28): "Option Menu"; the rows "Monitor", "Difficulty",
  "Music", "Memory Card", "Key Configuration", "Return to Top Menu"; then
  the values "Color" / "B&W", "Hard" / "Easy", "On" / "Off".
- `practice_sheets` (+0x38): "Training Mode"; the rows "Practice", "Practice
  Round", "Return to Top Menu"; then the two descriptions ("Train with the
  Ninja. Adjust the Ninja's fighting pattern by pressing the Start button."
  and "Practice with the actual weapon. Select your opponent and weapon.
  Overcome your weakness.").
- `white_page_sheets` (+0x14): the names in English: "Story Mode", "Vs. CPU
  Mode", "Training Mode", "Vs. Mode", "Group Battle Mode", "Link Mode",
  "Slash Mode", "Option Menu", "Memory Card", "Key Configuration".

### D_SEL.BIN's +0x14 sheet list

In `D_SEL.BIN` the list at +0x14 (`TeamSelectFile.name_sheets`, file 0x280) holds
22 sheet-header offsets, 0-terminated, all distinct and in no other list.
Decoded the same way as MOD.BIN's +0x1C (`select_LoadTeam` makes no upload
of its own: `sprite_LoadFile(6, ...)` (`CDROM_DATA_D_SEL_BIN`)
puts the image at (640,0), the bottom strip, CLUTs included, at (0,480)). All
22 are 4-bit. Entries 1 to 20 carry three headers ahead of one cell table
(+0x24) holding three placements of the same texels; the headers differ only
in CLUT: (0,480) grey, (48,480) blue-tinted, (64,480) red-tinted.
Readers are in `select_team.c`.

| Entry | Header | tpage / CLUT | Cells | Shows | Read by |
|---|---|---|---|---|---|
| 0 | 0xD90 | 0x0B / (0,483) | 2 + 2, 64x64 | two oval emblems, 128x64 each: cells[0..1] leaves / feathers, cells[2..3] a snake coiled round a sword | `select_DrawTeamClans` (both, at y 0x36 and 0x90); `select_DrawTeamFighters` (the half the side's clan picks) |
| 1 to 20 | 0x75C .. 0xC48, not in list order | 0x0A / (0,480), (48,480), (64,480) | 1 or 2 per placement, 11 high | one English name label each: 1 KANNUKI, 2 TATSUMI, 3 MIKADO, 4 UTSUSEMI, 5 RED SHADOW, 6 TSUBAME, 7 SAZANKA, 8 SUMINAGASHI, 9 NIGHTSTALKER, 10 MATSUMUSHI, 11 GENGORO, 12 KAUN, 13 JO, 14 HONGOU, 15 KATZE, 16 HIGHWAYMAN, 17 CHIHIRO, 18 UTAMARU, 19 TONY UMEDA, 20 ISOHACHI | as `[value + 1]`: `select_DrawTeamFighters` grid cells (placement 0) and pick column (placement 1); `select_DrawTeamWeapons` (placement 2); [1] also gives both their `SetDrawMode` tpage |
| 21 | 0xC84 | 0x0A / (32,480) | 2, 63x12 + 36x12 | a 99-px strike-through: rows 5-7 of 12, red core, pink edges, rest transparent | `select_DrawTeamStrike` |

`g_select_team_grid` (`menu_data.c`) puts entries 1 to 10 on grid page 0
and 11 to 20 on page 1, the page that also picks [0]'s emblem half. The
cursor row draws with header 1 + `side` (blue for the first side, red for
the second). [21] is drawn over a grid name that is unavailable (opaque) or
already in that side's team (semi-transparent); it is also the sheet
`[0x14 + 1]` that `select_DrawTeamFighters`'s pick column reads when
`select_HandleTeamFighters` stores the placeholder 0x14 (`TEAM_NO_FIGHTER`),
which views it as three headers, so past its cells into unreferenced bytes. Every
entry has a reader; no header repeats. Common subject: the fighters of
character select. [1] to [21] are indexed by the screen's fighter (`enum
SelectFighter` in `include/select.h`, `SELECT_KANNUKI` to `SELECT_ISOHACHI`:
`value + 1`, the placeholder 0x14 included) and [0] holds one emblem per
clan's page; the only
read outside the grid / pick screens is [0] in `select_DrawTeamClans`,
which shows both emblems.

### Inspector tools

```
python tools/inspect_stage.py disc/TIM2D/SEL.BIN      # outer-TOC summary
python tools/inspect_stage.py disc/TIM2D/SEL.BIN --dump OUT   # segment dump
```

To recover the embedded TIM/VAB resources catalogued above, extract the
segments with `inspect_stage.py --dump`, then run `inspect_tim.py` on the TIM
segments and `inspect_bnk.py` on any VAB (`pBAV`) segment. (The per-file
findings table was produced this way — by dumping segments and validating each
embedded TIM/VAB header individually.)

---

## Inspector tool

`tools/inspect_stage.py` — list / extract records from any of the BIN
container variants.

```
python tools/inspect_stage.py FILE.BIN              auto-detect + summary
python tools/inspect_stage.py FILE.BIN --dump OUT/  extract every record
python tools/inspect_stage.py FILE.BIN --hex N      hex-dump record/segment N
python tools/inspect_stage.py DIR/                  summarise every BIN in dir
```

The tool detects:
* `STG\*.BIN` "2-TIM wrapper" by the header `08 00 00 00 48 0B 00 00`.
* `STAGE\*.BIN` by `0x34` as the first u32 plus a valid 6-pair TOC.
* Generic "5-u32 TOC" BIN (SEL/NAR/MOD/STAFF/D_SEL family) by heuristic.

It does not interpret inner VABs or TIMs — for that, extract records with
`--dump`, split `STAGE*.BIN` packs with `repack_stage.py` / `repack_vab.py`,
and feed any TIMs to `inspect_tim.py`.

## Verified by

* Hex-dump of `disc/LOADSE/STAGE00.BIN` header (`asm/funcs/...` not directly
  involved — the format is data-only).
* Located 6 `pBAV` VAB-header magics in `STAGE00.BIN` exactly at the offsets
  predicted by the (offset, size) TOC pairs: `0x80, 0xE488, 0x11C30, 0x15328,
  0x18E10, 0x1C308`.
* Round-tripped `STG00.BIN` through `inspect_stage.py` and `inspect_tim.py`:
  the embedded TIMs decode to valid 256x22 and 256x127 4-bit indexed images.
* Verified all 32 `STG*.BIN` files are exactly 19,240 bytes with identical
  wrapper bytes at offsets 0..7.
* `STAGE*.BIN`: `tools/repack_stage.py --roundtrip` over `disc/LOADSE` and
  `disc/LOADSE1` (every record passes `repack_vab.py`'s VH / size-table
  checks; header and billboard-list checks as in §2), read against
  `snd_LoadStageVabs`, `eff_SetFlames` and `eff_DrawFlames`.

## Unverified / TODO

* What the five classes of `g_fighter_roster_classes` are (which of
  `STAGE*.BIN` records 1..5 a roster entry gets: footwear is a guess), and
  what each billboard type draws.
* Semantics of the 9-u32 secondary table in the `wrap5` family — confirmed
  to vary per file (mix of sizes and offsets), needs per-loader trace.
* Schema of the raw VRAM-ready blocks in segments 3 and 4 of `D_SEL.BIN`,
  `STAFF.BIN`, and `SEL/SEL1/SEL2.BIN` (no TIM headers — direct VRAM upload
  via `LoadImage` / `LoadClut`, format implied by destination rect).
* What distinguishes `SEL1.BIN` from `SEL2.BIN` — they share the outer TOC
  and embedded sound bank but their bulk graphics segments (file offsets
  0x49565..0xB50B9) differ in ~5.6% of bytes, suggesting two select-screen
  variants for distinct game modes; runtime trace would confirm.
