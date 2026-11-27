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
  `lights/` (a stage's lights, `.txt`: "Lights"), `dressing/` (a
  stage's grass, water and particles, `.txt`, and its meshes, `.obj`:
  "Dressing"),
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

### Made from your disc

Three mods are the game's own pictures and models made anew, which no
package may carry: **the art** ("The art mod"), **the stages' pages made
larger** ("The stages' pages made larger") and **the fighters** with their
geometry fixed (`tools/port_models/README.md`, "The geometry-only pass").
The PC package makes them itself, from the player's disc, the first time
it starts with it, so a player who has only the zip and a disc image gets
what the owner has (the owner, 2026-11-19):

```
Preparing the remastered art from your disc... 2/3
Stages HD
the stages loaded: stage 17
[==============            ]  48%   1:12
```

- **What does it.** The package's `prepare/` (`tools/port_prepare_bundle.py`
  puts it together): CPython 3.12 for Windows with numpy and Pillow, the
  very files of the Python the developer tools' `build` steps run on, made
  a Python of that folder alone (`python312._pth`: no setting, variable or
  package of the PC's is seen), the art's learned upscaler
  (`prepare/upscaler/`: `realesrgan-ncnn-vulkan.exe`, `vcomp140.dll` and the
  x4plus model, each held to its SHA-256 by `tools/port_prepare_bundle.py`,
  and their licences in `prepare/licenses/`: "The art mod"), and the
  repository's tools as they are:
  `tools/port_prepare.py`, `port_art.py`, `port_stage_hd.py`,
  `fighter_export.py` and `port_models/geometry.py`, with the plans the
  program plays (`prepare/plans/`, written by the packager from the
  program's own `.sym`). About 18 MB of the zip, and the upscaler 33 MB
  more (the model's weights 31 of them: they hardly compress); the zip is
  62 MB in all.
- **How the upscaler reaches the player.** In the package, beside the
  Python (asked for 2026-11-20 where the licences allow, over a download
  at the first start): its licences (MIT, BSD 3-Clause) let it be given,
  the player needs nothing from the internet, and the zip carries the
  very bytes the packager checked. The cost is the zip's size, 33 MB more
  for every player, those with no Vulkan card too, and so for every
  update that changes the package. A download at the first start, held
  to the same SHA-256, would keep the zip at 29 MB and cost a player with
  a card the release's whole 45 MB zip from GitHub (its three models and
  a sample video are one file), a first start that falls back to the art
  without the model when GitHub cannot be reached or is slow, and one
  more thing for the program to fetch, check and unpack.
- **How.** `src/port/window/prepare.c`, at a player's start (no `--plan`,
  `--frames`, `--fast` or `--mods`; `mods` and `disc_mods` on), looks at
  `mods/art`, `mods/stages-hd` and `mods/fighters`: each is made unless its
  `made-from-disc.txt` says the key of this program's version and this disc
  (`ReBushido 0.1 disc 701714496-D5201087`: the version without its
  commit, the disc's sectors by size and the CRC-32 of their first
  megabyte). A folder with a `mod.txt` and no `made-from-disc.txt` is the
  player's own and is never written over. It runs
  `prepare\python\python.exe prepare\tools\port_prepare.py` in a job of its
  own and draws the tool's `progress` lines on a screen of its own before
  the game's window opens. The tool runs **this program itself** to dump
  the textures, as the developer tools run the GL prover: `--hidden
  --no-sound --fast --plan`, `--mods` of nothing, `--dump-textures`, and a
  settings file of the prover's own (`CHILD_SETTINGS`: the stages the
  original picks, its fight music, 4:3, nothing of the picture's costly
  work), seven runs of the art's tour at once and the 38 stages' tours six
  at once, on a window never shown, on the PC's graphics card. Then the
  same `build` functions the developer tools call, and fighter_export and
  geometry for Tatsumi. Each mod is written in `prepare-work/` and moved into
  `mods/` whole, `made-from-disc.txt` beside its `mod.txt`; when all three
  are made `prepare-work/` goes.
- **Skipped and resumed.** Escape, or a controller's B, Back or Start, ends
  the tool and everything it started (the job) and the game starts with
  what was made. What was finished is kept in `prepare-work/` -- each run
  of the program that dumped (`.done` beside it), and each mod made -- and
  the next start goes on from there. Closing the window ends the program.
  `disc_mods = off` (the settings menu's Mods page) never runs it;
  `--prepare-only` makes them and ends, with no game.
- **The same bytes.** Same disc, same program: what the package makes is
  byte for byte what the developer tools make (`tools/port_prepare_prove.py`
  unpacks the zip into a folder of its own, runs its program with no Python
  on PATH and SDL's dummy drivers, makes the three again by the developer
  tools, and compares every file; the fighter with the owner's
  `tmp/mods/fighters/` too). Three things had to be held for it:
  the program's runs play with the prover's settings, not the window's
  (the window's stage select in every mode and its fight music took the
  tour to other screens); the GL prover is compiled again when the commit
  changes (`tools/port_gl_build.py`), since the options page draws the
  version and its letters are among the textures dumped; and numpy's BLAS
  is held to one kernel (`OPENBLAS_CORETYPE=Nehalem` in the three tools),
  since OpenBLAS picks one by the processor (this PC's is SkylakeX's, a PC
  with AVX2 and no AVX-512 gets Haswell's) and the stages' pages made with
  another of its kernels differed in 40 of 955 pages; Nehalem's runs on
  every processor numpy runs on. numpy's own code for each processor
  (`NPY_DISABLE_CPU_FEATURES=X86_V3`, as a PC without AVX2 runs it) gives
  the art and the stages the same bytes; the fighter's model, whose
  vertices are floats written to the last bit, is the same on every PC
  with AVX2 and FMA (any of the last ten years) and differs in a few of
  its floats on one without. The art's pictures and paintings made by the
  learned upscaler are the graphics card's arithmetic: the package and
  the developer tools make them on the one PC's card, and they are the
  same bytes; made on two PCs' cards they may differ by a few steps of 255
  ("The art mod", "With no model"). Without the model the art is the same
  bytes on every PC, as before.
- **Time and room**, measured on the owner's PC (Ryzen 9 9950X3D, its own
  graphics card): 205 seconds in all; the art 63 (50 of them the seven
  runs that dump), the stages 139 (75 dumping, 64 making the 955 pages
  larger, six at once), the fighter 3. `prepare-work/` holds about 500 MB
  while it works; the mods are 190 MB (the art 29, the stages 160). With
  the art's learned upscaler (2026-11-20, an RTX 3090): the art's `build`
  17 seconds (the model 6 of them), the art 39 MB; the whole first start,
  stopped once at 100 seconds and started again as the prover does it,
  200 seconds.
- **Never shipped.** `tools/port_release.py` refuses any folder of `mods/`
  it does not make from the repository (and any file of `prepare/` it did
  not write, a `prepare-work/` or a picture outside `mods/`), and
  `tools/owner_copy.py` copies the owner's `tmp/mods/art`, `stages-hd` and
  `fighters` only where they have a `mod.txt`, and `prepare/` beside the
  owner's `rebushido.exe`, so the owner's copy makes what is missing as a
  player's does.

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
  stage's faces and the fighters' are replaced by whole page instead ("The
  scene's textures by page", below); the weapons in their hands are not
  (their own models are: PORT.md, "The fighters' own models").
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

What it leaves to video memory: a face of a weapon, a page of 16-bit
texels, a face read through a texture window, and a face the side channel
has no depth for (the game's own tiles where the ground is not drawn as
one mesh are partly that).

### A fighter's pages

**The fighters are replaced by whole page too**, the same way: a mod's
picture of one of a fighter's pages, seen through one of his colour
tables, stands in for every face of his that reads that page through that
table. The key is the stage's, `page-256x256-D-HASH`, made the same way,
but **of the fighter's own texels**: as each fighter's model loads
(`model.c`, slot 0 the first fighter's, slot 1 the second's, two pages to
the right), it says which TIMs it loaded and where
(`port_RemasterBeginFighterTextures`, port_remaster.h; `tex_LoadTim` notes
each), and a texel of the page that no TIM of that fighter wrote is hashed
as 0, whatever the stage or the other fighter put there. So video memory's
pixels have an owner each -- the stage, the first fighter or the second --
and a page and table are keyed by their owner's pixels, the owner being
whoever loaded the table the faces name.

- **The table is the one the model's file loaded.** On a stage whose fog
  falls on a fighter (`stage_GetFoggedModel`), his faces name the fog's
  block of tables, which the fog writes over all through the frame; the
  key takes the colours from the plain copy of them the game loads below
  that block, as the stage's come from the row below the fog's. A fogged
  fighter's extra textures (the groups `model_LoadExtraTextures` loads on
  request) are loaded straight into the fog's rows with no plain copy, and
  are left to video memory.
- **The second fighter's tint.** Where the game tints the second fighter's
  clothes (`model_TintOpponent`, a fighter against himself), it writes his
  tables: the page is looked up again and its key is of the tinted colours,
  so the tinted clothes are a page of their own, dumped and replaced on
  their own. A fade or the fog's colours after that are followed as the
  stage's are (the colours now over the colours the picture was made from).
- **A write that changes nothing is passed over.** The game writes the
  same 16 by 32 pixels at the corner of each fighter's first page every
  frame; looking his pages up again for each (some thirty keys of a whole
  page a frame) cost 2.5 ms of the processor in a fight and put
  `port-bench` over its budget (2026-11-25). A rectangle written into
  video memory looks a page up again only where it changed a pixel of
  the backend's copy (`port_GlMarkMirrorFrom`, `src/port/gl/gl_draw.c`).
- **Not gated by the stage's kind.** A stage fought Classic draws the
  stage's faces from the game's own textures, not a mod's pages; a
  fighter's pages are his on every stage.
- **With the other replacements.** A face is drawn from a page only where
  no 2D replacement (a texture of the 2D layer, by its own key) stands in
  for it, and a fighter drawn by his own model (`models/M100.glb` and the
  rest: PORT.md, "The fighters' own models") draws none of the game's
  faces, so his pages are not read at all. A page of the stage and one of a
  fighter never stand in for each other's faces: the lookup knows whose
  table it holds.

`--dump-textures FOLDER` writes a fighter's pages into `FOLDER/pages/` as
the stage's, the list beside each naming its owner (`owner stage`, `owner
fighter 1` or `2`) and that owner's own textures in it;
`tools/port_stage_hd.py` makes the stage's alone larger.

- **Two on two's partners** (PORT.md, "Two on two"): a team's second of a
  character no slot holds has his textures in a second bank of the port's
  video memory, where his slot's would be in the console's. His pages are
  keyed and replaced the same way, of the bank's pixels and his own TIMs
  (`model_PortLoadPartner` says which), the list beside a dumped one
  saying `owner fighter 3 bank` (the first slot's partner) or `owner
  fighter 4 bank`; where his texels and colours are what his character's
  are in a slot, the key is the same, and one picture serves both. A
  partner of his first's character is his first's copy, his pages his
  first's. Four fighters bring more pages than two, and a face as it
  changes (eyes shut, mouth open) is a new key of its page: where the
  pictures found outgrow the card's array of them, it is made twice as
  large and the pages read again, up to 192.

**How it is proved** (the GL prover, `port_game`'s plan, Graphics:
Remastered, no mod but this): the first fighter's pages dumped from the
fight (27 pages and tables), each given its colours turned (red to green,
green to blue, blue to red) at four times the size, and put in a mod. The
pictures are the plain run's, pixel for pixel, until the fight; in the
fight the first fighter alone differs, his clothes and skin in the turned
colours, the stage, the second fighter and the lettering as they were. The
trace of the run is the plain run's, and so are both sizes' of the port
with the mod. Two on two's (2026-11-25, the Windows prover on the card,
`--fight tatara-shrine kannuki mikado --partners tatsumi:nodachi
utsusemi:yari --teams cpu`): the fight's pages dumped (25 of fighter 1,
24 of fighter 3 in the bank, 42 of fighter 4 in the bank, his face's
states among them), fighter 1's made red and the partners' green with a
grid: in the fight Kannuki is red, Tatsumi and Utsusemi green and gridded
all over, Mikado and the stage as they were.

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
`tmp/mods/stages-hd/` into `mods/` beside the program. The PC package
makes the same bytes itself at its first start ("Made from your disc").

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
| The select screens | Select Character, Select Weapon, Select School, and Select Battle Stage, its "Battle Stage" in the titles' look; every fighter's name, the fourteen every unlock brings among them; every weapon's name (Katana, Nodachi, Long Sword, Broadsword, Naginata, Yari); the stages' names on all three pages and ( Map 1 ), ( Map 2 ) (the game's own reads ( Map ? )), ( Map 3 ) |
| The group battle | its page (Group Battle Mode, Number of Entries, Match, Time Limit, Start, Return to Top Menu, and the values: 1 point, 3 point, 3 4 5, 60 120 sec); the teams' names; 1ST 2ND 3RD in each colour; the fighters and the weapons in the list's black and the sides' blue and red, and its small Select Weapon; the card before the battle and after each (the sides, 1ST 2ND 3RD, and every fighter's name, the twenty of the game's sheet of them) -- each name in each colour that one group battle's picks showed (a name in a colour no run has shown yet is still the game's); WINS, KILLS |
| The training mode | Training Mode, Practice, Practice Round, Return to Top Menu and the six lines under them; the Select button's page (Attack, Dash) |
| The story | the narration that opens it (Off the sea of Setonaikai ...); the stage card (Next Battle Stage, Battle Stage, Outside the Meikyokan) |
| The memory card | Checking Memory Card, Please wait, Do not remove Memory Card; Memory Card has been removed; Save current data on Memory Card? YES / NO |
| The options (setup) | Option Menu and its rows (Monitor, Difficulty, Music, Memory Card, Key Configuration, Return to Top Menu) in white and in the cursor's gold, and their choices (Color, B&W, Hard, Easy, On, Off); the memory card's page (Memory Card, Load Data, Save Data, Return); the key configuration (Key Configuration, 1P and 2P with their rules, Frontal Attack, Reverse Attack, Change Stance, Default, Custom, and EXIT, N/A and OK on their panels) |
| The slash mode | Slash Mode, the top menu's caption and its page's title; 100 Kills Time Trial Records; its banner, 100 Kills Time Trial |
| The staff roll | every heading and name of it, Square's upright and the Lightweight staff's leant as the game's italic is (the face has no italic of its own: `text(slant=)`) |
| Elsewhere | the fight's digits 0 to 9 (5 to 9 the slash mode's clock reaches), on and off, Frontal and Reverse; REMATCH, under the story's 再戦 |
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

The screens the tours reached last (the options' pages, the slash mode,
the staff roll; "The art mod", below) are sections of the table of their
own, and their texts say their screen (`text(screen=)`), so that a kind
of theirs is one size among themselves and the sizes of the screens before
are as they were. A row the cursor is on in the options is drawn through
a colour table of its own, gold, not a tint of the white one's (which the
program would draw from the white picture: "Fades"), so its gold is a block
of its own, cut where the white is. What stays the art's: lettering that is
part of a picture (the endings' pictures, whose Japanese characters the
port's cut of the face has not, and the staff roll's logos); the
Japanese titles (再戦, the HUD's 時間); and the best times' digits, of
which the tours draw only those of the times they show, so that a time
lettered from both would mix two faces. `tests/rebushido/ui_screens.py`
holds those sections: every piece inside its block, with a picture, of one
block alone, holding letters, a rule or a panel; the staff roll's pieces a
name each, whole; the gold cut where the white is.

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
python tools/port_art.py build                                          # Windows: numpy, Pillow, the upscaler
```

`dump` plays the game with no window on the GL prover
(`python3 tools/port_gl_build.py`), eight runs at once, seventeen tours
(`port_art.MODES`): from the boot through each mode of the top menu, its
select screens, a fight with its pause menu, the fight's end and the menus
after it; versus mode's stage pages walked across, left to the first and
right to the third (the other runs take the page they open on); the
options (`setup`: each choice both ways, the memory card's page, the key
configuration with both pads' cursors walked down it); the slash mode
(`slash`: opened by writing everything won into the save as the top menu
opens, as `--unlock-all` does but in the plan alone, so that the first
start's runs need no argument of their own -- and its run
won as it begins, a `poke` of the hundred opponents' count and one of the
opponent struck down, so that the replay, the best times and the save's
question follow as the game has them, each pressed on from a checkpoint of
its own, so that a load that takes other frames on one program than on
the other moves nothing; the endings of a run won are the endings' tours',
since with everything won already the game shows none); and the
endings (below), every texture written into `tmp/art/dump/` with no mod
in the way (about 1,590 of them), and where
each was in video memory into `where.txt` beside them (from the log's
`mods: wrote` and `mods: had` lines: every run says where it saw each
texture, whichever run wrote its file, so the file is the same however the
runs fall). A player who would rather play the screens through runs the
game with `--dump-textures FOLDER` and gives `build --dump FOLDER` (as
many as they like). `build` writes `tmp/mods/art/`, order -200 -- under
the built-in mods and under any of the player's own -- with each texture
six times larger (`--scale`), but those a built-in mod replaces (`ui`,
`english`), which would never be drawn from it and which a dump has or
has not from run to run (the group battle's card names, in the colours
one battle's picks show), so that the mod is the same bytes whatever the
runs drew; with `ui = off` those are the game's own. By its kind:

- **a few colours** (six or fewer: an emblem, a frame, a button): each
  colour's area smoothed as a shape of its own (bicubic, at twice the
  scale) and every pixel given the colour whose area is strongest, then
  averaged down, so an edge between two colours is a smooth line and no
  colour is mixed into a new one inside an area;
- **lettering and the like** (up to 48 colours): the colours smoothed
  over their alpha, and the outline made sharp again from the smoothed
  alpha, a pixel or two wide;
- **a painting** (8 to 48 colours that cover nine tenths of the texture or
  more, a side of 16 texels or more, and not one ramp of one hue: the
  backgrounds' tiles, the clouds of the top menu, the stage select's maps
  and its scroll, the select screens' panels and emblems) and **a
  picture** (more, or 16 bits: a portrait, the top menu's pictures of the
  modes, the loading screen's map, the boot logo): **Real-ESRGAN's x4plus
  model** (below), four times larger, then Lanczos to six.

A texture with a side under four texels is left as it is.

**The model.** [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s
`RealESRGAN_x4plus`, through
[realesrgan-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan),
a program of its own that runs on any graphics card with Vulkan and needs
no Python of its own: the release of Real-ESRGAN v0.2.5.0
(`realesrgan-ncnn-vulkan-20220424-windows.zip`, SHA-256
`abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d`). Its
licences, read from the repositories themselves (`tools/upscaler/`): the
program is MIT (with realsr-ncnn-vulkan's MIT, which it grew from), built
with Tencent's ncnn, BSD 3-Clause; the weights are published in Real-ESRGAN's
own releases under that repository's BSD 3-Clause licence, with no terms of
their own. (They were trained on DIV2K, Flickr2K and OST, whose terms are
the datasets' and do not reach the weights by any licence the project
publishes.) The zip has no licence file of its own, so the package carries
the three texts. Tried and not taken: `realesrgan-x4plus-anime` and
`realesr-animevideov3`, in the same zip, which flatten the clouds, the maps
and the faces into areas of one colour; x4plus keeps the paint's grain and
takes away the dither. Not looked at further: models of other licences
(4x-UltraSharp and the like are CC BY-NC-SA).

How it is used, in `tools/port_art.py`:

- **The tiles put back together.** The game cuts a background or a map
  into tiles of 64 texels and lays them side by side in a page of video
  memory, one colour table for all. Made larger one by one, each had an
  edge of its own, a seam the model's detail made plain. So `build` reads
  `where.txt` and lays every texture of one depth and colour table where
  it was, joins the ones that touch -- and only where the step across their
  join is no more than twice the steps beside it and six (`goes_on`): the
  top menu's moon lies as its two halves side by side, and the page of the
  stage select's maps holds two maps one over the other -- and makes each
  such canvas larger whole, then cuts each texture from it. Two textures
  that overlap and differ (two pictures loaded into one place at two
  times) are on canvases of their own. A canvas that holds a painting or a
  picture is the model's whole, so a cloud tile of less than nine tenths
  goes with its neighbours.
- **Round each texture or canvas** eight texels of its edge are repeated
  for the model to see, and what it does not draw takes its nearest drawn
  texel's colour, so no black creeps in from outside.
- **The outline** is today's way's (the picture's or the lettering's), and
  **the darkest colours** too: where today's way gives a texel no part
  over 24, its colour is today's, and the model's is mixed in above that.
  The model lifts black a step or two, which a texture the game draws
  adding or subtracting showed as a box round it.
- **One ramp of one hue** stays today's: a soft shape of a few steps from
  dark to light (the moon, which the game subtracts from the red sky, the
  flares of a fight) is one line of colours, and the model gives it a hard
  edge in place of its soft one. The spread of the colours across their
  main line over the spread along it is 0.025 for the moon and 0 for the
  flares; 0.03 is the limit, and the loading screen's map of ink on paper,
  of one hue as well, is 0.039.

**With no model.** `--upscaler off`, or no upscaler found, or one that
writes nothing (a PC with no Vulkan driver, where it says `invalid gpu
device`, or one slower than ten minutes): a painting is made as
lettering and a picture by Lanczos and an unsharp mask, which is every
texture as it was before the model, byte for byte (the same dump, the
tool of before and `--upscaler off`: no texture differs; `mod.txt` says
which way it was made). Those are numpy's and Pillow's and the same on
every PC. The model's are the same bytes every time on one graphics card
(the art made twice on the RTX 3090), and not on another: the same canvases
on this PC's own Radeon came out within 9 steps of 255, 1.2 on the
average, which no eye sees. `build` finds the upscaler at `--upscaler
FOLDER`, `RB_UPSCALER`, the package's `prepare/upscaler/`, or
`tmp/art-tools/realesrgan-ncnn-vulkan-20220424/` of the checkout or of
the main one; it is a Windows program, so a `build` in WSL makes the art
with no model.

**What it costs**, on the owner's PC (RTX 3090): the whole `build` 23
seconds, the model's part 7 of them: 209 textures on 74 canvases. The mod
is 46 MB. From one disc (2026-11-22, the seventeen tours): 1,591 textures
dumped, of which the built-in mods replace 567; 141 too small, 380
lettering, 294 of a few colours, 145 paintings and 64 pictures by the
model; 4 steps of fades. (Before the options, the slash mode and the
endings were toured, and with the built-in mods' textures in it: 1,368
dumped, 17 seconds, 39 MB.)

**The endings.** Ten endings (`enum Ending`, nine pictures) follow a
story, a full run against the CPU, a group battle or the slash mode won:
nothing a tour plays reaches one by playing. Each tour `ending-N` begins a
fight straight from the start (`--fight`; PORT.md, "Straight into a
fight") and, two frames in, writes the ending's number into
`g_fight_ending` and the screen `SCREEN_ENDING_INIT` into `g_sys_screen`
(`poke`s, as the game's own step does where a run is won): from there it
is the game's own way, the ending's picture over its scene and then the
screen after that ending. `ending-0` goes on through the staff roll to its
end (2,880 frames) and the save's question; `ending-6` and `ending-7`, a
slash mode won fast, are told the run had no continue as well
(`g_chanbara_clean_unlock`), so the game goes on to endings 8 and 9 as it
does. Each is a few hundred blanks to its ending: the fastest way there,
where a story played through is half an hour.

**The story's stage card** ("Next Battle Stage", the map and the stage's
picture) is drawn twice, into both display buffers, and then left on show
while the stage loads, the route's marks drawn once each into it: in the
window, whose mods' pictures are read beside the game and come a frame or
two after they are first asked for, it kept the game's own textures (an
art pass, 2026-11-21, saw it the same with the art as without). The card
and its marks now say so (`port_RemasterKeepsPicture`, PORT.md, "What
nothing clears is read at once"), and their pictures are read at once.

Copy `tmp/mods/art/` into `mods/` beside the program; it is never
committed (the PC package makes it itself at its first start: "Made from
your disc").

### What is covered

```
python3 tools/port_coverage.py play     # Linux or WSL: the tours, dumped, into build/coverage/
python  tools/port_coverage.py report [--art tmp/mods/art] [--check] [--sheet]
```

`play` plays the art's tours as `dump` does, keeping each tour's log;
`report` holds every texture each tour drew to the mods that replace it --
the built-in ones (`ui`, `english`) and the art made from the disc -- and
writes `build/coverage/report.txt`: for each tour how many are `ui`,
`english`, `art`, a step of a fade of one of those (`fade`), too small to
make larger (`small`, a side under four texels: a rule, a cursor's dot, as
the game drew them), or `NONE`; and, as a list of its own, the lettering
the art alone replaces (the art's kind `lettering` that no built-in mod
has: where the sharp menus are still to reach, though that kind has the
frames and icons as well). `--check` ends 1 if a texture is `NONE`;
`--sheet` draws each tour's of no replacement and of the art's lettering
with their keys. A tour that ended short of its screens says so.

With the art built from the same dump every texture the tours draw has a
replacement (2026-11-22: 1,591 textures; 567 the built-in mods', 883 the
art's, 141 small). So the gap it shows is what the tours
do not reach, which no tool can list, or an art mod made before a tour was
added: the art of a disc made before the options, the slash mode and the
endings were toured has 159 `NONE` of them. What no tour reaches:

- **the link cable's picture** and a link battle: the port has no link
  cable;
- **a group battle played to its end**: its endings are the
  `ending-4` and `ending-5` tours', but its result screen
  (`SCREEN_TEAM_RESULT`) is not toured;
- **a run against the CPU played to its end**, and its kill count, and
  **the story past its first fight** (its later stage cards, cutscenes and
  interludes): the tours stop after one fight;
- **the memory card's pages with a card in** (load, save and their
  messages): every tour runs with no card, so that a run is the same on any
  PC;
- **the debug menu**, which no player reaches.

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

## The movie made larger

The opening movie's frames made four times larger by a learned upscaler,
which the window shows in place of the console's frames with `movies =
enhanced` (the setting built in; PORT.md, "The movie made better"). It is
the game's own film made anew, so it is a mod each player makes from their
own disc and is never shipped or committed:

```
python tools/port_movies.py all --image "Bushido Blade 2 (USA).cue" \
    --upscaler PATH\realesrgan-ncnn-vulkan.exe
```

- `dump` runs the port with no window to the movie's end with
  `--dump-movie FOLDER`, which writes each frame as it is first shown,
  `movie-NNNN.png` by its number in the stream (`src/port/movie_shown.c`):
  2,814 frames, the 320 by 240 the chip decodes. (The last 506 of the
  stream's 3,320 are behind the title picture and never shown.)
- `upscale` runs **Real-ESRGAN's ncnn program** with the model
  **realesr-animevideov3** at four times, on any graphics card with
  Vulkan. The player downloads it: the release
  `realesrgan-ncnn-vulkan-20220424-windows.zip` (or `-ubuntu`, `-macos`) of
  github.com/xinntao/Real-ESRGAN, v0.2.5.0, which has the model in its
  `models/`; the tool is given the program's path and runs it with paths
  as its arguments, nothing else of the download.
- `build` writes the frames as baseline JPEGs, quality 85, into one file,
  `tmp/mods/movies-hd/movies/opening.rbmv` (the layout is in
  `src/port/gl/gl_movie.h`), with a manifest of order -200. Copy
  `tmp/mods/movies-hd/` into `mods/`.

**Licences**, read from the sources themselves: the program
(Real-ESRGAN-ncnn-vulkan) is MIT, Xintao Wang; ncnn, which it is built on,
is BSD-3-Clause, Tencent; the model's weights are released in the
Real-ESRGAN repository, BSD-3-Clause, Xintao Wang, with no terms of their
own. The port reads the JPEGs with stb_image (Sean Barrett, MIT or public
domain), compiled in (`src/port/vendor/stb_image.h`). None of the upscaler
is in the program or the package.

**Time and room**, on the owner's PC (RTX 3090): 89 seconds in all -- the
dump 9, the upscale 72 (39 frames a second), the JPEGs 8 -- and 125 MB.

**Why it is a step of its own, not the first start's** ("Made from your
disc"): it makes 125 MB where the three mods of the first start make
190 MB together, for one minute and fifty seconds of film; it is a minute
and a half on an RTX 3090 and many minutes on a PC's own graphics, on a
Vulkan driver nothing can vouch for (with none it writes nothing); and a
learned upscale differs from card to card in the last bits, so it cannot
be held to the same bytes everywhere as the first start's mods are. So a
player chooses it. Where the PC package keeps the upscaler in
`prepare/upscaler/` for the art, the tool finds it there (no `--upscaler`)
once the model, `realesr-animevideov3-x4.bin` and `.param` (1.2 MB), is
put beside the art's in that list of files; `--gpu N` picks the card.
The art itself uses `realesrgan-x4plus`, which keeps more of a still
picture's texture; for the film the video model was chosen, made for
frames that follow one another, and four times faster. Without it `movies = enhanced` still cleans
and enlarges the console's frames on the card, which is most of the way
for the film's smooth parts (PORT.md, "The movie made better").

The port shows the mod's frame N when the player shows frame N (the
stream's own number, followed through the player's buffers), so the film
keeps the disc's sound in step, frame for frame, and a mod with fewer
frames than the stream shows the console's frames after its last.

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

## Dressing

A **Remastered Classic** stage (PLAYING.md, "Kinds of stage") is
dressed: grass, bushes, reeds and flowers drawn by instancing, swayed by
the stage's wind and pushed aside by the fighters and by a blade swept
near the ground; water that reflects the sky, the stage and the fighters,
deeper in colour where it is deep, foaming along its shore and rippling
where a fighter wades or the rain falls; puddles; rain, candle smoke, sea
spray, and mist lying on the ground in banks the fight's camera looks through; shafts of light through a hall's doors and windows with
the dust hanging in them; dust kicked up by steps, leaves and petals
falling, embers rising from the fires, fireflies; scenery meshes, alone or
in a piece's place. ROADMAP 17; how it is drawn is PORT.md, "The
dressing".

**It is the picture's alone.** Nothing of a dressing is the game's state
or changes a value the game computes: a fight, a replay and a session
online are the same with it and without it, and two players online may
have different dressings (`tools/port_dressing.py hashes` holds it). It
needs Graphics Remastered and the ground drawn as one mesh
(`stage_mesh`); a Classic stage, an arena and the Original picture (F2)
have none of it. `dressing = off` turns it off.

```
mods/
  my-meadows/
    mod.txt              order = 10
    dressing/
      stage_08.txt       outside the Meikyokan
      boulder.obj        a mesh a stage's file names
```

- The key is `stage_NN`, as the skies' and the lights' (`STAGE_NAMES` of
  `tools/port_remaster_prove.py`); a mesh's file is any other name, `.obj`.
- **Layered** as the light files are: every mod's file of the stage, in
  the mods' order, each adding blocks, changing a block of the same kind
  and name (only the lines it says), `remove NAME` one, `clear` all those
  before it. At most 48 blocks a stage.
- The log says what each stage's files came to (`dressing: stage 8 has 5
  blocks of 1 files`), what it grew into (`dressing: stage 8 grown from 5
  blocks: 27000 tufts of grass, ...`) and any line not used, with why.

### The file

Lines of `name = value`, `#` after a space a comment (not a colour's):

```
format = 1

[wind]                   # the stage's one wind
strength = 0.5           # 0 to 4; 0.5 a breeze
direction = 30           # the way it blows, degrees about the upright
gusts = 0.35             # 0 steady to 1

[grass lawn]             # a kind and a name of one word
area = -13500 -4500 11500 19500   # x0 z0 x1 z1: where (up to 8 lines)
avoid = -1000 -20000 0 20000      # ... and where not (up to 8)
on = green               # which ground it grows on (below)
count = 20000            # how many tufts at most
height = 90 190          # how tall, the least and the most
```

The world's units, the world's y running down, as the light files'.

| Kind | What | Its own words |
|---|---|---|
| `grass` | tufts of seven blades, bent by the wind, pushed by what passes | `height`, `width`, `on`, `level`, `heights`, `sway` |
| `flowers` | three blades and a bloom of five petals; `color` is the bloom's | the same |
| `reeds` | three tall blades, one with a brown head | the same |
| `bush` | a mound of leaves; **looks solid** unless `solid = no` | the same |
| `water` | a level surface that reflects the sky, the sun or the moon, the lamps, and the stage and the fighters; waved by the wind, rippled where a fighter wades, ringed by rain; deep or shallow by the ground under it, foaming along its shore; or puddles | `y`, `color`, `shallow`, `clarity`, `waves`, `foam`, `reflect`, `sky`, `flow`, `puddles`, `rain`, `on` |
| `particles` | `kind = dust`, `leaves`, `embers`, `snow`, `petals`, `fireflies`, `rain`, `smoke`, `motes`, `spray`, `mist` or `gulls` | `size`, `top`, `count`, `on`, `glow` |
| `shafts` | the light of the stage's own lights through a door or a window, as shafts in the air, with motes hanging in them | `light`, `at`, `size`, `count`, `glow` |
| `mesh` | a scenery mesh of the mods' (`file`); **looks solid** | `file`, `at`, `turn`, `scale`, `sit`, `piece`, `culms`, `color`, `tint` |
| `wind` | the stage's wind | `strength`, `direction`, `gusts` |

What every kind may say: `area`, `avoid`, `count`, `color` (`#RRGGBB`,
three numbers, or `ground`: the stage's own colour under each place),
`tint` (each channel times the colour over `#808080`), `times` (`dawn`,
`day`, `dusk`, `night` or `always`: shown only then; the stage's own time
counts as night, as the light files'), `solid`.

- **Where foliage grows.** Each place is looked up on the ground's own
  mesh, its highest face that looks up: kept where that face is level
  enough (`level`, 0.8 if nothing says), is the ground's and not a
  piece's, lies within `heights = Y0 Y1` where the block says, and its
  texture's colour there is of the kind `on` says -- `green` (grass and
  moss), `earth` (browns, a wooden floor too), `sand`, `stone` (greys),
  `snow` (near white), `blue` (water's own colours) or `any` -- and so
  is the ground a tuft's reach about it, so that a path's stones stay
  bare. The colour of each thing is that texture's own, a little richer,
  unless the block gives one. So a dressing names no colour of the disc
  and carries none: it says where and how much, the stage says the rest.
- **Water** is a level quad of each `area` at height `y`; or, with `on`,
  laid on the ground's own faces of that colour in its areas, `y` above
  them (a stream's bed, whatever its shape). `color` is the water's own
  where one looks down into it, `clarity` how much of what is under it
  shows (0 to 1), `waves` their size (0 to 4). Where the ground under a
  level water lies deep it is `color`, where shallow `shallow` (lighter
  and greener than `color` if it says none); the ground standing above it
  is its shore, where `foam` (0 to 4, 1 if nothing says) laps, and rough
  water (`waves` 1.5 and more) has caps of it. `reflect` (0 to 1, 0.6) is
  how much it mirrors the stage's pieces and the fighters (the picture's
  own reflections, `reflections`, as a floor that shines), `sky` (0 to 1,
  1) how much of the sky it mirrors: 0 under a roof. `flow = WAY SPEED`
  (degrees, units a second) carries its ripples along. With `on` the
  water is laid on all the ground's faces in its areas and kept, texel
  by texel, to where the ground's texture is of that colour: a stream
  painted across the faces (the stage under the lost bridge's). With
  `puddles` (0 to 1: how much of the ground) it is the ground's own faces
  of `on`'s colour in its areas, wet only in patches, darkened about
  them, and `rain` (0 to 4) rings it with drops.
- **Particles**: `dust` is kicked up where a fighter steps, inside its
  areas (anywhere, if none) and on its `on` ground, its colour the
  ground's unless it says; `embers` rise from the stage's own flames
  (`on = flames`); `leaves`, `snow` and `petals` fall over the first
  area from `top` above the ground, drifting with the wind; `fireflies`
  hover from the ground to `top` and blink. `size` is a particle's.
  `rain` is streaks falling from `top` over the first area, slanted by
  the wind, and a splash for every third where it lands; `smoke` rises
  from the stage's flames (`on = flames`, or from the ground over its
  area), spreading and thinning; `motes` hang in the air over the area
  and are seen where the stage's lights fall on them; `spray` is thrown
  up in bursts along the area, as waves break, and falls back; `mist` is
  a volume of mist over the first area, thick at the ground under each
  place (the ground's own faces, within `heights` where it says) and
  thinning up to about `top` over it, in banks of `size` that drift with
  the wind, seen through from the fight's camera, held by the ground and
  the fighters, lit by the time of day (`glow` how thick, `color` its
  own); `gulls`
  circle places of the area, each at its own height up to `top` over the
  ground and its own pace, banked into the turn, flapping a few seconds
  and gliding the rest, `size` half a wing's span (a gull is a body, a
  tail and two jointed wings, not a quad). `glow` (0 to 4, 1) makes any of
  them brighter or thicker.
- **Shafts** take each of the stage's lights the block names (`light =
  NAME ...`, up to four, of its light files, "Lights" above), shown at the times the light is, and draw its light through
  an opening `size = WIDTH HEIGHT` across the light's way, centred on
  `at` (or the light's own place), to the floor: a box of air whose light
  is gathered along the line of sight, in blades as through a lattice,
  fading along it, with `count` motes hanging in it.
- **A mesh** is a Wavefront OBJ in the world's units, y down: `v x y z`
  or `v x y z r g b` (its colour, 0 to 1), `f` of three or more corners
  (a polygon is made a fan; texture and normal numbers are read past).
  It is turned `turn` degrees about the upright through its middle,
  scaled by `scale` and set at `at`, or, with `sit = yes`, on the ground
  under `at`'s x and z, `at`'s y above it. With `piece = N` it **stands
  in for the stage's piece N** (the pieces as `gl_stage.c` reads them)
  and keeps its silhouette: it is fitted into the piece's own box, so it
  is never larger than the piece, a mesh whose proportions are more than
  a quarter from the box's is refused (the log says so), and the piece
  is left out while it stands (a piece cut down takes its mesh with it;
  so does a piece the game leaves out right by the camera). With
  `culms = yes` as well it is set at **each culm of the piece** instead:
  each upright column of the piece's faces that stands on the piece's
  foot, fitted to the column's height and to its wider side (a little
  more, so a round culm keeps a square column's corners inside it), and
  the piece is kept (a bamboo clump's leaves stay).

### The check

`python3 tools/port_dressing.py check` (in WSL; a test of
`tests/run.py --rebushido`, `port/dressing`) reads every file as the
program does and fails where:

- a line is not understood, or a block lies outside its stage;
- **anything that looks solid** -- a mesh, a bush, foliage marked
  `solid = yes` -- **could stand where a fighter can walk**: where the
  stage's collision grid has a floor a fighter gets to. The walk is the
  arena validator's (`tools/arena_validate.py`), over the stage's own
  ground file (`formats/GROUND.md`, read from your disc by `rebushido
  --dump-stages`), from the game's own starts, every 250 units, climbing
  what a fighter climbs; a solid thing's rectangle, grown by how far it
  reaches (a bush two thirds of its height, a mesh its box turned and
  scaled), must hold none of the points walked. So nothing that looks
  solid stands where the grid would let a fighter walk through it. A
  mesh standing in for a piece is the piece's own shape and is not
  walked against. Grass, flowers and reeds, pushed aside by whoever walks
  through them, may grow anywhere.

Without a disc image and the port built the walk is skipped, and says so.

### Making one

- `python3 tools/port_lights.py dump` writes each stage's mesh from your
  disc (`tmp/lights/dump/`), and `python3 tools/port_dressing.py walk
  --stages N` a picture of where a fighter walks
  (`tmp/dressing/maps/walk_NN.png`, over a map of the stage from above where
  `stage_NN.png` is there) and the box of it: where to put what.
- Write `dressing/stage_NN.txt`, run `check`, and look:
  `python tools/port_dressing.py shots --stages N --times stage,night
  --mods FOLDER` (on Windows, the card's program, a window never shown)
  runs a fight of the CPU on the stage at 2,560 by 1,440, the dressing off
  and on, and keeps pictures of it in `tmp/stages17/`.
- `python tools/port_dressing.py meshes` makes the gravestones the
  program ships (`dressing/meshes/grave*.obj`): a footing stepped twice
  and the stone with its edges chamfered and a low pyramid on top, of the
  proportions of the pieces they stand in for.
- `python3 tools/port_dressing.py rocks` makes the rocks the program
  ships (`dressing/meshes/rock_N.obj`): an icosahedron cut twice, pushed
  out and in by waves of a seed, flattened below, mossed where it faces
  up. Procedural, nothing of anyone's art.

### The dressing the program ships

`src/port/mods/dressing/` (order -100, under yours), one file a stage,
written by hand from the stages' maps, walks and pictures:

| Stage | Its dressing |
|---|---|
| 0 Main Tatara shrine | shafts of light through the two doors in the west wall (the doors' own lights), motes |
| 1 Meikyokan dojo | shafts through the two high windows, motes |
| 2 Shainto HQ | smoke rising from the candles, motes in their light |
| 3 Tatara shrine | embers from the fire baskets; grass where the lawn is |
| 4 Bus stop | dust off the road's earth, leaves along it |
| 5 Lost bridge | the river far below, leaves into the gorge, mist in the gorge at dawn and at night |
| 6 Tatara shrine path | moss and grass along the path, flowers, shrubs on the hillsides, embers from the stone lanterns, fireflies, a ground mist at night and at dawn |
| 7 Bamboo thicket | bamboo leaves falling, dust off the earth; the clumps' seventeen culms round and ridged at their nodes, each at its piece's own column (`culms`), and the cut culm by the path cut on the slant |
| 8 Outside Meikyokan | the dojo's lawn, clover, fireflies |
| 9 Future dump site | rough grass, dust |
| 10 Snake cliff | grass in a strong wind |
| 11 Kimigaura coast | the sea, deep and shallow, foam along the shore and on the waves, mirroring the fighters; rocks in the shallows, sand kicked up |
| 12 Orange tree fields | grass, white flowers, leaves falling, fireflies, a morning mist |
| 13 Castle rooftop | cherry petals past the moon |
| 14 Under lost bridge | the stream painted across the ground as water, running along its bed; sand kicked up, leaves from the bridge, mist along the stream at dawn |
| 15 Terraced fields | grass on the banks, fireflies over the paddies, mist over them at dawn and at night |
| 16 Under railway | dust, leaves |
| 17 Water tower dam | leaves over the concrete |
| 18 Lumberyard | sawdust kicked up |
| 19 Lighthouse | the cliff top's grass in the sea wind, flowers |
| 20 Kabuki stage | embers from the torches |
| 21 Abandoned ship | sea spray thrown up past the rail on all four sides, in a strong sea wind |
| 22 Ferry dock | the sea about the quay, dust off the concrete |
| 23 Shainto gate | embers from the fires, grass in the moss |
| 24 Purple cavern | embers from the candles |
| 25 Abandoned station | grass over the ground, leaves, fireflies |
| 26 Railway | dust off the ballast |
| 27 Abandoned tunnel | grass, dust |
| 28 Abandoned shipyard | dust off the sand |
| 29 Building rooftop | rain blown across it and splashing on the roof, puddles ringed by it, mirroring the towers' lights and the fighters |
| 30 Building parking lot | puddles on the concrete, mirroring the ceiling's lights and the fighters, drips from the ceiling |
| 31 Shainto cemetery | grass between the graves, cherry petals, embers from the candles, fireflies, a ground mist at night and at dawn; three of its gravestones (pieces 3, 6 and 8, bare blocks) as stepped stones with chamfered edges |
| 32 Team battle 1 | three shafts through the band of windows in the south wall, motes |
| 33 Team battle 2 | embers from the fire baskets, sand kicked up |
| 34 Chanbara | three shafts through the paper windows along the north wall (lights of their own, `lights/stage_34.txt`), motes |
| 35, 36 Practice | dust motes over the boards |
| 37 Castle rooftop, link mode | cherry petals over the whole courtyard, gravel kicked up |

**What its assets are.** No picture and no model of anyone's: the
foliage, the water and the particles are drawn by the program's own
shaders from numbers (`src/port/gl/gl_dressing.c`), coloured by the
stage's own textures as the game loaded them; the rocks are made by
`tools/port_dressing.py rocks` from a seed, and the gravestones
(`grave.obj`, `grave_wide.obj`), the ferry dock's bollards
(`bollard.obj`), the orange trees' trunks (`orange_trunk.obj`) and the
bamboo's culms and cut culm (`bamboo_culm_N.obj`, `bamboo_stump.obj`) by
`tools/port_dressing.py meshes` from a few numbers (the last two turned
about the upright from a profile, the trunk's bark furrowed by a sine of
its angle and height); the gulls are the particles' shader's own geometry
and clock; the shafts' light is the stage's light files'. All of it
is the repository's, under its licence (AGPL-3.0, `LICENSE`); nothing was
downloaded, nothing was made by another program, and nothing comes from
the disc or from another game.

**Every stage is dressed**, 37 too: it is the castle rooftop as the
game's link mode fights on it (`select.c`, `SELECT_STAGE_LINK_TO`: link
mode's castle rooftop is stage 37, a courtyard seen from afar), which the
port reaches with the original menus' link mode over the network
(NETPLAY.md, "11. The link cable over UDP") or with `--fight 37` (`castle rooftop
link`). Gulls circle over the ship (21). Pieces standing in by meshes
of their own: the cemetery's three plainest gravestones (31), the ferry
dock's four bollards (22, pieces 0, 1, 2 and 5: blocks of eight sides by
the rail, made round iron with flared heads) and the orange trees' five
trunks (12, pieces 7 to 11: square columns, made round, flared at the
roots and furrowed; the game's own leaves and fruit above them stay).
They were chosen for being plain and many and seen near the fight; the
bamboo thicket's seventeen culms (7) are set at their pieces' own columns
(`culms`), the clumps' leaves kept. What is left: more pieces' meshes (a
piece's mesh has to keep its box's proportions within a quarter).

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

### A stage's own piece

The game shares five pieces among its 38 stages, so a replacement of
`bb2_m1-0` is heard on most of them. A mod's **`music/stage07.ogg`** (or
`.wav`: `stage` and the stage's number in two digits, `stage00` to
`stage37`, the numbers of "Stages" below) is **stage 7's alone**: a
fight on stage 7 hears it, and every other stage keeps its own piece of
the disc's (or a mod's replacement of that piece, as before).

- **When.** A fight whose stage select's row of music is at its first
  place, the stage's own ("Stage's own", or "Game's own" under
  `fight_music = original`), hears the stage's piece: every mode that
  plays the stage's music (versus, and with `fight_music = stage` versus
  the CPU, story and the rest; practice and chanbara keep their own
  pieces). Any other place of the row (L2 and R2: a piece of the game's,
  a mod's piece of its own, None) is heard as it is chosen, so the
  stage's piece is never forced on a player. The row says "Stage's own:
  mod" in gold where the stage under the cursor has one. An arena keeps
  its host's piece, or its own track: an arena is a place of its own.
- **How.** As a mod's piece of its own: the game starts the stage's own
  stream of the disc's as ever, and the stage's piece is heard in its
  place for as long as the stream plays (the loop, the length cut at the
  original's, the volumes and fades: below). The game's state is what it
  is with the disc's piece; `music_mods = off` plays that piece. A stage
  key is never offered in the row as a piece of its own.
- **Online.** Both machines must hear one track. The host's welcome and
  the guest's hello carry each one's stages' pieces as a sum of their
  files' bytes (`port_MusicStageSum`, in the high half of the session's
  music word): where the two are the same, both hear the stages' pieces;
  where they differ (one has a piece the other has not, or another
  file), neither does for that session, and both hear the stage's own of
  the disc's. The log says which (`session: the stages' own pieces of
  music are ...`). It never turns a session away: music is no part of
  the game.

**How it is proved** (headless, `port_game`'s plan, a versus fight on
stage 29, both sizes; `tmp/mods10/music_test.py` of the work, not
kept): `music/stage29.wav`, a tone of 300, 500 and 700 Hz a second each
at 48,000 a second, looped from its second second to the end of its
third: the trace is the plain run's line for line in both sizes, and
both sizes' traces are the same; the sound is the plain run's to the
fight's first sample of music (49.46 s), then 300, 500, 700, 500, 700
Hz second by second until the fight's stream ends (72.69 s), and the
plain run's again after (`tools/port_wav.py`'s spectrogram shows the
steps). The same with `--fight-music stage`. A `stage07.wav`, a stage
not fought, and `stage29.wav` with `--music-mods off`: the sound is the
plain run's byte for byte. `--fight-music bb2_m1-2` (the row moved off
the stage's own) with `stage29.wav`: byte for byte the plain run's with
that piece. In sessions of `tools/port_net.py`'s own check (a fight, a
rematch, the replays): both with a piece for every stage, both say the
pieces are the same and both open `stage29`, 75 state hashes the same;
the host with them and the guest with none, both say they are not the
same and neither opens one, 75 hashes the same. `tests/port/music.c`:
the keys, the stages' pieces, the sum and the hold.

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
own, in the stage pages' look: Select on a stage page opens it, Cross on
one fights on it in place of its host, which the player is never shown
(PLAYING.md, "Kinds of stage"; PORT.md, "The stage select's kinds of
stage"). The page shows the arenas' `name`s on the scroll, six to a
page, and the picture of the one under the cursor in the map's frame:
the file's preview (`preview =` of `arena.txt`, a PNG), or one the
compiler draws of the scene. The frame is about 2.4 times as wide as it
is high: a picture of another shape is shown filling it, its middle
kept (a 4:3 one loses a little of its top and bottom), so the arena is
best in the middle of its preview; with no picture the frame is a card
of the arena's name. The choice
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
picture made 128, 64 or 32 texels square in 16 colours (below). The scene is Y up in metres as
glTF and Blender write it (`scale = 1000` makes a metre 1,000 of the
game's units: the fighters are about 1,700 tall). A face is drawn and met
from its front only. What is drawn and what is met are two meshes on
purpose: steps drawn as steps are met as a ramp (the sample's), a wall
of a barrier may be met where nothing is drawn.

**Textures.** The stage's room is the bottom right quarter of video
memory's pages 10 to 12 (their halves and quarters hold the smaller
copies the renderer reads far off: formats/GROUND.md) and the fog's row
of 20 colour tables. Each material is a texture of **128** by 128 (a whole
quarter), **64** (a quarter of one, the disc's commonest) or **32** (a
sixteenth): the largest of those no larger than its picture's longer
side, or what `texture_size NAME = 32|64|128` of `arena.txt` says. The
larger are placed first, each in the first room of its size free, so the
three pages hold three of 128, twelve of 64, forty-eight of 32 or a mix
(one of 128, four of 64 and fifteen of 32 is twenty), and **20 textures**
at most. A face whose texture points run past its texture **repeats it**:
it is cut at every whole u and v into faces that each span it once (the
game's faces read no texture window), and the compiler says how many
pieces that made.

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
texture_size flagstone = 128   # its texture's size: 32, 64 or 128; if
                                # not given, the picture's (Textures, above)
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
   reaches; 20 colour tables in the fog's row; every picture, a texture
   or one of its smaller copies, in the stage's pages 10 to 13 and over no
   other;
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
  floor taken from under the first start; the textures' places (four of
  64 a page in the first compiler's order, the larger first, 48 of 32 and
  no more), their sizes, a face cut at its repeats into pieces that cover
  it; the **gallery** (`tools/arenas/gallery/make_gallery.py`, made at
  test time: a floor of one face repeating a 128 four times each way,
  brick walls repeating a 64, eighteen panels of 64 and 32, every picture
  worked out by the script) compiled to twenty textures and passed by the
  validator, none of its 60 pictures over another; and the stage prover's
  verdict on the sample held to the sample as compiled now. The sample
  compiles to the same bytes as before textures had sizes.
- The gallery fought: `arena_validate.py --fight` passes it, both sizes
  printing the same 329 lines, the second fighter the winner.
- `tools/port_stage_prove.py --arena` on the sample and the gallery: both
  keep the mesh (PORT.md, "The ground as one mesh"), the gallery's floor
  repeating its flagstones and their hairlines and every panel its own
  picture, with the tiles and with the mesh.
- `tests/port/arenas.c`: an arena's file read and every line that can be
  wrong refused with its reason, its picture found, the mods' arenas with
  one in play and one left out, every answer to the game for the host,
  for the other stages and with no arena, the ground read in place of the
  host's, the arena in play changed to another, to none and back, the
  session's hash.
- `tools/port_stage_select.py` (`make port-stage-select`, a step of the
  gate): the stage select's page driven to a fight on the sample and then
  to its host from the disc, on both sizes (PORT.md, "The stage select's
  kinds of stage"); and seven arenas (`many_arenas`): R1 to the second
  page, down, R1 round to the first, L1 back, Cross on the seventh, which
  is fought.
- `tools/arena_validate.py --fight` on the sample, above; `make
  port-net`'s session check refusing a guest without the host's arena;
  the play tests (`same_as_original` and the port's) and port-rollback
  as they were, with no arena in play.

### What a stage editor needs next

Done since the first arena:

- **More than six arenas**: L1 and R1, or the d-pad past the page's
  side, turn the arenas' page a page of six at a time, round from the
  last to the first, and the page says which of how many (PLAYING.md,
  "Kinds of stage").
- **The stage select's page for the arenas** in the stage pages' own
  look (2026-11-25): the game's page goes on drawing its title, frame
  and scroll and leaves out its map and the stages' names, and the
  arenas are where those were, six names in three rows of two and the
  picture in the map's frame; in versus, link, versus the CPU and
  training, online (where a guest without the host's arenas is told so)
  and in replays (PORT.md, "The stage select's kinds of stage").
- **Textures** of 128, 64 and 32, up to 20 of them, and a face that
  repeats its texture ("Textures", above).
- **The ground as one mesh held to the prover on an arena**:
  `tools/port_stage_prove.py --arena FILE.arena`, and the arena's own
  verdict in `g_arena_proved` (PORT.md, "The ground as one mesh"); the
  sample keeps the mesh by it.
- **An arena's pages from a mod's pictures**, as the disc's stages' are
  ("The scene's textures by page"): it needed nothing new, the arena's
  TIMs being loaded by `tex_LoadStageGroup` as a stage's are. Proved on the
  gallery: `--dump-textures` writes its 20 pages and tables, and a mod of
  a picture for each (a checkerboard of 512 by 512) draws them on the floor,
  walls and panels, the fog's colours over them; the trace is the plain
  run's, and the pictures are the same up to the fight.

Left:

- **More than 32 arenas** (`PORT_ARENAS_MAX`); a picture of each on its
  place, beside the names.
- **Larger arenas**: a ground longer than its host's needs NDATA.INF
  given its new length and room in the arena block (`ARENA_STAGE`); and
  past the grid's 32 by 32 cells, the game's collision and its cells'
  tables are of that size.
- **Pieces**: the compiler writes tiles alone; a piece (a mesh of its own
  placed and turned, `stage_ReadCells`) would let a repeated thing be one
  mesh drawn many times, and could be opened up when it hides a fighter.
  The validator already counts them (60 at most).
- **More than 20 textures, or larger than 128**: the fog's row holds 20
  colour tables, and the stage's pages are the three the disc's stages
  use; a fourth page (13), or textures sharing a table, would be more.
- **A texture window** in place of cutting a face at its repeats: the
  game's tile records name none, so it would be a record of the renderer's
  own; cutting costs primitives (the gallery's floor and walls are 27
  pieces more), which the frame's room bounds as it bounds cut quads.

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
| Dropped | `g_object_dropped_weapon_kinds` (`fight_data.c:401`), `g_object_kinds` (789) | [clan][weapon] | the loose object a defeated fighter's weapon becomes, and how it flies: the blades' bounce 0x800, the broadsword's 0x700, the guns' 0x600 of a landing's speed; a mod's weapon's `dropped =` |
| **Sounds** | `g_resource_weapon_sound_kinds`, `_alt` (`fight_data.c:1003`, 1037) | [character][weapon] | which bank of weapon sounds is loaded (the cut, the clang, the lock, the shot, the dropped weapon's landing); the swings are the motions' frame events; a mod's weapon's `sounds =` |
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
sounds = broadsword      # (format 3) the weapon whose sounds the character
                         # makes with it: a blade's of a blade, a gun's of a gun
dropped = broadsword     # (format 3) the weapon whose dropped weapon it
                         # becomes when its fighter is cut down
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

### A move joined to another file's motion

A move plays a motion of its own file: the motion is a number in the
file's table, its marks are in the file's marks, and its frames are in
the file's stream, packed with codes of the file's own
([`formats/BBM.md`](formats/BBM.md)). So a move cannot name another
file's motion; **`tools/bbm_join.py`** copies the motion in:

```
python tools/bbm_join.py moves disc/MOTION/K123.BBM
python tools/bbm_join.py join disc/MOTION/U123.BBM disc/MOTION/K123.BBM \
    --move 2100 --from-move 2098 --move 2046 --from-move 2090 -o kanabo.bbm
```

`moves` lists a file's moves by their place (in halfwords, as the file
names them), the motion each plays and its frames, the motion's kind and
length, and what leads to the move: the header's name for it (standing
in a stance, a row of the per-stance moves) or the command that leads to
it from standing in a stance. The same command leads to the moves two
files have for the same thing: in U123.BBM and K123.BBM command `00d5`
from standing in the first stance leads to the moves at 2100 and 2098,
the stance's first blow, and `00d6` to 2046 and 2090.

`join` decodes the other file's motion, adds it after the file's
motions, its marks after the file's marks, its frames after the file's
(in whole groups of eight, from the one its first frame is in), and
points the move at it: the move plays the other move's frames of it and
takes commands to the other move's last frame for them; everything else
of the move (what follows it, its commands, its flags) and of the file
(the other moves, the CPU's scripts) stays. The whole stream is packed
again with the codes that hold both files' frames, and decoded again
before anything is written: every frame of the file and of the motion is
the one it was, or the tool stops and writes nothing. (Packed so, the
disc's own files come back byte for byte: `bbm_join.py check`.)

The joined file is larger than the disc's: a mod uses it as a weapon's
own motions (`motions = KEY`, below), whose room is the disc's largest
set's file, not in place of the disc's file of its name, which must not
grow.

**The kanabo's**: `python3 tools/weapon_kanabo.py --motions disc/MOTION
--mod DIR` makes, from your disc, the club's own motions into a copy of
the mod at DIR: U123.BBM, the katana's, with the first stance's two blows
(`00d5`, `00d6`) the broadsword's of K123.BBM (`motions/kanabo.bbm`), and
`motions = kanabo` in its weapon's file. Nothing of the disc is in the
mod the program ships, which keeps the katana's motions.

### A mod's weapon on the select screens

Every new slot (a weapon file with `replaces = no`) is on the **weapon
select**, on its base: with the cursor on the katana, **L1** and **R1**
go round the katana's variants, the game's own katana first and then
each mod's weapon on the katana in the mods' order, and Cross picks
the one shown. A line under the weapon row says which (the mod's
weapon's `name` in gold, or "the game's own" and how many of the mods'
there are); the weapon row's own picture and name stay the base's.
Moving the cursor to another weapon goes back to the game's own. Each
side chooses on its own pad, as the row is chosen. In every mode whose
select screen has the weapon row (versus, versus the CPU, story,
practice), the fighter picked keeps it until the select screen loads
again, through a versus the CPU run and every rematch. A `--weapon`
of the run still gives a fighter a new slot without the select screen;
a pick on the select screen comes before it.

What the select screen holds of it is **the game's state**
(`g_port_weapon_pick`, `src/port/weapon_pick.c`, a state file): each
side's variant under its cursor and each fighter's pick, saved, put back
and hashed with the rest, so a rollback puts it back, the two machines
of a session pick the same with the same presses, and a replay picks it
again. The weapons themselves are the same on both machines or the
session is refused (Netplay, below).

**Its own model.** `model = KEY` (format 2) names a mod's
`models/KEY.glb`, which the remastered picture draws in the fighter's
hand in place of the weapon model, in the format of the game's own
weapon models (PORT.md, "The fighters' own models", "The weapons"). The
fighter is the one whose hand `weapon_Queue` put the weapon in
(`port_FighterHeldModel`). The console's picture,
Graphics: Original, draws `models =`'s numbers, the base's if it does not
say; so does two on two. A defeated fighter's weapon lying loose is
his mod's weapon's model too, as it falls and where it lies, and in the
replay of the kill (`port_RemasterNoteLooseWeapon`: `object_QueueAll`
and the replay's draws say whose it is). A model
that no mod has is said in the log, and the base's is drawn.

**Its own motions.** `motions = KEY` (format 2) names a mod's
`motions/KEY.bbm`, a whole motion file in the disc's format
([`formats/BBM.md`](formats/BBM.md)): every character fighting with the
weapon loads it in place of his set's file. Its motion set is a number of
its own (`port_WeaponMotionSet`): the weapon class 8 and on in the top
digit (one for each weapon with motions, eight at most) and the base's
stance kinds below, so the game's two slots of motion files tell it
from the disc's sets and two fighters share it only when both have it;
`motion_LoadFighterFiles` reads it from the mod
(`port_WeaponReadMotions`) where it would read a set's file off the disc,
and the two special stances follow the base's class
(`port_WeaponMotionClass`). A file larger than the game's room for one
(the disc's largest set's file) or missing leaves the weapon out, with the
reason in the log. The log says the set the base would have had,
`weapons: the fighter in place 1 fights with kanabo, whose base's motion
set is 0321`: its file on the disc (`U123.BBM`) is where a weapon's own
motions start from. Everything else of the weapon is still the base's:
the CPU's scripts (in the motion file: so a weapon's own file brings its
own) and the scabbard; its sounds and its dropped weapon are the base's
unless it says (below).

**Its sounds and its dropped weapon.** `sounds = WEAPON` (format 3): the
character makes the sounds he makes with that weapon, the cut, the clang,
the lock and the landing of his dropped weapon, from the game's tables
(`g_resource_weapon_sound_kinds`, and `_alt` in team battles and
practice), by his row: a kanabo of Tatsumi's on `sounds = broadsword`
loads Tatsumi's broadsword bank. A blade's are a blade's and a gun's a
gun's (the team battles' table has the blades' alone). `dropped = WEAPON`
(format 3): his weapon falls as that weapon's loose object, of his clan
(`g_object_dropped_weapon_kinds`): how it bounces, the broadsword's less
than the katana's. Both are numbers of the game's: `port_WeaponSoundKind`
(`fight.c`, `story.c`, where `resource_SetWeaponSounds` is told) and
`port_WeaponDropped` (`fighter.c`, where `object_DropWeapon` is called).
A weapon's own recorded sounds are not done (below).

**A file of format 3**: `sounds =` and `dropped =` are its, refused in
an earlier format ("sounds and dropped are format 3's"); a program before
them refuses a format 3 file with "a later ReBushido is needed".

**A file of format 2**: `model =` and `motions =` are its; a file that
says them with `format = 1` is refused ("model and motions are format
2's"), and a program before them refuses a format 2 file with "a later
ReBushido is needed".

### The kanabo

The program ships one weapon as an example, **`mods/kanabo/`**
(`src/port/mods/kanabo/`), left out until its `mod.txt` says `enabled =
yes`: an iron-studded war club on the katana, longer (its tip at 1,100
to the katana's 950), a little slower (0.95) and heavier in a clash (1.4), with
the katana's motions, the broadsword's heavier sounds and the
broadsword's dropped weapon, which bounces less (`sounds = broadsword`,
`dropped = broadsword`). Its model, `models/kanabo.glb`, is made by
`tools/weapon_kanabo.py` from numbers alone: an octagonal shaft
widening to the cap, rows of iron studs, a corded grip, a collar and a
pommel ring, its texture drawn by the script; nothing of the disc's or of
any other game's, and no AI-made or bought mesh.

### What is not done yet

- **A place of its own in the grid**: a mod's weapon is a variant of its
  base on the base's place, its picture and name the base's; a seventh
  place needs the select screen's file's pictures and names
  (`select_data.c`, `SelectFile.weapon_pixels`) and a `weapon_id` of its
  own everywhere the game indexes by weapon (the tables above, the
  sounds', the CPU's, the dropped object's).
- **A weapon's own recorded sounds**: `sounds =` takes one of the
  game's banks; a bank of the mod's own (a VAB of its own samples, laid
  out as the game's weapon banks are, read in place of the pair's file,
  `resource_PortReadWeaponSounds`) is not done. The CPU's own weapons in
  versus the CPU (`g_vscpu_weapons`) and team battle's are the game's.
- **Its own model in two on two**.

### Netplay

The weapons' and the motions' files, like the stages', are hashed into
the session's set-up with every `--weapon` given (`port_WeaponsSessionHash`,
`src/port/session.c`): a guest whose gameplay mods differ is turned
away, "the other player's mods that change how the game plays ... are
not the same" ([`NETPLAY.md`](NETPLAY.md), section 13). Both players
give the same `--weapon`s, which say both places'. What is read is
none of the game's state (`config/port_state.txt`): the table is made
once and only read; which weapon a fighter has is worked out each time
from his place and his `weapon_id` and the weapon select's pick, which
are the game's (`g_port_weapon_pick`), so a rollback puts it back with
the rest. A motion file is the game's state where the game loaded it, as
the disc's is; a weapon's own motion file, read from the mod, likewise.
A weapon's own model is hashed into the session's set-up too: both
players must have the same mod, every file of it, or the guest is turned
away with the reason on the online page.

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
- **The kanabo on the weapon select** (port_game's plan with player
  one's R1 on the weapon row before he picks; the mod copied with
  `enabled = yes`), both sizes, each size's two traces the same: with no
  mod R1 does nothing (the plain run's trace), and with the mod not
  picked neither; picked (the log: `picks kanabo`), the fight is the
  plain run's to its 56th frame and then goes otherwise, to a kill. Its
  own motions as a copy of the disc's U123.BBM (the file of the set the
  log names): the same fight line for line, but the load 26 frames
  shorter (the file is read from the mod, not through the drive); as
  S123.BBM, the long sword's: another fight from its 55th line, to a kill.
  Each fight's replay plays back with every hash the one recorded, and
  is refused without the mod ("recorded with other gameplay mods"). The
  rollback self-test passes with it picked (111 rounds, 888 frames, both
  sizes). A session of `tools/port_net.py`'s check with the host's R1:
  both machines say the first fighter picks the kanabo and fights with
  it, 78 state hashes the same, both sizes, and both machines' replays
  play back with the mod; a guest without the mod is turned away before
  the game, and the online page says why ("The other player's gameplay
  mods (weapons, motions, stages, arenas) are not the same as yours").
  The window's own pixels of the weapon row's line and the club in the
  hand: `tmp/mods10/` of the work.
- **Its sounds and dropped weapon** (format 3; the kanabo picked as
  above, its file as format 2 had it and as it ships, `sounds =
  broadsword` and `dropped = broadsword`), both sizes: the log says
  `kanabo: the broadsword's sounds, dropped as the broadsword is`; the
  trace is format 2's but for the sound lines, from the first clash
  (line 1,657, `sound key 3046`: another sample's address), and the
  fight is the same to its kill (the kanabo's fighter's); the sound kept
  (`--wav`) differs from 51.9 to 78.4 seconds; each size's trace and
  sound the other's. The remastered picture draws the club lying loose
  where the katana lay. Both are in `tmp/modsleft/listen/` of the work.
- **A move joined to another file's motion**: the kanabo's own motions
  made by `tools/weapon_kanabo.py --motions` (U123.BBM with K123.BBM's two
  first blows, 257,640 bytes for 252,816; its stream, packed again by
  `bbm_join.py check`, the same); the game's own decoder
  (`tests/motion_decode.c`, with `BB2_DISC` at a folder of the file) and
  the format's decoder in Python agree on all its 4,190 frames. The
  kanabo picked with it (both sizes, each size's trace the other's): its
  fighter plays motion 166, the broadsword's first blow, on the first
  press (`p1_motion` of a play test's `[state]` at Fighter's
  `motion_index`, the PlayStation's offset 0x5C), then 167; the fight is
  another from the load on (26 frames shorter, the file read from the
  mod). The rollback self-test passes with it picked (94 rounds, 752
  frames, both sizes). The club swung in the broadsword's blow, in the
  remastered picture: `tmp/modsleft/listen/` of the work.
  `tests/rebushido/bbm_join.py`: two made-up files joined, read by the
  format's decoder; the disc's files packed again byte for byte.
- `tests/port/weapons.c`: a weapon's file read, every line that can be
  wrong refused with its reason (the sounds' and the dropped weapon's
  among them), the factors, a folder of mods with a
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
| `tools/bbm_join.py`, `tests/rebushido/bbm_join.py` | a move joined to another file's motion: the frames decoded, the motion, its marks and frames copied in, the stream packed again and checked |
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

- **The weapons' textures by page**, as the fighters' are ("A fighter's
  pages"): `resource_RequestWeapon` loads their tables into a block of
  their own, which would need its TIMs noted as a model's are.
- **More of the lettering in a font**: what no tour of the dumps
  reached (the setup mode's own pages, the slash mode's, the results of
  the other modes, the endings) is the art mod's, made larger; each is a
  block of `tools/port_ui.py`'s table away, measured from a dump.
- **FLAC**: a third kind of music file, with a decoder of whole numbers
  (dr_flac, public domain, is one).
- Stages and arenas: "What a stage editor needs next", above.
- Weapons and motions: "What is not done yet", above.
