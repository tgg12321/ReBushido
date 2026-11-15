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
  with 32-bit SDL 2 (`libsdl2-2.0-0:i386` on Ubuntu).
- **The program.** There is no release of it yet: it is built from this
  repository ([`BUILD.md`](../BUILD.md), "The port with a window"), into
  `build/port-sdl/`:

  | | |
  |---|---|
  | Windows | `rebushido.exe`, with `SDL2.dll` and `rebushido.exe.sym` beside it |
  | Linux | `rebushido`, with `rebushido.sym` beside it |

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
build/port-sdl/rebushido --image "Bushido Blade 2 (USA).cue"
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
**Alt+Enter** fills the screen and comes back.

## Changing the keys and the picture

Make a text file `rebushido.cfg` beside the program. A line is
`name = value`; what follows a `#` is a comment. For example:

```
scale = integer          # fit (as large as fits), integer, or stretch
filter = linear          # nearest (each pixel a block) or linear
fullscreen = on
draw_distance = max      # original, far, or max: how much of the stage
antialiasing = 2         # off, 2 or 4: the scene's edges smoothed
aspect = 16:9            # 4:3 as the console, or any shape you like
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

**The window does not use it yet.** The renderer is written and proved
with no window, by drawing both pictures and comparing them
(`tools/port_gl_prove.py`); hanging it on the window is the next piece
of work, and with it:

- **F2** switches between Original and Remastered while the game runs,
  and `--graphics original` or `--graphics remastered` starts it either
  way (the options page's choice is what it follows if neither says);
- an option forces how many times the console's resolution the scene is
  drawn at, instead of the window's own size choosing (`--scale` is
  already the name of where the picture goes in the window, so the new
  one will be `--resolution N`).

**The three settings of the remastered picture** are a run's: they are
read once as it starts, and changing one means starting it again. Each
does nothing at all while Graphics is Original.

| | | |
|---|---|---|
| `draw_distance` | `original`, `far`, `max` | How much of the stage is drawn, and how far out its fog is placed. The console draws a disc of about 26,000 units about the camera and its fog ends there, so the ground fades into the distance; `max` draws the stage to its own far edge and pushes the fog out four times as far. The fog's colours are not touched, so a night stage stays as dark as it was. `original` with nothing said. |
| `antialiasing` | `off`, `2`, `4` | The scene drawn that many times over each way and averaged down, which smooths its edges. The HUD, the fonts and the menus are left exactly as crisp as they were. It costs fill rate and nothing else: four times over is sixteen times the pixels. `off` with nothing said; `2` is what the measurements say to take. |
| `aspect` | `4:3`, `16:9`, any `W:H` | The shape of the picture. A wider one is a **wider view of the same scene**, not a stretched picture: the camera keeps its height and gains width. The HUD and the menus stay four by three in the middle of it. Use it with `draw_distance max`: the stage's own choice of what to draw is cut for the console's field of view, so a wide picture at `original` has little to show at the sides. `4:3` with nothing said. |

What Remastered does **not** do yet: more than the game's own 30 or 60
pictures a second, and new lighting or shadows. Those are the steps
after it ([`ROADMAP.md`](ROADMAP.md), item 7). The HUD does not move out
to the edges of a wide picture yet ([`PORT.md`](PORT.md), "A wider
picture").

## What does not work yet

- **No memory card.** The game finds no card: the settings of its
  options and what you have unlocked are not kept when you close it.
- **No movies.** The opening movie is left out.
- **No link cable.**
- The window draws the PlayStation's own picture, scaled up; the
  remastered one is not hung on it yet (above).
- The sound has not been compared with a console's.

## If something is wrong

Start it from a terminal, so that what it says stays on the screen. A
line that begins `rebushido: stopped:` is the game's own code failing,
with where; that line and the ones after it are what to report. A
picture or a sound that is wrong is worth reporting with what was on the
screen: this is the first time anyone looks.
