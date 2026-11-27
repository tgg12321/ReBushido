/* The settings menu's pages of a local tournament (menu.h; docs/PLAYING.md,
 * "Tournament"; include/port_tournament.h): the page a tournament is
 * begun from (its four settings, each entrant, Begin), an entrant's own
 * page (its name, who plays it, a CPU's fighter and weapon), and the
 * bracket shown between the matches, with the next match to play and the
 * way out. Only the build with a window has this file. What it begins and
 * plays it asks of the game (tournament_ask.c), which puts it into the
 * game's state at the host's next boundary; the bracket it draws is the
 * game's state as the step left it (src/port/tournament.c). */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "menu.h"
#include "port_cpu.h"
#include "port_match.h"
#include "port_tournament.h"
#include "sdl_window.h"

#define TEXT_BYTES 160
/* Knuth's multiplier, to spread the clock's low bits */
#define SEED_SPREAD 2654435761U
#define COUNT_OF(array) ((s32)(sizeof(array) / sizeof((array)[0])))

/* An item of a page: its kind in the high bits, an entrant's number in
 * the low. */
#define ITEM_SHIFT 8
#define ITEM_INDEX 0xFF

enum {
    ITEM_ENTRANT = 1, /* the tournament's page: an entrant, `index` */
    ITEM_BEGIN = 2,
    ITEM_NAME = 3, /* an entrant's page */
    ITEM_WHO = 4,
    ITEM_FIGHTER = 5,
    ITEM_WEAPON = 6,
    ITEM_PLAY = 7, /* the bracket's page */
    ITEM_LEAVE = 8,
    ITEM_DONE = 9,
};

#define ITEM(kind, index) ((kind) << ITEM_SHIFT | (index))
#define ITEM_KIND(item) ((item) >> ITEM_SHIFT)

/* Who plays an entrant, as the page steps through it: the PC's four pads,
 * then the CPU at each of its levels. */
#define WHO_COUNT (PORT_TOURNAMENT_PADS + PORT_CPU_LEVELS + 1)

/* A CPU's blades, in the game's order (enum WeaponId). */
#define BLADES 6

static const char * const g_cpu_names[PORT_CPU_LEVELS + 1] = {
    "the game's CPU", "Novice", "Adept", "Veteran", "Master"};

static struct {
    PortTournamentEntrant entrant[PORT_BRACKET_MOST];
    s32 has_entrants;
    s32 editing;     /* the entrant whose page is up */
    s32 leave_asked; /* Leave pressed once: the second leaves */
} g_tour;

/* The page's entrants as they start: the first player's pad, then the CPU
 * at each level in turn, each drawing its fighter and weapon. */
static void port_TourEntrants(void) {
    s32 i;

    if (g_tour.has_entrants) {
        return;
    }
    g_tour.has_entrants = TRUE;
    for (i = 0; i < PORT_BRACKET_MOST; i++) {
        PortTournamentEntrant *entrant = &g_tour.entrant[i];

        entrant->name[0] = 0;
        entrant->pad = i == 0 ? 0 : PORT_TOURNAMENT_CPU;
        entrant->cpu =
            (u8)(PORT_CPU_NOVICE + (i + PORT_CPU_LEVELS - 2) % PORT_CPU_LEVELS);
        entrant->fighter = PORT_TOURNAMENT_RANDOM;
        entrant->weapon = PORT_TOURNAMENT_RANDOM;
    }
}

static s32 port_TourWho(const PortTournamentEntrant *entrant) {
    return entrant->pad < PORT_TOURNAMENT_PADS
               ? entrant->pad
               : PORT_TOURNAMENT_PADS + entrant->cpu;
}

static void port_TourSetWho(PortTournamentEntrant *entrant, s32 who) {
    if (who < PORT_TOURNAMENT_PADS) {
        entrant->pad = (u8)who;
    } else {
        entrant->pad = PORT_TOURNAMENT_CPU;
        entrant->cpu = (u8)(who - PORT_TOURNAMENT_PADS);
    }
}

static void port_TourWhoText(
    const PortTournamentEntrant *entrant, char *out, u32 room) {
    if (entrant->pad < PORT_TOURNAMENT_PADS) {
        snprintf(out, room, "Player %d's pad", entrant->pad + 1);
    } else {
        snprintf(out, room, "CPU, %s", g_cpu_names[entrant->cpu]);
    }
}

static s32 port_TourSetting(s32 value, s32 least, s32 most) {
    return value < least ? least : value > most ? most : value;
}

/* The settings' count of entrants. */
static s32 port_TourCount(void) {
    return port_TourSetting(g_port_settings.tournament_entrants,
                            PORT_BRACKET_LEAST, PORT_BRACKET_MOST);
}

/* ---- the rows ---- */

s32 port_TournamentPageItems(s32 page, s32 *items, s32 most) {
    const PortTournament *tournament = &g_port_tournament;
    s32 count = 0;
    s32 i;

    port_TourEntrants();
    if (page == PORT_TOUR_PAGE_SETUP) {
        for (i = 0; i < port_TourCount() && count < most; i++) {
            items[count++] = ITEM(ITEM_ENTRANT, i);
        }
        if (count < most) {
            items[count++] = ITEM(ITEM_BEGIN, 0);
        }
    } else if (page == PORT_TOUR_PAGE_ENTRANT) {
        items[count++] = ITEM(ITEM_NAME, g_tour.editing);
        items[count++] = ITEM(ITEM_WHO, g_tour.editing);
        if (g_tour.entrant[g_tour.editing].pad >= PORT_TOURNAMENT_PADS) {
            items[count++] = ITEM(ITEM_FIGHTER, g_tour.editing);
            items[count++] = ITEM(ITEM_WEAPON, g_tour.editing);
        }
    } else if (tournament->phase == PORT_TOURNAMENT_DONE) {
        items[count++] = ITEM(ITEM_DONE, 0);
    } else if (tournament->phase != PORT_TOURNAMENT_OFF) {
        items[count++] = ITEM(ITEM_PLAY, 0);
        items[count++] = ITEM(ITEM_LEAVE, 0);
    }
    return count;
}

u32 port_TournamentPageGeneration(void) {
    const PortTournament *tournament = &g_port_tournament;

    return (u32)tournament->phase | (u32)tournament->decided << ITEM_SHIFT |
           (u32)tournament->number << (2 * ITEM_SHIFT);
}

/* An entrant's name as the bracket shows it. */
static void port_TourName(s32 entrant, char *out, u32 room) {
    const PortTournament *tournament = &g_port_tournament;

    if (entrant == PORT_BRACKET_BYE) {
        snprintf(out, room, "bye");
    } else if (entrant >= tournament->setup.count) {
        snprintf(out, room, "-");
    } else {
        port_TournamentName(
            &tournament->setup.entrant[entrant], entrant, out, room);
    }
}

/* The match to play next, as a line: "A against B". */
static void port_TourNextText(char *out, u32 room) {
    const PortTournament *tournament = &g_port_tournament;
    s32 next = port_BracketNext(&tournament->bracket);
    char first[TEXT_BYTES];
    char second[TEXT_BYTES];

    if (next < 0) {
        snprintf(out, room, "-");
        return;
    }
    port_TourName(tournament->bracket.match[next].who[0], first, sizeof(first));
    port_TourName(
        tournament->bracket.match[next].who[1], second, sizeof(second));
    snprintf(out, room, "%s against %s", first, second);
}

void port_TournamentPageDescribe(
    s32 item, const char *typed, char *label, u32 label_room, char *value,
    u32 value_room, s32 *is_arrows) {
    s32 index = item & ITEM_INDEX;
    const PortTournamentEntrant *entrant =
        &g_tour.entrant[index < PORT_BRACKET_MOST ? index : 0];
    char who[TEXT_BYTES];
    char name[TEXT_BYTES];

    port_TourEntrants();
    *is_arrows = FALSE;
    value[0] = 0;
    switch (ITEM_KIND(item)) {
    case ITEM_ENTRANT:
        port_TournamentName(entrant, index, name, sizeof(name));
        port_TourWhoText(entrant, who, sizeof(who));
        snprintf(label, label_room, "%d. %s", index + 1, name);
        if (entrant->pad < PORT_TOURNAMENT_PADS) {
            snprintf(value, value_room, "%s", who);
        } else {
            snprintf(value, value_room, "%s, %s, %s", who,
                     entrant->fighter == PORT_TOURNAMENT_RANDOM
                         ? "any fighter"
                         : port_NameCharacter(entrant->fighter),
                     entrant->weapon == PORT_TOURNAMENT_RANDOM
                         ? "any blade"
                         : port_NameWeapon(entrant->weapon));
        }
        *is_arrows = TRUE;
        break;
    case ITEM_BEGIN:
        snprintf(label, label_room, "Begin the tournament");
        break;
    case ITEM_NAME:
        snprintf(label, label_room, "Name");
        if (typed != NULL) {
            snprintf(value, value_room, "%s_", typed);
        } else {
            port_TournamentName(entrant, index, value, value_room);
        }
        break;
    case ITEM_WHO:
        snprintf(label, label_room, "Played by");
        port_TourWhoText(entrant, value, value_room);
        *is_arrows = TRUE;
        break;
    case ITEM_FIGHTER:
        snprintf(label, label_room, "Fighter");
        snprintf(value, value_room, "%s",
                 entrant->fighter == PORT_TOURNAMENT_RANDOM
                     ? "Any (drawn for each match)"
                     : port_NameCharacter(entrant->fighter));
        *is_arrows = TRUE;
        break;
    case ITEM_WEAPON:
        snprintf(label, label_room, "Weapon");
        snprintf(value, value_room, "%s",
                 entrant->weapon == PORT_TOURNAMENT_RANDOM
                     ? "Any blade (drawn for each match)"
                     : port_NameWeapon(entrant->weapon));
        *is_arrows = TRUE;
        break;
    case ITEM_PLAY:
        snprintf(label, label_room, "Play the next match");
        port_TourNextText(value, value_room);
        break;
    case ITEM_LEAVE:
        snprintf(label, label_room, "%s",
                 g_tour.leave_asked ? "Leave: press again to leave"
                                    : "Leave the tournament");
        break;
    default:
        snprintf(label, label_room, "Back to the main menu");
        break;
    }
}

const char *port_TournamentPageHelp(s32 item, s32 is_typing) {
    switch (ITEM_KIND(item)) {
    case ITEM_ENTRANT:
        return "Left, Right: who plays it.  Enter / Cross: its name, and a "
               "CPU's fighter and weapon";
    case ITEM_BEGIN:
        return "The seeds drawn (or in order) and the bracket made; its "
               "first match is next";
    case ITEM_NAME:
        return is_typing ? "Type the name; Enter keeps it, Escape leaves it"
                         : "Enter / Cross: type a name (letters, digits, "
                           "spaces)";
    case ITEM_WHO:
        return "A player picks his fighter at the select screen with his "
               "own pad; the CPU plays at its level";
    case ITEM_FIGHTER:
        return "Set, or drawn for each match from the six of a new save";
    case ITEM_WEAPON:
        return "Set, or a blade drawn for each match (where the fighter's "
               "clan has it)";
    case ITEM_PLAY:
        return "Its players pick at the select screen; two CPUs go straight "
               "to their fight";
    case ITEM_LEAVE:
        return "The tournament ends here and is not kept";
    default:
        return "The result is kept in your record (Options, Stats)";
    }
}

/* The setup the page has, as the game takes it: FALSE, with why in
 * `note`, where two entrants are played by the same pad. */
static s32 port_TourSetup(PortTournamentSetup *setup, char *note, u32 room) {
    static const u8 g_rules[][2] = {
        {PORT_MATCH_FIRST_TO, 1},
        {PORT_MATCH_FIRST_TO, 2},
        {PORT_MATCH_FIRST_TO, 3},
        {PORT_MATCH_STOCK, 2},
        {PORT_MATCH_STOCK, 3}};
    s32 rule = port_TourSetting(
        g_port_settings.tournament_match, 0, COUNT_OF(g_rules) - 1);
    s32 used[PORT_TOURNAMENT_PADS];
    s32 i;

    memset(setup, 0, sizeof(*setup));
    memset(used, 0, sizeof(used));
    setup->count = (u8)port_TourCount();
    setup->is_double = (u8)(g_port_settings.tournament_elimination != 0);
    setup->seeding = (u8)(g_port_settings.tournament_seeding != 0
                              ? PORT_TOURNAMENT_SEED_ORDER
                              : PORT_TOURNAMENT_SEED_RANDOM);
    setup->rule = g_rules[rule][0];
    setup->rule_count = g_rules[rule][1];
    /* the dice: the PC's clock, never the game's */
    setup->seed =
        (u32)SDL_GetPerformanceCounter() * SEED_SPREAD ^ SDL_GetTicks();
    for (i = 0; i < setup->count; i++) {
        const PortTournamentEntrant *entrant = &g_tour.entrant[i];

        if (entrant->pad < PORT_TOURNAMENT_PADS && used[entrant->pad]++) {
            snprintf(note, room,
                     "Player %d's pad plays two entrants: give one another "
                     "pad, or the CPU",
                     entrant->pad + 1);
            return FALSE;
        }
        setup->entrant[i] = *entrant;
    }
    return TRUE;
}

s32 port_TournamentPageAccept(
    s32 item, char *typed, u32 typed_room, char *note, u32 note_room) {
    PortTournamentSetup setup;
    s32 index = item & ITEM_INDEX;

    port_TourEntrants();
    switch (ITEM_KIND(item)) {
    case ITEM_ENTRANT:
        g_tour.editing = index;
        return PORT_TOUR_ENTRANT;
    case ITEM_BEGIN:
        if (!port_TourSetup(&setup, note, note_room)) {
            return PORT_TOUR_NOTHING;
        }
        g_tour.leave_asked = FALSE;
        port_AskTournamentStart(&setup);
        port_Note("tournament page: begun, %d entrants", setup.count);
        return PORT_TOUR_BRACKET;
    case ITEM_NAME:
        snprintf(typed, typed_room, "%s", g_tour.entrant[index].name);
        return PORT_TOUR_TYPE;
    case ITEM_WHO:
    case ITEM_FIGHTER:
    case ITEM_WEAPON:
        return port_TournamentPageStep(item, 1);
    case ITEM_PLAY:
        g_tour.leave_asked = FALSE;
        port_AskTournamentPlay();
        return PORT_TOUR_CLOSE;
    case ITEM_LEAVE:
        if (!g_tour.leave_asked) {
            g_tour.leave_asked = TRUE;
            return PORT_TOUR_NOTHING;
        }
        g_tour.leave_asked = FALSE;
        port_AskTournamentLeave();
        return PORT_TOUR_CLOSE;
    default:
        port_AskTournamentLeave();
        return PORT_TOUR_CLOSE;
    }
}

s32 port_TournamentPageStep(s32 item, s32 step) {
    s32 index = item & ITEM_INDEX;
    PortTournamentEntrant *entrant =
        &g_tour.entrant[index < PORT_BRACKET_MOST ? index : 0];
    s32 fighters;
    const u8 *list = port_TournamentFighters(&fighters);
    s32 at;

    port_TourEntrants();
    switch (ITEM_KIND(item)) {
    case ITEM_ENTRANT:
    case ITEM_WHO:
        port_TourSetWho(
            entrant, (port_TourWho(entrant) + step + WHO_COUNT) % WHO_COUNT);
        port_PageSound(PORT_PAGE_SOUND_CURSOR);
        return ITEM_KIND(item) == ITEM_WHO ? PORT_TOUR_REBUILD
                                           : PORT_TOUR_NOTHING;
    case ITEM_FIGHTER:
        /* any, then each of the list */
        for (at = 0; at < fighters && list[at] != entrant->fighter; at++) {
        }
        at = entrant->fighter == PORT_TOURNAMENT_RANDOM ? fighters : at;
        at = (at + step + fighters + 1) % (fighters + 1);
        entrant->fighter = at == fighters ? PORT_TOURNAMENT_RANDOM : list[at];
        port_PageSound(PORT_PAGE_SOUND_CURSOR);
        return PORT_TOUR_NOTHING;
    case ITEM_WEAPON:
        at = entrant->weapon == PORT_TOURNAMENT_RANDOM
                 ? BLADES
                 : entrant->weapon;
        at = (at + step + BLADES + 1) % (BLADES + 1);
        entrant->weapon = at == BLADES ? PORT_TOURNAMENT_RANDOM : (u8)at;
        port_PageSound(PORT_PAGE_SOUND_CURSOR);
        return PORT_TOUR_NOTHING;
    default:
        return PORT_TOUR_NOTHING;
    }
}

s32 port_TournamentPageTakes(s32 item, char c) {
    return ITEM_KIND(item) == ITEM_NAME &&
           ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_');
}

s32 port_TournamentPageKeep(s32 item, const char *typed) {
    s32 index = item & ITEM_INDEX;

    if (ITEM_KIND(item) != ITEM_NAME || index >= PORT_BRACKET_MOST) {
        return FALSE;
    }
    snprintf(g_tour.entrant[index].name, sizeof(g_tour.entrant[index].name),
             "%s", typed);
    return TRUE;
}

const char *port_TournamentPageTitle(s32 page) {
    static char title[TEXT_BYTES];

    if (page == PORT_TOUR_PAGE_ENTRANT) {
        char name[TEXT_BYTES];

        port_TournamentName(&g_tour.entrant[g_tour.editing], g_tour.editing,
                            name, sizeof(name));
        snprintf(
            title, sizeof(title), "Entrant %d: %s", g_tour.editing + 1, name);
        return title;
    }
    return "Tournament";
}

void port_TournamentPageOpened(void) { g_tour.leave_asked = FALSE; }

/* ---- the bracket drawn ---- */

/* Where the bracket is drawn, in the game's places (menu.h): inside the
 * game's wide panel, above the page's rows. Single elimination is one
 * band of the winners' rounds and the champion; double elimination the
 * winners' rounds with the grand finals beside them, and under them the
 * losers' rounds. Each match a box of its two entrants, the line from
 * it to the match its winner goes to. */
#define BRACKET_LEFT 72
#define BRACKET_RIGHT 568
#define BRACKET_TOP 52
#define BRACKET_BOTTOM 178
#define BRACKET_GAP 6
#define BAND_HEAD 9
#define COLUMN_GAP 14
#define NAME_INSET 3
#define LINE_SIZE 7
#define HEAD_SIZE 6
#define BOX_HIGH (2 * LINE_SIZE + 4)
/* the room of each of the winners' first round's four matches, double
 * elimination of eight: the winners' band as high as they need, the
 * losers' the rest */
#define WINNERS_ROOM 17
/* the page's rows and lines under the bracket */
#define ROW_FIRST_Y 192
#define ROW_PITCH 11
#define HELP_Y 217
#define KEYS_Y 236
#define VALUE_RIGHT 556
/* a match's box, the next match's, and the lines between them */
#define BOX_COLOR 0xA0141C30U
#define BOX_NEXT 0xC0504018U
#define LINK_COLOR 0xB0A8A8C0U
#define LINK_LIT 0xFFE6C877U

/* A match's box, in the game's places; `shown` where it is drawn. */
typedef struct {
    s16 x;
    s16 y;
    s16 w;
    s16 h;
    u8 shown;
} TourBox;

/* the matches' boxes, and the champion's */
static TourBox g_tour_boxes[PORT_BRACKET_MATCHES + 1];
#define CHAMPION_BOX PORT_BRACKET_MATCHES

/* A match drawn in its box: its two slots, the winner white, the loser
 * dark, a slot not known yet a dash, the next match's lit. */
static void port_TourDrawMatch(PortLook *look, const PortBracket *bracket,
                               s32 index, s32 is_next, s32 pulse) {
    const PortBracketMatch *match = &bracket->match[index];
    const TourBox *box = &g_tour_boxes[index];
    s32 slot;

    port_LookBox(
        look, box->x, box->y, box->w, box->h, is_next ? BOX_NEXT : BOX_COLOR);
    for (slot = 0; slot < 2; slot++) {
        char name[TEXT_BYTES];
        u32 color = PORT_LOOK_GREY;
        s32 baseline = box->y + (slot + 1) * box->h / 2 - 1;

        port_TourName(match->who[slot], name, sizeof(name));
        if (match->skipped) {
            snprintf(name, sizeof(name), "%s", slot == 0 ? "not needed" : "");
            color = PORT_LOOK_OFF;
        } else if (is_next) {
            color = port_LookPulse(pulse);
        } else if (match->who[slot] == PORT_BRACKET_BYE) {
            color = PORT_LOOK_OFF;
        } else if (match->winner != PORT_BRACKET_NO_RESULT) {
            color = match->winner == slot ? PORT_LOOK_WHITE : PORT_LOOK_OFF;
        }
        port_LookFit(
            look, LINE_SIZE, PORT_FACE_TEXT, name, box->w - 2 * NAME_INSET);
        port_LookText(look, box->x + NAME_INSET, baseline, LINE_SIZE,
                      match->winner == slot && match->played ? PORT_FACE_BOLD
                                                             : PORT_FACE_TEXT,
                      name, color, PORT_LOOK_LEFT);
    }
}

/* One band of the bracket laid out: the rounds of `part` as columns
 * between `top` and `bottom` (`columns` across, the last ones the
 * finals'), each round's matches spread down its column, a column's head
 * its round's name. */
static void port_TourLayBand(
    PortLook *look, const PortBracket *bracket, s32 part, s32 rounds,
    s32 columns, s32 top, s32 bottom) {
    s32 width = (BRACKET_RIGHT - BRACKET_LEFT) / columns;
    s32 round;

    for (round = 1; round <= rounds; round++) {
        s32 x = BRACKET_LEFT + (round - 1) * width;
        s32 count = 0;
        s32 at = 0;
        s32 i;

        for (i = 0; i < bracket->match_count; i++) {
            count += bracket->match[i].part == part &&
                     bracket->match[i].round == round;
        }
        for (i = 0; i < bracket->match_count; i++) {
            TourBox *box = &g_tour_boxes[i];
            s32 room = (bottom - top - BAND_HEAD) / (count > 0 ? count : 1);
            s32 h = room - 2 < BOX_HIGH ? room - 2 : BOX_HIGH;

            if (bracket->match[i].part != part ||
                bracket->match[i].round != round) {
                continue;
            }
            if (at == 0) {
                port_LookText(look, x, top + HEAD_SIZE, HEAD_SIZE,
                              PORT_FACE_TEXT, port_BracketRoundName(bracket, i),
                              PORT_LOOK_GOLD, PORT_LOOK_LEFT);
            }
            box->x = (s16)x;
            box->y = (s16)(top + BAND_HEAD + at * room + (room - h) / 2);
            box->w = (s16)(width - COLUMN_GAP);
            box->h = (s16)h;
            box->shown = TRUE;
            at++;
        }
    }
}

/* The grand finals (double elimination) and the champion's box, in the
 * winners' band's last column. */
static void port_TourLayFinals(PortLook *look, const PortBracket *bracket,
                               s32 columns, s32 top, s32 bottom) {
    s32 width = (BRACKET_RIGHT - BRACKET_LEFT) / columns;
    s32 x = BRACKET_LEFT + (columns - 1) * width;
    s32 y = top;
    TourBox *champion = &g_tour_boxes[CHAMPION_BOX];
    s32 i;

    if (bracket->is_double) {
        for (i = 0; i < bracket->match_count; i++) {
            TourBox *box = &g_tour_boxes[i];

            if (bracket->match[i].part == PORT_BRACKET_FINAL ||
                bracket->match[i].part == PORT_BRACKET_RESET) {
                port_LookText(look, x, y + HEAD_SIZE, HEAD_SIZE, PORT_FACE_TEXT,
                              port_BracketRoundName(bracket, i), PORT_LOOK_GOLD,
                              PORT_LOOK_LEFT);
                box->x = (s16)x;
                box->y = (s16)(y + BAND_HEAD);
                box->w = (s16)(width - COLUMN_GAP);
                box->h = BOX_HIGH;
                box->shown = TRUE;
                y += BOX_HIGH + BAND_HEAD + 1;
            }
        }
    } else {
        y = top + (bottom - top) / 2 - BOX_HIGH / 2;
    }
    port_LookText(look, x, y + HEAD_SIZE, HEAD_SIZE, PORT_FACE_TEXT, "Champion",
                  PORT_LOOK_GOLD, PORT_LOOK_LEFT);
    champion->x = (s16)x;
    champion->y = (s16)(y + BAND_HEAD - 1);
    champion->w = (s16)(width - COLUMN_GAP);
    champion->h = LINE_SIZE + 5;
    champion->shown = TRUE;
}

/* A line of the game's places from (x1, y1) across to the middle of the
 * gap, down or up to y2, and across to x2. */
static void port_TourElbow(
    PortLook *look, s32 x1, s32 y1, s32 x2, s32 y2, u32 color) {
    s32 middle = x2 - COLUMN_GAP / 2;

    if (middle < x1) {
        middle = x1;
    }
    port_LookBox(look, x1, y1, middle - x1, 1, color);
    port_LookBox(look, middle, y1 < y2 ? y1 : y2, 1,
                 (y1 < y2 ? y2 - y1 : y1 - y2) + 1, color);
    port_LookBox(look, middle, y2, x2 - middle, 1, color);
}

/* The lines from each match to where its winner goes, lit where it has
 * gone; and the last match's to the champion. */
static void port_TourDrawLinks(PortLook *look, const PortBracket *bracket) {
    s32 i;
    s32 slot;

    for (i = 0; i < bracket->match_count; i++) {
        const PortBracketMatch *match = &bracket->match[i];
        const TourBox *to = &g_tour_boxes[i];

        if (!to->shown || match->skipped) {
            continue;
        }
        for (slot = 0; slot < 2; slot++) {
            s32 from = match->from[slot] & PORT_BRACKET_FROM_INDEX;
            const TourBox *box = &g_tour_boxes[from];

            if ((match->from[slot] & PORT_BRACKET_FROM_KIND) !=
                    PORT_BRACKET_FROM_WINNER ||
                !box->shown || box->x >= to->x) {
                continue;
            }
            port_TourElbow(look, box->x + box->w, box->y + box->h / 2, to->x,
                           to->y + (2 * slot + 1) * to->h / 4,
                           bracket->match[from].winner != PORT_BRACKET_NO_RESULT
                               ? LINK_LIT
                               : LINK_COLOR);
        }
    }
    if (!bracket->is_double) {
        const TourBox *box = &g_tour_boxes[bracket->match_count - 1];
        const TourBox *champion = &g_tour_boxes[CHAMPION_BOX];

        port_TourElbow(
            look, box->x + box->w, box->y + box->h / 2, champion->x,
            champion->y + champion->h / 2,
            bracket->champion != PORT_BRACKET_NOBODY ? LINK_LIT : LINK_COLOR);
    }
}

/* The champion's box. */
static void port_TourDrawChampion(PortLook *look, const PortBracket *bracket) {
    const TourBox *box = &g_tour_boxes[CHAMPION_BOX];
    char name[TEXT_BYTES];

    port_TourName(bracket->champion, name, sizeof(name));
    port_LookBox(look, box->x, box->y, box->w, box->h, BOX_COLOR);
    port_LookFit(
        look, LINE_SIZE + 1, PORT_FACE_BOLD, name, box->w - 2 * NAME_INSET);
    port_LookText(look, box->x + NAME_INSET, box->y + box->h - 2, LINE_SIZE + 1,
                  PORT_FACE_BOLD, name,
                  bracket->champion != PORT_BRACKET_NOBODY ? PORT_LOOK_GOLD
                                                           : PORT_LOOK_OFF,
                  PORT_LOOK_LEFT);
}

/* The line under the rows: the next match, or the places. */
static void port_TourStatusText(char *out, u32 room) {
    const PortTournament *tournament = &g_port_tournament;
    const PortBracket *bracket = &tournament->bracket;
    s32 next = port_BracketNext(bracket);
    u32 at = 0;
    s32 place;
    s32 i;

    out[0] = 0;
    if (tournament->phase == PORT_TOURNAMENT_DONE) {
        for (place = 1; place <= bracket->count && at + 1 < room; place++) {
            for (i = 0; i < bracket->count && at + 1 < room; i++) {
                if (port_BracketPlace(bracket, i) == place) {
                    char name[TEXT_BYTES];

                    port_TourName(i, name, sizeof(name));
                    at += (u32)snprintf(out + at, room - at, "%s%d. %s",
                                        at == 0 ? "" : "   ", place, name);
                }
            }
        }
        return;
    }
    if (next >= 0) {
        const PortBracketMatch *match = &bracket->match[next];
        char who[2][TEXT_BYTES];
        s32 slot;

        for (slot = 0; slot < 2; slot++) {
            port_TourWhoText(&tournament->setup.entrant[match->who[slot]],
                             who[slot], sizeof(who[slot]));
        }
        snprintf(out, room, "Match %d of up to %d, %s: %s against %s",
                 bracket->played + 1, port_BracketMost(bracket),
                 port_BracketRoundName(bracket, next), who[0], who[1]);
    }
}

void port_TournamentDrawBracket(PortLook *look, const s32 *items, s32 count,
                                s32 cursor, s32 pulse, const char *note) {
    const PortTournament *tournament = &g_port_tournament;
    const PortBracket *bracket = &tournament->bracket;
    s32 next = port_BracketNext(bracket);
    s32 columns = bracket->rounds + 1;
    char text[TEXT_BYTES * 2];
    char label[TEXT_BYTES];
    char value[TEXT_BYTES];
    s32 i;

    if (tournament->phase == PORT_TOURNAMENT_OFF || bracket->count == 0) {
        port_LookTitle(look, "Tournament");
        port_LookText(
            look, PORT_LOOK_GAME_W / 2, BRACKET_TOP + 60, PORT_LOOK_SIZE_TEXT,
            PORT_FACE_TEXT, "The bracket is being drawn...", PORT_LOOK_GREY,
            PORT_LOOK_CENTRE);
        return;
    }
    snprintf(text, sizeof(text), "%s Elimination, %d Entrants",
             bracket->is_double ? "Double" : "Single", bracket->count);
    port_LookTitle(look, text);
    memset(g_tour_boxes, 0, sizeof(g_tour_boxes));
    if (bracket->is_double && bracket->rounds > 1) {
        /* the winners' band over the losers', each as high as its first
         * round needs; the losers' as many columns as it has rounds */
        s32 split = bracket->rounds == 3
                        ? BRACKET_TOP + BAND_HEAD + 4 * WINNERS_ROOM
                        : BRACKET_TOP + (BRACKET_BOTTOM - BRACKET_TOP) / 2;
        s32 losers = 2 * bracket->rounds - 2;
        s32 across = losers > columns ? losers : columns;

        port_TourLayBand(look, bracket, PORT_BRACKET_WINNERS, bracket->rounds,
                         columns, BRACKET_TOP, split);
        port_TourLayFinals(look, bracket, columns, BRACKET_TOP, split);
        /* the rule between the two, under the losers' rounds alone: the
         * finals' column goes on down where they leave it */
        port_LookBox(
            look, BRACKET_LEFT, split + BRACKET_GAP / 2,
            (BRACKET_RIGHT - BRACKET_LEFT) * losers / across - COLUMN_GAP, 1,
            PORT_LOOK_RULE_FAINTER);
        port_TourLayBand(look, bracket, PORT_BRACKET_LOSERS, losers,
                         losers > columns ? losers : columns,
                         split + BRACKET_GAP, BRACKET_BOTTOM);
    } else {
        port_TourLayBand(look, bracket, PORT_BRACKET_WINNERS, bracket->rounds,
                         columns, BRACKET_TOP, BRACKET_BOTTOM);
        port_TourLayFinals(look, bracket, columns, BRACKET_TOP, BRACKET_BOTTOM);
    }
    port_TourDrawLinks(look, bracket);
    for (i = 0; i < bracket->match_count; i++) {
        if (g_tour_boxes[i].shown) {
            port_TourDrawMatch(look, bracket, i, i == next, pulse);
        }
    }
    port_TourDrawChampion(look, bracket);
    /* the rows */
    port_LookBox(look, BRACKET_LEFT, ROW_FIRST_Y - ROW_PITCH + 1,
                 BRACKET_RIGHT - BRACKET_LEFT, 1, PORT_LOOK_RULE_FAINT);
    for (i = 0; i < count; i++) {
        s32 is_arrows;

        port_TournamentPageDescribe(items[i], NULL, label, sizeof(label), value,
                                    sizeof(value), &is_arrows);
        port_LookText(
            look, BRACKET_LEFT + NAME_INSET, ROW_FIRST_Y + i * ROW_PITCH,
            PORT_LOOK_SIZE_TEXT + 1, PORT_FACE_BOLD, label,
            i == cursor ? port_LookPulse(pulse) : PORT_LOOK_ROW,
            PORT_LOOK_LEFT);
        if (value[0] != 0) {
            port_LookFit(look, PORT_LOOK_SIZE_TEXT, PORT_FACE_TEXT, value,
                         (BRACKET_RIGHT - BRACKET_LEFT) / 2);
            port_LookText(look, VALUE_RIGHT, ROW_FIRST_Y + i * ROW_PITCH,
                          PORT_LOOK_SIZE_TEXT, PORT_FACE_TEXT, value,
                          PORT_LOOK_WHITE, PORT_LOOK_RIGHT);
        }
    }
    if (note != NULL && note[0] != 0) {
        snprintf(text, sizeof(text), "%s", note);
    } else {
        port_TourStatusText(text, sizeof(text));
    }
    port_LookFit(look, PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT, text,
                 BRACKET_RIGHT - BRACKET_LEFT);
    port_LookText(
        look, BRACKET_LEFT + NAME_INSET, HELP_Y, PORT_LOOK_SIZE_NOTE,
        PORT_FACE_TEXT, text,
        note != NULL && note[0] != 0 ? PORT_LOOK_WARN : PORT_LOOK_GREY,
        PORT_LOOK_LEFT);
    port_LookText(
        look, PORT_LOOK_GAME_W / 2, KEYS_Y, PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT,
        "Cross / Enter: choose    Circle / Escape: the main menu "
        "(its Tournament comes back here)",
        PORT_LOOK_GREY, PORT_LOOK_CENTRE);
}
