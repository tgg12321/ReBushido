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

`--fullscreen` starts it filling the screen; `rebushido --help` lists
the rest.

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

**Escape**, or closing the window, ends the program. **F11** or
**Alt+Enter** fills the screen and comes back. **F3** shows and hides
the counter in the corner: `59.8/30.0 100%` is the pictures shown a
second, the game's own frames a second (30 in a fight and 60 in the
main menu are full speed), and the speed against the console's.

## Changing the keys and the picture

Make a text file `rebushido.cfg` beside the program. A line is
`name = value`; what follows a `#` is a comment. For example:

```
scale = integer          # fit (as large as fits), integer, or stretch
filter = linear          # nearest (each pixel a block) or linear
fullscreen = on
fps = off                # no counter in the corner (F3 brings it back)
graphics = remastered    # original (the console's picture) or remastered
resolution = 3           # how many times the console's; 0 = the window's
draw_distance = max      # original, far, or max: how much of the stage
antialiasing = 2         # off, 2 or 4: the scene's edges smoothed
aspect = 16:9            # 4:3 as the console (the default), or wider
frame_rate = display     # original (the default), display, or a number
p1.cross = key Space     # Space is player one's Cross, and Z no longer
p1.circle = key Left Ctrl
p2.start = pad a         # on player two's controller, A is Start
p1.select = none         # nothing presses it
```

`rebushido --help` prints the whole file as it is when you have none,
to copy lines from, and says how keys and controller buttons are named.
A line the program cannot use is named in the console window as it
starts.

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
| `draw_distance` | `original`, `far`, `max` | How much of the stage is drawn, and how far out its fog is placed. The console draws a disc of about 26,000 units about the camera and its fog ends there, so the ground fades into the distance; `far` draws about twice as much, and `max` draws the stage to its own far edge with the fog pushed out four times as far. The fog's colours are not touched, so a night stage stays as dark as it was. `far` with nothing said. |
| `antialiasing` | `off`, `2`, `4` | The scene drawn that many times over each way and averaged down, which smooths its edges. The HUD, the fonts and the menus are left exactly as crisp as they were. It costs fill rate and nothing else: four times over is sixteen times the pixels. `2` with nothing said. |
| `aspect` | `4:3`, `16:9`, any `W:H` | The shape of the picture. A wider one is a **wider view of the same scene**, not a stretched picture: the camera keeps its height and gains width. Use it with `draw_distance max`, which fills the sides. `4:3` with nothing said, because the HUD does not move out to the new edges yet: in a fight the counters and the clock stay in the middle band ([`PORT.md`](PORT.md), "A wider picture"). |

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

What Remastered does **not** do yet: new lighting or shadows. That is
the step after it ([`ROADMAP.md`](ROADMAP.md), item 7).

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
