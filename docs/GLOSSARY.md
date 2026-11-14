# Glossary

Words a reader of this repository meets that the source does not explain: the
PlayStation and its SDK, the MIPS processor, the toolchain, and the Japanese
words in the game's own strings and file names. The words the *source* uses
for the game's parts (fighter, stance, cutscene, cell...) are in
[`ARCHITECTURE.md`](ARCHITECTURE.md), "Vocabulary".

## PlayStation

| Term | Meaning |
|---|---|
| **PsyQ** | Sony's PlayStation development kit, made by SN Systems: a GCC 2.7.2 compiler (`cc1psx`), the assembler ASPSX, the linker PSYLINK, and Sony's libraries. The game's libraries are from the 4.0 release or close to it. |
| **PS-X EXE** | The executable format: a 0x800-byte header (entry point, load address, size, stack) and one block of code and data loaded as it is. |
| **BIOS** | The kernel in ROM. Its services are called by jumping to `0xA0`, `0xB0` or `0xC0` with the function number in `$t1`; the library wraps each in a three-instruction function, which is why those are assembly. |
| **GPU** | The graphics chip. It draws **primitives** (flat or textured, shaded or not, triangles `F3`/`G3`/`FT3`/`GT3`, quads `...4`, sprites, lines) from a linked list, the **ordering table** (OT), which the game fills back to front each frame. |
| **VRAM** | The GPU's 1 MB of memory, 1024x512 16-bit pixels: both display buffers and every texture and palette live in it. The CPU cannot address it; pictures are uploaded with `LoadImage`. |
| **TIM**, **CLUT**, **tpage** | Sony's picture file; a palette ("colour look-up table") stored in VRAM; the 256x256 VRAM page a primitive takes its texture from. |
| **GTE** | The "geometry transformation engine", coprocessor 2: fixed-point matrix and vector arithmetic, perspective projection, lighting, depth averaging. It has no C form; Sony's `gte_*` macros (`include/psxsdk/inline_c.h`) emit its instructions. |
| **Fixed point** | There is no floating point. Angles are 4096 to the turn, rotation-matrix entries and unit vectors are 4096 for 1.0 (`ONE`). |
| **SPU** | The sound chip: 24 voices of ADPCM samples, 512 KB of sound memory, reverb. |
| **VAB**, **VH**, **VB**, **VAG**, **SEQ** | Sony's sound bank (VAB = VH header + VB sample body), a single sample (VAG) and a MIDI-like sequence, played by libsnd. |
| **XA** | CD-ROM XA: sectors that carry compressed audio (XA-ADPCM) interleaved with data, played by the drive itself while the game reads on. The music and voices. |
| **STR**, **MDEC** | Sony's streamed movie format, and the chip that decompresses its frames. Only the overlay `MOVOVL.EXE` uses them. |
| **Scratchpad** | 1 KB of fast memory at `0x1F800000`, really the CPU's data cache. The game uses it as work space for the renderer, the effects and the hit tests. |
| **KSEG0** | The address range `0x80000000` and up, where the program runs. The 2 MB of RAM is `0x80000000`-`0x801FFFFF`. |
| **Root counter** | One of the three hardware timers. |
| **Memory card**, **link cable** | Saved data (libcard, the BIOS file functions); two consoles joined by their serial ports (libcomb). |

## MIPS

| Term | Meaning |
|---|---|
| **R3000A** | The CPU: 32-bit MIPS I, 33.8 MHz. |
| **Delay slot** | The instruction after a branch or jump runs before the branch takes effect. A value loaded from memory is likewise not available to the very next instruction. |
| **`$a0`-`$a3`**, **`$v0`** | The first four arguments of a call, and the return value. More arguments go on the stack. |
| **`$s0`-`$s7`**, **`$t0`-`$t9`** | Registers a function must preserve, and registers it may overwrite. |
| **`$sp`**, **`$ra`**, **`$gp`**, **`$at`** | Stack pointer, return address, global pointer, and the register the assembler keeps for itself. |
| **`$gp`, small data** | `$gp` holds one address for the whole program (`0x800A30CC`), and variables near it (`.sdata`, `.sbss`) are reached in one instruction instead of two. Which variables go there is decided per file: 8 bytes or less, and only in the file that defines them. |
| **`lui` / `addiu`**, **HI16 / LO16** | A 32-bit address is loaded as two 16-bit halves, each with its own relocation. |
| **`mult`, `div`, `mflo`, `mfhi`** | Multiply and divide leave their result in two special registers, read with `mflo` and `mfhi`. |
| **Pseudo-instruction** | Something the assembler accepts and expands to real instructions (`li`, `la`, `move`, `lw` from a symbol, `div` with its checks). ASPSX and GNU `as` expand some differently, which is what maspsx is for. |

## Toolchain and decompilation

| Term | Meaning |
|---|---|
| **Matching** | The source compiles to exactly the original's bytes. Checked by SHA1 on the whole executable. |
| **The oracle** | The original executable, or its SHA1: what the build is compared with. |
| **cc1** | The compiler proper: preprocessed C in, assembly out. `cc1psx` is Sony's build of it. |
| **ASPSX**, **maspsx** | Sony's assembler, and the program that makes GNU `as` produce what ASPSX would have ([`COMPILER.md`](COMPILER.md)). |
| **Translation unit (TU)** | One `.c` file with everything it includes, compiled to one object. |
| **`.text`, `.rodata`, `.data`, `.sdata`, `.bss`, `.sbss`** | Sections of an object: code; constants, strings and jump tables; initialised variables; small initialised variables; zeroed variables; small zeroed variables. |
| **Linker script** | `bb2.ld`: which object's section goes where. It reproduces the original's link order. |
| **Listing** | The disassembly of one function. `asm/funcs/<name>.s` holds the ones the build includes; `tools/listings.py --write` makes one of any other. |
| **`INCLUDE_ASM`** | The macro that puts a listing from `asm/funcs/` into a C file in place of a function that was assembly in the original. |
| **`FAKE`** | A comment on code shaped only to make the compiler produce the original's bytes ([`MATCHING.md`](MATCHING.md)). |
| **Jump table** | The table of addresses a `switch` compiles to. |
| **splat**, **m2c**, **decomp-permuter** | The splitter that cut the executable into listings at the start; a decompiler that drafts C from a listing; a tool that searches variations of a function for one that matches. None is needed to build. |
| **Kengo** | *Kengo: Master of Bushido* (PlayStation 2, 2000), by the same studio on a descendant of the same engine. Its release kept its debugging symbols, and early function names here were borrowed from them. |

## Words from the game

The original programmers' words survive in strings the program prints and in
file names. They are romanised Japanese, spelled the programmers' way. The
largest set is the kinds of move (`KAMAE`, `KARAMI`, `NOBORI_S`...): the
source names them in English (`MOVE_KIND_STANCE`, `MOVE_KIND_PAIRED`,
`MOVE_KIND_CLIMB_ST`) and gives the game's word for each in the comment at
`enum MoveKind` in `include/motion.h`. Identifiers are in English; only
proper nouns (fighters, clans, places, weapons, the chanbara mode, the
kaishaku) keep a Japanese word. The
others:

| Word | Meaning |
|---|---|
| **Marionation** | The engine's name ("marionette" + "animation"), from its error strings. `MAR` in file names. |
| **chanbara** | Sword-fight drama. The game's name for the mode the menu calls Slash, and for the two archive files with its fighters, `CHANBARA0` / `CHANBARA1`. |
| **kaishaku** | The blow that finishes an opponent who has given up, and the cutscene of it. Also spelled `KAISYAKU`: the game's name for the move of the fighter who kneels (`MOVE_KIND_SURRENDER`). |
| **kamae** | Stance (`MOVE_KIND_STANCE`). |
| **karami** | Entangling: a paired move, the two fighters locked together (`MOVE_KIND_PAIRED`). |
| **seri**, **tsubazeriai** | Blades locked, guard against guard: the blade lock (`MOVE_KIND_LOCK`). |
| **suna** | Sand: the dirt a fighter throws at the other's eyes (`MOVE_KIND_DIRT`). |
| **JADGE** | "Judge": a sound bank, `JADGE.SE`, the team battle's judge (`SND_VAB_JUDGE`). |
| **KAISYA** | Company (*kaisha*), in the names of two sound streams, `KAISYA_1.XA` and `KAISYA_2.XA`; presumably the company logos'. They are in the game's file table (`CDROM_AUDIO_KAISYA_1`, `_2`) and no code asks for them. |
| **Narukagami**, **Shainto** | The two clans. A character belongs to one (`g_fighter_clans`); the first twelve of `enum CharacterId` are the Narukagami's, the next twelve the Shainto's. |
