# TIM — PlayStation texture image

Files:
* `disc/TIM2D/*.TIM` (13 files: 10 × `LAS_*.TIM`, `LINK.TIM`, `LINK_M.TIM`,
  `SEA2_16.TIM`)
* `disc/STR/TITLE.TIM`

(14 TIM files total across the disc.)

TIM is the PlayStation SDK standard texture format. BB2 uses the unmodified
format throughout and reads it itself: `tex_LoadTim` (`src/main/tex.c`) for
the TIMs inside the archive's model and stage files, and a loader of its own
for each of the disc's `.TIM` files (below).

The byte layout below is the standard one (cross-referenced against
[PSX/TIM format on FF8 Modding Wiki][1] and the
[Just Solve the File Format Problem wiki][2]); this document additionally
catalogues which BB2 files use which PMODE / CLUT layouts and notes one
non-standard behaviour observed in `TITLE.TIM`.

## Header (8 bytes, little-endian)

| Offset | Size | Field   | Value / meaning                            |
|--------|------|---------|--------------------------------------------|
| 0x00   | 1    | tag     | Always `0x10` — identifies a TIM file      |
| 0x01   | 1    | version | Always `0x00` — only version known         |
| 0x02   | 2    | _pad    | Reserved, zero                             |
| 0x04   | 4    | flags   | bit-0..1 = PMODE, bit-3 = CLUT present     |

```
flags bits:
  [0..1] PMODE: 00=4-bit indexed
                01=8-bit indexed
                10=16-bit BGR555 direct
                11=24-bit BGR888 direct
  [3]    CLUT present (only meaningful when PMODE = 00 or 01)
  others reserved
```

The CLUT block (if present) immediately follows the 8-byte header; the image
block follows the CLUT (or the header if no CLUT).

## CLUT block (only when CLUT bit set)

| Offset | Size | Field           |
|--------|------|-----------------|
| 0x00   | 4    | block_length    | total CLUT block size in bytes, **including this header** |
| 0x04   | 2    | dx              | VRAM destination X (16-pixel aligned) |
| 0x06   | 2    | dy              | VRAM destination Y                    |
| 0x08   | 2    | width           | CLUT width in 16-bit entries          |
| 0x0A   | 2    | height          | number of CLUTs (palettes)            |
| 0x0C   | W*H*2| palette_data    | 16-bit BGR555 colour entries          |

A 4-bit image with one 16-colour palette has `width=16, height=1`. An 8-bit
image typically has `width=256, height=1`. Stacking palettes vertically allows
several CLUTs to share an image (the second picture of a stage card,
`STG*.BIN`, has two rows: [STAGE_BIN.md](STAGE_BIN.md)).

## Image block (always present)

| Offset | Size | Field           |
|--------|------|-----------------|
| 0x00   | 4    | block_length    | total image block size in bytes, **including this header** |
| 0x04   | 2    | dx              | VRAM destination X |
| 0x06   | 2    | dy              | VRAM destination Y |
| 0x08   | 2    | width           | image data width in 16-bit words (NOT pixels) |
| 0x0A   | 2    | height          | image height in pixel rows |
| 0x0C   | W*H*2| image_data      | raw pixels packed into 16-bit words |

The `width` field is measured in **16-bit words**, not pixels. Pixels per
16-bit word depend on PMODE:

* PMODE 0 (4-bit): 4 px per word ⇒ pixel width = `width * 4`
* PMODE 1 (8-bit): 2 px per word ⇒ pixel width = `width * 2`
* PMODE 2 (16-bit): 1 px per word ⇒ pixel width = `width`
* PMODE 3 (24-bit): packed as 3-byte BGR, two pixels span 3 words

## 16-bit color encoding (BGR555)

```
bit  15  14   ..  10   9   ..  5   4   ..  0
     [STP][B (5)         ][G (5)        ][R (5)         ]
```

STP = "semi-transparency" / "STP" bit — when 0 and RGB = 0 the pixel is
fully transparent; when 1 it participates in semi-transparency blending. PSX
hardware treats the BGR fields as the colour and the STP bit as an alpha-like
mask.

## BB2-specific catalogue (verified)

| File                  | size    | PMODE  | CLUT             | image (px)    | notes                |
|-----------------------|--------:|--------|------------------|---------------|----------------------|
| `TIM2D/LAS_CN.TIM`    | 32,832  | 4-bit  | 1 × 16 @ (0,480) | 256 × 256 px* | an ending's picture  |
| `TIM2D/LAS_CS.TIM`    | 32,832  | 4-bit  | 1 × 16 @ (0,480) | 256 × 256 px* | an ending's picture  |
| `TIM2D/LAS_CT.TIM` ... | 32,832 | 4-bit  | 1 × 16 @ (0,480) | 256 × 256 px* | (7 more: `LAS_SN`, `LAS_SS`, `LAS_KN`, `LAS_KS`, `LAS_DN`, `LAS_DS`, and `LAS_DD`, which the game does not read) |
| `TIM2D/LINK.TIM`      | 307,220 | 16-bit | (none)           | 640 × 240 px  | the picture shown while the link cable connects |
| `TIM2D/LINK_M.TIM`    | 307,220 | 16-bit | (none)           | 640 × 240 px  | `LINK.TIM` in grayscale |
| `TIM2D/SEA2_16.TIM`   | 24,596  | 16-bit | (none)           | 256 × 48 px   | the boot image       |
| `STR/TITLE.TIM`       | 153,620 | 16-bit | (none)           | 320 × 240 px  | title screen: the still shown after the opening movie |

The nine `LAS_*.TIM` the game reads are files `CDROM_DATA_LAS_TIM` + 0 to 8
of the disc's data group (`enum CdromDataFile`, `include/cdrom.h`), in the
order `LAS_SN`, `LAS_SS`, `LAS_KN`, `LAS_KS`, `LAS_DN`, `LAS_DS`, `LAS_CN`,
`LAS_CS`, `LAS_CT`; `LINK.TIM`, `LINK_M.TIM` and `SEA2_16.TIM` are
`CDROM_DATA_LINK_TIM`, `CDROM_DATA_LINK_M_TIM` and
`CDROM_DATA_SEA2_16_TIM`. `LAS_DD.TIM` is not in the game's file table.

*The LAS_* files report `width=64` in the header for 4-bit PMODE, which means
64 × 4 = 256 pixels wide. With height = 256 → effective 256 × 256 4-bit image
= 32,768 bytes of pixel data + 12 B image-header + 8 B file-header + 32 B CLUT
data + 12 B CLUT-header = 32,832 bytes. Matches exactly.

### Non-standard `image.block_length` in `TITLE.TIM`

`TITLE.TIM` reports `block_length = 0x000A8C0C` in its image block — a value
that overshoots the actual 153,612-byte block. The file is otherwise standard:
flags=0x00000002 (PMODE 2, no CLUT), DX=0, DY=0, W=320, H=240, and the trailing
153,600 bytes are valid 320×240 BGR555 pixel data, so the file decodes
correctly when the loader trusts `W*H*2` and ignores `block_length`. The PsyQ
SDK loader does exactly this (it uses W/H, not block_length, to determine the
VRAM DMA size).

`LINK.TIM`, `LINK_M.TIM`, `SEA2_16.TIM`, and every `LAS_*.TIM` carry the
correct `block_length` (`0x4B00C = 307,212`, `0x600C = 24,588`, `0x400C =
16,396`). Only `TITLE.TIM` is anomalous — likely a quirk of the screenshot/
authoring tool used to generate the title image.

## Loader

`tex_LoadTim(tim, x, y, clut_x, clut_y)` (`src/main/tex.c`) checks the magic,
reads the CLUT flag, walks the CLUT block (if any) and the image block, and
uploads each with `LoadImage` at the position the caller gives: the
position in the file is ignored. In grayscale mode the CLUT is converted
before it is uploaded. Sony's `OpenTIM` / `ReadTIM` are not used. It loads
the TIMs of a model's or a stage's texture groups (`tex_LoadGroup` and the
like).

The disc's `.TIM` files do not go through it. Each is read whole and
uploaded with `LoadImage` from fixed offsets into the file, to a place the
code gives: an ending's picture by `hud_LoadEndingPicture`
(`src/main/hud.c`), the link cable's picture by `menu_LoadLinkPicture`
(`src/main/menu_pages.c`), the boot image by `disp_ShowBootImage`
(`src/main/main.c`), and `TITLE.TIM` by the movie overlay
([MOVOVL.md](MOVOVL.md)).

## Inspector tool

`tools/inspect_tim.py` — prints TIM headers, validates blocks, and can dump
the image-block pixel rectangle.

```
python tools/inspect_tim.py file.tim                  summary
python tools/inspect_tim.py file.tim --verbose        full header dump
python tools/inspect_tim.py file.tim --raw out.bin    dump pixel bytes
python tools/inspect_tim.py file.tim --png out.png    decode to PNG (if PIL)
python tools/inspect_tim.py DIR/                      summarise dir
```

PNG decoding handles PMODE 0/1/2 (the only modes BB2 uses); PMODE 3 (24-bit
direct) is supported but unused in shipped BB2 files. Indexed PNG export uses
the first CLUT row; subsequent CLUTs in a multi-palette TIM can be selected
via `--clut N`.

## Repack tool

`tools/repack_tim.py` converts a TIM to an editable PNG and back (PMODE 0/1/2):

```
python tools/repack_tim.py decode file.tim out.png [--clut N]
python tools/repack_tim.py encode out.png file.tim
python tools/repack_tim.py --roundtrip disc/TIM2D        # exit 1 unless every TIM is byte-identical
```

The PNG carries every non-pixel byte (header, CLUT block, image-block header,
trailing bytes) as JSON in a `bb2tim` text chunk, so `encode` needs a PNG that
came from `decode`. 4/8-bit TIMs become indexed PNGs (indices = texels, palette
= CLUT row N; edited palette entries are written back to that row, keeping each
entry's STP bit). 16-bit TIMs become RGBA: alpha 0 = word 0x0000, 128 = STP set,
255 = STP clear (on encode, 1..254 sets STP); the PNG must stay RGBA. Colours are
cut from 8 to 5 bits per channel on encode, so anything darker than 8 per channel
is black: an opaque 16-bit pixel there is rejected (it would become the
transparent 0x0000; opaque black is alpha 128 = 0x8000), and a palette entry
edited to it with STP clear becomes 0x0000. A resized image gets new header
dimensions and a recomputed `block_length`; `TITLE.TIM`'s non-standard
`block_length` is kept while its size is unchanged. All 14 disc TIMs round-trip
byte-identically.

## Verified by

* `disc/TIM2D/LAS_CN.TIM` first 16 bytes: `10 00 00 00 08 00 00 00 2c 00 00 00
  00 00 e0 01` — tag=0x10, ver=0, flags=0x08 (PMODE=0 + CLUT bit), CLUT block
  starts with `0x2C` (=44 bytes total = 12 B header + 32 B palette data).
* `disc/STR/TITLE.TIM` first 16 bytes: `10 00 00 00 02 00 00 00 0c 8c 0a 00 00
  00 00 00` — tag=0x10, ver=0, flags=2 (PMODE=2 16-bit, no CLUT). Note the
  noted-anomalous `block_length` 0x000A8C0C.
* `tools/inspect_tim.py` round-trips all 14 BB2 TIM files: the catalogue
  table above is the tool's output.

## References

* [PSX/TIM format on FF8 Modding Wiki][1]
* [TIM (PlayStation graphics) - Just Solve the File Format Problem][2]

[1]: https://hobbitdur.github.io/FF8ModdingWiki/technical-reference/miscellaneous/tim-file-format/
[2]: http://justsolve.archiveteam.org/wiki/TIM_(PlayStation_graphics)
