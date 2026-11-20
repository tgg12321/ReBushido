#ifndef PORT_MATCH_H
#define PORT_MATCH_H

/* The native port's match rules for one against one (ROADMAP 21;
 * docs/PORT.md, "Match rules"; docs/PLAYING.md, "Match rules"). Beside the
 * game's own single fight a match may be:
 *
 * - Stock N: each side has N lives; a fight's loser loses one; the next
 *   fight is on the same stage with the same fighters, each side keeping
 *   its lives, until one side has none;
 * - First to N: fights until one side has won N.
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
 * The rule is chosen on the stage pages (Circle turns the rule, Start its
 * number), which start from the window's match_rule and match_count and
 * keep what the players left them at until the setting changes; a
 * mode with no stage page (versus the CPU with the original's stages)
 * plays the setting's. In a session the host's setting holds (the
 * session's welcome). What the players chose and the match's score are
 * the game's state (src/port/match.c is a state file): rolled back with
 * the game, and the same on both machines of a session. The window draws
 * the panel's column, the lives or the score over the fight and the cards
 * from it (src/port/window/match_page.c); none of it is in the game's
 * video memory. The PlayStation builds never see this header. */

#include "common.h"

/* The rules, in the order Circle turns them. */
enum {
    PORT_MATCH_SINGLE = 0,   /* the game's own one fight */
    PORT_MATCH_STOCK = 1,    /* lives */
    PORT_MATCH_FIRST_TO = 2, /* wins */
    PORT_MATCH_RULES = 3
};

#define PORT_MATCH_COUNT_LEAST 1
#define PORT_MATCH_COUNT_MOST 9
#define PORT_MATCH_COUNT_DEFAULT 3

/* What a fight of the match came to (PortMatch.last). */
enum {
    PORT_MATCH_WON_P1 = 0,
    PORT_MATCH_WON_P2 = 1,
    PORT_MATCH_DRAW = 2,
    PORT_MATCH_NOT_YET = 3
};

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

/* ---- the run's choice, outside the state (src/port/play_switches.c) ---- */

/* The window's match_rule and match_count (--match RULE:N): what the
 * stage pages start at, and what a mode with none plays. In a session the
 * host's, held: port_SetMatchDefault changes nothing after. */
extern void port_SetMatchDefault(s32 rule, s32 count);
extern s32 port_GetMatchRule(void);
extern s32 port_GetMatchCount(void);
/* in the session's word: the rule in the low byte, the number above */
extern u32 port_MatchBits(void);
extern void port_HoldMatch(u32 bits);

#endif /* PORT_MATCH_H */
