# BBM: motion files

Files: `disc/MOTION/*.BBM` (18 files) and `disc/MOTION/WIN.DAT`.

A BBM file holds the moves of the fighters and the motion the moves play.
`COMMON.BBM` has what every fighter shares; each of the other 17 is a
**motion set**: one weapon class with three stances. The code that reads them
is `src/main/fighter.c` (loading, finding a move) and `src/main/motion_decode.c`
(the frames); the types are in `include/motion.h`. Cutscenes (NDATA entries,
`include/cutscene.h`) use the same frame stream.

## File names and motion sets

A fighter's motion set is a 16-bit number: the weapon class in the top hex
digit and the three stance kinds in the others, the first stance lowest. The
file name says the same thing with a letter for the weapon class:

| Set | File | | Set | File |
|---|---|---|---|---|
| `0x0321` | `U123.BBM` | | `0x1432` | `S234.BBM` |
| `0x0421` | `U124.BBM` | | `0x2321` | `T123.BBM` |
| `0x0521` | `U125.BBM` | | `0x3321` | `K123.BBM` |
| `0x0431` | `U134.BBM` | | `0x4321` | `N123.BBM` |
| `0x0531` | `U135.BBM` | | `0x5321` | `Y123.BBM` |
| `0x0532` | `U235.BBM` | | `0x6321` | `GN.BBM` |
| `0x1321` | `S123.BBM` | | `0x7321` | `GS.BBM` |
| `0x1421` | `S124.BBM` | | `0x0666` | `ON.BBM` |
| `0x1521` | `S125.BBM` | | | |

The table is `g_motion_set_files` (`src/main/fight_data.c`); a file's place in
it is its place in the motion group of the disc's file table
(`CDROM_GROUP_MOTION`) after `WIN.DAT` and `COMMON.BBM` (`MOTION_FILE_SETS`).
Which stance kinds a character has with a weapon is `g_fighter_stance_kinds`.
Stance kinds 1 to 3 are the ordinary ones; 4 (two swords,
`STANCE_KIND_TWO_SWORDS`) and 5 (sheathed, `STANCE_KIND_SHEATHED`)
exist only for the first two weapon classes. A weapon class is the weapon's
number (`enum WeaponId`, `include/weapon.h`): 0 the katana (`U`), 1 the long
sword (`S`), 2 the nodachi (`T`), 3 the broadsword (`K`), 4 the naginata
(`N`), 5 the yari (`Y`), 6 and 7 the long gun and the short gun (`GN`,
`GS`). `ON.BBM` is the motion of the one character that fights without
a weapon and has no stance, the victim (`CHARACTER_VICTIM`, one of the
characters that have no name on the disc).

## Layout

All offsets are from the start of the file, little-endian.

| Offset | Size | |
|---|---|---|
| 0x00 | 2 | `MC` in `COMMON.BBM`, `MW` in the others. Not checked |
| 0x02 | 1 | 0 |
| 0x03 | 1 | rows of per-stance moves in the header (11) |
| 0x04 | 4 | offset of the motions |
| 0x08 | 4 | offset of the marks |
| 0x0C | 4 | offset of the CPU's scripts (in `COMMON.BBM`: equal to the next) |
| 0x10 | 4 | offset of the frame stream |

In `COMMON.BBM` the moves begin at 0x14. A fighter file's header goes on
(`MotionFile`):

| Offset | Size | |
|---|---|---|
| 0x14 | 2 | number of stances (3) |
| 0x16 | 2 | the move a fighter comes on with (a move of the common file) |
| 0x18 | 27 x 2 | each character's win pose (moves of the common file), by `enum CharacterId` |
| 0x4E | 3 x 2 | standing in each stance |
| 0x54 | 3 x 3 x 2 | going from one stance to another |
| 0x66 | rows x 3 x 2 | the moves that exist once for each stance |

and its moves begin after the last row. Every entry is the place of a move in
the file's moves, counted in halfwords.

### Moves

A move (`MoveScript`) is a run of halfwords:

| Halfword | |
|---|---|
| 0 | id of a table of move ids, one for each thing that can happen to the fighter |
| 1 | id of the move that follows when nothing else is asked for |
| 2 | which motion it plays |
| 3 | first frame (low byte), last frame (high byte) |
| 4 | last frame at which a command is taken (low byte), flags (high byte) |
| 5.. | commands: pairs of (flags, move id), four halfwords when the flags have bit 14 or 15, ended by a zero |

A move id with bit 15 set is a move of the fighter's own file, by halfword;
without it, of the common file. The ids 0x7FF0 to 0x7FFF stand for moves that
depend on the stance (`enum StanceMoveId`).

### Motions and marks

A motion is four bytes: the number of its first frame in the stream, and the
offset of its marks in the marks section. The marks begin with four bytes
(the move kind, `enum MoveKind`; the length in frames; two more) and go on
with a list ended by a zero: a byte of 0x80 or more is a mark (`enum
MoveMark`) followed by the frame it falls on; a byte below 0x80 is followed by
one byte and 0xFF by six.

### The frame stream

Read as 32-bit words, most significant bit first.

1. The opening pose: 63 values of 12 bits.
2. 63 codes of 2 bits: how each value is packed in what follows.
3. At byte 0x70, the group table: three bytes (big-endian) for every eighth
   frame, the bit at which the frame begins, counted from the table.
4. The frames, in groups of eight.

A decoded frame (`MotionFrame`) is 66 halfwords: the root's height, direction
and distance, then 63 angles (4096 to the turn): three for the waist and three
for each of 20 bones.

Every frame begins with the root's three values, each a bit and, if the bit
is set, 16 bits (else 0). Then:

- in the first frame of a group, for each of the 63 values a bit and, if it
  is set, the value's 12 bits (else the opening pose's value);
- in the other seven, for each value whose code is not 0, the change in its
  rate of change since the frame before (the first of the seven starts from a
  rate of 0):
  - code 1: a length-prefixed number of at most 12 bits, whose low bit is
    the sign: `n` even is a change of `n / 2`, `n` odd of `-(n / 2) - 1`;
  - code 2: a set bit for no change, or a clear bit and the change in 12
    bits;
  - code 3: a clear bit for no change, or a set bit, a sign bit, three low
    bits and a length-prefixed number of at most 8 bits for the rest: the
    change is that number times 8 plus the low bits, plus 1, negated if the
    sign bit is set.

  A length-prefixed number of at most `m` bits: count zero bits up to a set
  bit, which is taken, or up to `m` zeros. No zeros is the number 0 and one
  zero the number 1; `z` zeros (2 or more, fewer than `m`) are followed by
  `z - 1` bits, the number's bits below a leading one; `m` zeros (no set bit
  follows them) by `m - 1` bits below a leading one.

A value is 16 bits and wraps. Six of the 63 are signed, the three of the 17th
bone and of the 19th (values 54 to 56 and 60 to 62 of the 66): their low 12
bits are sign-extended at the end. The others are left as they come out.

`tests/motion_decode.py` is a decoder written from this page; `make test`
compares it with the game's on every frame of every file.

## Sizes

```
file            size  header+moves  motions  marks  CPU scripts    stream
COMMON.BBM    165860          2936      472   1464            0    160968
GN.BBM         96120          2652      316    856          140     92136
GS.BBM        108468          2668      328    952          140    104360
K123.BBM      284880          4884      632   2500          908    275936
N123.BBM      276596          4800      720   2836          796    267424
ON.BBM          3480           240       12     20            8      3180
S123.BBM      253672          5236      672   2596         1644    243504
S124.BBM      287740          5564      788   2984         1328    277056
S125.BBM      269308          4920      668   2736         1208    259756
S234.BBM      292724          5616      800   3064         1272    281952
T123.BBM      286120          5056      664   2528         1000    276852
U123.BBM      252816          5148      664   2508         1664    242812
U124.BBM      294060          5512      800   3048         1200    283480
U125.BBM      278076          4888      680   2744         1092    268652
U134.BBM      293716          5684      796   3036         1560    282620
U135.BBM      278972          5060      676   2732         1452    269032
U235.BBM      271600          4792      660   2712         1076    262340
Y123.BBM      282848          4984      740   2884          944    273276
```

## WIN.DAT

`disc/MOTION/WIN.DAT` (387,072 bytes) is 27 records of 0x3800 bytes (seven
sectors), one for each character (`enum CharacterId`, `include/fighter.h`:
record 0 is KANNUKI's, record 26 `CHARACTER_LAST_2`'s): the character's win pose as frames that are
not packed, each a `MotionFrame` of 0x84 bytes, about 95 of them, the rest
zero. The modes that can end on a win pose read each fighter's record
(`motion_LoadFighterFiles`), and a fighter in its win move is posed from it
frame by frame.

## Tools

```
python tools/inspect_bbm.py <file.bbm>             # the sections and their sizes
python tools/inspect_windat.py disc/MOTION/WIN.DAT # the records
```

## Not known

- What the letters of the file names stand for, beyond the two guns (`GN`,
  `GS`): the weapon each goes with is from `g_motion_set_files`.
- The CPU's scripts (read by `cpu_Init`).
- The two bytes after a motion's kind and length, and the marks below 0x80.
