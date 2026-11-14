# MOVOVL.EXE — FMV / MDEC playback overlay

File: `disc/STR/MOVOVL.EXE` (124,928 bytes).

A standalone, self-contained PS-X EXE that plays the BB2 title-screen and
opening cinematics (the stream is `disc/STR/OPENING.STR`). The main game
executable
(`disc/SLUS_006.63`) launches it through BIOS `Exec` and its code stays resident (the
overlay's buffers overwrite game RAM at `arena_base` and
`0x80118000..0x801D8800`); the
overlay re-initialises the pad / interrupt / GPU environments from scratch
and returns to the game when it finishes (see "Launch, argv and exit" below).

## Header

Standard PS-X EXE header (the same 0x800-byte layout used by the main EXE
and re-prepended by `tools/make_psexe.py` during the matching build):

| Field            | Value                                            |
|------------------|--------------------------------------------------|
| Signature        | `"PS-X EXE"` (offset `0x00`)                     |
| Marker text      | `Sony Computer Entertainment Inc. for Japan area`|
| Entry point (PC) | `0x801DA084`                                     |
| Initial `$gp`    | `0x00000000` (set by the entry code, not header) |
| Text load addr   | `0x801D8800`                                     |
| Text size        | `0x1E000` (122,880 bytes)                        |
| Data / BSS       | not declared in header (`addr=0, size=0`)        |
| Initial `$sp`    | `0x801FFFF0` in the header; the game's launch passes `s_addr` = `memcard_GetSharedBuffer() + 0x7FC`, `s_size` 0 (`sys_Exec`, `src/main/cdrom.c`), which BIOS `Exec` presumably uses instead: the player's stack is the first 0x800 bytes (`MOVIE_STACK_SIZE`) of the game's shared buffer, and `s_addr` is the last word of them |

The text region runs `0x801D8800 .. 0x801F6800`. The overlay's own BSS
(decoder state at `0x801F6800`, `g_dec_storage` in `movovl/src/movie.c`) lies after the text region from `0x801F6800`;
its big buffers (stream ring, MDEC work area) are supplied by the game in
argv words 2 and 3 (see "Launch, argv and exit").

## Entry-point bootstrap

```
801DA084  lui   $gp, 0x801F
801DA088  addiu $gp, $gp, 0x65D0     ; gp = 0x801F65D0
801DA08C  j     0x801D91CC           ; main()
801DA090  nop
```

`$gp = 0x801F65D0` puts the small-data window at `0x801F5DD0 .. 0x801F6DD0`
(the overlay's decoder state lives just past the text segment, inside
this window). Control then jumps to the C runtime's `main()` at
`0x801D91CC`.

## Library identification

Three CVS `$Id:` tags survive inside the linked PsyQ object files —
the same versions of these three modules as in the main EXE, whose
libraries are PsyQ 4.0-era:

| Library    | CVS tag                                            |
|------------|----------------------------------------------------|
| libgpu     | `sys.c v1.129 1996/12/25 03:36:20 noda`            |
| libcd      | `bios.c v1.86 1997/03/28 07:42:42 makoto`          |
| libetc     | `intr.c v1.76 1997/02/12 12:45:05 makoto`          |

The trailing string `Library Programs (c) 1993-1997 Sony Computer
Entertainment Inc., All Rights Reserved.` at `0x801F5488` is the
standard PsyQ runtime copyright.

The overlay also contains characteristic libpress debug strings
(`MDEC_in_sync`, `MDEC_out_sync`, `MDEC_rest:bad option(%d)`). The
`size of DECENV_R = 0x%08X` / `size of DECENV_B = 0x%08X` strings are not
libpress: the overlay's own `movie_SetArgs` prints them
with the stream-ring and work-area sizes (see "Launch, argv and exit"). MDEC register access (the `0x1F801820` / `0x1F801824`
hardware ports) does NOT appear inline anywhere — the overlay always
goes through these library wrappers.

## Code-side characterisation

`tools/inspect_movovl.py` performs heuristic static analysis. Current
findings (subject to change as the overlay is decompiled further):

| Metric           | Value                                                |
|------------------|------------------------------------------------------|
| Detected funcs   | 141 (counted by `addiu $sp, -N` + nearby `sw $ra`)   |
| BIOS call sites  | 17 (canonical `li $t2, TBL; jr $t2; li $t1, FN`)     |
| ASCII strings    | 516 (>= 4 chars; includes debug `printf` formats)    |

### BIOS calls

The 17 BIOS calls cover the minimal environment setup an overlay needs
when launched in isolation:

| Table | Func | Name                            | Count | Use                            |
|-------|------|---------------------------------|------:|--------------------------------|
| 0xA0  | 0x44 | `FlushCache`                    | 1     | cache invalidate after load    |
| 0xA0  | 0x49 | `GPU_cw`                        | 1     | low-level GPU command word     |
| 0xA0  | 0x72 | (unidentified)                  | 1     | likely `CdInit` or `CdEnable`  |
| 0xB0  | 0x07 | `DeliverEvent`                  | 1     | event-driven I/O               |
| 0xB0  | 0x12 | `InitPAD`                       | 1     | pad input init                 |
| 0xB0  | 0x13 | `StartPAD`                      | 1     | begin pad polling              |
| 0xB0  | 0x14 | `StopPAD`                       | 1     | pre-shutdown                   |
| 0xB0  | 0x15 | (unidentified)                  | 1     | likely `PAD_init` companion    |
| 0xB0  | 0x16 | (unidentified)                  | 1     | likely `OutdatedPadGetButtons` |
| 0xB0  | 0x17 | `ReturnFromException`           | 1     | exception ret                  |
| 0xB0  | 0x18 | `SetDefaultExitFromException`   | 1     | exception setup                |
| 0xB0  | 0x19 | `SetCustomExitFromException`    | 1     | exception setup                |
| 0xB0  | 0x35 | (unidentified)                  | 1     | likely `_card_*` BU-related    |
| 0xB0  | 0x5B | (unidentified)                  | 1     | likely `_card_chan`            |
| 0xC0  | 0x02 | `SysEnqIntRP`                   | 1     | install IRQ handler            |
| 0xC0  | 0x03 | `SysDeqIntRP`                   | 1     | remove IRQ handler             |
| 0xC0  | 0x0A | `ChangeClearRCnt`               | 1     | reset RCnt (root counter)      |

(Names tagged "unidentified" lie outside the small built-in name table
in `tools/inspect_movovl.py` — they are still valid BIOS indices, just
ones the tool doesn't yet have human names for.)

The overlay never calls into the main game (no shared symbol table is
loaded) and never re-loads the main EXE: `main()` returns, and BIOS `Exec`
returns to the game (see "Launch, argv and exit").

## Relationship to the main EXE

`eff_Init` (`src/main/main.c`) loads data into the same address
window (it does not load MOVOVL.EXE; see below):

```c
// src/main/main.c
void eff_Init(void) {
    ...
    size = eff_LoadTextures(SYS_PRIM_BUFFER_0, SYS_EFFECTS_ADDR);   /* 0x801D8800, 0x8010E800 */
    printf("eff_init:%08x size:%08x\n", SYS_EFFECTS_ADDR, size);
    if (SYS_EFFECTS_SIZE < size) sys_Panic();                        /* 0xA000 */
    ...
}
```

`eff_LoadTextures(buffer, quads)` (`src/main/eff.c`) reads the effect file into
`buffer`, uploads its two images to VRAM and carves the effect quad buffers from
`quads` (it returns the end of the quad buffers,
not a size). The file it reads is one of the data group of `g_cdrom_files`
(`cdrom_GetFileIndex(CDROM_GROUP_DATA, …)`), so `eff_Init` loads a *different* file (the
in-game effects images) into the same address window; MOVOVL.EXE is loaded
by `cdrom_LoadExec` from the movie group, `CDROM_GROUP_MOVIE` (next section).

The 0x801D8800 region is also used by the main game as a scratch buffer:

```c
// src/main/main.c, snd_Startup - stages sound data there
size = snd_LoadCommonVab(SYS_PRIM_BUFFER_0);                       /* 0x801D8800 */
if (size > SYS_SOUND_VAB_SIZE) sys_Panic();                         /* 0xD00 */
memcpy((u8 *)SYS_SOUND_VAB_ADDR, (u8 *)SYS_PRIM_BUFFER_0, size);   /* 0x8010DB00 */

// src/main/main.c, sys_InitGame - registers the region as a buffer
g_disp_prim_buffers[0] = SYS_PRIM_BUFFER_0;      // 0x801D8800
g_disp_prim_buffers[1] = SYS_PRIM_BUFFER_1;      // 0x801EBC00
g_disp_prim_buffer_size = SYS_PRIM_BUFFER_SIZE;  // 0x13400 = 78,848 bytes
```

`0x801EBC00 - 0x801D8800 = 0x13400 = 78,848 bytes` — the main game
registers only the first 78 KB of the region as this buffer (the effects
images above use more of it).

## Launch, argv and exit

The filename `MOVOVL.EXE` is not in the main EXE: the game finds it by
index in its baked disc file table `g_cdrom_files` (group 6,
`CDROM_GROUP_MOVIE`, whose three files are `enum CdromMovieFile` in
`include/cdrom.h`: `CDROM_MOVIE_PLAYER`, `STR/MOVOVL.EXE`;
`CDROM_MOVIE_OPENING`, `STR/OPENING.STR`; `CDROM_MOVIE_TITLE`,
`STR/TITLE.TIM`).

**Load.** `sys_MovieScreen` (`src/main/fight_after.c`)
calls `sys_ExecMovieOverlay(arena_GetBase(), SYS_MOVIE_WORK_ADDR,
OPENING_MOVIE_FILE, OPENING_MOVIE_END_FRAME, OPENING_MOVIE_STILL_FRAME)`:
`0x80118000`, file 1 of the group (`CDROM_MOVIE_OPENING`), frames 3320 and
2817. `sys_ExecMovieOverlay` (`src/main/cdrom.c`) fills a six-word argv
(`MOVIE_ARG_...`) and
calls `sys_Exec(6, argv, memcard_GetSharedBuffer() + 0x7FC)`; `sys_Exec`
(`cdrom.c`) shuts down sound, memcard and graphics, has
`cdrom_LoadExec` (`cdrom.c`) read the player's header sector
and `t_size` bytes to `t_addr`, then runs BIOS `Exec(&exec, 6, argv)`. The
entry stub `j`s (not `jal`s) to `main`, so `main(argc = 6, argv)` returns
straight into `Exec`.

**argv.** `main` (`movovl/src/main.c`) takes argv as a `MovieArgs` and hands
words 0, 1, 2, 3, 5 to `movie_SetArgs` (`movovl/src/movie.c`), which copies
them to globals:

| Word (`MOVIE_ARG_...`) | Main-EXE value (`cdrom.c`) | `MovieArgs` member | What the overlay does with it |
|-----:|----------------------------|--------------------|-------------------------------|
| 0 | the `g_cdrom_files` entry of `CDROM_MOVIE_OPENING` (file 1 of group 6, `STR/OPENING.STR`) | `movie_file` | copied whole as a 0x18-byte Sony `CdlFILE` (`g_movie_file`) from an 8-byte `CdromFile` (the table is in `src/main/cdrom.c`, the type in `include/cdrom.h`), so it takes that entry plus the next two; only the `CdlLOC` at +0 is used afterwards: `movie_StartStream` passes it to `CdControlB(CdlSetloc, ...)`: the movie stream's start position |
| 1 | `end_frame` = `0xCF8` (3320) | `end_frame` | in `movie_NextFrame` a sector whose `StHEADER` frame count is >= it sets the decoder state to `MOVIE_ENDED`, which makes `movie_PlayFrame` stop playback (`DecDCToutCallback(0)`, `StUnSetRing`, `StClearRing`, `CdControlF(CdlPause)`) |
| 2 | `arena_base` = `arena_GetBase()` | `ring` | stream ring buffer (`MovieRing`, 32 sectors, 0x10000 bytes): `StSetRing`; `movie_Start` zeroes it |
| 3 | `work_buffer` = `0x80118000` | `work` | read first by `main`: `CdRead(0x4C, work, CdlModeSpeed)` of the word-4 file, then `LoadImage(&g_still_rect, work + 0x14)`: a 320x240 16-bpp pixel block after a 0x14-byte header (a TIM, by the offset and TITLE.TIM's size; not decoded) at VRAM x 480. Then the decode work area (`MovieWork`): two run-level buffers of 0x28000 bytes (`DecDCTvlc`, `DecDCTin`) and two 320x240 24-bpp frames (0x38400 bytes each; `DecDCTout`; `movie_ShowFrame` uploads them in 20 strips of 24 VRAM words x 240 rows); `movie_Start` zeroes it. Size 0xC0800, so it ends at 0x801D8800, the overlay's load address |
| 4 | the `g_cdrom_files` entry of `CDROM_MOVIE_TITLE` (file 2 of group 6, `STR/TITLE.TIM`) | `still_loc` | `main` only, as a `CdlLOC`: `CdControl(CdlSetloc, still_loc, 0)` before the `CdRead` above: the still-image file (0x4C sectors = 0x26000 bytes; `disc/STR/TITLE.TIM` is 153,620 = 0x14 + 320*240*2 bytes, the size the table's entry has) |
| 5 | `still_image_frame` = `0xB01` (2817) | `still_frame` | in `movie_NextFrame` a sector with frame count >= it (and < word 1) sets `g_movie_still_reached`; `main`'s loop then, once, points the display at VRAM x 480 (the word-3 image) with `SetDefDispEnv`/`PutDispEnv`, switching it from 24-bit to 16-bit, while the movie keeps decoding to the end frame |

The two `printf`s in `movie_SetArgs` print `size of DECENV_R = 0x00010000`
and `size of DECENV_B = 0x000C0800`: `sizeof(MovieRing)` and
`sizeof(MovieWork)`.

**Exit.** `main` leaves its play loop three ways:

* a new press of pad bit 0x40 (cross): straight to the shutdown, no fade;
* `movie_PlayFrame` returns 0 and `movie_EndedNormally` returns 0 (the
  stream errored: decoder state `MOVIE_ERROR`, set on `Cd error!!!` or
  `strNext is NULL`; `movie_PlayFrame` then mutes the CD itself with
  `sys_SetCdVolume(0, 0, 0, 0)` and waits on `CdReady` /
  `CdControlF(CdlStandby)`): same, straight to the shutdown;
* normal end (`MOVIE_ENDED`): `main` mutes the CD audio
  (`sys_SetCdVolume`, the `CdMix` wrapper), holds up to 599 VSyncs (a cross
  press skips the rest and the fade), then fades out in 64 steps (a
  subtractive `POLY_G4` over the still image).

The shutdown clears the VRAM rect (0, 0, 480, 480) (`g_clear_rect`), calls
`PadStop`, `ResetGraph(3)`, `StopCallback` and returns.
`Exec` returns to `sys_Exec`, which re-initialises the game (`sys_Init`,
`snd_Startup`);
`sys_MovieScreen` then runs `sys_InitEngineState` and returns
to the menu.

## Source and build

The overlay's own code (16 functions, 3,832 bytes) is decompiled:
`movovl/src/main.c` and `movovl/src/movie.c`; the Sony libraries (91% of the
code) stay assembly. `make -C movovl` (or `wsl bash tools/build.sh movovl`)
rebuilds `MOVOVL.EXE` byte for byte. See `movovl/README.md`.

## Inspector

```
python tools/inspect_movovl.py disc/STR/MOVOVL.EXE              # summary
python tools/inspect_movovl.py disc/STR/MOVOVL.EXE --strings    # 516 strings
python tools/inspect_movovl.py disc/STR/MOVOVL.EXE --funcs      # 141 prologue addrs
python tools/inspect_movovl.py disc/STR/MOVOVL.EXE --syscalls   # 17 BIOS calls with names
```

For full MIPS disassembly use the WSL toolchain (`mipsel-linux-gnu-objdump
-D -b binary -m mips --adjust-vma=0x801D8800 <text-only-blob>`).

## Verified by

* Header parses correctly against the PS-X EXE format used by
  `make_psexe.py`. SHA1 of the original disc file is
  `a1307dbebefca0b057e02509207d00f6225e13e4`; `movovl/` rebuilds it byte
  for byte.
* All three PsyQ `$Id:` tags match the versions documented for the main
  EXE in AGENTS.md (libgpu sys.c v1.129, libcd bios.c v1.86, libetc
  intr.c v1.76).
* The 17 BIOS call sites use the canonical PSX trampoline pattern
  (`li $t2, 0xa0/b0/c0` then `jr $t2`, function index in `$t1` in the
  delay slot) and decode to sensible names for the identified subset.
* Cross-reference with the main EXE: `0x801D8800` is treated as a
  scratch buffer in `src/main/main.c` (`eff_Init`, `snd_Startup`,
  `sys_InitGame`) while the overlay is not running.
* The libpress debug strings (`MDEC_in_sync`, `MDEC_out_sync`, etc.)
  are PsyQ `libpress` library strings — the overlay does not reimplement MDEC handling, it calls the
  vendor library.

## Unverified / TODO

* Name the remaining 6 unidentified BIOS function indices (`0xa0/0x72`,
  `0xb0/0x15`, `0xb0/0x16`, `0xb0/0x35`, `0xb0/0x5B`). The PSX BIOS
  reference (no-cash docs / SDK headers) lists all of them.
