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
pixels, waiting for the display, the counter and its corner, Graphics
(as F2), the resolution, the ground as one mesh, the shadows, the dead
zone, and the buttons. What waits for the program to start again is said
on its line, `(after a restart)`: antialiasing, the draw distance, the
shape, the frame rate, the English top menu and the mods.

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
graphics = remastered    # original (the console's picture) or remastered
resolution = 3           # how many times the console's; 0 = the window's
draw_distance = far      # original, far, or max: how much of the stage
antialiasing = 2         # off, 2 or 4: the scene's edges smoothed
aspect = 4:3             # the console's shape; 16:9 is the default
stage_mesh = off         # the game's own tiles; on is the default
shadows = cast           # original (the game's black ones), soft or cast
frame_rate = display     # original (the default), display, or a number
mods = off               # none of the mods in mods/ (on is the default)
english = off            # the top menu in Japanese (on is the default)
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
later than the console's own**, so this costs no input lag.

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

What Remastered does **not** do yet: new lighting. That is the step
after it ([`ROADMAP.md`](ROADMAP.md), item 7). The fighters' shadows it
does do, where `shadows` asks for them (above).

## Mods

A folder `mods/` beside the program holds mods: each a folder of its
own, whose files stand in for the game's. The program comes with one,
`mods/english/`: **the top menu in English** (Story Mode, Vs. CPU Mode
and the rest in place of 決戦の間 and the others), drawn by Graphics
Remastered; `english = off` leaves it out ([`MODS.md`](MODS.md), "The
English menu"). The first kind is
**replacement textures** for Graphics Remastered: the menus, the pause,
the fight's counters and the boot logo redrawn at any size.
`--dump-textures FOLDER` writes the game's textures out as PNGs to make
them from; `--mods FOLDER` reads them from somewhere else, and `--mods
off` or `mods = off` runs without. How to make one, and how a texture
is named: [`MODS.md`](MODS.md).

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
