#ifndef PORT_STAGE_CHOICE_H
#define PORT_STAGE_CHOICE_H

/* The native port's three kinds of stage (ROADMAP 16; docs/PORT.md, "The
 * stage select's kinds of stage"): a stage of the disc's fought Classic
 * (the engine's and the renderer's own improvements only) or Remastered
 * Classic (the same stage and collision, dressed: the stage's own lights,
 * the stages-hd textures, the sky, item 17's dressing), and a custom arena
 * (port_arenas.h) fought in place of its host.
 *
 * What the players chose is the game's state (src/port/stage_choice.c is
 * a state file, config/port_state.txt): each stage's kind as the switch
 * on the stage pages left it, the fight's kind, the arena in play, and the
 * arenas' page with its cursor. So a rollback puts the choice back with
 * the stage the game keeps, and two players of a session, whose presses
 * are the same on both machines, choose the same. The picture asks one
 * question, port_StageIsDressed(), wherever a feature of the dressing
 * decides. The PlayStation builds never see this header. */

#include "common.h"

enum {
    PORT_STAGE_CLASSIC = 0,
    PORT_STAGE_REMASTERED = 1,
};

/* The time of day a stage is fought at (ROADMAP 20; docs/PORT.md, "The
 * time of day"): the stage's own lighting as the disc has it, or the
 * port's dawn, day, dusk or night, which only the remastered picture of a
 * dressed stage shows (port_StageTime). In this order on the page. */
enum {
    PORT_TIME_OWN = 0,
    PORT_TIME_DAWN = 1,
    PORT_TIME_DAY = 2,
    PORT_TIME_DUSK = 3,
    PORT_TIME_NIGHT = 4,
    PORT_TIME_COUNT = 5
};

/* no arena in play */
#define PORT_STAGE_NO_ARENA (-1)

typedef struct {
    u8 kinds[38];    /* each stage's PORT_STAGE_ kind (STAGE_COUNT) */
    u8 default_kind; /* what the kinds were last reset to */
    u8 fight_kind;   /* the fight's: the stage chosen's, or the default */
    s8 arena;        /* the arena in play (port_ArenaAt), or NO_ARENA */
    s8 loaded_arena; /* the arena the stage in memory was loaded with */
    u8 page_open;    /* the arenas' page is up over the stage pages */
    u8 page_cursor;  /* its row */
    u8 page_top;     /* the first row shown */
    u8 on_stages;    /* a stage page ran this frame (for the play tests) */
    u8 cursor_kind;  /* the kind of the stage under the grid's cursor */
    u8 is_started;
    u8 times[38];    /* each stage's PORT_TIME_ as L1 and R1 left it */
    u8 default_time; /* what the times were last reset to */
    u8 fight_time;   /* the fight's: the stage chosen's, or the default */
    u8 cursor_time;  /* the time of the stage under the grid's cursor */
} PortStageChoice;

extern PortStageChoice g_port_stage_choice;

/* ---- the picture's question ---- */

/* TRUE while the stage in play is dressed: Remastered Classic, or an
 * arena (which brings its own dressing). Classic is the stage as the disc
 * has it with only what the engine and renderer improve. */
extern s32 port_StageIsDressed(void);
/* The time of day the picture lights the stage in play at: the fight's
 * PORT_TIME_, or PORT_TIME_OWN on a stage fought Classic, which keeps the
 * game's own lighting. */
extern s32 port_StageTime(void);
/* The arena in play, as port_ArenaAt counts them, or PORT_STAGE_NO_ARENA. */
extern s32 port_StageArena(void);

/* ---- what the game calls (under RB_PORT) ---- */

/* What a stage page does with this frame's presses, as
 * port_StagePageStep says. */
enum {
    PORT_STAGE_PAGE_GRID = 0,  /* nothing of the port's: the grid's own */
    PORT_STAGE_PAGE_TAKEN = 1, /* the port's page took them */
    PORT_STAGE_PAGE_ARENA = 2, /* an arena was chosen: fight on `*stage` */
};

/* A frame of a stage page (select.c, select_StageStep), before the grid
 * reads the presses (L1 and R1 turn the time of day of the stage under
 * the cursor back and on): `pressed` the pads' presses, `stage` the stage under
 * the grid's cursor (-1: none), `is_open` the door open so the grid
 * takes presses. Keeps the kind of the stage under the cursor and the
 * arenas' page for the window to draw. Returns PORT_STAGE_PAGE_...; with
 * ..._ARENA `*stage` is the host to fight on. */
extern s32 port_StagePageStep(s32 pressed, s32 *stage, s32 is_open);
/* A ranked match's stage (src/port/ranked.c; docs/NETPLAY.md, section
 * 17), drawn by the lobby for the match: TRUE and its place among the
 * three pages' 18 (a page's six, in its order, page after page), which
 * the stage pages take as soon as they open. Outside the state, the same
 * on both machines of the match for all of it. */
extern s32 port_RankedStagePlace(s32 *place);
/* A frame of the select screen on none of the stage pages. */
extern void port_StageLeftPages(void);
/* The grid's own cross on `stage`: the fight is on the disc's stage, in
 * the kind the switch left it, with the run's own arena (--arena). */
extern void port_StageChosen(s32 stage);
/* The select screen loads (select.c, select_Load): the fight's kind the
 * default, the arena the run's own, the arenas' page down. */
extern void port_StageChoiceReset(void);
/* As the fight's stage is loaded (fight.c, fight_LoadStage), the modes
 * that choose no stage (all but versus and link) take the default kind
 * and the run's arena. TRUE if the arena in play is not the one the stage
 * in memory was loaded with, so the stage must be loaded again. */
extern s32 port_StageChoiceAtLoad(s32 mode);
/* The stage has been loaded with the arena in play. */
extern void port_StageChoiceLoaded(void);

/* ---- the run's choices, outside the state (src/port/arenas.c) ---- */

/* The kind the stages start as, and the modes with no stage page fight
 * in (the window's `stage_kind`, PORT_STAGE_...); in a session the
 * host's, and not changed after. Read by the game's step as it needs it,
 * so a change reaches the state at a frame of the game, and a rollback
 * or run-ahead puts nothing back of it. */
extern void port_SetStageKindDefault(s32 kind);
extern s32 port_GetStageKindDefault(void);
/* The session holds it: port_SetStageKindDefault changes nothing after. */
extern void port_HoldStageKindDefault(s32 kind);
/* The player's own kind and time of day kept before a session begun in
 * the running program holds the host's, and put back after it. */
extern void port_KeepStageDefaults(void);
extern void port_RestoreStageDefaults(void);
/* The time of day the stages start at, and the modes with no stage page
 * fight at (the window's `time_of_day`, PORT_TIME_...), as the kind is:
 * in a session the host's, held. */
extern void port_SetStageTimeDefault(s32 time);
extern s32 port_GetStageTimeDefault(void);
extern void port_HoldStageTimeDefault(s32 time);
/* The arena the run asked for (--arena): the one fought on its host
 * whenever no arena is chosen, or PORT_STAGE_NO_ARENA. */
extern s32 port_ArenaAsked(void);

#endif /* PORT_STAGE_CHOICE_H */
