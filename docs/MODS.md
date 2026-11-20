# Mods

The native port reads **mods**: folders beside the program whose files
stand in for the game's own. This is the roadmap's item 10
([`ROADMAP.md`](ROADMAP.md)): one framework for everything a mod can
replace, with **replacement textures** as its first user, and the
English top menu the program ships as its first mod ("The English
menu", below). The fighters' models, **music** ("Music"), a
**stage's files** ("Stages"), **custom arenas** ("Custom arenas"),
**weapons** and **motions** ("Weapons and motions") find their files the
same way.

Textures, models and music touch nothing of the game: what the game
computes is the same with those mods and without, and the PlayStation
builds have none of it. With no mod's textures and nothing to write out,
the picture is exactly what it was; with no mod's music, the sound.
Stages, arenas, weapons and motions are the game's own data: such a mod changes
what the game loads or reads, and so how it plays; two netplay players
must have the same ones, and the session's set-up holds them to it.

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
  `.glb` models, by the file's name on the disc, `M100.glb`, the
  weapons', by the weapon model's number, `W00.glb` to `W57.glb`, and the
  first-person view's tiny figures, `T00.glb` to `T26.glb`: PORT.md,
  "The fighters' own models"; `tools/fighter_hd.py` makes a whole mod of
  them, the hd mod, from your disc, and `tools/port_models/build.py` the
  remade mod, the fighters made anew one by one and checked against the
  game's own: PORT.md, "The fighters remade"), `skies/` (a stage's sky, below),
  `lights/` (a stage's lights, `.txt`: "Lights"),
  `music/` (`.ogg` and `.wav`: "Music"), `stages/` (any ending:
  "Stages"), `arenas/` (`.arena`: "Custom arenas"), `weapons/` (`.txt`)
  and `motions/` (`.bbm`: "Weapons and motions"). Under a kind's folder the files may be in folders of any
  names, four deep.
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

- **The scene's faces by the piece**: each face reads a small piece of
  its page that is not a texture of its own, so its key would be a piece
  of a fighter, and a fighter's texture would be hundreds of files. The
  stage's faces are replaced by whole page instead ("The scene's textures
  by page", below); the fighters and their weapons are not (their own
  models are: PORT.md, "The fighters' own models").
- **The fight drawn again under the pause** is the scene as well.
- **The movie**, which the console shows in its 24-bit mode and the
  remastered picture leaves to the software GPU (smoothed). The title
  picture held after it is replaced ("The title picture", below).
- With `--gl-exact` (the prover's picture held to the console's) nothing
  is replaced.

## The scene's textures by page

The stage -- its ground, its walls, its props, whatever of it the
renderer draws -- is replaced by **whole texture pages**: a mod's picture
of one page of 256 by 256 texels, as one of the stage's colour tables
shows it, stands in for every face of the stage that reads that page
through that table. A stage is some twenty such files.

**The key** is `page-256x256-D-HASH`: FNV-1a of 64 bits, as the 2D
layer's, over the depth's code, the width and the height (256 each, two
bytes, the low first), every texel of the page row by row (an index of
one byte, or a pixel of two) and then the colour table (16 colours or 256,
two bytes each). Two things make it the same in every run:

- **Only the stage's own texels count.** A page holds the stage's TIMs
  and, between them, whatever the menus or the stage before left in video
  memory, which differs from one run to the next. `tex_LoadStageGroup`
  says which TIMs it loads and where (`port_RemasterNoteStageTexture`,
  port_remaster.h); a texel whose pixel of video memory no TIM of the
  stage wrote is hashed as 0.
- **The table is the one the stage's file loaded.** The faces name the
  stage's tables in the row the fog writes over all through the frame
  (PORT.md, "The ground as one mesh"); the key takes the colours from the
  row below, where the stage loaded them and nothing changes them.

`src/port/mods.c`, `port_TexturePageKey`, makes it; `tests/port/mods.py`
works it out a second time from this description.

**When it is looked up**: as soon as a stage is loaded, every page and
table the stage's TIMs bring is keyed and its picture read, so nothing is
read in the middle of a fight; a page and table a face names that no TIM
brought is looked up the first time it is drawn. A page the game writes
over is looked up again. With `--dump-textures FOLDER` every page that has
no picture is written into `FOLDER/pages/`, with a list beside it of the
stage's own textures in it (`.txt`), which is what `tools/port_stage_hd.py`
makes its pictures from.

**How it is drawn** (`src/port/gl/gl_scene.c`, `PORT_GL_SCENE_SHADER`):

- The pictures are the layers of one array on the card, all of the first
  one's size (a picture of another size is made that size as it is read),
  with mipmaps down to a quarter of the game's texel and the card's
  anisotropic filtering where it has it. Which layer a page and a table
  have is a texel of a lookup table the shaders read, so the stage's faces
  are still one batch and the ground's mesh still one draw.
- **Nothing of a texture packed beside it is smoothed in.** A page packs
  textures edge to edge with no border, so each face is read only inside
  its own rectangle of texels (its texture points' least and most, out to
  their texels' edges), less half a texel of the level being read: the
  ground's mesh carries each face's rectangle in its vertices, and the
  renderer's geometry shader works out each triangle's.
- **Its colours follow the game's.** Where the game changes the colours a
  face is drawn in after the picture was made -- the fog's rows, which
  the mesh mixes by the depth of each pixel, a tint, a fade -- each part
  of the picture is multiplied by what the colours about that point are
  now over what they were when it was made (the table as loaded, kept as
  the layer's row of a table of bases), each with one added so that a
  black stays a black. Where nothing has changed, that is the picture
  itself. So the fog and the fades come out as the game's.
- A pixel of the picture whose alpha is under a half is not drawn;
  whether a pixel mixes, on a half-transparent face, is still the game's
  texel's to say.
- The stage's pieces -- its railings, fences, walls and roofs -- drawn
  from the ground's mesh (`stage_props_mesh`, PORT.md, "The stage's
  pieces as meshes") read the pictures the same way, each face inside its
  own rectangle.
- With no mod's textures nothing is looked up and no shader takes the
  path; with `--gl-exact` neither.

What it leaves to video memory: a face of a fighter or a weapon, a page
of 16-bit texels, a face read through a texture window, and a face the
side channel has no depth for (the game's own tiles where the ground is
not drawn as one mesh are partly that).

## The stages' pages made larger

The **stages-hd** mod is every stage's pages made four times larger, from
the player's own disc, by `tools/port_stage_hd.py`:

```
python3 tools/port_stage_hd.py dump --image "Bushido Blade 2 (USA).cue"   # Linux or WSL
python tools/port_stage_hd.py build                                          # numpy and Pillow
```

`dump` loads each of the 38 stages on the GL prover (the tour of
`tools/port_stage_prove.py` as far as its fight) with no mod in the way and
`--dump-textures tmp/stage-hd/dump`. `build` writes `tmp/mods/stages-hd/`,
order -200, a picture for each page and table. Each of the stage's own
textures in a page -- a TIM of its file -- is made larger by itself, so
that nothing of its neighbours is smoothed into it, in four steps:

1. **The dither taken out.** The textures are 16 colours each, and their
   artists dithered between two near colours to make a third, which at
   four times is a checkerboard. A texel that stands out from all four
   neighbours the same way round by no more than about a step of the
   texture's own colours is dither, and is smoothed with its neighbours of
   near colour (a bilateral filter); every other texel is kept, so lines,
   edges and the grain of the wood stay.
2. **Made larger along its edges**: twice, then twice again, by
   directional cubic convolution (Zhou, Shen and Zeng, 2012): each new
   texel by a cubic along whichever of two directions changes less about
   it, or both weighed, so an edge is made longer and not wider or
   stepped.
3. **Held to the game's picture**: made smaller again it is the texture it
   came from; what is left over is made larger and added back, three times
   (back-projection), each texel held inside the least and the most of the
   source texels about it, which keeps a bright rim off a dark bar.
4. **Sharpened without halos**: an unsharp mask, held the same way.

A texel of nothing is clear, and the clear and the drawn are made larger as
a shape, with an edge a texel or two wide. The game's own smaller copies of
a texture (half the size, at half its place in the page, which the renderer
reads far off) are made from the larger one's picture made smaller. The
textures of another table in a page are copied a block a texel, so that a
face that drew them through this table would still be drawn.

No learned model and no program of anyone else's is used. It is the game's
own pictures made larger, so it is never committed: copy
`tmp/mods/stages-hd/` into `mods/` beside the program.

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
what it would draw). They are the project's own art: text in the port's
one typeface, Shippori Mincho (`src/port/fonts/`, the SIL Open Font
Licence; [`PORT.md`](PORT.md), "The port's lettering"), its ExtraBold for
the title and the rows -- every row's name at one size, and "Group
Battle", too long for it, smaller on the same baseline -- and its Bold for
"TOP MENU", with the game's look -- white letters fading to pale blue, a dark edge,
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
game's 2D screens drawn again in a font, sharp at any size. It is on
unless `ui = off` in `rebushido.cfg` or the settings menu ("Sharp menus")
says otherwise; its order is -100, as the English menu's.

| What | Its lines |
|---|---|
| The pause menu | Pause Menu, Continue, Return to Sub Menu, Return to Top Menu |
| The menu after a fight | Rematch, Return to Sub Menu, Return to Top Menu |
| The top menu's captions | Story Mode, Vs. CPU Mode, Training Mode, Vs. Mode, Group Battle Mode, Link Mode, Option Menu, and the second ones under the picture (Battle of the Two Schools, Trust Your Skills, Master Your Weapon, Select Battle Stage, May the Best School Win, 2 Player Link Battle, Other Options); Side View and POV on their panel |
| The select screens | Select Character, Select Weapon, Select School, and Select Battle Stage, its "Battle Stage" in the titles' look; every fighter's name, the fourteen every unlock brings among them; every weapon's name (Katana, Nodachi, Long Sword, Broadsword, Naginata, Yari); the stages' names on all three pages and ( Map 1 ), ( Map ? ), ( Map 3 ) |
| The group battle | its page (Group Battle Mode, Number of Entries, Match, Time Limit, Start, Return to Top Menu, and the values: 1 point, 3 point, 3 4 5, 60 120 sec); the teams' names; 1ST 2ND 3RD in each colour; the fighters and the weapons in the list's black and the sides' blue and red, and its small Select Weapon; the card before the battle (the sides, 1ST 2ND 3RD; its fighters' names stay the game's: the card draws them a second time where a mod's picture does not reach) -- each name in each colour that one group battle's picks showed (a name in a colour no run has shown yet is still the game's); WINS, KILLS |
| The training mode | Training Mode, Practice, Practice Round, Return to Top Menu and the six lines under them; the Select button's page (Attack, Dash) |
| The story | the narration that opens it (Off the sea of Setonaikai ...); the stage card (Next Battle Stage, Battle Stage, Outside the Meikyokan) |
| The memory card | Checking Memory Card, Please wait, Do not remove Memory Card |
| Elsewhere | the fight's digits 0 to 4, on and off, Frontal and Reverse |
| ReBushido's options page | every character of the built-in font (`font.c`), eight by eight and the seven by seven its larger text reads |

**How it is made.** `python tools/port_ui.py` draws the pictures into
`src/port/mods/ui/` (`--check` as `port_english.py`'s). The game draws its
lettering as sprites up to 64 texels wide cut from lettering laid out in
a texture page: a line, lines one under another, or a long text wrapped
from one row of the page to the next -- the narration and the training
page are -- now and then in the middle of a letter. A word of one line is
sometimes another line's: the pause menu's "Return to Sub Menu" is the
"Return to " of "Return to Top Menu", then "Sub" and "Menu" from places of
their own. So the script's table is of **blocks**: a block is a rectangle
of a page as the game laid it out, its texts, each with the rectangle or
rectangles its letters fill, its font and its look, any rule drawn under
a title, and the pieces the game cuts from it, each its key and where it
lies (from the dump's log, above). Each text is drawn once, fitted to its
rectangle at eight pixels a texel -- a wrapped text as one line across its
rectangles laid end to end, cut at their widths, so a letter the game
split between two rows is split at the same place -- and each piece is
cut from the block. A piece that holds anything but lettering (an icon
beside a word) is left to the art mod.

The lettering is the port's one typeface, **Shippori Mincho**
(`src/port/fonts/`, the SIL Open Font Licence; [`PORT.md`](PORT.md), "The
port's lettering", which has how a text is fitted to its rectangle): its
Bold for the game's grotesque (the menus' lines, the captions, the
names), its ExtraBold for the fight's digits, and its Regular for the
game's roman, which has a dark edge round it (the select screens'
titles, the schools, the places, 1ST 2ND 3RD, the stage card). These
screens are in the console's mode of 640 by 240, whose texel is twice
as tall as it is wide, so the letters are drawn twice as wide in a texel
as the face has them, to be seen as it has them; texts of one kind (one
face, one height, capitals or not) are one size. The looks are the
game's: letters darkening
downward, with a shadow down and to the right (the pause menu's, the
training page's), a dark edge all round (the select screens'), a panel
under them (Side View, POV) or nothing, each in the colours its own
lettering has. The colours, the rectangles and the looks of the blocks
were measured from dumps of the screens (`--dump-textures`); those of the
first lines (the pause menu's, the captions', the titles') `--measure
DIR...` prints again. Like the English menu's, the pictures are the
project's own, kept in the repository: no pixel of the game's is in them.

The built-in font's characters are a texture each, the key of each worked
out from the font's picture in `font.c` (four characters share a place in
it, each through its own colour table): a character in Shippori Mincho's
Bold, white,
its capitals seven texels high, the tails of g j p q y in the eighth row.

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

## The title picture

The picture held after the opening movie (the logo of Bushido Blade 2 on
white, STR/TITLE.TIM) is not the movie's: the movie player loads it into
video memory beside its display, at x 480, before the movie starts,
shows it in the 16-bit mode from the movie's last frames on while the
movie's sound runs out, and fades it out by copying it into its display
buffers and darkening it ([`formats/MOVOVL.md`](formats/MOVOVL.md)). It is
a picture the game writes into video memory, so a mod replaces it as it
does the boot logo, by the key of its pixels (`320x240-16-...`): while
nothing is shown (the display off, as the player loads it, or the
movie's), a picture of a whole screen's size (320 by 240 or more) counts
as one to be seen wherever it is written, and `--dump-textures` writes it
out (`gl_replace.c`, `port_GlReplaceFindPicture`); the art mod makes it
larger from the player's disc (`tools/port_art.py`, "a picture"). A mod's
picture of a whole screen is drawn whole into the card's video memory,
not only the columns of the display of the moment (`port_GlDrawBlits`).
The copies the fade makes have the same pixels, so they are the same
key.
Nothing else written while the display is the movie's is looked up: each
of its frames is new, and with mods on they filled the table of keys seen
(16,384) before the top menu was reached, so that after the movie no new
texture was replaced -- the English menu among them.

While the player runs, the window shows the software GPU's picture (the
movie is in the 24-bit mode, which the card does not draw), but for a
display in the 16-bit mode that is wholly a picture drawn from a mod's
and not written over since (`port_GlShowsReplacedPicture`, `gl.h`): then
it is the remastered picture, so the title and its fade are the mod's
picture at the window's resolution. With no mod's picture of it the
title is the software GPU's, smoothed, as before.

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

## Lights

The remastered picture lights a stage with lights that have a place --
a torch, a lantern, a fire -- beside the stage's sun and its colour of
the dark, which stay the stage's mood (PORT.md, "Where the lights come
from"). A stage's lights are **a file of every mod's, read in the mods'
order**, each one adding to the lights of those before it, changing
them or putting them out: unlike every other kind, a later mod's file
does not stand in for an earlier one's.

```
mods/
  lanterns/
    mod.txt              order = 10
    lights/
      stage_02.txt       Shainto HQ's lights
```

- The key is `stage_NN`, as the skies': the stage's number in two
  digits (`STAGE_NAMES` of `tools/port_remaster_prove.py`).
- The program ships one mod of them, `mods/lights/` (order -100, under
  yours): the game's own flames lit on the nine stages that have them,
  and the lamps, lit windows and doors of more (below). `stage_lights = off` puts every stage's lights out
  ([`PLAYING.md`](PLAYING.md)); `effect_lights` is the fight's flashes',
  which are no file's.
- The log says what each stage's files came to (`lights: stage 2 has 1
  lights of 1 files`) and any line not used, with why.

### The file

Lines of `name = value`; `#` after a space begins a comment, but for a
colour (`#FF9442`). A light is a block that begins `[light NAME]`:

```
format = 1                # this program reads no other

[light lantern_gate]      # a name of one word, at most 31 letters
kind = point              # point, or spot: a cone
at = 1500 -1200 -3000     # where, in the world's units; y runs DOWN
color = #FFB060           # or three numbers of 0 to 255
intensity = 0.6           # 1 lights a white wall facing it, close by, to its own
                          # colour, as a noon sun does; a torch 0.3 to 0.8
radius = 3500             # how far it reaches; nothing farther is lit
flicker = 0.3             # 0 steady to 1: how much of it flickers
shadow = yes              # asks to cast shadows (the strongest few do)
direction = 0 1 0         # a spot's: which way it points (down)
cone = 30 45              # a spot's: full inside 30 degrees, none past 45
on = stage                # what it is fastened to (below)
times = dusk night        # lit only at these times of day (below)
```

Everything but the name may be left out: a light is a white point of
intensity 0.5 and radius 3,000 at 0, 0, 0 of the stage, steady, casting
no shadow, a spot pointing down with a cone of 30 and 45 degrees.

**How bright.** `intensity = 1` lights a white surface facing the light,
close to it, to its own texture's colour: the game's light byte of 128,
about a stage's noon sun. A torch, a lantern or a lamp near the
fighters sits well below that, 0.3 to 0.8, and `radius` does the
falling off; only a flash goes near 1 or past it (the clash's is 1.5,
`effect_lights`). The shipped files go from 0.3 (a candle at a grave)
to 0.7 (the ferry dock's lamp, high up); the moon or the day through a
hall's doors and windows is 0.5 and 0.4, falling off over the hall.

| `on =` | `at` is | |
|---|---|---|
| `stage` | the world | where it stands |
| `flames` | from each of the stage's own flames (the game's torches and fires, `eff.c`), in the world's axes | one light for every flame the game draws, each a little above the flame's foot, a fire reaching half as far again as a torch |
| `flame N` | the same, from flame N alone (0 to 4) | |
| `fighter N` | from the middle of fighter 1's or 2's body, in the world's axes | a light that walks with him: a lamp he carries |
| `fighter N bone B` | in bone B's own space (0 to 20), turned with it | a glow on a hand or a head |
| `weapon N` | in the N-th weapon drawn this frame's own space (from 0), turned with it | a glowing blade, a lantern on a staff |
| `piece N` | in the stage's piece N's own space (from 0, the pieces as `gl_stage.c` reads them) | a lamp on a prop that falls when it is cut |

A light fastened to something not in the frame (a fighter not drawn, a
weapon past the last) is not lit that frame.

**When.** `times` is one or more of `dawn`, `day`, `dusk` and `night`:
the light is lit only when the stage is played at one of them (the stage
select's time of day, ROADMAP 20, which says what a stage's own time
counts as). Left out, or
`always`, it is lit at every time: a fire burns at noon too, a street
lamp waits for dusk.

**Layered.** A block of a name the files before it already gave
**changes that light**: only the lines it says, the rest as they were.
`remove NAME` puts one light out; `clear` puts out every light the files
before it gave, so a mod's own set stands alone. At most 64 lights a
stage, all the files together.

```
# mods/blue-flames/lights/stage_02.txt: the built-in torches made blue
[light flames]
color = #4080FF
```

### The lights the program ships

`src/port/mods/lights/lights/`, one file a stage that has a light of its
own to give, placed by hand with `tools/port_lights.py`:

- `dump` loads every stage on the GL prover with `--dump-textures`, so
  that the program writes the stage's mesh as it built it -- each
  vertex in the world with its texture point, page and colour table --
  with video memory and the stage's textures (`meshes/stage_NN.bin`),
  and the stage's flames from the run's log (`flames.txt`); `--shots N`
  adds pictures of the fight's tour, six to a sheet, with the lights on.
- `find` reads the meshes, looks at every face's texels as they are
  drawn and gathers what looks lit from within (bright and warm, or
  bright and white) into drafts, beside the game's flames.
- `check` runs the test that holds the shipped files: every line used,
  every light on the stage inside the stage's bounds (`BOUNDS`, measured
  from the meshes by `find --bounds`).

Nothing of the disc is in the files: the flames' places are the game's
own, read as it runs (`on = flames`), and what is placed by hand is
numbers of where.

What the program ships, each looked at in the pictures of its tour:

| Stage | Lights | |
|---|---|---|
| 0 Main Tatara shrine | 4 spots | the two open doors of the hall: the moon through them at night, the day by day |
| 1 Meikyokan dojo | 4 spots | the two high windows of the hall, the same |
| 2 Shainto HQ | `on = flames`: 4 | candles on stands, and two before the Buddha |
| 3 Tatara shrine | `on = flames`: 4; 2 | fire baskets on the deck's railing; the hall's paper screens lit from within (dusk, night) |
| 4 Bus stop | 2 spots | the fluorescent tubes under the shelter's roof (dusk, night) |
| 6 Tatara shrine path | `on = flames`: 4 | stone lanterns |
| 8 Outside Meikyokan | `on = flames`: 2; 1 | fire baskets by the dojo's door; the stone lantern by the path (dusk, night) |
| 16 Under the railway | 2 spots | the street lamps on the two utility poles (dusk, night) |
| 20 Kabuki stage | `on = flames`: 4; 3 | torches along the walkway; the lanterns under the theatre's eaves (dusk, night) |
| 22 Ferry dock | 1 spot | the tall street lamp at the quay's edge, lit |
| 23 Shainto gate | `on = flames`: 2 | small fires on the gate's deck |
| 24 Purple cavern | `on = flames`: 4 | candles by the iron gate |
| 29 Building rooftop | 9 | the lamp over the stairwell's door; the red aviation lights at the corners of the two towers beside it (dusk, night) |
| 30 Building parking lot | 5 spots | the fluorescent lights down the ceiling |
| 31 Shainto cemetery | `on = flames`: 2 | candles at two graves |
| 32 Team battle 1 | 6 spots | the band of windows along the hall's south wall, the moon's and the day's |
| 33 Team battle 2 | `on = flames`: 4 | fire baskets at the square's corners |

A light of a window or a door comes twice, `times = night` in the moon's
cool colour and `times = dawn day dusk` in the day's; a lamp is lit from
dusk; a fire burns at every time. None on the rest: open ground lit by
the moon or the day alone (the bridges, the fields, the coast, the
cliffs, the railway, the castle rooftop), or places abandoned, whose lamps
would not be lit (the abandoned ship, station, tunnel and shipyard), or
with lamps drawn unlit (the outside of the Meikyokan's posts), or with a
source no fight's camera sees (the lighthouse's lamp, 16,000 up).

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
| `bb2_m1-5`, `-6` | on the disc; nothing of the original asks for them (the stage select's row of music can: below) |
| `bb2_m1-7` | the top menu |
| `bb2_m2-0`, `bb2_m2-1` | chanbara, the one clan and the other |
| `bb2_m2-2`, `bb2_m2-3` | a practice round; practice |
| `bb2_m2-4` | the staff roll |
| `bb2_m3-0` to `-5` | the voices of six endings |
| `mar_xa00-N` to `mar_xa28-N`, `end_xa1-N` to `end_xa3-N` | the story's cutscenes' voices, stream N |
| `opening` | the opening movie's sound |

A versus fight's piece is its stage's (`g_snd_stage_music_tracks`,
`src/main/fight_data.c`): one of `bb2_m1-0` to `-4`, so one piece is
heard on several stages, and a mod replaces it on all of them. With the
window's `fight_music = stage` (the default) every mode's fight plays
its stage's piece, versus the CPU, story and group battle too, and the
stage select's **row of music** (L2 and R2: [`PLAYING.md`](PLAYING.md),
"Music on the stage select") chooses any piece of the table's music for
a fight, `bb2_m1-0` to `-7` and `bb2_m2-0` to `-4`: a mod's
replacement of one is heard wherever it is chosen.

### A piece of its own

A mod's piece whose key is **none of the disc's** (`music/my
theme.ogg`, `music/rain.wav`: any name that is not a file of the disc's,
alone or with a dash and a number) stands in for nothing; it is offered
by its key in the stage select's row of music, after the game's own
pieces, up to 32 of them in the order the mods are read. Chosen, the
fight starts its stage's own stream of the disc's as ever, and the
mod's piece is heard in that stream's place, as a replacement is (the
loop, the length cut at the original's, the volumes: above), for as long
as the stream plays; the next stream the game starts ends it. So the
game's state is what it is with the stage's own piece, and
`music_mods = off` plays that piece. A session of netplay does not
offer them: the other player may not have them (and the row is part of
what both machines hold the same).

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
replace the files; a **custom arena** (below) brings a ground of its own
and the tables with it, and stands in for one of the game's stages.

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

### Custom arenas

An **arena** is a stage of a mod's own (ROADMAP 18): a ground made in
Blender, or in anything that writes glTF or OBJ, and the program's
tables for it, in one file, `arenas/NAME.arena`, that **stands in for
one of the game's stages, its host**, where the run says so:

```
rebushido --image GAME.cue --arena courtyard              # wherever its host is fought
rebushido --image GAME.cue --fight courtyard kannuki mikado   # straight into a fight on it
```

**On the stage select** every arena of the mods is on a page of its
own: Select on a stage page opens it, Cross on one fights on it in place
of its host, which the player is never shown (PLAYING.md, "Kinds of
stage"; PORT.md, "The stage select's kinds of stage"). The page shows
the arena's `name` and its picture: the file's preview (`preview =` of
`arena.txt`, a PNG), or one the compiler draws of the scene. The choice
is the game's state: rolled back with it, the same on both machines of
a session, and changed from one fight to the next (another arena, or
the host itself from the grid, is loaded again).

With `--arena` the arena is also fought whenever its host is chosen from
the grid or fought by a mode with no stage page, and from `--fight HOST
...`. Without it and with none chosen on the page no arena is in play
and the game is exactly what it was: the host's own files and tables,
the play tests' traces the same.

**Making one.** A source folder of `arena.txt` and a scene
(`tools/arenas/courtyard/`, the sample, is one), compiled by

```
python3 tools/arena_compile.py SOURCE_FOLDER mods/my-arenas/arenas/NAME.arena
```

The scene's objects are told apart by their names: `collision...`
objects are what the fighters stand on and walk into, each face's
**surface its material's name** (`firm`, `firm2`, `sand`, `sand2`, `wet`,
`wet2`, `barrier`, `plain` for a wall; what follows a `.` or `_` is the
author's own, `sand_yard`); `start1a`, `start1b` ... `start4b` are where
the fighters of each of four pairs start (empties; the compiler stands
each on the floor under it); `obstacle...` is a post a blade stops at
(its middle and its top); everything else is drawn, each material's
picture made 64 by 64 in 16 colours. The scene is Y up in metres as
glTF and Blender write it (`scale = 1000` makes a metre 1,000 of the
game's units: the fighters are about 1,700 tall). A face is drawn and met
from its front only. What is drawn and what is met are two meshes on
purpose: steps drawn as steps are met as a ramp (the sample's), a wall
of a barrier may be met where nothing is drawn.

`arena.txt` has the scene's name and the program's tables, every line
optional but the scene:

```
name = Stone courtyard
scene = courtyard.glb           # .gltf, .glb or .obj with its .mtl
scale = 1000
stands_in_for = 25              # the host: a stage of the select screen
                                # with no hook, areas, props or code of its
                                # own (0, 1, 6, 9, 10, 12, 15-17, 19-22,
                                # 25-31); 25 if not given
start1 = -2000 0 0 2000 0 0     # a pair given here and not by the scene;
                                # pairs not given are the first
light = 0xE8DCC4 0x3A3A48 0xD8CCB4 0x403E50 0x0D800A7D 0x101418
                                # light.c's preset: the fighters' sun and
                                # ambient, the scenery's, the sun's pitch
                                # and yaw (pitch << 16 | yaw), the distance's
fog = on                        # and fog_span = 32000, fog_farthest = 0|1
music = 0                       # one of the stages' five pieces (0 to 4)
sounds = 0                      # the stage whose sound file, echo and
                                # ambience are heard (LOADSE's STAGEnn.BIN)
lights = courtyard-noon         # the remastered light's key (lights/)
chain = ring 6 37 -500 -500 500 -500 500 500 -500 500
                                # the CPU's obstacles (cpu.h, NavChain):
                                # ring or open, a kind, a margin, corners
texture flagstone = flagstone.png   # a material's picture, if the scene
                                    # does not carry it
preview = preview.png           # the stage select's picture of it; if not
                                # given, the compiler draws one of the scene
                                # (from above, each face in its texture,
                                # 320 by 240): `--preview SOURCE OUT.png`
                                # draws only that, to start one from
```

A value not given is the host's own. The `.arena` file is that text
(the compiler writes the starts it found in) and then the ground file
(`ground = BYTES`, its last line):

```
RBARENA 2
name = Stone courtyard
stands_in_for = 25
...
start1 = -2000 0 0 2000 0 0
preview = 62572
<the picture, a PNG, and a newline>
ground = 61440
<the ground file, formats/GROUND.md>
```

(Format 1 is the same without the picture; the program reads both, and
an arena with no picture is shown on the page as a card of its name.)

A file wrong in one line is not used at all, and the log says which
line and why (`arenas: x (mods/a/arenas/x.arena): line 4: fog is on or
off; left out`); `--arena` of an arena no mod has ends the run.

**How it is played.** As the program starts (`port_ArenasStart`,
`src/port/arenas.c`) every arena is read and the place of its host's
ground in NDATA.DAT found (`port_FindArenaRoom`); an arena longer than
that is left out. While one is in play -- the game's state says which,
`port_StageArena` -- the disc reader asks for its ground in place of
its host's sectors first (`port_ArenaGroundSector`, from
`port_OverrideDiscSector`, the stage mods' way), so the game loads it as
it loads its own, by its own commands, into its own arena block. Where the game reads one of
its stage tables it asks the port, with its own value, and is given the
arena's for the host's row and its own for every other stage
(`include/port_arenas.h`, under `RB_PORT`):

| The game reads | Where | The port's answer |
|---|---|---|
| where the fighters start | `fight_GetStartPositions` (`scene_frame.c`), which all five readers go through | a copy of `g_fight_start_positions` with the host's row the arena's |
| the light | `light_SetStage` (`light.c`) | the arena's preset |
| the fog | `stage_QueueFog` (`stage.c`) | whether, how deep, the farthest copy |
| the music | `snd_StartFightMusic` (`fight.c`) | the arena's track |
| the sound file, the echo, the ambience | `fight_LoadStage`, `fight_Init` (`fight.c`), `snd_UpdateStageSounds` (`fight_event.c`) | the stage the arena is heard as |
| the CPU's obstacles | `cpu_Decide` (`cpu.c`) | the arena's chains |

The host is chosen so that nothing else of it is asked for: no hook
(`g_stage_hooks`), no areas or props, no `g_fight_stage == STAGE_...` of
its own in the code, and on the select screen. Stage 25 (Abandoned
Station) is the default for its room: 87 sectors (178,176 bytes), and
73,604 bytes kept in memory. **A new stage number** (38 and on) was not
taken: `g_fight_stage` indexes nine tables of the game, the music's has
only 32 rows, NDATA's ids past `0x4C` are the tiny models, the select
screen's grid is the game's; an arena standing in for a stage needs none
of that changed, and item 16's select page can choose it the same way
(below).

**Gameplay, rollback, netplay.** The arenas are read once and only read
after; none of them is the game's state (`config/port_state.txt`,
`outside`). Which is in play is: `g_port_stage_choice`
(`src/port/stage_choice.c`, a state file) beside the stage the game
keeps. What the game makes of an arena -- its ground in the arena
block, the fighters' places, the light -- is in the game's own
variables, saved and put back as the disc's stage's are. The session's
set-up hashes every arena the stage select offers (each file and key,
in their order) and the one `--arena` asks for, `port_ArenaSessionHash`,
with the gameplay mods: a guest whose arenas differ, or who has none
where the host has some, is turned away, "the other player's mods that
change how the game plays (weapons, motions, stages, the arena in play)
or --weapon and --arena are not the same as this one's" (`make
port-net`'s session check runs it, both sizes).

**The validator.** `python3 tools/arena_validate.py FILE.arena` fails
an arena a fight cannot be fought on, saying where:

1. the file is one the port reads, its ground six sections read whole
   and written again the same;
2. within the game's limits: a plain host; the ground no longer than the
   host's on the disc and no more of it kept in memory than the host's;
   500 tiles, 1,000 entries of the cells' lists, 256 corners a tile, a
   collision grid of 65,534 bytes; every collision face convex, on its
   plane, within the 8,000 units of its middle the GTE's projection
   reaches; 20 colour tables in the fog's row;
3. **the floor**: from every start, every point a fighter can get to is
   walked 250 units at a time with the game's own cast in Python
   (`tools/ground_cast.py`, `collision.c` number for number): a wall
   stops the walk, one he could climb does not; every point reached must
   have a floor under it, and the grid's edge must never be reached. So
   the barriers must close the arena, or ground lie where the author
   meant a fighter to fall to;
4. **the starts**: each on a level floor of its height (not a wall's or a
   barrier's surface), no wall within 500 units, the two of a pair 1,500
   apart, among the points walked;
5. **what is met is what is seen**: a floor drawn within 200 units of the
   floor met at every point walked, a face drawn within 200 units of
   every wall met but a barrier;
6. with `--fight` (in WSL, the programs of `make port`, the player's
   disc): the CPU against the CPU on the arena (`--fight NAME kannuki
   gengoro:nodachi --fight-cpu --no-cards`, a play test's plan written
   for each program), **both sizes of the port printing the same trace**,
   both fighters within the points walked and a floor's height at every
   checkpoint, a winner before 20,000 blanks.

**The sample**, `tools/arenas/courtyard/`: a stone courtyard of 16 by 16
metres, flagstones with two beds of sand, three steps of 15 cm up to a
dais along the north wall, a wall 3 metres high all round and two wooden
posts, built in Blender by `courtyard_blender.py` (headless: `blender -b
--factory-startup --python ...`; its textures by `make_textures.py`) and
exported as `courtyard.glb`, which is what is compiled: 96 tiles of 208
faces, 640 collision faces in 100 cells, 5 textures, 2 posts; a ground of
61,440 bytes of the host's 178,176, 44,580 kept in memory of its 73,604.
The window's build ships it compiled, as the mod `arena-courtyard`. The
validator passes it, the fight included: a fight of 9,800 frames to a
kill, both sizes the same, 329 lines. Its first draft had a hole -- the
collision floor's rectangles missed a strip beside a sand bed that the
drawn floor covered -- and the CPU, walking there, fell through the
world to a y of 7.3 million; the validator's walk found the hole at
the same place, (-5,250, -1,250), and `tests/rebushido/arena.py` keeps
such a hole failing.

**How it is proved.**

- `tools/stage_ground.py roundtrip FOLDER`: the 38 grounds of the disc
  (`--dump-stages`) read into numbers and written back byte for byte.
- `tests/rebushido/arena.py` (`make rebushido-test`; nothing of the disc):
  the cast on a grid made by hand (a face met from the front only, the
  next cell's, none past the grid), the sample compiled twice the same
  and read back whole, the validator passing it and failing it with the
  floor taken from under the first start.
- `tests/port/arenas.c`: an arena's file read and every line that can be
  wrong refused with its reason, its picture found, the mods' arenas with
  one in play and one left out, every answer to the game for the host,
  for the other stages and with no arena, the ground read in place of the
  host's, the arena in play changed to another, to none and back, the
  session's hash.
- `tools/port_stage_select.py` (`make port-stage-select`, a step of the
  gate): the stage select's page driven to a fight on the sample and then
  to its host from the disc, on both sizes (PORT.md, "The stage select's
  kinds of stage").
- `tools/arena_validate.py --fight` on the sample, above; `make
  port-net`'s session check refusing a guest without the host's arena;
  the play tests (`same_as_original` and the port's) and port-rollback
  as they were, with no arena in play.

### What a stage editor needs next

- **The stage select (ROADMAP 16)**: done, above. Left: more than five
  arenas are scrolled through five rows; a page of pages, or the
  arenas' pictures as a grid like the stages', when there are many.
- **Larger arenas**: a ground longer than its host's needs NDATA.INF
  given its new length and room in the arena block (`ARENA_STAGE`); and
  past the grid's 32 by 32 cells, the game's collision and its cells'
  tables are of that size.
- **Pieces**: the compiler writes tiles alone; a piece (a mesh of its own
  placed and turned, `stage_ReadCells`) would let a repeated thing be one
  mesh drawn many times, and could be opened up when it hides a fighter.
- **Textures** larger than 64 by 64, repeating across a face (a texture
  window), and more than 12 of them; the stage's pages from a mod's
  pictures in the remastered picture as the disc's are
  ("The stages' pages made larger").
- **The remastered picture's ground as one mesh** is proved stage by
  stage on the disc's grounds (`g_stage_proved`); an arena stands on its
  host's number, so `stage_mesh` takes the host's verdict; an arena
  should be held to it as the disc's grounds are
  (`tools/port_stage_prove.py`), and was not yet.

## Weapons and motions

A weapon is **tables in the program and motion files on the disc**. The
roadmap's later step, new weapon types and motions, needs both read as
data; the port now reads the weapon's numbers from a mod's file and a
mod's motion file in place of the disc's. Hitboxes, motions and timing
are the game's balance ([`ROADMAP.md`](ROADMAP.md), "After the
picture"): with no such mod the game reads its own tables and computes
exactly what it did (below, "How it is proved").

### What defines a weapon

There is **no damage number**, by weapon or by move. A blade is the
segment between its points, swept from last frame's place to this
frame's (two triangles, four when the tip moved 2,500 or more,
`fight_TestBladesOnBodies`, `src/main/fighter_hit.c:1858`), with no
thickness, met against the body's spheres (`hit_SphereMeetsTriangle`,
`src/main/hit_test.c`). A hit **kills** when it reaches the core of the
head, the chest or the hips; elsewhere it wounds, takes the off arm, or
counts a wound to the sword arm or the legs (`fighter_TakeHit`,
`fighter_hit.c:448`). So a weapon's reach is its points, and its power
decides only clashes and locks.

| | Where | Indexed by | What it is |
|---|---|---|---|
| **The blade's points** | `g_fighter_weapon_point_sets`, `g_fighter_weapon_point_counts` (`src/main/fight_data.c:346`, 348) | weapon | two points from the hand (bone 18, `FIGHTER_BONE_WEAPON_HAND`), the tip first; three for the naginata and the yari, whose shaft (points 1 to 2) is swept apart and never kills (`fighter_hit.c:1327`, 1984). Read every frame by `fighter_PlacePoints` (`fighter_pose.c:703`) |
| The story's weapon | `g_fighter_story_weapon_points` (`fight_data.c:301`) | | the points of `WEAPON_MODEL_STORY`, as long as a nodachi's |
| Second weapons | `g_fighter_sub_weapon_points` (`fight_data.c:361`) | sub-weapon kind | the off hand's sword or gun while it is out |
| **The body's spheres** | `g_fighter_body_point_table` (`fight_data.c:264`) | body point (22) | bone, place, radius and core of each; scaled to the model by `fighter_ScaleBodyPoints` (`fighter_pose.c:660`) |
| **Speed and power** | `g_fighter_speeds`, `g_fighter_powers` (`fight_data.c:666`, 699) | [character][6] | how fast a character's moves run (0x1000 as the motion) and how hard he hits (the clash: `fight_ClassPowerGap`, `fighter_hit.c:78`, `g_fight_clash_events`, `fight_data.c:1404`). Six columns: the guns read into the next character's row (the original's behaviour). Read by `fighter_Init` (`fighter.c:913`) |
| Wounds | `g_fighter_wound_powers`, `g_fighter_lost_arm_speeds`, `_arm_wound_speeds`, `_wound_speeds` (`fight_data.c:733`, 875 to 923) | character | power and speed left after wounds |
| **Stances and motion set** | `g_fighter_stance_kinds` (`fight_data.c:559`) | [character][weapon] | the three stance kinds; with the weapon in the top digit it is the motion set, whose file `g_motion_set_files` (`fight_data.c:594`) names; `g_fighter_default_stances` (629) the one a fight begins in |
| The moves | `MOTION/*.BBM` ([`formats/BBM.md`](formats/BBM.md)) | motion set | every move, the motion it plays, its frames, what may follow; each motion's head (kind, length, turn, **reach for the CPU**) and its marks: **when the weapon can hit** (`MOVE_MARK_HIT_START`, `_HIT_END`), the second blade's, the off hand's, the sounds of the swing. Loaded by `motion_LoadFighterFiles` (`fighter.c:89`), looked up by `motion_GetMove` (351) |
| **Models** | `g_fighter_clan_weapon_models` (`fight_data.c:1352`), `g_fighter_practice_weapon_models` (1368) | [weapon][clan], weapon | which of the 58 weapon models (`enum WeaponModel`, `include/weapon.h`) is in the hand; loaded by `resource_RequestWeapon` (`resource_weapon.c:48`) with its scabbard (`g_resource_weapon_linked`, `resource_data.c:318`), drawn on the hand by `weapon_Queue` (206); a mod's `models/Wnn.glb` draws one (PORT.md) |
| Dropped | `g_object_dropped_weapon_kinds` (`fight_data.c:401`), `g_object_kinds` (789) | [clan][weapon] | the loose object a defeated fighter's weapon becomes, and how it flies |
| **Sounds** | `g_resource_weapon_sound_kinds`, `_alt` (`fight_data.c:1003`, 1037) | [character][weapon] | which bank of weapon sounds is loaded (the cut, the clang, the lock, the shot); the swings are the motions' frame events |
| **The CPU** | `g_cpu_weapon_reaches`, `g_cpu_weapon_ranges` (`src/main/cpu_data.c:318`, 319) | weapon | how far the CPU takes an attack of the opponent's to reach, beyond the motion's own reach (`cpu_AttackReach`, `cpu.c:984`), and the distance its follow-ups are tried from; `g_cpu_side_step_stances` (`cpu_data.c:15`), `g_cpu_practice_rows` (435); the CPU's scripts are in the motion files; guns have branches of their own (`weapon_id >= WEAPON_ID_FIRST_GUN`, eleven in `cpu.c`) |
| Guns | `fighter_TestShot` (`fighter_hit.c:1680`), `FIGHTER_GUN_SHOTS` (`fighter.c`) | | a shot is a segment four `ONE`s long from the muzzle, once a move, with ammunition |
| **The select screens** | `g_select_weapon_ids`, `g_select_weapon_places` (`src/main/select_data.c:16`, 80), `SelectFile.weapon_pixels` (`include/select.h`) | place | the six blades in the crossed-weapons grid, their pictures and names from the select screen's file; each clan has five of them (no yari for the Narukagami, no naginata for the Shainto, `select.c:1677`); Tsubame and Katze have their guns, chanbara the katana, the team battle one weapon each (`select_team.c`), VS CPU its list (`g_vscpu_weapons`, `fight_data.c:1073`) |
| Unlocks | | | none for weapons: the unlocks are the fighters' (`select.c:506`) |

### A mod's weapon

`weapons/NAME.txt`, lines of `name = value`, `#` beginning a comment,
`-` and `_` the same in a name:

```
format = 1               # the version of this format: the one line every file has
name = Long katana       # for the log; the file's name if it does not say
base = katana            # whose model, motions, sounds and CPU it fights with:
                         # katana long_sword nodachi broadsword naginata yari
                         # long_gun short_gun
replaces = no            # yes: it stands in for the base wherever that is
                         # fought with; no (the default): a new slot
points = 0 -60 1250, 0 0 100   # x y z of each point from the hand, the tip
                         # first; as many as the base has (2, or 3 for the
                         # naginata and the yari); -8192 to 8192
speed = 0.875            # times the character's with the base (0.0625 to 4)
power = 1.25             # the same, for clashes and locks
models = 2 3             # the Narukagami's and the Shainto's model: a blade's
                         # are 0 to 9, a gun's 46 and 47
practice_model = 52      # team battles' and practice's: 51 to 56
default_stance = 2       # the stance every character begins in: 0, 1 or 2
cpu_reach = 300          # what the CPU takes it to reach beyond a move of the
                         # fighter's own file (an even number to 510) ...
cpu_range = 64           # ... and of the common file, and its follow-ups'
                         # distance (eights, -1024 to 1016)
```

Only `format` and `base` must be there; whatever else is left out is the
base's own. A file that is wrong in any line is **not used at all**,
and the log (`logs/`, or the error output) says which line and why:

```
weapons: broken (mods/x/weapons/broken.txt): points: this base's weapon has three points, the tip, where the blade meets the shaft and the butt; left out
weapons: katana-long ("Long katana"): a new slot on the katana, 2 points, speed 4096/4096, power 4096/4096
```

A format later than the program's is refused with "a later ReBushido is
needed"; a later program reads every earlier format.

**A new slot** is a weapon of its own made on its base: a fighter has it
when the run says so, **`--weapon 1=katana-long`** (or `2=`), and then
whenever his weapon is its base, in every mode. The select screens do
not show it yet (below). **A weapon that replaces** its base is
everybody's: with two that replace one weapon, the later in the mods'
order is used and the log says so.

Where the game reads its tables, the port asks `port_weapons.h`
(`src/port/weapons.c`), under `RB_PORT`: the points in
`fighter_PlacePoints`; the speed, the power, the model and the first
stance in `fighter_Init`; the reach and the range in `cpu_AttackReach`
and `cpu_Decide`. Each is given the game's own value and, with no mod's
weapon in play, gives it back untouched. The fighter's `weapon_id` stays
the base's, so every rule of the game that asks which weapon he has (the
pole arms' shaft, the guns, the scabbards, the motion set, the sounds,
the dropped weapon) is the base's.

### A mod's motions

`motions/NAME.bbm` stands in for the disc's `MOTION/NAME.BBM`: `u123`
for U123.BBM, `common` for COMMON.BBM (the keys are the disc's names,
[`formats/BBM.md`](formats/BBM.md), "File names and motion sets"). It
is read as a stage's file is: put over the disc's sectors of the file
(`port_OverrideDiscSector`, `src/port/discfiles.c`), loaded by the
game's own commands into the game's own place for it, shorter than the
disc's or as long and never longer. So a mod can change any move of a
motion set: which motion a move plays and what may follow it (the
moves), when its weapon can hit (the marks), the frames themselves, and
the CPU's scripts. The log says it: `motions: u123 from ..., 252816
bytes over the disc's sectors 3871 to 3994`.

**What is not done yet**, and how it would be:

- **A motion of a mod's own beside the game's**, not in place of one:
  a weapon's file saying `motions = KEY`, and its fighters loading
  that file into their motion slot in place of their set's
  (`motion_LoadFighterFiles`). The slot's file is found by the set's
  number (`g_motion_loaded_sets`), so the mod's set needs a number of
  its own (a weapon class past 7 in the set's top digit) that the rest
  of the game, which reads the weapon from the fighter and not from the
  set, never sees; the slot's room is the largest disc file's.
- **One move attached to another file's motion**: a move's motion is a
  number in its own file's table, and its frames are in that file's
  stream, so a move of another file's is the file's own after a merge:
  a tool (beside `tools/inspect_bbm.py`) that adds a motion, its marks
  and its frames to a file and points a move at it.
- **New weapon types on the select screens**: a slot past the eight
  with its place in the grid, its picture and its name
  (`select_data.c`, the select screen's file), and its own
  `weapon_id` everywhere the game indexes by weapon (the tables above,
  the motion set's digit, the sounds' table, the CPU's): the port's
  table would give those rows for it.
- **A weapon's sounds and its dropped object** as numbers of the file.

### Netplay

The weapons' and the motions' files, like the stages', are hashed into
the session's set-up with every `--weapon` given (`port_WeaponsSessionHash`,
`src/port/session.c`): a guest whose gameplay mods differ is turned
away, "the other player's mods that change how the game plays ... are
not the same" ([`NETPLAY.md`](NETPLAY.md), section 13). Both players
give the same `--weapon`s, which say both places'. What is read is
none of the game's state (`config/port_state.txt`): the table is made
once and only read; which weapon a fighter has is worked out each time
from his place and his `weapon_id`, which are the game's, so a rollback
has nothing of it to put back. A motion file is the game's state where
the game loaded it, as the disc's is.

### How it is proved

- **Without a mod** (the port's own runs of `port_game`'s plan, both
  sizes): every line the program prints is the build's before the change
  (the plain `--plan` trace), and the rollback self-test passes.
- **A longer katana**, made at test time and never committed:
  `weapons/katana-long.txt` with the tip at 1,250 for 950 and
  `--weapon 1=katana-long`. Both sizes print the same trace, which is the
  plain run's until the fight's 147th frame (frame 1,673), where the
  first fighter's blade reaches the second and throws him back; then
  the fight goes otherwise, and the first fighter is the one cut down.
  The rollback self-test passes with it (101 rounds, 808 frames).
- **A motion file**: U123.BBM (the first fighter's) copied from the disc
  into `motions/u123.bbm`: the trace is the plain run's, line for line,
  and the log says the file was read over the disc's. The same file
  with every hit's first frame three later (67 moves): the trace is the
  plain run's until the fighter's first blow (frame 1,612), and the
  fight is lost.
- `tests/port/weapons.c`: a weapon's file read, every line that can be
  wrong refused with its reason, the factors, a folder of mods with a
  new slot, one that replaces and one left out, every answer to the
  game with and without them, and the session's hash, worked out again.
  `tests/port/discfiles.c`: a motion file of a made-up disc's MOTION/
  over its sectors.

## For the code

| | |
|---|---|
| `include/port_mods.h`, `src/port/mods.c` | the folder read, the manifests, the table (`port_ModFind(kind, key)`), the texture's key |
| `include/port_png.h`, `src/port/png.c` | a PNG reader of every kind but the interlaced, with its own inflate, and the plainest PNG writer; no C library |
| `src/port/gl/gl_sky.c` | a stage's sky (`PORT_MOD_SKIES`), looked for once a stage |
| `include/port_lights.h`, `src/port/lights.c` | a stage's light files (`PORT_MOD_LIGHTS`) read and layered (`port_ModFindEach`), and what the effects say they lit up (`eff.c`, under `RB_REMASTER_VIEW`) |
| `src/port/gl/gl_light_list.c` | a frame's lights made from them, each on what it is fastened to, into the renderer's list (`gl_lights.h`); the mesh's dump for `tools/port_lights.py` |
| `tests/port/lights.c`, `lights.py` | a light file and its mistakes, two mods layered, the game's notes, and the shipped files held to their stages' bounds |
| `tools/port_lights.py`, `src/port/mods/lights/` | the stages' lights the program ships, and what helps place them |
| `src/port/gl/gl_replace.c` | the keys looked up per primitive and per picture, the mods' PNGs given to the card, the textures written out |
| `src/port/gl/gl_draw.c` | the hooks: a batch's replacement texture (`PortGlState`), the shader's `u_replaced`, the picture's in the stretch of video memory |
| `src/port/host_*.c` | `port_HostListFolder`, `port_HostMakeFolder` |
| `include/port_music.h`, `src/port/music.c` | the music: a stream's key, following the drive, the piece decoded and put in place of the disc's sound, `--dump-music` |
| `src/port/vorbis.c`, `src/port/vendor/` | stb_vorbis as it ships, and the empty C library headers it is compiled over |
| `src/port/xa.c`, `src/port/spu.c`, `src/port/sound.c`, `src/port/cd.c` | the hooks: the drive's volumes on other samples (`port_MixCdAudio`), the chip's `heard`, the call in the mix, the stream playing (`port_GetCdStream`) |
| `include/port_discfiles.h`, `src/port/discfiles.c` | the disc's directory, a stage's files by key, `--dump-stages`, the mods' files over the disc's sectors (stages and motions); `src/port/disc.c` calls it for every sector it reads |
| `include/port_weapons.h`, `src/port/weapons.c` | the weapons' files read and checked, `--weapon`, the answers to the game (`fighter_pose.c`, `fighter.c`, `cpu.c` under `RB_PORT`), the session's hash of the gameplay mods (`port_ModsGameplayHash`, `mods.c`) |
| `tests/port/weapons.c` | the weapons' files, the folder, the answers and the hash |
| `tests/port/music.c`, `music.py`, `tests/port/discfiles.c` | the music's keys, loops, files and player; the directory and the stage files |
| `tests/port/mods.c`, `mods.py` | the manifest, a folder of mods and its order, the keys from this page, PNGs of every kind written by zlib |
| `tools/port_english.py`, `src/port/mods/english/`, `src/port/fonts/` | the English menu the program ships, and what draws it |
| `tools/port_ui.py`, `src/port/mods/ui/` | the sharp menus the program ships, and what draws them |
| `tools/port_art.py` | the art mod, made from the player's disc |
| `src/port/gl/gl_scene.c`, `gl_scene.h` | the stage's pages: the keys looked up as a stage loads, the pictures as one array, the shaders' part |
| `tools/port_stage_hd.py` | the stages-hd mod, made from the player's disc |
| `tools/port_models/`, `tools/blender/fighter_unwrap.py`, `fighter_scene.py` | the remade mod (the game's fighters made smooth and their textures cleaned, held to the game's skeleton, proportions and outline), made from the player's disc; `tests/rebushido/fighter_check.py` |

The mods' files are none of the game's state (`config/port_state.txt`,
`docs/NETPLAY.md`): the table is made once, as
the program starts, and only read after; a rollback has nothing of it to
put back.

**A new kind** (the models, say) asks `port_ModFind(PORT_MOD_MODELS,
key)` with a key it decides, and reads the file with `port_ModReadFile`.
The folders are already read; `g_port_mod_kind_endings` says which
ending a kind's files must have.

## What is next

- **The fighters' textures by page**, as the stage's are: their own
  models are the larger step, and are taken already.
- **More of the lettering in a font**: what no tour of the dumps
  reached (the setup mode's own pages, the slash mode's, the results of
  the other modes, the endings) is the art mod's, made larger; each is a
  block of `tools/port_ui.py`'s table away, measured from a dump.
- **Music by stage**: a key for a fight's piece with its stage
  (`bb2_m1-0` on stage 7 alone), so that a mod can give each stage a
  piece of its own though the game shares five among them.
- **FLAC**: a third kind of music file, with a decoder of whole numbers
  (dr_flac, public domain, is one).
- Stages and arenas: "What a stage editor needs next", above.
- Weapons and motions: "What is not done yet", above.
