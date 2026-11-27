/* A local tournament (port_tournament.h; docs/PORT.md, "A local
 * tournament"): its matches begun from the main menu, its sides given to
 * the entrants, its results into the bracket. A state file: everything
 * here is saved, put back and hashed with the game's own variables, so a
 * rollback, a frame run ahead and a fight's replay see the tournament as
 * the fight did. The window's asks come in at the host's boundary
 * (tournament_ask.c), never during a step. */
#include "common.h"
#include "port_replay.h"
#include "bb2.h"

PortTournament g_port_tournament;

/* What a CPU's draw is made from: the six fighters an empty save offers
 * in versus, the six blades, and stages of open ground (the CPU's levels
 * were measured on them: tools/port_cpu_bench.py). */
static const u8 g_port_tournament_fighters[] = {
    CHARACTER_KANNUKI, CHARACTER_TATSUMI, CHARACTER_MIKADO,
    CHARACTER_GENGORO, CHARACTER_KAUN,    CHARACTER_JO,
};
static const u8 g_port_tournament_weapons[] = {
    WEAPON_ID_KATANA,     WEAPON_ID_LONG_SWORD, WEAPON_ID_NODACHI,
    WEAPON_ID_BROADSWORD, WEAPON_ID_NAGINATA,   WEAPON_ID_YARI,
};
static const u8 g_port_tournament_stages[] = {
    STAGE_TATARA_SHRINE,      STAGE_BAMBOO_THICKET,    STAGE_MEIKYOKAN_DOJO,
    STAGE_ORANGE_TREE_FIELDS, STAGE_TERRACED_FIELDS,   STAGE_LUMBERYARD,
    STAGE_KIMIGAURA_COAST,    STAGE_OUTSIDE_MEIKYOKAN,
};
#define COUNT_OF(array) ((u32)(sizeof(array) / sizeof((array)[0])))

/* xorshift32, the tournament's own dice (never the game's rand()) */
#define DICE_SHIFT_A 13
#define DICE_SHIFT_B 17
#define DICE_SHIFT_C 5
#define DICE_NONE 0x2545F491U

/* A throw of the dice, 0 to sides - 1. */
static u32 port_TournamentDie(PortTournament *tournament, u32 sides) {
    u32 dice = tournament->dice != 0 ? tournament->dice : DICE_NONE;

    dice ^= dice << DICE_SHIFT_A;
    dice ^= dice >> DICE_SHIFT_B;
    dice ^= dice << DICE_SHIFT_C;
    tournament->dice = dice;
    return sides != 0 ? dice % sides : 0;
}

static s32 port_TournamentIsCpu(const PortTournament *tournament, s32 entrant) {
    return entrant < 0 || entrant >= PORT_BRACKET_MOST ||
           tournament->setup.entrant[entrant].pad >= PORT_TOURNAMENT_PADS;
}

s32 port_TournamentIsOn(void) {
    return g_port_tournament.phase != PORT_TOURNAMENT_OFF;
}

s32 port_TournamentIsPlaying(void) {
    return g_port_tournament.phase == PORT_TOURNAMENT_PLAYING;
}

/* A side's CPU's fighter and weapon for the match: its own, or drawn;
 * never the other side's fighter, which the select screen would not
 * give. */
static void port_TournamentDraw(PortTournament *tournament, s32 side) {
    const PortTournamentEntrant *entrant =
        &tournament->setup.entrant[tournament->side_entrant[side]];
    u32 fighters = COUNT_OF(g_port_tournament_fighters);
    u32 i;

    tournament->fighter[side] =
        entrant->fighter != PORT_TOURNAMENT_RANDOM
            ? entrant->fighter
            : g_port_tournament_fighters[port_TournamentDie(
                  tournament, fighters)];
    tournament->weapon[side] =
        entrant->weapon != PORT_TOURNAMENT_RANDOM
            ? entrant->weapon
            : g_port_tournament_weapons[port_TournamentDie(
                  tournament, COUNT_OF(g_port_tournament_weapons))];
    if (side == 1 && tournament->fighter[1] == tournament->fighter[0]) {
        /* the same as the first side's: the next of the draw's list */
        for (i = 0; i < fighters; i++) {
            if (g_port_tournament_fighters[i] == tournament->fighter[1]) {
                break;
            }
        }
        tournament->fighter[1] = g_port_tournament_fighters[(i + 1) % fighters];
    }
}

/* The match the bracket says is next, its sides seated (a player first
 * where the other is the CPU), each CPU's draw and the stage made: FALSE
 * where the bracket has none. */
static s32 port_TournamentSeat(PortTournament *tournament) {
    s32 next = port_BracketNext(&tournament->bracket);
    const PortBracketMatch *match;
    s32 side;

    if (next < 0) {
        return FALSE;
    }
    match = &tournament->bracket.match[next];
    tournament->match = (u8)next;
    tournament->side_entrant[0] = match->who[0];
    tournament->side_entrant[1] = match->who[1];
    if (port_TournamentIsCpu(tournament, match->who[0]) &&
        !port_TournamentIsCpu(tournament, match->who[1])) {
        tournament->side_entrant[0] = match->who[1];
        tournament->side_entrant[1] = match->who[0];
    }
    for (side = 0; side < 2; side++) {
        port_TournamentDraw(tournament, side);
    }
    tournament->stage = g_port_tournament_stages[port_TournamentDie(
        tournament, COUNT_OF(g_port_tournament_stages))];
    tournament->began = FALSE;
    return TRUE;
}

/* A frame of the top menu's page (port_tournament.h): back with no
 * result, the bracket asked for, the match asked for seated. */
s32 port_TournamentTopMenu(void) {
    PortTournament *tournament = &g_port_tournament;

    if (tournament->phase == PORT_TOURNAMENT_OFF) {
        return PORT_TOURNAMENT_STAY;
    }
    if (tournament->phase == PORT_TOURNAMENT_PLAYING) {
        /* back with no result (the select screen left, the pause's Return
         * to Main Menu): the match is not played, the bracket again */
        tournament->phase = PORT_TOURNAMENT_BETWEEN;
        tournament->ask_bracket = TRUE;
    }
    if (tournament->ask_bracket) {
        tournament->ask_bracket = FALSE;
        port_TopMenuOpenPage(PORT_TOP_PAGE_TOP);
        port_TopMenuCursorOn(PORT_TOP_TOURNAMENT);
        port_TopMenuAsk(PORT_TOP_ASK_BRACKET);
    }
    if (tournament->phase != PORT_TOURNAMENT_STARTING) {
        return PORT_TOURNAMENT_STAY;
    }
    if (!port_TournamentSeat(tournament)) {
        tournament->phase = PORT_TOURNAMENT_DONE;
        return PORT_TOURNAMENT_STAY;
    }
    tournament->phase = PORT_TOURNAMENT_PLAYING;
    return port_TournamentIsCpu(tournament, tournament->side_entrant[0]) &&
                   port_TournamentIsCpu(tournament, tournament->side_entrant[1])
               ? PORT_TOURNAMENT_TO_FIGHT
               : PORT_TOURNAMENT_TO_SELECT;
}

void port_TournamentFightPicks(u8 *fighters, u8 *weapons, u8 *stage) {
    const PortTournament *tournament = &g_port_tournament;

    fighters[0] = tournament->fighter[0];
    fighters[1] = tournament->fighter[1];
    weapons[0] = tournament->weapon[0];
    weapons[1] = tournament->weapon[1];
    *stage = tournament->stage;
}

/* A mode begins: each side the CPU's or a player's, and what each
 * side picked kept. */
void port_TournamentApplyConfig(void) {
    PortTournament *tournament = &g_port_tournament;
    s32 side;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING) {
        return;
    }
    for (side = 0; side < 2; side++) {
        s32 entrant = tournament->side_entrant[side];

        g_fight_config.is_cpu[side] =
            (s8)port_TournamentIsCpu(tournament, entrant);
        /* what was picked, for the bracket's page and the record */
        tournament->fighter[side] =
            g_fighter_roster_characters[(u8)g_fight_config.roster[side]];
        tournament->weapon[side] = (u8)g_fight_config.weapon[side];
    }
    tournament->began = TRUE;
}

s32 port_TournamentRule(s32 *rule, s32 *count) {
    const PortTournament *tournament = &g_port_tournament;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING &&
        tournament->phase != PORT_TOURNAMENT_STARTING) {
        return FALSE;
    }
    *rule = tournament->setup.rule;
    *count = tournament->setup.rule_count;
    return TRUE;
}

/* A CPU entrant's level for its place. */
s32 port_TournamentCpuLevel(s32 place, s32 *level) {
    const PortTournament *tournament = &g_port_tournament;
    s32 entrant;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING || place < 0 ||
        place > 1) {
        return FALSE;
    }
    entrant = tournament->side_entrant[place];
    if (!port_TournamentIsCpu(tournament, entrant) ||
        entrant >= PORT_BRACKET_MOST) {
        return FALSE;
    }
    *level = tournament->setup.entrant[entrant].cpu;
    return TRUE;
}

s32 port_TournamentCpuPick(s32 side, s32 *fighter, s32 *weapon) {
    const PortTournament *tournament = &g_port_tournament;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING || side < 0 || side > 1 ||
        !port_TournamentIsCpu(tournament, tournament->side_entrant[side])) {
        return FALSE;
    }
    *fighter = tournament->fighter[side];
    *weapon = tournament->weapon[side];
    return TRUE;
}

/* The match in play won by the side `winner`: the bracket told, the
 * bracket asked for at the main menu. */
static void port_TournamentDecide(PortTournament *tournament, s32 winner) {
    const PortBracketMatch *match;
    s32 entrant;
    s32 side;

    match = &tournament->bracket.match[tournament->match];
    entrant = tournament->side_entrant[winner];
    for (side = 0; side < 2; side++) {
        s32 who = tournament->side_entrant[side];

        if (who < PORT_BRACKET_MOST) {
            tournament->last_fighter[who] = tournament->fighter[side];
            tournament->last_weapon[who] = tournament->weapon[side];
        }
    }
    if (port_BracketReport(&tournament->bracket, tournament->match,
                           match->who[0] == entrant ? 0 : 1)) {
        tournament->decided++;
    }
    tournament->phase = port_BracketIsDone(&tournament->bracket)
                            ? PORT_TOURNAMENT_DONE
                            : PORT_TOURNAMENT_BETWEEN;
    tournament->ask_bracket = TRUE;
}

/* The match decided at its result's card: the bracket told, the
 * main menu. */
s32 port_TournamentMatchOver(s32 winner) {
    PortTournament *tournament = &g_port_tournament;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING ||
        (winner != 0 && winner != 1)) {
        return FALSE;
    }
    port_TournamentDecide(tournament, winner);
    /* back to the main menu, as a Crew match's end goes (match.c) */
    cdrom_StopAndWait();
    snd_EndFight();
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    g_menu_reopen_mode = 0;
    g_sys_screen = SCREEN_MENU_INIT;
    return TRUE;
}

/* A fighter's wounds, as the judges count them: the legs', the sword
 * arm's and the body's, and the off arm lost. */
static s32 port_TournamentWounds(const Fighter *fighter) {
    return fighter->leg_wounds + fighter->arm_wounds + fighter->body_wounds +
           (fighter->off_arm_ok == 0);
}

/* Two CPUs past PORT_TOURNAMENT_JUDGE_FRAMES: the one less wounded
 * wins (the dice where they are alike), and out of the fight. */
s32 port_TournamentJudge(void) {
    PortTournament *tournament = &g_port_tournament;
    s32 first;
    s32 second;
    s32 winner;

    if (tournament->phase != PORT_TOURNAMENT_PLAYING ||
        !port_TournamentIsCpu(tournament, tournament->side_entrant[0]) ||
        !port_TournamentIsCpu(tournament, tournament->side_entrant[1]) ||
        g_fighters[0].is_defeated || g_fighters[1].is_defeated ||
        (g_fighters[0].frames_alive < PORT_TOURNAMENT_JUDGE_FRAMES &&
         g_fighters[1].frames_alive < PORT_TOURNAMENT_JUDGE_FRAMES)) {
        return FALSE;
    }
    first = port_TournamentWounds(&g_fighters[0]);
    second = port_TournamentWounds(&g_fighters[1]);
    winner = first < second   ? 0
             : second < first ? 1
                              : (s32)port_TournamentDie(tournament, 2);
    tournament->judged |= (u16)(1 << tournament->match);
    port_TournamentDecide(tournament, winner);
    /* out of the fight to the main menu, as the pause's Return to Main
     * Menu goes (main.c) */
    g_sys_screen = SCREEN_MENU_INIT;
    cdrom_StopAndWait();
    return TRUE;
}

/* The tournament over or left: nothing of it is the game's any more, and
 * the select screen's rule is the window's again (port_match.h). */
void port_TournamentEnd(void) {
    PortTournament *tournament = &g_port_tournament;

    if (tournament->phase == PORT_TOURNAMENT_OFF) {
        return;
    }
    tournament->phase = PORT_TOURNAMENT_OFF;
    tournament->ask_bracket = FALSE;
    port_MatchForgetPage();
}

void port_TournamentModeChosen(void) {
    if (g_port_tournament.phase == PORT_TOURNAMENT_BETWEEN ||
        g_port_tournament.phase == PORT_TOURNAMENT_DONE) {
        port_TournamentEnd();
    }
}

/* A tournament begun (tournament_ask.c, at the host's boundary): the
 * seeds, the bracket, the bracket shown at the main menu. */
void port_TournamentBegin(const PortTournamentSetup *setup) {
    PortTournament *tournament = &g_port_tournament;
    u8 seeds[PORT_BRACKET_MOST];
    u16 number = (u16)(tournament->number + 1);
    u16 taken[PORT_TOURNAMENT_ASKS];
    u32 i;

    for (i = 0; i < PORT_TOURNAMENT_ASKS; i++) {
        taken[i] = tournament->taken[i];
    }
    for (i = 0; i < sizeof(*tournament); i++) {
        ((u8 *)tournament)[i] = 0;
    }
    for (i = 0; i < PORT_TOURNAMENT_ASKS; i++) {
        tournament->taken[i] = taken[i];
    }
    tournament->setup = *setup;
    tournament->number = number;
    tournament->dice = setup->seed;
    port_BracketShuffle(seeds, setup->count, setup->seed);
    if (setup->seeding == PORT_TOURNAMENT_SEED_ORDER) {
        for (i = 0; i < setup->count; i++) {
            seeds[i] = (u8)i;
        }
    }
    if (!port_BracketBuild(
            &tournament->bracket, setup->count, setup->is_double, seeds)) {
        return;
    }
    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        tournament->last_fighter[i] = PORT_TOURNAMENT_RANDOM;
        tournament->last_weapon[i] = PORT_TOURNAMENT_RANDOM;
    }
    tournament->phase = port_BracketIsDone(&tournament->bracket)
                            ? PORT_TOURNAMENT_DONE
                            : PORT_TOURNAMENT_BETWEEN;
    tournament->ask_bracket = TRUE;
}

/* The window's asks for this input taken in, each once: a tournament
 * left, one begun, the next match played. */
static void port_TournamentTakeAsks(PortTournament *tournament) {
    s32 kind;

    for (kind = 0; kind < PORT_TOURNAMENT_ASKS; kind++) {
        u16 count;

        if (!port_TournamentAsked(kind, &count) ||
            count == tournament->taken[kind]) {
            continue;
        }
        tournament->taken[kind] = count;
        if (kind == PORT_TOURNAMENT_ASK_LEAVE) {
            port_TournamentEnd();
        } else if (kind == PORT_TOURNAMENT_ASK_BEGIN) {
            port_TournamentBegin(port_TournamentAskedSetup());
        } else if (tournament->phase == PORT_TOURNAMENT_BETWEEN) {
            tournament->phase = PORT_TOURNAMENT_STARTING;
        }
    }
}

/* The input the game takes (port_tournament.h): the window's asks for it
 * taken in, then each side's pad its entrant's, the CPU's none; with two
 * CPUs, any pad's Start on the first. */
void port_TournamentTakeInput(struct PortInput *input) {
    PortTournament *tournament = &g_port_tournament;
    u16 buttons[PORT_INPUT_PADS];
    u16 start = 0;
    s32 people = 0;
    s32 pad;
    s32 side;

    if (port_ReplayIsPlaying()) {
        /* (a replay's input was taken as it was given to the sides) */
        return;
    }
    port_TournamentTakeAsks(tournament);
    if (tournament->phase != PORT_TOURNAMENT_PLAYING) {
        return;
    }
    for (pad = 0; pad < PORT_INPUT_PADS; pad++) {
        buttons[pad] = input->buttons[pad];
        start |= (u16)(buttons[pad] & PADstart);
        input->buttons[pad] = 0;
    }
    for (side = 0; side < 2; side++) {
        s32 entrant = tournament->side_entrant[side];

        if (!port_TournamentIsCpu(tournament, entrant)) {
            input->buttons[side] =
                buttons[tournament->setup.entrant[entrant].pad];
            people++;
        }
    }
    if (people == 0) {
        /* two CPUs: any pad's Start pauses their fight (and its Return to
         * Main Menu leaves the match unplayed) */
        input->buttons[0] = start;
    }
}
