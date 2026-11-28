/* The native port: the fight `--fight STAGE FIGHTER FIGHTER` starts in
 * place of the options page, the boot image, the opening and the menus
 * (port_platform.h; docs/PORT.md, "Straight into a fight"), as the
 * PlayStation's quick-boot build starts the one it was built for
 * (docs/REBUSHIDO.md, "Booting straight into a fight"). The names are the
 * game's, written any way: `tatara-shrine`, `Tatara Shrine`,
 * `STAGE_TATARA_SHRINE` and `3` are the same stage, and a fighter may name
 * a weapon after a colon (`kannuki:nodachi`).
 *
 * Nothing here is the game's state (config/port_state.txt): the choice is
 * made once as the program starts, and the game copies it into its own
 * variables when it starts the fight. */
#include "common.h"
#include "bb2.h"

#define NAME_BYTES_MAX 48
/* the stage of --fight while it is an arena's name not yet looked for */
#define FIGHT_STAGE_ARENA (-2)
#define DECIMAL 10
#define COUNT(array) ((s32)(sizeof(array) / sizeof((array)[0])))

/* enum StageIndex (stage.h), as the game's own cards and the log name them. */
static const char * const g_port_fight_stages[] = {
    "main tatara shrine",
    "meikyokan dojo",
    "shainto hq",
    "tatara shrine",
    "bus stop",
    "lost bridge",
    "tatara shrine path",
    "bamboo thicket",
    "outside meikyokan",
    "future dump site",
    "snake cliff",
    "kimigaura coast",
    "orange tree fields",
    "castle rooftop",
    "under lost bridge",
    "terraced fields",
    "under railway",
    "water tower dam",
    "lumberyard",
    "lighthouse",
    "kabuki stage",
    "abandoned ship",
    "ferry dock",
    "shainto gate",
    "purple cavern",
    "abandoned station",
    "railway",
    "abandoned tunnel",
    "abandoned shipyard",
    "building rooftop",
    "building parking lot",
    "shainto cemetery",
    "team 1",
    "team 2",
    "chanbara",
    "practice 1",
    "practice 2",
    /* the castle rooftop as link mode fights on it (select.c,
     * SELECT_STAGE_LINK_TO): a stage of its own, a model of a castle town
     * below */
    "castle rooftop link",
};

/* enum RosterId (fighter.h). */
static const char * const g_port_fight_roster[] = {
    "kannuki story", "tatsumi story", "mikado story",
    "kannuki",       "tatsumi",       "mikado",
    "utsusemi",      "red shadow",    "tsubame",
    "sazanka",       "suminagashi",   "nightstalker",
    "matsumushi",    "vanisher",      "sidearm 1",
    "gengoro story", "kaun story",    "jo story",
    "gengoro",       "kaun",          "jo",
    "hongou",        "katze",         "highwayman",
    "chihiro",       "utamaru",       "tony umeda",
    "isohachi",      "armored",       "sidearm 2",
    "last 1",        "victim",        "last 2",
};

/* enum WeaponId (weapon.h). */
static const char * const g_port_fight_weapons[] = {
    "katana",   "long sword", "nodachi",  "broadsword",
    "naginata", "yari",       "long gun", "short gun",
};

/* What the stage's, fighter's or weapon's names are, for a mistake. */
static void port_FightList(const char * const *names, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        port_Log("  %2d %s\n", i, names[i]);
    }
}

static PortFight g_port_fight;
static s32 g_port_fight_chosen;

/* A name as it is compared: lower case, letters and digits alone, from
 * `text` up to `end` (or its own end), with the enum's prefix (`stage_`,
 * `roster_`, `weapon_id_`) left out. */
static void port_FightKey(
    const char *text, const char *end, const char *prefix, char *out) {
    u32 length = 0;
    const char *at;

    for (at = prefix; *at != 0 && text != end; at++, text++) {
        char letter =
            *text >= 'A' && *text <= 'Z' ? (char)(*text - 'A' + 'a') : *text;

        if (letter != *at) {
            break;
        }
    }
    if (*at != 0) {
        /* not the prefix after all: from the start */
        text -= at - prefix;
    }
    for (; *text != 0 && text != end && length + 1 < NAME_BYTES_MAX; text++) {
        char letter = *text;

        if (letter >= 'A' && letter <= 'Z') {
            letter = (char)(letter - 'A' + 'a');
        }
        if ((letter >= 'a' && letter <= 'z') ||
            (letter >= '0' && letter <= '9')) {
            out[length++] = letter;
        }
    }
    out[length] = 0;
}

static s32 port_FightKeysEqual(const char *first, const char *second) {
    while (*first != 0 && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

/* The index of the name in `names`, or -1. A number is the index itself. */
static s32 port_FightFind(const char *text, const char *end, const char *prefix,
                          const char * const *names, s32 count) {
    char key[NAME_BYTES_MAX];
    char name[NAME_BYTES_MAX];
    s32 number = 0;
    s32 digits = 0;
    const char *at;
    s32 i;

    for (at = text; *at != 0 && at != end; at++) {
        if (*at < '0' || *at > '9') {
            break;
        }
        number = number * DECIMAL + (*at - '0');
        digits++;
    }
    if (digits > 0 && (*at == 0 || at == end)) {
        return number < count ? number : -1;
    }
    port_FightKey(text, end, prefix, key);
    for (i = 0; i < count; i++) {
        port_FightKey(names[i], NULL, "", name);
        if (port_FightKeysEqual(key, name)) {
            return i;
        }
    }
    return -1;
}

/* One fighter: NAME or NAME:WEAPON, into `roster` and `weapon`. */
static s32 port_FightFighterInto(const char *text, s32 *roster, s32 *weapon) {
    const char *colon = text;

    while (*colon != 0 && *colon != ':') {
        colon++;
    }
    *roster = port_FightFind(text, colon, "roster_", g_port_fight_roster,
                             COUNT(g_port_fight_roster));
    *weapon = WEAPON_ID_KATANA;
    if (*colon == ':') {
        *weapon =
            port_FightFind(colon + 1, NULL, "weapon_id_", g_port_fight_weapons,
                           COUNT(g_port_fight_weapons));
    }
    return *roster >= 0 && *weapon >= 0;
}

/* ... into the side's place. */
static s32 port_FightFighter(const char *text, s32 side) {
    return port_FightFighterInto(
        text, &g_port_fight.roster[side], &g_port_fight.weapon[side]);
}

/* Two on two's seconds of the fight (port_platform.h). */
s32 port_ChooseFightPartners(const char *first, const char *second) {
    if (!port_FightFighterInto(first, &g_port_fight.partner_roster[0],
                               &g_port_fight.partner_weapon[0]) ||
        !port_FightFighterInto(second, &g_port_fight.partner_roster[1],
                               &g_port_fight.partner_weapon[1])) {
        port_Log("--partners: %s or %s is not a fighter (NAME or "
                 "NAME:WEAPON); the fighters:\n",
                 first, second);
        port_FightList(g_port_fight_roster, COUNT(g_port_fight_roster));
        return FALSE;
    }
    g_port_fight.has_partners = TRUE;
    return TRUE;
}

/* The fight to start (port_platform.h). */
s32 port_ChooseFight(const char *stage, const char *first, const char *second) {
    g_port_fight.mode = FIGHT_MODE_VS;
    g_port_fight.is_cpu[1] = TRUE;
    g_port_fight.stage = port_FightFind(
        stage, NULL, "stage_", g_port_fight_stages, COUNT(g_port_fight_stages));
    if (g_port_fight.stage < 0 && port_ArenaAsk(stage)) {
        /* a mod's arena, perhaps: port_ArenasStart says, and the fight is
         * on the stage it stands in for (port_QuickFight) */
        g_port_fight.stage = FIGHT_STAGE_ARENA;
    }
    if (g_port_fight.stage < 0 && g_port_fight.stage != FIGHT_STAGE_ARENA) {
        port_Log("--fight: no stage is called %s; the stages:\n", stage);
        port_FightList(g_port_fight_stages, COUNT(g_port_fight_stages));
        return FALSE;
    }
    if (!port_FightFighter(first, 0) || !port_FightFighter(second, 1)) {
        port_Log("--fight: %s or %s is not a fighter (NAME or NAME:WEAPON); "
                 "the fighters:\n",
                 first, second);
        port_FightList(g_port_fight_roster, COUNT(g_port_fight_roster));
        port_Log("and the weapons:\n");
        port_FightList(g_port_fight_weapons, COUNT(g_port_fight_weapons));
        return FALSE;
    }
    g_port_fight_chosen = TRUE;
    return TRUE;
}

/* Both fighters played from the pads (--fight-2p): the second is the
 * second pad's, not the CPU's. */
void port_FightTwoPlayers(void) { g_port_fight.is_cpu[1] = FALSE; }

/* Both fighters the CPU's (--fight-cpu): a fight that plays itself. */
void port_FightBothCpu(void) { g_port_fight.is_cpu[0] = TRUE; }

/* The fight the run was begun with, as it was asked (port_platform.h):
 * for a replay's lead-in, which keeps it. */
s32 port_FightAsked(PortFight *fight) {
    if (g_port_fight_chosen) {
        *fight = g_port_fight;
    }
    return g_port_fight_chosen;
}

/* A replay's playback from power on: the fight its run was begun with
 * (NULL: none) held in place of this run's own, which is put back
 * after. */
static PortFight g_port_fight_own;
static s32 g_port_fight_own_chosen;
static s32 g_port_fight_held;

void port_HoldFight(const PortFight *fight) {
    if (!g_port_fight_held) {
        g_port_fight_own = g_port_fight;
        g_port_fight_own_chosen = g_port_fight_chosen;
        g_port_fight_held = TRUE;
    }
    g_port_fight_chosen = fight != NULL;
    if (fight != NULL) {
        g_port_fight = *fight;
    }
}

void port_ReleaseFight(void) {
    if (g_port_fight_held) {
        g_port_fight = g_port_fight_own;
        g_port_fight_chosen = g_port_fight_own_chosen;
        g_port_fight_held = FALSE;
    }
}

const PortFight *port_QuickFight(void) {
    if (g_port_fight_chosen && g_port_fight.stage == FIGHT_STAGE_ARENA) {
        const PortArena *arena = port_ArenaInPlay();

        /* port_ArenasStart has ended the run if no mod has it */
        g_port_fight.stage = arena != NULL ? arena->host : 0;
    }
    return g_port_fight_chosen ? &g_port_fight : NULL;
}
