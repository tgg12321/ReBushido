/* A local tournament's bracket (src/port/bracket.c; port_bracket.h): for
 * two to eight entrants, single and double elimination, every match the
 * bracket offers played to its end by many ways of winning (the top seed
 * always, the bottom seed always, each match's slots by turns, and dice),
 * and held each time to what a bracket is: byes never played and only
 * where there are fewer entrants than places, never two byes met, each
 * match played between two entrants still in, a single elimination of N
 * played in N - 1 matches and a double in 2N - 2 or (the reset) 2N - 1,
 * nobody out with fewer losses than the kind allows, the champion's
 * losses, places that count the field (one champion, one second, the
 * rest after them by how far they got). Then the standard seeding of
 * eight, a reset played and not, the round names, the shuffle. */
#include "common.h"
#include "port_bracket.h"
#include "src/port/bracket.c"
#include "runtime/runtime.h"

/* How each match is won in a run of the bracket. */
enum {
    WIN_TOP_SEED = 0, /* the better seed (the lower number) */
    WIN_LOW_SEED = 1, /* the worse */
    WIN_FIRST_SLOT = 2,
    WIN_SECOND_SLOT = 3,
    WIN_DICE = 4,
    WAYS = 5,
};

#define DICE_RUNS 40
#define DICE_STEP 0x9E3779B9U

static s32 test_Same(const char *first, const char *second) {
    while (*first != 0 && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

/* The seed of an entrant (its place in `seeds`). */
static s32 test_SeedOf(const PortBracket *bracket, s32 entrant) {
    s32 seed;

    for (seed = 0; seed < bracket->count; seed++) {
        if (bracket->seeds[seed] == entrant) {
            return seed;
        }
    }
    return -1;
}

/* The slot that wins match `index` the run's way. */
static s32 test_Winner(
    const PortBracket *bracket, s32 index, s32 way, u32 *dice, s32 turn) {
    const PortBracketMatch *match = &bracket->match[index];

    switch (way) {
    case WIN_TOP_SEED:
        return test_SeedOf(bracket, match->who[0]) <
                       test_SeedOf(bracket, match->who[1])
                   ? 0
                   : 1;
    case WIN_LOW_SEED:
        return test_SeedOf(bracket, match->who[0]) >
                       test_SeedOf(bracket, match->who[1])
                   ? 0
                   : 1;
    case WIN_FIRST_SLOT:
        return turn & 1;
    case WIN_SECOND_SLOT:
        return (turn & 1) ^ 1;
    default:
        *dice = *dice * 1103515245U + 12345U;
        return (s32)((*dice >> 16) & 1);
    }
}

/* A whole bracket of `count`, played the run's way, held to everything
 * a bracket is. */
static void test_Run(s32 count, s32 is_double, s32 way, u32 dice) {
    PortBracket bracket;
    u8 seeds[PORT_BRACKET_MOST];
    s32 played = 0;
    s32 turn = 0;
    s32 next;
    s32 i;
    s32 entrant;
    s32 places[PORT_BRACKET_MOST + 1];
    s32 champions = 0;

    port_BracketShuffle(seeds, count, dice + (u32)count);
    CHECK(port_BracketBuild(&bracket, count, is_double, seeds));
    CHECK(bracket.count == count);
    CHECK(bracket.size >= count && bracket.size / 2 < count);
    CHECK(!port_BracketIsDone(&bracket));
    for (entrant = 0; entrant < count; entrant++) {
        CHECK(port_BracketIsIn(&bracket, entrant));
        CHECK(port_BracketPlace(&bracket, entrant) == 0);
    }
    /* a match out of turn is refused */
    next = port_BracketNext(&bracket);
    CHECK(next >= 0);
    if (next + 1 < bracket.match_count) {
        CHECK(!port_BracketReport(&bracket, next + 1, 0));
    }
    CHECK(!port_BracketReport(&bracket, next, 2));
    while ((next = port_BracketNext(&bracket)) >= 0) {
        const PortBracketMatch *match = &bracket.match[next];
        s32 winner;
        s32 slot;

        /* two entrants, both in, neither a bye */
        for (slot = 0; slot < 2; slot++) {
            CHECK(match->who[slot] < count);
            CHECK(port_BracketIsIn(&bracket, match->who[slot]));
        }
        CHECK(match->who[0] != match->who[1]);
        winner = test_Winner(&bracket, next, way, &dice, turn++);
        CHECK(port_BracketReport(&bracket, next, winner));
        CHECK(bracket.match[next].played);
        CHECK(bracket.match[next].order == played + 1);
        played++;
        CHECK(played <= PORT_BRACKET_MATCHES);
        if (played > PORT_BRACKET_MATCHES) {
            return;
        }
    }
    CHECK(port_BracketIsDone(&bracket));
    CHECK(bracket.played == played);
    if (is_double) {
        CHECK(played == 2 * count - 2 || played == 2 * count - 1);
        CHECK(played <= port_BracketMost(&bracket));
    } else {
        CHECK(played == count - 1);
        CHECK(played == port_BracketMost(&bracket));
    }
    CHECK(bracket.champion < count);
    /* byes: none played, a bye only where a place had no entrant, and
     * never against a bye in the first round */
    for (i = 0; i < bracket.match_count; i++) {
        const PortBracketMatch *match = &bracket.match[i];

        if (match->skipped) {
            CHECK(match->part == PORT_BRACKET_RESET);
            CHECK(!match->played);
            continue;
        }
        CHECK(match->winner != PORT_BRACKET_NO_RESULT);
        if (!match->played) {
            CHECK(count < bracket.size);
            CHECK(match->who[0] == PORT_BRACKET_BYE ||
                  match->who[1] == PORT_BRACKET_BYE);
        }
        if (match->part == PORT_BRACKET_WINNERS && match->round == 1) {
            CHECK(match->who[0] != PORT_BRACKET_BYE ||
                  match->who[1] != PORT_BRACKET_BYE);
        }
    }
    /* the losses: the champion's none or (after a reset) one; everyone
     * else out with as many as the kind allows */
    for (i = 0; i <= PORT_BRACKET_MOST; i++) {
        places[i] = 0;
    }
    for (entrant = 0; entrant < count; entrant++) {
        s32 losses = port_BracketLosses(&bracket, entrant);
        s32 place = port_BracketPlace(&bracket, entrant);

        CHECK(place >= 1 && place <= count);
        if (place >= 1 && place <= count) {
            places[place]++;
        }
        if (entrant == bracket.champion) {
            champions++;
            CHECK(place == 1);
            CHECK(port_BracketIsIn(&bracket, entrant));
            CHECK(losses == 0 || (is_double && losses == 1));
        } else {
            CHECK(!port_BracketIsIn(&bracket, entrant));
            CHECK(losses == (is_double ? 2 : 1));
            CHECK(place > 1);
        }
    }
    CHECK(champions == 1);
    CHECK(places[1] == 1);
    CHECK(places[2] == 1);
    /* the places count the field: a place shared by k leaves k - 1 out */
    {
        s32 at = 1;

        while (at <= count) {
            CHECK(places[at] > 0);
            if (places[at] == 0) {
                break;
            }
            at += places[at];
        }
        CHECK(at == count + 1);
    }
}

/* The standard order of eight: 1 against 8, 4 against 5, 2 against 7, 3
 * against 6. */
static void test_Seeding(void) {
    static const u8 g_pairs[4][2] = {{0, 7}, {3, 4}, {1, 6}, {2, 5}};
    PortBracket bracket;
    u8 seeds[PORT_BRACKET_MOST];
    s32 i;

    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        seeds[i] = (u8)i;
    }
    CHECK(port_BracketBuild(&bracket, 8, FALSE, seeds));
    CHECK(bracket.match_count == 7);
    CHECK(bracket.rounds == 3);
    for (i = 0; i < 4; i++) {
        CHECK(bracket.match[i].who[0] == g_pairs[i][0]);
        CHECK(bracket.match[i].who[1] == g_pairs[i][1]);
    }
    CHECK(test_Same(port_BracketRoundName(&bracket, 0), "Quarterfinal"));
    CHECK(test_Same(port_BracketRoundName(&bracket, 4), "Semifinal"));
    CHECK(test_Same(port_BracketRoundName(&bracket, 6), "Final"));
    /* five entrants: the top three seeds have byes and go on at once */
    CHECK(port_BracketBuild(&bracket, 5, FALSE, seeds));
    CHECK(port_BracketNext(&bracket) == 1);
    CHECK(bracket.match[1].who[0] == 3 && bracket.match[1].who[1] == 4);
    CHECK(bracket.match[0].winner == 0 && !bracket.match[0].played);
    CHECK(bracket.match[4].who[0] == 0);
    /* not a count */
    CHECK(!port_BracketBuild(&bracket, 1, FALSE, seeds));
    CHECK(!port_BracketBuild(&bracket, 9, TRUE, seeds));
    CHECK(port_BracketNext(&bracket) < 0);
    CHECK(!port_BracketIsDone(&bracket));
}

/* Double elimination of eight: fifteen matches built; the grand final
 * won by the winners' champion ends it (no reset), by the losers'
 * champion asks the reset, whose winner is the champion. */
static void test_Reset(void) {
    PortBracket bracket;
    u8 seeds[PORT_BRACKET_MOST];
    s32 losers_won;
    s32 i;

    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        seeds[i] = (u8)i;
    }
    for (losers_won = 0; losers_won < 2; losers_won++) {
        s32 next;
        s32 final = -1;

        CHECK(port_BracketBuild(&bracket, 8, TRUE, seeds));
        CHECK(bracket.match_count == PORT_BRACKET_MATCHES);
        CHECK(test_Same(port_BracketRoundName(&bracket, 13), "Grand final"));
        CHECK(test_Same(port_BracketRoundName(&bracket, 12), "Losers' final"));
        while ((next = port_BracketNext(&bracket)) >= 0) {
            const PortBracketMatch *match = &bracket.match[next];
            s32 winner;

            if (match->part == PORT_BRACKET_FINAL) {
                final = next;
                /* the winners' champion is the top seed, unbeaten */
                CHECK(match->who[0] == 0);
                CHECK(port_BracketLosses(&bracket, 0) == 0);
                CHECK(port_BracketLosses(&bracket, match->who[1]) == 1);
                winner = losers_won;
            } else if (match->part == PORT_BRACKET_RESET) {
                /* the reset: the two again, each with a loss, the
                 * losers' champion first; he wins it */
                CHECK(losers_won);
                CHECK(match->who[1] == 0);
                CHECK(port_BracketLosses(&bracket, 0) == 1);
                winner = 0;
            } else {
                /* the better seed wins */
                winner = match->who[0] < match->who[1] ? 0 : 1;
            }
            CHECK(port_BracketReport(&bracket, next, winner));
        }
        CHECK(final == 13);
        CHECK(port_BracketIsDone(&bracket));
        CHECK(bracket.match[14].skipped == !losers_won);
        CHECK(bracket.played == (losers_won ? 15 : 14));
        CHECK(port_BracketPlace(&bracket, bracket.champion) == 1);
        CHECK(port_BracketPlace(&bracket, 0) == (losers_won ? 2 : 1));
    }
}

static void test_Shuffle(void) {
    u8 order[PORT_BRACKET_MOST];
    u8 again[PORT_BRACKET_MOST];
    s32 seen[PORT_BRACKET_MOST];
    s32 differs = FALSE;
    s32 i;

    port_BracketShuffle(order, 8, 7);
    port_BracketShuffle(again, 8, 7);
    for (i = 0; i < 8; i++) {
        seen[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        CHECK(order[i] == again[i]);
        CHECK(order[i] < 8);
        seen[order[i] % 8]++;
    }
    for (i = 0; i < 8; i++) {
        CHECK(seen[i] == 1);
    }
    port_BracketShuffle(again, 8, 8);
    for (i = 0; i < 8; i++) {
        differs |= order[i] != again[i];
    }
    CHECK(differs);
}

void test_Main(void) {
    s32 count;
    s32 is_double;
    s32 way;
    u32 dice;

    test_Seeding();
    test_Reset();
    test_Shuffle();
    for (count = PORT_BRACKET_LEAST; count <= PORT_BRACKET_MOST; count++) {
        for (is_double = 0; is_double < 2; is_double++) {
            for (way = 0; way < WAYS - 1; way++) {
                test_Run(count, is_double, way, (u32)way);
            }
            for (dice = 1; dice <= DICE_RUNS; dice++) {
                test_Run(count, is_double, WIN_DICE, dice * DICE_STEP);
            }
        }
    }
}
