/* The match rules for one against one (ROADMAP 21; port_match.h;
 * docs/PORT.md, "Match rules"). A state file: the rule the stage pages
 * hold and the match's score are saved, put back and hashed with the
 * game's own variables, so a rollback puts them back with the fight and
 * the two machines of a session keep the same score. Nothing here draws:
 * the window lays the panel's column, the lives or the score and the
 * cards over the picture from what this file keeps
 * (src/port/window/match_page.c). */
#include "common.h"
#include "bb2.h"

PortMatch g_port_match;

s32 port_MatchModeHasRules(s32 mode) {
    return mode == FIGHT_MODE_VS || mode == FIGHT_MODE_NETWORK ||
           mode == FIGHT_MODE_VSCPU;
}

static s32 port_MatchCount(s32 count) {
    return count < PORT_MATCH_COUNT_LEAST  ? PORT_MATCH_COUNT_LEAST
           : count > PORT_MATCH_COUNT_MOST ? PORT_MATCH_COUNT_MOST
                                           : count;
}

/* A crew's size: two to eight, and no more than the fighters the select
 * screen offers (every pick of a crew is a different character). */
static s32 port_MatchCrewSize(s32 count) {
    s32 most = g_port_match.crew_most;

    if (most < PORT_MATCH_CREW_LEAST || most > PORT_MATCH_CREW_MOST) {
        most = PORT_MATCH_CREW_MOST;
    }
    return count < PORT_MATCH_CREW_LEAST ? PORT_MATCH_CREW_LEAST
           : count > most                ? most
                                         : count;
}

/* The rules whose fight's winner keeps his wounds into the next: Stock
 * and Crew (the owner, 2026-11-20); First to N's fights start unhurt. */
static s32 port_MatchKeepsWounds(s32 rule) {
    /* (not two on two's, whose four start every fight unhurt) */
    return (rule == PORT_MATCH_STOCK || rule == PORT_MATCH_CREW) &&
           g_port_match.teams == PORT_TEAMS_ONE;
}

/* Made the first time the game asks, and the page reset when the
 * window's setting has changed (port_SetMatchDefault). */
static PortMatch *port_Match(void) {
    PortMatch *match = &g_port_match;
    s32 rule = port_GetMatchRule();
    s32 count = port_MatchCount(port_GetMatchCount());
    s32 teams = port_GetTeamsDefault();

    if (!match->is_started) {
        match->is_started = TRUE;
        match->rule = PORT_MATCH_SINGLE;
        match->last = PORT_MATCH_NOT_YET;
        match->default_rule = (u8)(rule + 1); /* reset below */
    }
    if (match->default_rule != rule || match->default_count != count) {
        match->default_rule = (u8)rule;
        match->default_count = (u8)count;
        match->page_rule = (u8)rule;
        /* the setting's number is the crew's size when its rule is Crew;
         * otherwise Crew starts at five, Stock and First to at it */
        match->page_count =
            (u8)(rule == PORT_MATCH_CREW ? PORT_MATCH_COUNT_DEFAULT : count);
        match->page_crew =
            (u8)(rule == PORT_MATCH_CREW ? port_MatchCrewSize(count)
                                         : PORT_MATCH_CREW_DEFAULT);
    }
    if (match->default_teams != teams + 1) {
        /* (kept one more than the choice, so that the first ask sets it) */
        match->default_teams = (u8)(teams + 1);
        match->page_teams = (u8)teams;
    }
    return match;
}

/* A side's fighter in play into the fight's configuration: the crew's
 * place after those lost. */
static void port_MatchCrewPut(PortMatch *match, s32 side) {
    s32 place = match->count - match->lives[side];
    const PortCrewPick *pick;

    if (place < 0 || place >= PORT_MATCH_CREW_MOST) {
        return;
    }
    pick = &match->crew[side][place];
    g_fight_config.roster[side] =
        (s8)g_fighter_character_rosters[pick->fighter];
    g_fight_config.weapon[side] = (s8)pick->weapon;
}

/* The score back to the match's start: N lives (a crew's fighters), no
 * wins; a crew's first fighters in play. */
static void port_MatchRestart(PortMatch *match) {
    match->lives[0] = match->lives[1] = match->count;
    match->wins[0] = match->wins[1] = 0;
    match->fights = 0;
    match->last = PORT_MATCH_NOT_YET;
    match->over = FALSE;
    match->winner = 0;
    match->card = PORT_MATCH_CARD_NONE;
    match->card_frames = 0;
    match->wounds_kept[0] = match->wounds_kept[1] = FALSE;
    if (match->rule == PORT_MATCH_CREW) {
        port_MatchCrewPut(match, 0);
        port_MatchCrewPut(match, 1);
    }
}

/* A Crew match begins: the crews picked on the select screen; each place
 * not picked (a mode that picked none, or the CPU's crew before its
 * opponent is chosen) the fighter and weapon the menus started with. */
static void port_MatchCrewBegin(PortMatch *match) {
    s32 side;
    s32 place;

    match->count = (u8)port_MatchCrewSize(match->page_crew);
    if (match->crew_size != match->count) {
        match->crew_picked = 0;
    }
    for (side = 0; side < 2; side++) {
        s32 picked = side < match->crew_sides ? match->crew_picked : 0;

        for (place = picked; place < PORT_MATCH_CREW_MOST; place++) {
            match->crew[side][place].fighter =
                g_fighter_roster_characters[(u8)g_fight_config.roster[side]];
            match->crew[side][place].weapon = (u8)g_fight_config.weapon[side];
        }
    }
    match->crew_size = match->count;
    match->crew_picked = match->count;
    match->crew_grid = FALSE;
    /* kept for the stage pages after the match */
    match->crew_kept = TRUE;
}

/* A mode begun from the menus (port_match.h): its match, of the stage
 * pages' rule, from the start. */
void port_MatchBegin(s32 mode) {
    PortMatch *match = port_Match();

    match->to_stages = FALSE;
    match->to_top = FALSE;
    match->mode = (u8)mode;
    match->rule =
        port_MatchModeHasRules(mode) ? match->page_rule : (u8)PORT_MATCH_SINGLE;
    match->count = (u8)port_MatchCount(match->page_count);
    /* two on two with a mode that has matches, and not with Crew, whose
     * fighters change between fights (port_duo.h) */
    match->teams =
        (u8)(port_MatchModeHasRules(mode) && match->rule != PORT_MATCH_CREW
                 ? match->page_teams
                 : PORT_TEAMS_ONE);
    if (match->rule == PORT_MATCH_CREW) {
        port_MatchCrewBegin(match);
    }
    port_MatchRestart(match);
}

/* The most a wound's count is kept at: what an s8 holds. */
#define WOUND_MOST 0x7F

/* A value of a wound as the match keeps it. */
static s8 port_MatchWound(s32 value) {
    return (s8)(value < 0 ? 0 : value > WOUND_MOST ? WOUND_MOST : value);
}

/* The fight just decided (PortMatch.last): under Stock and Crew its
 * winner's wounds, taken as the fight ended (before the winner's view,
 * which gives the winner his off arm back for the pose), are kept for the
 * next fight; the loser comes back unhurt (Stock) or is out (Crew), and a
 * draw keeps nobody's. The counts are the game's own, and no count kills
 * (a kill is a blow's): a fighter who starts wounded is slowed, weakened
 * or one-armed as he was, never down. */
static void port_MatchKeepWounds(PortMatch *match) {
    s32 side;

    for (side = 0; side < 2; side++) {
        const Fighter *fighter = &g_fighters[side];
        s8 *wounds = match->wounds[side];

        match->wounds_kept[side] =
            (u8)(port_MatchKeepsWounds(match->rule) && !match->over &&
                 match->last ==
                     (side == 0 ? PORT_MATCH_WON_P1 : PORT_MATCH_WON_P2));
        if (!match->wounds_kept[side]) {
            memset(wounds, 0, PORT_WOUNDS);
            continue;
        }
        wounds[PORT_WOUND_OFF_ARM_OK] = (s8)(fighter->off_arm_ok != 0);
        wounds[PORT_WOUND_LEGS] = port_MatchWound(fighter->leg_wounds);
        wounds[PORT_WOUND_ARM] = port_MatchWound(fighter->arm_wounds);
        wounds[PORT_WOUND_BODY] = port_MatchWound(fighter->body_wounds);
    }
}

/* A fighter set up for the next fight (port_match.h): the winner's
 * wounds put back over the game's healing. */
void port_MatchPutWounds(
    s32 index, s16 *off_arm_ok, s16 *legs, s16 *arm, s16 *body) {
    const PortMatch *match = &g_port_match;
    const s8 *wounds;

    if (index < 0 || index >= 2 || !match->wounds_kept[index] ||
        !port_MatchKeepsWounds(match->rule) || match->mode != g_fight_mode ||
        match->over) {
        return;
    }
    wounds = match->wounds[index];
    *off_arm_ok = wounds[PORT_WOUND_OFF_ARM_OK];
    *legs = wounds[PORT_WOUND_LEGS];
    *arm = wounds[PORT_WOUND_ARM];
    *body = wounds[PORT_WOUND_BODY];
}

/* A fight of the match decided (port_match.h): the score, and the card
 * that follows it. */
void port_MatchFightOver(s32 mode, s32 p1_fell, s32 p2_fell) {
    PortMatch *match = port_Match();
    s32 side;

    if (match->rule == PORT_MATCH_SINGLE || match->mode != mode ||
        match->over) {
        return;
    }
    match->fights++;
    if (p1_fell && p2_fell) {
        match->last = PORT_MATCH_DRAW;
    } else {
        match->last = p1_fell ? PORT_MATCH_WON_P2 : PORT_MATCH_WON_P1;
    }
    if (match->rule == PORT_MATCH_STOCK) {
        if (match->last == PORT_MATCH_DRAW) {
            /* a life from both, unless that would leave neither one: then
             * the next fight decides */
            if (match->lives[0] > 1 || match->lives[1] > 1) {
                match->lives[0]--;
                match->lives[1]--;
            }
        } else {
            match->lives[match->last == PORT_MATCH_WON_P1]--;
        }
        for (side = 0; side < 2; side++) {
            if (match->lives[side] == 0) {
                match->over = TRUE;
                match->winner = (u8)(side == 0);
            }
        }
    } else if (match->rule == PORT_MATCH_CREW) {
        /* the loser's fighter out, both on a draw; the match is over when
         * a crew has none left, drawn if both ran out at once */
        if (match->last != PORT_MATCH_WON_P2) {
            match->lives[1]--;
        }
        if (match->last != PORT_MATCH_WON_P1) {
            match->lives[0]--;
        }
        if (match->lives[0] == 0 || match->lives[1] == 0) {
            match->over = TRUE;
            match->winner = match->lives[0] == match->lives[1]
                                ? (u8)PORT_MATCH_DRAWN
                                : (u8)(match->lives[0] == 0);
        }
    } else if (match->last != PORT_MATCH_DRAW) {
        /* First to N: a draw scores nothing */
        side = match->last;
        if (++match->wins[side] >= match->count) {
            match->over = TRUE;
            match->winner = (u8)side;
        }
    }
    port_MatchKeepWounds(match);
    match->card = match->over ? PORT_MATCH_CARD_RESULT : PORT_MATCH_CARD_SCORE;
    match->card_frames = 0;
}

/* Versus the CPU's win (port_match.h): the game's next opponent, or the
 * match goes on. */
s32 port_MatchNextOpponent(void) {
    PortMatch *match = port_Match();

    if (match->rule == PORT_MATCH_SINGLE) {
        return TRUE;
    }
    if (match->over && match->winner == 0 && match->rule != PORT_MATCH_CREW) {
        /* (a Crew match won is the run's end: its card, then the top
         * menu) */
        /* the next opponent's match */
        port_MatchRestart(match);
        return TRUE;
    }
    return FALSE;
}

/* The next fight of the match, as the game's Rematch starts it: where the
 * fighters are, the stage and fighters as they were. Not a continue: the
 * CPU is not made to forget, nothing is counted. */
static void port_MatchNextFight(void) {
    if (g_sys_screen == SCREEN_AFTER_MATCH_UPDATE) {
        cdrom_StopAndWait();
    }
    g_fighters[g_fight_winner == 0].face = MODEL_FACE_NORMAL;
    g_fight_restart = 1;
    g_fight_restart_pair =
        fight_FindNearestStartPair(&g_fighters[g_fight_winner].world_pos);
    g_sys_screen = SCREEN_FIGHT_INIT;
}

/* The next fight of a Crew match: each crew's fighter in play, loaded
 * afresh as any fight starts (the winner too), on the same stage. */
static void port_MatchNextCrewFight(PortMatch *match) {
    cdrom_StopAndWait();
    snd_EndFight();
    port_MatchCrewPut(match, 0);
    port_MatchCrewPut(match, 1);
    g_fight_restart = 0;
    /* no loading picture, the screen black (fight_Init blanks it): the
     * screen after a fight has drawn over the pictures' place in video
     * memory */
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    g_sys_screen = SCREEN_FIGHT_INIT;
}

/* A frame after a fight of a match (port_match.h): the card, then the
 * next fight, the stage select, versus the CPU's next opponent, or the
 * game's own menu. */
s32 port_MatchAfterFight(s32 pressed) {
    PortMatch *match = port_Match();
    s32 frames;

    if (match->rule == PORT_MATCH_SINGLE ||
        match->card == PORT_MATCH_CARD_NONE) {
        return FALSE;
    }
    frames = match->card == PORT_MATCH_CARD_RESULT ? PORT_MATCH_RESULT_FRAMES
                                                   : PORT_MATCH_SCORE_FRAMES;
    match->card_frames++;
    if (match->card_frames < frames &&
        (match->card_frames < PORT_MATCH_CARD_LEAST ||
         !(pressed & PAD_ANY(PADRdown)))) {
        return TRUE;
    }
    match->card = PORT_MATCH_CARD_NONE;
    if (!match->over) {
        if (match->rule == PORT_MATCH_CREW) {
            port_MatchNextCrewFight(match);
        } else {
            port_MatchNextFight();
        }
        return TRUE;
    }
    if (match->rule == PORT_MATCH_CREW) {
        /* a Crew match is the whole session: back to the top menu, in
         * versus, online and versus the CPU, won or lost (the owner,
         * 2026-11-20); a session of two ends there (session.c), a room's
         * ended as the match was decided */
        match->to_top = TRUE;
        port_MatchCrewDrop();
        cdrom_StopAndWait();
        snd_EndFight();
        g_disp_loading_screen = LOADING_SCREEN_NONE;
        g_menu_reopen_mode = 0;
        g_sys_screen = SCREEN_MENU_INIT;
        return TRUE;
    }
    if (match->mode == FIGHT_MODE_VSCPU) {
        if (match->winner == 0) {
            /* won by a draw the opponent fell second in: on to the next */
            port_MatchRestart(match);
            cdrom_StopAndWait();
            snd_EndFight();
            g_disp_loading_screen = LOADING_SCREEN_NEXT_OPPONENT;
            fight_NextVsCpuOpponent();
            return TRUE;
        }
        /* lost: the game's own menu, whose Rematch is a new match */
        port_MatchRestart(match);
        return FALSE;
    }
    /* versus and link: the stage select, the fighters kept */
    match->to_stages = TRUE;
    cdrom_StopAndWait();
    snd_EndFight();
    g_menu_reopen_mode = 1;
    g_sys_screen = SCREEN_MENU_INIT;
    return TRUE;
}

/* The game's wins counter of a fight of mode `mode` (port_match.h): left
 * out while a match is in play, whose strip shows the score. Reads the
 * state alone, as the window's port_MatchShown does. */
s32 port_MatchHidesWins(s32 mode) {
    const PortMatch *match = &g_port_match;

    return match->rule != PORT_MATCH_SINGLE && match->mode == mode &&
           port_MatchModeHasRules(mode);
}

/* The select screen loads (port_match.h): back to the stage pages after
 * a match of versus or link, the crews kept; otherwise begun afresh. */
s32 port_MatchToStages(void) {
    PortMatch *match = port_Match();
    s32 to_stages = match->to_stages;

    match->to_stages = FALSE;
    to_stages = to_stages && (match->mode == FIGHT_MODE_VS ||
                              match->mode == FIGHT_MODE_NETWORK);
    if (!to_stages) {
        /* a select screen begun afresh picks its crews afresh */
        port_MatchCrewDrop();
    }
    return to_stages;
}

/* A frame of a stage page (port_match.h): Circle the rule, Start its
 * number, one to nine and round again. */
s32 port_MatchPageStep(s32 mode, s32 pressed, s32 is_open) {
    PortMatch *match = port_Match();

    match->page_on = (u8)port_MatchModeHasRules(mode);
    if (!match->page_on || !is_open || match->rule_first) {
        /* (a rule chosen before the grid is shown, not changed: the crews
         * picked are of it) */
        return FALSE;
    }
    if (pressed & PAD_ANY(PADRright)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        match->page_rule = (u8)((match->page_rule + 1) % PORT_MATCH_RULES);
        return TRUE;
    }
    if ((pressed & PAD_ANY(PADstart)) && match->page_rule == PORT_MATCH_CREW) {
        /* a crew of two to eight, and round again */
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        match->page_crew =
            (u8)(match->page_crew >= port_MatchCrewSize(PORT_MATCH_CREW_MOST)
                     ? PORT_MATCH_CREW_LEAST
                     : port_MatchCrewSize(match->page_crew + 1));
        return TRUE;
    }
    if ((pressed & PAD_ANY(PADstart)) &&
        match->page_rule != PORT_MATCH_SINGLE) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        match->page_count = (u8)(match->page_count % PORT_MATCH_COUNT_MOST +
                                 PORT_MATCH_COUNT_LEAST);
        return TRUE;
    }
    return FALSE;
}

/* ---- Crew: the picks (port_match.h) ---- */

s32 port_MatchCrewWanted(s32 mode, s32 sides) {
    PortMatch *match = port_Match();

    if (!port_MatchModeHasRules(mode) || match->page_rule != PORT_MATCH_CREW) {
        return FALSE;
    }
    if (match->crew_picked < match->crew_size &&
        (match->crew_picked > 0 || match->rule_first)) {
        /* a later place being picked (or, the rule chosen before the
         * grid, the first) */
        return TRUE;
    }
    if (match->rule_first && match->crew_size != 0 &&
        match->crew_picked >= match->crew_size) {
        /* the crews picked before the stage page: its stage is chosen */
        return FALSE;
    }
    if (match->crew_kept && match->crew_picked == match->crew_size &&
        match->crew_size == port_MatchCrewSize(match->page_crew) &&
        match->crew_sides == sides) {
        /* the crews of the match before, at the page's size */
        return FALSE;
    }
    /* the first place: the picks the screen has just made */
    match->crew_size = (u8)port_MatchCrewSize(match->page_crew);
    match->crew_sides = (u8)sides;
    match->crew_picked = 0;
    match->crew_kept = FALSE;
    return TRUE;
}

/* A side's pick into the place being picked; the last side's takes the
 * place, and the grid is due for the next. */
void port_MatchCrewTake(s32 side, s32 fighter, s32 weapon) {
    PortMatch *match = port_Match();

    if (side < 0 || side >= 2 || match->crew_picked >= match->crew_size) {
        return;
    }
    match->crew[side][match->crew_picked].fighter = (u8)fighter;
    match->crew[side][match->crew_picked].weapon = (u8)weapon;
    if (side == match->crew_sides - 1) {
        /* the place is taken: the grid comes up for the next */
        match->crew_picked++;
        match->crew_grid = match->crew_picked < match->crew_size;
    }
}

s32 port_MatchCrewDone(void) {
    PortMatch *match = port_Match();

    return match->crew_size != 0 && match->crew_picked >= match->crew_size;
}

s32 port_MatchCrewPicking(void) {
    PortMatch *match = port_Match();

    return match->page_rule == PORT_MATCH_CREW && match->crew_picked > 0 &&
           match->crew_picked < match->crew_size;
}

s32 port_MatchCrewGridDue(void) {
    PortMatch *match = port_Match();
    s32 due = match->crew_grid;

    match->crew_grid = FALSE;
    return due;
}

s32 port_MatchCrewFirst(s32 side, s32 *fighter, s32 *weapon) {
    PortMatch *match = port_Match();

    if (match->crew_picked == 0 || side < 0 || side >= match->crew_sides) {
        return FALSE;
    }
    *fighter = match->crew[side][0].fighter;
    *weapon = match->crew[side][0].weapon;
    return TRUE;
}

void port_MatchCrewDrop(void) {
    PortMatch *match = port_Match();

    match->crew_picked = 0;
    match->crew_size = 0;
    match->crew_grid = FALSE;
    match->crew_kept = FALSE;
}

/* ---- Crew: versus the CPU ---- */

/* The dice's sequence: a 16-bit linear congruential one (its multiplier
 * and increment), and what spreads the frame count over the seed. */
#define CREW_DIE_MULTIPLY 0x6255U
#define CREW_DIE_ADD 0x3619U
#define CREW_SEED_SPREAD 0x9E37U

/* The CPU's dice, `sides` of them: a sequence of its own, so that the
 * game's rand() is drawn on as it would be without a crew. */
static s32 port_MatchCrewDie(PortMatch *match, s32 sides) {
    match->crew_seed =
        (u16)(match->crew_seed * CREW_DIE_MULTIPLY + CREW_DIE_ADD);
    return (s32)((u32)match->crew_seed * (u32)sides >> 16);
}

/* A weapon for a fighter of the CPU's as the game gives its opponents
 * one: the gunners their guns, the others any of the six blades, the
 * second clan the yari for the naginata. */
static s32 port_MatchCrewWeapon(PortMatch *match, s32 character) {
    s32 weapon;

    if (character == CHARACTER_TSUBAME) {
        return WEAPON_ID_LONG_GUN;
    }
    if (character == CHARACTER_KATZE) {
        return WEAPON_ID_SHORT_GUN;
    }
    weapon = port_MatchCrewDie(match, WEAPON_ID_FIRST_GUN);
    if (weapon == WEAPON_ID_NAGINATA && g_fighter_clans[character] != 0) {
        weapon = WEAPON_ID_YARI;
    }
    return weapon;
}

/* `pool` of `characters` with `character` taken out: how many are left. */
static s32 port_MatchCrewPoolLess(u8 *characters, s32 pool, s32 character) {
    s32 i;

    for (i = 0; i < pool;) {
        if (characters[i] == character) {
            characters[i] = characters[--pool];
        } else {
            i++;
        }
    }
    return pool;
}

/* The pool drawn dry before the CPU's crew is whole: every named
 * character a gun or a blade can be given (not the clans' last two, nor
 * the roster's nameless) that the crew's places before `place` do not
 * have. */
static s32 port_MatchCrewPoolRefill(
    PortMatch *match, u8 *characters, s32 place) {
    s32 pool = 0;
    s32 character;
    s32 i;

    for (character = 0; character < CHARACTER_LAST_1; character++) {
        if (character == CHARACTER_VANISHER ||
            character == CHARACTER_SIDEARM_1 ||
            character == CHARACTER_ARMORED ||
            character == CHARACTER_SIDEARM_2) {
            continue;
        }
        for (i = 0; i < place; i++) {
            if (match->crew[1][i].fighter == character) {
                break;
            }
        }
        if (i == place) {
            characters[pool++] = (u8)character;
        }
    }
    return pool;
}

/* Versus the CPU's opponent chosen (port_match.h): he leads the CPU's
 * crew, the rest drawn from the run's opponents (its last ones left out),
 * and the opponent's match begins with the player's crew whole. */
void port_MatchOpponentChosen(void) {
    PortMatch *match = port_Match();
    u8 characters[sizeof(g_vscpu_order)];
    s32 pool = 0;
    s32 place;
    s32 i;

    if (match->rule != PORT_MATCH_CREW || match->mode != FIGHT_MODE_VSCPU) {
        return;
    }
    for (i = 0; i < g_vscpu_count && i < (s32)sizeof(g_vscpu_order); i++) {
        if (g_vscpu_order[i] != CHARACTER_LAST_1 &&
            g_vscpu_order[i] != CHARACTER_LAST_2) {
            characters[pool++] = g_vscpu_order[i];
        }
    }
    match->crew_seed =
        (u16)((u32)g_disp_frame_count * CREW_SEED_SPREAD + (u32)g_vscpu_faced);
    match->crew[1][0].fighter =
        g_fighter_roster_characters[(u8)g_fight_config.roster[1]];
    match->crew[1][0].weapon = (u8)g_fight_config.weapon[1];
    /* every place a character of its own: the leader out of the pool,
     * each drawn taken out of it (the pool is the run's opponents; should
     * it run dry, any of the named characters not yet in the crew) */
    pool = port_MatchCrewPoolLess(characters, pool, match->crew[1][0].fighter);
    for (place = 1; place < PORT_MATCH_CREW_MOST; place++) {
        s32 character;

        if (pool == 0) {
            pool = port_MatchCrewPoolRefill(match, characters, place);
        }
        if (pool > 0) {
            s32 drawn = port_MatchCrewDie(match, pool);

            character = characters[drawn];
            characters[drawn] = characters[--pool];
        } else {
            character = match->crew[1][0].fighter;
        }
        match->crew[1][place].fighter = (u8)character;
        match->crew[1][place].weapon =
            (u8)port_MatchCrewWeapon(match, character);
    }
    match->lives[0] = match->lives[1] = match->count;
    port_MatchCrewPut(match, 0);
}

/* The game's Rematch (port_match.h): a Crew match begun again loads its
 * first fighters, behind a black screen as the next fight of a match. */
s32 port_MatchRematchLoads(void) {
    PortMatch *match = port_Match();

    if (match->rule != PORT_MATCH_CREW || match->mode != g_fight_mode) {
        return FALSE;
    }
    cdrom_StopAndWait();
    snd_EndFight();
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    return TRUE;
}

/* ---- the rule page, before the first select (port_match.h) ---- */

/* The rule taken before the grid: a Crew's places to be picked from the
 * first, each as its weapons cross. */
static void port_MatchRuleTaken(PortMatch *match) {
    port_MatchCrewDrop();
    if (match->page_rule == PORT_MATCH_CREW) {
        match->crew_size = (u8)port_MatchCrewSize(match->page_crew);
        match->crew_sides = match->rule_sides;
        match->crew_picked = 0;
    }
}

/* The select screen loads (port_match.h): the crew's most, whether the
 * rule comes first, and the page up if it is to be shown. */
void port_MatchRulePageOpen(s32 mode, s32 to_stages, s32 open, s32 sides) {
    PortMatch *match = port_Match();
    s32 room = port_SessionIsRoomMatch();

    match->crew_most = (u8)(open < PORT_MATCH_CREW_LEAST ? PORT_MATCH_CREW_LEAST
                            : open > PORT_MATCH_CREW_MOST ? PORT_MATCH_CREW_MOST
                                                          : open);
    match->page_crew = (u8)port_MatchCrewSize(match->page_crew);
    match->rule_first =
        (u8)(port_MatchModeHasRules(mode) && (port_TopMenuIsOn() || room));
    /* a room's rule is the room's, as its host set it: no page */
    match->rule_page = (u8)(match->rule_first && !room && !to_stages);
    match->rule_row = PORT_MATCH_ROW_RULE;
    match->rule_sides = (u8)sides;
    if (match->rule_first && room && !to_stages) {
        port_MatchRuleTaken(match);
    }
}

s32 port_MatchRuleFirst(void) { return g_port_match.rule_first; }

s32 port_MatchRuleHasRow(const PortMatch *match, s32 row) {
    if (row == PORT_MATCH_ROW_COUNT) {
        return match->page_rule != PORT_MATCH_SINGLE;
    }
    if (row == PORT_MATCH_ROW_TEAMS) {
        return match->page_rule != PORT_MATCH_CREW;
    }
    return row == PORT_MATCH_ROW_RULE;
}

/* The page's next row that it has, `step` 1 down or -1 up, round at the
 * ends. */
static void port_MatchRuleRowStep(PortMatch *match, s32 step) {
    s32 row = match->rule_row;
    s32 i;

    for (i = 0; i < PORT_MATCH_ROWS; i++) {
        row = (row + PORT_MATCH_ROWS + step) % PORT_MATCH_ROWS;
        if (port_MatchRuleHasRow(match, row)) {
            break;
        }
    }
    match->rule_row = (u8)row;
}

/* The value of the page's row a step on (`step` 1 or -1), round at the
 * ends. */
static void port_MatchRuleTurn(PortMatch *match, s32 step) {
    s32 least;
    s32 most;
    s32 value;

    if (match->rule_row == PORT_MATCH_ROW_TEAMS) {
        match->page_teams =
            (u8)((match->page_teams + PORT_TEAMS_CHOICES + step) %
                 PORT_TEAMS_CHOICES);
        return;
    }
    if (match->rule_row == PORT_MATCH_ROW_RULE) {
        match->page_rule = (u8)((match->page_rule + PORT_MATCH_RULES + step) %
                                PORT_MATCH_RULES);
        return;
    }
    if (match->page_rule == PORT_MATCH_CREW) {
        least = PORT_MATCH_CREW_LEAST;
        most = port_MatchCrewSize(PORT_MATCH_CREW_MOST);
        value = match->page_crew + step;
        match->page_crew = (u8)(value < least  ? most
                                : value > most ? least
                                               : value);
        return;
    }
    least = PORT_MATCH_COUNT_LEAST;
    most = PORT_MATCH_COUNT_MOST;
    value = match->page_count + step;
    match->page_count = (u8)(value < least  ? most
                             : value > most ? least
                                            : value);
}

/* A frame of the rule page (port_match.h): Triangle leaves, Cross takes
 * the rule, Up and Down the row, Left and Right its value. */
s32 port_MatchRulePageStep(s32 pressed, s32 *leave) {
    PortMatch *match = port_Match();
    /* in a session the host's pad chooses; Triangle is either player's,
     * as on the grid */
    s32 own = port_SessionIsOn() ? pressed & PAD_OF(0xFFFF, 0) : pressed;

    if (!match->rule_page) {
        return FALSE;
    }
    if (pressed & PAD_ANY(PADRup)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        *leave = TRUE;
        return TRUE;
    }
    if (own & PAD_ANY(PADRdown)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        match->rule_page = FALSE;
        port_MatchRuleTaken(match);
        return TRUE;
    }
    if (own & PAD_ANY(PADLup | PADLdown)) {
        u8 row = match->rule_row;

        port_MatchRuleRowStep(match, (own & PAD_ANY(PADLdown)) ? 1 : -1);
        if (match->rule_row != row) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
    } else if (own & PAD_ANY(PADLleft | PADLright)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        port_MatchRuleTurn(match, (own & PAD_ANY(PADLright)) ? 1 : -1);
        if (!port_MatchRuleHasRow(match, match->rule_row)) {
            match->rule_row = PORT_MATCH_ROW_RULE;
        }
    }
    return TRUE;
}

/* ---- Crew: a different character each place, and going back ---- */

s32 port_MatchCrewHas(s32 side, s32 fighter) {
    const PortMatch *match = &g_port_match;
    s32 place;

    if (side < 0 || side >= 2 || match->page_rule != PORT_MATCH_CREW ||
        match->crew_size == 0) {
        return FALSE;
    }
    for (place = 0; place < match->crew_picked && place < match->crew_size;
         place++) {
        if (match->crew[side][place].fighter == fighter) {
            return TRUE;
        }
    }
    return FALSE;
}

s32 port_MatchCrewBackOne(void) {
    PortMatch *match = port_Match();

    if (!match->rule_first || !port_MatchCrewPicking()) {
        return FALSE;
    }
    match->crew_picked--;
    match->crew_grid = TRUE;
    return TRUE;
}

/* Triangle on a stage page (port_match.h): a rule-first crew's last place
 * to be picked again at its weapons; otherwise the crews let go. */
void port_MatchCrewStageBack(void) {
    PortMatch *match = port_Match();

    if (match->rule_first && match->page_rule == PORT_MATCH_CREW &&
        match->crew_size != 0 && match->crew_picked >= match->crew_size &&
        !match->crew_kept) {
        /* the weapons of the last place again: its crossing takes it */
        match->crew_picked--;
        return;
    }
    port_MatchCrewDrop();
}

s32 port_MatchEndsSession(void) { return g_port_match.to_top; }
