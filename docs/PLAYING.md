# Playing the port

The native port is ReBushido as a program for a PC: the game's own code,
built for Windows or Linux, in a window, with sound, played with the
keyboard or with controllers. How it is made and what is proved about
it is in [`PORT.md`](PORT.md), "The window".

**Nobody has played it yet.** It has been built and run only with
nothing shown and nothing sounded, and checked by numbers: it plays the
same fights, draws the same pictures and makes the same sound as the
build that is held to the emulator. Whether the window looks right and
the sound is right in a room is for the first person who starts it to
say.

## What you need

- **Your own image of the Bushido Blade 2 disc** (the North American
  one, SLUS-00663), as a `.cue` and its `.bin`. The game is read from
  it; nothing of the game is in this repository or in the program.
- **Windows:** a 64-bit Windows 10 or 11. **Linux:** an x86-64 system
  with SDL 2 (`libsdl2-2.0-0` on Ubuntu).
- **The program.** For Windows it comes as a package,
  `ReBushido-PC-<version>-<commit>.zip`: unpack it anywhere you can
  write to and read its `README.txt`. It holds the program, `SDL2.dll`,
  the shipped mods and the licences, and nothing of the game
  ([`BUILD.md`](../BUILD.md), "The port's package", makes one). Or it is
  built from this repository ([`BUILD.md`](../BUILD.md), "The port with
  a window"), into `build/port-sdl/`:

  | | |
  |---|---|
  | Windows | `windows/rebushido.exe`, with `SDL2.dll` and `rebushido.exe.sym` beside it |
  | Linux | `linux/rebushido`, with `rebushido.sym` beside it |

  The `.sym` file is the program's names: with it a crash says where it
  was.

## Starting it

**Windows.** Put your `.cue` and `.bin` in the folder with
`rebushido.exe` and double-click `rebushido.exe`. A console window opens
with it and says which keys are which. If there is no image beside the
program, it asks for one with a file dialog. Or, from a terminal:

```
rebushido.exe --image "C:\Games\Bushido Blade 2 (USA).cue"
```

The first time it starts with your disc, the package's program first
makes the remastered art, the stages' textures and the fighters from it,
in about three and a half minutes, which Escape skips ("The first
start", under "Mods" below).

**Linux.**

```
build/port-sdl/linux/rebushido --image "Bushido Blade 2 (USA).cue"
```

It starts filling the screen; F11 or Alt+Enter, or `--windowed`, puts
it in a window. `rebushido --help` lists the rest.

**Straight into a fight**, with no opening or menu:

```
rebushido.exe --fight tatara-shrine kannuki mikado:nodachi
```

The stage, then the two fighters, by the game's names in any case and
with or without spaces or dashes (`"Tatara Shrine"`, `tatara-shrine`,
`STAGE_TATARA_SHRINE` and `3` are one stage); a fighter's weapon after a
colon, a katana if none. The second fighter is the CPU's; `--fight-2p`
gives it to the second pad. A name the game does not have lists the
ones it does. The fight starts with the settings the game starts with:
the memory card is not read on the way. After it, the game's own menu
comes up and the game goes on as ever.

## The keys

| | Player one | Player two | A controller |
|---|---|---|---|
| Up, Down, Left, Right | the arrow keys | I, K, J, L | the direction pad or the left stick |
| Cross, Circle | Z, X | G, H | A, B |
| Square, Triangle | A, S | T, Y | X, Y |
| L1, R1 | Q, W | R, U | the shoulder buttons |
| L2, R2 | 1, 2 | 5, 6 | the triggers |
| Start, Select | Enter, Right Shift | P, O | Start, Back |

On a controller the four buttons are the PlayStation's by where they
are: Cross is the lowest. The first controller plugged in is player
one's and the second player two's; they can be plugged in and pulled
out while the game runs.

**F1** opens the settings menu (below), and so does a controller's
guide button. **Escape** pauses a fight (it is Start there), closes
the pause menu, skips a story's cutscene, and goes back from any menu
or screen of the game (it is Triangle there); it never ends the
program. **Closing the window** does, and so does **Quit ReBushido** at
the foot of the settings menu. **F11** or
**Alt+Enter** fills the screen and comes back. **F3** shows and hides
the counter in the corner: `59.8/30.0 100%` is the pictures shown a
second, the game's own frames a second (30 in a fight and 60 in the
main menu are full speed), and the speed against the console's.

**F9 keeps a report of a problem**: press it when something looked or
sounded wrong, or the game stuttered, and a folder
`logs/report-YYYYMMDD-HHMMSS-N/` beside the program holds the picture
on the screen, where the time went in the last ten seconds (the
processor's work and the graphics card's, part by part, and when each
picture went up), the session's log, your `rebushido.cfg`, and what the
PC is (the card and its driver, the screen, the sound card). Send the
folder: it says where the time goes on your PC. The log says where it
went.

**F12 keeps a picture of the window**, exactly the pixels on the screen
at that moment, as a PNG in a `screenshots/` folder beside the program
(`shot-20261115-162200-000.png`: the date, the time of day, and a
number for pictures taken in the same second). The session's log says
where each one went, and whether it was the game's own frame or one of
the pictures drawn between two of them. Nothing is scaled or cut: a
window of 2,560 by 1,369 gives a picture of 2,560 by 1,369, with the
black bands at its sides or top if the picture does not fill it.

With the **Frame rate** setting at `display`, the window shows more
pictures a second than the game has frames (docs/PORT.md, "More
pictures a second"), and F12 keeps the last picture of the blank it is
pressed in -- which is usually one of those, and is what is on the
screen when you press it. That is the picture to send when something
looks wrong only while the game is moving.

## The top menu

The program goes straight to the game's opening: ReBushido's options
page, which the PlayStation disc shows first, is not shown, and its
setting, Bug fixes, is in the settings menu with the rest. A memory card
with the game's save on it is loaded without asking. Then the top menu,
in the game's own look, with five rows:

| The row | What it is |
|---|---|
| **Singleplayer** | a page of its own: **Story**, **Vs. CPU**, **Slash** (once it is won, as in the original) and **Group Battle**, and **Back** |
| **Versus** | two players, the second on the second pad |
| **Training** | the game's training, either kind of practice |
| **Online Play** | the settings menu's *Play online* page: host or join a game with another PC ("Playing online"); Back on it comes back here |
| **Options** | the settings menu (below), in place of the game's own options page |

Up and down choose, Cross takes the row, Triangle goes back (from the
top page, to the opening, as the game's does). Every fight is seen from
the side: the game's first person's view (POV) is not offered, and the
link cable's mode is Online Play. What the game's options page had --
Difficulty, Music and Monitor -- are settings of the settings menu's
first group, *The game*; its Memory Card and Key Configuration are not
needed (the cards are files kept beside the program, written whenever
the game saves; the keys and buttons are in *Controls*).

`menu = original` in `rebushido.cfg` (or *Menus* in *The game*) puts
the game's own menus back, the options page at boot included, the next
time the program starts. A test's run (`--plan`, `--frames`) has the
original's unless `--menu rebushido` says otherwise.

## The settings menu

Every setting of `rebushido.cfg` below and the buttons of both players
are in a menu of the program's own, drawn over the game at the window's
own resolution. **Options** on the top menu opens it, and **F1** or a
controller's **guide button** anywhere; **Select** opens it on the
screens where the game has no use for Select: the top menu, the options
page at boot (`menu = original`) and the fight's pause menu (Start,
then Select).

Its first page is a row for each group of settings -- **The game**,
**Display**, **The remastered picture**, **Controls** and **Mods** --
then **Play online**, **Back to the game** and **Quit ReBushido**. A
group's row opens its page: its settings, each with its value, a line
under them about the one the cursor is on, and beside the cursor's value
the one built in (`default`, or `default: Hard`); a value that is not
the one built in is gold.

| | The keyboard | A controller |
|---|---|---|
| choose a line, change its value | the arrow keys | the direction pad or the left stick |
| open (a player's buttons), bind | Enter | Cross (the lowest of the four) |
| back, or close | Escape, Backspace | Circle, Triangle |
| a button back to the layout's own | Delete | Square |
| close | F1 | Start, Back, guide |

The menu's own keys are always these, whatever the players' are. Its
last line, **Quit ReBushido**, keeps the settings and ends the program.

**The game is stopped while it is open**, at the end of the vertical
blank where it has just shown a frame, and goes on from there when it
closes as if no time had passed: nothing of the game runs in between,
and a key or a button still held as the menu closes is not given to the
game until it is let go. The sound runs out and comes back.

What changes at once: the full screen, the picture's size and its
pixels, waiting for the display, the counter and its corner, quick
loading, run-ahead and low latency, the stage select in every mode and
unlocking everything (each at the next screen that reads it), the bug
fixes, the game's Difficulty, Music and Monitor, Graphics
(as F2), the resolution, the ground as one mesh, the shadows, smooth
lighting, the effects, the dead zone, and the buttons. What waits for the program to start again is said
on its line, `(after a restart)`: antialiasing, the draw distance, the
shape, the frame rate, the menus (`menu`), the English top menu, the sharp menus and the
mods.

**Player 1's buttons** and **Player 2's buttons** list each of the
PlayStation's buttons with the keys and the controller's buttons that
press it. Enter or Cross on one waits for a key, a controller's button,
or a stick or a trigger pushed far, and binds it, as a `pN.BUTTON` line
of the file would (below); Escape leaves it as it was, Delete gives it
back the layout's own.

**Bug fixes**, in *The game*, is the game's own setting, the one the
options page at boot has on the PlayStation: the menu changes it at once
and keeps it in `rebushido.cfg`, and the program starts with it. With
`menu = original` the options page at boot sets it from the memory card
instead. **Difficulty** (Hard, which the game starts with, or Easy),
**Music** and **Monitor** (Color, or black and white) are the game's
own options, taken at the next frame of the top menu and written into
the game's save as its options page did.

**The menu writes `rebushido.cfg`** as it closes, if anything was
changed: the file beside the program, or the one `--config` named, made
if it is not there. Only the lines of what was changed are touched -- a
value is changed where its line is, with the comment after it kept in
its column, a line it does not have is added at the end, a button bound
again has its lines of that kind replaced -- and everything else of the
file, comments and lines it does not know included, is left as you
wrote it.

## Changing the keys and the picture

Make a text file `rebushido.cfg` beside the program, or let the
settings menu make it. A line is
`name = value`; what follows a `#` is a comment. For example:

```
scale = integer          # fit (as large as fits), integer, or stretch
filter = linear          # nearest (each pixel a block) or linear
fullscreen = off         # start in a window (on, filling the screen, is built in)
fps = off                # no counter in the corner (F3 brings it back)
run_ahead = 2            # 0, 1 or 2 frames run ahead (1 is the default)
low_latency = off        # let the driver queue pictures (on is the default)
late_input = off         # read the keys only as a frame begins (on is the default)
cpu_prefer = off         # never ask for the larger-cache cores (auto is the default)
graphics = remastered    # original (the console's picture) or remastered
resolution = 3           # how many times the console's; 0 = the window's
draw_distance = far      # original, far, or max: how much of the stage
antialiasing = 2         # off, 2 or 4: the scene's edges smoothed
aspect = 4:3             # the console's shape; 16:9 is the default
stage_mesh = off         # the game's own tiles; on is the default
stage_props_mesh = off   # railings and walls from the game's own faces
shadows = map            # original (the game's black ones), soft, cast or map
lights = on              # a stage's torches and lanterns, blades meeting
reflections = on         # polished floors reflect the fighters and the sky
ambient_occlusion = on   # the ground darkened softly in its corners
bloom = on               # torches, lamps and a clash's flash glow
tonemap = on             # bright light rolls off instead of clipping
smooth_light = on        # the game's lights worked out at each pixel
effects = smooth         # original (the default), exact or smooth
frame_rate = display     # display (the default), original, or a number
menu = original          # the game's own menus and the options page at boot
bugfixes = on            # the original's bugs fixed (off is the default)
difficulty = easy        # the CPU weaker (hard, the game's own default)
music = off              # no music, the sounds alone
monitor = bw             # the game's picture in black and white
mods = off               # none of the mods in mods/ (on is the default)
disc_mods = off          # never make the art, stages and fighters from the disc
english = off            # the top menu in Japanese (on is the default)
ui = off                 # the menus' own lettering (on is the default)
music_mods = off         # the game's own music (on is the default)
quick_loading = off      # every blank of a load at the console's pace
stage_select_everywhere = off  # versus the CPU and training pick their own stage
unlock_all = on          # every hidden fighter and slash mode, the save untouched
p1.cross = key Space     # Space is player one's Cross, and Z no longer
p1.circle = key Left Ctrl
p2.start = pad a         # on player two's controller, A is Start
p1.select = none         # nothing presses it
```

`rebushido --help` prints the whole file as it is when you have none,
to copy lines from, and says how keys and controller buttons are named.
A line the program cannot use is named in the console window as it
starts.

### Remapping the keys and buttons

Each of the PlayStation's buttons of each player is a line `pN.BUTTON
= ...`: `p1` or `p2`, then `up`, `down`, `left`, `right`, `cross`,
`circle`, `square`, `triangle`, `l1`, `r1`, `l2`, `r2`, `start` or
`select`. What follows the `=` is one of:

| | |
|---|---|
| `key NAME` | a key of the keyboard, as SDL names it: `Z`, `Space`, `Return`, `Left Shift`, `Keypad 4` |
| `pad NAME` | a button of that player's controller: `a`, `b`, `x`, `y`, `back`, `start`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright` |
| `pad +AXIS`, `pad -AXIS` | a stick or a trigger of that player's controller pushed one way: `leftx`, `lefty`, `rightx`, `righty`, `lefttrigger`, `righttrigger` (how far is `deadzone`) |
| `none` | nothing presses it |

A key line takes that button's built-in keys away and a `pad` line its
built-in controller buttons; either takes its own key or button away from
whatever that pressed as built in. A second line for the same button adds
a second key. Several buttons joined by `+` are pressed together by one
key or button:

```
p1.cross = key Space         # Space is Cross; Z is nothing now
p1.cross = key Keypad 0      # ... and so is Keypad 0
p2.l1+r1 = key Right Ctrl    # one key for L1 and R1 at once
p1.r2 = pad +righttrigger    # player one's right trigger is R2
p2.select = none
```

The file starts from a layout, `controls = original`, which is the one
in the table above and the only one so far; a `controls` line goes before
the file's own `pN` lines. Each player's keys are their own, and so is
each controller: the first plugged in is player one's.

`filter` is about **the console's own picture**, whose pixels are
blocks. The remastered picture is drawn at as many times the console's
resolution as the window asks for, rounded up, so it is almost always
larger than the window and is always smoothed where it is made smaller,
whatever `filter` says: taking one pixel of it for each pixel of the
window would throw the others away and leave the distance crawling.

## The picture: Original and Remastered

**Graphics**, in the settings menu's *The remastered picture*, is
**Original** or **Remastered**. Original is the PlayStation's own
picture, pixel for pixel. Remastered
draws the scene again with the graphics card, at the window's
resolution: the corners where the camera really put them instead of on
whole pixels, textures in perspective instead of sliding, true colours
instead of the chip's five bits and its dither, and the menus, the
fonts and the HUD crisp at the window's size (`PORT.md`,
"Remastered").

**F2 changes picture while the game runs**, at once and as often as you
like, and `--graphics original` or `--graphics remastered` starts it
either way; with neither, the window follows `graphics` (`follow`
follows the options page at boot, with `menu = original`). The
window needs a graphics card that does OpenGL 3.3, which anything since
about 2010 does. If it cannot get one it says so in the session's log
and draws the console's picture instead, and F2 then does nothing.

**The settings of the remastered picture** are a run's: they are read
once as it starts, and changing one means starting it again. Each does
nothing at all while Graphics is Original.

| | | |
|---|---|---|
| `resolution` | `0`, or `1` and up | How many times the console's resolution the scene is drawn at (`--resolution N`). `0`, which is the default, takes the window's own size, so the picture is as sharp as the window is large. |
| `draw_distance` | `original`, `far`, `max` | How much of the stage is drawn, and how far out its fog is placed. The console draws a disc of about 26,000 units about the camera and its fog ends there, so the ground fades into the distance; `far` draws about twice as much, and `max` draws the stage to its own far edge with the fog pushed out four times as far. The fog's colours are not touched, so a night stage stays as dark as it was. **`max` with nothing said**: it is the only one of the three that draws the same cells whichever way the camera looks, so nothing of the stage comes and goes as the camera turns. |
| `antialiasing` | `off`, `2`, `4` | The scene drawn that many times over each way and averaged down, which smooths its edges. The HUD, the fonts and the menus are left exactly as crisp as they were. It costs fill rate and nothing else: four times over is sixteen times the pixels. `2` with nothing said. |
| `aspect` | `4:3`, `16:9`, any `W:H` | The shape of the picture. A wider one is a **wider view of the same scene**, not a stretched picture: the camera keeps its height and gains width. Use it with `draw_distance max`, which fills the sides. `16:9` with nothing said; the fight's counters, clocks and points are drawn at the new edges, while the menus, the select screens and the loading pictures keep the console's shape in the middle ([`PORT.md`](PORT.md), "A wider picture"). |
| `shadows` | `original`, `soft`, a number, `cast`, `cast:N`, `map` | What a fighter's shadow is. **`map`, with nothing said**, is the stage's sun casting real shadows: everything with a place in the world -- the fighters, their swords, the stage's walls, pillars, fences and terraces -- casts onto the ground, the walls, the steps and the fighters themselves, with an edge as soft as the distance from what casts it makes it. In a shadow a surface loses the sun's light and keeps the rest of the stage's, so a shadow takes as much of a sunlit floor as `cast` does (68 hundredths) and the stage keeps its colours. A stage whose ground is the game's own tiles (`stage_mesh = off`) has `cast`'s shadows instead. The rest of this row is the older choices. `original`, the default, is the game's own: the fighter's shape flattened onto the ground and painted **black**, which hides the ground under it. `soft` **darkens** the ground under that same shape instead, by 55 hundredths, so the ground keeps its texture through the shadow; the edge is softened by about three pixels of the picture, two shadows that cross are no darker than one, and **a fighter standing in a shadow is never darkened** -- its own feet and legs are exactly as lit as they are with the setting off. A number says how much of the ground a shadow takes, in hundredths, for trying another. `cast` does the same with a shadow cast from the fighter's **own body** -- every polygon of him, his sword and his scabbard, flattened onto the ground along the stage's sun -- in place of the game's coarse shadow shape, so it has his hands and his blade and does not twitch while he stands still; it takes 68 hundredths, and `cast:N` takes N. **`original` with nothing said**: nobody has looked at the soft ones on a screen yet ([`PORT.md`](PORT.md), "The shadows"). |
| `stage_mesh` | `on`, `off` | The stage's ground drawn from **one mesh of the whole stage**, built when the stage loads, instead of from the tiles the console builds again every frame. Nothing of the ground comes and goes as the camera turns, its detail never steps, the fog runs on instead of stepping in bands across a long floor, a piece of ground between the camera and a fighter veils him again, and a picture drawn between two of the game's frames has only to move the camera. **`on` with nothing said.** Every one of the game's 38 stages was measured against the console's own tiles, and a stage the measurements did not hold would keep the tiles whatever the setting says; the log says which path each stage took ([`PORT.md`](PORT.md), "The ground as one mesh"). |
| `stage_props_mesh` | `on`, `off` | What **stands on the ground** -- the railings, the fences, the walls, the posts, the roofs -- drawn from meshes of its own, built when the stage loads from the stage's own pieces and drawn with the ground's mesh and the same camera, instead of from the faces the console projects again every frame. A railing then stands still where the camera is still, its corners no longer wobble by a pixel, and in a picture drawn between two of the game's frames it is moved by the camera alone, so nothing of it is held and then jumps. A piece the game moves or changes -- a bamboo cut down, a sign that falls -- and a face that mixes with what is behind it stay the game's own. It needs Graphics Remastered and `stage_mesh`, and changes nothing the game computes. **`on` with nothing said**; a stage the measurements did not hold would keep the game's own faces, and the log says which path each stage's pieces took ([`PORT.md`](PORT.md), "The stage's pieces as meshes"). |
| `lights` | `on`, `off` | The lights of a stage's own and of a fight -- torches, lanterns, the flash where blades meet (`stage_lights`, `effect_lights`) -- lighting the ground, the walls and the fighters at each pixel, the strongest four casting shadows of their own. `off` leaves the stage's own lighting alone. **On with nothing said.** |
| `reflections` | `on`, `off` | **Polished floors reflect**: the boards of the halls and dojos, the kabuki stage, the castle's glazed tiles, the parking lot's sealed floor, wet concrete at night -- each stage its own strength and roughness, only what lies level, nothing on earth, grass, sand or stone. They show the fighters, the pillars and the railings standing on them, and the sky where the picture shows it. Worked out on the picture itself (screen-space), so something off the picture is not reflected. Graphics Remastered only; changes nothing the game computes. **`on` with nothing said.** |
| `ambient_occlusion` | `on`, `off` | **The ground darkened softly** where it meets a wall, a step or a fighter's feet, and only the share of a stage's light that comes from all about, not the sun's: a sunlit stage takes a little, a night stage more. Graphics Remastered only. **`on` with nothing said.** |
| `bloom` | `on`, `off` | **Lights glow**: a torch, a lamp, the flash where blades meet and the sparks, each a soft halo of its own colour where nothing stands in front of it. It is the lights' own glow, not every bright pixel's, so a white wall or the HUD never blooms; with `lights = off` there is none. Graphics Remastered only. **`on` with nothing said.** |
| `tonemap` | `on`, `off` | Where the lights of a stage or a fight make a surface **brighter than white, it rolls off towards white** and keeps its colour, instead of clipping to a flat patch. The stage's own lighting is untouched, so with the lights off the picture is exactly what it was. Graphics Remastered only. **`on` with nothing said.** |
| `smooth_light` | `off`, `on` | The game's **own lights** -- each stage's three lights and its colour of the dark, the same ones the console uses -- worked out **at every pixel** of the ground and of the fighters' own models, instead of once a face of the ground and once a corner of a model. A slope of the ground is shaded as a slope instead of in flat facets, and a wall still meets the floor at an edge; a flat floor keeps exactly the colour it had, so a stage keeps its colours and its mood. **`off` with nothing said**: the owner has not looked at it yet ([`PORT.md`](PORT.md), "Lighting"). |
| `effects` | `original`, `exact`, `smooth` | How blood, sparks, flashes, dust, splashes, footprints, the stage's flames and the surf are drawn. `original`, the default, is where the console put them, on whole pixels of its own screen. `exact` draws each where the camera's projection really put it, its size without the console's rounding, and moves it smoothly between two frames with `frame_rate`; `smooth` also reads its texture smoothed, so a spark or a drop of blood has a soft round edge instead of the console's square texels. Their colours, their timing and how they mix with what is behind them are the console's either way, and none is ever hidden behind the ground, as on the console. **`original` with nothing said** until the owner has looked ([`PORT.md`](PORT.md), "The effects"). |
| `sky` | `night`, `original` | What is above a stage that is open to the sky. The console has no sky: a fight's picture begins as one flat colour, the stage's colour of the distance, black or a dark blue on every stage, and that is what shows wherever the stage does not cover it. `night` paints a **night sky of the port's own** there: a gradient that is the stage's own colour of the distance at the horizon, so that the ground's fog runs into it with no seam, lifting a little towards the stage's own ambient colour; stars; a few thin clouds; and a moon where the stage's light comes from. It turns with the camera and never moves as the camera moves. The halls, the dojos, the parking garage and the cavern keep the flat colour, and the castle rooftop its own painted sky and moon. A mod can give a stage a picture of its own sky ([`MODS.md`](MODS.md), "Skies"). It needs Graphics Remastered, and changes nothing the game computes. **`night` with nothing said** -- the owner's call, not yet seen on a real screen ([`PORT.md`](PORT.md), "The sky"). |
| `see_through` | `on`, `off` | The stage's walls, terraces and the rock of the cavern **thinned where they stand between the camera and a fighter**, so that he can always be seen, as modern fighting and action games do: a soft hole through the wall the size of the fighter, a dither that `antialiasing` smooths into a fade. The floor someone may stand on is never thinned, and nothing far behind a fighter or beside him is. The game does this itself for a few tiles a stage marks (half-transparent while a fighter is behind them); this does it for every face of the ground's mesh. It needs Graphics Remastered and `stage_mesh`, and changes nothing the game computes. **`on` with nothing said** -- the owner's call ([`PORT.md`](PORT.md), "Seeing the fighters through the stage"). |
| `stage_lights` | `on`, `off` | The stage's own **torches, fires and lanterns light what is about them**: the ground, the walls and the fighters. They come from the light files of the mods: the program ships one (`mods/lights/`) with the game's own flames lit on the nine stages that have them and the lamps, lit windows and doors of more, and a mod of yours can add lights, move them or put them out ([`MODS.md`](MODS.md), "Lights"). Graphics Remastered only; changes nothing the game computes. **`on` with nothing said.** |
| `effect_lights` | `on`, `off` | **The flash where blades meet**, of a hit and of a shot, and the glow of the sparks, as short lights of their own colours that light the fighters and the ground about them for the few frames the game lights the fighters itself. Graphics Remastered only; changes nothing the game computes. **`on` with nothing said.** |

## Kinds of stage

Each stage of the select screen can be fought two ways, and a mod's
arenas a third ([`ROADMAP.md`](ROADMAP.md), item 16):

- **Remastered Classic**: the disc's stage, the same ground and the same
  walls, dressed -- its own torches, fires and lanterns lit
  (`stage_lights`), the stages' pages made larger if you made the
  `stages-hd` mod, the night sky (`sky`), and later grass, water and
  the like;
- **Classic**: the disc's stage as it is, with only what the engine and
  the renderer improve for every stage (the resolution, the wide
  picture, the draw distance, the frame rate, the shadows, the walls
  seen through, the new lighting);
- **a custom arena**: a stage a mod brings ([`MODS.md`](MODS.md),
  "Custom arenas").

On a **stage page** of versus or link mode (and of versus the CPU and
training: "A stage in every mode", below), a panel under the scroll
has a column for each choice the page makes, its value over the button
that changes it: **Style** (Square), **Time** (L1 and R1), **Music** (L2
and R2) and, when a mod brings any, **Arenas** (Select).
**Square** switches the stage under the cursor between Classic and
Remastered Classic, and it stays so for as long as the program runs.
**Select** opens the page of the **custom arenas**: up and down choose
one, its picture beside the list; **Cross** fights on it; **Triangle**
or Select goes back to the stages. **L1** and **R1** turn the stage's
time of day ("The time of day", below), **L2** and **R2** choose the
fight's music ("Music on the stage select", below). On the keyboard
Square is A, Select is Right Shift, L1 and R1 are Q and W and L2 and R2
are 1 and 2 for player one ("The keys").

`stage_kind = remastered` (the default) or `classic` in `rebushido.cfg`,
the settings menu's "Stages", is what every stage starts as, and what
the modes with no stage page (story, chanbara, group battle, and versus
the CPU and training with `stage_select_everywhere = off`) fight on.
None of it is the game's play: Classic and Remastered Classic are the
same fight. In the Original picture (F2) a stage looks as the console's
whichever kind it is, and the panel and the page are still laid over it.
Online, both players choose on the same screen as on a couch, so the
kind and the arena are the same for both, and the kind the stages start
as is the host's.

## The time of day

Every stage of the disc is set at night. A **Remastered Classic** stage
(and an arena) can be fought at another time ([`ROADMAP.md`](ROADMAP.md),
item 20): on a stage page **L1** and **R1** (Q and W for player one on
the keyboard) turn the time of the stage under the cursor through
**the stage's own** (the game's lighting, as it always was), **dawn**,
**day**, **dusk** and **night**; the panel's Time says which.
A Classic stage keeps the game's own light ("The game's light" in the
panel), and so does the Original picture (F2).

- **Dawn** and **dusk**: the sun low over the horizon, gold or deep
  orange, the shadows long, the sky coloured on the sun's side;
- **day**: a high white sun, a blue sky with clouds, a light haze in the
  distance;
- **night**: not the stage's own night made darker but a night of its
  own -- a weak, cool moon with soft shadows, real darkness between the
  lights, and the stage's own torches, lanterns and lamps brighter and
  glowing more, the main light of the scene; stars and a moon in the
  sky.

The sun's way, the shadows, the sky, the fog's colour, the light on the
fighters and the glow of the lights all follow the time. A hall, a
cave or a garage keeps its own sun's way and no sky, and takes half the
time's colour, as light through its doors. Nothing of it touches the
fight.

`time_of_day = stage` (the default), `dawn`, `day`, `dusk` or `night` in
`rebushido.cfg`, the settings menu's "Time of day", is the time every
stage starts at and the one the modes with no stage page fight at.
Online the host's holds for both, as the kind does, and the presses on
the stage pages are both players'.

## A stage in every mode

In the original, versus the CPU and training choose their stage
themselves: versus the CPU fights each opponent on that opponent's own
stage, and training is in the dojo of its own. With
**`stage_select_everywhere = on`** (the default; the settings menu's
"Stage select in every mode", among the mods' lines) both go on, after
the crossed weapons, to the **same stage pages versus mode has**: the
game's own three pages of six stages, left and right across them, Cross
to choose, Triangle back to the weapons. Player 1 drives them alone; no
second controller is needed. Square switches the stage under the cursor
between Classic and Remastered Classic and Select opens the custom
arenas, as in versus mode ("Kinds of stage").

- **Versus the CPU**: top menu, Vs. CPU, a fighter, a weapon, then the
  stage pages. Every opponent of the run is fought on the stage chosen.
- **Training**: top menu, Training, either kind of practice, a fighter
  and the weapons, then the stage pages. The fight is on the stage
  chosen, the ninja or the opponent and the pause menu's options as
  before; "Return to Sub Menu" comes back to training's own page, and
  from there through the select screen to the stage pages again.

`stage_select_everywhere = off` gives both modes back their own stages,
as the original has them. Online, the host's setting holds for both
players (the session always plays versus mode, which has the pages
anyway).

## Music on the stage select

The original plays music over a fight only in versus mode (the stage's
own piece), practice and chanbara (pieces of their own); story, versus
the CPU and group battle have none, only the stage's own sounds of wind,
water and birds. In the port **every fight has its stage's music**,
and the stage select lets you choose any other piece for it.

On a **stage page**, the panel's Music says what the fight will play.
**L2** and **R2** (1 and 2 on the keyboard for player one) go back and
forward through:

- **Stage's own** (where the row starts): the piece versus mode plays on
  that stage, in every mode; a mod's arena's own piece on an arena.
  Practice and chanbara keep the pieces they have in the original;
- every piece the game has, by where the game plays it: Tatara Shrine
  (most stages', and the narration's), Select screen, Snake Cliff,
  Bamboo Thicket, Meikyokan Dojo, **Unheard I** and **Unheard II** (two
  pieces on the disc that the game never plays), Title menu, Chanbara:
  Narukagami, Chanbara: Shainto, Practice round, Practice and Staff
  roll;
- any piece a music mod brings under a name of its own (`music/my
  piece.ogg`: [`MODS.md`](MODS.md), "Music"), by that name;
- **None**: no music, the stage's own sounds instead.

The choice is the fight's, and in versus the CPU every opponent's of the
run, since they are all fought on the stage chosen; the row starts again
from the setting the next time the select screen opens. Whenever a fight
plays music the stage's own sounds are left out, and with None they are
heard, as versus mode has them with the music on and off. The options'
own music setting off still silences all of it.

**`fight_music`** in `rebushido.cfg` (the settings menu's "Fight music",
among the mods' lines) is where the row starts and what the modes with
no stage page play (story, group battle, chanbara, and versus the CPU
and training with `stage_select_everywhere = off`):

- `stage` (the default): the stage's own piece in every mode, as above;
- `original`: the game's own rule, no music in story, versus the CPU and
  group battle; the row's first place then reads "Game's own";
- `none`: no music in any fight; the row starts at None.

Online, the host's setting holds for both players, and both choose the
piece on the same page; a mod's own pieces are not offered online,
since the two players' mods may differ.

## Everything unlocked

**`unlock_all = on`** (the settings menu's "Unlock everything"; off with
nothing said, and `--unlock-all` on the command line) opens everything
the game keeps behind its progress, for trying every fighter:

- every hidden fighter on the select screens: the fourteen of the twenty
  named fighters an empty save has not won, in every mode that offers
  them (the two gunners among them, Tsubame and Katze, are offered by
  versus, group battle and link mode only, as in the original), and the
  two clans' last fighters, the crests beside the grid, in versus mode,
  the only mode that has them;
- versus the CPU's full run: the gunners and the clan's last fighter
  among the opponents, and the ending after the twentieth;
- **slash mode** on the top menu, which the game opens once all six
  stories have been finished.

The weapons are not behind any progress: each fighter has his clan's
five, or his gun, whatever the save says. **Nothing of it is written to
the memory card**: the game reads the save as if everything were won, and
the save itself keeps only what you have won, so a save made with the
setting on (after a story, a chanbara run, or from the game's own options page)
writes your own progress and nothing more, and turning the setting off
puts everything back as your card has it. It takes effect at the next
screen that reads it: a select screen as it opens, and the top menu the
next time it is shown. Online, the host's setting is both players'.
## More pictures a second

The game steps 30 times a second in a fight and 60 in the menus, as the
console does, and it has to go on doing exactly that: it is the same
game. So a higher frame rate is **more pictures of the same frames**,
not more frames. The renderer keeps the scene of the frame on show and
the scene of the frame after it — which the game has already worked out,
because the console shows a frame one blank after it is drawn — and
draws the scene again at any point between the two, every corner moved
part of the way as the camera's own projection would move it. The HUD,
the clock and the menus stay at the game's own rate, where they belong.
**Nothing is guessed ahead and nothing is held back: a picture is never
later than the port's frame on show**, so this adds no input lag of its
own; what lag there is, and what cuts it, is the next section.

| | | |
|---|---|---|
| `frame_rate` | `original`, `display`, a number | `original`, which is the default, draws one picture a vertical blank — the console's 59.83 a second, exactly as the window did before. `display` draws as many pictures a second as your screen shows, so 144 on a 144 Hz screen. A number asks for that many. `--frame-rate original|display|N` says it on the command line. It needs Graphics Remastered; with Original it does nothing. |

The counter in the corner (F3) shows both rates: `143.8/30.0` is 143.8
pictures a second of a game stepping 30 times a second.

**Why `original` is still the default.** Everything about it has been
measured and nothing about it has been *seen*: the pictures between two
frames were written to files and held to the two they lie between, but
nobody has yet watched them move on a screen ([`PORT.md`](PORT.md),
"More pictures a second"). Turn it on and tell the owner how it looks.
What may show: a weapon or a spark that appears or vanishes between two
frames waits for the next frame rather than fading in, and a camera cut
shows the console's own picture until the cut is over.

The lighting Remastered has is the game's own, worked out at each pixel
where `smooth_light` asks for it, and the fighters' shadows where
`shadows` asks for them (above); new lights of its own it does not add
([`ROADMAP.md`](ROADMAP.md), item 7).

## Loading

The port reads your disc image as fast as the PC reads a file, so a
load is not the drive's time but the game's own way of waiting for it:
a fixed number of the console's blanks for each file, about 0.6 seconds
a file at the console's pace, eleven files before a fight.

| | | |
|---|---|---|
| `quick_loading` | `on`, `off` | `on`, which is the default: the blanks in which the game waits for the disc behind its loading picture (the map and the title, or a black screen) are **hurried through**, not shown one by one at the console's 60 a second; a fight's load is then a blink rather than six seconds. A loading screen you read -- story mode's card of the next stage, the team battle's scores -- stays up two seconds before its load is hurried. The fighters' own models (Remastered) are read during the load too, not at the fight's first frame. `off` shows every blank, as the console's pace. **Nothing the game computes changes** either way: it is the same game, its fades and its timings are its own, and online both players still meet at every load. `--quick-loading on|off` says it on the command line. |

## Input lag

From a press to the picture that shows it, the window now takes about
two of the game's frames less than it did, and less than the console
itself ([`PORT.md`](PORT.md), "Input lag"). Three things do it, and all
three are on with nothing said:

- **The keys and controllers are read again just before the game takes
  its input**, not only at the vertical blank before it. Nothing to set:
  in a fight the press is taken a blank (17 ms) sooner, in the menus most
  of one.
- **`run_ahead`**, `0`, `1` or `2` (`1` with nothing said; the settings
  menu's window page, "Run ahead", at once): before each of the game's
  frames the port runs the next one or two, with what you hold, keeps the
  picture they leave, and puts the game back where it was; then the
  frame runs as it always did, heard and paced, and that picture is what
  is shown meanwhile. The game puts a frame on the screen two of its
  frames after it reads the pads (a fight's frame is two blanks), and
  the port draws a frame at once, where the console takes the frame's
  time to draw it: so the picture shown is **never a guess**, it is the
  frame the game will show, earlier. Each frame ahead is 33 ms sooner in
  a fight and 17 ms in the menus. **What the game computes is exactly
  the same** with it on or off: the sound is the game's own as it was,
  a memory card is written only by the game's own frame, and online play
  turns it off (it has its own way with time). What it costs is the
  processor's time, the game's work done two or three times a frame;
  with the pictures between frames on, at `2` the second frame ahead is
  what they move towards, and that one is a guess: where you press,
  the picture moves to the frame with the press in it when it comes.
  If the counter (F3) shows the speed under 100% on your PC, `1` costs
  half as much.
- **`low_latency`**, `on` or `off` (`on` with nothing said; "Low
  latency", at once): each picture waits for the graphics card to have
  drawn it and the swap to be made before the next is begun, so no
  picture waits in the driver's queue behind another. `off` lets the
  driver queue them, which may be smoother on a PC that is only just
  fast enough.
- **`late_input`**, `on` or `off` (`on` with nothing said; "Late
  input", at once): where a frame's first picture would be ready before
  the screen's refresh that shows it, the port waits out the difference
  first and reads the keys and controllers again just before the frame
  is worked out, so a press made meanwhile is in that picture. The
  picture comes at the same refresh as before, and the game is the same;
  in a fight about 1 to 3 ms sooner (measured at 60 and 144 Hz), in the
  menus up to 4. It needs `run_ahead` 1 or 2,
  `vsync` and `low_latency` on. About one frame in a hundred is then a
  refresh late for its picture, a picture held once more; `off` if that
  is ever seen.
- **`cpu_prefer`**, `auto` or `off` (`auto` with nothing said; "Larger-cache
  cores", the next start): on a processor whose cores are on two dies
  and only one of them has the larger cache (AMD's Ryzen 9 7950X3D,
  7900X3D, 9950X3D and 9900X3D: 3D V-Cache on one die), the game's own
  work runs on that die's cores, where it is quicker. On any other
  processor (one die, two dies alike, Intel's P and E cores) nothing is
  changed and the system decides as ever; the log says what was found.
  Where the game runs changes how soon a frame is ready, never what it
  computes.

What only your screen and PC decide, and what helps there:

- **Full screen (F11), not a window.** A window, a maximized one
  included (2,560 by 1,369 on a 2,560 by 1,440 screen is one), is
  composed with the desktop by Windows, which can hold each picture for
  a refresh of the screen. Full screen lets the driver put the picture
  up itself.
- **`vsync = on`** waits for the screen's refresh, and is the one to
  use. A step of the game that runs long no longer waits for the screen
  as well: a blank more than a refresh late shows no picture of its
  own, so a slow moment costs a refresh of the picture and not the
  game's pace or the sound (on the owner's RTX 3090 at 85 Hz, with the
  processor's work near a blank, a fight let time go 2 times in 40
  seconds with `on` and 19 times with `off`; [`PORT.md`](PORT.md),
  "Pacing on a real display"). `adaptive` waits too, but shows a picture
  that missed its refresh at once, torn, where the driver can; `off`
  shows each picture as soon as it is drawn, sooner by up to a refresh,
  and tears every one.
- A screen's own processing (its "game mode") and the graphics driver's
  own low-latency setting are outside the program.

## Mods

A folder `mods/` beside the program holds mods: each a folder of its
own, whose files stand in for the game's. The program comes with two,
drawn by Graphics Remastered. `mods/english/`: **the top menu in
English** (Story Mode, Vs. CPU Mode and the rest in place of 決戦の間 and
the others); `english = off` leaves it out ([`MODS.md`](MODS.md), "The
English menu"). `mods/ui/`: **the sharp menus**, the pause menu, the
menu after a fight, the top menu's captions, the select screens' titles
and the group battle's page lettered again in a font, sharp at any size;
`ui = off` leaves it out ("The sharp menus").

**Every other screen at a higher resolution, the stages' textures and
the fighters** are mods made from your own disc, since they are the
game's own pictures and models made anew and are not the project's to
give. The package for Windows makes them itself the first time it starts
with your disc ("The first start", below): `mods/art/` (the menus, the
select screens' portraits, the loading screens, the captions and the
fight's counters, every 2D texture six times larger), `mods/stages-hd/`
(every stage's textures four times larger) and `mods/fighters/`
(Tatsumi's model with its joints and crossing layers mended). From the
repository the same three are made by `tools/port_art.py`,
`tools/port_stage_hd.py` and `tools/port_models/` ([`MODS.md`](MODS.md),
"The art mod", "The stages' pages made larger"), into `tmp/mods/`, to be
copied into `mods/` beside the program. The first kind is
**replacement textures** for Graphics Remastered: the menus, the pause,
the fight's counters and the boot logo redrawn at any size.
`--dump-textures FOLDER` writes the game's textures out as PNGs to make
them from; `--mods FOLDER` reads them from somewhere else, and `--mods
off` or `mods = off` runs without. How to make one, and how a texture
is named: [`MODS.md`](MODS.md).

A mod's **music** (`music/`, `.ogg` or `.wav`) is heard in place of a
piece of the game's, faded and stopped as the game fades and stops its
own; `--dump-music FOLDER` writes each piece as it plays, named as a
replacement must be, and `music_mods = off` (the settings menu's
"Mods' music", at once) plays the game's own. A mod's **stage files**
(`stages/`) are read in place of the disc's: `--dump-stages FOLDER`
writes them all from your disc to begin from. A stage mod can change
how fights play on it, so both players of a netplay session need the
same ones. [`MODS.md`](MODS.md), "Music" and "Stages".

### The first start

The first time the package's `rebushido.exe` starts with a disc, before
the game's window opens, it makes those three mods from the disc, with a
screen of how far it has come ("Preparing the remastered art from your
disc... 1/3", the step, a bar and the time). It plays the game itself
with no window shown to see every texture (so the graphics card works
and a fan may spin up), then makes each larger with the Python the
package carries in `prepare/`: nothing is installed and nothing is
downloaded. It takes about three and a half minutes on the PC it was measured on
and needs about 500 MB free while it works. **Escape** (or B on a
controller) skips it and the game starts with what is done; the next
start goes on from there. Once made, the mods are made again only for
another disc image or another version of the program; a folder of
`mods/` deleted is made again at the next start, and one of your own
(a `mod.txt`, no `made-from-disc.txt`) is never written over.
`disc_mods = off` (the settings menu's Mods page, "Mods made from your
disc") never makes them, and `rebushido.exe --prepare-only` makes them
and ends. [`MODS.md`](MODS.md), "Made from your disc".

## Playing online

Two players on two PCs play versus over the internet or a home network,
with **rollback netcode**: your own presses count at once (after a
delay of a frame or two you choose), the other player's are guessed
until they arrive, and where a guess was wrong the game puts itself
right in a moment ([`NETPLAY.md`](NETPLAY.md), sections 12 and 13). Both
players need the **same build of ReBushido** (the same version, for the
same system, 64-bit with 64-bit) and an image of **the same disc**; the
two programs check both before anything starts, and say what differs.
The way the two PCs reach each other is the program's to find: a room
at the lobby is all either player has to choose.

**One of you hosts a room, the other joins it.** **Online Play** on the
top menu, or *Play online* in the settings menu (F1). The page's first
row says what you do; Left and Right change it.

- *Host a room*: a room at the **lobby** (a small server of the
  project's: *Lobby server* on the page, the owner's built in), with a
  name and, if you like, a password; then *Open the room now*. The
  program starts again, asks your router to open its port (UPnP,
  NAT-PMP or PCP: *Open the port in the router*), and waits. The panel
  says the room's code too, for a player who would rather type it.
- *Browse rooms*: *Look for rooms* lists the lobby's rooms; Enter on
  one joins it, asking first for a locked room's password. A room
  whose game is not compatible with yours says what differs (another
  version, build, disc image or gameplay mods) and cannot be joined.

The password is kept only while the program runs and never goes over
the network: the host's program gives the lobby a salted hash of it,
and the joining one proves it knows it by answering a fresh challenge
(NETPLAY.md, section 14).

**The way between you is found by itself.** Once the lobby has
introduced you, both programs try every way at once and take the best
that works: IPv6 directly, the host's router port it opened, a hole
punched through both routers, or, in the last resort, the lobby passing
the game's datagrams on (a little more delay). The line at the top of
the window says which, with the round trip. No VPN or other program is
needed.

**By address, the advanced way:** *Host by address* waits at a port of
your PC (7845 if you change nothing), asking the router to open it as a
room does; *Join by address* types the host's address and port (Enter
on the row, then type; `203.0.113.7:7845`, or IPv6 as
`[2001:db8::7]:7845`; Enter again keeps it). *Your address for the
other player* asks the router (or the lobby) what the internet sees you
at. Where neither router can be opened, both can still punch through:
each sends the other their address, the host types the joiner's into
*The other player's address*, and both start at once.

Or from the command line:

```
rebushido --lobby 135.181.108.74:7845 --room "Tatara" --room-password sesame
rebushido --lobby 135.181.108.74:7845 --join KGGRRY --room-password sesame
rebushido --host 0.0.0.0:7845                 by address: the host
rebushido --join 203.0.113.7:7845             ... and the other player
```

**Check you can meet first:** `rebushido --check-online` prints your
**setup code**, as `P7-RN1C-NYZ2-3ER0` (the protocol, then the build,
the disc and the gameplay mods), and the Play online page shows it on
its last row. Send each other your codes by message: the same code
means you can play. A disc image is told by its contents, not its file:
two dumps of the one disc have the same code however they were made
(NETPLAY.md, section 14).

**Turned away?** The window says why, in words, on both screens:
another build, version, disc image or gameplay mods (with your setup
code to compare), a wrong password, a full or closed room, an
incompatible room, nobody answering at the address, a lobby that does
not answer, or a player who stopped answering. Enter or Escape closes
the message and the program; the log has it too.

`--net-delay N` on the host's line sets the delay, 0 to 8 frames (2 if
nothing says); it is both players'. *Through: This PC alone* on the
page, or `--host 127.0.0.1:7845`, is for trying it with two windows on
one PC.

**The firewall.** The first time you host or join, Windows Defender
Firewall asks whether rebushido.exe may use the network: allow it on
Private networks (and Public, if Windows has yours marked public).

**Then:** the host's memory cards are sent to the other player, and
both play on them: the fighters the host has unlocked, the host's
button layouts in the game's own options, the host's ReBushido settings
(Bugfixes among them), and the host's `unlock_all` and
`stage_select_everywhere`. Nothing of the session is saved to either
player's cards. The game starts from the beginning on both PCs and goes
by itself to **versus character select**, where you both choose at
once, as on one console; the host picks the stage. After a fight the
game's menu offers a **rematch**. In a fight Start does nothing: there
is no pause online. The opening movie is skipped.

You play with your own keys or controller, whichever player's they are
in your settings: the host is player 1 and the other player 2 in the
game. A line at the top of the window says who you are, the **ping**
(the round trip to the other PC), the longest **rollback** of the last
seconds (frames the game went back and ran again) and the **delay**; or
that it is waiting for the other player. Closing the window
leaves; the other player is told and plays on alone. The settings menu
cannot be opened during a session (the other player's game would have
to stop too).

The draw distance is the console's online, whatever `draw_distance`
says, and the two games keep the faces the wider of your two pictures
shows: the two have to compute the same frame, whichever picture each
of you looks at. F2 still changes your own picture.

Online play has been tried on one PC between Windows programs and
between Linux ones, through every way of the lobby, and through the
owner's lobby server across the internet (NETPLAY.md, section 14); not
yet between two players' PCs.

## Replays

**Every fight is kept** as a replay, by itself: versus, versus the CPU,
the story, training, chanbara, team battles and online matches (from your
own machine; the other player keeps theirs). They go into `replays/`
beside the program, one file a fight, named by its date and its mode
(`2026-11-20_143005_vs.rbr`), about 220 KB each: the game's state as the
fight began, and every press of both players after it, so that the fight
is played again rather than filmed. Keep replays (the settings menu, the
game's page; `replays = off` in `rebushido.cfg`) turns it off.

**Watching one**: the settings menu (F1, or Options on the top menu),
Replays. The newest is at the top, with its date, both fighters and their
weapons, the mode, the stage and how long it was. Enter or Cross plays it
(from a menu, not in the middle of a fight), from the fight's loading
picture on, in your own picture settings (Remastered or Original, the lights, the
resolution); when it ends, or when you stop it, the game goes on where it
was when you opened the menu. While it plays:

| Key | Controller | What it does |
|---|---|---|
| Space, Enter or P | Cross, Start | pause, and go on |
| Right or `.` | D-pad right, R1 | one frame on (and pause) |
| Up or `=` | D-pad up | twice as fast, up to eight times |
| Down or `-` | D-pad down | half as fast, down to an eighth |
| R or Home | Square | from the start |
| K or End | Triangle | to a moment before the first cut |
| Escape or Backspace | Circle, Back | stop, back to the game |

The line at the top says where it is, the speed, and the keys. F1, F2, F3
and F12 are the window's as ever.

**From the command line**: `rebushido --replay FILE` plays one and ends
there; `--replay-check FILE` plays it as fast as it runs, showing and
playing nothing, and says whether it played the same as it was recorded.

**A replay plays only in the build that recorded it**, from the same disc
image, with the same gameplay mods (stages, weapons, motions, arenas):
it is the game run again, and another build runs otherwise. The Replays
page says why one cannot be played. Each replay notes the state of the
game every second as it was recorded and the playback holds itself to
it; a difference (a "desync", which must never happen) stops it and says
so on the line and in the log.

**How many**: Replays kept (`replays_keep`, the newest 200) and Replays'
room (`replays_megabytes`, 500 MB) take the oldest away as new ones come;
0 keeps them all. Copy a replay out of the folder to keep it for good.

A fight that the program ended in the middle of is kept as far as it
went. A fight online is kept once both players' presses up to its end
are known; one the other player left in the middle of is not kept. A
fight in which the settings menu's Bug fixes was changed is not kept:
that changes the game without a press.

## Where the saves are

ReBushido saves what the game saves, in the same way: **memory card
files**, written in the PlayStation's own card format. There are two of
them, and the game never touches ReBushido's:

| The file | What it holds |
|---|---|
| `BASLUS-00663BUSHIDO2` | the game's own save: the fighters unlocked, chanbara's best times and the game's options. One block |
| `BASLUS-00663REBUSHI` | ReBushido's settings, which the options page sets (`menu = original`; ReBushido's top menu keeps them in `rebushido.cfg`). One block |

On a PlayStation, and in an emulator, they are on the memory card in slot
1, as any save is. In the port each slot is a file on the computer: an
ordinary raw memory card image of 128 KB, the same thing an emulator's
card file is. So a card tool or an emulator opens the port's card, and a
card file from an emulator works here.

- Slot 1 is `card1.mcd` and slot 2 is `card2.mcd`, **beside the
  program**, unless `--card1 FILE` or `--card2 FILE` says otherwise.
- A file that is not there is made the first time the game looks at that
  slot: formatted and empty, as a new card is.
- `--no-cards` leaves both slots empty, as a console with no card in it.
- What the game has been told is saved is on the disc before the message
  goes away: every write goes through to the file at once.

`tools/card.py` lists, checks and opens up a card image, whoever wrote it
(`python tools/card.py list card1.mcd`).

## The opening movie

The opening plays, with its sound, and **cross skips it** -- only cross,
at any moment. It is the game's own player, decoded by the port
([`PORT.md`](PORT.md), "The movie"): 3,320 frames of 320 by 240 at 29.97
a second, about a minute and fifty, and then the title picture is held
for ten seconds and fades.

## What does not work yet

- **No link cable.**
- The sound has not been heard by anybody yet, and what the port makes
  of the hardware's filters is not the console's to the last sample.
- Nobody has yet looked at the remastered picture on a real graphics
  card: it has been drawn only by software drivers, with no window on
  screen ([`PORT.md`](PORT.md), "Remastered in the window").
- Online play has not been played between two PCs, nor on Windows
  ("Playing online").
- Nobody has watched the pictures between the game's frames move either,
  which is why `frame_rate` is `original` until someone does. On a
  machine with no card at all there is no room to draw a frame twice:
  the fight slowed to about half speed in the one run that was made.

## If something is wrong

**Every session writes a log**: the folder `logs` beside the program,
one file a session, named for its date and time
(`rebushido-20261115-203000.log`); the newest twenty are kept. It has
everything the program said, what it ran on, where the game was (each
screen, each fight with its fighters and stage) and a line every ten
seconds. If the game stops by itself, a box says so and names the log,
and the log's last lines say where it stopped: that file is what to
send. A line in it that begins `rebushido: stopped:` is the game's own
code failing, with where. Where the game stuttered or the sound broke
up, the log says why each time: `pace: blank N was 106 ms late ...; the
blank before took 39.7 ms, 27.6 of it work: TWEEN 5.6 AHEAD 5.4 ...`
and `sound: ran out before blank N: ...` name the parts that took the
time. **F9** (above) keeps all of that with the last ten seconds' times
and a picture, in one folder. A
picture or a sound that is wrong is worth reporting with what was on the
screen: this is the first time anyone looks.
