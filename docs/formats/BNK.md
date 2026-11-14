# BNK — VRAM block (effect textures and more)

Files: `disc/TIM2D/GAME.BNK`, `GAMECHAN.BNK`, `GAMEDAN.BNK`, `GAMESTO.BNK`,
`GAMETRA.BNK` — 5 files, each exactly 129,920 bytes.

Despite the name, a BNK is not a sound bank: it is raw 16-bit VRAM data with
no header, uploaded as two rectangles in the VRAM column x = 896..1023.

## Loader (`eff_LoadTextures`, `src/main/eff.c`)

It reads one BNK into a buffer, picked by `g_game_settings.menu_mode` (the row
`menu_RunTop` stores from the main row menu), and uploads it with
`LoadImage`. The files are of the disc's data group (`CDROM_GROUP_DATA`,
`enum CdromDataFile` in `include/cdrom.h`):

| `menu_mode` | File of the data group | File           |
|-----------|----------------|----------------|
| `FIGHT_MODE_STORY` (0) | `CDROM_DATA_GAMESTO_BNK` (60) | `GAMESTO.BNK`  |
| `FIGHT_MODE_CHANBARA` (3) | `CDROM_DATA_GAMECHAN_BNK` (47) | `GAMECHAN.BNK` |
| `FIGHT_MODE_PRACTICE` (2) | `CDROM_DATA_GAMETRA_BNK` (48) | `GAMETRA.BNK`  |
| `FIGHT_MODE_TEAM` (5) | `CDROM_DATA_GAMEDAN_BNK` (49) | `GAMEDAN.BNK`  |
| other     | `CDROM_DATA_GAME_BNK` (0) | `GAME.BNK`     |

## Layout

```
+0x00000  0x1DC00 bytes  VRAM rect (896, 0)   128 x 476 halfwords
+0x1DC00  0x01F80 bytes  VRAM rect (896, 476) 112 x 36 halfwords
                          (end of file = 129,920 bytes)
```

The five files are identical for their first 0x10000 bytes (VRAM rows
0..255) and differ after that. Rows 0..255 hold the effect textures: the
effect code in `src/main/eff.c` draws with texture pages 0x2E and 0x2F
(4-bit, x = 896 and 960, y = 0: `EFF_PAGE_LEFT` and `EFF_PAGE_RIGHT` with
`EFF_PAGE_ADD`). Nothing in that file uses a page at y = 256.

The per-file part is what is drawn over the scene in the file's mode. The
sheets of `src/main/hud.c` and `src/main/hud_data.c` have texture pages
0x1E and 0x1F (4-bit, x = 896 and 960, y = 256; two sheets have 0x0E, in
the shared rows) and colour tables at
x = 896..992, y = 508..511, which is in the 112 x 36 strip at (896, 476):
the counters and timers, the captions, the team battle's table
(`GAMEDAN.BNK`), chanbara's best times (`GAMECHAN.BNK`). The characters'
names of the last two are cells of the page at (960, 256)
(`g_hud_name_sheet`, `g_hud_name_cells` in `hud.c`: twenty names in the
characters' order, without the ones that have no name).

The old reading of these files as SPU-ADPCM sample data was wrong:
byte 1 of a 16-byte ADPCM block would be a flag value 0..7, and only
4,451..5,298 of each file's 8,120 blocks fit that.

## Tools

`tools/repack_bnk.py` writes each rect as a 16-bit TIM (no CLUT) carrying
its VRAM position, so `tools/repack_tim.py` turns it into a PNG and back,
and rebuilds the BNK from the two TIMs (their rects must stay as the loader
uploads them):

```
python tools/repack_bnk.py unpack disc/TIM2D/GAME.BNK out/   # main.tim, strip.tim
python tools/repack_tim.py decode out/main.tim main.png      # 16-bit RGBA view
python tools/repack_bnk.py pack out/ GAME.BNK                # rebuild
python tools/repack_bnk.py --roundtrip disc/TIM2D            # exit 1 unless every .BNK is byte-identical
```

A 16-bit PNG is a raw view of VRAM: 4- and 8-bit textures stored there look
scrambled in it, but editing it and encoding back is lossless. All five
files round-trip byte-identically, also through TIM -> PNG -> TIM.

`tools/inspect_bnk.py` predates this reading: its BNK mode prints ADPCM
block statistics, which do not apply.

## Verified by

* `eff_LoadTextures` (`src/main/eff.c`): the five `cdrom_GetFileIndex`
  reads and the two `LoadImage` rects; 128 x 476 x 2 + 112 x 36 x 2 =
  129,920, the file size.
* The CD-table entries (`g_cdrom_files`, group base `g_cdrom_group_first_file[2]` = 21)
  hold size 129,920 at those five indices; reading each entry's start
  sector from `Bushido Blade 2 (USA).bin` gives the file named in the table
  above, byte for byte.
* The sheets of `src/main/hud.c` and `src/main/hud_data.c`: all but two
  have `tp0` 0x1E or 0x1F (the two have 0x0E, the shared rows), and every
  one a colour table at x = 896..992, y = 508..511.

## Unverified / TODO

* Which sheet of `hud.c` and `hud_data.c` shows what in each of the five
  files' per-file part: the sheets were read in the source, the pictures
  were not matched to them one by one except the characters' names.
