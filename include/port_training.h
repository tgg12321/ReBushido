#ifndef PORT_TRAINING_H
#define PORT_TRAINING_H

/* The native port's training mode (ROADMAP 26; docs/PORT.md, "Training";
 * docs/PLAYING.md, "Training"): a fight of versus against a dummy, with
 * nothing of the fight's rules changed, that never ends by itself. It is
 * the Training row of ReBushido's main menu, a page of its own: Training,
 * the game's own Classic Training (its practice), Training Options (the
 * settings menu's Training page) and Back.
 *
 * - The fighters, the weapons and the stage are picked as for versus,
 *   the first pad picking both sides' (the dummy's after its own). No
 *   rule page: a single fight, never a match.
 * - Two seconds after a kill (the game's own wait before its replay) both
 *   fighters start again where they are, as a team battle's round or
 *   practice starts again (g_fight_restart): nothing is loaded, no replay,
 *   no menu after the fight. A surrender's cutscene likewise. Leaving is
 *   the pause menu's.
 * - The dummy is the second side, driven only by the second pad's input,
 *   which training writes in place of the second pad's own: nothing (it
 *   stands), the game's answer to an attack about to land (it guards: the
 *   attack button the game's CPU answers that kind of attack with, pressed
 *   a frame before the blow), or what the player recorded for it.
 * - With Select held on the first pad, its presses are training's and
 *   none of them reach its fighter:
 *     Cross     record the dummy (the first pad drives the dummy's side
 *               and is kept in the slot), and again to stop
 *     Circle    play the slot back from its start
 *     L1, R1    keep the moment, and put it back (state.c's save and load)
 *     L2        slow motion on and off
 *     Triangle  freeze and run on
 *     Square    a step of one frame (frozen after it)
 *   Start pauses as ever, frozen or not.
 * - Slow motion and the freeze hold the fight between its frames as the
 *   pause does, its last frame shown again (main.c, fight_RunTrainingHold):
 *   the fight's own frames are the game's, one after another, and a press
 *   made while it is held is kept for the next.
 *
 * What a fight of training is (is_on, the dummy's place in its slot, the
 * freeze, the presses shown) is the game's state, src/port/training.c: a
 * moment put back puts it back. The recordings, the moment itself and
 * the run's switches are kept outside it (src/port/training_kept.c), so
 * that neither a moment put back nor run-ahead takes them away. Training
 * is never on in a session, and no replay is kept of its fights. The
 * PlayStation builds never see this header. */

#include "common.h"

struct PortInput; /* port_platform.h */

/* What the dummy does (the window's training_dummy, --training-dummy). */
enum {
    PORT_TRAINING_STAND = 0,
    PORT_TRAINING_GUARD = 1,
    PORT_TRAINING_PLAYBACK = 2,
    PORT_TRAINING_DUMMIES = 3,
};

/* The slots a dummy's moves are recorded into, and how long each is: a
 * minute of the fight's frames (thirty a second). */
#define PORT_TRAINING_SLOTS 4
#define PORT_TRAINING_SLOT_FRAMES 1800
/* Slow motion: a frame of the fight in every 2 to 8 shown. */
#define PORT_TRAINING_SLOW_LEAST 2
#define PORT_TRAINING_SLOW_MOST 8
/* The presses shown: the last so many changes of each pad's buttons. */
#define PORT_TRAINING_SHOWN 16

/* One line of the input display: the buttons held, for how many frames. */
typedef struct {
    u16 buttons;
    u16 frames;
} PortTrainingPress;

typedef struct {
    u8 is_on;       /* the mode's fights are training's (its page's row) */
    u8 in_pause;    /* the pause menu is up: its presses are its own */
    u8 recording;   /* the slot being recorded, and one; 0 for none */
    u8 playing;     /* the slot being played, and one; 0 for none */
    u8 frozen;      /* held until a step or a run on */
    u8 slow;        /* slow motion: the frames held after each, and one */
    u8 holding;     /* the fight held between two of its frames */
    u8 held;        /* frames held since the fight's last */
    u8 step;        /* a frame asked for while frozen */
    u8 wants_pause; /* Start while held: the pause menu */
    u8 dummy;       /* what the dummy did at the last frame (PORT_TRAINING_) */
    u8 shown_count[2];
    u16 last;       /* the first pad's buttons at the input before */
    u16 latched[2]; /* presses made while held, for the next frame */
    u16 record_at;  /* the frame of the slot recorded or played next */
    u16 play_at;
    u16 played_slot; /* the slot playing back, 0 to 3 */
    u32 seed;        /* for a random slot: training's own, not the game's */
    u32 frames;      /* frames of training's fights */
    u32 restarts;    /* kills that started both fighters again */
    u32 holds;       /* frames held */
    /* the presses shown, the newest first: both pads' as their fighters
     * were given them */
    PortTrainingPress shown[2][PORT_TRAINING_SHOWN];
} PortTraining;

extern PortTraining g_port_training;

/* ---- what the game calls (under RB_PORT) ---- */

/* Whether the mode being played is training. */
extern s32 port_TrainingIsOn(void);
/* The main menu chose a mode: training's row, or another. */
extern void port_TrainingChoose(s32 on);
/* The game takes an input (pad.c, port_TakeInput): in a fight of
 * training, Select's presses are training's and the second pad is the
 * dummy's. */
extern void port_TrainingTakeInput(struct PortInput *input);
/* A fight of training has had its kill (fight.c, fight_AfterDefeat; or a
 * surrender, fight_after.c): TRUE if both start again, which it has set
 * up (g_fight_restart, g_sys_screen). */
extern s32 port_TrainingRestart(void);
/* The pause menu is up, or down again. */
extern void port_TrainingPause(s32 up);
/* After a frame of the fight: whether it is held before the next (slow
 * motion, the freeze). */
extern s32 port_TrainingHolds(void);
/* main.c's loop of a hold: begun, a frame of it over (TRUE while it goes
 * on), ended (TRUE if Start asked for the pause menu). */
extern void port_TrainingHoldBegin(void);
extern s32 port_TrainingHoldGoesOn(void);
extern s32 port_TrainingHoldEnd(void);

/* After the fighters of a frame of training's fight are tested against
 * each other (fight.c, fight_Update): their hit areas noted for the
 * window, as the frame's camera puts them on the screen. */
extern void port_TrainingNoteAreas(void);

/* The hit areas as the window draws them (training_page.c): each body
 * point's sphere, and its core where it has one (the head, the chest and
 * the hips: a blade that reaches the core kills, one that reaches the
 * sphere wounds), and each weapon's points (its blade, from the hand to
 * the point), on the game's screen of 640 by 240, as the last frame of
 * training's fight projected them. Outside the state (training_kept.c):
 * the picture's. */
#define PORT_TRAINING_BODY_POINTS 22
#define PORT_TRAINING_BLADE_POINTS 3

typedef struct {
    s16 x;
    s16 y;
    s16 radius;      /* across: down is half of it, as the screen is */
    s16 core_radius; /* 0: no core */
    u8 seen;         /* in front of the camera */
    u8 group;        /* 0 the head, 1 the chest and hips, 2 an arm, 3 a leg */
} PortTrainingArea;

typedef struct {
    u32 frame; /* the game's frame it was noted at (g_disp_frame_count) */
    u8 on;     /* a fight of training's: drawn */
    u8 blade_points[2];
    u8 blade_live[2]; /* the weapon can hit at all */
    u8 blade_hits[2]; /* ... and is in its frames of hitting */
    PortTrainingArea body[2][PORT_TRAINING_BODY_POINTS];
    s16 blade[2][PORT_TRAINING_BLADE_POINTS][2];
    u8 blade_seen[2][PORT_TRAINING_BLADE_POINTS];
} PortTrainingAreas;

extern PortTrainingAreas g_port_training_areas;

/* ---- the run's switches and what is kept outside the state
 * (training_kept.c) ---- */

typedef struct {
    s32 dummy;  /* PORT_TRAINING_... */
    s32 slot;   /* the slot recorded and played, 0 to 3 */
    s32 random; /* playback plays a random slot of those recorded */
    s32 slow;   /* slow motion's frames shown for each of the fight's */
} PortTrainingSwitches;

extern void port_SetTrainingSwitches(const PortTrainingSwitches *switches);
extern const PortTrainingSwitches *port_GetTrainingSwitches(void);
/* A slot's frames: written while it is recorded, read while it plays. */
extern void port_TrainingKeep(s32 slot, s32 at, u16 buttons);
extern s32 port_TrainingKeptLength(s32 slot);
extern u16 port_TrainingKept(s32 slot, s32 at);

/* The moment asked to be kept or put back, done at the frame's boundary
 * (port_TrainingBoundary, from the host's loop). */
enum {
    PORT_TRAINING_KEEP = 1,
    PORT_TRAINING_PUT_BACK = 2,
};

extern void port_TrainingAsk(s32 what);
/* The first pad's buttons as the game last took them, which a moment put
 * back keeps as training's `last`: a button held through the put back is
 * not pressed again. */
extern void port_TrainingNoteHeld(u16 buttons);
extern void port_TrainingBoundary(void);
/* How many moments were kept and put back in this run. */
extern u32 port_TrainingMomentsKept(void);
extern u32 port_TrainingMomentsPutBack(void);

#endif /* PORT_TRAINING_H */
