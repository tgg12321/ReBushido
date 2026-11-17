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
   as nothing is clear in the PNG. The log says where each was
   (`mods: wrote ... (page X Y, at U V, table X Y)`): the pieces of one
   line of a menu's lettering lie side by side in one page.
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
its colour table is a new key at each step, and is drawn from the
replacement it fades from ("Fades", below).

## Fades

A texture the game fades through its colour table (`tex_TintClut`, the
fades of `disp_`) is a new key at each step of the fade, and so is a
picture it fades by writing it again each frame (the boot logo, five
keys). A replacement still holds through them: **each replacement found
is kept with what it was found with** -- the colour table of a texture,
the pixels of a picture -- and a key with no replacement of its own is
held to the kept ones. Where its texels are a kept texture's (the key of
the texels alone, `port_TextureTexelKey`) and every colour of its table
is that one's with each part times one number (`port_TextureFadeOf`:
colour 0 stays 0, a texel's top bit stays, each part within two of its
31 steps), or where it is a picture of a kept one's size whose every
pixel is so, it is drawn from the kept one's picture, its colours
**times those three numbers** (`u_replace_tint`, and `u_tint` for a
picture). The log says so once for each: `mods: texture KEY is a fade
(62, 62, 62 of 128)`. A key that had no replacement is looked at again
whenever another replacement is kept. The tint is a mod's as much as
the game's: a texture whose table the game tints gold for the cursor is
drawn gold from the same picture.

What this cannot do is a fade that begins before its texture has been
seen whole, a fade in from black on a screen's first showing: the kept
one is not there yet. `tools/port_art.py` finds those among its dump
(textures of one size and outline whose colours are the brightest one's
times a number) and writes each step as the brightest one's picture,
tinted.

`tests/port/mods.c` holds the fit to the game's own arithmetic: every
step of `tex_TintClut`'s fade to black is found, with its tint; a table
in another order, a colour of nothing drawn, a top bit changed or one
colour out of step is not.

## The sharp menus

The program ships a second mod: **`mods/ui/`**, the lettering of the
menus drawn again in a font, sharp at any size. It is on unless `ui =
off` in `rebushido.cfg` or the settings menu ("Sharp menus") says
otherwise; its order is -100, as the English menu's.

| What | Its lines |
|---|---|
| The pause menu | Pause Menu, Continue, Return to Sub Menu, Return to Top Menu |
| The menu after a fight | Rematch, Return to Sub Menu, Return to Top Menu |
| The top menu's captions | Story Mode, Vs. CPU Mode, Training Mode, Vs. Mode, Group Battle Mode, Link Mode, Option Menu |
| The select screens' titles | Select Character, Select Weapon |
| The group battle's page | Group Battle Mode, Number of Entries, Match, Time Limit, Start, Return to Top Menu |

**How it is made.** `python tools/port_ui.py` draws the pictures into
`src/port/mods/ui/` (`--check` as `port_english.py`'s). The game draws a
line of a menu as sprites up to 64 texels wide cut from one line of
lettering in a texture page, and a word of one line is sometimes another
line's: the pause menu's "Return to Sub Menu" is the "Return to " of
"Return to Top Menu", then "Sub" and "Menu" from places of their own. So
the script's table is of lines -- the text, the line's size, the
rectangle the game's own lettering fills, its look, and its pieces, each
its key and where it begins along the line (from the dump's log, above)
-- and each line is drawn once, in Arimo (SIL Open Font Licence,
`src/port/fonts/`), fitted to that rectangle at eight pixels a texel,
and cut into its pieces. Every line of one height has letters of one
size. The looks are the game's: the pause menu's letters with a shadow
down and to the right, the select screens' with a dark edge all round,
the captions' darkening downward, each in the colours its own lettering
has (`--measure DIR...` prints them from a dump). Like the English
menu's, the pictures are the project's own, kept in the repository.

## The art mod

Everything else of the screens is **the game's own pictures made
larger**, which is not the project's to give, so it is a mod each player
makes from their own disc, as the hd fighters are (`tools/fighter_hd.py`):

```
python3 tools/port_art.py dump --image "Bushido Blade 2 (USA).cue"   # Linux or WSL
python tools/port_art.py build                                          # numpy and Pillow
```

`dump` plays the game with no window on the GL prover
(`python3 tools/port_gl_build.py`), seven runs at once: from the boot
through each mode of the top menu, its select screens, a fight with its
pause menu, the fight's end and the menus after it, every texture
written into `tmp/art/dump/` with no mod in the way (about 1,200 of
them). A player who would rather play the screens through runs the game
with `--dump-textures FOLDER` and gives `build --dump FOLDER` (as many
as they like). `build` writes `tmp/mods/art/`, order -200 -- under the
built-in mods and under any of the player's own -- with each texture six
times larger (`--scale`), by its kind:

- **a few colours** (six or fewer: an emblem, a frame, a button): each
  colour's area smoothed as a shape of its own (bicubic, at twice the
  scale) and every pixel given the colour whose area is strongest, then
  averaged down, so an edge between two colours is a smooth line and no
  colour is mixed into a new one inside an area;
- **lettering and the like** (up to 48 colours): the colours smoothed
  over their alpha, and the outline made sharp again from the smoothed
  alpha, a pixel or two wide;
- **a picture** (more, or 16 bits: a portrait, a loading screen's map,
  the boot logo): Lanczos and an unsharp mask.

A texture with a side under four texels is left as it is. Each is
written here in numpy and Pillow; no model and no other project's
program is used, so nothing is added to what the licence covers (a
learned upscaler with weights of a clear licence would do better on the
portraits; none is used). Copy `tmp/mods/art/` into `mods/` beside the
program; it is never committed. From one disc: 1,224 textures dumped,
1,070 made larger (653 lettering, 360 of a few colours, 57 pictures), 154
too small, in 8 seconds.

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
| `tools/port_ui.py`, `src/port/mods/ui/` | the sharp menus the program ships, and what draws them |
| `tools/port_art.py` | the art mod, made from the player's disc |

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
- **More of the lettering in a font**: the dialogue boxes, the names on
  the select screens and the options' pages are the art mod's for now,
  made larger; each is a line of `tools/port_ui.py`'s table away.
- **The title picture** after the opening movie is the movie's own
  frame in its 24-bit mode, which the remastered picture leaves to the
  software GPU (PORT.md, "Remastered in the window").
- Music and stages, through the same table (the models are read now:
  PORT.md, "The fighters' own models").
