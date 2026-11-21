#ifndef PORT_REPLAY_H
#define PORT_REPLAY_H

/* The native port's replays (docs/PORT.md, "Replays"; src/port/
 * replay.c): every fight kept as a small file and played back by running
 * the game again, which is deterministic (docs/NETPLAY.md, section 1).
 *
 * A fight is from the first frame boundary whose screen is the fight
 * (SCREEN_FIGHT_UPDATE) to the first that is not. Its file holds the
 * state at the boundary before that one, the last before the fight's load
 * (port_state.h), compressed, but for what
 * is this run's own (config/port_state.txt's `run` group, the hooks the
 * host set), then every input the game took from there (port_TakeInput)
 * with what it read of the picture (port_RemasterGameView), and the
 * state's hash every REPLAY_HASH_EVERY frames. Playing it back puts
 * that state in, gives the game the inputs again one by one, and holds
 * every hash to the one recorded: a hash that differs is a desync, and
 * the playback stops there and says so.
 *
 * Only a program of the same build, from the same disc, with the same
 * gameplay mods plays one (the sums netplay holds two programs to,
 * docs/NETPLAY.md, section 13); and one with the port's own primitive
 * buffers where the fight had them (a draw distance past the console's
 * when it started). */

#include "common.h"

struct PortInput;
struct PortDisc;

#define PORT_REPLAY_HASH_EVERY 30
#define PORT_REPLAY_PATH_BYTES 512
#define PORT_REPLAY_NAME_BYTES 96
#define PORT_REPLAY_WHY_BYTES 200

/* ---- recording ---- */

/* Every fight is recorded into `folder` (made if it is not there) from
 * now on; NULL or "" records none. */
extern void port_SetReplayFolder(const char *folder);
extern const char *port_GetReplayFolder(void);
/* After each one written, the oldest are taken away until at most
 * `count` are left, and they take at most `megabytes` (0: no limit). */
extern void port_SetReplayKeep(u32 count, u32 megabytes);
/* How long the writing of a fight's file may take at each boundary after
 * the fight (the window's, so that no frame waits long for it), in
 * microseconds; 0: all of it at once, as the fight ends (the program
 * with no window). */
extern void port_SetReplaySlice(u32 microseconds);
/* Whether the next fights are recorded at all (the window's `replays`):
 * a fight being recorded when this goes off is let go. */
extern void port_SetReplayRecording(s32 on);
/* The fight being recorded is let go, and why, for the log: something
 * changed the game's state that no input did (the settings menu's Bug
 * fixes). */
extern void port_ReplayAbandon(const char *why);

/* The program ends (port_Quit): a fight being recorded is written as
 * far as it went, and one being written is finished. */
extern void port_ReplayAtQuit(void);
/* The host's turn at a frame's boundary, before the step (main.c's loop
 * and netplay's frame): where a fight's start is seen and kept, a hash
 * noted, and a playback run. */
extern void port_ReplayBoundary(void);
/* The game takes input number `index` (pad.c, port_TakeInput), after
 * the take hook: kept, if a fight is being recorded. */
extern void port_ReplayTook(const struct PortInput *input, u32 index);
/* ... and the menu sound the window asked the game for as it took it
 * (port_top_menu.h), kept with it; and the one a replay being played
 * kept with input number `index`, or -1. */
extern void port_ReplayTookSound(u32 index, s32 sound);
extern s32 port_ReplaySoundAt(u32 index);

/* The disc the game runs from, whose runs of bytes a fight's start is
 * not written with (main.c); and each sector the game reads from it
 * (cd.c), the last of which are looked for in the start. */
extern void port_SetReplayDisc(const struct PortDisc *disc);
extern void port_ReplayNoteSector(u32 sector);

/* ---- a room's spectators (docs/NETPLAY.md, section 16) ---- */

/* This program is a room's match's host: each fight's start is recorded
 * (whether fights are written or not) and compressed as soon as it is
 * final, for the match's spectators. */
extern void port_ReplayServe(s32 on);
/* The latest fight's start so compressed, a replay's chunks: TRUE, its
 * bytes, netplay's frame it is of and a count that is new for each new
 * start; FALSE before there is one. */
extern s32 port_ReplayServedStart(
    const u8 **blob, u32 *size, u32 *frame, u32 *made);
/* A spectator: a start served so put into the state, this run's own kept
 * as a playback keeps it; netplay's frame it is of. FALSE where it is not
 * whole, or not the state its host had (its hash). */
extern s32 port_ReplayLoadServed(const u8 *blob, u32 size, u32 *frame);

/* ---- playing back ---- */

/* What a replay says of itself, read from its file. */
typedef struct {
    char path[PORT_REPLAY_PATH_BYTES];
    char name[PORT_REPLAY_NAME_BYTES]; /* the file's own name */
    char date[24];                     /* YYYY-MM-DD HH:MM:SS */
    s32 mode;                          /* enum FightMode */
    s32 stage;
    s32 roster[2];
    s32 weapon[2];
    s32 is_cpu[2];
    s32 stage_kind; /* PORT_STAGE_... */
    s32 arena;      /* PORT_STAGE_NO_ARENA, or the arena's number */
    s32 time;       /* PORT_TIME_... */
    s32 music;      /* the fight's music track, or -1 */
    s32 online;     /* 0, or 1 for the host's machine and 2 the guest's */
    s32 winner;     /* 0, 1, or -1 for nobody */
    u32 frames;
    u32 kill; /* the frame the first fighter was cut down at, or frames */
    u32 bytes;
    s32 playable; /* this program can play it; `why` says why not */
    char why[PORT_REPLAY_WHY_BYTES];
} PortReplayInfo;

/* What a replay file says of itself: TRUE where it is one. */
extern s32 port_ReadReplayInfo(const char *path, PortReplayInfo *info);
/* The replays of the folder, newest first: how many were found. */
extern s32 port_ListReplays(PortReplayInfo *infos, s32 most);

/* The replay is played from the next frame's boundary, the game as it
 * is then put back when it ends: FALSE, with the reason in `why`, where
 * it cannot be (the sums, the file). `quiet`: as fast as it runs, with
 * nothing drawn or heard, and the program ends after it with its
 * verdict (--replay-check). `then_quit`: the program ends after it
 * (--replay). */
extern s32 port_PlayReplay(
    const char *path, s32 quiet, s32 then_quit, char *why, u32 room);
extern s32 port_ReplayIsPlaying(void);

/* What the player asks of a playback. */
enum {
    PORT_REPLAY_PAUSE,   /* pause, or go on */
    PORT_REPLAY_STEP,    /* one frame, paused */
    PORT_REPLAY_SLOWER,  /* half as fast, down to an eighth */
    PORT_REPLAY_FASTER,  /* twice as fast, up to eight times */
    PORT_REPLAY_RESTART, /* from its first frame */
    PORT_REPLAY_KILL,    /* to a moment before the first cut */
    PORT_REPLAY_STOP     /* back to the game */
};

extern void port_ReplayControl(s32 what);

typedef struct {
    s32 is_playing;
    s32 is_paused;
    s32 speed; /* in eighths: 8 is as it was played */
    u32 frame;
    u32 frames;
    u32 kill;
    s32 desync; /* a hash differed at `frame` */
} PortReplayStatus;

extern void port_GetReplayStatus(PortReplayStatus *status);

/* The window's: while a playback waits (paused, or slower than it was
 * played), called about once a blank, to read the window's events and
 * show the picture again; and told when one ends, with what it ended
 * on (NULL: it was played to its end, or stopped). */
extern void (*g_port_replay_wait_hook)(void);
extern void (*g_port_replay_end_hook)(const char *trouble);

#endif /* PORT_REPLAY_H */
