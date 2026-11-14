# ReBushido

ReBushido is a modded line of *Bushido Blade 2*: the original's bugs fixed,
the picture improved as far as a PlayStation can, and new features. It is
built from the matching decompilation and lives on the branch `main`.
`decomp` stays the byte-matching source and never changes what the program
does.

This page is the branch's rules, how to build and play it, how a change is
written and how it is tested. What is planned, in order, is in
[`ROADMAP.md`](ROADMAP.md). As it stands the branch is the foundation, the
options page at boot with the settings' own file on the memory card, the
tools for trying a change (a build that boots straight into a fight, and
play tests in an emulator), and fifteen fixes of the original's bugs,
which the "Bugfixes" setting turns on ("The fixes"), and the tool that
makes a release's patch ("Making a release"). Nothing reads the
"Graphics" setting yet.

## The rules

1. **Every change sits behind a compile-time switch.** With all switches
   off the branch builds the original executable byte for byte, and the
   checks hold it to that on every push. That is how the branch knows that
   nothing leaked outside its switch.
2. **ReBushido is a PlayStation program first.** It has to run on the
   original console and on emulators. A native port of the same source is
   planned beside it ([`ROADMAP.md`](ROADMAP.md)), not instead of it.
3. **The player chooses at boot.** One "Bugfixes" setting turns every fix
   on; only a fix that changes how the game plays by much gets a setting of
   its own. One setting switches between the original and the enhanced
   picture (a wide picture, high resolution, a high frame rate: mostly the
   port's work).
   So most changes ask a setting while the game runs, as well as being
   behind their switch. With nothing chosen everything is as the original.
4. **A release is a patch file** against the player's own retail disc, never
   a disc image ("Making a release", "Applying the patch"). No game data is
   ever committed.
5. **The licence** is AGPL-3.0-or-later ([`LICENSE`](../LICENSE),
   [`NOTICE`](../NOTICE)).
6. **The version is in the build**: the text `ReBushido <version> (<commit>)`
   is in the executable. The number is written once, `RB_VERSION_NUMBER`
   in `include/rebushido.h`; every build but a release's has `-dev` after
   it.

## Building

Set up as [`BUILD.md`](../BUILD.md) says; ReBushido needs nothing more.

```bash
bash tools/build.sh check             # the matching build, as on main
bash tools/build.sh rebushido         # the ReBushido build; ends with "OK: rebushido fits"
bash tools/build.sh rebushido-disc    # and a disc image to play (needs your retail image)
bash tools/build.sh checks            # every check; "OK: all 10 checks pass"
bash tools/build.sh quickboot-disc    # a disc that boots straight into a fight (a developer's tool)
bash tools/build.sh playtest          # the play tests, in an emulator with no window
bash tools/build.sh rebushido-patch   # a release's patch, into build/release/ (needs your retail image)
```

From a Windows shell put `wsl` in front, as with the other commands.

| Target | What it does |
|---|---|
| `make rebushido` | Compiles the source with the master switch on, and the files of `src/rebushido/`, into `build/rebushido/`; links `build/rebushido/rebushido.exe`; prints its size and the room left, and fails if it does not fit |
| `make rebushido-disc` | The same, then `build/play/ReBushido.bin` and `.cue`: the retail image with that executable in place of the game's |
| `make rebushido-patch` | The matching build and the ReBushido disc, then `build/release/`: the patch `ReBushido-<version>.bps`, a `README.txt` for players and a copy of the tool that applies it ("Making a release"). `RB_RELEASE=1` makes it a release's |
| `make rebushido-test` | The host tests with the master switch on, and ReBushido's own (`tests/rebushido/`) |
| `make rebushido-modern` | `make modern` with the master switch on, `src/rebushido/` included |
| `make quickboot` | The ReBushido build with quick boot on, into `build/quickboot/`: a program that starts one fight in place of the opening and the menus (below, "Booting straight into a fight") |
| `make quickboot-disc` | The same, then `build/play/QuickBoot.bin` and `.cue` |
| `make playtest` | Builds the discs the play tests want and runs them in an emulator with no window ([`tests/play/README.md`](../tests/play/README.md)); on the developer's machine only |

The builds share one tree and no object: the matching build is in
`build/` exactly as on `decomp`, the ReBushido build in `build/rebushido/`,
the quick-boot build in `build/quickboot/`. `make clean` removes them all
and leaves `build/play/` and `build/release/`.

The build ends with a report:

```
build/rebushido/rebushido.exe: ReBushido 0.1-dev (1a2b3c4d5)
  file     614400 bytes; loaded at 0x80010000 to 0x800A5800
  program  ends at 0x801095D0, its zeroed variables included
           0x1CF0 bytes more than the matching build (build/bb2.elf)
  room     0x4530 bytes below the first fixed buffer (0x8010DB00, SYS_SOUND_VAB_ADDR)
           0xCF230 below the movie overlay and the primitive buffers (0x801D8800)
           0xF6A20 below the stack's top (0x801FFFF0)
  gp       the small data ends 0x8B8 from gp, which reaches 0x7FFF
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
per game gives it the retail game's memory card; ReBushido's settings are
a second file on that card, beside the game's save ("The options page").

The executable is where the retail one is on the disc, and the image is
the retail image's size. It is three sectors longer than the retail one,
and three empty sectors are all the disc has after that file, before
`SYSTEM.CNF`: `tools/build_disc.py` takes them. It fits only because the
file leaves out its last zeroes: the small zeroed variables are the end
of what is loaded, the start-up code clears them before `main` in any
case, and `tools/make_psexe.py` ends the ReBushido build's file on the
first sector boundary after they begin (`--zeroed-from`). That saves one
sector, and as it stands `0x120` bytes are left before the file needs it
back. When the program outgrows the place, `build_disc.py` puts it after
the last file, as it did until this was found; the disc then boots about
160 vertical blanks (2.7 seconds) later in the emulator, the drive
seeking to the end of the disc and back, and nothing else changes. In its
place it boots when the retail disc does, to the vertical blank, the
options page not counted.

`build/play/` is not in git and `make clean` leaves it alone. Other images
there (the shifted disc of the play test, `BB2_shifted.*`) are not touched.
Beside the image the build writes `ReBushido.sym`, the names of the program
and their addresses, which the play tests read.

## Applying the patch

For players. A release is three files: `ReBushido-<version>.bps`, the
patch; `README.txt`, which says all of this; and `rebushido_patch.py`.
The patch is for one image and no other: *Bushido Blade 2*, the North
American disc (SLUS-00663), as a single `.bin` of 701,714,496 bytes with
its `.cue` (the README has its CRC-32 and SHA-1).

- With [Floating IPS](https://github.com/Alcaro/Flips) (Flips), or
  another patcher that takes BPS files: apply the patch to the `.bin`,
  save the result as `ReBushido.bin`, copy the `.cue` as `ReBushido.cue`
  and change the file's name in its first line.
- Or with Python 3, nothing installed, on Windows, macOS or Linux:

  ```
  python3 rebushido_patch.py apply --patch ReBushido-0.1.bps --image "Bushido Blade 2 (USA).bin" --out ReBushido.bin
  ```

  It checks the image first, by its size and CRC-32, and says what is
  wrong with one the patch is not for (another region's disc, another
  dump, an `.iso`, an image already patched). Then it writes
  `ReBushido.bin` and `ReBushido.cue`. The image is only read, and
  `--out` may not be the image; a file already there is left alone
  without `--force`.

Load `ReBushido.cue` in an emulator. The saves are the retail game's and
go both ways ("The options page" has what ReBushido adds to the card).

## The options page

A ReBushido disc shows a page of its own first, before the boot image and
the opening: the name and the version, a row for each setting, what the
buttons do, and a line about the memory card.

| Row | Choices | |
|---|---|---|
| Bugfixes | Off, On | `g_rb_settings.bugfixes` |
| Graphics | Original, Remastered | `g_rb_settings.graphics`, an `RbGraphics` |

Up and down choose a row, left and right change it (each goes round at
the ends), Start or cross goes on into the game. It comes up at every
boot. Both settings start as the original. "Bugfixes" turns the fixes
on ("The fixes"); nothing reads "Graphics" yet: the changes of the
roadmap's later items will.

The switch is `RB_OPTIONS_PAGE`. With it on, `main` calls
`rb_RunOptionsPage` (`src/rebushido/options.c`) after the game is set up
and before it shows the boot image; the quick-boot build does not. The
page has a loop of its own, like the pause menu's: a 320 by 240 screen,
as the boot image's, drawn in the game's built-in font (`font.c`) and
plain rectangles, the title in the same font at twice the size. It reads
the pads from the driver's packets and leaves `g_pad_state` alone, and
when it is done video memory is black again.

**The rows are a table**, `g_rb_options` (`src/rebushido/settings.c`): a
name, the names of the choices, the member of `g_rb_settings`, and the
setting's place in the file. A new setting is a member of `RbSettings`,
a new `RbSaveSlot` and a line of that table; the page, the file and
`rb_ResetSettings` follow. `tests/rebushido/options.c` fails if a row
does not fit the page or two share a place.

**The settings' file** is ReBushido's own, on the card in slot 1:
`BASLUS-00663REBUSHI`, one block, titled "ReBushido Settings", with an
icon drawn for it (`src/rebushido/save.c`, `include/rb_save.h`). The
game's save, `BASLUS-00663BUSHIDO2`, is never opened, written, renamed
or formatted over, and the retail game does not see the new file: it
looks for its own by name. A card goes between the two discs either way.

| Offset | |
|---|---|
| 0x000 | Sony's header: `SC`, one icon frame, one block, the title in Shift-JIS, the icon's sixteen colours and its 16 by 16 picture |
| 0x100 | `RBST` |
| 0x104 | the format's version, 1 |
| 0x105 | how many of the values mean something (2) |
| 0x108 | 32 values, a byte each: the setting whose `RbSaveSlot` is that place; zero where there is none yet |
| 0x128 | the CRC-32 of the 0x28 bytes from 0x100, low byte first |

0x200 bytes are written, zero after the checksum. A place is a setting's
for ever, so a file from an older version lacks only the newer settings
(they are as the original) and one from a newer version has values this
one skips. A value with no such choice is taken as the original's. A file
with other letters, another version or a wrong checksum is not read, and
the settings are as the original.

The file is read before the page takes the pad, with the page on show
("Reading the memory card"), and written when the player leaves the
page, only if a setting is not what it was when the page came up. So a
card with no settings' file stays without one until something is
changed, and a file that could not be read is left as it is until then.

**Nothing waits on the card and nothing is asked.** While the page is up
it looks at the card every few frames and its last line says what it
found: the settings will be saved, or there is no card, an unformatted
one (never formatted here) or a full one, and they will not be. The
choice then holds until the console is switched off. If a write fails on
a card that was ready, the page says so for two and a half seconds and
the game goes on.

How the card is used: the look at the card is the game's own probe
(`memcard_ProbeCard`: `_card_info`, then `_card_clear` for a new card
and `_card_load` for its directory), written again in `save.c` because
the game's keeps what it found in variables of its own, and those must
be, when the game first looks at the card in the main menu, as the
retail game has them. The directory and the file are then read and
written by the game's routines (`memcard_ReadDirectory`,
`memcard_FileExists`, `memcard_ReadFile`, `memcard_WriteFile`), one
thing at a time, and the look in flight is finished before the page
ends. The game does nothing with the card until the main menu, long
after.

Tried in the emulator, with its own BIOS: a new card, a second boot with
the card the first left, and no card (the play tests below). Not tried:
a real console or Sony's BIOS, a card the BIOS reports as new (the
emulator's never does), an unformatted card, a full one, a card pulled
out during the write.

## The fixes

"Bugfixes" on the options page turns on fifteen fixes of the original's
bugs, all together. With it off the game does what the original does
(`same_as_original` holds it to that). Each fix has a switch of its own
in `include/rebushido.h` as well, so that a build can leave one out.

They are the bugs of [`STATUS.md`](STATUS.md), "Suspected bugs and
oddities of the original", that are plainly mistakes, have a small fix,
and change nothing for a player who never met the bug. In the last
column, "run" is a play test that shows the game doing one thing with
the setting off and the other with it on; "host" is a host test of the
function (`tests/rebushido/fix_*.c`); "read" is neither: the fix was
read, and compiles.

| What the original does, and the fix | Where | Switch | Tested |
|---|---|---|---|
| A fighter's first frame gives a last place to the first of its weapon's three points only. The blade's far end and a pole arm's shaft are swept, that frame, from where the fight before left them, or from what lay on the scratchpad. Fixed: all three get one | `fighter_Update` | `RB_FIX_FIRST_FRAME_SWEEP` | run: `first_frame_sweep` |
| A blade's hit has `fighter_TakeHit` clear a word at address 0. Fixed: it is given a place | `fight_TestBladesOnBodies` | `RB_FIX_HIT_NULL_WRITE` | read. The hits of `bugfixes_fight` and `vanish` go through it; the write itself cannot be seen in the emulator, whose memory has 0 there already |
| Off the shrine's stage the vanisher, struck, comes back at another stage's start position (the table is read with half its stride), and where that is off the ground it falls for ever. Fixed: its own stage's. No mode of the game puts the vanisher on another stage | `fighter_Vanish` | `RB_FIX_VANISH_PLACE` | run: `vanish`; host: `fix_vanish` |
| A splash asked for while twelve are alive is written over the start of the next table. Fixed: it is not made | `eff_SpawnSplash` | `RB_FIX_SPLASH_POOL` | host: `fix_splash_pool`, the fixed side only |
| An object launched while twelve are loose is written after the pool. Fixed: it takes the last one's place | `object_Launch` | `RB_FIX_OBJECT_POOL` | host: `fix_object_pool`, the fixed side only |
| The muzzle flashes keep six ordering-table indices, two flashes' quads, with the size of the flash being made straight after them: with four alive the third's quads are linked in wherever the fourth's size says. Fixed: an index for every quad of all six | `eff_DrawMuzzleFlashes` | `RB_FIX_MUZZLE_FLASH_INDICES` | read |
| The team battle's points and its scores each link a second drawing mode just past the size they return, where the caller builds what it draws next. Fixed: the size counts it | `hud_DrawPointsAndTime`, `hud_DrawTeamScores` | `RB_FIX_HUD_DRAW_MODE` | host: `fix_hud_draw_mode` |
| The ending's picture and the count of opponents beaten move the primitive cursor on four times as far as they drew. Fixed: as far | `cutscene_EndingUpdate`, `fight_KillCountUpdate` | `RB_FIX_PRIM_CURSOR` | read |
| The boot image's fade starts 20 bytes into the pixels and runs 20 bytes past them: the first ten pixels never fade. Fixed: the picture's pixels. Nothing to see with the game's picture, whose first ten pixels are black | `disp_ShowBootImage` | `RB_FIX_BOOT_FADE` | host: `fix_boot_fade` |
| The first frame's `pad_Update` is told whether a frame was skipped from a variable nothing has set. Fixed: none was | `main` | `RB_FIX_FIRST_FRAME_PAD` | read |
| Coming back to chanbara's page from a fight starts the pages' music twice. Fixed: once | `menu_EnterMode` | `RB_FIX_CHANBARA_MUSIC` | read |
| A link that gave up still makes its three trades, and takes the other side's unlocks, its buttons and the fight's seed from whatever was received last. Fixed: it trades nothing | `link_Connect` | `RB_FIX_LINK_GIVE_UP` | read |
| When the arena moves the castle rooftop's stage, the backdrop's meshes are moved without being told how far. Fixed: by as far as the stage. The original gets away with it: as it is compiled, the register still holds the distance from the call before | `resource_OnStageMoved` | `RB_FIX_ROOFTOP_REBASE` | read |
| A splash's drops begin to fade a frame early, with a blue that does not fit its byte: one frame of the wrong colour. Fixed: that frame is at the full colour | `eff_DrawSplashes` | `RB_FIX_SPLASH_FADE` | read |
| The dirt puff is drawn with 33 rows of a texture whose frame has 32. Fixed: 32 | `eff_SetUpParticles` | `RB_FIX_DIRT_PUFF_ROW` | read |

Every play test with the setting on runs the fixed code where the fight
goes through it, and `bugfixes_fight` plays a whole versus fight that
way, from the options page to the menu after the fight, with no crash.
The two pools' host tests have only the fixed side because the original
writes outside its tables there, which a test cannot ask a program to
do. Together the fifteen take 0x320 bytes.

**Left as the original has it.** These are in that list too, and no fix
of this kind fits them:

- They change how a fight plays, or what the CPU does: the CPU's two
  tests of its own move where the opponent's was meant, its reach tables
  that disagree and its footwork read from a variable nothing set; the
  leaping attack's fall; the body points' reach scaled once for a
  fighter's size; the blow's direction taken from the last sweep tested;
  the second weapon's last places reset when the main weapon goes live;
  the sweeps of a pole arm with a live sub-weapon.
- They change what everybody has seen or heard, or what was meant is
  not known: a mortal wound that bleeds once (the timer is cleared every
  frame, which may be the authors' way of turning the rest off); the
  splashes, the dust and the hit flashes drawn 3.9 times their
  constants' size; the drops black until they fade; the particles' two
  tests in the wrong order; the lock flash's other texture; the cloth
  under the ground and its last spring; the reverb's depth taken from
  the stage's number; a stage piece's rotation test; the fogged model's
  extra colour tables; the tiny model's camera; the unlock bits' clans;
  the save file's table of patches.
- They do no harm, or cannot happen with the game's data: the name
  compared a byte too far, the table of kinds read one entry early, a
  ninth cloth and a ninth spring, the weapon file's wrong table entry,
  and everything that is only said twice or never called.

## Booting straight into a fight

This is a developer's tool, for trying a change without the opening and the
menus, by hand or by script. It is no feature of the game: the ReBushido a
player gets does not have it.

```bash
bash tools/build.sh quickboot-disc QB_P1=ROSTER_TSUBAME QB_WEAPON1=WEAPON_ID_LONG_GUN QB_STAGE=STAGE_SNAKE_CLIFF
```

writes `build/play/QuickBoot.cue` (and its `.bin`), a disc whose program
shows no boot image, plays no movie and opens no menu: it sets itself up
and starts the fight it was built for. The ReBushido disc beside it is not
touched. In the emulator the fight is running 24 seconds of the console's
time after power on, nearly all of it the disc being read; the retail disc
needs 35 seconds to reach its top menu with the opening skipped, and about
40 more to be in a fight.

The fight is chosen by variables given to `make`, by the game's own names.
`python3 tools/quickboot.py --list` prints every value each can take.

| Variable | What | Values | If not given |
|---|---|---|---|
| `QB_MODE` | The mode | `FIGHT_MODE_VS`, `FIGHT_MODE_VSCPU`, `FIGHT_MODE_STORY`, `FIGHT_MODE_PRACTICE`, `FIGHT_MODE_CHANBARA` (`enum FightMode`, `include/fight.h`) | `FIGHT_MODE_VS` |
| `QB_P1`, `QB_P2` | The two fighters | a member of `enum RosterId` (`include/fighter.h`): `ROSTER_KANNUKI`, `ROSTER_TSUBAME`, `ROSTER_GENGORO`, ... 33 in all | `ROSTER_KANNUKI`, `ROSTER_GENGORO` |
| `QB_WEAPON1`, `QB_WEAPON2` | Their weapons | a member of `enum WeaponId` (`include/weapon.h`): `WEAPON_ID_KATANA`, `WEAPON_ID_LONG_SWORD`, `WEAPON_ID_NODACHI`, `WEAPON_ID_BROADSWORD`, `WEAPON_ID_NAGINATA`, `WEAPON_ID_YARI`, `WEAPON_ID_LONG_GUN`, `WEAPON_ID_SHORT_GUN` | `WEAPON_ID_KATANA` for both |
| `QB_STAGE` | The stage | a member of `enum StageIndex` (`include/stage.h`): `STAGE_MAIN_TATARA_SHRINE` to `STAGE_SHAINTO_CEMETERY`, and the modes' own | `STAGE_BAMBOO_THICKET` |
| `QB_CPU2` | Whether the CPU plays the second fighter | `0`, `1` | `1` |
| `QB_BUGFIXES` | Whether the fight starts with the "Bugfixes" setting on (the build has no options page to turn it on at) | `0`, `1` | `0` |
| `QB_NAME` | The disc's name in `build/play/` | begins with `QuickBoot` | `QuickBoot` |

A name the game does not have stops the build, which lists the names it
has. What the modes do with the choice:

- `FIGHT_MODE_VS` is one fight between the two, with `QB_CPU2=1` against
  the CPU: the one to use for trying things. After it the game's own menu
  comes up (Rematch, Return to Sub Menu, Return to Top Menu), and from
  there the game goes on as ever.
- The other four are started as the original's own debug menu starts them
  (`fight_StartMode`), and each takes over part of the choice, as it does
  in the game: `FIGHT_MODE_VSCPU` chooses the opponents and the stages;
  `FIGHT_MODE_STORY` runs the first fighter's story from its stage card
  (give it a story entry, `ROSTER_KANNUKI_STORY` and the like);
  `FIGHT_MODE_PRACTICE` and `FIGHT_MODE_CHANBARA` have their own stage and
  opponent. Each was started once in the emulator and came up; none was
  played further. A team battle and a link fight need what their menus set
  up, and cannot be chosen.
- Nothing checks that a fighter can hold the weapon it is given or stand
  on the stage: the debug menu did not either. A pair the select screen
  does not offer may look wrong or fail.
- No memory card is read on the way, so the fighters and the options are
  the ones the game starts with until a card is loaded from the menus.

How it is made: the switch is `RB_QUICK_BOOT` (`include/rebushido.h`),
which is off in the ReBushido build as well as in the matching one; only
`make quickboot` turns it on. With it on, `main` calls `rb_QuickBoot`
(`src/rebushido/quickboot.c`) where it would show the boot image and choose
the movie's screen. That function does what the main menu does when it
first opens (no stage in memory, no sound bank but the common one), writes
the choice into `g_fight_config` and calls `fight_StartMode`. The build is
in `build/quickboot/`, so the ReBushido build is not disturbed; it is 0x4B0
bytes larger than the matching build, with 0x5D70 bytes of room, and has
no options page. `make rebushido` compiles the two files that ask the switch
with it on, so the checks keep quick boot compiling, and
`tests/rebushido/quickboot.c` runs `rb_QuickBoot` on the host.

## How much room there is

The program, its zeroed variables included, ends a little under
`0x80108000`. Above it the game keeps buffers at fixed addresses
(`include/sys.h`): the first is the common sound bank at `0x8010DB00`. The
matching program ends `0x6220` bytes (about 25 KB) below it, and that is
what ReBushido's code and data can grow by as things are. The options
page and the settings' file take `0x19A0` of it and the fixes `0x320`;
`0x4530` is left. `make rebushido`
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
- Every object's section in the program is put on a 4-byte boundary, and
  so is the end of each kind. In the original each one already is on one,
  so this moves nothing until a file's data changes length; then it keeps
  the next file from starting off a word, which the hand-written script
  would not. The end matters as much: the zeroed variables begin where the
  small data ends, and the start-up code clears them a word at a time. (The
  first ReBushido build ended its small data on a two-byte variable and
  stopped there, on an address error, before `main`: the first play test
  found it.)

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

One switch is not a change to the game and is written another way:
`RB_QUICK_BOOT` is `(REBUSHIDO && RB_QUICK_BOOT_BUILD)`, a developer's
build of its own ("Booting straight into a fight"), off in ReBushido as
well unless the build gives that name. A switch of that kind ends in
`_BUILD` on its right-hand side; the tools know it by that.

**2. Leave the original's lines as they are, once.** They are not deleted,
not copied into ReBushido's side and not reworded: the diff against `decomp`
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
needs a setting of its own gets a member there, a place in the settings'
file and a row of `g_rb_options` ("The options page"); its 0 is the
original's behaviour.

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
Actions runs the same ten on every push to `main` and on pull requests.

The first seven are `decomp`'s, on the source with every switch off: the
counts, the lint, `modern`, the tests, the game's SHA1, the overlay's SHA1
and `shift`. The SHA1 is the one that matters most here: a change that
reaches the original, through a missing `#if` or a moved original line,
fails it.

The last three are ReBushido's:

| Check | Command | Fails when |
|---|---|---|
| The build | `make rebushido` | the ReBushido build does not compile or link, does not fit below the fixed buffers, its small data is out of `gp`'s reach, an object was left out of the link, or its version text is missing; or a file that asks `RB_QUICK_BOOT` does not compile with it on |
| Tests | `make rebushido-test` | a host test fails with the master switch on. These are the game's tests, linked with the settings at their defaults, so they also show that a change does nothing until its setting is on; and ReBushido's own in `tests/rebushido/`: the settings, the options page's table, the settings' file (its checksum, its header, and every kind of file that must not be read), the release's patch tool on made-up bytes (round trips, a damaged patch and a wrong source refused), quick boot, the offsets the play tests read memory at, and five of the fixes with the setting off and on (`fix_*`) |
| Modern | `make rebushido-modern` | the host's `gcc` finds something serious in the source with the master switch on that `tools/modern/baseline.tsv` does not have |

The counts and the lint understand the switches. `tools/count.py` counts
the source as the matching build compiles it, so what
[`STATUS.md`](STATUS.md) reports stays true on this branch, and lists
ReBushido's files and `#if` blocks apart. `tools/lint.py` reads both sides
of every block, holds ReBushido's code to the same checklist, and has one
check of its own for the switches (`rb-flag`).

None of those ten runs the game. The play tests do
([`tests/play/README.md`](../tests/play/README.md)):

```bash
bash tools/build.sh playtest      # from Windows: wsl bash tools/build.sh playtest
```

builds the ReBushido disc and three quick-boot discs and runs nine tests in an
emulator (PCSX-Redux) with no window, pressing the pads by script and
reading the game's memory by the names of the source:

| Test | What it shows |
|---|---|
| `boot` | The retail disc reaches its top menu |
| `same_as_original` | The ReBushido disc, left at its options page with every setting at its default, is in the same state as the retail disc at eleven checkpoints from the opening through the menus, a versus fight, a kill, the replay and the menu after: every watched value, the vertical blanks from the first of them to each, and the picture to the pixel |
| `options_page` | The options page comes up with the original's settings; the pad changes them; leaving it writes the settings' file and the game reaches its top menu; a second boot with the same card comes up with the changed setting and writes nothing |
| `options_no_card` | With no card in either slot the page comes up at once and says so, the choice holds, and the game reaches its top menu |
| `quickboot` | A quick-boot disc is in the fight it was built for, with no input |
| `fight_end` | From a quick boot, a fight to a kill, the menu after it, and a rematch, with no crash |
| `bugfixes_fight` | With Bugfixes turned on at the options page: the opening, the menus, a versus fight to a kill, the replay and the menu after, with no crash |
| `first_frame_sweep` | A naginata's first frame sweeps its shaft from a stale place; with Bugfixes on, from where it is |
| `vanish` | Off the shrine's stage the vanisher, struck, comes back at another stage's place and falls out of the stage; with Bugfixes on it comes back where it started and stands |

They are not among the checks and GitHub Actions does not run them: they
need your own retail image and the emulator, which are never in the
repository. Without either the command says what it skipped and exits 0.
Run them before pushing a change to the game, and write a test for the
change: a new one is a small file (the disc, the presses, the checkpoints
and what must hold at each).

What they do not show: the sound (the emulator runs silent), anything the
watched values and eleven pictures do not catch, and any mode but the
versus fight. `same_as_original` cannot compare how long the boot takes:
the ReBushido disc has the options page, so each checkpoint is 109
vertical blanks later than on the retail disc (it was 270 while the
program was at the end of the disc). What it holds the two to is that
the difference is the same
at every checkpoint (the blanks are counted from the first one, the
opening's screen), with the game's own count of frames, which starts
after the page, equal outright. It compares what `tests/play/state.toml`
names, not all of memory; the original reads leftover memory in one known
place ([`STATUS.md`](STATUS.md), "Suspected bugs and oddities of the
original", "The fight", the entry marked watched: a naginata or a yari
that follows a two-point weapon sweeps its shaft, on its first frame, from
whatever the scratchpad held), which a build laid out differently can
show as a difference in a fight's first frames. The test's fight is katana
against katana and does not go through it, and the two discs agree at
every checkpoint. ("Bugfixes" takes that read away: "The fixes", the
first row.)

## Making a release

A release is a patch against the retail image, a README and the tool
that applies the patch: never an image, never the program. Nothing of it
is committed (`build/` is not in git).

1. **The version.** `RB_VERSION_NUMBER` in `include/rebushido.h` is the
   number (`"0.1"`). Commit and push everything; wait for the checks.
2. **Build the patch**, from a tree with nothing uncommitted:

   ```bash
   bash tools/build.sh rebushido-patch RB_RELEASE=1
   ```

   It builds the matching program and stops if that does not match,
   builds ReBushido with the version `ReBushido 0.1 (<commit>)` (no
   `-dev`; a tree with changes is refused), makes the disc, and writes
   into `build/release/`: `ReBushido-0.1.bps`, `README.txt`,
   `rebushido_patch.py` and `patch-report.txt`. It applies the patch to
   the retail image and compares the result with the disc by SHA-1
   before it says OK. Without `RB_RELEASE=1` the same is made as a
   trial, `ReBushido-0.1-dev.bps`.
3. **Test that disc**: `bash tools/build.sh playtest RB_RELEASE=1`. The
   `RB_RELEASE=1` matters: without it the discs are built again with
   `-dev` in the version, and the tests run another image than the one
   the patch makes. Afterwards the SHA-1 of `build/play/ReBushido.bin`
   is still the one `README.txt` gives for the result. Try the patch in
   Flips as well, once (below).
4. **Tag and publish**: `git tag v0.1 && git push origin v0.1`, then a
   GitHub release of that tag with the three files attached
   (`ReBushido-0.1.bps`, `README.txt`, `rebushido_patch.py`) and the
   notes. `patch-report.txt` is for you.
5. **Afterwards** raise `RB_VERSION_NUMBER` for the next one.

**The patch** is a BPS file (`tools/rebushido_patch.py` has the format at
its top, and writes and applies it with Python's standard library alone;
`tools/rebushido_release.py` is what the target runs). BPS says where
each run of the result's bytes comes from: the image at the same place,
the image somewhere else, the result so far, or the patch itself. Only
the last kind is carried, and the tool that makes the patch looks for
every run of ReBushido's program in the retail image's own program
before it carries a byte, so code that only moved is "this many bytes
from there". The report counts what is left. For 0.1 as it stands, a
patch of 176 KB carries 115,587 bytes:

| Bytes | What they are |
|---|---|
| 80,316 | The error codes (EDC and ECC) of the 299 sectors of the program and of the directory's. They follow from the sectors' contents, and a patch of the raw image has to carry them |
| 28,350 | Addresses inside the game's instructions and tables: the program was linked again, most of it is somewhere else, and every call and pointer to what moved has other bytes than the original's |
| 4,092 | ReBushido's own code and data |
| 2,588 | The seventeen functions of the game that ReBushido changes, as they compile now. Much of that is the original function's code in another shape |
| 145 | Bytes of the original program that the tool found no run for: the same as the retail program has at that place of the same function or table |
| 96 | The rest: the directory's record of the program's size, the program's header, two bytes of sector headers, padding |

So the patch does hold a little of the original as it is (the 145),
and the changed functions hold more of it recompiled. What would shrink
all of this is a link that leaves the original where it is, ReBushido's
additions after the original's sections and a changed function moved out
whole: then nothing moves, no address changes, and only the sectors
ReBushido touches differ, with their error codes. That is a redesign of
`tools/rebushido.py`'s script and has not been done.

Not known: whether Flips takes the patch. The format is written from
its description, and the tool's own applier and its tests
(`tests/rebushido/patch.py`) agree with it, but no other patcher has
been tried, and neither has an image of 700 MB in one.

## Keeping up with `decomp`

`decomp` goes on being corrected (names, comments, a truer type). Merge it
into `main` often (`git merge decomp`), and never the other way. Because
the original's lines stay as they are on this branch, such merges are
mostly clean.
