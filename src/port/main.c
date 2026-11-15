/* The native port's program (docs/PORT.md, "The program"):
 *
 *   rebushido --image "Bushido Blade 2 (USA).cue" [--frames N] [--trace]
 *             [--plan FILE] [--dump NAME=ADDRESS:BYTES] [--out FOLDER]
 *
 * It gives the game the PlayStation's memory and the player's disc
 * image, and then calls the game's own main, which never returns: the
 * run ends at a vertical blank, by --frames or by the plan
 * (port_OnBlank). There is no window, no picture and no sound yet, and
 * the pads are pressed only by a plan. */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "bb2.h"

#define PATH_BYTES_MAX 1024

static PortDisc g_port_disc;
static s32 g_port_quitting;

static const char g_port_usage[] =
    "rebushido: the native port of ReBushido, as far as it goes: the game's\n"
    "start-up and menus with no picture, sound or pads (docs/PORT.md)\n"
    "\n"
    "  rebushido --image FILE [--frames N] [--trace] [--plan FILE]\n"
    "            [--dump NAME=ADDRESS:BYTES] [--out FOLDER]\n"
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
    "\n"
    "At the end it prints a line for each thing the game asked for that\n"
    "the port does not do yet, with how often: stub NAME COUNT.\n";

void port_Quit(s32 code) {
    /* once: what is written at the end may itself fail */
    if (!g_port_quitting) {
        g_port_quitting = 1;
        port_WriteDumps();
        port_PrintStubs();
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

/* `folder` with a `/` after it, if it has none. */
static const char *port_FolderPrefix(const char *folder) {
    static char prefix[PATH_BYTES_MAX];
    u32 length = port_TextLength(folder);
    u32 i;

    if (length == 0 || length + 2 > sizeof(prefix)) {
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

/* The program: its arguments, the memory, the disc, and then the game
 * (port_host.h). */
s32 port_Main(s32 argc, char **argv) {
    const char *image = NULL;
    const char *plan = NULL;
    const char *problem;
    s32 frames = 0;
    s32 i;

    port_CatchCrashes();
    port_MapMemory();
    if (argc > 0) {
        port_LoadOwnNames(argv[0]);
    }
    for (i = 1; i < argc; i++) {
        const char *word = argv[i];
        s32 has_value = i + 1 < argc;

        if (port_TextsEqual(word, "--trace")) {
            port_SetTrace(1);
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
        } else if (port_TextsEqual(word, "--out")) {
            port_SetDumpFolder(port_FolderPrefix(argv[++i]));
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
    main();
    /* the game's main does not return */
    port_Quit(PORT_EXIT_FAILED);
}
