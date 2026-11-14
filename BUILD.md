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
| `make clean` | Delete `build/` (but not `build/play/`, a play test's disc) |
| `make rebushido`, `rebushido-disc`, `rebushido-test`, `rebushido-modern` | The ReBushido build, into `build/rebushido/`, its disc and its checks ([`docs/REBUSHIDO.md`](docs/REBUSHIDO.md)); they leave the matching build alone |

`tools/build.sh` exits with `make`'s status, so a script can rely on it.
The overlay builds 43 of Sony's library modules from `src/main/psxsdk/`, so
a change there has to pass both byte checks.

## What is checked

GitHub Actions runs ten checks on every push and pull request
(`.github/workflows/build.yml`), each a step of its own, and one that fails
does not stop the rest: `decomp`'s seven, on the source with every switch of
ReBushido off, and three for the ReBushido build. All of them locally, in
the same order, with a summary at the end (about three minutes):

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
| ReBushido, tests | `make rebushido-test` | a host test fails with ReBushido's master switch on |
| ReBushido, modern | `make rebushido-modern` | the host's `gcc` finds something serious and new with the master switch on |

The first four and the last two need no build of the game and nothing but `python3` and the
host's `gcc`: no virtualenv, no 32-bit packages (`-m32` is used, but nothing
is linked against a library). CI has no disc, so there two of the thirteen
tests are skipped, saying so, and two more do the part that needs none; with
the disc in the checkout all thirteen run. `make modern`'s baseline is gcc
13.3's (Ubuntu 24.04's, which CI runs on too); under another gcc it says so,
since another analyser finds other things. Not checked by CI: anything that
needs the game running, and the source's layout
(`python tools/format.py --check --all`, which needs clang-format from
`requirements.txt`).

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
