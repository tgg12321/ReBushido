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
p1.cross = key Space     # Space is player one's Cross, and Z no longer
p1.circle = key Left Ctrl
p2.start = pad a         # on player two's controller, A is Start
p1.select = none         # nothing presses it
```

`rebushido --help` prints the whole file as it is when you have none,
to copy lines from, and says how keys and controller buttons are named.
A line the program cannot use is named in the console window as it
starts.

## What does not work yet

- **No memory card.** The game finds no card: the settings of its
  options and what you have unlocked are not kept when you close it.
- **No movies.** The opening movie is left out.
- **No link cable.**
- The picture is the PlayStation's own, at the PlayStation's size,
  scaled up: the wide picture and the higher resolution are not made
  yet.
- The sound has not been compared with a console's.

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
