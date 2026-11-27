/* A tournament's bracket (port_bracket.h): its matches built, the results
 * put in and what follows from them, the places. Arithmetic alone, with
 * no variable of its own: the bracket is the caller's (the tournament's,
 * in the game's state; a test's). */
#include "common.h"
#include "bb2.h"

/* xorshift32's three shifts, for the shuffle */
#define SHUFFLE_SHIFT_A 13
#define SHUFFLE_SHIFT_B 17
#define SHUFFLE_SHIFT_C 5
#define SHUFFLE_SEED_NONE 0x9E3779B9U

static u8 port_BracketFrom(s32 kind, s32 index) {
    return (u8)(kind | (index & PORT_BRACKET_FROM_INDEX));
}

/* A match added at the end, its slots fed from `first` and `second`. */
static s32 port_BracketAdd(
    PortBracket *bracket, s32 part, s32 round, u8 first, u8 second) {
    PortBracketMatch *match;

    if (bracket->match_count >= PORT_BRACKET_MATCHES) {
        return -1;
    }
    match = &bracket->match[bracket->match_count];
    match->from[0] = first;
    match->from[1] = second;
    match->who[0] = PORT_BRACKET_NOBODY;
    match->who[1] = PORT_BRACKET_NOBODY;
    match->winner = PORT_BRACKET_NO_RESULT;
    match->part = (u8)part;
    match->round = (u8)round;
    match->played = FALSE;
    match->skipped = FALSE;
    match->order = 0;
    return bracket->match_count++;
}

/* The standard order of the seeds in the places of a bracket of `size`:
 * 1 2; 1 4 2 3; 1 8 4 5 2 7 3 6 (from 0 here). */
static void port_BracketPlaces(u8 *places, s32 size) {
    s32 have = 1;
    s32 i;

    places[0] = 0;
    while (have < size) {
        /* each seed s of the half-size order becomes s and (2 have - 1 - s),
         * in place */
        for (i = have - 1; i >= 0; i--) {
            u8 seed = places[i];

            places[2 * i] = seed;
            places[2 * i + 1] = (u8)(2 * have - 1 - seed);
        }
        have *= 2;
    }
}

/* The first match of each round of a part, as they are added. */
typedef struct {
    s32 first[PORT_BRACKET_MATCHES];
    s32 count[PORT_BRACKET_MATCHES];
} BracketRounds;

/* The losers' rounds that the winners' round `round` (from 2) feeds: the
 * round of the losers' own winners (none after the winners' second
 * round, whose losers' round pairs the first round's losers), then the
 * round where the winners' round's losers come in, crossed over. */
static void port_BracketLosersAfter(
    PortBracket *bracket, BracketRounds *winners, BracketRounds *losers,
    s32 round) {
    s32 odd = 2 * round - 3;
    s32 even = 2 * round - 2;
    s32 count = winners->count[round];
    s32 i;

    losers->first[odd] = bracket->match_count;
    losers->count[odd] = count;
    for (i = 0; i < count; i++) {
        if (round == 2) {
            /* the first round's losers, two by two */
            s32 first = winners->first[1] + 2 * i;

            port_BracketAdd(
                bracket, PORT_BRACKET_LOSERS, odd,
                port_BracketFrom(PORT_BRACKET_FROM_LOSER, first),
                port_BracketFrom(PORT_BRACKET_FROM_LOSER, first + 1));
        } else {
            s32 first = losers->first[odd - 1] + 2 * i;

            port_BracketAdd(
                bracket, PORT_BRACKET_LOSERS, odd,
                port_BracketFrom(PORT_BRACKET_FROM_WINNER, first),
                port_BracketFrom(PORT_BRACKET_FROM_WINNER, first + 1));
        }
    }
    losers->first[even] = bracket->match_count;
    losers->count[even] = count;
    for (i = 0; i < count; i++) {
        port_BracketAdd(
            bracket, PORT_BRACKET_LOSERS, even,
            port_BracketFrom(PORT_BRACKET_FROM_WINNER, losers->first[odd] + i),
            port_BracketFrom(PORT_BRACKET_FROM_LOSER,
                             winners->first[round] + count - 1 - i));
    }
}

/* Who a slot's source gives now: an entrant, a bye, or nobody yet. */
static u8 port_BracketSource(const PortBracket *bracket, u8 from) {
    s32 index = from & PORT_BRACKET_FROM_INDEX;
    const PortBracketMatch *match;

    if ((from & PORT_BRACKET_FROM_KIND) == PORT_BRACKET_FROM_SEED) {
        return index < bracket->count ? bracket->seeds[index]
                                      : (u8)PORT_BRACKET_BYE;
    }
    if (index >= bracket->match_count) {
        return PORT_BRACKET_NOBODY;
    }
    match = &bracket->match[index];
    if (match->winner == PORT_BRACKET_NO_RESULT) {
        return PORT_BRACKET_NOBODY;
    }
    if ((from & PORT_BRACKET_FROM_KIND) == PORT_BRACKET_FROM_WINNER) {
        return match->who[match->winner];
    }
    return match->who[1 - match->winner];
}

/* Everything that follows from the results so far: the slots filled, the
 * byes gone on, the reset needed or not, the champion. */
static void port_BracketSettle(PortBracket *bracket) {
    s32 changed = TRUE;
    s32 i;
    s32 slot;

    while (changed) {
        changed = FALSE;
        for (i = 0; i < bracket->match_count; i++) {
            PortBracketMatch *match = &bracket->match[i];

            if (match->skipped) {
                continue;
            }
            if (match->part == PORT_BRACKET_RESET) {
                const PortBracketMatch *final = &bracket->match[i - 1];

                if (final->winner == 0) {
                    /* the winners' champion won: no reset */
                    match->skipped = TRUE;
                    changed = TRUE;
                    continue;
                }
            }
            for (slot = 0; slot < 2; slot++) {
                if (match->who[slot] == PORT_BRACKET_NOBODY) {
                    u8 who = port_BracketSource(bracket, match->from[slot]);

                    if (who != PORT_BRACKET_NOBODY) {
                        match->who[slot] = who;
                        changed = TRUE;
                    }
                }
            }
            if (match->winner == PORT_BRACKET_NO_RESULT &&
                match->who[0] != PORT_BRACKET_NOBODY &&
                match->who[1] != PORT_BRACKET_NOBODY &&
                (match->who[0] == PORT_BRACKET_BYE ||
                 match->who[1] == PORT_BRACKET_BYE)) {
                /* a bye: the other goes on (two byes: a bye goes on) */
                match->winner = match->who[0] == PORT_BRACKET_BYE ? 1 : 0;
                if (match->who[1] == PORT_BRACKET_BYE) {
                    match->winner = 0;
                }
                match->played = FALSE;
                changed = TRUE;
            }
        }
    }
    bracket->champion = PORT_BRACKET_NOBODY;
    for (i = bracket->match_count - 1; i >= 0; i--) {
        const PortBracketMatch *match = &bracket->match[i];

        if (match->skipped) {
            continue;
        }
        if (match->winner != PORT_BRACKET_NO_RESULT) {
            bracket->champion = match->who[match->winner];
        }
        break;
    }
}

/* The bracket of `count` (port_bracket.h): the winners' rounds, and
 * for double elimination the losers' after each and the grand finals. */
s32 port_BracketBuild(
    PortBracket *bracket, s32 count, s32 is_double, const u8 *seeds) {
    BracketRounds winners;
    BracketRounds losers;
    u8 places[PORT_BRACKET_MOST] = {0};
    s32 round;
    s32 i;
    u32 byte;

    for (byte = 0; byte < sizeof(*bracket); byte++) {
        ((u8 *)bracket)[byte] = 0;
    }
    bracket->champion = PORT_BRACKET_NOBODY;
    if (count < PORT_BRACKET_LEAST || count > PORT_BRACKET_MOST) {
        return FALSE;
    }
    bracket->count = (u8)count;
    bracket->is_double = (u8)(is_double != 0);
    bracket->size = 2;
    bracket->rounds = 1;
    while (bracket->size < count) {
        bracket->size *= 2;
        bracket->rounds++;
    }
    for (i = 0; i < count; i++) {
        bracket->seeds[i] = seeds[i];
    }
    port_BracketPlaces(places, bracket->size);
    /* the winners' first round: the seeds in their places */
    winners.first[1] = 0;
    winners.count[1] = bracket->size / 2;
    for (i = 0; i < bracket->size / 2; i++) {
        port_BracketAdd(
            bracket, PORT_BRACKET_WINNERS, 1,
            port_BracketFrom(PORT_BRACKET_FROM_SEED, places[2 * i]),
            port_BracketFrom(PORT_BRACKET_FROM_SEED, places[2 * i + 1]));
    }
    for (round = 2; round <= bracket->rounds; round++) {
        winners.first[round] = bracket->match_count;
        winners.count[round] = winners.count[round - 1] / 2;
        for (i = 0; i < winners.count[round]; i++) {
            s32 first = winners.first[round - 1] + 2 * i;

            port_BracketAdd(
                bracket, PORT_BRACKET_WINNERS, round,
                port_BracketFrom(PORT_BRACKET_FROM_WINNER, first),
                port_BracketFrom(PORT_BRACKET_FROM_WINNER, first + 1));
        }
        if (bracket->is_double) {
            port_BracketLosersAfter(bracket, &winners, &losers, round);
        }
    }
    if (bracket->is_double) {
        s32 final = winners.first[bracket->rounds];
        /* the losers' champion: its last round's winner, or with no
         * losers' rounds (two entrants) the winners' final's loser */
        u8 second =
            bracket->rounds == 1
                ? port_BracketFrom(PORT_BRACKET_FROM_LOSER, final)
                : port_BracketFrom(PORT_BRACKET_FROM_WINNER,
                                   losers.first[2 * bracket->rounds - 2]);
        s32 grand = port_BracketAdd(
            bracket, PORT_BRACKET_FINAL, 1,
            port_BracketFrom(PORT_BRACKET_FROM_WINNER, final), second);

        port_BracketAdd(bracket, PORT_BRACKET_RESET, 1,
                        port_BracketFrom(PORT_BRACKET_FROM_WINNER, grand),
                        port_BracketFrom(PORT_BRACKET_FROM_LOSER, grand));
    }
    port_BracketSettle(bracket);
    return TRUE;
}

/* The first match with both slots known and no result. */
s32 port_BracketNext(const PortBracket *bracket) {
    s32 i;

    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];

        if (!match->skipped && match->winner == PORT_BRACKET_NO_RESULT &&
            match->who[0] != PORT_BRACKET_NOBODY &&
            match->who[1] != PORT_BRACKET_NOBODY) {
            return i;
        }
    }
    return -1;
}

/* A result put in: only the next match's, and only a slot. */
s32 port_BracketReport(PortBracket *bracket, s32 index, s32 winner) {
    PortBracketMatch *match;

    if (index != port_BracketNext(bracket) || index < 0 ||
        (winner != 0 && winner != 1)) {
        return FALSE;
    }
    match = &bracket->match[index];
    match->winner = (u8)winner;
    match->played = TRUE;
    match->order = (u8)++bracket->played;
    port_BracketSettle(bracket);
    return TRUE;
}

s32 port_BracketIsDone(const PortBracket *bracket) {
    return bracket->count != 0 && port_BracketNext(bracket) < 0;
}

/* The matches played that the entrant lost. */
s32 port_BracketLosses(const PortBracket *bracket, s32 entrant) {
    s32 losses = 0;
    s32 i;

    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];

        if (match->played && match->who[1 - match->winner] == entrant) {
            losses++;
        }
    }
    return losses;
}

s32 port_BracketIsIn(const PortBracket *bracket, s32 entrant) {
    if (entrant < 0 || entrant >= bracket->count) {
        return FALSE;
    }
    if (port_BracketIsDone(bracket)) {
        return bracket->champion == entrant;
    }
    return port_BracketLosses(bracket, entrant) < (bracket->is_double ? 2 : 1);
}

/* How far an entrant out got: the round of its last loss, the losers'
 * rounds and the finals after the winners' (higher, further); 0 while it
 * is in, the most for the champion. */
static s32 port_BracketReach(const PortBracket *bracket, s32 entrant) {
    s32 reach = 0;
    s32 i;

    if (port_BracketIsDone(bracket) && bracket->champion == entrant) {
        return PORT_BRACKET_MATCHES + 1;
    }
    if (port_BracketIsIn(bracket, entrant)) {
        return 0;
    }
    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];

        if (match->played && match->who[1 - match->winner] == entrant) {
            reach = match->part == PORT_BRACKET_WINNERS ? match->round
                    : match->part == PORT_BRACKET_LOSERS
                        ? match->round
                        : 2 * bracket->rounds - 1;
        }
    }
    return reach;
}

/* An entrant's place: one more than those who got further. */
s32 port_BracketPlace(const PortBracket *bracket, s32 entrant) {
    s32 reach = port_BracketReach(bracket, entrant);
    s32 place = 1;
    s32 other;

    if (reach == 0) {
        return 0;
    }
    for (other = 0; other < bracket->count; other++) {
        s32 theirs = port_BracketReach(bracket, other);

        /* those still in finish above, as do those out later */
        if (other != entrant && (theirs == 0 || theirs > reach)) {
            place++;
        }
    }
    return place;
}

s32 port_BracketMost(const PortBracket *bracket) {
    if (bracket->count == 0) {
        return 0;
    }
    return bracket->is_double ? 2 * bracket->count - 1 : bracket->count - 1;
}

/* A Fisher-Yates shuffle by xorshift32 of `seed`. */
void port_BracketShuffle(u8 *order, s32 count, u32 seed) {
    u32 dice = seed != 0 ? seed : SHUFFLE_SEED_NONE;
    s32 i;

    for (i = 0; i < count; i++) {
        order[i] = (u8)i;
    }
    for (i = count - 1; i > 0; i--) {
        s32 pick;
        u8 kept;

        dice ^= dice << SHUFFLE_SHIFT_A;
        dice ^= dice >> SHUFFLE_SHIFT_B;
        dice ^= dice << SHUFFLE_SHIFT_C;
        pick = (s32)(dice % (u32)(i + 1));
        kept = order[i];
        order[i] = order[pick];
        order[pick] = kept;
    }
}

/* The round's name, counted from the bracket's end. */
const char *port_BracketRoundName(const PortBracket *bracket, s32 index) {
    static const char * const g_single[] = {
        "Final", "Semifinal", "Quarterfinal"};
    static const char * const g_winners[] = {
        "Winners' final", "Winners' semifinal", "Winners' round 1"};
    static const char * const g_losers[] = {
        "Losers' round 1", "Losers' round 2", "Losers' round 3",
        "Losers' round 4"};
    const PortBracketMatch *match;
    s32 from_end;

    if (index < 0 || index >= bracket->match_count) {
        return "";
    }
    match = &bracket->match[index];
    from_end = bracket->rounds - match->round;
    switch (match->part) {
    case PORT_BRACKET_WINNERS:
        if (from_end < 0 || from_end > 2) {
            return "";
        }
        return bracket->is_double ? g_winners[from_end] : g_single[from_end];
    case PORT_BRACKET_LOSERS:
        if (match->round == 2 * bracket->rounds - 2) {
            return "Losers' final";
        }
        return match->round >= 1 && match->round <= 4
                   ? g_losers[match->round - 1]
                   : "";
    case PORT_BRACKET_FINAL:
        return "Grand final";
    default:
        return "Grand final, reset";
    }
}
