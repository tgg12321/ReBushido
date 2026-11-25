# GROUND — a stage's ground file

NDATA.DAT's `S00` to `S37` (NDATA ids `0x27` to `0x4C`,
[NDATA.md](NDATA.md)); `rebushido --dump-stages FOLDER` writes them as
`s00.dat` to `s37.dat`. The game reads one with `resource_LoadStage`
(`src/main/resource.c`): everything of a stage that is drawn and met, but
its sounds (`STAGE_BIN.md`) and the program's tables (`MODS.md`,
"Stages").

**Proved by a round trip**: `python3 tools/stage_ground.py roundtrip
FOLDER` reads each of the 38 into numbers (every section below, a tile's
mesh face by face, the collision grid face by face) and writes the file
again from the numbers alone: **all 38 come back byte for byte**, and the
same through the JSON of `unpack` and `pack`. Sections of the six stages
with areas or props that are not described here (below) are kept as
bytes; a piece's mesh (layouts 0 and 4, `include/mesh.h`) is kept as
bytes. Everything is little-endian. The numbers below are measured over
the 38 files.

## The header

```
u32 count                 n: 6, or 8, 9 or 21 for the stages with more
u32 offset[n]             each section's, from the file's start
u32 end                   where the last section ends
```

The first section starts right after (`4 * (n + 2)`); the sections follow
one another in order, each on a word. A section that ends off a word is
followed by two zero bytes (13 files' cells, 6 files' collision grids).
After `end` the file runs on to a whole sector: garbage of the build's,
never read (48 to 1,936 bytes).

| Section | n = 6 | What |
|---|---|---|
| 1 | meshes | everything drawn, by mesh |
| 2 | collision | the collision grid |
| 3 | unused | a list nothing reads (NDATA.md) |
| 4 | obstacles | posts a blade meets |
| n - 1 | cells | the tiles, the pieces and each cell's list |
| n | textures | the TIMs and where each goes in video memory |

With n > 6 the sections between 4 and n - 1 are a stage's own: the
Tatara Shrine's and chanbara's areas (each area five offsets after the
header's first word, `resource_SelectStageArea`), the bus stop's, bamboo
thicket's and lumberyard's props (their meshes and motion), the castle
rooftop's backdrop. An arena has none.

What the game keeps of the file in its arena block is up to the cells:
the cells are read into tables of their own and the textures into video
memory (`resource_LoadStage`). That is the stage's room in memory: 31,796
(S21) to 93,164 (S09) bytes.

## Meshes

```
u32 count                 meshes
u32 offset[count]         each mesh's, from the section's start
u32 end
mesh[count]               one after another, in order
```

`reloc_RelocateBlock` turns the offsets into pointers. A **tile's mesh**
(`MESH_COMPACT`, what `render_DrawStageTiles` draws; `gl_stage.c`) is:

```
u16 vertices, u16 1       the count and the layout
s16 x, y, z [vertices]    from the cell's corner; padded to a word
{ u16 records, u16 type, record[records] }...   groups
u16 0, u16 0              the end
```

A group's `type >> 3` is 0 for triangles and 1 for quads; the disc has
types 4 (triangles), 6 (two-sided triangles), 12 (quads) and 14
(two-sided quads). A record is 0x18 bytes:

| Bytes | Triangle | Quad |
|---|---|---|
| 0x00 | u16 texture page (x / 64, `0x10` for y 256, blend, depth) | the same |
| 0x02 | u8 bits: `(bits >> 1) & 3` how often the renderer cuts it near the camera | the same |
| 0x03 | u8 code: `0x24` (POLY_FT3), the low two bits raw and half see-through | `0x2C` (POLY_FT4) |
| 0x04 | u16 colour table (x / 16, y << 6) | the same |
| 0x06 | u16 texture point (u, v) ×3 | ×4 |
| 0x0C | u8 corner ×3, u8 0 | — |
| 0x0E | — | u8 corner ×4 |
| 0x10 | s16 normal x, y, z (4096 long), u16 0 | — |
| 0x12 | — | s16 normal z, then x, y at 0x14 |

The corners are a quad's in the GPU's order (the polygon's 0, 1, 3, 2),
and **the face is seen from where `(c1 - c0) × (c2 - c0)` points along
its normal**: all 17,197 records of the disc's tiles have it so. A tile is
2,040 units across on the disc (its cell and 40 more), and the commonest
bits are 0 and 2 (5,892 and 5,712 quads).

## The collision grid

```
u16 offset[32 * 32]       each cell's faces, from the grid's start; 0xFFFF none
then each cell's faces, in the cells' order, back to back:
  s16 count
  face[count]
```

The grid is 32 by 32 cells of 2,000 units centred on the world's origin
(`include/collision.h`); cell (x, z) is `offset[z * 32 + x]`. An offset is
a halfword, so a grid is at most 65,535 bytes (the disc's largest is
61,984, S10). No two cells share a list. A face, all halfwords but `d`,
everything measured from the **cell's corner** (`x - (cx * 2000 -
32000)`, `y`, `z - ...`):

```
s16 normal x, y, z        0x4000 long; a floor's y is -0x4000 (the world's y grows down)
s32 d                     the plane: normal . p + d, written as two halfwords, low first
s16 origin x, y, z        a point of the face
s16 u x, y, z             two axes in its plane, 0x4000 long
s16 v x, y, z
u16 surface << 8 | edges  enum Surface; how many edges
s16 u, v [edges + 1]      the outline on the axes, the first point again last
```

`collision_TestCell` (`src/main/collision.c`; in Python,
`tools/ground_cast.py`) meets a face only **from its front**: the
segment's start not behind the plane and its end behind it. The crossing
is put on the axes (`(p - origin) · axis >> 14`, by the GTE: its IR
registers saturate, so a point more than 8,191 units from the origin on
an axis is never inside) and is inside when `(p - from) × (to - from) ≤
0` for every edge of the outline. The surfaces met on the disc: plain
3,555, firm 4,008, firm2 1,146, sand 4,497, sand2 886, wet2 232, barrier
1,556; wet (5) none. Edges: 3 to 7, quads 15,264 of 15,880.

A cast is followed across the cells it passes over and each cell's faces
are tested with the segment **in that cell's measure, in halfwords**
(`SegmentCast.local_*`): a segment's ends must be within 32,767 of the
cell's corner and its y within ±32,767, or it wraps. The disc's faces
reach past their cell by 250 units on every side (cell 528 of S29: an
outline from -50 to 2,450 about an origin at 2,200), so that a cast that
starts or ends near an edge meets the face from the cell it is tested
in.

## Obstacles

```
{ s16 type, s16 used, s32 x, y, z }...   type 2: a prop a blade cuts down; any other: a post that stops it
s16 0, s16 ...                           the end: four bytes
```

`(x, z)` is where it stands and `y` its top: a blade's sweep this frame
that covers `(x, z)` and reaches below `y` meets it
(`fight_BladeMeetsObstacle`, `src/main/fighter_hit.c`). `used` is set when
a prop is cut down and cleared by `fight_ResetObstacles`. 0 to 12 a
stage.

## Cells

Three lists of halfwords, each ended by 0xFFFF (`stage_ReadCells`):

```
{ u16 cell, u16 mesh }...         the tiles: a cell (z * 32 + x) and its
                                  mesh, 0x8000 if it may be opened when it
                                  hides a fighter; 500 at most
{ u16 mesh, s16 x lo, x hi, y lo, y hi, z lo, z hi, s16 angle x, y, z }...
                                  the pieces: a mesh at a place, turned; 60
                                  at most
{ u16 cell, u16 entry... }...     each cell's list of what is drawn when the
                                  cell is seen: tiles by number, pieces from
                                  the tiles' count up, 0x8000 on the last;
                                  1,000 entries in all at most
```

## Textures

```
u32 group[32]             each group's offset from the section's start
{ u32 count, record[count] } [32]
TIM...                    the TIMs, one after another
```

The game loads group 0 (`tex_LoadStageGroup`); on the disc the other 31
are empty. A record is `u32 TIM's offset, s16 picture x, y, s16 colour
table x, y` (video memory). Every TIM of the disc's grounds is 4-bit with
a colour table (flags 8), its picture in pages 10 to 13 at y 256 (video
memory 640 to 895), its colour table in row 480 at an x under 320. A colour
table there is loaded **a row down**, at 481, and the faces name row 480:
`stage_BuildFog` makes 30 fogged copies of row 481, and the fog copies the
one each depth needs over row 480 as the frame is drawn. So a stage has
room for 20 colour tables of 16 colours.

Far from the camera the renderer halves a face's texture points once or
twice (`render_TileShrink`, `render_ShrinkUv`): a texture at (u, v) in its
page has a copy half its size at (u / 2, v / 2), and a quarter at (u / 4,
v / 4). A texture of 64 by 64 at (128, 128) of a page, the disc's
commonest, has its copies at (64, 64) and (32, 32).

## Written from a scene

`tools/arena_compile.py` writes a ground of six sections from a glTF or
OBJ scene (`MODS.md`, "Custom arenas"): a tile a cell (two where a cell
has more than 256 corners) with each face cut at the cell's edges, quads
cut near the camera only when an edge is longer than 1,200 units (every
cut is four primitives where there was one, and a frame's room for them
is the game's: a ground of 1,700 small quads all cut overflows it); the
collision grid with each face cut to every cell it crosses, 250 units past
the cell as the disc's are, its outline grown by 2 units; up to 20
textures (the fog's row) of 128, 64 or 32 texels square in the bottom
right quarter of pages 10 to 12 (a 128 the whole quarter, a 64 a quarter
of it, a 32 a sixteenth; the larger placed first), each with its two
smaller copies at half and a quarter of its place, their colour tables in
row 480; a face that repeats its texture cut at each repeat.
