/* The port's record of fights (src/port/stats_store.c; port_stats.h): the
 * line of a fight written and read back, and of a local tournament (its
 * entrants and matches, read back the same, kept beside the fights), the file
 * written whole through a temporary file and a rename, read again at the start,
 * a file of a later version or with a broken line kept aside (and its good
 * fights kept), and the counting the Stats page shows. The host's files are the
 * test's own, in memory; nothing on the disk is read or written. */
#include "common.h"
#include "port_host.h"
#include "port_platform.h"
#include "port_stats.h"
#include "src/port/text.c"
#include "src/port/stats_store.c"
#include "runtime/runtime.h"

/* the CPU's Master (port_cpu.h) and a Stock match (port_match.h) */
#define PORT_CPU_LEVELS_TEST 4
#define PORT_MATCH_STOCK_TEST 1

s32 g_port_ahead;
s32 g_port_rerun;

void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    (void)stream;
    (void)bytes;
    (void)count;
}

void port_Quit(s32 code) {
    test_Print("port_Quit(%d)\n", code);
    CHECK(0);
}

/* ---- the host's files, in memory ---- */

#define TEST_FILES 8
#define TEST_PATH_BYTES 96
#define TEST_FILE_BYTES 0x10000
#define TEST_PATH "rebushido-stats.txt"
#define TEST_PATH_PART "rebushido-stats.txt.part"
#define TEST_PATH_ASIDE "rebushido-stats.txt.broken-20261127-140322"

typedef struct {
    char path[TEST_PATH_BYTES];
    s32 is_there;
    u32 size;
    u8 bytes[TEST_FILE_BYTES];
} TestFile;

static TestFile g_test_files[TEST_FILES];
static s32 g_test_create_fails;
static s32 g_test_rename_fails;

static void test_CopyText(char *to, const char *from, u32 room) {
    u32 i;

    for (i = 0; from[i] != 0 && i + 1 < room; i++) {
        to[i] = from[i];
    }
    to[i] = 0;
}

static TestFile *test_Find(const char *path) {
    s32 i;

    for (i = 0; i < TEST_FILES; i++) {
        if (g_test_files[i].is_there &&
            test_StringsEqual(g_test_files[i].path, path)) {
            return &g_test_files[i];
        }
    }
    return NULL;
}

static TestFile *test_Make(const char *path) {
    TestFile *file = test_Find(path);
    s32 i;

    for (i = 0; file == NULL && i < TEST_FILES; i++) {
        if (!g_test_files[i].is_there) {
            file = &g_test_files[i];
        }
    }
    CHECK(file != NULL);
    test_CopyText(file->path, path, sizeof(file->path));
    file->is_there = TRUE;
    file->size = 0;
    return file;
}

static s32 test_Number(const TestFile *file) {
    return file == NULL ? -1 : (s32)(file - g_test_files);
}

s32 port_HostOpen(const char *path) { return test_Number(test_Find(path)); }

s32 port_HostCreate(const char *path) {
    if (g_test_create_fails) {
        return -1;
    }
    return test_Number(test_Make(path));
}

void port_HostClose(s32 file) { (void)file; }

u32 port_HostFileSize(s32 file) { return g_test_files[file].size; }

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    TestFile *of = &g_test_files[file];

    if (offset >= of->size) {
        return 0;
    }
    if (count > of->size - offset) {
        count = of->size - offset;
    }
    test_Copy(dest, of->bytes + offset, count);
    return count;
}

s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    TestFile *of = &g_test_files[file];

    if (of->size + count > TEST_FILE_BYTES) {
        return FALSE;
    }
    test_Copy(of->bytes + of->size, bytes, count);
    of->size += count;
    return TRUE;
}

s32 port_HostRemove(const char *path) {
    TestFile *file = test_Find(path);

    if (file == NULL) {
        return FALSE;
    }
    file->is_there = FALSE;
    return TRUE;
}

s32 port_HostRename(const char *from, const char *to) {
    TestFile *file = test_Find(from);
    TestFile *there = test_Find(to);

    if (file == NULL || g_test_rename_fails) {
        return FALSE;
    }
    if (there != NULL && there != file) {
        there->is_there = FALSE;
    }
    test_CopyText(file->path, to, sizeof(file->path));
    return TRUE;
}

void port_HostDate(PortDate *date) {
    date->year = 2026;
    date->month = 11;
    date->day = 27;
    date->hour = 14;
    date->minute = 3;
    date->second = 22;
    date->is_utc = 0;
}

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

static void test_Clear(void) {
    s32 i;

    for (i = 0; i < TEST_FILES; i++) {
        g_test_files[i].is_there = FALSE;
    }
    g_test_create_fails = FALSE;
    g_test_rename_fails = FALSE;
}

static void test_Put(const char *path, const char *text) {
    TestFile *file = test_Make(path);

    file->size = port_TextLength(text);
    test_Copy(file->bytes, text, file->size);
}

/* Whether the file's text is `text`. */
static s32 test_Holds(const char *path, const char *text) {
    const TestFile *file = test_Find(path);
    u32 length = port_TextLength(text);

    return file != NULL && file->size == length &&
           test_BytesEqual(file->bytes, text, length);
}

/* The file's lines. */
static s32 test_Lines(const char *path) {
    const TestFile *file = test_Find(path);
    s32 lines = 0;
    u32 i;

    if (file == NULL) {
        return -1;
    }
    for (i = 0; i < file->size; i++) {
        lines += file->bytes[i] == '\n';
    }
    return lines;
}

/* ---- fights ---- */

static PortStatsFight test_Fight(s32 result, s32 fighter, s32 weapon) {
    PortStatsFight fight;
    u32 i;

    for (i = 0; i < sizeof(fight); i++) {
        ((u8 *)&fight)[i] = 0;
    }
    fight.year = 2026;
    fight.month = 11;
    fight.day = 27;
    fight.hour = 9;
    fight.minute = 5;
    fight.second = 7;
    fight.mode = 4; /* versus */
    fight.teams = 1;
    fight.cpu = PORT_STATS_PERSON;
    fight.match_fight = 1;
    fight.match_result = (u8)result;
    fight.stage = 29;
    fight.fighter[0] = (s16)fighter;
    fight.weapon[0] = (s16)weapon;
    fight.fighter[1] = 18;
    fight.weapon[1] = 2;
    fight.result = (u8)result;
    fight.how = PORT_STATS_HOW_KILL;
    fight.frames = 600;
    return fight;
}

static s32 test_SameFight(const PortStatsFight *a, const PortStatsFight *b) {
    return test_BytesEqual(a, b, sizeof(*a));
}

/* Every field written and read back the same; names with what a line
 * cannot hold as it is. */
static void test_Line(void) {
    PortStatsFight fight = test_Fight(PORT_STATS_WON, 3, 1);
    PortStatsFight back;
    char line[1024];
    u32 length;

    fight.online = PORT_STATS_RANKED;
    fight.teams = 2;
    fight.cpu = PORT_CPU_LEVELS_TEST;
    fight.side = 1;
    fight.rule = PORT_MATCH_STOCK_TEST;
    fight.count = 3;
    fight.match = 41;
    fight.match_fight = 2;
    fight.match_result = PORT_STATS_GOING;
    fight.how = PORT_STATS_HOW_SURRENDER;
    fight.kills_given = 2;
    fight.kills_taken = 1;
    fight.given.blow = PORT_STATS_BLOW_LEAPING;
    fight.given.place = PORT_STATS_PLACE_CHEST;
    fight.given.behind = 1;
    fight.taken.blow = PORT_STATS_BLOW_THROWN;
    fight.taken.place = PORT_STATS_PLACE_LEGS;
    fight.taken.down = 1;
    fight.wounds_given[PORT_STATS_WOUND_LEGS] = 2;
    fight.wounds_given[PORT_STATS_WOUND_OFF_ARM] = 1;
    fight.wounds_taken[PORT_STATS_WOUND_BODY] = 3;
    fight.frames = 123456;
    test_CopyText(fight.opponent, "Mr 100% = x", sizeof(fight.opponent));
    test_CopyText(
        fight.replay, "2026-11-27_090507_vs.rbr", sizeof(fight.replay));
    test_CopyText(
        fight.tournament, "2026-11-27T09:01:00", sizeof(fight.tournament));
    length = port_StatsFormat(&fight, line, sizeof(line));
    CHECK(length > 0 && line[length - 1] == '\n');
    CHECK(port_StatsParse(line, length - 1, &back));
    CHECK(test_SameFight(&fight, &back));
    CHECK(test_StringsEqual(back.opponent, "Mr 100% = x"));
    /* no room: nothing */
    CHECK_EQ(port_StatsFormat(&fight, line, 40), 0);
    /* a later version's key is passed over; a missing one is built in */
    CHECK(port_StatsParse("fight result=lost how=time mood=calm", 36, &back));
    CHECK_EQ(back.result, PORT_STATS_LOST);
    CHECK_EQ(back.how, PORT_STATS_HOW_TIME);
    CHECK_EQ(back.teams, 1);
    CHECK_EQ(back.cpu, PORT_STATS_PERSON);
    CHECK_EQ(back.match_result, PORT_STATS_GOING);
    /* what is not a fight */
    CHECK(!port_StatsParse("fighter result=won", 18, &back));
    CHECK(!port_StatsParse("fight result=wonn", 17, &back));
    CHECK(!port_StatsParse("fight frames=12x", 16, &back));
    CHECK(!port_StatsParse("fight frames", 12, &back));
    CHECK(!port_StatsParse("fight given=slash:head:0:0", 26, &back));
}

/* Written whole after each fight, read again at the start. */
static void test_File(void) {
    PortStatsFight first = test_Fight(PORT_STATS_WON, 3, 1);
    PortStatsFight second = test_Fight(PORT_STATS_LOST, 3, 1);
    PortStatsFight third = test_Fight(PORT_STATS_WON, 5, 4);
    PortStatsFight kept[3];
    s32 i;

    test_Clear();
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 0);
    CHECK(test_Find(TEST_PATH) == NULL);
    /* the match's numbers: a match's later fights the first's */
    second.match_fight = 2;
    second.match_result = PORT_STATS_LOST;
    first.match_result = PORT_STATS_GOING;
    CHECK(port_StatsAdd(&first));
    CHECK(port_StatsAdd(&second));
    CHECK(port_StatsAdd(&third));
    CHECK_EQ(first.match, 1);
    CHECK_EQ(second.match, 1);
    CHECK_EQ(third.match, 2);
    CHECK(test_Find(TEST_PATH_PART) == NULL);
    CHECK_EQ(test_Lines(TEST_PATH), 4);
    CHECK(test_BytesEqual(
        test_Find(TEST_PATH)->bytes, "rebushido-stats 2\n", 18));
    kept[0] = first;
    kept[1] = second;
    kept[2] = third;
    /* read back as it was */
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 3);
    for (i = 0; i < 3; i++) {
        CHECK(test_SameFight(port_StatsFightAt(i), &kept[i]));
    }
    /* a replay's name given later */
    port_StatsSetReplay(1, "2026-11-27_090600_vs.rbr");
    port_SetStatsFile(TEST_PATH);
    CHECK(test_StringsEqual(
        port_StatsFightAt(1)->replay, "2026-11-27_090600_vs.rbr"));
    /* a write refused: kept in memory, the file as it was */
    g_test_create_fails = TRUE;
    CHECK(!port_StatsAdd(&third));
    CHECK_EQ(port_StatsCount(), 4);
    CHECK_EQ(test_Lines(TEST_PATH), 4);
    /* a rename refused: no part left, the file as it was */
    g_test_create_fails = FALSE;
    g_test_rename_fails = TRUE;
    CHECK(!port_StatsAdd(&third));
    CHECK(test_Find(TEST_PATH_PART) == NULL);
    CHECK_EQ(test_Lines(TEST_PATH), 4);
    g_test_rename_fails = FALSE;
    CHECK(port_StatsAdd(&third));
    CHECK_EQ(test_Lines(TEST_PATH), 7);
    /* no file: nothing kept */
    port_SetStatsFile("");
    CHECK_EQ(port_StatsCount(), 0);
    CHECK(!port_StatsAdd(&third));
    CHECK(test_StringsEqual(port_GetStatsFile(), ""));
}

/* A file that cannot be read is kept aside, never written over. */
static void test_Broken(void) {
    static const char later[] = "rebushido-stats 3\nfight result=won\n";
    static const char other[] = "this is not a record\n";
    static const char torn[] =
        "rebushido-stats 1\nfight result=won how=kill\nfight resu\n"
        "fight result=lost how=kill\n";

    /* of a later version */
    test_Clear();
    test_Put(TEST_PATH, later);
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 0);
    CHECK(test_Find(TEST_PATH) == NULL);
    CHECK(test_Holds(TEST_PATH_ASIDE, later));
    /* a record begun again beside it */
    {
        PortStatsFight fight = test_Fight(PORT_STATS_WON, 1, 1);

        CHECK(port_StatsAdd(&fight));
        CHECK_EQ(test_Lines(TEST_PATH), 2);
        CHECK(test_Holds(TEST_PATH_ASIDE, later));
    }
    /* not a record at all */
    test_Clear();
    test_Put(TEST_PATH, other);
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 0);
    CHECK(test_Holds(TEST_PATH_ASIDE, other));
    /* a line broken: the file aside, its good fights kept and written */
    test_Clear();
    test_Put(TEST_PATH, torn);
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 2);
    CHECK(test_Holds(TEST_PATH_ASIDE, torn));
    CHECK_EQ(test_Lines(TEST_PATH), 3);
    CHECK_EQ(port_StatsFightAt(1)->result, PORT_STATS_LOST);
    /* ... and one that cannot be put aside is not written over, and no
     * record is kept */
    test_Clear();
    test_Put(TEST_PATH, other);
    g_test_rename_fails = TRUE;
    port_SetStatsFile(TEST_PATH);
    CHECK(test_StringsEqual(port_GetStatsFile(), ""));
    CHECK(test_Holds(TEST_PATH, other));
    g_test_rename_fails = FALSE;
    /* an empty file is a record of nothing, not a broken one */
    test_Clear();
    test_Put(TEST_PATH, "");
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 0);
    CHECK(test_Find(TEST_PATH_ASIDE) == NULL);
    /* lines of CR LF, as an editor on Windows writes them */
    test_Clear();
    test_Put(TEST_PATH, "rebushido-stats 1\r\nfight result=draw\r\n");
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 1);
    CHECK_EQ(port_StatsFightAt(0)->result, PORT_STATS_DRAW);
    CHECK(test_Find(TEST_PATH_ASIDE) == NULL);
}

/* ---- tournaments ---- */

/* A tournament of three: a person, two CPUs; two matches. */
static PortStatsTournament test_Tournament(void) {
    PortStatsTournament t;
    u32 i;

    for (i = 0; i < sizeof(t); i++) {
        ((u8 *)&t)[i] = 0;
    }
    t.year = 2026;
    t.month = 11;
    t.day = 27;
    t.hour = 10;
    t.minute = 2;
    t.second = 3;
    test_CopyText(t.begun, "2026-11-27T09:01:00", sizeof(t.begun));
    t.count = 3;
    t.is_double = 1;
    t.seeding = 1;
    t.rule = PORT_MATCH_STOCK_TEST;
    t.rule_count = 2;
    t.champion = 2;
    t.played = 2;
    test_CopyText(
        t.entrant[0].name, "Trial 1: 100% = x", sizeof(t.entrant[0].name));
    t.entrant[0].cpu = PORT_STATS_PERSON;
    t.entrant[0].pad = 1;
    t.entrant[0].place = 2;
    t.entrant[0].losses = 2;
    t.entrant[0].seed = 1;
    test_CopyText(t.entrant[1].name, "CPU 2", sizeof(t.entrant[1].name));
    t.entrant[1].cpu = 0;
    t.entrant[1].pad = 0xFF;
    t.entrant[1].place = 3;
    t.entrant[1].losses = 2;
    t.entrant[1].seed = 3;
    test_CopyText(t.entrant[2].name, "Master", sizeof(t.entrant[2].name));
    t.entrant[2].cpu = PORT_CPU_LEVELS_TEST;
    t.entrant[2].pad = 0xFF;
    t.entrant[2].place = 1;
    t.entrant[2].losses = 0;
    t.entrant[2].seed = 2;
    t.match[0].part = 0;
    t.match[0].round = 1;
    t.match[0].first = 2;
    t.match[0].second = 1;
    t.match[0].winner = 2;
    t.match[1].part = 3; /* the grand final's reset */
    t.match[1].round = 1;
    t.match[1].first = 0;
    t.match[1].second = 2;
    t.match[1].winner = 2;
    return t;
}

/* A tournament's line written and read back the same; kept in the file
 * after the fights, read again; a broken one keeps the file aside. */
static void test_Tournaments(void) {
    PortStatsTournament t = test_Tournament();
    PortStatsTournament back;
    PortStatsFight fight = test_Fight(PORT_STATS_LOST, 3, 1);
    char line[2048];
    u32 length;

    length = port_StatsFormatTournament(&t, line, sizeof(line));
    CHECK(length > 0 && line[length - 1] == '\n');
    CHECK(port_StatsParseTournament(line, length - 1, &back));
    CHECK(test_BytesEqual(&t, &back, sizeof(t)));
    CHECK(test_StringsEqual(back.entrant[0].name, "Trial 1: 100% = x"));
    /* no room: nothing */
    CHECK_EQ(port_StatsFormatTournament(&t, line, 60), 0);
    /* what is not a tournament */
    CHECK(!port_StatsParseTournament("fight result=won", 16, &back));
    CHECK(!port_StatsParseTournament("tournament entrants=1", 21, &back));
    CHECK(!port_StatsParseTournament(
        "tournament entrants=2 e1=pad9:1:0:1:x", 37, &back));
    CHECK(!port_StatsParseTournament(
        "tournament entrants=2 m1=final:1:1:2", 36, &back));
    /* a later version's key passed over */
    CHECK(port_StatsParseTournament(
        "tournament entrants=2 mood=calm", 31, &back));
    /* in the file, after the fights, and read again */
    test_Clear();
    port_SetStatsFile(TEST_PATH);
    test_CopyText(
        fight.tournament, "2026-11-27T09:01:00", sizeof(fight.tournament));
    CHECK(port_StatsAdd(&fight));
    CHECK(port_StatsAddTournament(&t));
    CHECK(port_StatsAdd(&fight));
    CHECK_EQ(test_Lines(TEST_PATH), 4);
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 2);
    CHECK_EQ(port_StatsTournamentCount(), 1);
    CHECK(port_StatsTournamentAt(1) == NULL);
    CHECK(test_BytesEqual(port_StatsTournamentAt(0), &t, sizeof(t)));
    CHECK(test_StringsEqual(
        port_StatsFightAt(0)->tournament, "2026-11-27T09:01:00"));
    CHECK(test_Find(TEST_PATH_ASIDE) == NULL);
    /* a version 1 record has none, and is read as it is */
    test_Clear();
    test_Put(TEST_PATH, "rebushido-stats 1\nfight result=won\n");
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 1);
    CHECK_EQ(port_StatsTournamentCount(), 0);
    CHECK(test_Find(TEST_PATH_ASIDE) == NULL);
    /* a torn tournament's line: the file aside, the rest kept */
    test_Clear();
    test_Put(TEST_PATH, "rebushido-stats 2\nfight result=won\n"
                        "tournament entrants=2 e1=pad1:1:0\n");
    port_SetStatsFile(TEST_PATH);
    CHECK_EQ(port_StatsCount(), 1);
    CHECK_EQ(port_StatsTournamentCount(), 0);
    CHECK(test_Find(TEST_PATH_ASIDE) != NULL);
}

/* The counting the page shows. */
static void test_Tally(void) {
    PortStatsTally tally;
    PortStatsFight f;
    s32 i;

    test_Clear();
    port_SetStatsFile(TEST_PATH);
    /* won, won, won, lost, won, won, draw: the longest run 3, now 0 */
    for (i = 0; i < 7; i++) {
        s32 result = i == 3   ? PORT_STATS_LOST
                     : i == 6 ? PORT_STATS_DRAW
                              : PORT_STATS_WON;

        f = test_Fight(result, i < 4 ? 3 : 5, i < 2 ? 1 : 4);
        f.stage = (s16)(i < 5 ? 29 : 7);
        f.frames = 300;
        if (result == PORT_STATS_WON) {
            f.kills_given = 1;
            f.given.blow =
                (u8)(i == 0 ? PORT_STATS_BLOW_THROWN : PORT_STATS_BLOW_STRIKE);
            f.given.place =
                (u8)(i < 2 ? PORT_STATS_PLACE_HEAD : PORT_STATS_PLACE_CHEST);
            f.given.behind = (u8)(i == 4);
        } else if (result == PORT_STATS_LOST) {
            f.kills_taken = 1;
            f.taken.blow = PORT_STATS_BLOW_RUNNING;
            f.taken.place = PORT_STATS_PLACE_HIP;
        }
        f.wounds_given[PORT_STATS_WOUND_LEGS] = 1;
        f.wounds_taken[PORT_STATS_WOUND_ARM] = (u8)(i == 3 ? 2 : 0);
        /* the opponents: the CPU's Master twice, the game's CPU, a person
         * here, and an online name twice (once ranked) */
        f.cpu = (s8)(i < 2    ? PORT_CPU_LEVELS_TEST
                     : i == 2 ? 0
                              : PORT_STATS_PERSON);
        f.mode = (u8)(i < 4 ? 1 : 4);
        if (i >= 5) {
            f.online = (u8)(i == 5 ? PORT_STATS_ONLINE : PORT_STATS_RANKED);
            test_CopyText(f.opponent, "Hayate", sizeof(f.opponent));
        }
        f.teams = (u8)(i == 4 ? 2 : 1);
        /* a match of two: the third fight going, the fourth losing it */
        if (i == 2) {
            f.rule = PORT_MATCH_STOCK_TEST;
            f.match_result = PORT_STATS_GOING;
        } else if (i == 3) {
            f.rule = PORT_MATCH_STOCK_TEST;
            f.match_fight = 2;
        }
        CHECK(port_StatsAdd(&f));
    }
    CHECK_EQ(port_StatsFightAt(3)->match, port_StatsFightAt(2)->match);
    port_StatsTally(&tally);
    CHECK_EQ(tally.fights.fights, 7);
    CHECK_EQ(tally.fights.won, 5);
    CHECK_EQ(tally.fights.lost, 1);
    CHECK_EQ(tally.fights.drawn, 1);
    CHECK_EQ(tally.matches.fights, 6);
    CHECK_EQ(tally.matches.lost, 1);
    CHECK_EQ(tally.longest_run, 3);
    CHECK_EQ(tally.run_now, 0);
    CHECK_EQ(tally.frames, 2100);
    CHECK_EQ(tally.by_fighter[3].fights, 4);
    CHECK_EQ(tally.by_fighter[3].lost, 1);
    CHECK_EQ(tally.by_fighter[5].won, 2);
    CHECK_EQ(tally.by_weapon[1].fights, 2);
    CHECK_EQ(tally.by_weapon[4].fights, 5);
    CHECK_EQ(tally.by_stage[29].fights, 5);
    CHECK_EQ(tally.by_stage[7].fights, 2);
    CHECK_EQ(tally.by_against[18].fights, 7);
    CHECK_EQ(tally.by_mode[PORT_STATS_BY_VSCPU].fights, 4);
    CHECK_EQ(tally.by_mode[PORT_STATS_BY_VERSUS].fights, 1);
    CHECK_EQ(tally.by_mode[PORT_STATS_BY_ONLINE].fights, 1);
    CHECK_EQ(tally.by_mode[PORT_STATS_BY_RANKED].drawn, 1);
    CHECK_EQ(tally.by_mode[PORT_STATS_BY_TWO_ON_TWO].fights, 1);
    CHECK_EQ(tally.by_how[PORT_STATS_HOW_KILL].fights, 7);
    CHECK_EQ(tally.opponent_count, 4);
    CHECK_EQ(tally.opponents[0].cpu, PORT_CPU_LEVELS_TEST);
    CHECK_EQ(tally.opponents[0].score.won, 2);
    CHECK_EQ(tally.opponents[1].cpu, 0);
    CHECK_EQ(tally.opponents[2].cpu, PORT_STATS_PERSON);
    CHECK_EQ(tally.opponents[2].score.fights, 2);
    CHECK(test_StringsEqual(tally.opponents[3].name, "Hayate"));
    CHECK_EQ(tally.opponents[3].score.fights, 2);
    CHECK_EQ(tally.kills_given, 5);
    CHECK_EQ(tally.kills_taken, 1);
    CHECK_EQ(tally.given_by_blow[PORT_STATS_BLOW_THROWN], 1);
    CHECK_EQ(tally.given_by_blow[PORT_STATS_BLOW_STRIKE], 4);
    CHECK_EQ(tally.given_by_place[PORT_STATS_PLACE_HEAD], 2);
    CHECK_EQ(tally.given_by_place[PORT_STATS_PLACE_CHEST], 3);
    CHECK_EQ(tally.given_behind, 1);
    CHECK_EQ(tally.taken_by_blow[PORT_STATS_BLOW_RUNNING], 1);
    CHECK_EQ(tally.taken_by_place[PORT_STATS_PLACE_HIP], 1);
    CHECK_EQ(tally.wounds_given[PORT_STATS_WOUND_LEGS], 7);
    CHECK_EQ(tally.wounds_taken[PORT_STATS_WOUND_ARM], 2);
    /* the run now: two more won */
    f = test_Fight(PORT_STATS_WON, 3, 1);
    CHECK(port_StatsAdd(&f));
    CHECK(port_StatsAdd(&f));
    port_StatsTally(&tally);
    CHECK_EQ(tally.run_now, 2);
    CHECK_EQ(tally.longest_run, 3);
    /* the counting is the file's: read again, the same */
    port_SetStatsFile(TEST_PATH);
    {
        PortStatsTally again;

        port_StatsTally(&again);
        CHECK(test_BytesEqual(&tally, &again, sizeof(tally)));
    }
    /* the words */
    CHECK(test_StringsEqual(
        port_StatsHowWord(PORT_STATS_HOW_SURRENDER), "surrender"));
    CHECK(test_StringsEqual(port_StatsBlowWord(99), "none"));
    CHECK(test_StringsEqual(port_StatsPlaceWord(PORT_STATS_PLACE_HIP), "hip"));
    CHECK(
        test_StringsEqual(port_StatsModeWord(PORT_STATS_BY_RANKED), "Ranked"));
}

void test_Main(void) {
    test_Line();
    test_File();
    test_Broken();
    test_Tournaments();
    test_Tally();
}
