# Mods

The native port reads **mods**: folders beside the program whose files
stand in for the game's own. This is the roadmap's item 10
([`ROADMAP.md`](ROADMAP.md)): one framework for everything a mod can
replace, with **replacement textures** as its first user, and the
English top menu the program ships as its first mod ("The English
menu", below). The fighters' models, **music** ("Music") and a
**stage's files** ("Stages") find their files the same way.

Textures, models and music touch nothing of the game: what the game
computes is the same with those mods and without, and the PlayStation
builds have none of it. With no mod's textures and nothing to write out,
the picture is exactly what it was; with no mod's music, the sound. A
stage's files are the one kind that is the game's own data: a stage mod
changes what the game loads, and so can change how it plays
("Stages").

## Where the mods are

```
rebushido(.exe)
mods/
  better-menus/
    mod.txt              the manifest (optional)
    textures/
      64x64-4-0123456789abcdef.png
      title/             folders of your own, as deep as four
        320x240-16-....png
  english-top-menu/
    mod.txt
    textures/...
```

- `mods/` **beside the program**, or the folder `--mods FOLDER` names.
  `--mods off`, or `mods = off` in `rebushido.cfg`, runs the game with
  none ([`PLAYING.md`](PLAYING.md), "Changing the keys and the
  picture").
- Each folder of `mods/` is one mod. Its kinds of file are folders of
  their own: `textures/` (read and used), `models/` (the fighters'
  `.glb` models, by the file's name on the disc, `M100.glb`, and the
  weapons', by the weapon model's number, `W00.glb` to `W57.glb`: PORT.md,
  "The fighters' own models"; `tools/fighter_hd.py` makes a whole mod of
  them, the hd mod, from your disc), `music/` (`.ogg` and `.wav`:
  "Music") and `stages/` (any ending: "Stages"). Under a kind's folder
  the files may be in folders of any names, four deep.
- A file's **key** is its name without its ending, in small letters,
  wherever it is under its kind's folder. A texture has to end `.png`,
  a piece of music `.ogg` or `.wav`.
- What the program found is said in the session's log (`logs/`): each
  mod, in its order, with how many files of each kind it gives, and each
  texture as it is first used.

### The manifest

`mod.txt`, lines of `name = value`, `#` beginning a comment:

```
name = English top menu
version = 1.0
order = 10        # later in the order wins; 0 if it does not say
enabled = yes     # no leaves the mod out without moving it
author = you      # read and not used, as `about` and `url` are
```

A mod without a manifest is a mod all the same, named for its folder,
in order 0.

### The order

The mods are read **by `order`, then by their folders' names**. Where
two mods have a file of the same kind and key, the one later in the
order is the one used, and the log counts how many were overridden. So
a mod meant to sit over another says a larger `order`.

## Replacement textures

Replacement textures work the way DuckStation's and Beetle's do: the
game's texture is found by **what it looks like**, not by where it is,
and a picture of any size stands in for it. They are drawn by the
remastered picture (Graphics: Remastered, `docs/PORT.md`,
"Remastered"); the console's own picture, Graphics: Original, never
changes.

### Making one

1. Run with `--dump-textures FOLDER`. Every texture the game draws is
   written there as a PNG the first time it is drawn, named by its key,
   once: play through what you want to change. A texel the game draws
   as nothing is clear in the PNG.
2. Draw a better one, **at any size** -- two, four or eight times the
   texture, or any other size; it is stretched over the texture and
   smoothed. Keep the clear parts clear: a pixel whose alpha is less than
   half is not drawn, as the console's texel of nothing is not.
3. Put it, with the same name, in a mod's `textures/`.

The game writes pieces of a screen as separate sprites, so a menu is
many textures: the top menu's background is 64 by 64 tiles, its lines of
text 64 texels wide at the most. Each piece is replaced on its own, and
the pieces of one picture can be drawn as one and cut up.

### The key

The name is `WxH-D-HASH`: the texture's size in texels, its depth (4, 8
or 16 bits a texel), and a 64-bit FNV-1a hash in 16 small hexadecimal
digits, of these bytes in this order:

- the depth's code (0 for 4 bits, 1 for 8, 2 for 16), one byte;
- the width and the height, two bytes each, the low byte first;
- every texel of the rectangle, a row after a row from the top: its
  index into the colour table, one byte (4 and 8 bits), or the pixel
  itself, two bytes, the low first (16 bits). The texel is read as the
  GPU reads it, through the texture window;
- the colour table, 16 colours or 256, two bytes each, the low first
  (none for 16 bits).

So the key holds wherever the game puts the texture in video memory and
whichever slot its colour table has, and changes where either its texels
or its colours do (`src/port/mods.c`, `port_TextureKey`;
`tests/port/mods.py` works it out a second time from this description).

**What a texture is**: for a sprite (a rectangle), its own rectangle of
texture points; for a polygon of the 2D layer, the rectangle of its
corners' texture points, its right and bottom edges not read, as the GPU
reads them.

**A picture the game copies into video memory itself** -- the boot
logo ("Published by Square Electronic Arts") is one -- is the 16-bit
texture of its own rectangle: a picture that is not all one colour,
written where the display shows. It is drawn from the mod's picture,
stretched over the rectangle, opaque.

### How it is drawn

The texture of a sprite is still read through the console's rules
everywhere but its colour: the mod's picture is read, **smoothed** (and
mipmapped where it is drawn smaller than it is), at the point the
primitive's texture coordinate runs to between the console's texels; its
alpha says what is clear; its colour stands for the texel's and is
multiplied by the primitive's colour as the console's would be (a white
of 255 is the console's 31), and **whether it mixes** with what is under
it, where the primitive is half-transparent, is still the console's
texels' to say. The console asks each texel's top bit; a mod's picture
draws where the game's texture drew nothing as well (an English word
across the gap between two Japanese characters), so where every texel
the game drew says the same, the whole picture is taken to say it, and
only a texture whose texels disagree is asked texel by texel
(`gl_replace.c`, `port_ReplaceMixOf`). So a fade, a highlight and a
half-transparent panel come out as they did. The PNG is read once, the first time its key is drawn,
and kept on the card.

A replacement is batched by its texture: each sprite with one is a draw
of its own.

### What is not replaced, and why

- **The scene's faces**: the stage, the fighters, their weapons. Each
  face reads a small piece of its page that is not a texture of its own,
  so its key would be a piece of a fighter, and a fighter's texture
  would be hundreds of files. The scene wants keys by whole page and
  colour table, which is the next step (below).
- **The fight drawn again under the pause** is the scene as well.
- **A texture whose colour table the game is fading** is a new key at
  each step of the fade, and is drawn from video memory during it. The
  boot logo fades by writing its pixels again each frame, so it is five
  keys, one for each step: a mod gives each of them, or only the first.
- **The movie**, which the console shows in its 24-bit mode and the
  remastered picture leaves to the software GPU. The title picture held
  after it has not been looked at yet: the play tests' presses skip it.
- With `--gl-exact` (the prover's picture held to the console's) nothing
  is replaced.

## The English menu

The program ships one mod of its own: **`mods/english/`**, the top
menu's Japanese drawn again in English. It is on unless `english = off`
in `rebushido.cfg` or the settings menu says otherwise
([`PLAYING.md`](PLAYING.md)), which leaves it out as `enabled = no` would
(`port_ModsLeaveOut`); `mods = off` leaves it out with the rest. Its
order is -100, so a mod of yours with the same keys wins. Like every
replacement texture it is drawn by Graphics: Remastered; the console's
own picture stays Japanese.

| The game's | Literally | The mod's |
|---|---|---|
| 試練項目選択 over "TOP MENU" | the choice of trials | Select a Trial, over TOP MENU |
| 決戦の間 | the hall of the decisive battle | Story Mode |
| 勝抜の間 | the hall of winning through | Vs. CPU Mode |
| 精進の間 | the hall of devotion | Training Mode |
| 対刃の間 | the hall of crossed blades | Vs. Mode |
| 対抗の間 | the hall of rivals | Group Battle Mode |
| 連結の間 | the hall of linking | Link Mode |
| 百斬の間 | the hall of a hundred cuts | Slash Mode |
| 支度の間 | the hall of preparations | Setup Mode |

The names are the US disc's own: its caption under the rows already
calls each mode so (MOD.BIN's `mode_caption_sheets`,
[`formats/STAGE_BIN.md`](formats/STAGE_BIN.md)). The game draws a row as
two sprites of 64 by 16 texels, the mode's two characters and then の間,
which is **one texture that every row shares**: so the English is in two
pieces as well, the mode's name and the "Mode" every row shares. The
options' row, whose US name is "Option Menu", is "Setup Mode" for that
reason: 支度, "preparations", with the word the other rows have.

**How it is made.** `python tools/port_english.py` draws the pictures
into `src/port/mods/english/` (`--check` says whether what is there is
what it would draw). They are the project's own art: text in two fonts
of the SIL Open Font Licence, Kaushan Script for the brush lettering and
Marcellus for "TOP MENU" (`src/port/fonts/`, each with its licence),
with the game's look -- white letters fading to pale blue, a dark edge,
a blue shadow down and to the right; the game tints a row itself (gold,
the cursor's yellow). No pixel of the game's is in them or is read to
draw them, so they are kept in the repository, and the script makes the
same bytes again. What was taken from the game is in the script's table:
each texture's key, its size, and where its lettering sits, measured
with `--measure DIR` from a dump of the top menu (`--dump-textures`).
The top menu is in the console's mode of 640 by 240, whose texel is
twice as tall as it is wide on the screen, so the pictures are drawn six
times the texture across and twelve down, square on the screen.
`tools/port_sdl_build.py` copies the mod beside the program.

**Fades.** The top menu fades nothing through its colour tables: the
dump of a run from the boot to a mode chosen has one key for each row
and each piece of the title, and the mod replaces every one of them
from the menu's first frame to its last. A texture that is faded through
its colour table is still a new key at each step (below).

## Music

Every piece of music the game plays is **XA sound off the disc**: a
stream (a channel) of one of the disc's sound files, which the drive
plays by itself while the game only watches where it is
([`PORT.md`](PORT.md), "Sound"; [`formats/XA.md`](formats/XA.md)). A
mod's piece takes the place of one stream.

### The key

A piece's key is **the disc's file and the stream's channel**: the file's
name in small letters without its ending, a dash, and the channel. The
movie's sound, which is chosen by no channel, is the file's name alone.

| Key | What it is |
|---|---|
| `bb2_m1-0` | the narration, and the stages whose fight it is (most of them: below) |
| `bb2_m1-1` | the menu's pages: character select and the rest |
| `bb2_m1-2`, `-3`, `-4` | stages' fights |
| `bb2_m1-5`, `-6` | on the disc; nothing asks for them |
| `bb2_m1-7` | the top menu |
| `bb2_m2-0`, `bb2_m2-1` | chanbara, the one clan and the other |
| `bb2_m2-2`, `bb2_m2-3` | a practice round; practice |
| `bb2_m2-4` | the staff roll |
| `bb2_m3-0` to `-5` | the voices of six endings |
| `mar_xa00-N` to `mar_xa28-N`, `end_xa1-N` to `end_xa3-N` | the story's cutscenes' voices, stream N |
| `opening` | the opening movie's sound |

A versus fight's piece is its stage's (`g_snd_stage_music_tracks`,
`src/main/fight_data.c`): one of `bb2_m1-0` to `-4`, so one piece is
heard on several stages, and a mod replaces it on all of them.

`--dump-music FOLDER` writes each piece **as the game plays it**,
`KEY.wav`: the drive's own sound before the game's volumes, 16 bits,
stereo, 44,100 a second, from the moment the drive starts the stream.
Play through what you want; a piece heard twice is kept from the longer
play. That is what a replacement stands in for, and its key is its name.

### Making one

Put `KEY.ogg` (Ogg Vorbis) or `KEY.wav` (whole numbers of 8, 16, 24 or
32 bits, one channel or two) in a mod's `music/`. Any rate from 8,000 to
96,000 a second; it is brought to the chip's 44,100 by a line between
samples.

- **How loud.** The piece is heard where the disc's sound was, through
  the same volumes, and the game sets the drive to **twice** what is on
  the disc (`CdMix` at 0xFF). The disc's pieces peak at about half of
  full scale (12,700 to 15,500 in the dumps): make a piece as loud as its
  dump, not louder, or it is clipped. A piece replaced by its own dump
  is heard exactly as the original.
- **Its loop.** An Ogg file's tags `LOOPSTART` (the sample the loop goes
  back to) and `LOOPLENGTH` or `LOOPEND` (the sample it goes back from),
  in samples of the file; a WAV's first loop in its `smpl` chunk. With
  neither, the piece plays once and is then silent until the game starts
  the stream again.
- **Its length.** The game ends and restarts a looping piece after the
  original's own length, as it does on the disc (the top menu's after
  3:44: `g_cdrom_music_tracks`, `src/main/cdrom.c`), and the mod's piece
  starts again from its beginning with it. A piece longer than the
  original is cut there; a shorter one wants a loop.

### How it is heard

The piece follows the drive, not the game's calls: it starts on the
sample the drive starts the stream on, and stops when the drive does.
Everything the game does to its music it does to the drive's volumes
and the drive itself, so the mod's piece is faded, set loud, muted and
stopped as the original is, on the same samples (a fade, blank by blank:
the proof below). The disc's sound is still read and decoded and goes
into the sound chip as before: the chip, its memory, its reverb and the
samples it hands the mixer are what they were, and what is heard (the
`--wav` file, the sound card) is a second copy of those samples with the
mod's piece where the disc's was, made beside them in memory that is no
part of the state (`port_RunSpu`'s `heard_out`, `music.c`'s). With no
music mod the sound is the same, sample for sample.

**A rollback** (netplay, `NETPLAY.md`) neither restarts nor stutters a
piece: where it is comes from the sound's clock (samples since the
stream began), which is the game's, and a step run again quietly is not
heard and moves nothing of the music's. Two peers may have different
music mods, or none: the music is no part of the state.

The setting **`music_mods = off`** (the settings menu's "Mods' music",
or `--music-mods off`) plays the game's own music, at once and at the
same place in it; on again, the mod's piece is back where the stream is.
`mods = off` leaves out every mod, music included.

The Ogg decoder is **stb_vorbis** 1.24 by Sean Barrett, in the public
domain (or MIT, as its user chooses; the licence is at the end of
`src/port/vendor/stb_vorbis.c`), kept as it ships. The port has no C
library: `src/port/vorbis.c` writes the little stb_vorbis takes from
one ahead of it, and gives it one buffer as all its memory; the
headers it names are the empty ones in `src/port/vendor/libc/`. It is
the port's one file with numbers that are not whole, and the 64-bit
program compiles it with the vector registers that numbers of that kind
are passed in (`tools/port_build.py`, `PORT_OWN_FLAGS`). The two sizes
decode an Ogg file a part in 16,000 apart at most (one or two in the
last bit); a WAV is the same in both.

**How it is proved** (headless, `port_game`'s run, both sizes):

- with no music mod, the run's `--wav` and its trace are byte for byte
  what they were before there was any of this; so with `--music-mods
  off` and a mod present;
- every piece the run plays replaced by its own dump: the sound is the
  original's, sample for sample, through the whole run but the last
  0.78 s, the top menu's second play, whose drive started it a sample
  apart from the play that was dumped (the drive's sectors reach its
  decoder as the blanks fall);
- `bb2_m1-1` replaced by an Ogg file at 48,000 a second, a tone that
  rises a step each second, looped from its second second to the end of
  its third: in the run it is heard from the same sample to the same
  sample as the original (blanks 1,388 to 2,559), at 300, 500, 700, 500,
  700 Hz... second by second, and twice as loud as the file, as the
  disc's sound is;
- `bb2_m1-7`, the top menu's, replaced by the same tone: as the menu is
  left, the game's fade takes it down blank by blank in the proportion
  it takes the original down (18,707 to 725 in RMS, against 4,753 to
  164), and cuts it on the same sample;
- the trace of every one of those runs is the plain run's, in both
  sizes, and the rollback self-test (`tools/port_rollback.py --with=--mods
  --with=FOLDER`) passes with a music mod and a stage mod in, loud and
  quiet. It is what put the heard samples outside the state: with them
  in the mixer's own buffer, a step run again quietly, which hears no
  music, left that buffer otherwise than the first run had;
- on every push, `tests/port/music.c` (`music.py`): the keys, the tags,
  where a sample falls at other rates to an hour and more, WAVs of every
  kind it reads and those it does not, an Ogg file made by the reference
  encoder (where the system has libvorbisenc) decoded by stb_vorbis to
  its length, pitch and tags with a seek to the sample, and the player
  through a stream's start, a step run again, another stream, the
  setting, a loop and an end.

## Stages

A stage is **files on the disc and tables in the program**. A mod can
replace the files; the tables are the next step (below).

### What the game reads for a stage

| | Where | What |
|---|---|---|
| **The ground**, key `s07` | NDATA.DAT's `S07` ([`formats/NDATA.md`](formats/NDATA.md); `resource_LoadStage`, `src/main/resource.c`) | a header of section offsets, then: the **meshes** (everything drawn, by cell); the **collision grid** (32 by 32 cells of 2,000 units, each listing the faces that cross it, with each face's surface: firm, sand, wet, barrier...: `include/collision.h`); a list nothing reads; the fight's **obstacles**; the **cells** of the stage grid (what stands on each of the 32 by 32, `stage_ReadCells`); and the **textures**, TIMs with where each goes in video memory (`tex_LoadStageGroup`). Stages with more have more sections: areas (the shrine, the castle rooftop, chanbara's hall), props that fall when cut (the bus stop, the bamboo thicket, the lumberyard), the rooftop's backdrop, the coast's surf |
| **The sounds**, key `stage07` | `LOADSE/STAGE07.BIN` or `LOADSE1/` ([`formats/STAGE_BIN.md`](formats/STAGE_BIN.md)) | six sound banks (footsteps and landings on each surface class, the stage's own) and the billboards' list |
| **The card**, key `stg07` | `U_PIC/STG07.BIN`, `TIM2D/` for 30 and 31 | the stage's name and view that story mode shows; stages 0 to 31 |
| The light | the program: `g_light_stage_presets` (`src/main/light.c`) | each stage's sun and ambient colours for the fighters and the scenery, the sun's direction, the colour of the distance |
| The fog | the program: `g_stage_fog_spans`, `g_stage_fog_settings` (`include/stage.h`) | how far the scene reaches and how the fog steps |
| The sky | the stage's own meshes and textures (a dome or a backdrop is a mesh like any other), and the clear colour | |
| Where the fighters start | the program: `g_fight_start_positions` (`include/fight.h`) | each stage's pairs of start positions |
| Its music | the program: `g_snd_stage_music_tracks` | one of five pieces (Music, above) |
| What it does of its own | the program: `g_stage_hooks` (`src/main/stage_hooks.c`) | the surf, a turning texture, the props |

**Gameplay.** The collision grid is what the fighters stand on and
walk into, what a blade meets, what sand is thrown from and where a
fight is out of bounds: a stage's ground file changes **how fights
play**, not only how they look. A changed ground is a changed game: two
netplay peers must have the same stage mods (the stage is loaded into
the game's state, and the hash would part at the first frame of the
fight), and a stage file whose collision does not match what is drawn
lets a fighter walk through a wall or fall through the floor. The
remastered picture's ground as one mesh (`stage_mesh`,
`src/port/gl/gl_stage.c`) is proved stage by stage on the disc's
grounds (`g_stage_proved`): a stage whose ground is replaced should be
looked at with `stage_mesh = off` until it is measured again.

### Replacing a stage's files

`--dump-stages FOLDER` writes every stage's files from your disc under
their keys: `s00.dat` to `s37.dat`, `stage00.bin` to `stage37.bin`,
`stg00.bin` to `stg31.bin`. Change one, and put it in a mod's `stages/`,
in a folder of any name:

```
mods/
  blue-rooftop/
    stages/
      29 building rooftop/
        s29.dat
```

As the program starts, it reads the disc's directory (ISO 9660,
`src/port/discfiles.c`), finds where each stage file is (NDATA's through
NDATA.INF), and keeps each mod's file whose key it knows. Then every
sector the drive reads of that file has the mod's data in place of the
disc's (`port_OverrideDiscSector`, called by the disc's reader): the game
loads it as it loads its own, by the same commands, and none of its code
knows. A mod's file may be **shorter** than the disc's (the rest of the
disc's sectors read as zeros) and **not longer**: the game reads as many
sectors as its own tables say (NDATA.INF for a ground). One that is
longer is left out, and the log says so.

**How it is proved.** Stage 29's ground (the building rooftop, the stage
of `port_game`'s fight) dumped from the disc, its 54 colour tables given
red for blue (491 colours) by a script at test time, and put in a mod:
the log says `s29` is read over the disc's sectors 9,520 to 9,597; the
trace of the run and its sound are the plain run's in both sizes, and
the pictures are the same until the fight and differ from its first
picture on (the floor blue, the fighters as they were). The rollback
self-test passes with it in. `tests/port/discfiles.c`: a made-up disc's
directory, a folder of two sectors, the files by path and by sector,
NDATA's stage found through its INF, the dump, and a mod's files over
the disc's sectors: one that fits, one shorter, one too long, and every
other sector untouched. Nothing of the player's disc is in the
repository.

### What a stage editor needs next

- **The ground file written, not only changed**: a reader and writer of
  its sections (`tools/inspect_stage.py` reads the BIN files; the ground
  wants the same), and an exporter from a modelling program: meshes into
  the game's mesh format by cell, the collision grid made from the
  meshes (each face's cells and surface), the cells' table, textures
  packed into the stage's place in video memory with their colour
  tables.
- **Larger files**: a ground longer than the disc's needs NDATA.INF
  given its new length (the INF is read off the disc like the rest, and
  can be replaced the same way) and room in the arena block the stage is
  loaded into (`ARENA_STAGE`).
- **The program's tables as data**: the light, the fog, the start
  positions and the music by stage from a stage mod's own file, read
  where the game reads its tables (a switch of the port's).
- **New stages**, not only replaced ones: a stage number past 37, and
  the menus to choose it.

## For the code

| | |
|---|---|
| `include/port_mods.h`, `src/port/mods.c` | the folder read, the manifests, the table (`port_ModFind(kind, key)`), the texture's key |
| `include/port_png.h`, `src/port/png.c` | a PNG reader of every kind but the interlaced, with its own inflate, and the plainest PNG writer; no C library |
| `src/port/gl/gl_replace.c` | the keys looked up per primitive and per picture, the mods' PNGs given to the card, the textures written out |
| `src/port/gl/gl_draw.c` | the hooks: a batch's replacement texture (`PortGlState`), the shader's `u_replaced`, the picture's in the stretch of video memory |
| `src/port/host_*.c` | `port_HostListFolder`, `port_HostMakeFolder` |
| `include/port_music.h`, `src/port/music.c` | the music: a stream's key, following the drive, the piece decoded and put in place of the disc's sound, `--dump-music` |
| `src/port/vorbis.c`, `src/port/vendor/` | stb_vorbis as it ships, and the empty C library headers it is compiled over |
| `src/port/xa.c`, `src/port/spu.c`, `src/port/sound.c`, `src/port/cd.c` | the hooks: the drive's volumes on other samples (`port_MixCdAudio`), the chip's `heard`, the call in the mix, the stream playing (`port_GetCdStream`) |
| `include/port_discfiles.h`, `src/port/discfiles.c` | the disc's directory, a stage's files by key, `--dump-stages`, the mods' files over the disc's sectors; `src/port/disc.c` calls it for every sector it reads |
| `tests/port/music.c`, `music.py`, `tests/port/discfiles.c` | the music's keys, loops, files and player; the directory and the stage files |
| `tests/port/mods.c`, `mods.py` | the manifest, a folder of mods and its order, the keys from this page, PNGs of every kind written by zlib |
| `tools/port_english.py`, `src/port/mods/english/`, `src/port/fonts/` | the English menu the program ships, and what draws it |

The mods' files are none of the game's state (`config/port_state.txt`,
`docs/NETPLAY.md`): the table is made once, as
the program starts, and only read after; a rollback has nothing of it to
put back.

**A new kind** (the models, say) asks `port_ModFind(PORT_MOD_MODELS,
key)` with a key it decides, and reads the file with `port_ModReadFile`.
The folders are already read; `g_port_mod_kind_endings` says which
ending a kind's files must have.

## What is next

- **The scene's textures by page**: a key of a whole texture page and
  its colour table, and a replacement of the whole page, so that a
  stage's or a fighter's textures are a few files each.
- **A fading colour table** found by the table it fades from, so that a
  replacement fades with it.
- **The other menus redrawn**: the top menu is in English (above); the
  rest of the game's menu text is English already, at the console's
  resolution, and each piece of it is a sprite with a key, so a sharper
  menu is a set of PNGs drawn as `tools/port_english.py` draws its own.
- **Music by stage**: a key for a fight's piece with its stage
  (`bb2_m1-0` on stage 7 alone), so that a mod can give each stage a
  piece of its own though the game shares five among them.
- **FLAC**: a third kind of music file, with a decoder of whole numbers
  (dr_flac, public domain, is one).
- Stages: "What a stage editor needs next", above.
