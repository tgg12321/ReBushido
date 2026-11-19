/* The native port's program (docs/PORT.md, "The program"):
 *
 *   rebushido --image "Bushido Blade 2 (USA).cue" [--frames N] [--trace]
 *             [--plan FILE] [--dump NAME=ADDRESS:BYTES] [--out FOLDER]
 *             [--shots FOLDER] [--every N] [--vram FILE]
 *             [--wav FILE] [--spu FILE]
 *             [--card1 FILE] [--card2 FILE] [--no-cards]
 *             [--rollback-test N] [--rollback-every M] [--rollback-quiet]
 *             [--whole-state]
 *             [--net-bind ADDRESS:PORT --net-peer ADDRESS:PORT]
 *             [--link-side 0|1] [--net-latency MS] [--net-loss PERCENT]
 *             [--netplay 1|2] [--net-delay N]
 *             [--host ADDRESS:PORT | --join ADDRESS:PORT]
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
#include "port_discfiles.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_movie.h"
#include "port_music.h"
#include "port_net.h"
#include "port_spu.h"
#include "bb2.h"

#define PATH_BYTES_MAX 1024
/* The bits of an address the game can keep. */
#define ADDRESS_BITS 32
/* A picture a second, where --shots does not say how often. */
#define SHOTS_EVERY 60
/* Frames from one save of the rollback self-test to the next, where
 * --rollback-every does not say. */
#define ROLLBACK_EVERY 24
/* --net-loss is so many in a hundred. */
#define PERCENT 100
/* Frames a netplay press counts on, where --net-delay does not say, and
 * at most. */
#define NET_DELAY 2
#define NET_DELAY_MAX 8

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
    "            [--fight STAGE FIGHTER FIGHTER] [--fight-2p]\n"
    "            [--rollback-test N] [--rollback-every M] "
    "[--rollback-quiet]\n"
    "            [--whole-state]\n"
    "            [--mods FOLDER|off] [--dump-textures FOLDER]\n"
    "            [--music-mods on|off] [--dump-music FOLDER]\n"
    "            [--dump-stages FOLDER] [--weapon 1|2=NAME]\n"
    "            [--arena NAME] [--fight-cpu]\n"
    "            [--unlock-all] [--stage-select]\n"
    "            [--net-bind ADDRESS:PORT --net-peer ADDRESS:PORT]\n"
    "            [--link-side 0|1] [--net-latency MS] [--net-loss PERCENT]\n"
    "            [--netplay 1|2] [--net-delay N]\n"
    "            [--host ADDRESS:PORT | --join ADDRESS:PORT]\n"
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
    "  --fight STAGE FIGHTER FIGHTER   straight into a fight between the\n"
    "                 two, the second the CPU's, with no options page,\n"
    "                 opening or menu: the game's names in any case,\n"
    "                 `kannuki:nodachi` for a weapon (a katana if none); a\n"
    "                 name not known lists them (docs/PORT.md, \"Straight\n"
    "                 into a fight\")\n"
    "                 STAGE may be a mod's arena (docs/MODS.md, \"Custom\n"
    "                 arenas\"), fought on the stage it stands in for\n"
    "  --fight-2p     ... the second fighter is the second pad's\n"
    "  --fight-cpu    ... the first fighter is the CPU's too\n"
    "  --rollback-test N   every so often save the game's state, run N\n"
    "                 frames, put it back and run them again; stop at the\n"
    "                 first difference, and report (docs/NETPLAY.md)\n"
    "  --rollback-every M  ... M frames from one save to the next (24)\n"
    "  --rollback-quiet    ... and run them again as netplay does: nothing\n"
    "                 drawn, heard or printed; video memory not compared\n"
    "  --whole-state  save and put back every byte of the state, not only\n"
    "                 the pages written since (for measuring)\n"
    "  --run-ahead N  before each frame run the next N (0 to 2) and put\n"
    "                 the state back: what the window does to show a\n"
    "                 press sooner (docs/PORT.md, \"Input lag\")\n"
    "  --net-bind ADDRESS:PORT   a UDP socket here, for another program of\n"
    "                 the port (docs/NETPLAY.md): 127.0.0.1:PORT to play on\n"
    "                 this PC alone\n"
    "  --net-peer ADDRESS:PORT   ... and the other program's socket\n"
    "  --link-side 0|1   the game's own link cable over that socket: this\n"
    "                 console is side 0 (the first player) or side 1\n"
    "  --netplay 1|2  rollback netplay over that socket: the whole session\n"
    "                 as one machine, this program the first pad or the\n"
    "                 second, the other program the other\n"
    "  --net-delay N  ... a press counts N frames on (2); the host's is\n"
    "                 both players' in a session\n"
    "  --host ADDRESS:PORT   a session of netplay (docs/NETPLAY.md): wait\n"
    "                 for the other player at this address of this PC\n"
    "                 (0.0.0.0 for every one, 127.0.0.1 for this PC\n"
    "                 alone); the two programs check each other, the\n"
    "                 guest plays on the host's memory cards, and both go\n"
    "                 to the versus select screen\n"
    "  --join ADDRESS:PORT   ... join the host there (--net-bind says\n"
    "                 where this program's own socket is: 0.0.0.0, any\n"
    "                 port, if it does not)\n"
    "  --net-latency MS   for tests: every datagram waits this long first\n"
    "  --net-loss PERCENT ... and so many in a hundred are dropped\n"
    "  --mods FOLDER  the mods are in this folder (mods beside the program\n"
    "                 if this does not say); off for none (docs/MODS.md)\n"
    "  --dump-textures FOLDER   write each texture the game draws there as\n"
    "                 a PNG, once, named by its key: what a replacement is\n"
    "                 made from (the remastered picture only)\n"
    "  --music-mods on|off   off plays the game's own music where a mod\n"
    "                 has some (on)\n"
    "  --dump-music FOLDER   write each piece of music the game plays\n"
    "                 there as it plays, KEY.wav: what a mod's music\n"
    "                 replaces, and its key\n"
    "  --dump-stages FOLDER  write every stage's files there from the disc,\n"
    "                 by their keys: what a mod's stage begins from\n"
    "  --arena NAME   a mod's arena (arenas/NAME.arena) stands in for the\n"
    "                 stage its file names, wherever that is fought\n"
    "  --weapon 1=NAME  the fighter in place 1 (or 2) fights with a mod's\n"
    "                 weapon NAME (weapons/NAME.txt) whenever his weapon\n"
    "                 is the one it is made on\n"
    "  --unlock-all   every fighter and mode the save keeps for its\n"
    "                 progress, without writing them into it (the\n"
    "                 window's unlock_all; docs/PLAYING.md)\n"
    "  --stage-select versus the CPU and training choose their stage on\n"
    "                 the stage pages, as versus mode does (the window's\n"
    "                 stage_select_everywhere)\n"
    "\n"
    "At the end it prints a line for each thing the game asked for that\n"
    "the port does not do yet, with how often: stub NAME COUNT.\n";

/* The end of a run (port_platform.h): what it was asked to write, the
 * stubs it called, and out. */
void port_Quit(s32 code) {
    /* a frame run ahead does not end the run: the step itself does, if
     * it comes to that (port_platform.h) */
    if (g_port_ahead) {
        port_GiveUpAhead();
    }
    /* once: what is written at the end may itself fail */
    if (!g_port_quitting) {
        g_port_quitting = 1;
        port_WriteDumps();
        port_WriteSpuDump();
        port_PrintMovieSummary();
        if (port_IsWavOn()) {
            port_PrintSoundSummary();
            port_WriteWav();
        }
        port_PrintStubs();
        port_PrintRollbackReport();
        port_PrintAheadReport();
        port_EndNetplay();
        port_PrintNetReport();
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
    s32 rollback_frames = 0;
    s32 rollback_every = ROLLBACK_EVERY;
    s32 rollback_quiet = 0;
    PortNetAddress net_bind;
    PortNetAddress net_peer;
    s32 net_addresses = 0;
    s32 net_latency = 0;
    s32 net_loss = 0;
    s32 link_side = -1;
    s32 netplay_pad = 0;
    s32 net_delay = NET_DELAY;
    s32 session_role = PORT_SESSION_NONE;
    PortNetAddress session_at;
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
        } else if (port_TextsEqual(word, "--fight-cpu")) {
            port_FightBothCpu();
        } else if (port_TextsEqual(word, "--unlock-all")) {
            port_SetUnlockAll(1);
        } else if (port_TextsEqual(word, "--stage-select")) {
            port_SetStageSelectEverywhere(1);
        } else if (port_TextsEqual(word, "--fight-2p")) {
            port_FightTwoPlayers();
        } else if (port_TextsEqual(word, "--fight")) {
            if (i + 3 >= argc) {
                port_UsageError("--fight takes STAGE FIGHTER FIGHTER", "");
            }
            if (!port_ChooseFight(argv[i + 1], argv[i + 2], argv[i + 3])) {
                /* it has said what the names are */
                port_Quit(PORT_EXIT_USAGE);
            }
            i += 3;
        } else if (port_TextsEqual(word, "--rollback-quiet")) {
            rollback_quiet = 1;
        } else if (port_TextsEqual(word, "--whole-state")) {
            port_SetWholeState();
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
        } else if (port_TextsEqual(word, "--mods")) {
            port_ModsSetFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--dump-textures")) {
            port_ModsSetDumpFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--dump-music")) {
            port_MusicSetDumpFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--arena")) {
            if (i + 1 >= argc || !port_ArenaAsk(argv[++i])) {
                port_UsageError("--arena takes the name of a mod's arena", "");
            }
        } else if (port_TextsEqual(word, "--dump-stages")) {
            port_SetStageDumpFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--weapon")) {
            if (!port_WeaponsChoose(argv[++i])) {
                port_UsageError(
                    "--weapon takes 1=NAME or 2=NAME, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--music-mods")) {
            i++;
            if (port_TextsEqual(argv[i], "on") ||
                port_TextsEqual(argv[i], "off")) {
                port_MusicSetOn(port_TextsEqual(argv[i], "on"));
            } else {
                port_UsageError("--music-mods is on or off, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--rollback-test")) {
            if (!port_ParseNumber(argv[++i], &rollback_frames) ||
                rollback_frames <= 0) {
                port_UsageError(
                    "--rollback-test takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--rollback-every")) {
            if (!port_ParseNumber(argv[++i], &rollback_every) ||
                rollback_every <= 0) {
                port_UsageError(
                    "--rollback-every takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--net-bind")) {
            if (!port_ParseNetAddress(argv[++i], &net_bind)) {
                port_UsageError("--net-bind takes ADDRESS:PORT, not ", argv[i]);
            }
            net_addresses |= 1;
        } else if (port_TextsEqual(word, "--net-peer")) {
            if (!port_ParseNetAddress(argv[++i], &net_peer) ||
                net_peer.port == 0) {
                port_UsageError("--net-peer takes ADDRESS:PORT, not ", argv[i]);
            }
            net_addresses |= 2;
        } else if (port_TextsEqual(word, "--link-side")) {
            if (!port_ParseNumber(argv[++i], &link_side) || link_side < 0 ||
                link_side > 1) {
                port_UsageError("--link-side takes 0 or 1, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--netplay")) {
            if (!port_ParseNumber(argv[++i], &netplay_pad) || netplay_pad < 1 ||
                netplay_pad > PORT_PADS) {
                port_UsageError("--netplay takes 1 or 2, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--host") ||
                   port_TextsEqual(word, "--join")) {
            if (session_role != PORT_SESSION_NONE) {
                port_UsageError("--host or --join, once: ", word);
            }
            session_role = port_TextsEqual(word, "--host") ? PORT_SESSION_HOST
                                                           : PORT_SESSION_JOIN;
            if (!port_ParseNetAddress(argv[++i], &session_at) ||
                session_at.port == 0) {
                port_UsageError(
                    "--host and --join take ADDRESS:PORT, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--net-delay")) {
            if (!port_ParseNumber(argv[++i], &net_delay) || net_delay < 0 ||
                net_delay > NET_DELAY_MAX) {
                port_UsageError(
                    "--net-delay takes a number to 8, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--net-latency")) {
            if (!port_ParseNumber(argv[++i], &net_latency) || net_latency < 0) {
                port_UsageError("--net-latency takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--net-loss")) {
            if (!port_ParseNumber(argv[++i], &net_loss) || net_loss < 0 ||
                net_loss > PERCENT) {
                port_UsageError(
                    "--net-loss takes a number to 100, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--frames")) {
            if (!port_ParseNumber(argv[++i], &frames) || frames <= 0) {
                port_UsageError("--frames takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--run-ahead")) {
            s32 ahead = 0;

            if (!port_ParseNumber(argv[++i], &ahead) || ahead < 0 ||
                ahead > PORT_AHEAD_MAX) {
                port_UsageError("--run-ahead takes 0, 1 or 2, not ", argv[i]);
            }
            port_SetRunAhead(ahead);
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
    /* the disc's files by name: the music's keys, the stages' files */
    port_ReadDiscFiles(&g_port_disc);
    /* the mods, before the game reads anything a mod could stand in for */
    port_ModsStart(argc > 0 ? argv[0] : NULL);
    port_MusicStart();
    port_StartStageMods(&g_port_disc);
    port_WeaponsStart();
    port_ArenasStart();
    if (plan != NULL) {
        port_LoadPlan(plan);
    }
    if (frames > 0) {
        port_SetBlankLimit((u32)frames);
    }
    if (shots != NULL) {
        port_SetShots(shots, (u32)every);
    }
    if (session_role != PORT_SESSION_NONE) {
        if (link_side >= 0 || netplay_pad != 0 || rollback_frames > 0) {
            port_UsageError("a session (--host, --join) is neither the link "
                            "cable, --netplay nor the rollback self-test",
                            "");
        }
        if ((net_addresses & 2) != 0 ||
            (session_role == PORT_SESSION_HOST && net_addresses != 0)) {
            port_UsageError("--host is where this program listens, and "
                            "--join says whom it talks to: no --net-peer, "
                            "and no --net-bind with --host",
                            "");
        }
        if (session_role == PORT_SESSION_JOIN && net_addresses == 0) {
            /* any address of this PC, any port: the host answers where
             * it was spoken to from */
            net_bind.address = 0;
            net_bind.port = 0;
        }
    } else if (net_addresses != 0 && net_addresses != 3) {
        port_UsageError("the network needs both --net-bind and --net-peer", "");
    }
    if ((link_side >= 0 || netplay_pad != 0) && net_addresses == 0) {
        port_UsageError("--link-side and --netplay need --net-bind and "
                        "--net-peer",
                        "");
    }
    if (link_side >= 0 && netplay_pad != 0) {
        port_UsageError("the link cable or netplay, not both", "");
    }
    if (netplay_pad != 0 && rollback_frames > 0) {
        port_UsageError("netplay or the rollback self-test, not both", "");
    }
    if (netplay_pad != 0) {
        port_SetNetplay(netplay_pad - 1, (u32)net_delay);
    }
    if (session_role == PORT_SESSION_HOST) {
        port_SetNetListen(&session_at);
    } else if (session_role == PORT_SESSION_JOIN) {
        port_SetNetAddresses(&net_bind, &session_at);
    } else if (net_addresses != 0) {
        port_SetNetAddresses(&net_bind, &net_peer);
        port_SetLinkSide(link_side);
    }
    if (session_role != PORT_SESSION_NONE || net_addresses != 0) {
        port_SetNetTrouble((u32)net_latency, (u32)net_loss);
        if (!port_StartNet()) {
            return PORT_EXIT_FAILED;
        }
    }
    if (session_role != PORT_SESSION_NONE) {
        /* the other program found and held to this one, the host's cards
         * in hand: netplay is on from here (port_net.h) */
        port_SetSession(session_role, &session_at);
        if (!port_StartSession(&g_port_disc, (u32)net_delay)) {
            port_Log("rebushido: no session\n");
            return PORT_EXIT_FAILED;
        }
    }
#if RB_REMASTER_VIEW
    if (port_IsNetplayOn()) {
        /* What the game computes of a frame must not hang on either
         * player's picture: the stage is computed as far as the console
         * draws it, on both machines, whatever each draws (docs/
         * NETPLAY.md, section 13). */
        PortRemasterView view;

        port_RemasterGetView(&view);
        if (view.distance != PORT_DISTANCE_ORIGINAL) {
            port_Log("netplay: the draw distance is the console's for this "
                     "session (the game's state may not depend on it)\n");
        }
        view.distance = PORT_DISTANCE_ORIGINAL;
        port_RemasterSetView(&view);
        /* and which faces it keeps at the left edge must not follow the
         * picture either: a session fixes the wider of the two players'
         * shapes (session.c), --netplay this program's own */
        if (port_GetSessionRole() == PORT_SESSION_NONE) {
            port_RemasterFixMargin(view.aspect_w, view.aspect_h);
        }
    }
#endif
    if (rollback_frames > 0) {
        port_StartRollbackTest(
            (u32)rollback_frames, (u32)rollback_every, rollback_quiet);
    }
    /* where primitives may be: the console's memory, the game's stack,
     * and the game's own variables with the rest of its state, the two
     * runs a snapshot saves (port_state.h). What is outside the state
     * (the remastered side channel, the window's) and what cannot be
     * written hold none, and need no place among the low 24 bits of an
     * address that an ordering table's link has (docs/PORT.md, "Where
     * the program's variables are") */
    port_AddLinkRegion(PORT_RAM_ADDR, PORT_RAM_SIZE);
    port_AddLinkRegion(SCRATCHPAD, PORT_SCRATCHPAD_SIZE);
    port_AddLinkRegion(PORT_STACK_ADDR, PORT_STACK_SIZE);
    port_AddLinkRegion((u32)ADDR(g_port_game_data),
                       (u32)(g_port_state_data_end - g_port_game_data));
    port_AddLinkRegion((u32)ADDR(g_port_game_bss),
                       (u32)(g_port_state_bss_end - g_port_game_bss));
#if RB_REMASTER_VIEW
    /* ... and the port's own buffers of primitives, where this run draws
     * more of the stage than the console does (port_remaster.h) */
    if (port_RemasterPrimBufferBytes() != 0) {
        port_AddLinkRegion(PORT_PRIMS_ADDR, PORT_PRIMS_SIZE);
    }
#endif
    /* the game, a step at a time (step.c): the rollback self-test has
     * its turn between two */
    port_StartGame(port_RunGame);
    if (port_IsNetplayOn()) {
        port_RunNetplay();
    }
    for (;;) {
        port_BeforeStep();
        port_StepGame();
        port_AfterStep();
        port_OnRollbackFrame();
    }
}
