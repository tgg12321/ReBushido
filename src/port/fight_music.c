/* The choice of a fight's music (port_fight_music.h; docs/PORT.md, "A
 * fight's music"). A state file: the row the stage pages left and what
 * the fight's start made of it are saved, put back and hashed with the
 * game's own variables, so a rollback puts the choice back and the two
 * machines of a session choose the same. Nothing here draws: the window
 * lays the row over the stage pages from what this file keeps
 * (src/port/window/stage_page.c). */
#include "common.h"
#include "bb2.h"

PortFightMusic g_port_fight_music;

/* The game's pieces, by their place in the row: the twelve tracks of
 * cdrom.c, then the staff roll's. Named by where the game plays them. */
static const char * const g_port_fight_music_keys[PORT_FIGHT_MUSIC_PIECES] = {
    "bb2_m1-0", "bb2_m1-1", "bb2_m1-2", "bb2_m1-3", "bb2_m1-4",
    "bb2_m1-5", "bb2_m1-6", "bb2_m1-7", "bb2_m2-0", "bb2_m2-1",
    "bb2_m2-2", "bb2_m2-3", "bb2_m2-4",
};
static const char * const g_port_fight_music_names[PORT_FIGHT_MUSIC_PIECES] = {
    "Tatara Shrine",        /* the narration's, and most stages' */
    "Select screen",        /* the menu's pages */
    "Snake Cliff",          /* the coast, the docks, the Shainto's */
    "Bamboo Thicket",       /* the cavern, the station, the rooftops */
    "Meikyokan Dojo",       /* the castle, the railway, the tunnel */
    "Unheard I",            /* on the disc; nothing asks for it */
    "Unheard II",           /* likewise */
    "Title menu",           /* the top menu */
    "Chanbara: Narukagami", /* chanbara, the Narukagami's fighter */
    "Chanbara: Shainto",    /* and the Shainto's */
    "Practice round",       /* a round of practice */
    "Practice",             /* practice */
    "Staff roll",           /* the staff roll */
};

/* The modes whose fights come from a stage page (as stage_choice.c's):
 * versus and link, and versus the CPU and training when their select
 * screen went on to the stage pages (port_play.h). */
static s32 port_FightMusicModeChooses(s32 mode) {
    return mode == FIGHT_MODE_VS || mode == FIGHT_MODE_NETWORK ||
           port_PlayStageChosen(mode);
}

/* The row the setting starts the page at, a piece of a mod's that is no
 * longer offered the rule's own. */
static s32 port_FightMusicDefaultRow(void) {
    s32 row = port_GetFightMusicRow();

    if (row >= PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA &&
        row - PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA >= port_MusicExtraCount()) {
        return PORT_FIGHT_MUSIC_ROW_OWN;
    }
    return row;
}

/* Made the first time the game asks; the rule taken in whenever it
 * does. */
static PortFightMusic *port_FightMusic(void) {
    PortFightMusic *music = &g_port_fight_music;

    if (!music->is_started) {
        music->is_started = TRUE;
        music->row = (u8)port_FightMusicDefaultRow();
        music->track = -1;
    }
    music->rule = (u8)port_GetFightMusicRule();
    return music;
}

/* The row's places in the order L1 and R1 go through them: the rule's
 * own, the game's pieces, the mods', None. */
static s32 port_FightMusicPlace(s32 row) {
    if (row == PORT_FIGHT_MUSIC_ROW_NONE) {
        return 1 + PORT_FIGHT_MUSIC_PIECES + port_MusicExtraCount();
    }
    if (row >= PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA) {
        return 1 + PORT_FIGHT_MUSIC_PIECES +
               (row - PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA);
    }
    return row;
}

static s32 port_FightMusicRowAt(s32 place) {
    if (place <= PORT_FIGHT_MUSIC_PIECES) {
        return place;
    }
    if (place == 1 + PORT_FIGHT_MUSIC_PIECES + port_MusicExtraCount()) {
        return PORT_FIGHT_MUSIC_ROW_NONE;
    }
    return PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA +
           (place - 1 - PORT_FIGHT_MUSIC_PIECES);
}

void port_FightMusicReset(void) {
    PortFightMusic *music = port_FightMusic();

    music->row = (u8)port_FightMusicDefaultRow();
}

/* A frame of a stage page (port_fight_music.h): R1 a place on, L1 a
 * place back, round from None to the rule's own and back. */
s32 port_FightMusicPageStep(s32 pressed, s32 is_open) {
    PortFightMusic *music = port_FightMusic();
    s32 count = 2 + PORT_FIGHT_MUSIC_PIECES + port_MusicExtraCount();
    s32 place;

    if (!is_open || !(pressed & PAD_ANY(PADL1 | PADR1))) {
        return FALSE;
    }
    place = port_FightMusicPlace(music->row);
    if (place >= count) {
        place = 0;
    }
    place += (pressed & PAD_ANY(PADR1)) ? 1 : count - 1;
    music->row = (u8)port_FightMusicRowAt(place % count);
    snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    return TRUE;
}

/* The fight's start (port_fight_music.h): the row of a mode whose stage
 * the pages chose, the setting's otherwise, made a track, nothing, or
 * the game's own switch; and what it heard, for the ambience. */
s32 port_FightMusicStart(s32 mode, s32 stage, s32 music_off) {
    PortFightMusic *music = port_FightMusic();
    s32 row = port_FightMusicModeChooses(mode) ? music->row
                                               : port_FightMusicDefaultRow();
    s32 own = stage >= 0 && stage < STAGE_COUNT
                  ? port_ArenaMusic(stage, g_snd_stage_music_tracks[stage])
                  : PORT_FIGHT_MUSIC_GAME;
    /* the stage's own piece of a mod's (port_MusicStagePiece), heard over
     * the stage's stream; an arena is a place of its own, and keeps its
     * host's stream */
    const char *stage_piece =
        own != PORT_FIGHT_MUSIC_GAME && port_StageArena() == PORT_STAGE_NO_ARENA
            ? port_MusicStagePiece(stage)
            : NULL;
    s32 track;

    music->heard = PORT_FIGHT_MUSIC_HEARD_GAME;
    music->track = -1;
    port_MusicSetFightPiece(NULL, NULL);
    /* the options' music off: the game's own, which is then quiet */
    if (music_off) {
        return PORT_FIGHT_MUSIC_GAME;
    }
    /* the game's own rule, and practice's and chanbara's own pieces
     * under the stage's (`none` is a rule of the stage's that starts the
     * row at None) */
    if (row == PORT_FIGHT_MUSIC_ROW_OWN &&
        (music->rule == PORT_FIGHT_MUSIC_ORIGINAL ||
         mode == FIGHT_MODE_PRACTICE || mode == FIGHT_MODE_CHANBARA ||
         own == PORT_FIGHT_MUSIC_GAME)) {
        /* versus, whose fight the game itself gives the stage's stream
         * (snd_StartFightMusic): the stage's own piece over it */
        if (mode == FIGHT_MODE_VS && stage_piece != NULL) {
            port_MusicSetFightPiece(port_FightMusicPieceKey(own), stage_piece);
        }
        return PORT_FIGHT_MUSIC_GAME;
    }
    if (row == PORT_FIGHT_MUSIC_ROW_NONE) {
        music->heard = PORT_FIGHT_MUSIC_HEARD_QUIET;
        return PORT_FIGHT_MUSIC_QUIET;
    }
    if (row == PORT_FIGHT_MUSIC_ROW_OWN) {
        track = own;
        if (stage_piece != NULL) {
            port_MusicSetFightPiece(port_FightMusicPieceKey(own), stage_piece);
        }
    } else if (row < PORT_FIGHT_MUSIC_ROW_NONE) {
        /* a track of cdrom.c's, or the staff roll's piece */
        track = row - PORT_FIGHT_MUSIC_ROW_FIRST_PIECE;
    } else {
        /* a mod's piece, heard over the stage's own stream (or the
         * first piece's where the stage has none), which the game plays
         * as ever */
        track = own == PORT_FIGHT_MUSIC_GAME ? 0 : own;
        if (row - PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA < port_MusicExtraCount()) {
            port_MusicSetFightPiece(
                port_FightMusicPieceKey(track),
                port_MusicExtraKey(row - PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA));
        }
    }
    music->heard = PORT_FIGHT_MUSIC_HEARD_PIECE;
    music->track = (s8)track;
    return track;
}

s32 port_FightMusicAmbience(void) {
    switch (g_port_fight_music.heard) {
    case PORT_FIGHT_MUSIC_HEARD_PIECE:
        return 0;
    case PORT_FIGHT_MUSIC_HEARD_QUIET:
        return 1;
    default:
        return -1;
    }
}

/* ---- what the window draws ---- */

const char *port_FightMusicPieceKey(s32 piece) {
    return piece >= 0 && piece < PORT_FIGHT_MUSIC_PIECES
               ? g_port_fight_music_keys[piece]
               : NULL;
}

/* What the row's place is called: the rule's own by the rule, a piece of
 * the game's by where the game plays it, a mod's by its key. */
const char *port_FightMusicRowName(s32 row) {
    if (row == PORT_FIGHT_MUSIC_ROW_OWN) {
        return port_GetFightMusicRule() == PORT_FIGHT_MUSIC_ORIGINAL
                   ? "Game's own"
                   : "Stage's own";
    }
    if (row == PORT_FIGHT_MUSIC_ROW_NONE) {
        return "None";
    }
    if (row < PORT_FIGHT_MUSIC_ROW_NONE) {
        return g_port_fight_music_names[row - PORT_FIGHT_MUSIC_ROW_FIRST_PIECE];
    }
    return port_FightMusicRowKey(row);
}

/* The key of the piece at the row's place, empty for the rule's own and
 * None. */
const char *port_FightMusicRowKey(s32 row) {
    const char *key;

    if (row > PORT_FIGHT_MUSIC_ROW_OWN && row < PORT_FIGHT_MUSIC_ROW_NONE) {
        return g_port_fight_music_keys[row - PORT_FIGHT_MUSIC_ROW_FIRST_PIECE];
    }
    if (row >= PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA) {
        key = port_MusicExtraKey(row - PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA);
        return key != NULL ? key : "";
    }
    return "";
}
