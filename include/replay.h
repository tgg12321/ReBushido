#ifndef REPLAY_H
#define REPLAY_H

/* The instant replay shown after a bout: the last REPLAY_FRAMES frames of
 * both fighters, plus the sounds, effects and loose objects of those frames
 * (replay.c). */

#include "common.h"
#include "motion.h"
#include "fighter.h"

#define REPLAY_FRAMES 120
#define REPLAY_EVENTS 32
#define REPLAY_OBJECT_DRAWS 180

/* One fighter in one recorded frame: enough of the fighter record to pose and
 * draw it again. */
typedef struct ReplayFighterFrame {
    PTR32(MoveScript *) cur_move;
    s16 pos_x;
    s16 pos_y;
    s16 pos_z;
    u16 yaw_and_face; /* yaw in the low 12 bits, the face in the top 4 */
    s16 ground_y;
    s16 pose_from_frame;
    s16 pose_to_frame;
    s16 pose_weight;
    s8 aim_turn; /* the three offsets are stored >> 2 */
    s8 aim_lean;
    s8 gun_aim;
    u8 move_section_bits; /* REPLAY_MOVE_... */
    u8 frame_flags;
    u8 move_frame;
    u8 pad1A[2];
} ReplayFighterFrame; /* 0x1C bytes */

/* ReplayFighterFrame.move_section_bits: where the move's motion is. */
#define REPLAY_MOVE_COMMON 1      /* in the common motion file */
#define REPLAY_MOVE_SECOND_SLOT 2 /* else: in the second fighter file */

/* A loose object (a dropped weapon, a thrown item) drawn at one place for a
 * run of frames. */
typedef struct ReplayObjectDraw {
    s16 age;         /* frames since first drawn here, or REPLAY_DRAW_FREE */
    u8 span;         /* how many frames it stayed, less one */
    u8 weapon_model; /* which model */
    s16 pos[3];
    u16 rot_x_and_slot; /* rotation x in the low 12 bits, the object's slot in
                           the top 4 */
    s16 rot_y;
    s16 rot_z;
} ReplayObjectDraw;

#define REPLAY_DRAW_FREE (-1)

/* A sound or effect that went off in a recorded frame. */
typedef struct ReplayEvent {
    u8 frame; /* the frame it belongs to; REPLAY_EVENT_FREE: free */
    u8 variant;
    u8 kind;
    u8 pad3;
    s16 pos[3];
    u16 vector[3];
} ReplayEvent;

#define REPLAY_EVENT_FREE 0xFF

/* The work buffer that holds the ring of frames. Outside a fight it is
 * borrowed: the memory-card code builds its save image in it, and the CD
 * loader uses it too. */
extern u8 g_shared_buffer[REPLAY_FRAMES * 2 * sizeof(ReplayFighterFrame)];

/* The replay's screen (enum Screen). */
extern void replay_Start(void);
extern void replay_PlayFrame(void);

/* Recording, called from the fight loop. */
extern void replay_Reset(void);
extern void replay_BeginFrame(void);
extern void replay_RecordFighter(Fighter *fighter);
extern void replay_RecordObjectDraw(
    s32 slot, s32 weapon_model, s32 *pos, s16 *rot);
extern void replay_RecordEvent(s32 variant, s32 kind, s32 *pos, u16 *vector);
extern void replay_AdvanceFrame(void);

/* Used only inside replay.c. */

/* The ring of frames, [frame][fighter]. It lives in the shared work buffer. */
extern ReplayFighterFrame (*g_replay_fighter_frames)[2];

/* What replay_RecordObjectDraw and replay_RecordEvent record. */
extern ReplayObjectDraw g_replay_object_draws[REPLAY_OBJECT_DRAWS];
extern ReplayEvent g_replay_events[REPLAY_EVENTS];

#endif /* REPLAY_H */
