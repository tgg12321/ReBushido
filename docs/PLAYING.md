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
- **The program.** There is no release of it yet: it is built from this
  repository ([`BUILD.md`](../BUILD.md), "The port with a window"), into
  `build/port-sdl/`:

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

**Linux.**

```
build/port-sdl/linux/rebushido --image "Bushido Blade 2 (USA).cue"
```

It starts filling the screen; F11 or Alt+Enter, or `--windowed`, puts
it in a window. `rebushido --help` lists the rest.

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
guide button. **Escape**, or closing the window, ends the program. **F11** or
**Alt+Enter** fills the screen and comes back. **F3** shows and hides
the counter in the corner: `59.8/30.0 100%` is the pictures shown a
second, the game's own frames a second (30 in a fight and 60 in the
main menu are full speed), and the speed against the console's.

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

## The settings menu

Every setting of `rebushido.cfg` below and the buttons of both players
are in a menu of the program's own, drawn over the game at the window's
own resolution. **F1** or a controller's **guide button** opens it
anywhere; **Select** opens it on the three screens where the game has
no use for Select: the options page at boot, the top menu and the
fight's pause menu (Start, then Select).

| | The keyboard | A controller |
|---|---|---|
| choose a line, change its value | the arrow keys | the direction pad or the left stick |
| open (a player's buttons), bind | Enter | Cross (the lowest of the four) |
| back, or close | Escape, Backspace | Circle, Triangle |
| a button back to the layout's own | Delete | Square |
| close | F1 | Start, Back, guide |

The menu's own keys are always these, whatever the players' are.

**The game is stopped while it is open**, at the end of the vertical
blank where it has just shown a frame, and goes on from there when it
closes as if no time had passed: nothing of the game runs in between,
and a key or a button still held as the menu closes is not given to the
game until it is let go. The sound runs out and comes back.

What changes at once: the full screen, the picture's size and its
pixels, waiting for the display, the counter and its corner, quick
loading, run-ahead and low latency, Graphics
(as F2), the resolution, the ground as one mesh, the shadows, smooth
lighting, the effects, the dead zone, and the buttons. What waits for the program to start again is said
on its line, `(after a restart)`: antialiasing, the draw distance, the
shape, the frame rate, the English top menu, the sharp menus and the
mods.

**Player 1's buttons** and **Player 2's buttons** list each of the
PlayStation's buttons with the keys and the controller's buttons that
press it. Enter or Cross on one waits for a key, a controller's button,
or a stick or a trigger pushed far, and binds it, as a `pN.BUTTON` line
of the file would (below); Escape leaves it as it was, Delete gives it
back the layout's own.

**Bug fixes**, the game's own setting, is the one the options page at
boot has: the menu changes it for the session, and the options page
keeps it on the memory card.

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
run_ahead = 1            # 0, 1 or 2 frames run ahead (2 is the default)
low_latency = off        # let the driver queue pictures (on is the default)
graphics = remastered    # original (the console's picture) or remastered
resolution = 3           # how many times the console's; 0 = the window's
draw_distance = far      # original, far, or max: how much of the stage
antialiasing = 2         # off, 2 or 4: the scene's edges smoothed
aspect = 4:3             # the console's shape; 16:9 is the default
stage_mesh = off         # the game's own tiles; on is the default
stage_props_mesh = off   # railings and walls from the game's own faces
shadows = cast           # original (the game's black ones), soft or cast
smooth_light = on        # the game's lights worked out at each pixel
effects = smooth         # original (the default), exact or smooth
frame_rate = display     # display (the default), original, or a number
mods = off               # none of the mods in mods/ (on is the default)
english = off            # the top menu in Japanese (on is the default)
ui = off                 # the menus' own lettering (on is the default)
music_mods = off         # the game's own music (on is the default)
quick_loading = off      # every blank of a load at the console's pace
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

The options page at boot has a second line, **Graphics**, with
**Original** and **Remastered** (`REBUSHIDO.md`, "The options page").
Original is the PlayStation's own picture, pixel for pixel. Remastered
draws the scene again with the graphics card, at the window's
resolution: the corners where the camera really put them instead of on
whole pixels, textures in perspective instead of sliding, true colours
instead of the chip's five bits and its dither, and the menus, the
fonts and the HUD crisp at the window's size (`PORT.md`,
"Remastered").

**F2 changes picture while the game runs**, at once and as often as you
like, and `--graphics original` or `--graphics remastered` starts it
either way; with neither, the window follows the options page. The
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
| `shadows` | `original`, `soft`, a number, `cast`, `cast:N` | What a fighter's shadow is. `original`, the default, is the game's own: the fighter's shape flattened onto the ground and painted **black**, which hides the ground under it. `soft` **darkens** the ground under that same shape instead, by 55 hundredths, so the ground keeps its texture through the shadow; the edge is softened by about three pixels of the picture, two shadows that cross are no darker than one, and **a fighter standing in a shadow is never darkened** -- its own feet and legs are exactly as lit as they are with the setting off. A number says how much of the ground a shadow takes, in hundredths, for trying another. `cast` does the same with a shadow cast from the fighter's **own body** -- every polygon of him, his sword and his scabbard, flattened onto the ground along the stage's sun -- in place of the game's coarse shadow shape, so it has his hands and his blade and does not twitch while he stands still; it takes 68 hundredths, and `cast:N` takes N. **`original` with nothing said**: nobody has looked at the soft ones on a screen yet ([`PORT.md`](PORT.md), "The shadows"). |
| `stage_mesh` | `on`, `off` | The stage's ground drawn from **one mesh of the whole stage**, built when the stage loads, instead of from the tiles the console builds again every frame. Nothing of the ground comes and goes as the camera turns, its detail never steps, the fog runs on instead of stepping in bands across a long floor, a piece of ground between the camera and a fighter veils him again, and a picture drawn between two of the game's frames has only to move the camera. **`on` with nothing said.** Every one of the game's 38 stages was measured against the console's own tiles, and a stage the measurements did not hold would keep the tiles whatever the setting says; the log says which path each stage took ([`PORT.md`](PORT.md), "The ground as one mesh"). |
| `stage_props_mesh` | `on`, `off` | What **stands on the ground** -- the railings, the fences, the walls, the posts, the roofs -- drawn from meshes of its own, built when the stage loads from the stage's own pieces and drawn with the ground's mesh and the same camera, instead of from the faces the console projects again every frame. A railing then stands still where the camera is still, its corners no longer wobble by a pixel, and in a picture drawn between two of the game's frames it is moved by the camera alone, so nothing of it is held and then jumps. A piece the game moves or changes -- a bamboo cut down, a sign that falls -- and a face that mixes with what is behind it stay the game's own. It needs Graphics Remastered and `stage_mesh`, and changes nothing the game computes. **`on` with nothing said**; a stage the measurements did not hold would keep the game's own faces, and the log says which path each stage's pieces took ([`PORT.md`](PORT.md), "The stage's pieces as meshes"). |
| `smooth_light` | `off`, `on` | The game's **own lights** -- each stage's three lights and its colour of the dark, the same ones the console uses -- worked out **at every pixel** of the ground and of the fighters' own models, instead of once a face of the ground and once a corner of a model. A slope of the ground is shaded as a slope instead of in flat facets, and a wall still meets the floor at an edge; a flat floor keeps exactly the colour it had, so a stage keeps its colours and its mood. **`off` with nothing said**: the owner has not looked at it yet ([`PORT.md`](PORT.md), "Lighting"). |
| `effects` | `original`, `exact`, `smooth` | How blood, sparks, flashes, dust, splashes, footprints, the stage's flames and the surf are drawn. `original`, the default, is where the console put them, on whole pixels of its own screen. `exact` draws each where the camera's projection really put it, its size without the console's rounding, and moves it smoothly between two frames with `frame_rate`; `smooth` also reads its texture smoothed, so a spark or a drop of blood has a soft round edge instead of the console's square texels. Their colours, their timing and how they mix with what is behind them are the console's either way, and none is ever hidden behind the ground, as on the console. **`original` with nothing said** until the owner has looked ([`PORT.md`](PORT.md), "The effects"). |
| `sky` | `night`, `original` | What is above a stage that is open to the sky. The console has no sky: a fight's picture begins as one flat colour, the stage's colour of the distance, black or a dark blue on every stage, and that is what shows wherever the stage does not cover it. `night` paints a **night sky of the port's own** there: a gradient that is the stage's own colour of the distance at the horizon, so that the ground's fog runs into it with no seam, lifting a little towards the stage's own ambient colour; stars; a few thin clouds; and a moon where the stage's light comes from. It turns with the camera and never moves as the camera moves. The halls, the dojos, the parking garage and the cavern keep the flat colour, and the castle rooftop its own painted sky and moon. A mod can give a stage a picture of its own sky ([`MODS.md`](MODS.md), "Skies"). It needs Graphics Remastered, and changes nothing the game computes. **`night` with nothing said** -- the owner's call, not yet seen on a real screen ([`PORT.md`](PORT.md), "The sky"). |
| `see_through` | `on`, `off` | The stage's walls, terraces and the rock of the cavern **thinned where they stand between the camera and a fighter**, so that he can always be seen, as modern fighting and action games do: a soft hole through the wall the size of the fighter, a dither that `antialiasing` smooths into a fade. The floor someone may stand on is never thinned, and nothing far behind a fighter or beside him is. The game does this itself for a few tiles a stage marks (half-transparent while a fighter is behind them); this does it for every face of the ground's mesh. It needs Graphics Remastered and `stage_mesh`, and changes nothing the game computes. **`on` with nothing said** -- the owner's call ([`PORT.md`](PORT.md), "Seeing the fighters through the stage"). |

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
- **`run_ahead`**, `0`, `1` or `2` (`2` with nothing said; the settings
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

What only your screen and PC decide, and what helps there:

- **Full screen (F11), not a window.** A window, a maximized one
  included (2,560 by 1,369 on a 2,560 by 1,440 screen is one), is
  composed with the desktop by Windows, which can hold each picture for
  a refresh of the screen. Full screen lets the driver put the picture
  up itself.
- **`vsync = on`** waits for the screen's refresh; `off` shows a picture
  as soon as it is drawn, sooner by up to a refresh, and tears.
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

**Every other screen at a higher resolution** is a mod you make from
your own disc, since it is the game's own pictures made larger and is
not the project's to give: `python tools/port_art.py` (the art mod:
the menus, the select screens' portraits, the loading screens, the
captions and the fight's counters, every 2D texture six times larger;
[`MODS.md`](MODS.md), "The art mod"), then copy `tmp/mods/art/` into
`mods/` beside the program. The first kind is
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

## Playing online

Two players on two PCs play versus over the internet or a home network,
with **rollback netcode**: your own presses count at once (after a
delay of a frame or two you choose), the other player's are guessed
until they arrive, and where a guess was wrong the game puts itself
right in a moment ([`NETPLAY.md`](NETPLAY.md), sections 12 and 13). Both
players need the **same build of ReBushido** (the same version, for the
same system, 64-bit with 64-bit) and an image of **the same disc**; the
two programs check both before anything starts, and say what differs.

**One of you hosts, the other joins.** From the settings menu (F1, or
Select on the top menu): *Play online*.

- The host chooses *Host a game*, the port to wait at (7845 if you
  change nothing) and *Host now*. The program starts again and waits.
- The other chooses *Join a game*, types the host's address and port
  (Enter on the row, then digits, dots and a colon, as
  `203.0.113.7:7845`; Enter again keeps it) and *Join now*.

Or from the command line:

```
rebushido --host 0.0.0.0:7845                 the host
rebushido --join 203.0.113.7:7845             the other player
```

`--net-delay N` on the host's line sets the delay, 0 to 8 frames (2 if
nothing says); it is both players'. *Through: This PC alone* on the
page, or `--host 127.0.0.1:7845`, is for trying it with two windows on
one PC.

**Then:** the host's memory cards are sent to the other player, and
both play on them: the fighters the host has unlocked, the host's
button layouts in the game's own options, the host's ReBushido settings
(Bugfixes among them). Nothing of the session is saved to either
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
that it is waiting for the other player. Escape or closing the window
leaves; the other player is told and plays on alone. The settings menu
cannot be opened during a session (the other player's game would have
to stop too).

**Across the internet** the host's router has to let the port through:
forward UDP port 7845 (or the one you chose) to the host's PC in the
router's settings. The player who joins needs nothing. The draw distance
is the console's online, whatever `draw_distance` says, and the two
games keep the faces the wider of your two pictures shows: the two have
to compute the same frame, whichever picture each of you looks at. F2
still changes your own picture.

Online play has been tried with two windows on one PC, not yet between
two PCs, and not yet on Windows.

## Where the saves are

ReBushido saves what the game saves, in the same way: **memory card
files**, written in the PlayStation's own card format. There are two of
them, and the game never touches ReBushido's:

| The file | What it holds |
|---|---|
| `BASLUS-00663BUSHIDO2` | the game's own save: the fighters unlocked, chanbara's best times and the game's options. One block |
| `BASLUS-00663REBUSHI` | ReBushido's settings, which the options page sets. One block |

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
code failing, with where. A
picture or a sound that is wrong is worth reporting with what was on the
screen: this is the first time anyone looks.
