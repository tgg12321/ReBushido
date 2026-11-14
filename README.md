# ReBushido

A decompilation of *Bushido Blade 2* (PlayStation, 1998, SLUS-00663), and the
base for ReBushido, a modded version of the game.

- **`main`** is the decompilation. It builds the original executable byte for
  byte (`SLUS_006.63`, SHA1 `62efab4f73f992798c43e8c730aa43baa10bb4fa`) and
  nothing on it changes how the game behaves.
- **`rebushido`** is the mod: bug fixes and enhancements. Early days.

> **This is the `rebushido` branch.** Every change of ReBushido's is behind
> a switch; with every switch off this branch still builds the original
> executable byte for byte, and the checks hold it to that. What ReBushido
> is, how to build and play it and how to write a change:
> [`docs/REBUSHIDO.md`](docs/REBUSHIDO.md). What is planned:
> [`docs/ROADMAP.md`](docs/ROADMAP.md). As it stands the branch is the
> foundation only, and plays as the original.
>
> ```bash
> bash tools/build.sh rebushido        # the ReBushido build; "OK: rebushido fits"
> bash tools/build.sh rebushido-disc   # and a disc image to play, from your retail image
> ```

No game files are in this repository. To play anything built from it you need
your own copy of the North American disc.

## Status

All 1,484 functions are decompiled and the build matches. The source has been
reorganised and renamed to be readable, but the names are ours, not the
original developers', and some are guesses. About seventy probable bugs in the
original are documented and left as they are on `main`.

[`docs/STATUS.md`](docs/STATUS.md) has the details, including what was tested
and what was not.

## Building

Linux, or Windows with WSL 2 (Ubuntu 24.04). The build needs no game files.

```bash
bash tools/setup_wsl.sh      # once: toolchain and compiler
bash tools/build.sh check    # ends with "OK: bb2 matches!"
bash tools/build.sh checks   # everything CI runs
```

[`BUILD.md`](BUILD.md) covers the rest.

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): how the game works
- [`docs/formats/`](docs/formats/README.md): the disc's file formats and the tools for them
- [`docs/STATUS.md`](docs/STATUS.md): state, suspected bugs, open questions
- [`docs/FAKES.md`](docs/FAKES.md): code that matches but is probably not what the authors wrote
- [`docs/STYLE.md`](docs/STYLE.md), [`docs/MATCHING.md`](docs/MATCHING.md),
  [`docs/COMPILER.md`](docs/COMPILER.md): for working on the source
- [`docs/GLOSSARY.md`](docs/GLOSSARY.md), [`docs/HISTORY.md`](docs/HISTORY.md)

## AI use

Most of this code was written by AI coding agents (Anthropic's Claude) under
one person's direction. That is how a project this size got done by a single
maintainer, and it is also a reason to read it critically.

The part a machine can check is solid: every function is compiled with the
game's original compiler and compared with the retail executable, on every
push. The part it cannot check is softer. Whether a name is right, or a
comment describes what the game really does, was mostly decided by an AI
reading the code, with some confirmation in an emulator and by playing. Treat
names and comments as a careful reading, not as fact, and say so if you find
one that is wrong.

## Credits

Square and Lightweight made the game; Sony made the SDK it was built with.
This project depends on [mips-gcc-2.7.2](https://github.com/decompals/mips-gcc-2.7.2)
and [maspsx](https://github.com/mkst/maspsx), started from
[splat](https://github.com/ethteck/splat), and learned a great deal from
[sotn-decomp](https://github.com/Xeeynamo/sotn-decomp) and the other
PlayStation decompilation projects. The full list is in [`NOTICE`](NOTICE).

## Licence

The project's own work is under the
[GNU Affero General Public License v3 or later](LICENSE). *Bushido Blade 2*
itself belongs to its rights holders, and this project is not affiliated with
them; [`NOTICE`](NOTICE) says what the licence does and does not cover. The
source was written from the published game's machine code, not from any
leaked material, and contributions must be made the same way
([`CONTRIBUTING.md`](CONTRIBUTING.md)).

If you hold rights in the game and have a concern, please open an issue.
