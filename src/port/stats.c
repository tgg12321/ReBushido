/* The native port's record of fights: watching the game (port_stats.h;
 * docs/PORT.md, "Stats").
 *
 * The game's hooks note what happened in the step being run, with the
 * input's number it was run at: a blow that killed, a fighter's fall, a
 * fight decided. Each boundary takes in what the step before it noted
 * (a note of a later input than the boundary's is a step a rollback took
 * back, and is dropped), and what was seen at a later boundary than this
 * one is forgotten first: the state of this boundary was put back, and the
 * steps after it, run again, note it again (the step before it, whose
 * notes this boundary saw, is not run again). A frame run ahead notes
 * nothing (g_port_ahead).
 *
 * A fight's end, seen, is the record's line made of the game's state
 * there (the fighters, their wounds, the match's score); it is counted at
 * once outside a session, and in a session when netplay says that no
 * rollback can reach it (port_NetplayFinalFrames). A fight's end is
 * counted once whatever runs it again: one at or before the last counted
 * (the rollback self-test's second run, a session's rerun) is not
 * counted again. Nothing is counted while a replay plays, in training, by
 * a room's spectator, or of a fight no person plays (both fighters the
 * CPU's). Outside the state (config/port_state.txt). */
#include "common.h"
#include "port_host.h"
#include "port_net.h"
#include "port_ranked.h"
#include "port_replay.h"
#include "bb2.h"

/* notes a step may make; kills a fight may see; the fights counted last
 * and the replays written last, kept to be given to each other */
#define NOTES 8
#define KILLS_SEEN 160
#define DONE_KEPT 8
#define REPLAYS_KEPT 4
/* a fall this soon after a killing blow is that blow's */
#define LETHAL_WITHIN 8
/* the group battle's rule word (stats_store.c), after port_match.h's */
#define RULE_GROUP 4
#define COUNTS_MOST 255

typedef struct {
    u32 at;
    s32 victim;
    PortStatsKill kill;
} StatsHitNote;

typedef struct {
    u32 at;
    s32 fighter;
    s32 paired;
} StatsFallNote;

typedef struct {
    u32 seen;
    s32 victim;
    PortStatsKill kill;
} StatsKillSeen;

static struct {
    /* noted in the step */
    StatsHitNote hits[NOTES];
    s32 hit_count;
    StatsFallNote falls[NOTES];
    s32 fall_count;
    s32 decided;
    u32 decided_at;
    s32 how;
    s32 fell[2];
    s32 winner;
    s32 team;
    u32 team_at;
    s32 on_time;
    /* seen at the boundaries */
    s32 netplay;
    s32 was_in_fight;
    u32 fight_start;
    u32 left_fight;
    s32 has_lethal[2];
    u32 lethal_seen[2];
    PortStatsKill lethal[2];
    StatsKillSeen kills[KILLS_SEEN];
    s32 kill_count;
    u32 last_decided;
    u32 before_decided;
    /* a fight's end seen, waiting to be final */
    s32 pending;
    u32 pending_at;
    u32 pending_start;
    PortStatsFight fight;
    /* the last fight's end counted: a run's own, a session's */
    u32 counted[2];

    /* the fights counted last, and the replays written last */
    struct {
        s32 index;
        s32 netplay;
        u32 start;
        u32 end;
    } done[DONE_KEPT];

    s32 done_next;

    struct {
        char name[PORT_STATS_REPLAY_BYTES];
        s32 netplay;
        u32 end;
    } replays[REPLAYS_KEPT];

    s32 replay_next;
} g_stats;

static s32 port_StatsIsOn(void) { return port_GetStatsFile()[0] != 0; }

/* ---- the game's hooks ---- */

static s32 port_PlaceOf(s32 body_point) {
    if (body_point == BODY_HEAD) {
        return PORT_STATS_PLACE_HEAD;
    }
    if (body_point < BODY_HIP) {
        return PORT_STATS_PLACE_CHEST;
    }
    if (body_point < BODY_OFF_ARM) {
        return PORT_STATS_PLACE_HIP;
    }
    if (body_point < BODY_SWORD_ARM) {
        return PORT_STATS_PLACE_OFF_ARM;
    }
    if (body_point < BODY_LEGS) {
        return PORT_STATS_PLACE_SWORD_ARM;
    }
    return body_point < BODY_POINT_COUNT ? PORT_STATS_PLACE_LEGS
                                         : PORT_STATS_PLACE_NONE;
}

/* What struck: a thrown thing, a shot, or the striker's blade by the
 * move it struck in. */
static s32 port_BlowOf(
    const Fighter *striker, s32 by_object, s32 blade, s32 by_sidearm) {
    u16 move = striker->move_kind;

    if (by_object == 1) {
        return PORT_STATS_BLOW_THROWN;
    }
    if (by_sidearm || WEAPON_IS_GUN(striker->weapon_id)) {
        return PORT_STATS_BLOW_SHOT;
    }
    if (move == MOVE_KIND_ARUN) {
        return PORT_STATS_BLOW_RUNNING;
    }
    if (move == MOVE_KIND_HOM_ST || move == MOVE_KIND_HOM_AT ||
        move == MOVE_KIND_HOM_ED) {
        return PORT_STATS_BLOW_LEAPING;
    }
    if (move == MOVE_KIND_CLIMB_ATTACK) {
        return PORT_STATS_BLOW_CLIMBING;
    }
    if (move == MOVE_KIND_PAIRED) {
        return PORT_STATS_BLOW_PAIRED;
    }
    return blade != 0 ? PORT_STATS_BLOW_OFF_HAND : PORT_STATS_BLOW_STRIKE;
}

/* A killing blow noted (port_stats.h): what struck, where, from where;
 * not a blow to one dead already, nor a frame run ahead. */
void port_StatsHit(const Fighter *victim, s32 body_point, s32 facing,
                   s32 by_object, s32 blade, s32 by_sidearm, s32 result) {
    StatsHitNote *note;
    s32 i;

    if (result != 1 || g_port_ahead || !port_StatsIsOn() ||
        victim->is_defeated || victim->move_kind == MOVE_KIND_END_GAME ||
        victim->move_kind == MOVE_KIND_DTH) {
        return;
    }
    for (i = 0; i < g_stats.hit_count; i++) {
        if (g_stats.hits[i].victim == victim->index) {
            /* its first killing blow of the step */
            return;
        }
    }
    if (g_stats.hit_count >= NOTES) {
        return;
    }
    note = &g_stats.hits[g_stats.hit_count++];
    note->at = g_port_input_count;
    note->victim = victim->index != 0;
    note->kill.blow =
        (u8)port_BlowOf(victim->other, by_object, blade, by_sidearm);
    note->kill.place = (u8)port_PlaceOf(body_point);
    /* from in front where the game's own fall is (fighter_TakeHit) */
    note->kill.behind = (u8)(facing <= ANGLE_TURN / 4);
    note->kill.down = (u8)(victim->move_kind == MOVE_KIND_DOWN ||
                           victim->move_kind == MOVE_KIND_GET_UP);
}

void port_StatsFell(const Fighter *fighter) {
    StatsFallNote *note;

    if (g_port_ahead || !port_StatsIsOn() || g_stats.fall_count >= NOTES) {
        return;
    }
    note = &g_stats.falls[g_stats.fall_count++];
    note->at = g_port_input_count;
    note->fighter = fighter->index != 0;
    note->paired = fighter->move_kind == MOVE_KIND_PAIRED;
}

void port_StatsFightDecided(s32 how) {
    if (g_port_ahead || !port_StatsIsOn() || g_stats.decided) {
        return;
    }
    g_stats.decided = TRUE;
    g_stats.decided_at = g_port_input_count;
    g_stats.how = how;
    g_stats.fell[0] = g_fighters[0].is_defeated != 0;
    g_stats.fell[1] = g_fighters[1].is_defeated != 0;
    g_stats.winner = g_fight_winner;
}

void port_StatsTeamMatch(s32 on_time) {
    if (g_port_ahead || !port_StatsIsOn()) {
        return;
    }
    g_stats.team = TRUE;
    g_stats.team_at = g_port_input_count;
    g_stats.on_time = on_time;
}

/* ---- the boundary ---- */

static void port_StatsForgetNotes(void) {
    g_stats.hit_count = 0;
    g_stats.fall_count = 0;
    g_stats.decided = FALSE;
    g_stats.team = FALSE;
}

/* Everything of the fight being fought forgotten: a replay plays, or a
 * session begins or ends. */
static void port_StatsForgetFight(void) {
    port_StatsForgetNotes();
    g_stats.kill_count = 0;
    g_stats.has_lethal[0] = FALSE;
    g_stats.has_lethal[1] = FALSE;
    g_stats.pending = FALSE;
    g_stats.last_decided = 0;
    g_stats.before_decided = 0;
    g_stats.fight_start = 0;
    g_stats.left_fight = 0;
    g_stats.was_in_fight = FALSE;
}

/* What was seen after the boundary `count` forgotten: a rollback put its
 * state back, and runs the steps after it again. */
static void port_StatsRollBack(u32 count) {
    s32 kept = 0;
    s32 i;

    for (i = 0; i < g_stats.kill_count; i++) {
        if (g_stats.kills[i].seen <= count) {
            g_stats.kills[kept++] = g_stats.kills[i];
        }
    }
    g_stats.kill_count = kept;
    for (i = 0; i < 2; i++) {
        if (g_stats.has_lethal[i] && g_stats.lethal_seen[i] > count) {
            g_stats.has_lethal[i] = FALSE;
        }
    }
    if (g_stats.pending && g_stats.pending_at > count) {
        g_stats.pending = FALSE;
    }
    if (g_stats.last_decided > count) {
        g_stats.last_decided = g_stats.before_decided;
        g_stats.before_decided = 0;
    }
}

/* A kill seen at the boundary `count`: the oldest let go where there are
 * too many. */
static void port_StatsSeeKill(u32 count, s32 victim, const PortStatsKill *of) {
    if (g_stats.kill_count >= KILLS_SEEN) {
        s32 i;

        for (i = 1; i < KILLS_SEEN; i++) {
            g_stats.kills[i - 1] = g_stats.kills[i];
        }
        g_stats.kill_count--;
    }
    g_stats.kills[g_stats.kill_count].seen = count;
    g_stats.kills[g_stats.kill_count].victim = victim;
    g_stats.kills[g_stats.kill_count].kill = *of;
    g_stats.kill_count++;
}

/* The step's notes taken in at the boundary `count`. */
static void port_StatsTakeNotes(u32 count) {
    s32 i;

    for (i = 0; i < g_stats.hit_count; i++) {
        const StatsHitNote *note = &g_stats.hits[i];

        if (note->at <= count) {
            g_stats.has_lethal[note->victim] = TRUE;
            g_stats.lethal_seen[note->victim] = count;
            g_stats.lethal[note->victim] = note->kill;
        }
    }
    for (i = 0; i < g_stats.fall_count; i++) {
        const StatsFallNote *note = &g_stats.falls[i];
        s32 victim = note->fighter;
        PortStatsKill kill;

        if (note->at > count) {
            continue;
        }
        if (g_stats.has_lethal[victim] &&
            g_stats.lethal_seen[victim] + LETHAL_WITHIN >= count) {
            kill = g_stats.lethal[victim];
        } else {
            kill.blow = (u8)(note->paired ? PORT_STATS_BLOW_PAIRED
                                          : PORT_STATS_BLOW_OTHER);
            kill.place = PORT_STATS_PLACE_NONE;
            kill.behind = 0;
            kill.down = 0;
        }
        /* (the blow is kept: a rollback to between the two runs the fall
         * again, not the blow) */
        port_StatsSeeKill(count, victim, &kill);
    }
}

/* The player's side in the fight: -1 where no person plays it. */
static s32 port_StatsSide(void) {
    if (port_IsNetplayOn()) {
        u32 mask = port_NetplayLocalPads();
        s32 pad;

        for (pad = 0; pad < 4; pad++) {
            if ((mask >> pad) & 1) {
                /* a team is a slot: the first and third pads, the second
                 * and fourth (port_duo.h) */
                return pad & 1;
            }
        }
        return 0;
    }
    if (g_fighters[0].is_cpu && g_fighters[1].is_cpu) {
        return -1;
    }
    return g_fighters[0].is_cpu ? 1 : 0;
}

static u8 port_Clamp(s32 count) {
    return (u8)(count < 0 ? 0 : count > COUNTS_MOST ? COUNTS_MOST : count);
}

static void port_StatsWounds(const Fighter *of, u8 *out) {
    out[PORT_STATS_WOUND_LEGS] = port_Clamp(of->leg_wounds);
    out[PORT_STATS_WOUND_ARM] = port_Clamp(of->arm_wounds);
    out[PORT_STATS_WOUND_BODY] = port_Clamp(of->body_wounds);
    out[PORT_STATS_WOUND_OFF_ARM] = (u8)(of->off_arm_ok == 0);
}

/* The result for the side `me` of `side_won` (0, 1, or 2 a draw). */
static u8 port_ResultFor(s32 me, s32 side_won) {
    return (u8)(side_won == 2    ? PORT_STATS_DRAW
                : side_won == me ? PORT_STATS_WON
                                 : PORT_STATS_LOST);
}

/* The fight's line made of the game's state at the boundary `count`
 * where it was decided, `side_won` its winner's side (2: drawn). FALSE
 * where it is not a fight to count. */
static s32 port_StatsMake(u32 count, s32 side_won, s32 how) {
    PortStatsFight *line = &g_stats.fight;
    const PortMatch *match = &g_port_match;
    s32 me = port_StatsSide();
    s32 other = 1 - me;
    u32 start = g_stats.fight_start > g_stats.last_decided
                    ? g_stats.fight_start
                    : g_stats.last_decided;
    u32 end = g_stats.left_fight > start && g_stats.left_fight <= count
                  ? g_stats.left_fight
                  : count;
    PortDate date;
    u32 i;

    if (me < 0 || g_fight_mode == FIGHT_MODE_PRACTICE) {
        return FALSE;
    }
    for (i = 0; i < sizeof(*line); i++) {
        ((u8 *)line)[i] = 0;
    }
    port_HostDate(&date);
    line->year = (u16)date.year;
    line->month = (u8)date.month;
    line->day = (u8)date.day;
    line->hour = (u8)date.hour;
    line->minute = (u8)date.minute;
    line->second = (u8)date.second;
    line->mode = (u8)g_fight_mode;
    line->teams = (u8)(port_DuoIsOn() ? 2 : 1);
    line->side = (u8)me;
    line->stage = g_fight_stage;
    line->fighter[0] = g_fighters[me].roster_id;
    line->weapon[0] = g_fighters[me].weapon_id;
    line->fighter[1] = g_fighters[other].roster_id;
    line->weapon[1] = g_fighters[other].weapon_id;
    line->cpu = (s8)(!g_fighters[other].is_cpu ? PORT_STATS_PERSON
                     : port_CpuPlays(other)    ? port_GetCpuLevel(other)
                                               : PORT_CPU_ORIGINAL);
    line->result = port_ResultFor(me, side_won);
    line->how = (u8)how;
    line->frames = end - start;
    port_StatsWounds(&g_fighters[me], line->wounds_taken);
    port_StatsWounds(&g_fighters[other], line->wounds_given);
    line->given.blow = PORT_STATS_BLOW_NONE;
    line->taken.blow = PORT_STATS_BLOW_NONE;
    for (i = 0; i < (u32)g_stats.kill_count; i++) {
        const StatsKillSeen *seen = &g_stats.kills[i];

        if (seen->seen <= g_stats.last_decided || seen->seen > count) {
            continue;
        }
        if (seen->victim == me) {
            if (line->kills_taken++ == 0) {
                line->taken = seen->kill;
            }
        } else if (line->kills_given++ == 0) {
            line->given = seen->kill;
        }
    }
    /* the match it is a fight of */
    if (g_fight_mode == FIGHT_MODE_TEAM) {
        line->rule = RULE_GROUP;
        line->count = g_team_match_count;
        line->match_fight = g_team_match;
        line->match_result = PORT_STATS_GOING;
        if (g_team_match >= g_team_match_count) {
            s32 wins[3] = {0, 0, 0};

            for (i = 0; i < g_team_match_count; i++) {
                wins[g_team_results[i] < 3 ? g_team_results[i] : 2]++;
            }
            line->match_result = port_ResultFor(
                me, wins[0] > wins[1]   ? 0
                    : wins[1] > wins[0] ? 1
                                        : 2);
        }
    } else if (match->rule != PORT_MATCH_SINGLE &&
               match->mode == g_fight_mode && match->fights > 0) {
        line->rule = match->rule;
        line->count = match->count;
        line->match_fight = match->fights;
        line->match_result =
            !match->over
                ? (u8)PORT_STATS_GOING
                : port_ResultFor(
                      me,
                      match->winner == PORT_MATCH_DRAWN ? 2 : match->winner);
    } else {
        line->rule = PORT_MATCH_SINGLE;
        line->match_fight = 1;
        line->match_result = line->result;
    }
    /* online */
    if (port_IsNetplayOn()) {
        PortSessionLobby lobby;
        PortRankedView ranked;

        port_GetSessionLobby(&lobby);
        line->online =
            port_SessionIsRoomMatch() ? PORT_STATS_ROOM : PORT_STATS_ONLINE;
        for (i = 0; lobby.peer_name[i] != 0 && i + 1 < PORT_STATS_NAME_BYTES;
             i++) {
            line->opponent[i] = lobby.peer_name[i];
        }
        line->opponent[i] = 0;
        if (port_RankedIsOn()) {
            port_RankedGet(&ranked);
            if (ranked.phase == PORT_RANKED_MATCHED ||
                ranked.phase == PORT_RANKED_REPORTED) {
                line->online = PORT_STATS_RANKED;
                for (i = 0;
                     ranked.opponent[i] != 0 && i + 1 < PORT_STATS_NAME_BYTES;
                     i++) {
                    line->opponent[i] = ranked.opponent[i];
                }
                line->opponent[i] = 0;
            }
        }
    }
    return TRUE;
}

/* A fight's end seen at the boundary `count`: its line made, waiting to
 * be final. */
static void port_StatsDecide(u32 count, s32 side_won, s32 how) {
    if (port_StatsMake(count, side_won, how)) {
        g_stats.pending = TRUE;
        g_stats.pending_at = count;
        g_stats.pending_start = g_stats.fight_start > g_stats.last_decided
                                    ? g_stats.fight_start
                                    : g_stats.last_decided;
    }
    g_stats.before_decided = g_stats.last_decided;
    g_stats.last_decided = count;
}

/* The file's name of a path, without its folder. */
static void port_StatsBaseName(const char *path, char *out, u32 room) {
    const char *name = path;
    u32 i;

    for (; *path != 0; path++) {
        if (*path == '/' || *path == '\\') {
            name = path + 1;
        }
    }
    for (i = 0; name[i] != 0 && i + 1 < room; i++) {
        out[i] = name[i];
    }
    out[i] = 0;
}

/* The pending fight counted, unless it was before. */
static void port_StatsCountPending(void) {
    s32 netplay = g_stats.netplay;
    s32 index;
    s32 i;

    g_stats.pending = FALSE;
    if (g_stats.pending_at <= g_stats.counted[netplay]) {
        /* run again (the rollback self-test, a session's rerun) */
        return;
    }
    g_stats.counted[netplay] = g_stats.pending_at;
    /* its replay, if it was written first (a kaishaku's) */
    for (i = 0; i < REPLAYS_KEPT; i++) {
        if (g_stats.replays[i].name[0] != 0 &&
            g_stats.replays[i].netplay == netplay &&
            g_stats.replays[i].end > g_stats.pending_start &&
            g_stats.replays[i].end <= g_stats.pending_at) {
            u32 j;

            for (j = 0; j < PORT_STATS_REPLAY_BYTES; j++) {
                g_stats.fight.replay[j] = g_stats.replays[i].name[j];
            }
            g_stats.replays[i].name[0] = 0;
        }
    }
    port_StatsAdd(&g_stats.fight);
    index = port_StatsCount() - 1;
    g_stats.done[g_stats.done_next].index = index;
    g_stats.done[g_stats.done_next].netplay = netplay;
    g_stats.done[g_stats.done_next].start = g_stats.pending_start;
    g_stats.done[g_stats.done_next].end = g_stats.pending_at;
    g_stats.done_next = (g_stats.done_next + 1) % DONE_KEPT;
    port_Log("stats: a fight counted (%s, %s by %s), %d in the record\n",
             port_StatsModeWord(port_StatsModeOf(&g_stats.fight)),
             g_stats.fight.result == PORT_STATS_WON    ? "won"
             : g_stats.fight.result == PORT_STATS_LOST ? "lost"
                                                       : "drawn",
             port_StatsHowWord(g_stats.fight.how), port_StatsCount());
}

/* A replay's file written (port_stats.h): given to its fight counted,
 * or kept for the fight not counted yet. */
void port_StatsReplayWritten(const char *path, u32 end_count) {
    char name[PORT_STATS_REPLAY_BYTES];
    s32 netplay = port_IsNetplayOn();
    s32 i;

    if (!port_StatsIsOn()) {
        return;
    }
    port_StatsBaseName(path, name, sizeof(name));
    for (i = 0; i < DONE_KEPT; i++) {
        const PortStatsFight *fight = port_StatsFightAt(g_stats.done[i].index);

        if (fight != NULL && fight->replay[0] == 0 &&
            g_stats.done[i].netplay == netplay &&
            end_count > g_stats.done[i].start &&
            end_count <= g_stats.done[i].end) {
            port_StatsSetReplay(g_stats.done[i].index, name);
            return;
        }
    }
    /* the fight's end not counted yet: kept for it */
    for (i = 0; i < PORT_STATS_REPLAY_BYTES; i++) {
        g_stats.replays[g_stats.replay_next].name[i] = name[i];
    }
    g_stats.replays[g_stats.replay_next].netplay = netplay;
    g_stats.replays[g_stats.replay_next].end = end_count;
    g_stats.replay_next = (g_stats.replay_next + 1) % REPLAYS_KEPT;
}

/* The boundary (port_stats.h): a rollback's forgotten, the notes taken
 * in, a fight's end seen, and one final counted. */
void port_StatsBoundary(void) {
    u32 count = g_port_input_count;
    s32 netplay = port_IsNetplayOn();
    s32 in_fight = g_sys_screen == SCREEN_FIGHT_UPDATE;

    if (!port_StatsIsOn() || port_ReplayIsPlaying() || port_TrainingIsOn() ||
        port_GetSessionRole() == PORT_SESSION_WATCH) {
        port_StatsForgetFight();
        return;
    }
    if (netplay != g_stats.netplay) {
        /* a session begins or ends: its numbers are its own */
        port_StatsForgetFight();
        g_stats.netplay = netplay;
        if (netplay) {
            g_stats.counted[1] = 0;
        }
    }
    port_StatsRollBack(count);
    if (in_fight && !g_stats.was_in_fight) {
        g_stats.fight_start = count;
    } else if (!in_fight && g_stats.was_in_fight) {
        g_stats.left_fight = count;
    }
    port_StatsTakeNotes(count);
    if (g_stats.decided && g_stats.decided_at <= count &&
        g_fight_mode != FIGHT_MODE_CHANBARA &&
        g_fight_mode != FIGHT_MODE_TEAM) {
        s32 side_won =
            g_stats.how == PORT_STATS_HOW_SURRENDER ? (g_stats.winner != 0)
            : g_stats.fell[0] && g_stats.fell[1]    ? 2
            : g_stats.fell[0]                       ? 1
            : g_stats.fell[1]                       ? 0
            : g_stats.winner == FIGHT_NOBODY        ? 2
                                                    : g_stats.winner != 0;

        port_StatsDecide(count, side_won, g_stats.how);
    }
    if (g_stats.team && g_stats.team_at <= count && g_team_match > 0) {
        s32 result = g_team_results[g_team_match - 1];

        port_StatsDecide(
            count, result < 2 ? result : 2,
            g_stats.on_time ? PORT_STATS_HOW_TIME : PORT_STATS_HOW_POINTS);
    }
    if (!in_fight && g_stats.was_in_fight &&
        g_fight_mode == FIGHT_MODE_CHANBARA) {
        /* a run of Slash: a fight of a hundred opponents, lost where the
         * player fell */
        port_StatsDecide(
            count, g_fighters[0].is_defeated ? 1 : 0, PORT_STATS_HOW_KILL);
    }
    g_stats.was_in_fight = in_fight;
    port_StatsForgetNotes();
    if (g_stats.pending &&
        (!netplay ||
         (!g_port_rerun && port_NetplayFinalFrames() >= g_stats.pending_at))) {
        port_StatsCountPending();
    }
}
