# Mods

The native port reads **mods**: folders beside the program whose files
stand in for the game's own. This is the roadmap's item 10
([`ROADMAP.md`](ROADMAP.md)): one framework for everything a mod can
replace, with **replacement textures** as its first user, and the
English top menu the program ships as its first mod ("The English
menu", below). Models, music
and stages are to find their files the same way; their folders are read
already and nothing uses them yet.

Nothing here touches the game: what the game computes is the same with
mods and without, and the PlayStation builds have none of it. With no
mod's textures and nothing to write out, the picture is exactly what it
was.

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
  them, the hd mod, from your disc), `skies/` (a stage's sky, below),
  `music/`,
  `stages/` (read, not used yet). Under a kind's folder the files may be
  in folders of any names, four deep.
- A file's **key** is its name without its ending, in small letters,
  wherever it is under its kind's folder. A texture has to end `.png`.
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

## Skies

A stage open to the sky has a night sky of the port's own in the
remastered picture (`sky = night`, [`PLAYING.md`](PLAYING.md); PORT.md,
"The sky"). A mod can give it a picture instead:

```
mods/
  my-skies/
    skies/
      stage_04.png       the bus stop's sky
      default.png        every other open stage's
```

- The key is `stage_NN`, the stage's number in two digits (the order of
  `STAGE_NAMES` in `tools/port_remaster_prove.py`: `stage_15` is the
  terraced fields), or `default` for every open stage the mods give no
  picture of its own.
- The picture is **equirectangular**: its width goes once round the eye
  from left to right, its top row is straight up and its bottom row
  straight down, the horizon across its middle. Any size; it is
  smoothed and mipmapped.
- It stands in for the whole sky, the gradient, stars and moon included,
  and is drawn as it is: the stage's fog still fades the ground to the
  stage's own colour of the distance, so a horizon of about that colour
  meets the ground with no seam.
- Only the stages that have a sky use one: a hall or the cavern keeps
  the game's colour whatever the mods say. The log says which picture
  each stage took (`sky: stage N is the mod's picture ...`).

## For the code

| | |
|---|---|
| `include/port_mods.h`, `src/port/mods.c` | the folder read, the manifests, the table (`port_ModFind(kind, key)`), the texture's key |
| `include/port_png.h`, `src/port/png.c` | a PNG reader of every kind but the interlaced, with its own inflate, and the plainest PNG writer; no C library |
| `src/port/gl/gl_sky.c` | a stage's sky (`PORT_MOD_SKIES`), looked for once a stage |
| `src/port/gl/gl_replace.c` | the keys looked up per primitive and per picture, the mods' PNGs given to the card, the textures written out |
| `src/port/gl/gl_draw.c` | the hooks: a batch's replacement texture (`PortGlState`), the shader's `u_replaced`, the picture's in the stretch of video memory |
| `src/port/host_*.c` | `port_HostListFolder`, `port_HostMakeFolder` |
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
- Music and stages, through the same table (the models are read now:
  PORT.md, "The fighters' own models").
