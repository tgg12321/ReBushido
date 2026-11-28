#ifndef PORT_BRACKET_H
#define PORT_BRACKET_H

/* A tournament's bracket on the native port (ROADMAP 31; docs/PORT.md,
 * "A local tournament"): two to eight entrants, single or double
 * elimination, as arithmetic alone. Nothing here knows the game: the
 * tournament (port_tournament.h) keeps a PortBracket in the game's state
 * and asks it which match comes next and what a result does; the window
 * draws it; tests/port/bracket.c holds it to what a bracket is.
 *
 * - The entrants are given in seed order (the first the top seed). The
 *   bracket is the next power of two of them (2, 4 or 8 places); a place
 *   with no entrant is a bye, set against the top seeds in the standard
 *   order (1 against 8, 4 against 5, 2 against 7, 3 against 6), so that no
 *   two byes meet. A match with a bye is not played: its entrant goes on.
 * - Single elimination: a match lost is out. Its places: the winner
 *   first, the final's loser second, the semifinals' losers third, the
 *   quarterfinals' fifth.
 * - Double elimination: the winners' bracket as above, and a losers'
 *   bracket that a first loss sends an entrant to (a round of its own
 *   losers, then a round against those the winners' next round drops,
 *   and so on; the drops crossed over, so that two who met need not meet
 *   again at once). A second loss is out. The grand final is the winners'
 *   champion against the losers' champion, with a bracket reset: if the
 *   losers' champion wins it (the winners' champion's first loss), a
 *   second grand final decides, so that nobody is out without two losses.
 *   Its places: the champion first, the grand final's loser second, the
 *   losers' bracket's last round's loser third, its round before fourth,
 *   then the rounds before in pairs (fifth, seventh).
 *
 * A match's two slots are each fed by an entrant's seed, or by the winner
 * or the loser of a match before it; the matches are kept in the order
 * they are played (the winners' first round, then each later winners'
 * round followed by the two losers' rounds it feeds, then the grand
 * finals), and the next match is the first whose two slots are known and
 * that has no result. Everything is bytes, so a bracket is a copy of
 * itself (the game's state saves it as it saves anything). */

#include "common.h"

#define PORT_BRACKET_LEAST 2
#define PORT_BRACKET_MOST 8
/* the most matches: double elimination of eight, a reset included
 * (4 + 2 + 1 of the winners', 2 + 2 + 1 + 1 of the losers', 2 finals) */
#define PORT_BRACKET_MATCHES 15

/* Who is in a slot: an entrant's number (0 to 7), or one of these. */
#define PORT_BRACKET_NOBODY 0xFF /* not known yet */
#define PORT_BRACKET_BYE 0xFE    /* nobody: the other goes on */

/* What feeds a slot: the kind in the top bits, the seed's or the match's
 * number in the low. */
#define PORT_BRACKET_FROM_SEED 0x00
#define PORT_BRACKET_FROM_WINNER 0x40
#define PORT_BRACKET_FROM_LOSER 0x80
#define PORT_BRACKET_FROM_KIND 0xC0
#define PORT_BRACKET_FROM_INDEX 0x3F

/* The part of the bracket a match is in. */
enum {
    PORT_BRACKET_WINNERS = 0,
    PORT_BRACKET_LOSERS = 1,
    PORT_BRACKET_FINAL = 2, /* the grand final (double elimination) */
    PORT_BRACKET_RESET = 3, /* the second grand final, where it is needed */
};

/* A match's result: the slot that won, or none yet. A grand final's reset
 * that is not needed is `skipped`. */
#define PORT_BRACKET_NO_RESULT 0xFF

typedef struct {
    u8 from[2]; /* PORT_BRACKET_FROM_... | number */
    u8 who[2];  /* the entrant in each slot, BYE, or NOBODY yet */
    u8 winner;  /* 0 or 1, or PORT_BRACKET_NO_RESULT */
    u8 part;    /* PORT_BRACKET_WINNERS, ... */
    u8 round;   /* in its part, from 1 */
    u8 played;  /* decided by a match played, not a bye */
    u8 skipped; /* a reset not needed: never played */
    u8 order;   /* its number among the matches played, from 1; 0 not */
} PortBracketMatch;

typedef struct {
    u8 count;     /* entrants */
    u8 size;      /* places: 2, 4 or 8 */
    u8 rounds;    /* the winners' bracket's rounds: 1, 2 or 3 */
    u8 is_double; /* double elimination */
    u8 match_count;
    u8 played;                   /* matches played so far */
    u8 champion;                 /* the winner, or PORT_BRACKET_NOBODY */
    u8 seeds[PORT_BRACKET_MOST]; /* the entrant of each seed */
    PortBracketMatch match[PORT_BRACKET_MATCHES];
} PortBracket;

/* The bracket of `count` entrants (2 to 8), `seeds[i]` the entrant of the
 * i-th seed: FALSE (and an empty bracket) where the count is not one. */
extern s32 port_BracketBuild(
    PortBracket *bracket, s32 count, s32 is_double, const u8 *seeds);
/* The next match to play, or -1 where the bracket is done. */
extern s32 port_BracketNext(const PortBracket *bracket);
/* Match `index` won by its slot `winner` (0 or 1): FALSE where it is not
 * a match to play now. What follows from it (its winner and loser placed,
 * the byes after them gone on, the reset needed or not) is done. */
extern s32 port_BracketReport(PortBracket *bracket, s32 index, s32 winner);
extern s32 port_BracketIsDone(const PortBracket *bracket);
/* An entrant's matches lost, and its place (1 the champion; 0 while it is
 * not known). Entrants out at the same round share a place. */
extern s32 port_BracketLosses(const PortBracket *bracket, s32 entrant);
extern s32 port_BracketPlace(const PortBracket *bracket, s32 entrant);
/* Whether an entrant is still in it. */
extern s32 port_BracketIsIn(const PortBracket *bracket, s32 entrant);
/* The matches there will be to play at most, the byes left out: for the
 * window's "match 3 of 7". */
extern s32 port_BracketMost(const PortBracket *bracket);
/* `order[0..count)` 0 to count-1 shuffled by the dice `seed` (a
 * Fisher-Yates of its own generator): the same seed, the same order. */
extern void port_BracketShuffle(u8 *order, s32 count, u32 seed);
/* A short name of a match's round: "Quarterfinal", "Losers' round 2",
 * "Grand final", ... */
extern const char *port_BracketRoundName(const PortBracket *bracket, s32 index);

#endif /* PORT_BRACKET_H */
