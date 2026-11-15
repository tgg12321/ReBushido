/* The native port's window: what the session's log says of the game
 * (window.h; docs/PORT.md, "The window"). The window's SDL files cannot
 * see the game's variables (they are compiled with a C library's
 * headers, and the game's names are the game's object's own), so this
 * file, which is compiled as the port's own are, reads them out for
 * those files and has the names the log gives things. It only reads. */
#include "common.h"
#include "window.h"
#include "bb2.h"

#define NAME_COUNT(names) ((s32)(sizeof(names) / sizeof((names)[0])))

/* enum Screen (sys.h). */
static const char * const g_port_screen_names[] = {
    "fight (loading)",
    "fight",
    "debug menu (set-up)",
    "debug menu",
    "replay (set-up)",
    "replay",
    "cutscene (set-up)",
    "cutscene",
    "menu (set-up)",
    "menu",
    "staff roll (set-up)",
    "staff roll",
    "after fight (set-up)",
    "after fight",
    "unused",
    "movie",
    "winner (set-up)",
    "winner",
    "ending (set-up)",
    "ending",
    "team result (set-up)",
    "team result",
    "stage card (set-up)",
    "stage card",
    "after match (set-up)",
    "after match",
    "save (set-up)",
    "save",
    "kaishaku (set-up)",
    "kaishaku",
    "best times (set-up)",
    "best times",
    "kill count (set-up)",
    "kill count",
};

/* enum FightMode (fight.h). */
static const char * const g_port_mode_names[] = {
    "story", "vs CPU", "practice", "chanbara", "vs", "team", "network",
};

/* The roster (fighter.h). */
static const char * const g_port_roster_names[] = {
    "Kannuki (story)",
    "Tatsumi (story)",
    "Mikado (story)",
    "Kannuki",
    "Tatsumi",
    "Mikado",
    "Utsusemi",
    "Red Shadow",
    "Tsubame",
    "Sazanka",
    "Suminagashi",
    "Nightstalker",
    "Matsumushi",
    "the vanisher",
    "sidearm 1",
    "Gengoro (story)",
    "Kaun (story)",
    "Jo (story)",
    "Gengoro",
    "Kaun",
    "Jo",
    "Hongou",
    "Katze",
    "Highwayman",
    "Chihiro",
    "Utamaru",
    "Tony Umeda",
    "Isohachi",
    "the armored one",
    "sidearm 2",
    "last 1",
    "the victim",
    "last 2",
};

/* enum WeaponId (weapon.h). */
static const char * const g_port_weapon_names[] = {
    "katana",   "long sword", "nodachi",  "broadsword",
    "naginata", "yari",       "long gun", "short gun",
};

/* enum StageIndex (stage.h). */
static const char * const g_port_stage_names[] = {
    "Main Tatara Shrine",   "Meikyokan Dojo",     "Shainto HQ",
    "Tatara Shrine",        "Bus Stop",           "Lost Bridge",
    "Tatara Shrine Path",   "Bamboo Thicket",     "Outside the Meikyokan",
    "Future Dump Site",     "Snake Cliff",        "Kimigaura Coast",
    "Orange Tree Fields",   "Castle Rooftop",     "Under Lost Bridge",
    "Terraced Fields",      "Under Railway",      "Water Tower Dam",
    "Lumberyard",           "Lighthouse",         "Kabuki Stage",
    "Abandoned Ship",       "Ferry Dock",         "Shainto Gate",
    "Purple Cavern",        "Abandoned Station",  "Railway",
    "Abandoned Tunnel",     "Abandoned Shipyard", "Building Rooftop",
    "Building Parking Lot", "Shainto Cemetery",   "team battle 1",
    "team battle 2",        "chanbara's hall",    "practice 1",
    "practice 2",
};

static const char *port_NameOf(
    const char * const *names, s32 count, s32 index) {
    return index >= 0 && index < count ? names[index] : "?";
}

const char *port_NameScreen(s32 screen) {
    return port_NameOf(
        g_port_screen_names, NAME_COUNT(g_port_screen_names), screen);
}

const char *port_NameMode(s32 mode) {
    return port_NameOf(g_port_mode_names, NAME_COUNT(g_port_mode_names), mode);
}

const char *port_NameRoster(s32 roster) {
    return port_NameOf(
        g_port_roster_names, NAME_COUNT(g_port_roster_names), roster);
}

const char *port_NameWeapon(s32 weapon) {
    return port_NameOf(
        g_port_weapon_names, NAME_COUNT(g_port_weapon_names), weapon);
}

const char *port_NameStage(s32 stage) {
    return port_NameOf(
        g_port_stage_names, NAME_COUNT(g_port_stage_names), stage);
}

/* `ReBushido VERSION (COMMIT)`, as the options page has it. */
const char *port_GetVersionText(void) { return g_rb_version; }

/* The game as it is now (window.h). */
void port_ReadGameState(PortGameState *state) {
    s32 i;

    state->screen = g_sys_screen;
    state->in_fight = g_sys_screen == SCREEN_FIGHT_UPDATE;
    state->frames = g_disp_frame_count;
    state->mode = g_fight_mode;
    state->stage = g_fight_stage;
    state->winner = g_fight_winner;
    state->defeat_frames = g_fight_defeat_frames;
    for (i = 0; i < 2; i++) {
        state->roster[i] = g_fighters[i].roster_id;
        state->weapon[i] = g_fighters[i].weapon_id;
        state->is_cpu[i] = g_fighters[i].is_cpu;
    }
}
