#ifndef SYS_H
#define SYS_H

/* Boot and the main loop (main.c), and running the movie player
 * (cdrom.c).
 *
 * main() starts the machine and then runs one frame after another for ever.
 * Each frame calls one handler out of a table: the current *screen*. A screen
 * moves the game on by storing another screen in g_sys_screen. */

#include "common.h"

/* The screens. Most come in pairs: an _INIT that runs for one frame, sets the
 * screen up and stores its _UPDATE, which then runs every frame until the
 * screen is over. The numbers index g_sys_screen_handlers. */
enum Screen {
    /* the fight itself: loads stage and fighters, then a frame of fighting */
    SCREEN_FIGHT_INIT = 0,
    SCREEN_FIGHT_UPDATE = 1,
    /* a text-only menu that picks fighters, stage and mode (a debug aid) */
    SCREEN_DEBUG_MENU_INIT = 2,
    SCREEN_DEBUG_MENU_UPDATE = 3,
    /* the replay of the fight just fought */
    SCREEN_REPLAY_INIT = 4,
    SCREEN_REPLAY_UPDATE = 5,
    /* a story cutscene: recorded motion and camera with a voice track */
    SCREEN_CUTSCENE_INIT = 6,
    SCREEN_CUTSCENE_UPDATE = 7,
    /* the main menu and everything reached from it (enum MenuPage) */
    SCREEN_MENU_INIT = 8,
    SCREEN_MENU_UPDATE = 9,
    SCREEN_STAFF_ROLL_INIT = 10,
    SCREEN_STAFF_ROLL_UPDATE = 11,
    /* after a fight: a three-item menu ("Rematch", "Return to Sub Menu",
     * "Return to Top Menu") over a low view of the loser lying where it
     * fell, the winner standing by */
    SCREEN_AFTER_FIGHT_INIT = 12,
    SCREEN_AFTER_FIGHT_UPDATE = 13,
    /* an empty handler; nothing selects it */
    SCREEN_UNUSED = 14,
    /* plays the opening movie, then goes to the main menu. The game starts
     * here. */
    SCREEN_MOVIE = 15,
    /* the moment after the last blow: the camera swings round to the winner */
    SCREEN_WINNER_VIEW_INIT = 16,
    SCREEN_WINNER_VIEW_UPDATE = 17,
    /* an ending: a cutscene with a still picture wiped in over it. Also used
     * for the team-battle winner and for newly unlocked fighters. */
    SCREEN_ENDING_INIT = 18,
    SCREEN_ENDING_UPDATE = 19,
    /* the team battle's table of results */
    SCREEN_TEAM_RESULT_INIT = 20,
    SCREEN_TEAM_RESULT_UPDATE = 21,
    /* story mode: the card that names the next stage, shown while it loads */
    SCREEN_STAGE_CARD_INIT = 22,
    SCREEN_STAGE_CARD_UPDATE = 23,
    /* the same three-item menu as SCREEN_AFTER_FIGHT, over a slowly turning
     * view of the stage: after a team battle or a kaishaku */
    SCREEN_AFTER_MATCH_INIT = 24,
    SCREEN_AFTER_MATCH_UPDATE = 25,
    /* saves to the memory card, then goes to the main menu */
    SCREEN_SAVE_INIT = 26,
    SCREEN_SAVE_UPDATE = 27,
    /* the cutscene of a kaishaku: a fighter that has given up (SELECT) kneels
     * and the other strikes it down from behind */
    SCREEN_KAISHAKU_INIT = 28,
    SCREEN_KAISHAKU_UPDATE = 29,
    /* the table of best times after a chanbara run */
    SCREEN_BEST_TIMES_INIT = 30,
    SCREEN_BEST_TIMES_UPDATE = 31,
    /* "n Kills Completed!": a run against the CPU has beaten every
     * opponent there is, and they were fewer than twenty */
    SCREEN_KILL_COUNT_INIT = 32,
    SCREEN_KILL_COUNT_UPDATE = 33,
};

/* The game runs at 30 frames a second, and counts time in frames. A time
 * is shown in minutes, seconds and hundredths of a second. */
#define FRAMES_PER_SECOND 30
#define SECONDS_PER_MINUTE 60
#define FRAMES_PER_MINUTE (SECONDS_PER_MINUTE * FRAMES_PER_SECOND)
#define HUNDREDTHS_PER_SECOND 100

/* Where things are in memory above the program. */
#define SYS_SOUND_VAB_ADDR 0x8010DB00 /* the common sound bank */
#define SYS_SOUND_VAB_SIZE 0xD00
#define SYS_EFFECTS_ADDR 0x8010E800 /* effect textures and tables */
#define SYS_EFFECTS_SIZE 0xA000
/* the movie overlay's work area, while the opening movie plays */
#define SYS_MOVIE_WORK_ADDR 0x80118000
#define SYS_LOAD_ADDR 0x80118800 /* whatever the current screen loads */
/* where a file is read to before it is copied or uploaded; in a fight, the
 * second fighter's motion file */
#define SYS_STAGING_ADDR 0x80190800
#define SYS_PRIM_BUFFER_0 0x801D8800 /* a frame's GPU primitives... */
#define SYS_PRIM_BUFFER_1 0x801EBC00 /* ...and the other frame's */
#define SYS_PRIM_BUFFER_SIZE 0x13400
#define SYS_STACK_TOP 0x801FFF00

/* The screen that runs this frame (enum Screen). */
extern s16 g_sys_screen;
/* Frames the current screen has run; each screen counts for itself. */
extern s32 g_sys_screen_frames;
/* How many vertical blanks a frame lasts: 2 (30 frames a second) everywhere
 * but the main menu, which runs at 1. */
extern u8 g_sys_frame_period;
/* Frames still to run without showing them (set while a screen has nothing
 * drawn yet). */
extern u8 g_sys_skip_frames;

/* Starts the machine: interrupts, pads, display, CD, memory card. */
extern void sys_Init(void);
/* Resets the engine's state: archive, arena, scene, lights, models. Run at
 * boot and again after the movie player returns. */
extern void sys_InitEngineState(void);
/* The root counter that counts horizontal retraces, which times the frame. */
extern void sys_StartHblankCounter(void);
/* Stops for good, printing "OVER FLOW" to the development host. */
extern void sys_Panic(void);
extern void sys_PollHost(void);
/* Stops the game where it cannot go on (math_asm.c). No prototype: defined
 * with no arguments, but stage_ReadCells passes it a number and
 * cloth_CreatePiece a message. */
extern void sys_Trap();

/* The two screens that are the system's own: the one nothing selects, and
 * the opening movie. */
extern void sys_UnusedScreen(void);
extern void sys_MovieScreen(void);
/* Shuts the game's systems down, runs the movie player on a file of
 * CDROM_GROUP_MOVIE, and starts the systems again when it returns.
 * `arena_base` and `work_buffer` are memory the player may use (cdrom.c). */
extern void sys_ExecMovieOverlay(
    s32 arena_base, s32 work_buffer, s32 movie_file, s32 end_frame,
    s32 still_image_frame);

/* A function's variable arguments, which the game's two printing
 * functions walk for themselves: each is a word on the stack after the
 * last named one. */
#if RB_PORT
/* In the port they are the compiler's: a PC's arguments are not words on
 * the stack one after the other (a 64-bit program has its first six in
 * registers). */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#else
typedef char *va_list;
#define va_start(ap, last) ((ap) = (va_list)(&(last) + 1))
#define va_arg(ap, type) ((type *)(void *)(ap += 4))[-1]
#endif

/* The game's own random numbers (0..0x7FFF), separate from the C library's
 * rand(). */
extern void rng_SetSeed(s32 seed);
extern s32 rng_Next(void);

#endif /* SYS_H */
