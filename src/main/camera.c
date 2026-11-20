/* The camera. A view is a point looked at, three angles and a distance back
 * from the point (CameraView); scene_Begin places the scene's camera from
 * one. There are two views. The main one is the fight's side view: it looks
 * at the middle of the two fighters from beside them, pulls back as they
 * part, and tips up until neither is hidden behind the ground. The second
 * is the first-person view, through one fighter's eyes, when that setting
 * is on. After a fight the main view swings round to the winner and circles;
 * a replay follows one fighter with both views and cuts between them.
 *
 * What chooses between the two views each frame and begins the scene
 * (camera_BeginFightScene, camera_BeginReplayScene) is in the fight's file,
 * fight.c. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

CameraView g_camera_second;
CameraView g_camera_main;
CameraView g_camera_offset;

/* ---- the side view ---- */

/* Distances are in world units; a view's distance is from its focus back to
 * its eye. */
#define SIDE_DISTANCE_START 5000
#define SIDE_DISTANCE_MIN 6000
#define SIDE_DISTANCE_MAX 100000
/* with the blades locked, and as a round starts, the camera comes in close */
#define SIDE_DISTANCE_CLOSE_UP 3000
/* the stage's far view goes on above the first and off again below the
 * second */
#define SIDE_FAR_VIEW_ON 22001
#define SIDE_FAR_VIEW_OFF 21901
/* the focus goes a quarter of the way to where it is wanted each frame, the
 * distance a twelfth and the angles an eighth */
#define SIDE_FOCUS_EASE 4
#define SIDE_DISTANCE_EASE 12
#define SIDE_ANGLE_EASE 8
/* the view looks down by at least the first and at most the second */
#define SIDE_PITCH_MIN (ANGLE_TURN / 32)
#define SIDE_PITCH_MAX (7 * ANGLE_TURN / 64)
/* a fighter is looked for this far above where it stands */
#define SIDE_SIGHT_HEIGHT 200
/* a face in the line of sight hides a fighter if it faces up this much
 * (its normal's y; up is negative): ground, not a wall */
#define SIDE_GROUND_NORMAL (-800)
/* The pitch search's span is kept in eighths of an angle unit. It starts at
 * two units, and each frame the answer lies the same way it grows: by four
 * units up to 64 while the fighter is hidden, by two up to 32 while it is
 * seen. */
#define PITCH_STEP_ONE 8
#define PITCH_STEP_MIN (2 * PITCH_STEP_ONE)
#define PITCH_STEP_GROW_UP (4 * PITCH_STEP_ONE)
#define PITCH_STEP_MAX_UP (64 * PITCH_STEP_ONE)
#define PITCH_STEP_GROW_DOWN (2 * PITCH_STEP_ONE)
#define PITCH_STEP_MAX_DOWN (32 * PITCH_STEP_ONE)

/* A difference too long for the table's square root: it is halved until it
 * is within 0x4000 either way, so that the squares' sum fits a word. */
#define TOO_LONG(d) (((d) + 0x4000) > 0x8000U)

#define IS_RUNNING(fighter)                                                    \
    ((fighter)->move_kind == MOVE_KIND_RUN ||                                  \
     (fighter)->move_kind == MOVE_KIND_ARUN ||                                 \
     (fighter)->move_kind == MOVE_KIND_RUN_ST)

/* the round's start, and the moves of two locked blades */
#define IS_CLOSE_UP(fighter)                                                   \
    ((fighter)->move_kind == MOVE_KIND_LOCK ||                                 \
     (fighter)->move_kind == MOVE_KIND_START ||                                \
     (fighter)->move_kind == MOVE_KIND_LOCK_EVEN ||                            \
     (fighter)->move_kind == MOVE_KIND_LOCK_WINNING_1 ||                       \
     (fighter)->move_kind == MOVE_KIND_LOCK_WINNING_2 ||                       \
     (fighter)->move_kind == MOVE_KIND_LOCK_LOSING_1 ||                        \
     (fighter)->move_kind == MOVE_KIND_LOCK_LOSING_2)

#define CLEAR_OPEN_CELLS(view)                                                 \
    (view).open_cells[0][0] = CAMERA_NO_CELL;                                  \
    (view).open_cells[0][1] = 0;                                               \
    (view).open_cells[0][2] = CAMERA_NO_CELL;                                  \
    (view).open_cells[1][0] = CAMERA_NO_CELL;                                  \
    (view).open_cells[1][1] = 0;                                               \
    (view).open_cells[1][2] = CAMERA_NO_CELL

/* ---- the first-person view ---- */

/* the eye is lifted this far above a head that is nearer the ground than
 * FIRST_PERSON_STANDING (for a fighter of the usual size) */
#define FIRST_PERSON_LIFT 950
#define FIRST_PERSON_STANDING 913
#define FIRST_PERSON_CROUCH_PITCH (ANGLE_TURN / 8)
/* the pitch goes an eighth of the way to where it is wanted each frame */
#define FIRST_PERSON_PITCH_EASE 8
/* a turn of more than a quarter at once is followed over this many frames */
#define FIRST_PERSON_TURN_FRAMES 25
/* The zoom is kept in sixteenths of a unit. L2 with R1 or R2 moves the eye
 * back or forward by 76.75 units a frame, from 448 units in front of the
 * head to 1856 behind it. */
#define ZOOM_ONE 16
#define ZOOM_STEP 1228
#define ZOOM_MIN (-448 * ZOOM_ONE)
#define ZOOM_MAX (1856 * ZOOM_ONE)

/* ---- the replay ---- */

#define CAMERA_REPLAY_EYE_HEIGHT 300
/* the followed fighter's head this near the ground for more than
 * CAMERA_REPLAY_DOWN_FRAMES frames: it is down, and the camera looks from above
 */
#define CAMERA_REPLAY_DOWN_HEIGHT 200
#define CAMERA_REPLAY_DOWN_FRAMES 10
/* fighters further apart than this are not both kept in the picture */
#define CAMERA_REPLAY_NEAR 10000
/* every value goes a sixteenth of the way to where it is wanted each frame */
#define CAMERA_REPLAY_EASE 16
/* The view starts looking down a little. As the replay goes on it sinks to
 * look up by as much (a sixteenth of a turn in all), or over a fighter that
 * is down it rises to an eighth of a turn. */
#define CAMERA_REPLAY_PITCH (ANGLE_TURN / 32)
#define CAMERA_REPLAY_PITCH_RISE (3 * ANGLE_TURN / 32)
/* The distance the first frame sets, at the replay's beginning and at its
 * end; and the one the view then eases to: at the end it is close to the
 * fighters, or less so over one that is down. */
#define CAMERA_REPLAY_DISTANCE_FIRST 10000
#define CAMERA_REPLAY_DISTANCE_FIRST_END 2500
#define CAMERA_REPLAY_DISTANCE 12000
#define CAMERA_REPLAY_DISTANCE_END 500
#define CAMERA_REPLAY_DISTANCE_END_DOWN 2000
/* The main view is from this far round the line between the fighters,
 * swinging up to three quarters of a turn from it; the second from an
 * eighth of a turn behind the followed fighter. */
#define CAMERA_REPLAY_MAIN_YAW (5 * ANGLE_TURN / 16)
#define CAMERA_REPLAY_SECOND_YAW (-ANGLE_TURN / 8)

/* ---- after the fight ---- */

/* The view of a fighter that the screen after a fight circles with: its
 * pitch, in angle units, and its distance. */
#define LOOK_PITCH 288
#define LOOK_DISTANCE 4450
/* The swing round to the winner: the focus is this far above its feet; the
 * view comes in from the first distance to the second, drops from a
 * sixteenth of a turn above level to 32 units under it, and turns a quarter
 * of the way round from the yaw it starts at. */
#define SWING_FOCUS_HEIGHT 1100
#define SWING_DISTANCE_START 8000
#define SWING_DISTANCE_END 3000
#define SWING_PITCH_START (ANGLE_TURN / 16)
#define SWING_PITCH_DROP 288
#define SWING_YAW_START (11 * ANGLE_TURN / 16)

/* Whether each fighter was hidden behind the ground last frame, and the
 * span its pitch search has reached (camera_UpdateSideView). Of the first,
 * two entries are used. */
static u8 g_camera_blocked[4] = {0, 0, 0, 0};
static s32 g_camera_pitch_steps[2] = {0, 0};

/* How many frames of this replay the followed fighter has been down. */
static s16 g_camera_replay_down_frames;

/* The short way round from one angle to another, given their difference,
 * divided by `divisor`: one step of a turn that eases in. */
static s32 camera_EaseAngle(s32 difference, s32 divisor) {
    s32 turn = difference & ANGLE_MASK;
    if (turn >= ANGLE_TURN / 2) {
        turn -= ANGLE_TURN;
    }
    return turn / divisor;
}

/* Where a view's eye is: eye_distance back from the focus along the angles. */
void camera_CalcEye(CameraView *view, s32 *eye) {
    MATRIX matrix;
    matrix.m[0][0] = ONE;
    matrix.m[0][1] = 0;
    matrix.m[0][2] = 0;
    matrix.m[1][0] = 0;
    matrix.m[1][1] = ONE;
    matrix.m[1][2] = 0;
    matrix.m[2][0] = 0;
    matrix.m[2][1] = 0;
    matrix.m[2][2] = ONE;
    RotMatrixX(-view->angles.vx, &matrix);
    RotMatrixY(-view->angles.vy, &matrix);
    RotMatrixZ(-view->angles.vz, &matrix);
    eye[0] = view->focus.x - ((matrix.m[0][2] * view->eye_distance) >> 12);
    eye[1] = view->focus.y - ((matrix.m[1][2] * view->eye_distance) >> 12);
    eye[2] = view->focus.z - ((matrix.m[2][2] * view->eye_distance) >> 12);
}

/* The stage cell a world coordinate is in, counted from the middle of the
 * grid. */
static s32 camera_CellOf(s32 coordinate) {
    if (coordinate < 0) {
        return -((STAGE_CELL_SIZE - 1 - coordinate) / STAGE_CELL_SIZE);
    }
    return coordinate / STAGE_CELL_SIZE;
}

/* The cell of the ground that hides a fighter: the one a little way (1024
 * units) on from where the line of sight went into the ground at `hit`,
 * towards the eye. The scene draws all of that cell opened. */
static void camera_FindHidingCell(s16 *cell, s32 *eye, s32 *hit) {
    s32 dx;
    s32 dz;
    u32 dist_sq;
    s32 distance;
    s32 lzc;
    dx = eye[0] - hit[0];
    dz = eye[2] - hit[2];
    while (TOO_LONG(dx) || TOO_LONG(dz)) {
        dx = dx / 2;
        dz = dz / 2;
    }
    /* the square root, from the table of roots (math.h) */
    dist_sq = (dx * dx) + (dz * dz);
    if (dist_sq < MATH_SQRT_TABLE_SIZE) {
        distance = g_math_sqrt8_table[dist_sq] >> 3;
    } else {
        u32 shift_a;
        u32 shift_b;
        gte_Lzc(dist_sq, &lzc);
        {
            /* FAKE: shift_a = MATH_SQRT_SHIFT - (lzc & ~1) */
            s32 even_lzc = -2;
            even_lzc &= lzc;
            shift_a = MATH_SQRT_SHIFT - even_lzc;
        }
        shift_b = shift_a >> 1;
        distance = (((u32)((g_math_sqrt8_table[dist_sq >> shift_a]))) << 16) >>
                   (MATH_SQRT_UNSHIFT - shift_b);
    }
    cell[0] = camera_CellOf(hit[0] + ((dx << 10) / distance));
    cell[2] = camera_CellOf(hit[2] + ((dz << 10) / distance));
}

/* camera_UpdateSideView's work area, at the start of the scratchpad. */
typedef VECTOR CamVec;

typedef struct {
    s16 pad0;              /* 0x00 the first and the fourth of four ... */
    s16 yaw;               /* 0x02: the angles the view is turning to */
    s16 roll;              /* 0x04 */
    s16 pad6;              /* 0x06 ... angles: not touched */
    CamVec focus;          /* 0x08: where the view's focus is moving to */
    CamVec eye;            /* 0x18 */
    CamVec from;           /* 0x28: the fighter's end of a line of sight */
    s32 hit[8];            /* 0x38: where the line went into something */
    s16 normal[4];         /* 0x58: and which way that faces */
    SegmentCast cast_work; /* 0x60: the cast's own work area */
} SideViewScratch;

#define SIDE_SCRATCH ((SideViewScratch *)SCRATCHPAD)

/* A frame of the side view. `first_point` and `second_point` are the two
 * fighters' camera points (chest height).
 *
 * The focus moves a quarter of the way to the middle of the two each frame.
 * The distance eases to one that grows with how far apart they are, further
 * back still while one is running, and close in with the blades locked. The
 * yaw eases to square on to the line between them. Then, for each fighter,
 * the pitch is searched for the lowest angle from which the line from the
 * eye to the fighter does not go through the ground; the view eases to the
 * higher of the two answers. The search is a bisection of two steps a frame
 * from last frame's pitch, over a span that widens while the answer keeps
 * lying the same way. */
void camera_UpdateSideView(Vec3 *first_point, Vec3 *second_point,
                           Fighter *first_fighter, Fighter *second_fighter) {
    SideViewScratch *scr;
    CameraView *view;
    s32 dx, dy, dz;
    s32 x, y, z;
    s32 shift;
    u32 dist;
    u32 margin;
    s32 target_distance;
    s32 first_fighter_pitch, second_fighter_pitch;
    s32 start_pitch;
    s32 which; /* the fighter looked for: 0 or 1 */
    /* the pitch being tried for a fighter, then the pitch to ease to, then
     * the step taken towards it */
    s32 work;
    s32 i, j;

    scr = SIDE_SCRATCH;
    view = &g_camera_main;
    CLEAR_OPEN_CELLS(*view);
    dx = second_point->x - first_point->x;
    dy = second_point->y - first_point->y;
    dz = second_point->z - first_point->z;
    if (!g_fight_debug_view) {
        scr->focus.vx = (first_point->x + second_point->x) / 2;
        scr->focus.vy = (first_point->y + second_point->y) / 2;
        scr->focus.vz = (first_point->z + second_point->z) / 2;
    } else {
        /* the debug menu's view: on the first fighter alone */
        scr->focus = *(CamVec *)first_point;
    }
    view->focus.x += (scr->focus.vx - view->focus.x) / SIDE_FOCUS_EASE;
    view->focus.y += (scr->focus.vy - view->focus.y) / SIDE_FOCUS_EASE;
    view->focus.z += (scr->focus.vz - view->focus.z) / SIDE_FOCUS_EASE;

    /* how far apart they are: the square root from the table of roots */
    x = dx;
    y = dy;
    z = dz;
    shift = 0;
    while (TOO_LONG(x) || TOO_LONG(z)) {
        x /= 2;
        y /= 2;
        z /= 2;
        shift++;
    }
    dist = math_TableSqrt(x * x + z * z + y * y);
    dist <<= shift;

    /* The distance wanted is 1.28 times how far apart they are and a
     * margin. The margin shrinks as they part, from 3072 with the two
     * together towards 1024; it is 4096 more while either runs. */
    margin = 0x2000000U / (dist + 0x4000) + 0x400;
    if (IS_RUNNING(first_fighter) || IS_RUNNING(second_fighter)) {
        margin += 0x1000; /* 4096 units */
    }
    /* 128 hundredths */
    target_distance = ((dist + margin) << 7) / 100;
    if (dy < 0) {
        dy = -dy;
    }
    target_distance += dy;
    if (IS_CLOSE_UP(first_fighter)) {
        target_distance = SIDE_DISTANCE_CLOSE_UP;
    }
    view->eye_distance +=
        (target_distance - view->eye_distance) / SIDE_DISTANCE_EASE;
    if (!IS_CLOSE_UP(first_fighter) && view->eye_distance < SIDE_DISTANCE_MIN) {
        view->eye_distance = SIDE_DISTANCE_MIN;
    }
    if (view->eye_distance > SIDE_DISTANCE_MAX) {
        view->eye_distance = SIDE_DISTANCE_MAX;
    }
    if (view->far_view) {
        view->far_view = view->eye_distance >= SIDE_FAR_VIEW_OFF;
    } else {
        view->far_view = view->eye_distance >= SIDE_FAR_VIEW_ON;
    }
    stage_SetFarView(view->far_view);

    if (first_fighter->move_kind == MOVE_KIND_PAIRED) {
        scr->yaw = view->angles.vy;
    } else {
        scr->yaw = (ANGLE_TURN / 4 - ratan2(dx, dz)) & ANGLE_MASK;
    }
    scr->roll = 0;
    view->angles.vy +=
        camera_EaseAngle(scr->yaw - view->angles.vy, SIDE_ANGLE_EASE);
    view->angles.vz +=
        camera_EaseAngle(scr->roll - view->angles.vz, SIDE_ANGLE_EASE);
    start_pitch = view->angles.vx;

    for (which = 0; which < 2; which++) {
        /* the other end of the span being bisected: above `work` when the
         * fighter is hidden, below it when it is seen */
        s32 bound;

        work = start_pitch;
        view->angles.vx = work;
        camera_CalcEye(view, &scr->eye.vx);
        if (which != 0) {
            scr->from = second_fighter->foot_pos;
        } else {
            scr->from = first_fighter->foot_pos;
        }
        scr->from.vy -= SIDE_SIGHT_HEIGHT;
        if (collision_CastWith((s32 *)&scr->from, (s32 *)&scr->eye, scr->hit,
                               scr->normal, &scr->cast_work) &&
            scr->normal[1] < SIDE_GROUND_NORMAL) {
            /* hidden: look for the lowest pitch above this that sees it */
            camera_FindHidingCell(
                view->open_cells[which], (s32 *)&scr->eye, scr->hit);
            if (g_camera_blocked[which]) {
                g_camera_pitch_steps[which] += PITCH_STEP_GROW_UP;
            } else {
                g_camera_pitch_steps[which] = PITCH_STEP_MIN;
                work--;
            }
            if (g_camera_pitch_steps[which] < PITCH_STEP_MIN) {
                g_camera_pitch_steps[which] = PITCH_STEP_MIN;
            } else if (g_camera_pitch_steps[which] > PITCH_STEP_MAX_UP) {
                g_camera_pitch_steps[which] = PITCH_STEP_MAX_UP;
            }
            bound = work + g_camera_pitch_steps[which] / PITCH_STEP_ONE;
            for (i = 0; i < 2; i++) {
                s32 span = (work - bound) & ANGLE_MASK;
                if (span >= ANGLE_TURN / 2) {
                    span -= ANGLE_TURN;
                }
                view->angles.vx = bound + span / 2;
                camera_CalcEye(view, &scr->eye.vx);
                if (collision_CastWith(
                        (s32 *)&scr->from, (s32 *)&scr->eye, scr->hit,
                        scr->normal, &scr->cast_work) &&
                    scr->normal[1] < SIDE_GROUND_NORMAL) {
                    work = view->angles.vx;
                } else {
                    bound = view->angles.vx;
                }
            }
            work = bound;
            g_camera_blocked[which] = 1;
        } else {
            /* seen: look for how far the view could come down */
            if (g_camera_blocked[which]) {
                g_camera_pitch_steps[which] = PITCH_STEP_MIN;
                work++;
            } else {
                g_camera_pitch_steps[which] += PITCH_STEP_GROW_DOWN;
            }
            if (g_camera_pitch_steps[which] < PITCH_STEP_MIN) {
                g_camera_pitch_steps[which] = PITCH_STEP_MIN;
            } else if (g_camera_pitch_steps[which] > PITCH_STEP_MAX_DOWN) {
                g_camera_pitch_steps[which] = PITCH_STEP_MAX_DOWN;
            }
            bound = work - g_camera_pitch_steps[which] / PITCH_STEP_ONE;
            for (j = 0; j < 2; j++) {
                s32 span = (work - bound) & ANGLE_MASK;
                if (span >= ANGLE_TURN / 2) {
                    span -= ANGLE_TURN;
                }
                view->angles.vx = bound + span / 2;
                camera_CalcEye(view, &scr->eye.vx);
                if (collision_CastWith(
                        (s32 *)&scr->from, (s32 *)&scr->eye, scr->hit,
                        scr->normal, &scr->cast_work) &&
                    scr->normal[1] < SIDE_GROUND_NORMAL) {
                    camera_FindHidingCell(
                        view->open_cells[which], (s32 *)&scr->eye, scr->hit);
                    bound = view->angles.vx;
                } else {
                    work = view->angles.vx;
                }
            }
            g_camera_blocked[which] = 0;
        }
        if (which != 0) {
            second_fighter_pitch = work;
        } else {
            first_fighter_pitch = work;
        }
    }
    work = (first_fighter_pitch < second_fighter_pitch)
               ? second_fighter_pitch
               : first_fighter_pitch;
    if (work < SIDE_PITCH_MIN) {
        work = SIDE_PITCH_MIN;
    }
    if (work > SIDE_PITCH_MAX) {
        work = SIDE_PITCH_MAX;
    }
    view->angles.vx = start_pitch;
    work = camera_EaseAngle(work - start_pitch, SIDE_ANGLE_EASE);
    view->angles.vx += work;
}

/* Takes the camera's buttons out of what the pads hold, before the fight
 * sees them. In a fight in the first-person view, L2 with R1 moves the eye
 * back out of the fighter's head and L2 with R2 forward again; L2 itself
 * never reaches the fight, from either pad. */
void camera_ReadPad(s32 *held) {
    g_camera_offset.focus.x = 0;
    g_camera_offset.focus.y = 0;
    g_camera_offset.focus.z = 0;
    g_camera_offset.angles.vx = 0;
    g_camera_offset.angles.vy = 0;
    g_camera_offset.angles.vz = 0;
    g_camera_offset.eye_distance = 0;
    if (g_fight_first_person && g_sys_screen == SCREEN_FIGHT_UPDATE) {
        if (*held & PADL2) {
            g_camera_zoom_used = 1;
            if (*held & PADR1) {
                g_camera_zoom = g_camera_zoom + ZOOM_STEP;
            }
            if (*held & PADR2) {
                g_camera_zoom = g_camera_zoom - ZOOM_STEP;
            }
            if (g_camera_zoom < ZOOM_MIN) {
                g_camera_zoom = ZOOM_MIN;
            }
            if (g_camera_zoom > ZOOM_MAX) {
                g_camera_zoom = ZOOM_MAX;
            }
            *held = *held & ~(PADL2 | PADR2 | PADR1);
        }
        g_camera_offset.eye_distance = g_camera_zoom / ZOOM_ONE;
    }
#if RB_PORT
    /* the port's stage pages keep it (select.c clears `on_stages` as it
     * leaves them): L2 and R2 choose the fight's music there
     * (port_fight_music.h), L1 and R1 being the stage's time */
    if (g_port_stage_choice.on_stages && g_sys_screen == SCREEN_MENU_UPDATE) {
        return;
    }
#endif
    *held = *held & ~(PADL2 | (PADL2 << 16));
}

/* Puts the side view where a round starts: on the middle of the two
 * fighters, level, square on to them. */
void camera_InitSideView(Fighter *first_fighter, Fighter *second_fighter) {
    g_camera_mode = CAMERA_FIRST_PERSON;
    CLEAR_OPEN_CELLS(g_camera_main);
    stage_SetFarView(0);
    g_camera_main.focus.x =
        (first_fighter->world_pos.x + second_fighter->world_pos.x) / 2;
    g_camera_main.focus.y =
        (first_fighter->world_pos.y + second_fighter->world_pos.y) / 2;
    g_camera_main.focus.z =
        (first_fighter->world_pos.z + second_fighter->world_pos.z) / 2;
    g_camera_main.angles.vx = 0;
    g_camera_main.angles.vy =
        ANGLE_TURN / 4 -
        ratan2(second_fighter->world_pos.x - first_fighter->world_pos.x,
               second_fighter->world_pos.z - first_fighter->world_pos.z);
    g_camera_main.angles.vz = 0;
    g_camera_main.eye_distance = SIDE_DISTANCE_START;
    g_camera_main.far_view = 0;
}

/* Puts the first-person view at the head of the fighter it looks through. */
void camera_InitFirstPerson(Fighter *fighter, Fighter *second_fighter) {
    g_camera_mode = CAMERA_FIRST_PERSON;
    CLEAR_OPEN_CELLS(g_camera_second);
    stage_SetFarView(0);
    if (g_fight_viewer != 0) {
        fighter = second_fighter;
    }
    g_camera_second.focus.x = fighter->head_pos.x;
    g_camera_second.focus.z = fighter->head_pos.z;
    g_camera_second.focus.y = fighter->head_pos.y;
    g_camera_second.unk40 = 0;
}

/* A frame of the first-person view. The eye is the fighter's head (lifted
 * when the head is low, so that a fallen fighter does not look along the
 * ground) and it faces the way the fighter does. It looks up or down at the
 * opponent, the more so the more nearly the fighter faces it; crouching, it
 * looks down. */
void camera_UpdateFirstPerson(Fighter *viewer) {
    CameraView *view = &g_camera_second;
    s32 focus_y;
    /* how high the head is above the feet; where the focus is wanted; the
     * pitch wanted; then the yaw wanted */
    s32 work;
    s32 standing;

    stage_SetFarView(0);

    work = (viewer->foot_points[0].y + viewer->foot_points[1].y) / 2 -
           viewer->head_pos.y;
    standing = work >= FIRST_PERSON_STANDING;

    if (viewer->move_kind == MOVE_KIND_CROUCH) {
        work = FIRST_PERSON_CROUCH_PITCH;
    } else {
        view->focus.x = viewer->head_pos.x;
        view->focus.z = viewer->head_pos.z;
        work = viewer->head_pos.y;

        if (!standing) {
            work += -(viewer->size_scale * FIRST_PERSON_LIFT) / ONE;
        }

        /* a quarter of the way there each frame */
        focus_y = view->focus.y + (work - view->focus.y) / 4;
        view->focus.y = focus_y;

        /* (tested again, though it cannot have changed) */
        if (viewer->move_kind == MOVE_KIND_CROUCH) {
            work = FIRST_PERSON_CROUCH_PITCH;
        } else {
            /* how far the view is turned away from the opponent, up to a
             * quarter turn */
            work = (-viewer->opponent_bearing - view->angles.vy) & ANGLE_MASK;

            if (work >= ANGLE_TURN / 2) {
                work = ANGLE_TURN - work;
            }
            if (work >= ANGLE_TURN / 4) {
                work = ANGLE_TURN / 4;
            }

            /* the angle up or down to the opponent, less in proportion as
             * the view is turned away (of a quarter turn) */
            work = (ratan2(viewer->other->world_pos.y - focus_y,
                           g_fight_distance) *
                    (ANGLE_TURN / 4 - work)) >>
                   10;
        }
    }

    {
        s16 old_pitch = view->angles.vx;
        view->angles.vx =
            old_pitch + (work - old_pitch) / FIRST_PERSON_PITCH_EASE;
    }
    view->angles.vz = 0;

    {
        s16 frames;
        work = -viewer->orientation.vy;
        frames = g_camera_turn_frames;

        if (frames != 0) {
            /* the fighter has spun round: follow it a quarter of the way a
             * frame */
            s16 old_yaw = view->angles.vy;
            view->angles.vy = old_yaw + (work - old_yaw) / 4;
            frames--;
            g_camera_turn_frames = frames;

            view->turn_lag = view->turn_lag * 3 / 4;
        } else {
            view->angles.vy = work;
            view->turn_lag = 0;
        }
    }
    view->eye_distance = 0;
}

/* The fight tells the camera that a fighter's move has turned it by `turn`
 * at once. If it is the fighter looked through and the turn is more than a
 * quarter, the first-person view follows over the next frames instead of
 * jumping. */
void camera_EaseTurn(s32 fighter_index, s32 turn) {
    if (!g_fight_first_person) {
        return;
    }
    if (g_fight_viewer != fighter_index) {
        return;
    }
    turn &= ANGLE_MASK;
    if (turn >= ANGLE_TURN / 2) {
        turn = ANGLE_TURN - turn;
    }
    if (turn > ANGLE_TURN / 4) {
        g_camera_turn_frames = FIRST_PERSON_TURN_FRAMES;
        g_camera_second.turn_lag = ANGLE_TURN / 2;
    }
}

/* Before a replay: both views are to be placed afresh by the first frame. */
void camera_StartReplay(void) {
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    g_camera_mode = CAMERA_SIDE;
    g_camera_main.replay_started = 0;
    g_camera_second.replay_started = 0;
    stage_SetFarView(0);
}

/* A frame of one of the replay's views. `followed` and `other` are the two
 * fighters' recorded frames; the focus is between them, `weight` (of
 * ONE) towards the followed one, at eye height. `progress` is how far
 * through the replay it is (of ONE): the camera closes in as the replay
 * goes on, and sinks to look up at the fighters, or, once the followed
 * fighter is down, rises to look down on it. The first frame places the
 * view; the rest ease it a sixteenth of the way. */
static void camera_FollowReplay(
    CameraView *view, ReplayFighterFrame *followed, ReplayFighterFrame *other,
    s32 progress, s32 weight, s32 yaw) {
    s32 other_weight = ONE - weight;
    s32 remaining = ONE - progress;
    Fighter *fighter = &g_fighters[g_fight_winner];
    s32 dx;
    s32 dy;
    s32 dz;
    /* each eased value as it is, and what it is easing to */
    s32 current;
    s32 is_down;
    s32 target;
    s32 distance_delta;
    if (!view->replay_started) {
        view->replay_started = 1;
        view->focus.x =
            ((weight * followed->pos_x) + (other_weight * other->pos_x)) >> 12;
        view->focus.y =
            (((weight * followed->pos_y) + (other_weight * other->pos_y)) >>
             12) -
            CAMERA_REPLAY_EYE_HEIGHT;
        g_camera_replay_down_frames = 0;
        view->focus.z =
            ((weight * followed->pos_z) + (other_weight * other->pos_z)) >> 12;
        view->angles.vy = yaw;
        view->angles.vx = CAMERA_REPLAY_PITCH;
        view->angles.vz = 0;
        view->eye_distance = ((progress * CAMERA_REPLAY_DISTANCE_FIRST_END) +
                              (remaining * CAMERA_REPLAY_DISTANCE_FIRST)) >>
                             12;
        return;
    }
    if ((fighter->foot_points[0].y + fighter->foot_points[1].y) / 2 -
            fighter->head_pos.y <
        CAMERA_REPLAY_DOWN_HEIGHT) {
        g_camera_replay_down_frames += 1;
    }
    is_down = g_camera_replay_down_frames > CAMERA_REPLAY_DOWN_FRAMES;
    current = view->focus.x;
    target = ((weight * followed->pos_x) + (other_weight * other->pos_x)) >> 12;
    dx = target - current;
    view->focus.x = current + dx / CAMERA_REPLAY_EASE;
    current = view->focus.y;
    target =
        (((weight * followed->pos_y) + (other_weight * other->pos_y)) >> 12) -
        CAMERA_REPLAY_EYE_HEIGHT;
    dy = target - current;
    view->focus.y = current + dy / CAMERA_REPLAY_EASE;
    current = view->focus.z;
    target = ((weight * followed->pos_z) + (other_weight * other->pos_z)) >> 12;
    dz = target - current;
    view->focus.z = current + dz / CAMERA_REPLAY_EASE;
    if (is_down) {
        target =
            ((progress * CAMERA_REPLAY_PITCH_RISE) >> 12) + CAMERA_REPLAY_PITCH;
    } else {
        /* less a sixteenth of a turn by the end */
        target = CAMERA_REPLAY_PITCH - ((progress << 8) >> 12);
    }
    view->angles.vx =
        view->angles.vx +
        camera_EaseAngle(target - view->angles.vx, CAMERA_REPLAY_EASE);
    view->angles.vy +=
        camera_EaseAngle(yaw - view->angles.vy, CAMERA_REPLAY_EASE);
    view->angles.vz = 0;
    if (is_down) {
        target = ((progress * CAMERA_REPLAY_DISTANCE_END_DOWN) +
                  (remaining * CAMERA_REPLAY_DISTANCE)) >>
                 12;
    } else {
        target = ((progress * CAMERA_REPLAY_DISTANCE_END) +
                  (remaining * CAMERA_REPLAY_DISTANCE)) >>
                 12;
    }
    current = view->eye_distance;
    distance_delta = target - current;
    view->eye_distance = current + distance_delta / CAMERA_REPLAY_EASE;
    CLEAR_OPEN_CELLS(*view);
}

/* A frame of the replay's main view: from beside the fighters, swinging
 * round them as the replay goes on. While they are near each other it looks
 * between them, leaning to the followed one; apart, at the followed one. */
void camera_UpdateReplayMain(
    ReplayFighterFrame *followed, ReplayFighterFrame *other, s32 progress) {
    s32 bearing;
    s32 weight;
    s32 swing;

    if (g_fight_distance <= CAMERA_REPLAY_NEAR) {
        weight = (progress / 2) + ONE / 2;
    } else {
        weight = ONE;
    }
    bearing =
        ratan2(other->pos_x - followed->pos_x, other->pos_z - followed->pos_z);
    /* the sine of a quarter turn over the replay, as three quarters of a
     * turn */
    swing = g_math_sin_table[(progress / 4) & ANGLE_MASK] * 3 / 4;
    camera_FollowReplay(&g_camera_main, followed, other, progress, weight,
                        (CAMERA_REPLAY_MAIN_YAW - bearing) - swing);
}

/* A frame of the replay's second view: from behind the followed fighter's
 * shoulder, on the middle of the two while they are near each other and on
 * the other fighter when they are apart. */
void camera_UpdateReplaySecond(
    ReplayFighterFrame *followed, ReplayFighterFrame *other, s32 progress) {
    s32 weight;
    /* half of ONE, or none */
    weight = (g_fight_distance <= CAMERA_REPLAY_NEAR) << 11;
    camera_FollowReplay(
        &g_camera_second, followed, other, progress, weight,
        CAMERA_REPLAY_SECOND_YAW - ratan2(other->pos_x - followed->pos_x,
                                          other->pos_z - followed->pos_z));
}

/* Looks at a fighter from `yaw`, from a little above: the screen after a
 * fight circles with it. */
void camera_LookAtFighter(Fighter *fighter, s32 yaw) {
    stage_SetFarView(0);
    g_camera_main.focus = fighter->hip_pos;
    g_camera_main.angles.vx = LOOK_PITCH;
    g_camera_main.angles.vy = yaw;
    g_camera_main.angles.vz = 0;
    g_camera_main.eye_distance = LOOK_DISTANCE;
}

/* The swing round to the winner after the last blow. `progress` runs from 0
 * to ONE: the view comes in, drops from looking down to looking a little
 * up, and turns a quarter of the way round the fighter. */
void camera_SwingToFighter(Fighter *fighter, s32 progress) {
    s32 remaining = ONE - progress;

    stage_SetFarView(0);

    /* FAKE: the x, y and z of foot_pos, copied as one */
    g_camera_main.focus = *(Vec3 *)&fighter->foot_pos;

    g_camera_main.focus.y -= SWING_FOCUS_HEIGHT;

    g_camera_main.angles.vx =
        SWING_PITCH_START - (progress * SWING_PITCH_DROP) / ONE;

    {
        s32 yaw = SWING_YAW_START - progress / 4;
        u16 facing = fighter->orientation.vy;
        g_camera_main.eye_distance =
            (progress * SWING_DISTANCE_END +
             remaining * SWING_DISTANCE_START) >>
            12;
        g_camera_main.angles.vz = 0;
        g_camera_main.angles.vy = yaw - facing;
    }
}
