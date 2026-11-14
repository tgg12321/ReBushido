# ReBushido

ReBushido is a modded line of *Bushido Blade 2*: the original's bugs fixed,
the picture improved as far as a PlayStation can, and new features. It is
built from the matching decompilation and lives on the branch `rebushido`.
`main` stays the byte-matching source and never changes what the program
does.

This page is the branch's rules, how to build and play it, how a change is
written and how it is tested. What is planned, in order, is in
[`ROADMAP.md`](ROADMAP.md). As it stands the branch is the foundation only:
no change to the game is in it yet.

## The rules

1. **Every change sits behind a compile-time switch.** With all switches
   off the branch builds the original executable byte for byte, and the
   checks hold it to that on every push. That is how the branch knows that
   nothing leaked outside its switch.
2. **ReBushido is a PlayStation program.** It has to run on the original
   console and on emulators. A native port is not part of this.
3. **The player chooses at boot.** One "Bugfixes" setting turns every fix
   on; only a fix that changes how the game plays by much gets a setting of
   its own. One setting switches between the original and the enhanced
   picture (a wide picture, the 640 by 480 mode, up to 60 frames a second).
   So most changes ask a setting while the game runs, as well as being
   behind their switch. With nothing chosen everything is as the original.
4. **A release is a patch file** against the player's own retail disc, never
   a disc image. No game data is ever committed.
5. **The licence** is AGPL-3.0-or-later ([`LICENSE`](../LICENSE),
   [`NOTICE`](../NOTICE)).
6. **The version is in the build**: the text `ReBushido <version> (<commit>)`
   is in the executable.

## Building

Set up as [`BUILD.md`](../BUILD.md) says; ReBushido needs nothing more.

```bash
bash tools/build.sh check             # the matching build, as on main
bash tools/build.sh rebushido         # the ReBushido build; ends with "OK: rebushido fits"
bash tools/build.sh rebushido-disc    # and a disc image to play (needs your retail image)
bash tools/build.sh checks            # every check; "OK: all 10 checks pass"
```

From a Windows shell put `wsl` in front, as with the other commands.

| Target | What it does |
|---|---|
| `make rebushido` | Compiles the source with the master switch on, and the files of `src/rebushido/`, into `build/rebushido/`; links `build/rebushido/rebushido.exe`; prints its size and the room left, and fails if it does not fit |
| `make rebushido-disc` | The same, then `build/play/ReBushido.bin` and `.cue`: the retail image with that executable in place of the game's |
| `make rebushido-test` | The host tests with the master switch on, and ReBushido's own (`tests/rebushido/`) |
| `make rebushido-modern` | `make modern` with the master switch on, `src/rebushido/` included |

The two builds share one tree and no object: the matching build is in
`build/` exactly as on `main`, the ReBushido build in `build/rebushido/`.
`make clean` removes both and leaves `build/play/`.

The build ends with a report:

```
build/rebushido/rebushido.exe: ReBushido 0.1-dev (1a2b3c4d5)
  file     606208 bytes; loaded at 0x80010000 to 0x800A3800
  program  ends at 0x80107920, its zeroed variables included
           0x40 bytes more than the matching build (build/bb2.elf)
  room     0x61E0 bytes below the first fixed buffer (0x8010DB00, SYS_SOUND_VAB_ADDR)
           0xD0EE0 below the movie overlay and the primitive buffers (0x801D8800)
           0xF86D0 below the stack's top (0x801FFFF0)
  gp       the small data ends 0x870 from gp, which reaches 0x7FFF
OK: rebushido fits
```

The commit has `-dirty` after it when the tree had changes that were not
committed.

## Playing

`make rebushido-disc` needs your own image of the North American disc,
`Bushido Blade 2 (USA).bin`, in the checkout (`RETAIL_IMAGE=...` names
another place). The image is only read. The result is
`build/play/ReBushido.cue` with its `.bin` beside it: load the `.cue` in an
emulator as any disc. It has the retail serial, so an emulator with a card
per game gives it the retail game's memory card.

`build/play/` is not in git and `make clean` leaves it alone. Other images
there (the shifted disc of the play test, `BB2_shifted.*`) are not touched.

## How much room there is

The program, its zeroed variables included, ends a little under
`0x80108000`. Above it the game keeps buffers at fixed addresses
(`include/sys.h`): the first is the common sound bank at `0x8010DB00`. The
matching program ends `0x6220` bytes (about 25 KB) below it, and that is
what ReBushido's code and data can grow by as things are. `make rebushido`
fails, saying so, when the program would run into that buffer. Past that
the buffers have to be moved up, which is a change like any other: they are
constants in `include/sys.h`.

The movie overlay loads at `0x801D8800`, where the game later keeps its
primitive buffers, and the stack is at the top of memory; the report gives
the room below both.

## How it links

`bb2.ld` is the matching build's linker script. It is written by hand, lists
every object's sections in the original's order, and places each variable
that no file initialises by name. It is not changed for ReBushido, and it
does not know ReBushido's files.

The ReBushido build links by a script of its own,
`build/rebushido/rebushido.ld`, which `tools/rebushido.py` writes from
`bb2.ld` each time: the same script with the objects' paths made the
ReBushido build's, and everything those objects hold that `bb2.ld` does not
place added at the end of its kind.

- A new file's code goes after the last file's code, its read-only data
  after the last file's, and so on for each section.
- A section an old file did not have before (a first small variable in a
  file that had none) is placed the same way.
- A new variable that no file initialises goes after the last of its size
  class: with the small ones, which the code reaches through `gp`, if it is
  of eight bytes or fewer, else with the large ones. That is the original
  linker's own rule.
- Every object's section in the program is put on a 4-byte boundary. In the
  original each one already starts on one, so this moves nothing until a
  file's data changes length; then it keeps the next file from starting off
  a word, which the hand-written script would not.

So **adding a file, a function or a variable needs no edit of a linker
script**. The order of the original stays where `bb2.ld` has it, and
ReBushido's additions follow it. Nothing can be dropped silently: the tool
reads every object, a section of a kind it does not know stops the build,
and after the link the build checks that the linker read every object the
script names. Given only the matching build's objects, the script it
writes links the matching executable, byte for byte.

It was done this way, and not with a second hand-written script, because a
second script would have to follow every change to `bb2.ld` by hand, and
not by editing `bb2.ld` to take additions, because the matching build must
not change. The cost is that the tool finds its places in `bb2.ld` by six
lines of it (`main_RODATA_END = .;` and the like) and two block names. If
one is renamed there, the ReBushido build stops and says which.

That code can move at all is what `make shift` shows on every push: it
relinks the matching objects with padding and fails if anything depends on
an address.

## Writing a change

[`STYLE.md`](STYLE.md) stays in force; its section "ReBushido" has the rules
in short. In full:

The examples here are made up.

**1. Give the change a switch.** One line in `include/rebushido.h`, with a
comment saying what the change is:

```c
/* The replay no longer reads past the end of its ring. */
#define RB_FIX_REPLAY_RING (REBUSHIDO && 1)
```

A fix is `RB_FIX_...`. The switch is 0 whenever the master switch is off.
The `1` can be made `0` to build ReBushido without that change. The source
asks `#if RB_NAME`, never `#ifdef` (the name is always defined).
`tools/lint.py` reports a name asked in the source that is not defined
here: misspelt, it would be 0, and the change silently missing.

**2. Leave the original's lines as they are, once.** They are not deleted,
not copied into ReBushido's side and not reworded: the diff against `main`
should show what was added and nothing else. The shapes:

Something added:

```c
#if RB_WIDESCREEN
    rb_WidenView(view);
#endif
```

Something replaced, with no setting:

```c
#if RB_FIX_REPLAY_RING
    index = (index + 1) % REPLAY_RING_SIZE;
#else
    index++;
#endif
```

Something the player can turn off, which is most changes. The original's
lines go in a block of their own; with the switch off the braces are all
that is left, and they cost no byte:

```c
#if RB_FIX_REPLAY_RING
    if (g_rb_settings.bugfixes) {
        index = (index + 1) % REPLAY_RING_SIZE;
    } else
#endif
    {
        index++;
    }
```

Each side of a block is whole statements or declarations, with its braces
closed: never half an expression. The tools read both sides of a block as
code, and need that.

**3. Ask the setting.** `g_rb_settings` (`include/rb_settings.h`) holds what
the player chose: `g_rb_settings.bugfixes` for a fix,
`g_rb_settings.graphics == RB_GRAPHICS_ENHANCED` for the picture. A fix that
needs a setting of its own gets a member there, and its default in
`src/rebushido/settings.c` is the original's behaviour.

**4. Put new code of any size in `src/rebushido/`.** A new file there needs
no line anywhere: the Makefile finds it, compiles it like a file of the
game's, and the link places it. Its header is `include/rb_<file>.h`,
included by `bb2.h` in its `#if REBUSHIDO` block. Names are `rb_VerbNoun`,
`g_rb_noun`, `RbType` and `RB_CONSTANT`. The matching build does not
compile these files, so the game's files may use them only inside an `#if`:
outside one, the matching build fails to compile or to link, which is the
point.

A new variable needs no line in a linker script either, wherever it is
defined. Do not edit `bb2.ld` for ReBushido.

**5. Keep the notes.** Where the original's bug is noted with "the
original's behaviour", the note stays and the fix goes beside it. Nothing
in ReBushido's side is marked `FAKE`: it has no bytes to match.

**6. Run the checks** (below) before pushing.

## How it is tested

`bash tools/build.sh checks` runs ten checks and prints a summary; GitHub
Actions runs the same ten on every push to `rebushido` and on pull requests.

The first seven are `main`'s, on the source with every switch off: the
counts, the lint, `modern`, the tests, the game's SHA1, the overlay's SHA1
and `shift`. The SHA1 is the one that matters most here: a change that
reaches the original, through a missing `#if` or a moved original line,
fails it.

The last three are ReBushido's:

| Check | Command | Fails when |
|---|---|---|
| The build | `make rebushido` | the ReBushido build does not compile or link, does not fit below the fixed buffers, its small data is out of `gp`'s reach, an object was left out of the link, or its version text is missing |
| Tests | `make rebushido-test` | a host test fails with the master switch on. These are the game's tests, linked with the settings at their defaults, so they also show that a change does nothing until its setting is on; and ReBushido's own in `tests/rebushido/` |
| Modern | `make rebushido-modern` | the host's `gcc` finds something serious in the source with the master switch on that `tools/modern/baseline.tsv` does not have |

The counts and the lint understand the switches. `tools/count.py` counts
the source as the matching build compiles it, so what
[`STATUS.md`](STATUS.md) reports stays true on this branch, and lists
ReBushido's files and `#if` blocks apart. `tools/lint.py` reads both sides
of every block, holds ReBushido's code to the same checklist, and has one
check of its own for the switches (`rb-flag`).

None of this runs the game. Play tests in an emulator, scripted, are the
third item of the [roadmap](ROADMAP.md); until then a change is played by
hand from `make rebushido-disc`.

## Keeping up with `main`

`main` goes on being corrected (names, comments, a truer type). Merge it
into `rebushido` often (`git merge main`), and never the other way. Because
the original's lines stay as they are on this branch, such merges are
mostly clean.
