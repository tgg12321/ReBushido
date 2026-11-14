# MOVOVL.EXE: the movie overlay

`disc/STR/MOVOVL.EXE` is the small separate program the game runs to play the
opening movie. This folder rebuilds it byte for byte. It is its own build:
nothing here is part of the main executable or its check.

```bash
wsl bash tools/build.sh movovl      # clean build + SHA1 check, from the repo root
make -C movovl                      # the same without the clean, inside WSL
```

`OK: movovl matches!` means `build/MOVOVL.EXE` has the SHA1 in `movovl.sha1`
(`a1307dbebefca0b057e02509207d00f6225e13e4`, 124,928 bytes). The build needs
no disc file. It uses the main build's compiler and maspsx (`../tools/`), the main
build's Sony headers (`../include/psxsdk/`) and the main build's source for
the Sony library modules the two programs share (`../src/main/psxsdk/`).

## What it is

A PS-X EXE loaded at `0x801D8800`, entry `0x801DA084`. 16 functions of its
own (3,832 bytes) and 39,892 bytes of Sony library code (libcd, libpress,
libgpu, libetc, libapi, libc2, libds).

The game's part is Sony's streaming-movie sample with a title screen around
it: play the movie through the MDEC in 24-bit colour, switch the display to a
still picture from a given frame on, hold it, fade it out. How the game
launches it and what it passes is in `docs/formats/MOVOVL.md`.

## Layout

| Path | What |
|---|---|
| `src/main.c` | `main`: set-up, the play loop, the still picture and its fade |
| `src/movie.c` | the movie player: CD stream, bitstream decode, MDEC, frame upload |
| `include/movie.h` | the player's interface and the two buffers the game lends it |
| `include/sdk.h` | Sony declarations the main game's headers do not have |
| `asm/start.s` | `__main` and the entry stub (hand-written in the original) |
| `../src/main/psxsdk/` | the 43 Sony library modules this program shares with the main executable, built from the main build's source into `build/sdk/` (the list is `SDK_MODULES` in the `Makefile`) |
| `sdk/` | the 14 compiled library modules only this program has (`SDK_OWN_MODULES`): the streaming half of libcd, libpress's MDEC driver, libetc's old pad interface, libds's callbacks |
| `asm/libpress_vlc.s`, `asm/libapi_a22.s` | the two assembly library modules only this program has: the bitstream decoder `DecDCTvlc`, and a BIOS stub |
| `asm/data/libpress_vlc.data.s` | `DecDCTvlc`'s tables |
| `asm/funcs/dma_execute.s` | one function of `sdk/libcd/c_011.c` that would not match in C |
| `asm/header.s` | the 0x800-byte PS-X EXE header |
| `movovl.ld` | link order, hand-maintained; it also places every library variable that has no initial value |
| `undefined_syms.txt` | an address inside `DecDCTvlc` that its own code refers to |

Of the 59 library modules, 43 are byte for byte the modules the main
executable links (apart from the addresses in them), so they are built from
the same source: C where Sony's module was compiled, a listing where it was
assembly. The other 16 are not in the main executable; 14 of them were
compiled and are C in `sdk/`, 2 were assembly and stay so.

## Notes for whoever edits it

- Build files are LF, as in the main build.
- A change to a file of `../src/main/psxsdk/` that this program links changes
  this program too: run both checks. Renaming one of its uninitialised
  variables means renaming its `.gnu.linkonce.b.<name>` line in `movovl.ld`
  as well as the one in `bb2.ld` (`tools/rename.py` does both).
- The C is compiled with a small-data threshold of 8. A variable of 8 bytes or
  less is reached through `$gp` only in the file that defines it, which is why
  `g_movie_still_reached` lives in `main.c` and `movie.c` reaches it by
  address.
- `movovl.ld` puts the C files' small bss inside the loaded image (the file is
  padded with zeroes to a 2048-byte boundary and the small bss falls in the
  padding) and their bss just after it at `0x801F6800`.
- Three things are shaped by the bytes and not understood: a word of padding
  at the end of the decoder state (`unk24`), eight unused bytes of locals in
  `movie_DecodeFrame`, and the word of padding between the small bss and the
  libraries' bss (`ALIGN` in `movovl.ld`).
