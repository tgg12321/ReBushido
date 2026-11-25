#ifndef PORT_MATCH_H
#define PORT_MATCH_H

/* The native port's match rules for one against one (ROADMAP 21;
 * docs/PORT.md, "Match rules"; docs/PLAYING.md, "Match rules"). Beside the
 * game's own single fight a match may be:
 *
 * - Stock N: each side has N lives; a fight's loser loses one; the next
 *   fight is on the same stage with the same fighters, each side keeping
 *   its lives, until one side has none;
 * - First to N: fights until one side has won N;
 * - Crew N: each side picks a crew of N fighters and weapons, in order,
 *   before the match (the select screen's grid and weapons once a place,
 *   after the stage page); each fight is the two crews' fighters in play,
 *   the loser's is out and the next of its crew comes in, the winner
 *   stays, loaded afresh as any fight starts, with the wounds he had at
 *   the fight's end; a draw takes one from both.
 *   The match ends when a crew has none left, drawn when both run out at
 *   once.
 *
 * Inside a fight the game's own kill, replay and winner's view run. A
 * fight both fighters fall in is a draw: in Stock it takes a life from
 * both, except that a draw that would leave neither with a life takes
 * none (the next fight decides); in First to N it scores nothing. A
 * surrender (kaishaku) is the surrenderer's loss.
 *
 * Between fights the menu after a fight is not shown: a card of the score
 * is, for SCORE_FRAMES (Cross shortens it), and the next fight starts as
 * the game's Rematch starts it, where the fighters are. When the match is
 * decided a card of the result, then, in versus and online, the stage
 * select, the fighters and weapons kept (Triangle goes back to them, as
 * on any stage page). Versus the CPU plays a match against each opponent:
 * the player's match won goes on to the next as the game does after a
 * win, a match lost to the game's own menu after a defeat, whose Rematch
 * is a new match against the same opponent.
 *
 * With ReBushido's top menu the rule is chosen on a page of its own before
 * the first select's grid (Left and Right the value, Up and Down the row,
 * Cross), the crews then picked from the grid's first place, each a
 * character the crew does not have; the stage page shows the rule and
 * leaves it. With menu = original it is chosen on the stage pages (Circle
 * turns the rule, Start its number). Both start from the window's
 * match_rule and match_count and keep what the players left them at until
 * the setting changes; a mode with no stage page and no rule page plays
 * the setting's. Under Stock and Crew a fight's winner keeps his wounds
 * into the next fight; a Crew match's end goes back to the top menu. In a
 * session the host's setting holds (the session's welcome). What the players
 * chose and the match's score are the game's state (src/port/match.c is a state
 * file): rolled back with the game, and the same on both machines of a session.
 * The window draws the panel's column, the lives or the score over the fight
 * and the cards from it (src/port/window/match_page.c); none of it is in the
 * game's video memory. The PlayStation builds never see this header. */

#include "common.h"

/* The rules, in the order Circle turns them. */
enum {
    PORT_MATCH_SINGLE = 0,   /* the game's own one fight */
    PORT_MATCH_STOCK = 1,    /* lives */
    PORT_MATCH_FIRST_TO = 2, /* wins */
    PORT_MATCH_CREW = 3,     /* crews of fighters */
    PORT_MATCH_RULES = 4
};

#define PORT_MATCH_COUNT_LEAST 1
#define PORT_MATCH_COUNT_MOST 9
#define PORT_MATCH_COUNT_DEFAULT 3
/* A crew's size: the count of a Crew match. */
#define PORT_MATCH_CREW_LEAST 2
#define PORT_MATCH_CREW_MOST 8
#define PORT_MATCH_CREW_DEFAULT 5

/* What a fight of the match came to (PortMatch.last). */
enum {
    PORT_MATCH_WON_P1 = 0,
    PORT_MATCH_WON_P2 = 1,
    PORT_MATCH_DRAW = 2,
    PORT_MATCH_NOT_YET = 3
};

/* PortMatch.winner of a Crew match whose two crews ran out at once. */
#define PORT_MATCH_DRAWN 2

/* The card up after a fight (PortMatch.card). */
enum {
    PORT_MATCH_CARD_NONE = 0,
    PORT_MATCH_CARD_SCORE = 1,  /* between two fights */
    PORT_MATCH_CARD_RESULT = 2, /* the match decided */
};

/* How long the cards are up, in the game's frames (30 a second), and the
 * least of it before Cross takes a card down. */
#define PORT_MATCH_SCORE_FRAMES 75
#define PORT_MATCH_RESULT_FRAMES 120
#define PORT_MATCH_CARD_LEAST 20

/* A place of a crew: the fighter (enum CharacterId, as the select screen
 * picks it) and the weapon (enum WeaponId). */
typedef struct {
    u8 fighter;
    u8 weapon;
} PortCrewPick;

/* A fighter's wounds as the match keeps them (the Fighter's off_arm_ok,
 * leg_wounds, arm_wounds, body_wounds). */
enum {
    PORT_WOUND_OFF_ARM_OK = 0,
    PORT_WOUND_LEGS = 1,
    PORT_WOUND_ARM = 2,
    PORT_WOUND_BODY = 3,
    PORT_WOUNDS = 4
};

/* The rule page's rows. */
enum {
    PORT_MATCH_ROW_RULE = 0,
    PORT_MATCH_ROW_COUNT = 1, /* the number, or the crew's size */
    PORT_MATCH_ROW_TEAMS = 2, /* one on one or two on two (port_duo.h) */
    PORT_MATCH_ROWS = 3
};

typedef struct {
    u8 is_started;
    u8 page_rule;     /* the stage pages' rule (PORT_MATCH_...) */
    u8 page_count;    /* and its number */
    u8 default_rule;  /* what the page was last reset to */
    u8 default_count; /* ... */
    u8 rule;          /* the match in play, PORT_MATCH_SINGLE for none */
    u8 count;
    u8 lives[2];     /* Stock: each side's lives left */
    u8 wins[2];      /* First to N: each side's wins */
    u8 fights;       /* the match's fights decided */
    u8 last;         /* the last of them: PORT_MATCH_WON_..., DRAW */
    u8 over;         /* the match is decided */
    u8 winner;       /* its winner, 0 or 1 */
    u8 card;         /* PORT_MATCH_CARD_... */
    u8 to_stages;    /* the select screen opens on the stage pages */
    u8 mode;         /* the match's mode (enum FightMode) */
    u8 page_on;      /* the stage page this frame offers a match */
    u16 card_frames; /* frames the card has been up */
    /* Crew (PORT_MATCH_CREW). The two crews are crew[side][place], the
     * fighter of a side in play crew[side][count - lives[side]]; versus
     * the CPU the second crew is the CPU's. */
    u8 page_crew;   /* the stage pages' crew size */
    u8 crew_size;   /* the crews picked or being picked: their size */
    u8 crew_picked; /* places of the crews picked, the same for each side */
    u8 crew_sides;  /* the sides the select screen picks for: 1 or 2 */
    u8 crew_grid;   /* the grid is to be cleared for the next place */
    u8 crew_kept;   /* the crews of the match before, kept for the next */
    u16 crew_seed;  /* the CPU's crew's dice */
    PortCrewPick crew[2][PORT_MATCH_CREW_MOST];
    /* The rule chosen before the first select (ReBushido's top menu, or a
     * room of several's rule): the select screen's rule page is up
     * (rule_page) on its row (PORT_MATCH_ROW_...); the screen's rule was
     * chosen before its grid (rule_first), so a crew's places are taken
     * at the weapons' crossing and the stage page shows the rule alone. */
    u8 rule_page;
    u8 rule_row;
    u8 rule_first;
    u8 crew_most; /* the most a crew may have here: the fighters open */
    /* Stock and Crew: the wounds a fight's winner takes into the next
     * (PORT_WOUND_...), for the side whose wounds_kept is set */
    u8 wounds_kept[2];
    s8 wounds[2][PORT_WOUNDS];
    u8 to_top;     /* the match decided went back to the top menu */
    u8 rule_sides; /* the sides the rule page's select screen picks for */
    /* Two on two (port_duo.h): the rule page's choice (PORT_TEAMS_...),
     * what it was last reset to, and the match in play's */
    u8 page_teams;
    u8 default_teams;
    u8 teams;
    /* and each side's second of a character and weapon of his own: the
     * second place the select screen picked for him (crew[side][1], as
     * its Crew picks a crew's places), or versus the CPU the CPU's */
    u8 partner_picked[2];
    PortCrewPick partner[2];
    u32 pads_held; /* the third and fourth pads', at the select screen */
} PortMatch;

extern PortMatch g_port_match;

/* ---- what the game calls (under RB_PORT) ---- */

/* A mode is started from the menus (fight_start.c, fight_ApplyConfig): a
 * match begins with the rule chosen, if the mode is one with matches. */
extern void port_MatchBegin(s32 mode);
/* A fight of the mode is decided (fight.c, fight_AfterDefeat; fight_after.c
 * after a kaishaku): who fell. Counts it into the match in play. */
extern void port_MatchFightOver(s32 mode, s32 p1_fell, s32 p2_fell);
/* Versus the CPU, the first player won the fight (fight.c,
 * fight_WinnerViewUpdate): TRUE to go on to the next opponent as the game
 * does (no match, or the match won: the next opponent's begins), FALSE to
 * go to the screen after a fight, where the match goes on. */
extern s32 port_MatchNextOpponent(void);
/* A frame of the screens after a fight and after a kaishaku, before their
 * menu (fight.c, fight_AfterFightUpdate; fight_after.c,
 * fight_AfterMatchUpdate), `pressed` the pads' presses: the card, and what
 * follows it, done here. TRUE if the frame is the match's, and the menu is
 * not shown. */
extern s32 port_MatchAfterFight(s32 pressed);
/* A frame of a fight of mode `mode` (fight.c, fight_UpdateMode): TRUE when
 * a match is in play, whose strip over the fight shows the score, and the
 * game's own "WINS 00" counter is left out. The game's one fight keeps it. */
extern s32 port_MatchHidesWins(s32 mode);
/* The select screen loads (select.c, select_Load): TRUE once after a match
 * of versus or link was decided, when the screen is to open on the stage
 * pages with the fighters and weapons kept. */
extern s32 port_MatchToStages(void);
/* A frame of a stage page of mode `mode` (select.c, select_StageStep):
 * with the door open Circle turns the rule, Start its number. TRUE if it
 * took the presses. */
extern s32 port_MatchPageStep(s32 mode, s32 pressed, s32 is_open);
/* TRUE for the modes that play matches: versus, link, versus the CPU. */
extern s32 port_MatchModeHasRules(s32 mode);

/* ---- Crew: the picks and the fighters (under RB_PORT) ---- */

/* The select screen would be done (select.c: a stage chosen, or the
 * weapons of a mode with no stage page): TRUE while the mode's rule is
 * Crew and its crews are not yet all picked, so the place the screen's
 * `sides` picks have just made is taken (port_MatchCrewTake) and the grid
 * comes up again for the next. FALSE if the crews are picked (or kept
 * from the match before at the page's size): the screen is done. */
extern s32 port_MatchCrewWanted(s32 mode, s32 sides);
/* A side's pick for the place being picked. */
extern void port_MatchCrewTake(s32 side, s32 fighter, s32 weapon);
/* The place just taken was the crews' last. */
extern s32 port_MatchCrewDone(void);
/* A crew's place after the first is being picked (the grid, the weapons). */
extern s32 port_MatchCrewPicking(void);
/* The grid has come up for the next place: TRUE once, to clear the
 * picks. */
extern s32 port_MatchCrewGridDue(void);
/* The crews' first places (what the settings' match_picks hold for the
 * first fight): the side's fighter and weapon. */
extern s32 port_MatchCrewFirst(s32 side, s32 *fighter, s32 *weapon);
/* The crews picked are let go: Triangle back from a stage page or from
 * the grid of a later place, or a select screen begun afresh. */
extern void port_MatchCrewDrop(void);
/* Versus the CPU's next opponent is chosen (fight_modes.c,
 * fight_NextVsCpuOpponent): in a Crew match it leads the CPU's crew, the
 * rest at random. */
extern void port_MatchOpponentChosen(void);
/* The game's Rematch of a lost Crew match (versus the CPU): TRUE if the
 * fighters are to be loaded again (the crews' first), FALSE to start
 * where the fighters are, as the game does. */
extern s32 port_MatchRematchLoads(void);

/* ---- the rule page, the wounds and the end (under RB_PORT) ---- */

/* The select screen loads (select.c, select_Load), `to_stages` if it opens
 * on the stage pages after a match, `open` the fighters its grid offers:
 * whether the rule comes before the grid this time, and its page up if so
 * (with ReBushido's top menu; a room's rule is the room's, so its page is
 * not shown), `sides` the sides that pick. */
extern void port_MatchRulePageOpen(
    s32 mode, s32 to_stages, s32 open, s32 sides);
/* A frame of the select screen while the rule page is up: Up and Down the
 * row, Left and Right its value, Cross the rule taken and the grid's
 * turn. TRUE while the page has the frame (the grid is given no presses);
 * *leave set when Triangle leaves the screen. In a session the first pad,
 * the host's, chooses. */
extern s32 port_MatchRulePageStep(s32 pressed, s32 *leave);
/* The rule was chosen before this select screen's grid. */
extern s32 port_MatchRuleFirst(void);
/* The rule page has the row (PORT_MATCH_ROW_...) for the rule on it: the
 * number but for the single fight, the teams but for Crew. */
extern s32 port_MatchRuleHasRow(const PortMatch *match, s32 row);
/* Stage pages after the crews of a rule-first Crew match are picked:
 * Triangle back to the weapons un-takes the last place. */
extern void port_MatchCrewStageBack(void);
/* Triangle on the grid of a crew's later place, nothing picked: back to
 * the place before (TRUE), its picks taken again. */
extern s32 port_MatchCrewBackOne(void);
/* Two on two (port_duo.h): side `side`'s second's fighter (enum
 * CharacterId) and weapon, TRUE if he has a pick of his own; FALSE where
 * he is his first's (a fight with no select screen, a side picked for
 * by none). */
extern s32 port_MatchPartner(s32 side, s32 *fighter, s32 *weapon);
/* ... given (the CPU's, versus the CPU; --partners, port_platform.h). */
extern void port_MatchSetPartner(s32 side, s32 fighter, s32 weapon);
/* A frame of the select screen (select.c): while two on two with four
 * players has its partners picked, each side's half of the pads' `held`
 * and `pressed` is the third pad's (the first side) or the fourth's,
 * where that pad is plugged in. */
extern void port_MatchPartnerPads(s32 *held, s32 *pressed);
/* The crew of `side` already has `fighter` (enum CharacterId) in a place
 * taken: the select refuses it. */
extern s32 port_MatchCrewHas(s32 side, s32 fighter);
/* A fighter set up for a fight (fighter.c, fighter_Init), after the
 * game has healed it: the winner of a Stock or Crew match's fight before
 * comes in with that fight's wounds. */
extern void port_MatchPutWounds(
    s32 index, s16 *off_arm_ok, s16 *legs, s16 *arm, s16 *body);
/* The match went back to the top menu at its end (a Crew match): a
 * session of two ends once the game is there (session.c). */
extern s32 port_MatchEndsSession(void);
/* The session's (session.c, outside the state but the same on every
 * machine of it): a room of several's match is being played. */
extern s32 port_SessionIsRoomMatch(void);
/* a session of netplay is being played: the first pad is the host's */
extern s32 port_SessionIsOn(void);

/* ---- the run's choice, outside the state (src/port/play_switches.c) ---- */

/* The window's match_rule and match_count (--match RULE:N): what the
 * stage pages start at, and what a mode with none plays. In a session the
 * host's, held: port_SetMatchDefault changes nothing after. */
extern void port_SetMatchDefault(s32 rule, s32 count);
extern s32 port_GetMatchRule(void);
extern s32 port_GetMatchCount(void);
/* in the session's word: the rule in the low byte, the number above */
extern u32 port_MatchBits(void);
/* its with its teams 	eams (PORT_TEAMS_...): a session of four
 * players' match is two on two, every fighter a player's (session.c). */
extern u32 port_MatchBitsTeams(u32 bits, s32 teams);
extern void port_HoldMatch(u32 bits);
/* The rule and whether it is held, kept, and put back: a ranked match
 * holds the ladder's rule (online.c), and the player's comes back after. */
extern void port_KeepMatch(void);
extern void port_RestoreMatch(void);

#endif /* PORT_MATCH_H */
