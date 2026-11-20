#ifndef PORT_FIGHT_MUSIC_H
#define PORT_FIGHT_MUSIC_H

/* The native port's choice of a fight's music (docs/PORT.md, "A fight's
 * music"; docs/PLAYING.md, "Music on the stage select"). The original
 * plays music over a fight only in versus (the stage's piece,
 * g_snd_stage_music_tracks), practice and chanbara (pieces of their own),
 * and none in story, versus the CPU and team battle, which have the
 * stage's ambient sounds (snd_StartFightMusic, snd_AmbienceEnabled).
 *
 * The window's `fight_music` says what a fight starts with:
 *
 * - stage: the stage's versus piece in every mode (a mod's arena's own
 *   track where it has one), practice and chanbara keeping their own;
 * - original: the game's own rule;
 * - none: no music, the ambient sounds alone (the row starts at None;
 *   its first place is then the stage's own).
 *
 * On a stage page L1 and R1 go through a row: the rule's own ("Stage's
 * own", or "Game's own" with `original`), then every piece of the game's
 * music (PORT_FIGHT_MUSIC_PIECES, the two nothing asks for among them),
 * then the pieces a music mod brings under keys of its own (docs/MODS.md,
 * "Music"), then None. The row starts from the setting each time the
 * select screen loads, and what it is left at is the fight's, and the
 * rest of a versus the CPU run's, whose stage carries through. A fight
 * with music has no ambient sounds and one with none has them, as versus
 * does with the music on and off.
 *
 * What the row holds is the game's state (src/port/fight_music.c is a
 * state file): rolled back with the game, and the same on both machines
 * of a session, whose presses are. The pieces of a mod are left out of
 * the row in a session, where two peers' mods may differ; the setting is
 * the host's. The PlayStation builds never see this header. */

#include "common.h"

/* The rule of a fight with no piece chosen: the setting's words. */
enum {
    PORT_FIGHT_MUSIC_STAGE = 0,
    PORT_FIGHT_MUSIC_ORIGINAL = 1,
    PORT_FIGHT_MUSIC_NONE = 2,
};

/* The game's own pieces: the twelve music tracks of cdrom.c (bb2_m1-0 to
 * -7, bb2_m2-0 to -3) and the staff roll's (bb2_m2-4). */
#define PORT_FIGHT_MUSIC_PIECES 13
#define PORT_FIGHT_MUSIC_STAFF_ROLL 12
/* its stream of BB2_M2.XA (fight_after.c's STAFF_MUSIC_CHANNEL) */
#define PORT_FIGHT_MUSIC_STAFF_ROLL_CHANNEL 4
/* the most pieces of mods the row offers */
#define PORT_FIGHT_MUSIC_EXTRAS_MAX 32

/* What the row holds (PortFightMusic.row): the rule's own, a piece of
 * the game's (FIRST_PIECE + its number), None, or a mod's piece
 * (FIRST_EXTRA + its number among port_MusicExtraKey's). */
enum {
    PORT_FIGHT_MUSIC_ROW_OWN = 0,
    PORT_FIGHT_MUSIC_ROW_FIRST_PIECE = 1,
    PORT_FIGHT_MUSIC_ROW_NONE =
        PORT_FIGHT_MUSIC_ROW_FIRST_PIECE + PORT_FIGHT_MUSIC_PIECES,
    PORT_FIGHT_MUSIC_ROW_FIRST_EXTRA = PORT_FIGHT_MUSIC_ROW_NONE + 1,
};

/* What a fight's start heard of the choice (PortFightMusic.heard). */
enum {
    PORT_FIGHT_MUSIC_HEARD_GAME = 0,  /* the game's own: its ambience too */
    PORT_FIGHT_MUSIC_HEARD_PIECE = 1, /* a piece: no ambient sounds */
    PORT_FIGHT_MUSIC_HEARD_QUIET = 2, /* none: the ambient sounds */
};

typedef struct {
    u8 row;   /* PORT_FIGHT_MUSIC_ROW_... the page left */
    u8 rule;  /* the setting's PORT_FIGHT_MUSIC_ rule, as last taken */
    u8 heard; /* PORT_FIGHT_MUSIC_HEARD_..., at the fight's start */
    s8 track; /* the track the fight started (-1: none or the game's) */
    u8 is_started;
} PortFightMusic;

extern PortFightMusic g_port_fight_music;

/* What snd_StartFightMusic starts, as port_FightMusicStart says: a track
 * of cdrom.c (0 to 11) or the staff roll's piece, or these. */
#define PORT_FIGHT_MUSIC_GAME (-2)  /* the game's own switch */
#define PORT_FIGHT_MUSIC_QUIET (-1) /* nothing */

/* ---- what the game calls (under RB_PORT) ---- */

/* The select screen loads (select_Load): the row starts from the
 * setting. */
extern void port_FightMusicReset(void);
/* A frame of a stage page, after the port's kinds of stage have had the
 * presses (select_StageStep): L1 and R1 move the row while the door is
 * open. TRUE if it took a press. */
extern s32 port_FightMusicPageStep(s32 pressed, s32 is_open);
/* The fight starts (snd_StartFightMusic): what to start, for `mode` on
 * `stage`, the game's music off (the options' setting) or not. */
extern s32 port_FightMusicStart(s32 mode, s32 stage, s32 music_off);
/* Whether the fight has its ambient sounds (snd_AmbienceEnabled): 1 or 0,
 * or -1 for the game's own rule. */
extern s32 port_FightMusicAmbience(void);

/* ---- what the window draws (src/port/window/stage_page.c) ---- */

/* The row's text for what it holds, and the piece's key (empty for the
 * rule's own and None). */
extern const char *port_FightMusicRowName(s32 row);
extern const char *port_FightMusicRowKey(s32 row);
/* The key of a piece of the game's (bb2_m1-0), or NULL. */
extern const char *port_FightMusicPieceKey(s32 piece);

/* ---- the run's choice, outside the state (src/port/play_switches.c) ---- */

/* The window's fight_music, or --fight-music: a rule (`original` if
 * nothing says, as the console plays), and the row the page starts at
 * for it (PORT_FIGHT_MUSIC_ROW_...). --fight-music KEY names a piece, of
 * the game's or a mod's, for the row to start at under the stage's rule.
 * Read by the game's step as it needs them. */
extern void port_SetFightMusic(s32 rule);
extern void port_SetFightMusicPiece(const char *key);
extern s32 port_GetFightMusicRule(void);
extern s32 port_GetFightMusicRow(void);
/* As a word for the session's welcome, and the session holding it: the
 * setters change nothing after. */
extern u32 port_FightMusicBits(void);
extern void port_HoldFightMusic(u32 bits);

/* ---- a mod's own pieces (src/port/music.c) ---- */

/* The pieces of the mods' music whose keys are no stream of the disc's,
 * in the order the mods were read: how many (none in a session; the
 * same for the whole run otherwise, music_mods = off or on), and each
 * one's key. */
extern s32 port_MusicExtraCount(void);
extern const char *port_MusicExtraKey(s32 index);
/* The fight's stream, `carrier` (a key of the disc's), is heard as the
 * mod's piece `extra` the next time the drive starts it; NULL for none.
 * Nothing of the game's: the disc's stream plays and goes to the chip as
 * before, so two peers are the same whatever their mods. */
extern void port_MusicSetFightPiece(const char *carrier, const char *extra);

#endif /* PORT_FIGHT_MUSIC_H */
