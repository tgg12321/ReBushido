/* The run's two gameplay switches as it asked for them (port_play.h):
 * the window's unlock_all and stage_select_everywhere, or --unlock-all and
 * --stage-select. Outside the state: the game's step takes them in when it
 * next asks (src/port/play.c), and nothing a rollback or run-ahead loads
 * puts them back. In a session the host's hold. The fight's music as the
 * window's fight_music or --fight-music asks (port_fight_music.h) is kept
 * here likewise. */
#include "common.h"
#include "bb2.h"

static struct {
    s32 unlock_all;
    s32 stage_pages;
    s32 held;
} g_port_play_switches;

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

u32 port_PlaySwitchBits(void) {
    return (g_port_play_switches.unlock_all ? PORT_PLAY_BIT_UNLOCK_ALL : 0) |
           (g_port_play_switches.stage_pages ? PORT_PLAY_BIT_STAGE_PAGES : 0);
}

void port_HoldPlaySwitches(u32 bits) {
    g_port_play_switches.held = FALSE;
    port_SetUnlockAll((bits & PORT_PLAY_BIT_UNLOCK_ALL) != 0);
    port_SetStageSelectEverywhere((bits & PORT_PLAY_BIT_STAGE_PAGES) != 0);
    g_port_play_switches.held = TRUE;
}

/* ---- the fight's music (port_fight_music.h) ---- */

/* in the session's word: the rule in the low byte, the row in the next */
#define FIGHT_MUSIC_ROW_SHIFT 8
#define FIGHT_MUSIC_BYTE 0xFF

/* With nothing said the game's own rule, as the console plays it: the
 * window's fight_music is `stage` unless its file says otherwise. */
static struct {
    s32 rule;
    s32 row;
    const char *piece; /* --fight-music KEY: the page starts on it */
    s32 held;
} g_port_fight_music_asked = {.rule = PORT_FIGHT_MUSIC_ORIGINAL};

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
