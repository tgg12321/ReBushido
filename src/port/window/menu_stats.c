/* The settings menu's Stats page (menu.h; docs/PLAYING.md, "Stats"): the
 * player's record of fights (port_stats.h) counted up. Its first view is
 * the totals -- fights and matches won and lost, the longest run of wins,
 * kills and wounds given and taken, the time fought, the ladder's rating
 * where the program has it -- and a row for each list: by fighter, by
 * weapon, by stage, by mode, by opponent, the kills by their kind and
 * place, and the history of fights, the newest first, whose rows play the
 * fight's replay where one was kept. Only the build with a window has
 * this file; the record is the core's (src/port/stats_store.c). */
#include <stdio.h>
#include <string.h>

#include "menu.h"
#include "port_cpu.h"
#include "port_ranked.h"
#include "port_replay.h"
#include "port_stats.h"
#include "sdl_window.h"

#define LINES_MAX 192
#define TEXT_BYTES 128
#define FRAMES_PER_SECOND 30
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR 60
#define PERCENT 100
#define PATH_BYTES 1024

typedef struct {
    char label[TEXT_BYTES];
    char value[TEXT_BYTES];
} StatsLine;

static StatsLine g_lines[LINES_MAX];
static s32 g_line_count;
static PortStatsTally g_tally;
static char g_help[TEXT_BYTES * 2];

static const char * const g_view_names[PORT_STATS_VIEWS] = {
    "Stats",   "By fighter",  "By weapon", "By stage",
    "By mode", "By opponent", "Kills",     "Match history"};

static const char * const g_cpu_shown[PORT_CPU_LEVELS + 1] = {
    "the game's own", "Novice", "Adept", "Veteran", "Master"};

static const char * const g_blow_shown[PORT_STATS_BLOWS] = {
    "nothing",
    "a strike",
    "a running strike",
    "a leaping strike",
    "a strike from a climb",
    "the second sword",
    "a thrown weapon",
    "a shot",
    "a paired move",
    "a fall"};

static const char * const g_place_shown[PORT_STATS_PLACES] = {
    "", "head", "chest", "hips", "sword arm", "off arm", "legs"};

static const char * const g_rule_shown[] = {
    "", "Stock", "First to", "Crew", "Group Battle of"};

const char *port_StatsViewName(s32 view) {
    return view >= 0 && view < PORT_STATS_VIEWS ? g_view_names[view] : "";
}

static StatsLine *port_StatsLine(void) {
    StatsLine *line;

    if (g_line_count >= LINES_MAX) {
        return NULL;
    }
    line = &g_lines[g_line_count++];
    line->label[0] = 0;
    line->value[0] = 0;
    return line;
}

static void port_StatsScoreText(
    const PortStatsScore *score, char *out, u32 room) {
    if (score->drawn != 0) {
        snprintf(out, room, "%u: %u won, %u lost, %u drawn (%u%%)",
                 score->fights, score->won, score->lost, score->drawn,
                 score->won * PERCENT / score->fights);
    } else {
        snprintf(out, room, "%u: %u won, %u lost (%u%%)", score->fights,
                 score->won, score->lost,
                 score->fights ? score->won * PERCENT / score->fights : 0);
    }
}

static void port_StatsAddScore(const char *label, const PortStatsScore *score) {
    StatsLine *line;

    if (score->fights == 0 || (line = port_StatsLine()) == NULL) {
        return;
    }
    snprintf(line->label, sizeof(line->label), "%s", label);
    port_StatsScoreText(score, line->value, sizeof(line->value));
}

static void port_StatsAddText(const char *label, const char *value) {
    StatsLine *line = port_StatsLine();

    if (line != NULL) {
        snprintf(line->label, sizeof(line->label), "%s", label);
        snprintf(line->value, sizeof(line->value), "%s", value);
    }
}

/* The scores of `count` keys, the most fought first, each named. */
static void port_StatsAddKeys(
    const PortStatsScore *scores, s32 count, const char *before,
    const char *(*name_of)(s32 key)) {
    u8 done[PORT_STATS_KEYS];
    s32 key;

    memset(done, 0, sizeof(done));
    for (;;) {
        s32 best = -1;
        char label[TEXT_BYTES];

        for (key = 0; key < count; key++) {
            if (!done[key] && scores[key].fights != 0 &&
                (best < 0 || scores[key].fights > scores[best].fights)) {
                best = key;
            }
        }
        if (best < 0) {
            return;
        }
        done[best] = 1;
        snprintf(label, sizeof(label), "%s%s", before, name_of(best));
        if (label[0] >= 'a' && label[0] <= 'z') {
            label[0] = (char)(label[0] - 'a' + 'A');
        }
        port_StatsAddScore(label, &scores[best]);
    }
}

static void port_StatsWoundsText(const u32 *wounds, char *out, u32 room) {
    snprintf(out, room, "legs %u, sword arm %u, body %u, off arm %u",
             wounds[PORT_STATS_WOUND_LEGS], wounds[PORT_STATS_WOUND_ARM],
             wounds[PORT_STATS_WOUND_BODY], wounds[PORT_STATS_WOUND_OFF_ARM]);
}

static void port_StatsOpponentName(
    const PortStatsOpponent *of, char *out, u32 room) {
    if (of->online) {
        snprintf(
            out, room, "%s (online)", of->name[0] != 0 ? of->name : "someone");
    } else if (of->cpu == PORT_STATS_PERSON) {
        snprintf(out, room, "A player beside you");
    } else {
        snprintf(out, room, "The CPU: %s",
                 of->cpu >= 0 && of->cpu <= PORT_CPU_LEVELS
                     ? g_cpu_shown[(s32)of->cpu]
                     : "?");
    }
}

static void port_StatsTotals(void) {
    char value[TEXT_BYTES];
    PortRankedView ranked;
    u32 seconds = g_tally.frames / FRAMES_PER_SECOND;
    s32 view;

    if (g_tally.fights.fights == 0) {
        port_StatsAddText("No fights yet", "");
    } else {
        port_StatsAddScore("Fights", &g_tally.fights);
        port_StatsAddScore("Matches", &g_tally.matches);
        snprintf(value, sizeof(value), "%u (now %u)", g_tally.longest_run,
                 g_tally.run_now);
        port_StatsAddText("Longest run of wins", value);
        snprintf(value, sizeof(value), "%u given, %u taken",
                 g_tally.kills_given, g_tally.kills_taken);
        port_StatsAddText("Kills", value);
        port_StatsWoundsText(g_tally.wounds_given, value, sizeof(value));
        port_StatsAddText("Wounds given", value);
        port_StatsWoundsText(g_tally.wounds_taken, value, sizeof(value));
        port_StatsAddText("Wounds taken", value);
        snprintf(value, sizeof(value), "%u:%02u:%02u",
                 seconds / (SECONDS_PER_MINUTE * MINUTES_PER_HOUR),
                 seconds / SECONDS_PER_MINUTE % MINUTES_PER_HOUR,
                 seconds % SECONDS_PER_MINUTE);
        port_StatsAddText("Time fought", value);
    }
    /* the ladder keeps its own ratings: only shown where the program has
     * the player's (Ranked, NETPLAY.md section 17) */
    port_RankedGet(&ranked);
    if (ranked.registered && ranked.rank != 0) {
        snprintf(value, sizeof(value), "%d (%s%d), %u of %u", ranked.rating,
                 "+/- ", ranked.rd, ranked.rank, ranked.of);
        port_StatsAddText("Ladder rating", value);
    }
    for (view = PORT_STATS_VIEW_FIGHTERS; view < PORT_STATS_VIEWS; view++) {
        port_StatsAddText(g_view_names[view], "...");
    }
}

static const char *port_StatsModeName(s32 by_mode) {
    return port_StatsModeWord(by_mode);
}

static void port_StatsKills(void) {
    static const char * const how[PORT_STATS_HOWS] = {
        "Ended by a kill", "Ended by a surrender", "Ended on points",
        "Ended on the clock"};
    char label[TEXT_BYTES];
    char value[TEXT_BYTES];
    s32 i;
    s32 side;

    for (side = 0; side < 2; side++) {
        const u32 *by_blow =
            side == 0 ? g_tally.given_by_blow : g_tally.taken_by_blow;
        const u32 *by_place =
            side == 0 ? g_tally.given_by_place : g_tally.taken_by_place;
        u32 behind = side == 0 ? g_tally.given_behind : g_tally.taken_behind;

        for (i = 1; i < PORT_STATS_BLOWS; i++) {
            if (by_blow[i] != 0) {
                snprintf(label, sizeof(label), "%s %s",
                         side == 0 ? "Killed with" : "Killed by",
                         g_blow_shown[i]);
                snprintf(value, sizeof(value), "%u", by_blow[i]);
                port_StatsAddText(label, value);
            }
        }
        for (i = 1; i < PORT_STATS_PLACES; i++) {
            if (by_place[i] != 0) {
                snprintf(label, sizeof(label), "%s the %s",
                         side == 0 ? "Kills to" : "Killed by a blow to",
                         g_place_shown[i]);
                snprintf(value, sizeof(value), "%u", by_place[i]);
                port_StatsAddText(label, value);
            }
        }
        if (behind != 0) {
            snprintf(value, sizeof(value), "%u", behind);
            port_StatsAddText(
                side == 0 ? "Kills from behind" : "Killed from behind", value);
        }
    }
    for (i = 0; i < PORT_STATS_HOWS; i++) {
        port_StatsAddScore(how[i], &g_tally.by_how[i]);
    }
    if (g_line_count == 0) {
        port_StatsAddText("No kills yet", "");
    }
}

s32 port_StatsPageItems(s32 view) {
    s32 i;

    port_StatsTally(&g_tally);
    g_line_count = 0;
    switch (view) {
    case PORT_STATS_VIEW_TOTALS:
        port_StatsTotals();
        break;
    case PORT_STATS_VIEW_FIGHTERS:
        port_StatsAddKeys(
            g_tally.by_fighter, PORT_STATS_KEYS, "As ", port_NameRoster);
        port_StatsAddKeys(
            g_tally.by_against, PORT_STATS_KEYS, "Against ", port_NameRoster);
        break;
    case PORT_STATS_VIEW_WEAPONS:
        port_StatsAddKeys(
            g_tally.by_weapon, PORT_STATS_KEYS, "", port_NameWeapon);
        break;
    case PORT_STATS_VIEW_STAGES:
        port_StatsAddKeys(
            g_tally.by_stage, PORT_STATS_KEYS, "", port_NameStage);
        break;
    case PORT_STATS_VIEW_MODES:
        port_StatsAddKeys(
            g_tally.by_mode, PORT_STATS_MODES, "", port_StatsModeName);
        break;
    case PORT_STATS_VIEW_OPPONENTS:
        for (i = 0; i < g_tally.opponent_count; i++) {
            char name[TEXT_BYTES];

            port_StatsOpponentName(&g_tally.opponents[i], name, sizeof(name));
            port_StatsAddScore(name, &g_tally.opponents[i].score);
        }
        break;
    case PORT_STATS_VIEW_KILLS:
        port_StatsKills();
        break;
    case PORT_STATS_VIEW_HISTORY:
        return port_StatsCount();
    }
    return g_line_count;
}

/* The history's row `item`, the newest first. */
static const PortStatsFight *port_StatsHistoryFight(s32 item) {
    return port_StatsFightAt(port_StatsCount() - 1 - item);
}

void port_StatsPageDescribe(s32 view, s32 item, char *label, u32 label_room,
                            char *value, u32 value_room) {
    value[0] = 0;
    if (view == PORT_STATS_VIEW_HISTORY) {
        const PortStatsFight *f = port_StatsHistoryFight(item);
        u32 seconds;

        if (f == NULL) {
            snprintf(label, label_room, "No fights yet");
            return;
        }
        seconds = f->frames / FRAMES_PER_SECOND;
        snprintf(
            label, label_room, "%s   %s  vs  %s",
            f->result == PORT_STATS_WON    ? "Won"
            : f->result == PORT_STATS_LOST ? "Lost"
                                           : "Drawn",
            port_NameRoster(f->fighter[0]), port_NameRoster(f->fighter[1]));
        snprintf(value, value_room, "%u:%02u%s", seconds / SECONDS_PER_MINUTE,
                 seconds % SECONDS_PER_MINUTE,
                 f->replay[0] != 0 ? ", replay" : "");
        return;
    }
    if (item < 0 || item >= g_line_count) {
        snprintf(label, label_room, "Nothing yet");
        return;
    }
    snprintf(label, label_room, "%s", g_lines[item].label);
    snprintf(value, value_room, "%s", g_lines[item].value);
}

void port_StatsPageDetail(s32 item, char *out, u32 room) {
    const PortStatsFight *f = port_StatsHistoryFight(item);
    char opponent[TEXT_BYTES];
    char match[TEXT_BYTES];
    char how[TEXT_BYTES];
    const PortStatsKill *kill;

    if (f == NULL) {
        out[0] = 0;
        return;
    }
    if (f->online != PORT_STATS_LOCAL) {
        snprintf(opponent, sizeof(opponent), "against %s",
                 f->opponent[0] != 0 ? f->opponent : "someone");
    } else if (f->cpu != PORT_STATS_PERSON) {
        snprintf(opponent, sizeof(opponent), "against the CPU %s",
                 f->cpu >= 0 && f->cpu <= PORT_CPU_LEVELS
                     ? g_cpu_shown[(s32)f->cpu]
                     : "");
    } else {
        snprintf(opponent, sizeof(opponent), "against a player beside you");
    }
    match[0] = 0;
    if (f->rule != 0 &&
        f->rule < sizeof(g_rule_shown) / sizeof(*g_rule_shown)) {
        snprintf(match, sizeof(match), ", %s %u, fight %u%s",
                 g_rule_shown[f->rule], f->count, f->match_fight,
                 f->match_result == PORT_STATS_WON    ? ", the match won"
                 : f->match_result == PORT_STATS_LOST ? ", the match lost"
                 : f->match_result == PORT_STATS_DRAW ? ", the match drawn"
                                                      : "");
    }
    kill = f->result == PORT_STATS_LOST ? &f->taken : &f->given;
    if (f->how == PORT_STATS_HOW_SURRENDER) {
        snprintf(how, sizeof(how), "by a surrender");
    } else if (f->how == PORT_STATS_HOW_POINTS) {
        snprintf(how, sizeof(how), "on points");
    } else if (f->how == PORT_STATS_HOW_TIME) {
        snprintf(how, sizeof(how), "on the clock");
    } else if (
        kill->blow != PORT_STATS_BLOW_NONE && kill->blow < PORT_STATS_BLOWS) {
        snprintf(
            how, sizeof(how), "by %s%s%s%s", g_blow_shown[kill->blow],
            kill->place != 0 && kill->place < PORT_STATS_PLACES ? " to the "
                                                                : "",
            kill->place < PORT_STATS_PLACES ? g_place_shown[kill->place] : "",
            kill->behind ? ", from behind" : "");
    } else {
        snprintf(how, sizeof(how), "by a kill");
    }
    snprintf(
        out, room, "%04u-%02u-%02u %02u:%02u   %s %s%s   %s   %s   %s and %s",
        f->year, f->month, f->day, f->hour, f->minute,
        port_StatsModeWord(port_StatsModeOf(f)), opponent, match, how,
        port_NameStage(f->stage), port_NameWeapon(f->weapon[0]),
        port_NameWeapon(f->weapon[1]));
}

const char *port_StatsPageHelp(s32 view, s32 item) {
    if (view == PORT_STATS_VIEW_TOTALS) {
        if (item >= g_line_count - (PORT_STATS_VIEWS - 1) &&
            item < g_line_count) {
            return "Enter / Cross: the list";
        }
        snprintf(g_help, sizeof(g_help),
                 "Every fight and match finished is kept in %s",
                 port_GetStatsFile()[0] != 0 ? "rebushido-stats.txt beside "
                                               "the settings"
                                             : "(no record in this run)");
        return g_help;
    }
    if (view == PORT_STATS_VIEW_HISTORY) {
        const PortStatsFight *f = port_StatsHistoryFight(item);

        if (f == NULL) {
            return "";
        }
        return f->replay[0] != 0 ? "Enter / Cross: watch its replay"
                                 : "No replay was kept of this fight";
    }
    if (view == PORT_STATS_VIEW_KILLS) {
        return "The first kill of each fight each way, by what struck and "
               "where";
    }
    return "Fights won and lost of the record";
}

s32 port_StatsPageAccept(s32 item, char *note, u32 room) {
    const PortStatsFight *f = port_StatsHistoryFight(item);
    char path[PATH_BYTES];
    char why[PORT_REPLAY_WHY_BYTES];

    if (f == NULL || f->replay[0] == 0) {
        return FALSE;
    }
    snprintf(path, sizeof(path), "%s/%s", port_GetReplayFolder(), f->replay);
    if (!port_PlayReplay(path, FALSE, FALSE, why, sizeof(why))) {
        snprintf(note, room, "Not played: %s", why);
        return FALSE;
    }
    port_Note("replay: %s, from the Stats page", f->replay);
    return TRUE;
}
