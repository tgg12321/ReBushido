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

/* Made the first time the game asks, and the page reset when the
 * window's setting has changed (port_SetMatchDefault). */
static PortMatch *port_Match(void) {
    PortMatch *match = &g_port_match;
    s32 rule = port_GetMatchRule();
    s32 count = port_MatchCount(port_GetMatchCount());

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
        match->page_count = (u8)count;
    }
    return match;
}

/* The score back to the match's start: N lives, no wins. */
static void port_MatchRestart(PortMatch *match) {
    match->lives[0] = match->lives[1] = match->count;
    match->wins[0] = match->wins[1] = 0;
    match->fights = 0;
    match->last = PORT_MATCH_NOT_YET;
    match->over = FALSE;
    match->winner = 0;
    match->card = PORT_MATCH_CARD_NONE;
    match->card_frames = 0;
}

void port_MatchBegin(s32 mode) {
    PortMatch *match = port_Match();

    match->to_stages = FALSE;
    match->mode = (u8)mode;
    match->rule =
        port_MatchModeHasRules(mode) ? match->page_rule : (u8)PORT_MATCH_SINGLE;
    match->count = (u8)port_MatchCount(match->page_count);
    port_MatchRestart(match);
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
    } else if (match->last != PORT_MATCH_DRAW) {
        /* First to N: a draw scores nothing */
        side = match->last;
        if (++match->wins[side] >= match->count) {
            match->over = TRUE;
            match->winner = (u8)side;
        }
    }
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
    if (match->over && match->winner == 0) {
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
        port_MatchNextFight();
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

s32 port_MatchToStages(void) {
    PortMatch *match = port_Match();
    s32 to_stages = match->to_stages;

    match->to_stages = FALSE;
    return to_stages &&
           (match->mode == FIGHT_MODE_VS || match->mode == FIGHT_MODE_NETWORK);
}

/* A frame of a stage page (port_match.h): Circle the rule, Start its
 * number, one to nine and round again. */
s32 port_MatchPageStep(s32 mode, s32 pressed, s32 is_open) {
    PortMatch *match = port_Match();

    match->page_on = (u8)port_MatchModeHasRules(mode);
    if (!match->page_on || !is_open) {
        return FALSE;
    }
    if (pressed & PAD_ANY(PADRright)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        match->page_rule = (u8)((match->page_rule + 1) % PORT_MATCH_RULES);
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
