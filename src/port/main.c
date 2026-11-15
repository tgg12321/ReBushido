/* The native port's program (docs/PORT.md, "The program"):
 *
 *   rebushido --image "Bushido Blade 2 (USA).cue" [--frames N] [--trace]
 *             [--plan FILE] [--dump NAME=ADDRESS:BYTES] [--out FOLDER]
 *             [--shots FOLDER] [--every N] [--vram FILE]
 *             [--wav FILE] [--spu FILE]
 *             [--card1 FILE] [--card2 FILE] [--no-cards]
 *
 * It gives the game the PlayStation's memory and the player's disc
 * image, and then calls the game's own main, which never returns: the
 * run ends at a vertical blank, by --frames or by the plan
 * (port_OnBlank). There is no window and no sound card yet, and the
 * pads are pressed only by a plan; the picture is drawn into the port's
 * video memory and the sound is made by the port's sound chip, and both
 * are written to files when a run asks. The memory cards are two card
 * images beside the program, unless --card1, --card2 or --no-cards says
 * otherwise (port_card.h). */
#include "common.h"
#include "port_card.h"
#include "port_disc.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_spu.h"
#include "bb2.h"

#define PATH_BYTES_MAX 1024
/* The bits of an address the game can keep. */
#define ADDRESS_BITS 32
/* A picture a second, where --shots does not say how often. */
#define SHOTS_EVERY 60

static PortDisc g_port_disc;
static s32 g_port_quitting;

/* The window's turn as the program ends (port_platform.h): nothing in
 * the build without one. */
void (*g_port_quit_hook)(s32 code);

static const char g_port_usage[] =
    "rebushido: the native port of ReBushido, as far as it goes: the game\n"
    "with no window, sound card or pads; its picture and its sound go to\n"
    "files (docs/PORT.md)\n"
    "\n"
    "  rebushido --image FILE [--frames N] [--trace] [--plan FILE]\n"
    "            [--dump NAME=ADDRESS:BYTES] [--out FOLDER]\n"
    "            [--shots FOLDER] [--every N] [--vram FILE]\n"
    "            [--wav FILE] [--spu FILE]\n"
    "            [--card1 FILE] [--card2 FILE] [--no-cards]\n"
    "\n"
    "  --image FILE   your own image of the game's disc: the .cue, or the\n"
    "                 .bin of raw sectors it names\n"
    "  --frames N     end after N vertical blanks (sixty a second); 3600\n"
    "                 if neither this nor a plan says\n"
    "  --trace        print the game's state at every vertical blank where\n"
    "                 it has changed (the values of tests/play/state.toml)\n"
    "  --plan FILE    follow a play test's plan: its presses, checkpoints\n"
    "                 and limit (tests/play/README.md)\n"
    "  --dump NAME=ADDRESS:BYTES   write that much of the PlayStation's\n"
    "                 memory to NAME.bin when the run ends\n"
    "  --out FOLDER   where the dumps go\n"
    "  --shots FOLDER a PNG of the picture, blank-NNNNNN.png, into this\n"
    "                 folder every sixty vertical blanks\n"
    "  --every N      ... every N of them\n"
    "  --vram FILE    all of video memory to this file when the run ends:\n"
    "                 1024 by 512 pixels of 16 bits\n"
    "  --wav FILE     keep the run's sound and write it there when the run\n"
    "                 ends, with a line for each voice started\n"
    "  --spu FILE     write sound memory and the sound chip's registers\n"
    "                 there when the run ends\n"
    "  --card1 FILE   the memory card in slot 1, a card image of 128 KB\n"
    "                 (card1.mcd beside the program if this does not say);\n"
    "                 a file that is not there is made, formatted and empty\n"
    "  --card2 FILE   the memory card in slot 2 (card2.mcd beside it)\n"
    "  --no-cards     no memory card in either slot\n"
    "\n"
    "At the end it prints a line for each thing the game asked for that\n"
    "the port does not do yet, with how often: stub NAME COUNT.\n";

/* The end of a run (port_platform.h): what it was asked to write, the
 * stubs it called, and out. */
void port_Quit(s32 code) {
    /* once: what is written at the end may itself fail */
    if (!g_port_quitting) {
        g_port_quitting = 1;
        port_WriteDumps();
        port_WriteSpuDump();
        if (port_IsWavOn()) {
            port_PrintSoundSummary();
            port_WriteWav();
        }
        port_PrintStubs();
        if (g_port_quit_hook != NULL) {
            g_port_quit_hook(code);
        }
    }
    port_FlushOutput();
    port_HostExit(code);
}

static void port_UsageError(const char *problem, const char *word) {
    port_Log("rebushido: %s%s\n\n%s", problem, word, g_port_usage);
    port_HostExit(PORT_EXIT_USAGE);
}

/* `NAME=ADDRESS:BYTES`, cut in place. */
static void port_ParseDump(char *word) {
    char *address = word;
    char *size;
    s32 at = 0;
    s32 bytes = 0;

    while (*address != 0 && *address != '=') {
        address++;
    }
    size = address;
    while (*size != 0 && *size != ':') {
        size++;
    }
    if (*address == 0 || *size == 0 || address == word) {
        port_UsageError("--dump takes NAME=ADDRESS:BYTES, not ", word);
    }
    *address++ = 0;
    *size++ = 0;
    if (!port_ParseNumber(address, &at) || !port_ParseNumber(size, &bytes) ||
        bytes <= 0) {
        port_UsageError("--dump takes NAME=ADDRESS:BYTES; the name was ", word);
    }
    port_AddDump(word, (u32)at, (u32)bytes);
}

/* `folder` with a `/` after it, if it has none, in `prefix`. */
static const char *port_FolderPrefix(const char *folder, char *prefix) {
    u32 length = port_TextLength(folder);
    u32 i;

    if (length == 0 || length + 2 > PATH_BYTES_MAX) {
        return "";
    }
    for (i = 0; i < length; i++) {
        prefix[i] = folder[i];
    }
    if (prefix[length - 1] != '/' && prefix[length - 1] != '\\') {
        prefix[length++] = '/';
    }
    prefix[length] = 0;
    return prefix;
}

/* The names the build wrote: the program's own path and ".sym". */
static void port_LoadOwnNames(const char *program) {
    static const char ending[] = ".sym";
    static char path[PATH_BYTES_MAX];
    u32 length = port_TextLength(program);
    u32 i;

    if (length + sizeof(ending) > sizeof(path)) {
        return;
    }
    for (i = 0; i < length; i++) {
        path[i] = program[i];
    }
    for (i = 0; i < sizeof(ending); i++) {
        path[length + i] = ending[i];
    }
    port_LoadNames(path);
}

/* The game, on its own stack. Its main does not return. */
static void port_RunGame(void) {
    main();
    port_Quit(PORT_EXIT_FAILED);
}

/* The program: its arguments, the memory, the disc, and then the game
 * (port_host.h). */
s32 port_Main(s32 argc, char **argv) {
    static char out_folder[PATH_BYTES_MAX];
    static char shots_folder[PATH_BYTES_MAX];
    const char *image = NULL;
    const char *plan = NULL;
    const char *shots = NULL;
    const char *problem;
    s32 frames = 0;
    s32 every = SHOTS_EVERY;
    s32 i;

    port_CatchCrashes();
    /* everything the game can point at is below 4 GB (docs/PORT.md, "64
     * bits"): the program is linked there, and says so if it was not */
    if (((u64)(PortAddress)g_port_data_end >> ADDRESS_BITS) != 0 ||
        ((u64)(PortAddress)port_Main >> ADDRESS_BITS) != 0) {
        port_Log("rebushido: the program is not below 4 GB, where the game's "
                 "32-bit addresses reach: it has to be linked at a fixed "
                 "address, with no position independence\n");
        return PORT_EXIT_FAILED;
    }
    port_MapMemory();
    if (argc > 0) {
        port_LoadOwnNames(argv[0]);
    }
    for (i = 1; i < argc; i++) {
        const char *word = argv[i];
        s32 has_value = i + 1 < argc;

        if (port_TextsEqual(word, "--trace")) {
            port_SetTrace(1);
        } else if (port_TextsEqual(word, "--no-cards")) {
            port_SetCardsOff();
        } else if (
            port_TextsEqual(word, "--help") || port_TextsEqual(word, "-h")) {
            port_Print("%s", g_port_usage);
            port_FlushOutput();
            return PORT_EXIT_OK;
        } else if (!has_value) {
            port_UsageError("not known, or nothing after it: ", word);
        } else if (port_TextsEqual(word, "--image")) {
            image = argv[++i];
        } else if (port_TextsEqual(word, "--plan")) {
            plan = argv[++i];
        } else if (port_TextsEqual(word, "--wav")) {
            port_StartWav(argv[++i]);
        } else if (port_TextsEqual(word, "--spu")) {
            port_SetSpuDump(argv[++i]);
        } else if (port_TextsEqual(word, "--card1")) {
            port_SetCardPath(0, argv[++i]);
        } else if (port_TextsEqual(word, "--card2")) {
            port_SetCardPath(1, argv[++i]);
        } else if (port_TextsEqual(word, "--out")) {
            port_SetDumpFolder(port_FolderPrefix(argv[++i], out_folder));
        } else if (port_TextsEqual(word, "--shots")) {
            shots = port_FolderPrefix(argv[++i], shots_folder);
        } else if (port_TextsEqual(word, "--every")) {
            if (!port_ParseNumber(argv[++i], &every) || every <= 0) {
                port_UsageError("--every takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--vram")) {
            port_SetVramPath(argv[++i]);
        } else if (port_TextsEqual(word, "--dump")) {
            port_ParseDump(argv[++i]);
        } else if (port_TextsEqual(word, "--frames")) {
            if (!port_ParseNumber(argv[++i], &frames) || frames <= 0) {
                port_UsageError("--frames takes a number, not ", argv[i]);
            }
        } else {
            port_UsageError("not known: ", word);
        }
    }
    if (argc > 0) {
        /* the slots no option named: a card image beside the program */
        port_SetDefaultCardPaths(argv[0]);
    }
    if (image == NULL) {
        port_UsageError("no --image: the game is read from your own image "
                        "of its disc",
                        "");
    }
    problem = port_OpenDisc(&g_port_disc, image);
    if (problem != NULL) {
        port_Log("rebushido: %s: %s\n", image, problem);
        return PORT_EXIT_FAILED;
    }
    port_InsertDisc(&g_port_disc);
    if (plan != NULL) {
        port_LoadPlan(plan);
    }
    if (frames > 0) {
        port_SetBlankLimit((u32)frames);
    }
    if (shots != NULL) {
        port_SetShots(shots, (u32)every);
    }
    /* where primitives may be: the console's memory, the program's own
     * variables, the game's stack */
    port_AddLinkRegion(PORT_RAM_ADDR, PORT_RAM_SIZE);
    port_AddLinkRegion(SCRATCHPAD, PORT_SCRATCHPAD_SIZE);
    port_AddLinkRegion(PORT_STACK_ADDR, PORT_STACK_SIZE);
    port_AddLinkRegion((u32)ADDR(g_port_data_start),
                       (u32)(g_port_data_end - g_port_data_start));
    port_HostCallOnStack(port_RunGame, PORT_STACK_ADDR + PORT_STACK_SIZE);
}
