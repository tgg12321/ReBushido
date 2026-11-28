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
#include "port_online.h"
#include "port_ranked.h"
#include "port_remix.h"
#include "port_replay.h"
#include "port_room.h"
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
#define DECIMAL 10

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
    "            [--sound original|remastered] [--headphones on|off]\n"
    "            [--card1 FILE] [--card2 FILE] [--no-cards]\n"
    "            [--fight STAGE FIGHTER FIGHTER] [--fight-2p]\n"
    "            [--rollback-test N] [--rollback-every M] "
    "[--rollback-quiet]\n"
    "            [--whole-state]\n"
    "            [--mods FOLDER|off] [--dump-textures FOLDER]\n"
    "            [--music-mods on|off] [--dump-music FOLDER]\n"
    "            [--fight-music stage|original|none|KEY]\n"
    "            [--dump-stages FOLDER] [--weapon 1|2=NAME]\n"
    "            [--dump-movie FOLDER]\n"
    "            [--arena NAME] [--fight-cpu] [--match RULE[:N]]\n"
    "            [--cpu LEVEL[,LEVEL]] [--cpu-seed N] [--cpu-log FILE]\n"
    "            [--unlock-all] [--stage-select] [--menu rebushido|original]\n"
    "            [--net-bind ADDRESS:PORT --net-peer ADDRESS:PORT]\n"
    "            [--link-side 0|1] [--net-latency MS] [--net-loss PERCENT]\n"
    "            [--netplay 1|2] [--net-delay N]\n"
    "            [--host ADDRESS:PORT | --join ADDRESS:PORT]\n"
    "            [--lobby ADDRESS:PORT [--room NAME | --join CODE]\n"
    "             [--player NAME] [--room-password WORD]]\n"
    "            [--punch-to ADDRESS:PORT] [--net-paths LIST]\n"
    "            [--check-online]\n"
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
    "  --partners FIGHTER FIGHTER   ... and with --teams cpu|pads, the\n"
    "                 second of each team (as --fight names one); without\n"
    "                 it each team is two of its first\n"
    "  --fight-2p     ... the second fighter is the second pad's\n"
    "  --fight-cpu    ... the first fighter is the CPU's too\n"
    "  --cpu A[,B]    which CPU plays a CPU's fighter, in the first place\n"
    "                 and the second: original (the game's own), or the\n"
    "                 port's novice, adept, veteran or master (0 to 4;\n"
    "                 docs/CPU.md)\n"
    "  --cpu-seed N   ... mixed into the port's CPU's dice\n"
    "  --cpu-log FILE each fighter's pad and move, a line a frame\n"
    "  --rollback-test N   every so often save the game's state, run N\n"
    "                 frames, put it back and run them again; stop at the\n"
    "                 first difference, and report (docs/NETPLAY.md)\n"
    "  --rollback-every M  ... M frames from one save to the next (24)\n"
    "  --rollback-quiet    ... and run them again as netplay does: nothing\n"
    "                 drawn, heard or printed; video memory not compared\n"
    "  --replays FOLDER   keep every fight as a replay in FOLDER\n"
    "                 (docs/PORT.md, \"Replays\")\n"
    "  --replays-keep N   ... and at most the N newest (100; 0: all)\n"
    "  --stats FILE   keep the record of the fights finished in FILE (the\n"
    "                 settings menu's Stats; docs/PORT.md, \"Stats\")\n"
    "  --replay FILE  play a replay from the first frame, then end\n"
    "  --replay-check FILE   play it as fast as it runs, nothing drawn or\n"
    "                 heard, from the start again at its middle and on\n"
    "                 to before its first cut, and end: 0 where every\n"
    "                 hash was the one recorded, 1 on a desync or a\n"
    "                 replay not played\n"
    "  --replay-from-boot   ... from power on (its lead-in), as another\n"
    "                 build plays it, even on this one\n"
    "  --replay-differ N   ... and inputs N on (30) from power on changed, as\n"
    "                 a game that went otherwise would have them (a test)\n"
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
    "                 ADDRESS:PORT is IPv4's a.b.c.d:PORT or IPv6's\n"
    "                 [x:y::z]:PORT; a bind of 0.0.0.0 is of both kinds\n"
    "  --lobby ADDRESS:PORT   meet by way of the lobby server there\n"
    "                 (docs/NETPLAY.md, section 14): --room NAME opens a\n"
    "                 room and hosts it, --join CODE joins the room of that\n"
    "                 code; the two are told each other's addresses, try\n"
    "                 them all at once, and play through the server where\n"
    "                 no way is found\n"
    "  --players 2|4|2x2   with --host: the session's players: two, four\n"
    "                 on four PCs (the host meets three guests; --host's\n"
    "                 port and the two after it, or a room of four at the\n"
    "                 lobby), or two on each of two PCs (docs/NETPLAY.md,\n"
    "                 section 18); four play two on two\n"
    "  --player NAME  ... the name the lobby shows\n"
    "  --room-password WORD   ... the room's password (the host's), or\n"
    "                 the one typed to join it\n"
    "  --punch-to ADDRESS:PORT   with --host and no lobby: the other\n"
    "                 player's public address, sent to while waiting, so\n"
    "                 that both routers let the two through (both keep\n"
    "                 the port they said)\n"
    "  --net-paths LIST   for tests: the ways a lobby's meeting may take,\n"
    "                 of public,lan,mapped,v6,relay\n"
    "  --mapped ADDRESS:PORT   for tests: a port the router mapped, offered\n"
    "                 as a way (the window maps it itself)\n"
    "  --check-online print the setup code two players compare, and end\n"
    "  --session-at N for tests: the session --host or --join says begins\n"
    "                 in this program after N frames of its own, with no\n"
    "                 start again (docs/NETPLAY.md, section 15); when it\n"
    "                 ends the program goes on with its own game\n"
    "  --sessions K   ... K of them, one after another, N frames apart;\n"
    "                 the program ends after the last\n"
    "  --net-seed N   for tests: the host's seed, not one of its clock's\n"
    "  --room-open NAME   with --lobby: open a room of several there\n"
    "                 (docs/NETPLAY.md, section 16) and play and watch its\n"
    "                 matches as they come; --room-enter CODE enters one\n"
    "  --room-rule fixed|winner|off   ... the rotation (the opener's)\n"
    "  --room-size 2|4   ... its matches' players (the opener's): 4, two\n"
    "                 on two, four PCs\n"
    "  --room-watcher ... a spectator, not in the rotation; --room-no-watch\n"
    "                 watches none of the matches it is not in\n"
    "  --room-matches N   ... leave the room and end once it has had N\n"
    "                 results\n"
    "  --room-side open|join   ... open a match beside the room's, or join\n"
    "                 one, and end after it\n"
    "  --leave-at N:F for tests: leave match N of the room at its frame F\n"
    "                 (--room-quit-left: and the room, and end)\n"
    "  --room-tournament WORDS   ... the room's host begins a tournament\n"
    "                 (docs/NETPLAY.md, section 22) once every member it\n"
    "                 names is in: --tournament's words, @Name a member;\n"
    "                 --room-until-champion leaves and ends once it is over\n"
    "  --ranked       with --lobby: play ranked (docs/NETPLAY.md, section\n"
    "                 17): into the queue, its matches played as they\n"
    "                 come, as --player; --ranked-matches N ends the\n"
    "                 program once N are decided (--leave-at N:F leaves\n"
    "                 this program's Nth); --ranked-lists prints the\n"
    "                 ladder then\n"
    "  --identity FILE    the ranked identity's file (made if it is not\n"
    "                 there; rebushido-identity.key here if not said)\n"
    "  --ranked-lie   for tests: report the other player won (a dispute)\n"
    "  --net-plan-pads   for tests: in a session, the plan's pad of this\n"
    "                 program's place alone (a plan presses both)\n"
    "  --resync-auto N   where the machines' games part online, the host\n"
    "                 puts its state on every one by itself N frames after\n"
    "                 (180; 0: only at a press, F5) (docs/NETPLAY.md, 21)\n"
    "  --resync-ask-at F   for tests: this program's player asks for a\n"
    "                 resync at its frame F, once the games have parted\n"
    "  --net-part-at F   for tests: this program's state is made another's\n"
    "                 at its frame F (a word of the RAM nothing reads)\n"
    "  --session-differ build|disc|mods|protocol   for tests: say another\n"
    "  --net-latency MS   for tests: every datagram waits this long first\n"
    "  --net-loss PERCENT ... and so many in a hundred are dropped\n"
    "  --mods FOLDER  the mods are in this folder (mods beside the program\n"
    "                 if this does not say); off for none (docs/MODS.md)\n"
    "  --dump-textures FOLDER   write each texture the game draws there as\n"
    "                 a PNG, once, named by its key: what a replacement is\n"
    "                 made from (the remastered picture only)\n"
    "  --dump-movie FOLDER   each frame of the movie as it is first shown,\n"
    "                 movie-NNNN.png by its number in the stream: what\n"
    "                 tools/port_movies.py makes larger\n"
    "  --sound original|remastered   the chip's own sound (built in), or\n"
    "                 the sound remastered: a finer resampling, a reverb\n"
    "                 of the stage's own (docs/PORT.md, \"The sound\n"
    "                 remastered\"); what --wav keeps and a card plays\n"
    "  --headphones on|off   the remastered sound placed for headphones\n"
    "                 (off)\n"
    "  --music-mods on|off   off plays the game's own music where a mod\n"
    "                 has some (on)\n"
    "  --dump-music FOLDER   write each piece of music the game plays\n"
    "                 there as it plays, KEY.wav: what a mod's music\n"
    "                 replaces, and its key\n"
    "  --fight-music stage|original|none|KEY   a fight's music: the\n"
    "                 stage's piece in every mode, the game's own rule\n"
    "                 (with nothing said), none, or the piece KEY\n"
    "                 (bb2_m1-5, or a mod's); the stage select's row\n"
    "                 starts there (the window's fight_music)\n"
    "  --dump-stages FOLDER  write every stage's files there from the disc,\n"
    "                 by their keys: what a mod's stage begins from\n"
    "  --arena NAME   a mod's arena (arenas/NAME.arena) stands in for the\n"
    "                 stage its file names, wherever that is fought\n"
    "  --time-of-day W  stage (the default), dawn, day, dusk or night:\n"
    "                 the time the stages start at (the window's\n"
    "                 time_of_day; docs/PLAYING.md, \"The time of day\")\n"
    "  --teams one|cpu|pads  one on one, or two on two with the CPU's\n"
    "                 partners or the third and fourth pads' (the\n"
    "                 window's teams; docs/PLAYING.md, \"Two on two\")\n"
    "  --training-dummy stand|guard|playback  what training's dummy does\n"
    "                 (the window's training_dummy; docs/PLAYING.md,\n"
    "                 \"Training\")\n"
    "  --training-slot N  the slot, 1 to 4, recorded and played back\n"
    "  --training-random  playback plays a random slot of those recorded\n"
    "  --training-slow N  slow motion shows each frame N times, 2 to 8\n"
    "  --tournament SPEC  a local tournament begun at the start: words split\n"
    "                 by commas, single or double, random or order (the\n"
    "                 seeding), one, first_to:N, stock:N or crew:N (2 to\n"
    "                 5), seed:N, and two\n"
    "                 to eight entrants, pN (the PC's pad N) or\n"
    "                 cpu:LEVEL[:FIGHTER[:WEAPON]], each perhaps /Name\n"
    "                 (docs/PLAYING.md, \"Tournament\")\n"
    "  --tournament-auto N  ... each match begun N inputs after the bracket is\n"
    "                 up at the main menu, as if Play were pressed (a test's)\n"
    "  --tournament-quit  ... and the program ended N inputs after it is over\n"
    "  --tournament-file FILE  keep a local tournament not finished in FILE,\n"
    "                 to be resumed (the window's is beside the settings)\n"
    "  --tournament-resume  the tournament kept there begun again at the start\n"
    "  --tournament-quit-in N  the program ended in the fight of the match\n"
    "                 after N decided (a test's, of a tournament resumed)\n"
    "  --match single|stock:N|first_to:N|crew:N  the match rule the stage\n"
    "                 select starts at, N 1 to 9, a crew's 2 to 8 (five\n"
    "                 if not given; the window's match_rule and\n"
    "                 match_count; docs/PLAYING.md, \"Match rules\")\n"
    "  --weapon 1=NAME  the fighter in place 1 (or 2) fights with a mod's\n"
    "                 weapon NAME (weapons/NAME.txt) whenever his weapon\n"
    "                 is the one it is made on\n"
    "  --unlock-all   every fighter and mode the save keeps for its\n"
    "                 progress, without writing them into it (the\n"
    "                 window's unlock_all; docs/PLAYING.md)\n"
    "  --stage-select versus the CPU and training choose their stage on\n"
    "                 the stage pages, as versus mode does (the window's\n"
    "                 stage_select_everywhere)\n"
    "  --menu W       rebushido: ReBushido's main menu, with no options page\n"
    "                 at boot (the window's menu; docs/PLAYING.md, \"The\n"
    "                 main menu\"); original, as it is without it: the\n"
    "                 game's own menus\n"
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
        port_ReplayAtQuit();
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

/* the longest of the match rules' names, and its end */
#define MATCH_NAME_BYTES 12

/* --match's `single`, `stock:N`, `first_to:N` or `crew:N` (port_match.h),
 * a crew of five without its number. */
static void port_ParseMatch(const char *text) {
    static const char * const rules[PORT_MATCH_RULES] = {
        "single", "stock", "first_to", "crew"};
    char name[MATCH_NAME_BYTES];
    s32 count = -1;
    s32 length = 0;
    s32 rule = PORT_MATCH_RULES;

    while (text[length] != 0 && text[length] != ':' &&
           length < MATCH_NAME_BYTES - 1) {
        name[length] = text[length];
        length++;
    }
    name[length] = 0;
    if (text[length] == ':' && text[length + 1] >= '1' &&
        text[length + 1] <= '9' && text[length + 2] == 0) {
        count = text[length + 1] - '0';
    } else if (text[length] != 0) {
        name[0] = 0;
    }
    for (rule = 0; rule < PORT_MATCH_RULES; rule++) {
        if (port_TextsEqual(name, rules[rule])) {
            break;
        }
    }
    if (rule >= PORT_MATCH_RULES) {
        port_UsageError(
            "--match takes single, stock:N, first_to:N or crew:N (N 1 to 9), "
            "not ",
            text);
    }
    if (count < 0) {
        count = rule == PORT_MATCH_CREW ? PORT_MATCH_CREW_DEFAULT
                                        : PORT_MATCH_COUNT_DEFAULT;
    }
    port_SetMatchDefault(rule, count);
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

/* `text` into `out`, cut to `room`. */
static void port_KeepWord(char *out, u32 room, const char *text) {
    u32 i;

    for (i = 0; text != NULL && text[i] != 0 && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = 0;
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
    static char movie_folder[PATH_BYTES_MAX];
    const char *image = NULL;
    const char *plan = NULL;
    const char *replay = NULL;
    s32 replay_quiet = 0;
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
    const char *session_text = NULL;
    PortNetAddress lobby;
    s32 has_lobby = 0;
    const char *room = NULL;
    const char *player = "";
    const char *password = NULL;
    PortNetAddress punch_to;
    s32 has_punch_to = 0;
    PortNetAddress mapped;
    u32 paths = 0;
    s32 check_online = 0;
    s32 session_after = 0;
    s32 sessions = 1;
    const char *bind_text = "";
    const char *key_text = "";
    const char *punch_text = "";
    const char *lobby_text = "";
    /* --players: the session's players and their PCs (docs/NETPLAY.md,
     * section 18) */
    s32 session_pads = 2;
    s32 session_pcs = 2;
    /* a room of several (--room-open, --room-enter) */
    static PortOnlineRoom room_auto;
    s32 i;

    port_CatchCrashes();
    room_auto.rule = PORT_ROOM_RULE_FIXED;
    room_auto.size = 2;
    room_auto.plays = TRUE;
    room_auto.watch = TRUE;
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

        if (port_TextsEqual(word, "--check-online")) {
            check_online = 1;
        } else if (port_TextsEqual(word, "--trace")) {
            port_SetTrace(1);
        } else if (port_TextsEqual(word, "--no-cards")) {
            port_SetCardsOff();
        } else if (port_TextsEqual(word, "--replay-from-boot")) {
            port_SetReplayFromBoot(1);
        } else if (port_TextsEqual(word, "--fight-cpu")) {
            port_ReplayLeadBreak("--fight-cpu");
            port_FightBothCpu();
        } else if (port_TextsEqual(word, "--cpu") && has_value) {
            if (!port_CpuParseLevels(argv[++i])) {
                port_UsageError("--cpu takes original, novice, adept, veteran "
                                "or master (0 to 4), or two of them with a "
                                "comma, not ",
                                argv[i]);
            }
        } else if (port_TextsEqual(word, "--cpu-seed") && has_value) {
            s32 seed;

            if (!port_ParseNumber(argv[++i], &seed)) {
                port_UsageError("--cpu-seed takes a number, not ", argv[i]);
            }
            port_CpuSetAskedSeed((u32)seed);
        } else if (port_TextsEqual(word, "--cpu-tune") && has_value) {
            if (!port_CpuParseTune(argv[++i])) {
                port_UsageError(
                    "--cpu-tune takes LEVEL:FIELD=VALUE,..., not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--cpu-log") && has_value) {
            port_CpuSetLog(argv[++i]);
        } else if (port_TextsEqual(word, "--unlock-all")) {
            port_SetUnlockAll(1);
        } else if (port_TextsEqual(word, "--stage-select")) {
            port_SetStageSelectEverywhere(1);
        } else if (port_TextsEqual(word, "--menu") && has_value) {
            if (port_TextsEqual(argv[i + 1], "rebushido")) {
                port_SetTopMenuSwitch(1);
            } else if (port_TextsEqual(argv[i + 1], "original")) {
                port_SetTopMenuSwitch(0);
            } else {
                port_UsageError(
                    "--menu takes rebushido or original, not ", argv[i + 1]);
            }
            i++;
        } else if (port_TextsEqual(word, "--fight-2p")) {
            port_ReplayLeadBreak("--fight-2p");
            port_FightTwoPlayers();
        } else if (port_TextsEqual(word, "--fight")) {
            port_ReplayLeadBreak("the run began in a fight (--fight)");
            if (i + 3 >= argc) {
                port_UsageError("--fight takes STAGE FIGHTER FIGHTER", "");
            }
            if (!port_ChooseFight(argv[i + 1], argv[i + 2], argv[i + 3])) {
                /* it has said what the names are */
                port_Quit(PORT_EXIT_USAGE);
            }
            i += 3;
        } else if (port_TextsEqual(word, "--partners")) {
            port_ReplayLeadBreak("--partners");
            if (i + 2 >= argc) {
                port_UsageError("--partners takes FIGHTER FIGHTER", "");
            }
            if (!port_ChooseFightPartners(argv[i + 1], argv[i + 2])) {
                /* it has said what the names are */
                port_Quit(PORT_EXIT_USAGE);
            }
            i += 2;
        } else if (port_TextsEqual(word, "--rollback-quiet")) {
            rollback_quiet = 1;
        } else if (port_TextsEqual(word, "--whole-state")) {
            port_SetWholeState();
        } else if (port_TextsEqual(word, "--ranked")) {
            room_auto.on = TRUE;
            room_auto.ranked = TRUE;
        } else if (port_TextsEqual(word, "--ranked-lists")) {
            room_auto.lists = TRUE;
        } else if (port_TextsEqual(word, "--ranked-lie")) {
            room_auto.lie = TRUE;
        } else if (port_TextsEqual(word, "--tournament-quit")) {
            port_SetTournamentQuit();
        } else if (port_TextsEqual(word, "--tournament-resume")) {
            port_SetTournamentResumeAtStart();
        } else if (port_TextsEqual(word, "--room-until-champion")) {
            room_auto.until_champion = TRUE;
        } else if (port_TextsEqual(word, "--room-quit-left")) {
            room_auto.quit_left = TRUE;
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
        } else if (port_TextsEqual(word, "--replays")) {
            port_SetReplayFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--stats")) {
            port_SetStatsFile(argv[++i]);
        } else if (port_TextsEqual(word, "--replays-keep")) {
            s32 keep;

            if (!port_ParseNumber(argv[i + 1], &keep) || keep < 0) {
                port_UsageError(
                    "--replays-keep takes a count, not ", argv[i + 1]);
            }
            port_SetReplayKeep((u32)keep, 0);
            i++;
        } else if (port_TextsEqual(word, "--replay-differ")) {
            s32 input;

            if (!port_ParseNumber(argv[i + 1], &input) || input < 0) {
                port_UsageError("--replay-differ takes an input's number, not ",
                                argv[i + 1]);
            }
            port_SetReplayDiffer((u32)input);
            i++;
        } else if (port_TextsEqual(word, "--replay") ||
                   port_TextsEqual(word, "--replay-check")) {
            replay_quiet = port_TextsEqual(word, "--replay-check");
            replay = argv[++i];
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
        } else if (port_TextsEqual(word, "--dump-movie")) {
            port_MovieSetDumpFolder(port_FolderPrefix(argv[++i], movie_folder));
        } else if (port_TextsEqual(word, "--dump-music")) {
            port_MusicSetDumpFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--arena")) {
            port_ReplayLeadBreak("--arena");
            if (i + 1 >= argc || !port_ArenaAsk(argv[++i])) {
                port_UsageError("--arena takes the name of a mod's arena", "");
            }
        } else if (port_TextsEqual(word, "--time-of-day")) {
            static const char * const times[PORT_TIME_COUNT] = {
                "stage", "dawn", "day", "dusk", "night"};
            s32 time = PORT_TIME_COUNT;

            if (i + 1 < argc) {
                i++;
                for (time = 0; time < PORT_TIME_COUNT; time++) {
                    if (port_TextsEqual(argv[i], times[time])) {
                        break;
                    }
                }
            }
            if (time >= PORT_TIME_COUNT) {
                port_UsageError("--time-of-day takes stage, dawn, day, "
                                "dusk or night",
                                "");
            }
            port_SetStageTimeDefault(time);
        } else if (port_TextsEqual(word, "--match")) {
            port_ParseMatch(i + 1 < argc ? argv[++i] : "");
        } else if (port_TextsEqual(word, "--training-dummy") && has_value) {
            PortTrainingSwitches switches = *port_GetTrainingSwitches();
            const char *does = argv[++i];

            if (port_TextsEqual(does, "stand")) {
                switches.dummy = PORT_TRAINING_STAND;
            } else if (port_TextsEqual(does, "guard")) {
                switches.dummy = PORT_TRAINING_GUARD;
            } else if (port_TextsEqual(does, "playback")) {
                switches.dummy = PORT_TRAINING_PLAYBACK;
            } else {
                port_UsageError(
                    "--training-dummy takes stand, guard or playback, not ",
                    does);
            }
            port_SetTrainingSwitches(&switches);
        } else if ((port_TextsEqual(word, "--training-slot") ||
                    port_TextsEqual(word, "--training-slow")) &&
                   has_value) {
            PortTrainingSwitches switches = *port_GetTrainingSwitches();
            s32 is_slot = port_TextsEqual(word, "--training-slot");
            const char *value = argv[++i];
            s32 number = value[0] >= '1' && value[0] <= '9' && value[1] == 0
                             ? value[0] - '0'
                             : 0;

            if (is_slot && number >= 1 && number <= PORT_TRAINING_SLOTS) {
                switches.slot = number - 1;
            } else if (!is_slot && number >= PORT_TRAINING_SLOW_LEAST &&
                       number <= PORT_TRAINING_SLOW_MOST) {
                switches.slow = number;
            } else {
                port_UsageError(is_slot ? "--training-slot takes 1 to 4, not "
                                        : "--training-slow takes 2 to 8, not ",
                                value);
            }
            port_SetTrainingSwitches(&switches);
        } else if (port_TextsEqual(word, "--tournament") && has_value) {
            if (!port_AskTournamentWords(argv[++i])) {
                port_UsageError("--tournament: not a tournament: ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--tournament-file") && has_value) {
            port_SetTournamentFile(argv[++i]);
        } else if (port_TextsEqual(word, "--tournament-quit-in") && has_value) {
            s32 decided;

            if (!port_ParseNumber(argv[++i], &decided) || decided < 0) {
                port_UsageError("--tournament-quit-in takes a number of "
                                "matches, not ",
                                argv[i]);
            }
            port_SetTournamentQuitIn(decided);
        } else if (port_TextsEqual(word, "--tournament-auto") && has_value) {
            s32 steps;

            if (!port_ParseNumber(argv[++i], &steps) || steps < 1) {
                port_UsageError("--tournament-auto takes a number of steps, "
                                "not ",
                                argv[i]);
            }
            port_SetTournamentAuto(steps);
        } else if (port_TextsEqual(word, "--training-random")) {
            PortTrainingSwitches switches = *port_GetTrainingSwitches();

            switches.random = TRUE;
            port_SetTrainingSwitches(&switches);
        } else if (port_TextsEqual(word, "--teams") && has_value) {
            const char *teams = argv[++i];

            if (port_TextsEqual(teams, "one")) {
                port_SetTeamsDefault(PORT_TEAMS_ONE);
            } else if (port_TextsEqual(teams, "cpu")) {
                port_SetTeamsDefault(PORT_TEAMS_TWO_CPU);
            } else if (port_TextsEqual(teams, "pads")) {
                port_SetTeamsDefault(PORT_TEAMS_TWO_PADS);
            } else {
                port_UsageError("--teams takes one, cpu or pads, not ", teams);
            }
        } else if (port_TextsEqual(word, "--dump-stages")) {
            port_SetStageDumpFolder(argv[++i]);
        } else if (port_TextsEqual(word, "--weapon")) {
            port_ReplayLeadBreak("--weapon");
            if (!port_WeaponsChoose(argv[++i])) {
                port_UsageError(
                    "--weapon takes 1=NAME or 2=NAME, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--sound") && has_value) {
            i++;
            if (port_TextsEqual(argv[i], "original") ||
                port_TextsEqual(argv[i], "remastered")) {
                port_SetSoundRemastered(port_TextsEqual(argv[i], "remastered"));
            } else {
                port_UsageError(
                    "--sound is original or remastered, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--headphones") && has_value) {
            i++;
            if (port_TextsEqual(argv[i], "on") ||
                port_TextsEqual(argv[i], "off")) {
                port_SetSoundHeadphones(port_TextsEqual(argv[i], "on"));
            } else {
                port_UsageError("--headphones is on or off, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--music-mods")) {
            i++;
            if (port_TextsEqual(argv[i], "on") ||
                port_TextsEqual(argv[i], "off")) {
                port_MusicSetOn(port_TextsEqual(argv[i], "on"));
            } else {
                port_UsageError("--music-mods is on or off, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--fight-music")) {
            i++;
            if (port_TextsEqual(argv[i], "stage")) {
                port_SetFightMusic(PORT_FIGHT_MUSIC_STAGE);
            } else if (port_TextsEqual(argv[i], "original")) {
                port_SetFightMusic(PORT_FIGHT_MUSIC_ORIGINAL);
            } else if (port_TextsEqual(argv[i], "none")) {
                port_SetFightMusic(PORT_FIGHT_MUSIC_NONE);
            } else {
                /* a piece's key, found once the mods are read */
                port_SetFightMusicPiece(argv[i]);
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
        } else if (port_TextsEqual(word, "--session-at")) {
            if (!port_ParseNumber(argv[++i], &session_after) ||
                session_after <= 0) {
                port_UsageError("--session-at takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--sessions")) {
            if (!port_ParseNumber(argv[++i], &sessions) || sessions <= 0) {
                port_UsageError("--sessions takes a number, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--net-seed")) {
            s32 seed = 0;

            if (!port_ParseNumber(argv[++i], &seed) || seed < 0) {
                port_UsageError("--net-seed takes a number, not ", argv[i]);
            }
            port_SetSessionSeed((u32)seed);
        } else if (port_TextsEqual(word, "--net-bind")) {
            bind_text = argv[i + 1];
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
            port_ReplayLeadBreak("the program began a session or a link");
            if (!port_ParseNumber(argv[++i], &link_side) || link_side < 0 ||
                link_side > 1) {
                port_UsageError("--link-side takes 0 or 1, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--netplay")) {
            port_ReplayLeadBreak("the program began a session or a link");
            if (!port_ParseNumber(argv[++i], &netplay_pad) || netplay_pad < 1 ||
                netplay_pad > PORT_PADS) {
                port_UsageError("--netplay takes 1 or 2, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--host") ||
                   port_TextsEqual(word, "--join")) {
            port_ReplayLeadBreak("the program began a session or a link");
            if (session_role != PORT_SESSION_NONE) {
                port_UsageError("--host or --join, once: ", word);
            }
            session_role = port_TextsEqual(word, "--host") ? PORT_SESSION_HOST
                                                           : PORT_SESSION_JOIN;
            /* an address, or with --lobby a room's code: read below */
            session_text = argv[++i];
        } else if (port_TextsEqual(word, "--lobby") ||
                   port_TextsEqual(word, "--relay")) {
            lobby_text = argv[i + 1];
            if (!port_ParseNetAddress(argv[++i], &lobby) || lobby.port == 0) {
                port_UsageError("--lobby takes ADDRESS:PORT, not ", argv[i]);
            }
            has_lobby = 1;
        } else if (port_TextsEqual(word, "--room")) {
            room = argv[++i];
        } else if (port_TextsEqual(word, "--room-open") ||
                   port_TextsEqual(word, "--room-enter")) {
            room_auto.on = TRUE;
            port_KeepWord(port_TextsEqual(word, "--room-open") ? room_auto.name
                                                               : room_auto.code,
                          PORT_ONLINE_TEXT_BYTES, argv[++i]);
        } else if (port_TextsEqual(word, "--room-rule")) {
            i++;
            room_auto.rule =
                port_TextsEqual(argv[i], "winner") ? PORT_ROOM_RULE_WINNER
                : port_TextsEqual(argv[i], "off")  ? PORT_ROOM_RULE_OFF
                                                   : PORT_ROOM_RULE_FIXED;
            if (!port_TextsEqual(argv[i], "winner") &&
                !port_TextsEqual(argv[i], "off") &&
                !port_TextsEqual(argv[i], "fixed")) {
                port_UsageError(
                    "--room-rule takes fixed, winner or off, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--room-size")) {
            i++;
            room_auto.size = port_TextsEqual(argv[i], "4")   ? 4
                             : port_TextsEqual(argv[i], "2") ? 2
                                                             : 0;
            if (room_auto.size == 0) {
                port_UsageError("--room-size takes 2 or 4, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--players")) {
            /* 2; 4, on four PCs; 2x2, two on each of two PCs */
            i++;
            session_pads = port_TextsEqual(argv[i], "2") ? 2 : 4;
            session_pcs = port_TextsEqual(argv[i], "4") ? 4 : 2;
            if (!port_TextsEqual(argv[i], "2") &&
                !port_TextsEqual(argv[i], "4") &&
                !port_TextsEqual(argv[i], "2x2")) {
                port_UsageError("--players takes 2, 4 or 2x2, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--room-watcher")) {
            room_auto.plays = FALSE;
        } else if (port_TextsEqual(word, "--room-no-watch")) {
            room_auto.watch = FALSE;
        } else if (port_TextsEqual(word, "--room-matches")) {
            s32 count = 0;

            if (!port_ParseNumber(argv[++i], &count) || count < 0) {
                port_UsageError("--room-matches takes a number, not ", argv[i]);
            }
            room_auto.matches = (u32)count;
        } else if (port_TextsEqual(word, "--room-side")) {
            i++;
            room_auto.side = port_TextsEqual(argv[i], "open")   ? 1
                             : port_TextsEqual(argv[i], "join") ? 2
                                                                : 0;
            if (room_auto.side == 0) {
                port_UsageError(
                    "--room-side takes open or join, not ", argv[i]);
            }
        } else if (port_TextsEqual(word, "--room-tournament")) {
            PortTournamentSetup setup;

            port_KeepWord(
                room_auto.tournament, PORT_ONLINE_SPEC_BYTES, argv[++i]);
            if (!port_RoomTournamentParse(argv[i], &setup)) {
                port_UsageError("--room-tournament takes --tournament's "
                                "words, @Name a member, not ",
                                argv[i]);
            }
        } else if (port_TextsEqual(word, "--leave-at")) {
            s32 match = 0;
            s32 frame = 0;
            const char *at = argv[++i];
            u32 k;

            for (k = 0; at[k] != 0 && at[k] != ':'; k++) {
            }
            if (at[k] != ':' || !port_ParseNumber(at + k + 1, &frame) ||
                frame <= 0) {
                port_UsageError("--leave-at takes MATCH:FRAME, not ", at);
            }
            for (k = 0; at[k] >= '0' && at[k] <= '9'; k++) {
                match = match * DECIMAL + (at[k] - '0');
            }
            room_auto.leave_match = (u32)match;
            room_auto.leave_frame = (u32)frame;
        } else if (port_TextsEqual(word, "--ranked-matches")) {
            s32 count = 0;

            if (!port_ParseNumber(argv[++i], &count) || count < 0) {
                port_UsageError(
                    "--ranked-matches takes a number, not ", argv[i]);
            }
            room_auto.matches = (u32)count;
        } else if (port_TextsEqual(word, "--identity")) {
            port_RankedSetIdentity(argv[++i]);
        } else if (port_TextsEqual(word, "--net-plan-pads")) {
            port_SetNetplayOwnPadOnly(TRUE);
        } else if (port_TextsEqual(word, "--resync-auto") ||
                   port_TextsEqual(word, "--resync-ask-at") ||
                   port_TextsEqual(word, "--net-part-at")) {
            s32 count = 0;

            if (!port_ParseNumber(argv[++i], &count) || count < 0) {
                port_UsageError("--resync-auto, --resync-ask-at and "
                                "--net-part-at take a number of frames, not ",
                                argv[i]);
            }
            if (port_TextsEqual(word, "--resync-auto")) {
                port_SetNetplayResyncAuto((u32)count);
            } else if (port_TextsEqual(word, "--resync-ask-at")) {
                port_SetNetplayResyncAskAt((u32)count);
            } else {
                port_SetNetplayPartAt((u32)count);
            }
        } else if (port_TextsEqual(word, "--player")) {
            player = argv[++i];
        } else if (port_TextsEqual(word, "--room-password")) {
            password = argv[++i];
        } else if (port_TextsEqual(word, "--room-key")) {
            u8 salt[PORT_RELAY_SALT_BYTES];
            u8 key[PORT_RELAY_KEY_BYTES];

            key_text = argv[i + 1];
            if (!port_ParseRoomKey(argv[++i], salt, key)) {
                port_UsageError("--room-key takes SALT:KEY in hex (16 and 32 "
                                "bytes), not ",
                                argv[i]);
            }
            port_SetRelayKey(salt, key);
        } else if (port_TextsEqual(word, "--punch-to")) {
            punch_text = argv[i + 1];
            if (!port_ParseNetAddress(argv[++i], &punch_to) ||
                punch_to.port == 0) {
                port_UsageError("--punch-to takes ADDRESS:PORT, not ", argv[i]);
            }
            has_punch_to = 1;
        } else if (port_TextsEqual(word, "--mapped")) {
            if (!port_ParseNetAddress(argv[++i], &mapped) || mapped.port == 0) {
                port_UsageError("--mapped takes ADDRESS:PORT, not ", argv[i]);
            }
            port_SetRelayMapped(&mapped);
        } else if (port_TextsEqual(word, "--net-paths")) {
            paths = port_ParseNetPaths(argv[++i]);
            if (paths == 0) {
                port_UsageError("--net-paths takes some of public,lan,mapped,"
                                "v6,relay, not ",
                                argv[i]);
            }
        } else if (port_TextsEqual(word, "--session-differ")) {
            static const char * const parts[] = {
                "build", "disc", "mods", "protocol"};
            s32 part;

            i++;
            for (part = 0; part < 4; part++) {
                if (port_TextsEqual(argv[i], parts[part])) {
                    port_SetSessionDiffer(PORT_SESSION_DIFFER_BUILD + part);
                    break;
                }
            }
            if (part == 4) {
                port_UsageError("--session-differ takes build, disc, mods or "
                                "protocol, not ",
                                argv[i]);
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
    port_SetSessionDisc(&g_port_disc);
    port_SetReplayDisc(&g_port_disc);
    if (check_online) {
        port_PrintSessionCheck();
        return PORT_EXIT_OK;
    }
    if (plan != NULL) {
        port_LoadPlan(plan);
    }
    if (replay != NULL) {
        char why[PORT_REPLAY_WHY_BYTES];

        if (!port_PlayReplay(replay, replay_quiet, TRUE, why, sizeof(why))) {
            port_Log("replay: %s: %s\n", replay, why);
            return PORT_EXIT_FAILED;
        }
        if (frames == 0) {
            /* the replay's clock goes on from the fight's: it ends the
             * run itself */
            port_SetBlankLimit(PORT_NO_BLANK_LIMIT);
        }
    }
    if (frames > 0) {
        port_SetBlankLimit((u32)frames);
    }
    if (shots != NULL) {
        port_SetShots(shots, (u32)every);
    }
    if (room_auto.on) {
        /* a room of several: its matches are sessions begun in this
         * program (online.c), each from the state of the game's start */
        if (!has_lobby || session_role != PORT_SESSION_NONE || room != NULL ||
            session_after > 0) {
            port_UsageError("--room-open, --room-enter and --ranked need a "
                            "--lobby, and no --host, --join, --room or "
                            "--session-at",
                            "");
        }
        port_KeepWord(room_auto.lobby, sizeof(room_auto.lobby), lobby_text);
        port_KeepWord(room_auto.player, sizeof(room_auto.player), player);
        port_KeepWord(room_auto.password, sizeof(room_auto.password),
                      password != NULL ? password : "");
        port_KeepWord(room_auto.bind, sizeof(room_auto.bind), bind_text);
        room_auto.delay = (u32)net_delay;
        room_auto.paths = paths;
        port_SetNetTrouble((u32)net_latency, (u32)net_loss);
        port_OnlineWanted();
        port_OnlineRoomAuto(&room_auto);
        has_lobby = 0;
        net_addresses = 0;
    }
    if (room != NULL) {
        if (!has_lobby || session_role == PORT_SESSION_JOIN) {
            port_UsageError(
                "--room opens a room at a --lobby, and hosts it", "");
        }
        if (session_role == PORT_SESSION_NONE) {
            session_role = PORT_SESSION_HOST;
            session_text = "0.0.0.0:0";
        }
    }
    if (session_after > 0) {
        /* the session begins later, in this program (online.c): it is
         * set up then, from these words, as many times as asked */
        PortOnlineAsk ask;
        s32 is_room = room != NULL;

        if (session_role == PORT_SESSION_NONE) {
            port_UsageError("--session-at is a session's (--host, --join, "
                            "--room)",
                            "");
        }
        if (link_side >= 0 || netplay_pad != 0 || rollback_frames > 0) {
            port_UsageError("a session (--host, --join) is neither the link "
                            "cable, --netplay nor the rollback self-test",
                            "");
        }
        for (i = 0; i < (s32)sizeof(ask); i++) {
            ((u8 *)&ask)[i] = 0;
        }
        ask.role = session_role;
        ask.delay = (u32)net_delay;
        ask.paths = paths;
        ask.players = session_pads;
        ask.pcs = session_pcs;
        port_KeepWord(ask.at, sizeof(ask.at), session_text);
        port_KeepWord(ask.bind, sizeof(ask.bind), bind_text);
        port_KeepWord(ask.lobby, sizeof(ask.lobby), lobby_text);
        port_KeepWord(ask.room, sizeof(ask.room), is_room ? room : "");
        port_KeepWord(ask.player, sizeof(ask.player), player);
        port_KeepWord(ask.password, sizeof(ask.password),
                      password != NULL ? password : "");
        port_KeepWord(ask.key, sizeof(ask.key), key_text);
        port_KeepWord(ask.punch_to, sizeof(ask.punch_to), punch_text);
        port_SetNetTrouble((u32)net_latency, (u32)net_loss);
        port_OnlineWanted();
        port_OnlineAt(&ask, (u32)session_after, (u32)sessions);
        session_role = PORT_SESSION_NONE;
        net_addresses = 0;
        has_lobby = 0;
        has_punch_to = 0;
    } else if (session_role != PORT_SESSION_NONE) {
        if (has_lobby && session_role == PORT_SESSION_JOIN) {
            /* the room's code; the server is the peer until the way to
             * the host is found */
            port_SetRelay(&lobby, session_text, paths);
            session_at = lobby;
        } else if (!port_ParseNetAddress(session_text, &session_at) ||
                   (session_at.port == 0 && !has_lobby)) {
            port_UsageError("--host and --join take ADDRESS:PORT (--join "
                            "a room's code with --lobby), not ",
                            session_text);
        } else if (has_lobby) {
            port_SetRelay(&lobby, "", paths);
        }
        if (has_lobby) {
            port_SetRelayRoom(room != NULL ? room : "", player);
            if (password != NULL) {
                port_SetRelayPassword(password);
            }
        }
        if (has_punch_to) {
            port_SetSessionPunchTo(&punch_to);
        }
    } else if (has_lobby || has_punch_to) {
        port_UsageError("--lobby and --punch-to are a session's (--host, "
                        "--join, --room)",
                        "");
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
            port_ClearNetAddress(&net_bind);
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
        port_SetSessionPlayer(player);
        port_SetSessionPlayers(session_pads, session_pcs);
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
     * more of the stage than the console does (port_remaster.h), or a
     * fight of two on two draws four fighters (port_duo.h) */
    port_AddLinkRegion(PORT_PRIMS_ADDR, PORT_PRIMS_SIZE);
#endif
    /* the game, a step at a time (step.c): the rollback self-test has
     * its turn between two */
    port_StartGame(port_RunGame);
    if (port_IsNetplayOn()) {
        port_RunNetplay();
    }
    /* the state of the game's start, before its first step, where a
     * session may begin in this program (online.c) */
    port_OnlineKeepBoot(&g_port_disc);
    /* ... and where a replay of another build's is played from */
    port_ReplayKeepBoot();
    for (;;) {
        /* a replay's turn: a fight's start seen, or a playback's frame */
        port_ReplayBoundary();
        port_BeforeStep();
        port_StepGame();
        port_AfterStep();
        /* training's moment kept or put back (port_training.h): at the
         * boundary of the step that asked, before anything else of the
         * host's there (the rollback self-test's save or load) */
        port_TrainingBoundary();
        /* a local tournament's asks put in, its record written
         * (port_tournament.h): between two steps, never in one */
        port_TournamentBoundary();
        port_OnRollbackFrame();
        port_OnlineBoundary();
    }
}
