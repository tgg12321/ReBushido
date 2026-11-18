# Building Bushido Blade 2

From a clean checkout to a byte-identical `SLUS_006.63`. The build ends with

```
build/bb2.exe: OK
OK: bb2 matches!
```

when the executable it produced has the original's SHA1
(`62efab4f73f992798c43e8c730aa43baa10bb4fa`). That line is the test of the
bytes; the checks beside it are under [What is checked](#what-is-checked).

## What you need

- Linux, or Windows 10/11 with WSL 2. Ubuntu 24.04 is what the project is
  built on; the cross binutils of another release may assemble differently.
- About 300 MB of disk for the compiler and the build.
- Internet access once, to fetch the compiler's source.
- **No game files.** The build compiles the source in this repository and
  compares a SHA1. The disc image is only needed for the asset tools (below).

## Setting up

On Windows, install WSL with Ubuntu 24.04 first (`wsl --install -d
Ubuntu-24.04` from an administrator PowerShell, then create the user it asks
for), open the Ubuntu shell and go to the checkout under `/mnt/c/...`.

```bash
bash tools/setup_wsl.sh
```

It installs `build-essential`, `binutils-mipsel-linux-gnu`,
`cpp-mipsel-linux-gnu`, `python3-venv` and `git` with apt, makes a Python
virtualenv in `.venv/` with the packages in `requirements.txt`, and builds the
compiler with `tools/build_cc1.sh` (GCC 2.7.2 at a pinned commit, into
`tools/gcc-2.7.2/`; a couple of minutes). On a distribution without apt,
install the equivalents by hand and run `tools/build_cc1.sh` yourself; the
Makefile expects the binutils to be named `mipsel-linux-gnu-*`.

The assembler stand-in, maspsx, is in the repository (`tools/maspsx/`, a
modified copy) and needs nothing installed.

## Building

```bash
bash tools/build.sh check         # build what changed, check the SHA1
bash tools/build.sh clean-check   # from scratch (about a minute)
bash tools/build.sh movovl        # the movie overlay, from scratch
```

`tools/build.sh` activates the virtualenv and runs `make -j8` with the
arguments you give it, hiding the assembler's noise. From a Windows shell,
prefix the same commands with `wsl`: `wsl bash tools/build.sh check`. Inside
WSL with the virtualenv active you can also call `make` directly:

| Target | What it does |
|---|---|
| `make`, `make check` | Build what changed and compare the SHA1 with `config/bb2.sha1` |
| `make clean-check` | `make clean`, then `make check` |
| `make movovl` | Clean build of `movovl/` and its own SHA1 check (`OK: movovl matches!`) |
| `make shift` | Relink with padding inserted and fail if anything depends on where code or data is (`tools/shift_test.py`); does not touch the matching build |
| `make modern` | The game's C through the host's modern `gcc` for its warnings and analyser; fails on a serious finding that is new (`tools/modern/check.py`) |
| `make test` | Host tests of what the game's C computes ([`tests/README.md`](tests/README.md)) |
| `make checks` | Every check below, in order, with a summary (`tools/checks.sh`) |
| `make clean` | Delete `build/` (but not `build/play/`, the disc images) |
| `make rebushido`, `rebushido-disc`, `rebushido-test`, `rebushido-modern` | The ReBushido build, into `build/rebushido/`, its disc and its checks ([`docs/REBUSHIDO.md`](docs/REBUSHIDO.md)); they leave the matching build alone |
| `make quickboot`, `quickboot-disc` | A ReBushido build that boots straight into a chosen fight, into `build/quickboot/`, and its disc: a developer's tool ([`docs/REBUSHIDO.md`](docs/REBUSHIDO.md), "Booting straight into a fight") |
| `make port` | The native port without a window, `build/port/rebushido`: one of the checks ([`docs/PORT.md`](docs/PORT.md)) |
| `make port-sdl`, `port-sdl-windows` | The native port with a window, for this host and for Windows, into `build/port-sdl/linux/` and `build/port-sdl/windows/`. Not one of the checks: it needs SDL 2 ("The port with a window", below) |
| `make playtest` | The play tests: the game run by script in an emulator with no window ([`tests/play/README.md`](tests/play/README.md)). Not one of the checks: it needs your retail image and PCSX-Redux, and says what it skipped without them |

`tools/build.sh` exits with `make`'s status, so a script can rely on it.
The overlay builds 43 of Sony's library modules from `src/main/psxsdk/`, so
a change there has to pass both byte checks.

### Where the build runs

A checkout on the Windows drive (`/mnt/c/...`) builds three to five times
slower than one on WSL's own disk: every compile reads its source and its
headers through the drive's file server. So `tools/build.sh` and
`tools/gate.py`'s builds run in a copy of the sources on WSL's disk,
`~/.cache/rebushido-build/<checkout>-<hash>/`, one per checkout and
worktree (`tools/mirror.sh`), and bring `build/` back when they end. The
commands are the same and the outputs land in `build/` as before:

- in: the sources, by what changed since the last build (rsync, about two
  seconds with nothing changed; a file deleted here is deleted there);
  the virtualenv, `tools/sdl2-mingw/` and your disc image are links, the
  compiler (`tools/gcc-2.7.2/build/cc1`) a copy;
- out: what the copy built that is newer than what is here; never the
  compilers' objects, lists of inputs and dumps (`*.o`, `*.d`, `*.gimple`,
  `*.ssa`), which only the build reads, and nothing is deleted here. The
  play tests' discs (`build/play/`) are made here directly through a link,
  as large as the disc as they are;
- `make clean` through `build.sh` cleans both; a release
  (`RB_RELEASE=1`) builds in place, as it asks git whether the tree is
  clean and the copy has no `.git` (the commit's hash is given to the
  copy's build as `RB_GIT_HASH`);
- `RB_BUILD_IN_PLACE=1` builds in the checkout as before, and so does a
  checkout that is not on `/mnt/`. The gate's steps that only run what was
  built (`port-rollback`, `port-net`, `port-budget`) run in place, and so
  do `build.sh port-rollback` and `build.sh port-net`. What they write
  (`build/rollback/`, `net/`, `budget/`, `bench/`, and the gate's
  `build/gate/`) is never brought back from the copy: a copy's old
  `build/rollback/` brought back by a build step that ended mid-run
  replaced the files `port-rollback` was comparing, and failed the gate
  now and then (docs/NETPLAY.md, "What failed in the gate").

Measured on the owner's machine (2026-11-18, other builds running beside):

| | on `/mnt/c` | in the copy, with the copying |
|---|---|---|
| `port` from nothing | 80 s | 14 s (and 2 s to copy in) |
| `port`, nothing changed | 68 s before, 23 s now* | 16 s |
| `port`, one file changed | 68 s before, 31 s now* | 14–16 s |
| `port-sdl-windows` from nothing / one file | 46 s / 53–57 s | 11 s / 8–9 s |
| `check` from nothing / one file | 14 s / 6 s | 3 s / 3 s |

\* `make port`'s checks `port_casts` and `port_order` keep gcc's dump of
each file and read it again where none of its inputs changed, and a port
build that compiled nothing links nothing and writes nothing
(`tools/port_build.py`): on the drive itself that took a no-op from 68 s
to 23 s.

## The port with a window

`make port` needs nothing more than the checks do. The port a person can
play ([`docs/PLAYING.md`](docs/PLAYING.md); [`docs/PORT.md`](docs/PORT.md),
"The window") needs SDL 2 and a C library, which nothing else here does
and no check uses. It needs **nothing of OpenGL at build time**, though
it draws the remastered picture with it: the backend looks every GL
function up through SDL as it starts ("Remastered in the window"), and
where the machine has no OpenGL 4.5 the window shows the console's own
picture and says so in its log. Both programs have 64-bit pointers; `PORT_BITS=32`
builds the 32-bit one beside each, into `build/port-sdl/linux32/` or
`windows32/`.

For Linux (and WSL), the program `build/port-sdl/linux/rebushido`:

```bash
sudo apt install libsdl2-dev
bash tools/build.sh port-sdl
```

For the 32-bit program as well:

```bash
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install lib32gcc-13-dev libsdl2-dev:i386
PORT_BITS=32 bash tools/build.sh port-sdl
```

**Do not install `gcc-multilib` for it:** on Ubuntu 24.04 it removes
`gcc-mipsel-linux-gnu`. `lib32gcc-13-dev` is the part of it that is
needed. Those two packages bring some three hundred others with them, the
32-bit libraries SDL stands on, and update a few that are installed in
both sizes.

For Windows, built with MinGW in WSL or on Linux,
`build/port-sdl/windows/rebushido.exe` with `SDL2.dll` beside it:

```bash
sudo apt install gcc-mingw-w64-x86-64 gcc-mingw-w64-i686
mkdir -p tools/sdl2-mingw
curl -L -o /tmp/SDL2-mingw.tar.gz \
    https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
tar -xzf /tmp/SDL2-mingw.tar.gz -C tools/sdl2-mingw
bash tools/build.sh port-sdl-windows
```

That file is SDL's own development package for MinGW, from the SDL
project's releases: 13,892,870 bytes, SHA-256
`83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2`. It
holds a half for each size, `x86_64-w64-mingw32/` and
`i686-w64-mingw32/`, and each build takes its own. Any
SDL 2 from 2.0.18 on will do (SDL 3 is another library). `tools/sdl2-mingw/`
is ignored by git; `SDL2_MINGW=/path/to/SDL2-2.x.y` names another place, and
`build/sdl2-mingw/` is looked in as well (`make clean` empties that one).
Nothing of SDL is in this repository.

`python3 tools/port_sdl_build.py --host [--windows] [--bits N]` says
whether the machine can build it, and what is missing if not.

## The remastered renderer, with nothing shown

The port's remastered picture ([`docs/PORT.md`](docs/PORT.md),
"Remastered") is drawn by a graphics card, and it is proved by a
program that uses no window and no display at all: it takes an OpenGL
4.5 core context from EGL's surfaceless platform and draws into a
framebuffer object, so a machine with no card draws it with Mesa's
software driver.

```bash
python3 tools/port_gl_build.py                  # build/port-gl/rebushido-gl
python3 tools/port_gl_prove.py --image "Bushido Blade 2 (USA).cue"
# the pictures it draws between the game's frames (docs/PORT.md,
# "More pictures a second"), into build/fps/
python3 tools/port_fps_prove.py --image "Bushido Blade 2 (USA).cue"
# the stage's ground drawn as one mesh, against the game's own tiles,
# stage by stage (docs/PORT.md, "The ground as one mesh"), into
# build/stagemesh/: the verdict a stage keeps the mesh by is
# build/stagemesh/verdict.txt
python3 tools/port_stage_prove.py --image "Bushido Blade 2 (USA).cue"
```

On Ubuntu it needs `lib32gcc-13-dev` and 32-bit EGL (`libegl-dev:i386`),
which the window's own packages bring; `mesa-libgallium` is the software
driver. No check uses it, and nothing of it is in `make port` or in the
build with a window. `python3 tools/port_gl_build.py --host` says
whether the machine can build it.

## What is checked

GitHub Actions runs eleven checks on every push and pull request
(`.github/workflows/build.yml`), each a step of its own, and one that fails
does not stop the rest: `decomp`'s seven, on the source with every switch of
ReBushido off, three for the ReBushido build, and one for the native port.
All of them locally, in the same order, with a summary at the end (about
three and a half minutes):

```bash
bash tools/build.sh checks        # or `make checks`; from Windows, `wsl bash tools/build.sh checks`
```

| Check | Command | Fails when |
|---|---|---|
| Counts | `python3 tools/count.py --check` | a `FAKE` note or a `goto` in the source and its row in `docs/FAKES.md` disagree |
| Lint | `python3 tools/lint.py --check` | a file has more of something on `docs/STYLE.md`'s checklist than `tools/lint_baseline.tsv` |
| Modern | `make modern` | the host's `gcc` reports something of a serious kind (a read of something never set, an index past an array) that `tools/modern/baseline.tsv` does not have |
| Tests | `make test` | a function of the game's no longer computes what it did ([`tests/README.md`](tests/README.md)) |
| The game | `make check` | the executable's SHA1 is not the original's |
| The overlay | `make movovl` | the movie overlay's SHA1 is not the original's |
| Shift | `make shift` | something depends on where code or data is |
| ReBushido, the build | `make rebushido` | the ReBushido build does not compile, link or fit ([`docs/REBUSHIDO.md`](docs/REBUSHIDO.md)) |
| ReBushido, tests | `make rebushido-test` | a host test fails with ReBushido's master switch on; the native port's C is built and tested in this run ([`docs/PORT.md`](docs/PORT.md)) |
| ReBushido, modern | `make rebushido-modern` | the host's `gcc` finds something serious and new with the master switch on, the port's C included |
| The port | `make port` | the native port does not link, with 64-bit pointers or with 32-bit ones: a name the game's C uses is defined nowhere, one of the port's own files has a warning, or a number is made a pointer without `ADDR()`; or a structure is laid out otherwise in one of the two programs or on the PlayStation and `config/port_layout.txt` does not name it; or the port has a place where the order an expression is evaluated in may matter that `config/port_order.txt` does not have ([`docs/PORT.md`](docs/PORT.md), "64 bits", "What the PlayStation's compiler decided") |

The first four and the last three need no build of the game and nothing but `python3` and the
host's `gcc`: no virtualenv, no 32-bit packages (`-m32` is used, but nothing
is linked against a library). CI has no disc, so there two of the thirteen
tests are skipped, saying so, and two more do the part that needs none; with
the disc in the checkout all thirteen run. `make modern`'s baseline is gcc
13.3's (Ubuntu 24.04's, which CI runs on too); under another gcc it says so,
since another analyser finds other things. Not checked by CI: anything that
needs the game running, and the source's layout
(`python tools/format.py --check --all`, which needs clang-format from
`requirements.txt`).

## The gate

What a change to `main` passes before it is pushed: the eleven checks,
the window's two builds, the play tests' discs, the layout, the port's
rollback self-test, `make port-net`, the window's budget
(`tools/port_budget.py`: a fight's processor time with `run_ahead 2`
against `run_ahead 0`, with the HD fighters; docs/PORT.md, "The
budget"), the bench on the graphics card (`tools/port_bench.py --budget
--report-only`: the Windows build at 2560 by 1440 on the PC's own card,
the processor's and the card's time of a fight held to 144 pictures a
second; docs/PORT.md, "Measuring on the card"; it reports what is over
and passes until the targets are met, and skips itself where there is
no card, as in CI) and the play tests. One command runs
it all, from Windows or inside WSL, each step as soon as the steps whose
outputs it reads have passed and side by side with the others:

```bash
python tools/gate.py                 # every step, and a summary
python tools/gate.py --owner --push  # then the owner's copy (tools/owner_copy.py) and git push
python tools/gate.py --changed main  # only the play tests the change reaches (tests/play/README.md)
python tools/gate.py --list          # the steps and what each waits for
```

The checks are read from `tools/checks.sh`, so the list is CI's. What
each step waits for is `WAITS` in `tools/gate.py`: a step waits for the
steps it reads and for any that write where it writes (the relink and
the GTE probe for the matching build; the quick-boot discs for
`make rebushido`, which compiles into `build/quickboot/` too; the
ReBushido host tests for the probe, which builds three of them). The
headers several builds write (`build/modern/include/`,
`build/port/include/`) are written whole and only when they change, so
that builds side by side never read one half written. `port-net` takes
96 UDP ports of 127.0.0.1 from a base chosen by the checkout's path
(`--net-base`), so that two checkouts' gates do not meet; the play tests
run four at once (`-j`). The builds and checks run in the copy on WSL's
disk ("Where the build runs"), each bringing `build/` back as it ends, so
the play tests and the steps that run the programs (`IN_PLACE`) find them
here. Each step's output is `build/gate/<step>.log`
and the summary `build/gate/summary.txt`; after the first failure no step
is started, those running finish, and the summary says which were not
run. `port-budget` and `port-bench` run last, one after the other, with
nothing beside them (`ALONE`).

The bench alone, from Windows' Python on the PC with the card, after
`wsl bash tools/build.sh port-sdl-windows`:

```bash
python tools/port_bench.py                      # every case, a report
python tools/port_bench.py --case ra1           # one case (ra0 ra1 ra2 ra1-plain ra1-stress)
python tools/port_bench.py --budget             # the gate's cases against the limits
python tools/port_bench.py --case ra1 --against main   # and against main's numbers
```

Its runs are in `build/bench/runs/<case>/` (each with `profile.txt`,
which `tools/port_profile.py` reads), and every result is kept by commit
in the main checkout's `build/bench/<commit>/results.json`, so that a
worktree compares itself to `main` without running `main` again. With
`--budget`, a blank of a fight over 50 ms after its first second (a
stall) fails even with `--report-only` (docs/PORT.md, "Measuring on
the card").

The stall hunter, the same way: long sessions through the menus and
fights on six stages, every blank over 25 ms named with the functions
the window's thread was in (docs/PORT.md, "Stalls"):

```bash
python tools/port_stalls.py                     # every session; build/stalls/report.txt
python tools/port_stalls.py --session game      # one (--list: the sessions)
python tools/port_stalls.py --reuse             # the runs already made, read again
```

## How a file is built

```
src/**/*.c  ->  mipsel-linux-gnu-cpp  ->  cc1 (GCC 2.7.2)  ->  maspsx  ->  mipsel-linux-gnu-as  ->  .o
```

- **cc1** is the compiler proper, built from source
  ([`docs/COMPILER.md`](docs/COMPILER.md)). The flags are in the `Makefile`;
  `-mel` and `-msoft-float` must stay.
- **maspsx** rewrites the compiler's assembly the way Sony's assembler, ASPSX
  2.34, would have assembled it (how `div`, `lb` and loads from a symbol
  expand, what is reached through `$gp`).
- Every file of the game is compiled with a small-data threshold of 8
  (`-G8`). A handful of files need different flags, and the `Makefile` lists
  them by name: `PSYQ_LIBRARY_FILES` (Sony's library code, no small data),
  `DATA_ONLY_NO_SDATA_FILES` (the game's data files, the same), `ASM_FILES`
  (the three files of hand-written assembly), `NOWARN_FILES`. A file that is renamed or split
  must take its entries along; the build stops if a list names a file that is
  not there.
- Functions that were assembly in the original are included from
  `asm/funcs/<name>.s` by `INCLUDE_ASM` in the C file that owns them.
- `asm/header.s` is assembled as it is.
- A global with no initialiser gets a section of its own in its object,
  `.gnu.linkonce.b.<name>` (maspsx `--comm-own-sections`), and `bb2.ld` lists
  those sections in the original's order. **A new one needs a line there**;
  without it the link fails with "defined in discarded section". The order
  is the original linker's, which goes by the variable's name, and the
  section's alignment is that linker's too, by the variable's size
  (`docs/FAKES.md`, "The zeroed variables").
- A string constant of eight bytes or fewer is small data too (maspsx moves
  it there, as Sony's compiler did), and at `-G8` the compiler writes all of
  a file's other strings before its jump tables. So a string in the code is
  written as a literal where it is used.
- A `static` with no initialiser needs no line: it is in its file's `.sbss`
  (eight bytes or fewer) or `.bss`, in the order the file declares it. So the
  order in which a file declares its `static`s is part of the match.

The objects are linked with `mipsel-linux-gnu-ld -T bb2.ld -T config/named_syms.txt`.
`bb2.ld` lists every object once per section, in the original's order, and is
maintained by hand: adding, renaming or splitting a source file means editing
it; no tool writes it, and nothing regenerates the listings in `asm/funcs/`.
(The ReBushido build links by a script written from `bb2.ld`, and a file of
`src/rebushido/` needs no line in either: [`docs/REBUSHIDO.md`](docs/REBUSHIDO.md),
"How it links".)
`objcopy` takes the image out of the ELF and `tools/make_psexe.py` puts the
0x800-byte PS-X EXE header in front of it.

## Line endings

Every build file (`*.c`, `*.h`, `*.s`, `Makefile`, `*.ld`, the `.txt` files
the build reads, `*.sh`) must have LF line endings. The GNU tools do not
complain about CRLF; they produce wrong output or strange errors.
`.gitattributes` keeps a checkout right (`config/` included), but a Windows editor or a Windows
Python script writing in text mode will put CRLF back. `python3
tools/normalize_lf.py <file>...` repairs a file.

## The disc, for the asset tools

The inspectors and repackers in `tools/` (see
[`docs/formats/README.md`](docs/formats/README.md)) work on the files of the
disc. With your own image of the North American release (`Bushido Blade 2
(USA).bin` and `.cue`, one MODE2/2352 track) in the checkout:

```bash
python3 tools/extract_iso.py "Bushido Blade 2 (USA).bin" disc
```

writes the disc's files under `disc/`: `SLUS_006.63`, `STR/MOVOVL.EXE` and the
asset folders. `disc/SLUS_006.63` should have the SHA1 above. Everything under
`disc/`, and any `*.bin` / `*.cue`, is ignored by git. `tools/repack_tim.py`
and `tools/inspect_tim.py --png` also need Pillow (in `requirements.txt`).

`tools/build_disc.py` goes the other way: the original image plus a rebuilt
executable and replaced files makes a new image.

## When it does not work

| What you see | Why | What to do |
|---|---|---|
| `MISMATCH: bb2 does not match` on an untouched checkout | A different binutils release, or a compiler not built by `tools/build_cc1.sh` | Check `mipsel-linux-gnu-as --version` (2.42 on Ubuntu 24.04); move `tools/gcc-2.7.2` away and rebuild it with `tools/build_cc1.sh` |
| `MISMATCH` after an edit | The edit changed the generated code | `python3 tools/objdiff.py <old>.o <new>.o` names the functions whose code changed, against a copy of the object kept from a matching build; [`docs/MATCHING.md`](docs/MATCHING.md), "The loop" |
| `tools/gcc-2.7.2/build/cc1: No such file` | The compiler is not built | `bash tools/build_cc1.sh` |
| `mipsel-linux-gnu-as: not found` | The cross binutils are not installed | `sudo apt-get install binutils-mipsel-linux-gnu cpp-mipsel-linux-gnu` |
| `.venv/bin/activate: No such file` | No virtualenv | `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` |
| Odd assembler errors, or wrong bytes, after editing from Windows | CRLF line endings | `python3 tools/normalize_lf.py <file>` |
| `Failed to start the systemd user session` from `wsl` | Nothing; WSL prints it | Ignore it |
| A `.sh` fails with `required file not found` | CRLF in the script | `python3 tools/normalize_lf.py tools/*.sh` |
