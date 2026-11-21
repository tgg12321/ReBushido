/* The run's gameplay switches as it asked for them (port_play.h,
 * port_top_menu.h): the window's unlock_all and stage_select_everywhere,
 * or --unlock-all and --stage-select; its menu, bugfixes and the game's
 * three options, or --menu. Outside the state: the game's step takes them
 * in when it next asks (src/port/play.c, src/port/top_menu.c), and nothing
 * a rollback or run-ahead loads puts them back. In a session the host's
 * hold. The fight's music as the window's fight_music or --fight-music
 * asks (port_fight_music.h) is kept here likewise, and the match rule of
 * the window's match_rule and match_count, or --match (port_match.h). */
#include "common.h"
#include "bb2.h"

typedef struct {
    s32 unlock_all;
    s32 stage_pages;
    s32 top_menu;
    s32 bugfixes;
    u32 game_options; /* the save's SAVE_FLAG_... bits */
    s32 held;
} PortPlaySwitches;

static PortPlaySwitches g_port_play_switches;

void port_SetUnlockAll(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.unlock_all = on != 0;
    }
}

void port_SetStageSelectEverywhere(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.stage_pages = on != 0;
    }
}

s32 port_GetUnlockAll(void) { return g_port_play_switches.unlock_all; }

s32 port_GetStageSelectEverywhere(void) {
    return g_port_play_switches.stage_pages;
}

void port_SetTopMenuSwitch(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.top_menu = on != 0;
    }
}

void port_SetBugfixesSwitch(s32 on) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.bugfixes = on != 0;
    }
}

void port_SetGameOptions(u32 flags) {
    if (!g_port_play_switches.held) {
        g_port_play_switches.game_options = flags;
    }
}

/* The menu sounds the window's pages asked for (port_top_menu.h): a few
 * at most between two of the game's inputs, the oldest played first. */
#define ASKED_SOUNDS 4

static struct {
    s32 count;
    s32 sounds[ASKED_SOUNDS];
} g_port_asked_sounds;

void port_AskMenuSound(s32 sound) {
    if (g_port_asked_sounds.count < ASKED_SOUNDS) {
        g_port_asked_sounds.sounds[g_port_asked_sounds.count++] = sound;
    }
}

/* The oldest asked for, taken off the list (port_top_menu.h). */
s32 port_TakeMenuSound(void) {
    s32 sound;
    s32 i;

    if (g_port_asked_sounds.count == 0) {
        return -1;
    }
    sound = g_port_asked_sounds.sounds[0];
    g_port_asked_sounds.count--;
    for (i = 0; i < g_port_asked_sounds.count; i++) {
        g_port_asked_sounds.sounds[i] = g_port_asked_sounds.sounds[i + 1];
    }
    return sound;
}

s32 port_GetTopMenuSwitch(void) { return g_port_play_switches.top_menu; }

s32 port_GetBugfixesSwitch(void) { return g_port_play_switches.bugfixes; }

u32 port_GetGameOptions(void) { return g_port_play_switches.game_options; }

u32 port_PlaySwitchBits(void) {
    return (g_port_play_switches.unlock_all ? PORT_PLAY_BIT_UNLOCK_ALL : 0) |
           (g_port_play_switches.stage_pages ? PORT_PLAY_BIT_STAGE_PAGES : 0) |
           (g_port_play_switches.top_menu ? PORT_PLAY_BIT_TOP_MENU : 0) |
           (g_port_play_switches.bugfixes ? PORT_PLAY_BIT_BUGFIXES : 0) |
           ((g_port_play_switches.game_options & PORT_PLAY_OPTION_MASK)
            << PORT_PLAY_OPTION_SHIFT);
}

void port_HoldPlaySwitches(u32 bits) {
    g_port_play_switches.held = FALSE;
    port_SetUnlockAll((bits & PORT_PLAY_BIT_UNLOCK_ALL) != 0);
    port_SetStageSelectEverywhere((bits & PORT_PLAY_BIT_STAGE_PAGES) != 0);
    port_SetTopMenuSwitch((bits & PORT_PLAY_BIT_TOP_MENU) != 0);
    port_SetBugfixesSwitch((bits & PORT_PLAY_BIT_BUGFIXES) != 0);
    port_SetGameOptions(
        (bits >> PORT_PLAY_OPTION_SHIFT) & PORT_PLAY_OPTION_MASK);
    g_port_play_switches.held = TRUE;
}

/* ---- the fight's music (port_fight_music.h) ---- */

/* in the session's word: the rule in the low byte, the row in the next */
#define FIGHT_MUSIC_ROW_SHIFT 8
#define FIGHT_MUSIC_BYTE 0xFF

/* With nothing said the game's own rule, as the console plays it: the
 * window's fight_music is `stage` unless its file says otherwise. */
typedef struct {
    s32 rule;
    s32 row;
    const char *piece; /* --fight-music KEY: the page starts on it */
    s32 held;
} PortFightMusicAsked;

static PortFightMusicAsked g_port_fight_music_asked = {
    .rule = PORT_FIGHT_MUSIC_ORIGINAL};

/* The player's own, kept while a session begun in the running program
 * holds the host's (online.c). */
static PortPlaySwitches g_port_play_switches_kept;
static PortFightMusicAsked g_port_fight_music_kept;

void port_KeepPlaySwitches(void) {
    g_port_play_switches_kept = g_port_play_switches;
    g_port_fight_music_kept = g_port_fight_music_asked;
}

void port_RestorePlaySwitches(void) {
    g_port_play_switches = g_port_play_switches_kept;
    g_port_fight_music_asked = g_port_fight_music_kept;
}

/* The rule, and the row it starts the page at: None for `none`, the
 * rule's own otherwise. */
void port_SetFightMusic(s32 rule) {
    if (g_port_fight_music_asked.held) {
        return;
    }
    g_port_fight_music_asked.rule =
        rule >= PORT_FIGHT_MUSIC_STAGE && rule <= PORT_FIGHT_MUSIC_NONE
            ? rule
            : PORT_FIGHT_MUSIC_STAGE;
    g_port_fight_music_asked.row =
        rule == PORT_FIGHT_MUSIC_NONE ? PORT_FIGHT_MUSIC_ROW_NONE
                                      : PORT_FIGHT_MUSIC_ROW_OWN;
    g_port_fight_music_asked.piece = NULL;
}

void port_SetFightMusicPiece(const char *key) {
    if (g_port_fight_music_asked.held) {
        return;
    }
    g_port_fight_music_asked.rule = PORT_FIGHT_MUSIC_STAGE;
    g_port_fight_music_asked.row = PORT_FIGHT_MUSIC_ROW_OWN;
    g_port_fight_music_asked.piece = key;
}

s32 port_GetFightMusicRule(void) { return g_port_fight_music_asked.rule; }

/* A piece the command line named is found when it is first asked for:
 * the mods' own are known only once the mods have been read. */
s32 port_GetFightMusicRow(void) {
    const char *key = g_port_fight_music_asked.piece;
    s32 i;

    if (key == NULL) {
        return g_port_fight_music_asked.row;
    }
    g_port_fight_music_asked.piece = NULL;
    for (i = 0; i < PORT_FIGHT_MUSIC_PIECES; i++) {
        if (port_TextsEqual(port_FightMusicPieceKey(i), key)) {
            g_port_fight_music_asked.row = PORT_FIGHT_MUSIC_ROW_FIRST_PIECE + i;
            return g_port_fight_music_asked.row;
        }
    }
    for (i = 0; i < port_MusicExtraCount(); i++) {
        if (port_TextsEqual(port_MusicExtraKey(i), key)) {
            g_port_fight_music_asked.row = PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA + i;
            return g_port_fight_music_asked.row;
        }
    }
    port_Log("--fight-music: no piece of the game's or the mods' is %s; the "
             "stage's own\n",
             key);
    return g_port_fight_music_asked.row;
}

u32 port_FightMusicBits(void) {
    return (u32)g_port_fight_music_asked.rule |
           (u32)port_GetFightMusicRow() << FIGHT_MUSIC_ROW_SHIFT;
}

void port_HoldFightMusic(u32 bits) {
    s32 row = (s32)(bits >> FIGHT_MUSIC_ROW_SHIFT & FIGHT_MUSIC_BYTE);

    g_port_fight_music_asked.held = FALSE;
    port_SetFightMusic((s32)(bits & FIGHT_MUSIC_BYTE));
    /* a piece of the host's mods is none of the guest's: the stage's own
     * on both */
    g_port_fight_music_asked.row =
        row < PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA ? row : PORT_FIGHT_MUSIC_ROW_OWN;
    g_port_fight_music_asked.held = TRUE;
}

/* ---- the match rule (port_match.h) ---- */

/* in the session's word: the rule in the low byte, the number above */
#define MATCH_COUNT_SHIFT 8
#define MATCH_BYTE 0xFF

typedef struct {
    s32 rule;
    s32 count;
    s32 held;
} PortMatchAsked;

static PortMatchAsked g_port_match_asked = {
    PORT_MATCH_SINGLE, PORT_MATCH_COUNT_DEFAULT, FALSE};

/* The window's or the command line's rule and number, each kept to what
 * there is; nothing once the session holds the host's. */
void port_SetMatchDefault(s32 rule, s32 count) {
    if (g_port_match_asked.held) {
        return;
    }
    g_port_match_asked.rule =
        rule >= PORT_MATCH_SINGLE && rule < PORT_MATCH_RULES
            ? rule
            : PORT_MATCH_SINGLE;
    g_port_match_asked.count =
        count >= PORT_MATCH_COUNT_LEAST && count <= PORT_MATCH_COUNT_MOST
            ? count
            : PORT_MATCH_COUNT_DEFAULT;
}

s32 port_GetMatchRule(void) { return g_port_match_asked.rule; }

s32 port_GetMatchCount(void) { return g_port_match_asked.count; }

u32 port_MatchBits(void) {
    return (u32)g_port_match_asked.rule |
           (u32)g_port_match_asked.count << MATCH_COUNT_SHIFT;
}

/* The rule as it was before a ranked match held the ladder's
 * (online.c), put back after. */
static PortMatchAsked g_port_match_kept;

void port_KeepMatch(void) { g_port_match_kept = g_port_match_asked; }

void port_RestoreMatch(void) { g_port_match_asked = g_port_match_kept; }

void port_HoldMatch(u32 bits) {
    g_port_match_asked.held = FALSE;
    port_SetMatchDefault((s32)(bits & MATCH_BYTE),
                         (s32)(bits >> MATCH_COUNT_SHIFT & MATCH_BYTE));
    g_port_match_asked.held = TRUE;
}
