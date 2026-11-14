/* A model's face: the eyes and the mouth are rectangles of video memory
 * copied over the face texture. A file of its own in the original (its
 * jump table is on the other 8-byte grid from model.c's, and its variables
 * end before model.c's begin). */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s32 g_model_face_move_frame;
static u16 g_model_face_parts[];
static u8 g_model_face_part_masks[64];

/* A face part is a rectangle of video memory copied over the eyes or the
 * mouth of a model's face texture. The copies are drawing commands, done
 * before anything else in the frame; there is room for ten a frame. */
enum {
    FACE_PART_EYES_FIRST = 0,  /* three */
    FACE_PART_MOUTH_FIRST = 3, /* two */
    FACE_PART_OWN_EYES = 5,
    FACE_PART_OWN_MOUTH = 6,
    FACE_PART_COUNT,
};

/* The parts a face is put together from: the spare eyes and mouths. */
#define FACE_SPARE_PARTS FACE_PART_OWN_EYES

/* A set of spare parts, as a face names its own and
 * g_model_face_part_masks the ones a model has: the first part is the
 * highest bit. */
#define FACE_PART_BIT(part) (1 << (FACE_SPARE_PARTS - 1 - (part)))
#define FACE_EYES(n) FACE_PART_BIT(FACE_PART_EYES_FIRST + (n))
#define FACE_MOUTH(n) FACE_PART_BIT(FACE_PART_MOUTH_FIRST + (n))

/* A row of g_model_face_parts: where the part is copied from, how big it
 * is, and where it goes. */
#define FACE_PART_FIELDS 6

#define FACE_MOVES_PER_FRAME 10

static DR_MOVE g_model_face_moves[2][FACE_MOVES_PER_FRAME];

/* The next free copy command of this frame's ten. */
static DR_MOVE *g_model_face_move_cursor;

/* Queues the copy of face part `part` for the model in slot `slot`. The
 * second slot's textures are to the right of the first's. */
static void model_CopyFacePart(s32 slot, s32 part) {
    s32 frame;
    RECT src;
    u16 *row;
    s16 x, y;

    frame = g_disp_frame_count & 1;
    if (frame != g_model_face_move_frame) {
        g_model_face_move_cursor = g_model_face_moves[frame];
        g_model_face_move_frame = frame;
    }
    if (g_model_face_move_cursor !=
        g_model_face_moves[g_model_face_move_frame] + FACE_MOVES_PER_FRAME) {
        row = g_model_face_parts + part * FACE_PART_FIELDS;
        src.x = *row++;
        src.y = *row++;
        src.w = *row++;
        src.h = *row++;
        x = *row++;
        y = *row;
        if (slot != 0) {
            src.x = src.x + MODEL_SECOND_SLOT_DX;
            x = x + MODEL_SECOND_SLOT_DX;
        }
        SetDrawMove(g_model_face_move_cursor, &src, x, y);
        addPrim(&g_render_ot[RENDER_OT_FARTHEST], g_model_face_move_cursor);
        g_model_face_move_cursor++;
    }
}

/* Gives the model in slot `slot` face `face` (0..5: one of three pairs of
 * eyes with one of two mouths; MODEL_FACE_RESTORE: its own). Models that
 * have no such parts are left alone. */
void model_SetFace(s32 slot, s32 face) {
    Model *model;
    s32 parts; /* FACE_PART_BIT of each part to copy */
    s32 i;

    model = model_Get(slot);
    if (model) {
        switch (face) {
        case 0:
            parts = FACE_EYES(2) | FACE_MOUTH(1);
            break;
        case 1:
            parts = FACE_EYES(1) | FACE_MOUTH(1);
            break;
        case 2:
            parts = FACE_EYES(0) | FACE_MOUTH(1);
            break;
        case 3:
            parts = FACE_EYES(2) | FACE_MOUTH(0);
            break;
        case 4:
            parts = FACE_EYES(1) | FACE_MOUTH(0);
            break;
        case 5: /* the last face */
            parts = FACE_EYES(0) | FACE_MOUTH(0);
            break;
        case MODEL_FACE_RESTORE:
            model_CopyFacePart(slot, FACE_PART_OWN_EYES);
            model_CopyFacePart(slot, FACE_PART_OWN_MOUTH);
            parts = 0;
            break;
        }
        parts = parts & g_model_face_part_masks[model->model_id];
        for (i = 0; i < FACE_SPARE_PARTS; i++) {
            if (parts & 1) {
                model_CopyFacePart(slot, FACE_SPARE_PARTS - 1 - i);
            }
            parts >>= 1;
        }
    }
}

/* ---- data ---- */

/* The face parts: where each is copied from and to in video memory, for the
 * first slot. The face is the top of the model's first texture page; the
 * spare eyes and mouths are in the three columns beside it, and the
 * texture's own are kept further along. */
/* clang-format off */
#define FACE_X MODEL_TEXTURES_X
static u16 g_model_face_parts[FACE_PART_COUNT * FACE_PART_FIELDS] = {
    /* from x,   y,  w,  h,  to x,   y */
    FACE_X + 16,  0, 16, 32, FACE_X,  0, /* eyes */
    FACE_X + 32,  0, 16, 32, FACE_X,  0,
    FACE_X + 48,  0, 16, 32, FACE_X,  0,
    FACE_X + 16, 32, 16, 40, FACE_X, 32, /* mouths */
    FACE_X + 32, 32, 16, 40, FACE_X, 32,
    FACE_X + 64, 64, 16, 32, FACE_X,  0, /* the texture's own eyes */
    FACE_X + 80, 64, 16, 40, FACE_X, 32, /* and mouth */
};
/* clang-format on */

/* Which spare parts each model has, by model id: FACE_PART_BIT of each.
 * Two models have the spare eyes and no spare mouth. */
#define EYES (FACE_EYES(0) | FACE_EYES(1) | FACE_EYES(2))
#define ALL (EYES | FACE_MOUTH(0) | FACE_MOUTH(1))
/* clang-format off */
static u8 g_model_face_part_masks[64] = {
    ALL,  /* ROSTER_KANNUKI_STORY */
    ALL,  /* ROSTER_TATSUMI_STORY */
    ALL,  /* ROSTER_MIKADO_STORY */
    ALL,  /* ROSTER_KANNUKI */
    ALL,  /* ROSTER_TATSUMI */
    ALL,  /* ROSTER_MIKADO */
    ALL,  /* ROSTER_UTSUSEMI */
    ALL,  /* ROSTER_RED_SHADOW */
    ALL,  /* ROSTER_TSUBAME */
    ALL,  /* ROSTER_SAZANKA */
    ALL,  /* ROSTER_SUMINAGASHI */
    EYES, /* ROSTER_NIGHTSTALKER */
    ALL,  /* ROSTER_MATSUMUSHI */
    ALL,  /* ROSTER_VANISHER */
    0,    /* ROSTER_SIDEARM_1 */
    ALL,  /* ROSTER_GENGORO_STORY */
    ALL,  /* ROSTER_KAUN_STORY */
    ALL,  /* ROSTER_JO_STORY */
    ALL,  /* ROSTER_GENGORO */
    ALL,  /* ROSTER_KAUN */
    ALL,  /* ROSTER_JO */
    ALL,  /* ROSTER_HONGOU */
    ALL,  /* ROSTER_KATZE */
    ALL,  /* ROSTER_HIGHWAYMAN */
    ALL,  /* ROSTER_CHIHIRO */
    ALL,  /* ROSTER_UTAMARU */
    ALL,  /* ROSTER_TONY_UMEDA */
    ALL,  /* ROSTER_ISOHACHI */
    ALL,  /* ROSTER_ARMORED */
    0,    /* ROSTER_SIDEARM_2 */
    ALL,  /* ROSTER_LAST_1 */
    /* models 31 to 57, which no roster entry has */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    EYES, /* chanbara's kind 0 */
    ALL,  /* chanbara's kind 1 */
    ALL,  /* chanbara's kind 2 */
    ALL,  /* chanbara's kind 3 */
    0,    /* ROSTER_VICTIM */
    ALL,  /* ROSTER_LAST_2 */
};
/* clang-format on */
#undef ALL
#undef EYES

/* Which frame's copy commands g_model_face_move_cursor is in. */
static s32 g_model_face_move_frame = -1;
