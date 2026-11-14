/* The stage (stage.h): its distance fog, and its grid of cells: reading
 * them from the stage file, working out which the camera sees, and queueing
 * what stands on them. */
#include "common.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static void stage_SetCellDetail(s32 cell_x, s32 cell_z, s32 detail);
static void stage_AddFarthestPrim(u32 packet_addr);

/* Defined further down. */
static void stage_SetFogMoves(
    RECT *src_rect, s32 move_count, DR_MOVE (*move_pairs)[2]);
static void stage_QueueFogMoves(
    u32 move_depth, s32 move_count, DR_MOVE (*move_pairs)[2],
    u32 depth_span_limit, s32 link_farthest);
static s32 g_stage_fogged_model;
static void stage_OpenCell(s16 *cell_pos);
static s32 g_stage_first_person;
static StageNode **stage_QueueFarCells(
    s32 window_first_col, s32 window_first_row, StageNode **seen);
static s32 g_stage_run_entry_peak;

/* The fog's copies: one for each frame's buffer. */
static DR_MOVE g_stage_fog_moves[STAGE_FOG_LEVELS][2];
static DR_MOVE g_stage_model_fog_moves[STAGE_MODEL_FOG_LEVELS][2];
/* 0x80 bytes that no code names: one variable or several. */
static u8 g_stage_unused_block[0x80];

/* The most a stage can have of each. */
#define STAGE_MAX_TILES 500
#define STAGE_MAX_NODES 60
#define STAGE_MAX_RUN_ENTRIES 1000

/* An entry of a cell's list: the number of a tile or a piece, and a bit on
 * the last of the list. */
#define RUN_ENTRY_NUMBER 0x7FFF
#define RUN_ENTRY_LAST 0x8000
/* A tile in the stage file: its mesh, and a bit if it can be opened. */
#define FILE_TILE_MESH 0x7FFF
#define FILE_TILE_CAN_OPEN 0x8000

static StageTile g_stage_tiles[STAGE_MAX_TILES];
static StageNode g_stage_nodes[STAGE_MAX_NODES];
/* The pieces met while the cells are gone through, each once. */
static StageNode *g_stage_seen_nodes[STAGE_MAX_NODES];
/* For each cell, where its list starts in g_stage_run_entries, or -1. A
 * list is numbers of tiles and, from g_stage_tile_count up, of pieces; the
 * last has bit 15 set. */
static s16 g_stage_cell_runs[STAGE_CELLS][STAGE_CELLS];
static u16 g_stage_run_entries[STAGE_MAX_RUN_ENTRIES];
/* STAGE_CELL_... for each cell, row by row. */
static u8 g_stage_cell_flags[STAGE_CELLS * STAGE_CELLS];

static s32 g_stage_tile_count;
/* The way the camera looked when the stage was last drawn. */
static s32 g_stage_view_yaw;

/* The fog is never shallower than this, in world units. */
#define FOG_MIN_SPAN 6000

/* The stage's colour tables in video memory: one row, with the fogged
 * copies in the rows under it. */
#define FOG_CLUT_X 0
#define FOG_CLUT_Y 480
#define FOG_CLUT_W 320
/* The most colours fogged at once: a model's block (the stage's row is
 * fewer). */
#define FOG_MAX_COLORS (STAGE_FOG_MODEL_WIDTH * STAGE_FOG_MODEL_HEIGHT)

/* The five bits of a colour's byte that a pixel keeps. */
#define COLOR_PIXEL_BITS 0xF8

/* The scene's ordering table has this many entries; the fog's farthest
 * copy, when it is not linked in at the very end, goes four short of it. */
#define OT_LENGTH (RENDER_OT_FARTHEST + 1)
#define OT_FOG_FARTHEST (RENDER_OT_FARTHEST - 4)

/* ---- fog ---- */

/* Sets up the copies that bring each fogged row, and each fogged block of a
 * model, to where the textures look for their colours. */
void stage_InitFog(void) {
    RECT rect;
    rect.x = FOG_CLUT_X;
    rect.y = FOG_CLUT_Y;
    rect.w = FOG_CLUT_W;
    rect.h = 1;
    stage_SetFogMoves(&rect, STAGE_FOG_LEVELS, g_stage_fog_moves);
    rect.w = STAGE_FOG_MODEL_WIDTH;
    rect.x = STAGE_FOG_MODEL_X;
    rect.y = STAGE_FOG_MODEL_Y;
    rect.h = STAGE_FOG_MODEL_HEIGHT;
    stage_SetFogMoves(&rect, STAGE_MODEL_FOG_LEVELS, g_stage_model_fog_moves);
}

/* `move_count` copies to the rectangle `src_rect`, from the rectangles
 * that follow it downwards (and, at the bottom of video memory, from the
 * top of the next column). Each copy is made twice, for the two frames'
 * buffers. Leaves `src_rect` at the last source. */
static void stage_SetFogMoves(
    RECT *src_rect, s32 move_count, DR_MOVE (*move_pairs)[2]) {
    s32 start_x, start_y;
    s32 moves_after_this;

    start_x = src_rect->x;
    start_y = src_rect->y;

    for (moves_after_this = move_count - 1; moves_after_this != -1;
         moves_after_this--) {
        s16 next_y;
        next_y = src_rect->y + src_rect->h;
        src_rect->y = next_y;
        if (next_y >= VRAM_HEIGHT) {
            src_rect->y = start_y;
            src_rect->x = src_rect->x + src_rect->w;
        }
        SetDrawMove(&(*move_pairs)[0], src_rect, start_x, start_y);
        (*move_pairs)[1] = (*move_pairs)[0];
        move_pairs++;
    }
}

/* Queues this frame's fog for stage `stage`, if it has any: the copies of
 * the stage's rows and of the fogged model's blocks, spread from the
 * camera's focus to where the scene ends. When the focus is so far that
 * less than FOG_MIN_SPAN is left, the fog starts nearer by the difference.
 * In the far view the fog keeps the depth and start it had on the frame the
 * view began. */
void stage_QueueFog(s32 stage, s32 eye_distance) {
    s32 *span_limit_entry;
    s32 span;
    s32 offset;

    g_stage_fog_ot_index = STAGE_FOG_NONE;
    if (*g_stage_fog_settings[stage]) {
        {
            /* whether the view is the first-person one, then where its
             * scene ends */
            s32 far = stage_IsFirstPerson();
            if (!far) {
                far = STAGE_FAR;
            } else {
                far = STAGE_FAR_FIRST_PERSON;
            }
            span = far - eye_distance;
        }
        if (span < FOG_MIN_SPAN) {
            offset = FOG_MIN_SPAN - span;
            span = FOG_MIN_SPAN;
        } else {
            offset = 0;
        }
        {
            s16 far_view = g_scene_settings.far_view.value;
            s16 last_far_view = g_stage_fog_last_far_view;
            if ((far_view & ~last_far_view) & 1) {
                /* the far view has just begun */
                g_stage_far_fog_span = span;
                g_stage_far_fog_offset = offset;
            }
            if (far_view & 1) {
                eye_distance -= g_stage_far_fog_offset;
            } else {
                eye_distance -= offset;
            }
        }
        g_stage_fog_last_far_view = g_scene_settings.far_view.value;
        span_limit_entry = &g_stage_fog_spans[stage];
        {
            /* FAKE: g_stage_fog_settings[stage][1], with no index */
            s32 index = stage * 4;
            stage_QueueFogMoves(
                eye_distance, STAGE_FOG_LEVELS, g_stage_fog_moves,
                *span_limit_entry,
                *(s16 *)((u8 *)g_stage_fog_settings + 2 + index));
            stage_QueueFogMoves(
                eye_distance, STAGE_MODEL_FOG_LEVELS, g_stage_model_fog_moves,
                *span_limit_entry,
                *(s16 *)((u8 *)g_stage_fog_settings + 2 + index));
        }
    }
}

/* Links `move_count` copies into the scene's ordering table at even steps
 * of depth from `move_depth` to the end of the scene, the plain colours
 * nearest; the steps cover at most `depth_span_limit`. The most fogged copy
 * is not given a depth: it is the first thing drawn, either at the far end
 * of the table (`link_farthest`) or just before it. */
static void stage_QueueFogMoves(
    u32 move_depth, s32 move_count, DR_MOVE (*move_pairs)[2],
    u32 depth_span_limit, s32 link_farthest) {
    s32 limit;
    /* the depth the copies are spread over, then the step between two */
    s32 depth_delta;
    DR_MOVE *move;
    s32 i;

    move = *move_pairs;

    if (link_farthest) {
        limit = move_count;
    } else {
        limit = move_count - 1;
    }

    if (g_scene_settings.far_view.value & 1) {
        depth_delta = g_stage_far_fog_span;
    } else {
        if (!stage_IsFirstPerson()) {
            depth_delta = STAGE_FAR - move_depth;
        } else {
            depth_delta = STAGE_FAR_FIRST_PERSON - move_depth;
        }
    }

    if (depth_delta < 0) {
        return;
    }

    if (depth_span_limit < depth_delta) {
        depth_delta = depth_span_limit / move_count;
    } else {
        depth_delta = depth_delta / move_count;
    }

    /* this frame's buffer's copy of each pair */
    move += g_disp_frame_count & 1;

    for (i = 0; i < limit; i++) {
        s32 index = render_DepthToOtShifted(move_depth >> 2, 2);
        if (index < OT_LENGTH) {
            addPrim(&g_render_ot[index], move);
            move += 2;
            if (i == limit - 1) {
                g_stage_fog_ot_index = index;
            }
        }
        move_depth += depth_delta;
    }

    if (link_farthest) {
        stage_AddFarthestPrim((u32)move);
    } else {
        addPrim(&g_render_ot[OT_FOG_FARTHEST], move);
    }
}

/* Makes `packet_addr` the first primitive drawn of the scene: the table's
 * last entry is pointed at it (an entry is a primitive's address, 24 bits,
 * under a length that is left 0). */
static void stage_AddFarthestPrim(u32 packet_addr) {
    u32 *ot = g_render_ot;
    packet_addr &= 0xFFFFFF; /* the address in a tag */
    ot[RENDER_OT_FARTHEST] = packet_addr;
}

/* Makes the fogged copies of the colours in the rectangle `rect` of video
 * memory. The colours themselves are first copied to the rectangle above,
 * where the textures look for them. Then `count` - 1 copies are written
 * below, each blended further towards the fog colour (the GTE's far
 * colour), the last being the fog colour itself; at the bottom of video
 * memory they go on from the top of the next column. Colour 0, the
 * transparent one, stays as it is. */
static void stage_BuildFogLevels(RECT *rect, s32 count) {
    u16 src_buf[FOG_MAX_COLORS];
    u16 dst_buf[FOG_MAX_COLORS];
    CVECTOR far_color;
    s32 unused[2]; /* nothing uses it */
    s32 i;
    s32 saved_y;
    u32 r;
    s32 g;
    s32 b;
    s32 far_color_pixel;

    DrawSync(0);
    count--;
    StoreImage(rect, (u32 *)src_buf);
    DrawSync(0);
    rect->y -= rect->h;
    LoadImage(rect, (u32 *)src_buf);
    saved_y = rect->y;
    rect->y = rect->h + saved_y;
    gte_ReadFarColor(&far_color);

    r = far_color.r;
    g = far_color.g;
    b = far_color.b;
    far_color_pixel =
        ((r >> 3) | -PIXEL_TOP_BIT) | ((g & COLOR_PIXEL_BITS) << 2) |
        ((b & COLOR_PIXEL_BITS) << 7);

    for (i = 0; i < count; i++) {
        u16 *src = src_buf;
        u16 *dst = dst_buf;
        /* how much of the fog colour this copy has, of ONE */
        s32 factor = ((i + 1) << 12) / count;
        s32 j;
        for (j = 0; j < rect->w * rect->h; j++) {
            if (i == count - 1) {
                u16 pixel = *src;
                if (pixel == 0) {
                    *dst = pixel;
                    src++;
                    dst++;
                    continue;
                }
                *dst++ = far_color_pixel;
                src++;
                continue;
            }
            {
                u16 pixel = *src;
                u32 px = pixel & 0xFFFF;
                if (px == 0) {
                    *dst = pixel;
                    src++;
                    dst++;
                    continue;
                }
                {
                    s32 red = (pixel & PIXEL_PART_MASK) << 3;
                    s32 green_work = (px >> 2) & COLOR_PIXEL_BITS;
                    s32 sum;
                    s32 weighted_source_red;
                    s32 weighted_source_green;
                    s32 green;
                    s32 blue;
                    src++;
                    /* FAKE: each colour blended in one expression */
                    weighted_source_red = red * (ONE - factor);
                    red = r * factor;
                    sum = weighted_source_red + red;
                    red = sum >> 15;
                    red = red & PIXEL_PART_MASK;
                    weighted_source_green = green_work * (ONE - factor);
                    green_work = g * factor;
                    sum = weighted_source_green + green_work;
                    green_work = sum >> 10;
                    blue = (px >> 7) & COLOR_PIXEL_BITS;
                    blue = blue * (ONE - factor);
                    sum = blue + b * factor;
                    blue = sum >> 5;
                    green = green_work & PIXEL_GREEN_MASK;
                    *dst = (pixel & PIXEL_TOP_BIT) | red | green |
                           (blue & PIXEL_BLUE_MASK);
                }
            }
            dst++;
        }

        rect->y += rect->h;
        if (rect->y >= VRAM_HEIGHT) {
            rect->y = saved_y;
            rect->x += rect->w;
        }
        LoadImage(rect, (u32 *)dst_buf);
        DrawSync(0);
    }
}

/* The stage's colour tables, loaded one row down by tex_LoadStageGroup. */
void stage_BuildFog(void) {
    RECT rect;
    rect.y = FOG_CLUT_Y + 1;
    rect.w = FOG_CLUT_W;
    rect.x = FOG_CLUT_X;
    rect.h = 1;
    stage_BuildFogLevels(&rect, STAGE_FOG_LEVELS);
}

/* A model's, moved one block down by stage_FogModel. */
void stage_BuildModelFog(void) {
    RECT rect;
    rect.x = STAGE_FOG_MODEL_X;
    rect.y = STAGE_FOG_MODEL_Y + STAGE_FOG_MODEL_HEIGHT;
    rect.w = STAGE_FOG_MODEL_WIDTH;
    rect.h = STAGE_FOG_MODEL_HEIGHT;
    stage_BuildFogLevels(&rect, STAGE_MODEL_FOG_LEVELS);
}

/* Has the model in slot `slot_index` fogged with the stage: its colour
 * tables are copied into the fog's block, its meshes pointed at them, and
 * the fogged copies made. The first-person view does this for the fighter
 * who is seen. A model fogged before is put back first. */
void stage_FogModel(s32 slot_index) {
    RECT rect;
    Model *model;

    if (g_stage_fogged_model != slot_index) {
        stage_UnfogModel();
        model = model_Get(slot_index);
        if (model) {
            if (slot_index != 0) {
                rect.x = MODEL_CLUT_X + MODEL_SECOND_SLOT_DX;
            } else {
                rect.x = MODEL_CLUT_X;
            }
            rect.y = MODEL_CLUT_Y;
            rect.w = STAGE_FOG_MODEL_WIDTH;
            rect.h = MODEL_CLUT_ROWS;
            MoveImage(&rect, STAGE_FOG_MODEL_X,
                      STAGE_FOG_MODEL_Y + STAGE_FOG_MODEL_HEIGHT);
            if (slot_index == 0) {
                tex_ShiftBlock(model->mesh_block, 0, 0, MODEL_FOG_CLUT_DX,
                               MODEL_FOG_CLUT_DY);
            } else {
                tex_ShiftBlock(model->mesh_block, 0, 0,
                               MODEL_FOG_CLUT_DX - MODEL_SECOND_SLOT_DX,
                               MODEL_FOG_CLUT_DY);
            }
            DrawSync(0);
            stage_BuildModelFog();
        }
    }
    g_stage_fogged_model = slot_index;
}

/* Points the fogged model's meshes back at its own colour tables. */
void stage_UnfogModel(void) {
    Model *model;

    if (g_stage_fogged_model != -1) {
        model = model_Get(g_stage_fogged_model);
        if (model) {
            if (g_stage_fogged_model == 0) {
                tex_ShiftBlock(model->mesh_block, 0, 0, -MODEL_FOG_CLUT_DX,
                               -MODEL_FOG_CLUT_DY);
            } else {
                tex_ShiftBlock(model->mesh_block, 0, 0,
                               MODEL_SECOND_SLOT_DX - MODEL_FOG_CLUT_DX,
                               -MODEL_FOG_CLUT_DY);
            }
        }
        g_stage_fogged_model = -1;
    }
}

s32 stage_GetFoggedModel(void) { return g_stage_fogged_model; }

/* Clears the far view's second bit, which nothing reads. Nothing calls
 * it. */
static void stage_ClearFarViewBit1(void) {
    g_scene_settings.far_view.bits.bit1 = 0;
}

u32 stage_GetFogOtIndex(void) { return g_stage_fog_ot_index; }

/* ---- the cells ---- */

/* Marks every cell that the line from (x0, z0) to (end_x, end_z) passes
 * through as STAGE_CELL_OPEN. The line is walked a cell at a time along the
 * axis it runs further on; where it also crosses into the next cell of the
 * other axis, that cell is marked too. x0 and z0 go on to hold the line's
 * length along each axis. */
static void stage_OpenCellsAlong(s32 x0, s32 z0, s32 end_x, s32 end_z) {
    s16 cell_pos[4];
    s16 minor_step_cell_pos[4];
    s32 minor_step;
    s32 to_x;
    s32 to_z;
    s32 end_cell_x;
    s32 slope;
    s32 from_z;
    s32 end_cell_z;
    s32 from_x;
    s32 start_cell_center_z;
    s32 swap;
    s32 start_cell_center_x;
    s32 cell_z;
    s32 cell_x;
    s32 steps_left;
    s32 z_is_major;
    s32 z_direction;
    s32 x_direction;

    from_x = x0 + STAGE_HALF_SIZE;
    from_z = z0 + STAGE_HALF_SIZE;
    to_x = end_x + STAGE_HALF_SIZE;
    to_z = end_z + STAGE_HALF_SIZE;
    x0 = to_x - from_x;
    z0 = to_z - from_z;
    cell_x = from_x / STAGE_CELL_SIZE;
    cell_z = from_z / STAGE_CELL_SIZE;
    end_cell_x = to_x / STAGE_CELL_SIZE;
    end_cell_z = to_z / STAGE_CELL_SIZE;
    if ((x0 == 0) && (z0 == 0)) {
        if ((cell_x >= 0) && (cell_z >= 0) && (cell_x < STAGE_CELLS) &&
            (cell_z < STAGE_CELLS)) {
            cell_pos[0] = cell_x - STAGE_CELLS / 2;
            cell_pos[2] = cell_z - STAGE_CELLS / 2;
            stage_OpenCell(cell_pos);
        }
    } else {
        /* from here the two ends are measured from the middle of the first
         * cell, forwards along each axis, and with the longer axis first */
        start_cell_center_x = (cell_x * STAGE_CELL_SIZE) + STAGE_CELL_SIZE / 2;
        from_x -= start_cell_center_x;
        start_cell_center_z = (cell_z * STAGE_CELL_SIZE) + STAGE_CELL_SIZE / 2;
        from_z -= start_cell_center_z;
        to_x -= start_cell_center_x;
        to_z -= start_cell_center_z;
        if (x0 < 0) {
            x_direction = -1;
            x0 = -x0;
            from_x = -from_x;
            to_x = -to_x;
        } else {
            x_direction = 1;
        }
        z_direction = 1;
        if (z0 < 0) {
            z_direction = -1;
            z0 = -z0;
            from_z = -from_z;
            to_z = -to_z;
        }
        if (x0 < z0) {
            swap = x0;
            x0 = z0;
            z0 = swap;
            swap = from_x;
            from_x = from_z;
            from_z = swap;
            to_x = to_z;
            z_is_major = 1;
        } else {
            z_is_major = 0;
        }
        slope = (z0 << 12) / x0; /* of ONE */
        to_x += STAGE_CELL_SIZE / 2;
        from_x += STAGE_CELL_SIZE / 2;
        steps_left = (to_x / STAGE_CELL_SIZE) - (from_x / STAGE_CELL_SIZE);
        from_z += STAGE_CELL_SIZE / 2;
        from_z -= (from_x * slope) >> 12;
        minor_step = (slope * STAGE_CELL_SIZE) >> 12;
        for (; steps_left != -1; steps_left--) {
            if (cell_x >= 0) {
                if ((cell_z >= 0) && (cell_x < STAGE_CELLS) &&
                    (cell_z < STAGE_CELLS)) {
                    cell_pos[0] = cell_x - STAGE_CELLS / 2;
                    cell_pos[2] = cell_z - STAGE_CELLS / 2;
                    stage_OpenCell(cell_pos);
                }
            }
            from_z %= STAGE_CELL_SIZE;
            from_z += minor_step;
            if (steps_left == 0) {
                break;
            }
            if (from_z >= STAGE_CELL_SIZE + 1) {
                if (z_is_major) {
                    if (x_direction < 0) {
                        cell_x -= 1;
                    } else {
                        cell_x += 1;
                    }
                } else if (z_direction < 0) {
                    cell_z -= 1;
                } else {
                    cell_z += 1;
                }
                if ((cell_x >= 0) && (cell_z >= 0) && (cell_x < STAGE_CELLS) &&
                    (cell_z < STAGE_CELLS)) {
                    minor_step_cell_pos[0] = cell_x - STAGE_CELLS / 2;
                    minor_step_cell_pos[2] = cell_z - STAGE_CELLS / 2;
                    stage_OpenCell(minor_step_cell_pos);
                }
            }
            if (z_is_major) {
                if (z_direction < 0) {
                    cell_z -= 1;
                } else {
                    cell_z += 1;
                }
            } else if (x_direction < 0) {
                cell_x -= 1;
            } else {
                cell_x += 1;
            }
        }
        if ((end_cell_x >= 0) && (end_cell_z >= 0) &&
            (end_cell_x < STAGE_CELLS) && (end_cell_z < STAGE_CELLS)) {
            cell_pos[0] = end_cell_x - STAGE_CELLS / 2;
            cell_pos[2] = end_cell_z - STAGE_CELLS / 2;
            stage_OpenCell(cell_pos);
        }
    }
}

/* The same from where the camera is: the fight calls it with each
 * fighter's place, so that what stands between the camera and a fighter can
 * be opened. */
void stage_OpenCellsTo(s32 end_x, s32 end_z) {
    stage_OpenCellsAlong(g_node_camera_root.node.local.t[0],
                         g_node_camera_root.node.local.t[2], end_x, end_z);
}

/* Which cells are drawn. A mask is a word for each row of the 31 x 31
 * cells around the camera, the highest bit but one the first column. There
 * is a set of masks for each of 64 ways the camera can look, for the usual
 * view and for the first-person view; in the usual view only the cells
 * near the camera are kept of it. */

/* Queues the stage for drawing. Goes through the cells of the mask for the
 * way the camera looks; each one's tiles are queued with the cell's detail
 * and marked open or not, and its pieces are collected and queued after,
 * so that a piece standing on several cells is drawn once. Then the stage's
 * own hook is run and the stage's flames are drawn.
 * `eye_distance` is not used. */
void stage_Draw(s32 eye_distance) {
    s32 matrix[sizeof(MATRIX) / 4];
    s16 ahead[4];
    s32 view_dir[3];
    s16 view_angles[4];
    s32 view_cell_x;
    s32 view_cell_z;
    s16 window_x; /* the window's first column and row */
    s16 window_z;
    s32 *mask;
    StageNode **seen;
    s32 i;
    s32 j;
    s32 bits;
    s16 row;
    s16 col;
    s16 run;
    u16 run_entry;
    s32 record_id;
    s32 cell_flags;
    StageTile *tile;
    StageNode *piece;
    void **queue;
    s32 yaw;
    s32 index;

    view_cell_x = (g_node_camera_root_ptr->local.t[0] + STAGE_HALF_SIZE) /
                  STAGE_CELL_SIZE;
    view_cell_z = (g_node_camera_root_ptr->local.t[2] + STAGE_HALF_SIZE) /
                  STAGE_CELL_SIZE;
    stage_CalcViewAngles();
    math_RotMatrixYXZ(&g_node_camera_root_ptr->rot, (MATRIX *)matrix);
    ahead[2] = ONE;
    ahead[0] = 0;
    ahead[1] = 0;
    gte_ApplyMatrix(matrix, ahead, view_dir);
    yaw = ratan2(view_dir[0], view_dir[2]);
    g_stage_view_yaw = yaw;
    window_x = view_cell_x - STAGE_WINDOW / 2;
    window_z = view_cell_z - STAGE_WINDOW / 2;
    /* a turn is 64 times STAGE_VIEW_YAWS */
    index = (yaw >> 6) & (STAGE_VIEW_YAWS - 1);
    if (g_stage_first_person) {
        mask = g_stage_first_person_masks[index];
    } else if (g_scene_settings.far_view.value & 1) {
        mask = g_stage_view_masks[index];
    } else {
        /* the mask for the way the camera looks, less what is far off: made
         * on the scratchpad */
        s32 *near_row;
        s32 *masked_row;
        near_row = g_stage_near_mask;
        mask = g_stage_view_masks[index];
        masked_row = (s32 *)(SCRATCHPAD + 4);
        for (i = 0; i < STAGE_WINDOW; i++) {
            *masked_row++ = *near_row++ & *mask++;
        }
        mask = (s32 *)(SCRATCHPAD + 4);
    }

    seen = g_stage_seen_nodes;
    for (i = 0; i < STAGE_WINDOW; i++) {
        row = window_z + i;
        if (row < 0) {
            mask++;
            continue;
        }
        if (row >= STAGE_CELLS) {
            break;
        }
        bits = *mask++;
        if (bits == 0) {
            continue;
        }
        bits <<= 1;
        for (j = 0; j < STAGE_WINDOW; j++) {
            col = window_x + j;
            if (col < 0) {
                bits <<= 1;
                continue;
            }
            if (col >= STAGE_CELLS) {
                break;
            }
            if (bits < 0) {
                run = g_stage_cell_runs[row][col];
                if (run >= 0) {
                    cell_flags = g_stage_cell_flags[row * STAGE_CELLS + col];
                    do {
                        run_entry = g_stage_run_entries[run++];
                        record_id = run_entry & RUN_ENTRY_NUMBER;
                        if (record_id < g_stage_tile_count) {
                            tile = &g_stage_tiles[record_id];
                            tile->detail = cell_flags & STAGE_CELL_DETAIL;
                            if ((cell_flags & STAGE_CELL_OPEN_ALL) ||
                                ((cell_flags & STAGE_CELL_OPEN) &&
                                 (tile->draw_flags & STAGE_TILE_CAN_OPEN))) {
                                tile->draw_flags |= STAGE_TILE_OPEN;
                            } else {
                                tile->draw_flags &= ~STAGE_TILE_OPEN;
                            }
                            queue = g_render_queue_end;
                            g_render_queue_end = queue + 1;
                            *queue = tile;
                        } else {
                            piece =
                                &g_stage_nodes[record_id - g_stage_tile_count];
                            if (!piece->is_collected) {
                                *seen++ = piece;
                                piece->is_collected = 1;
                            }
                        }
                    } while (!(run_entry & RUN_ENTRY_LAST));
                }
            }
            bits <<= 1;
        }
    }

    if ((g_scene_settings.far_view.value & 1) && !g_stage_first_person) {
        seen = stage_QueueFarCells(window_x, window_z, seen);
    }
    while (seen != g_stage_seen_nodes) {
        seen--;
        *g_render_queue_end = *seen;
        ((StageNode *)*g_render_queue_end)->is_collected = 0;
        g_render_queue_end++;
    }
    if (g_stage_hooks[resource_GetStage()].update) {
        g_stage_hooks[resource_GetStage()].update();
    }
    view_angles[0] = -g_node_camera_root.node.rot.vx;
    view_angles[1] = -g_node_camera_root.node.rot.vy;
    view_angles[2] = -g_node_camera_root.node.rot.vz;
    eff_DrawFlames(view_angles, g_node_camera_root.node.world.t);
}

/* The far view: queues every cell of the stage that is outside the window
 * around the camera, whichever way the camera looks. The window's first
 * column and row are given. */
static StageNode **stage_QueueFarCells(
    s32 window_first_col, s32 window_first_row, StageNode **seen) {
    s32 row_masks[STAGE_CELLS];
    s32 mask;
    s32 i;
    s32 row;
    s32 col;
    s32 column_bits;
    s16 run;
    u16 run_entry;
    s32 record_id;
    s32 cell_flags;
    StageTile *tile;
    StageNode *piece;
    void **queue;

    /* the columns outside the window, as bits from the top down */
    if (window_first_col >= 0 ? window_first_col >= STAGE_CELLS
                              : -window_first_col >= STAGE_CELLS) {
        mask = -1;
    } else if (window_first_col < 0) {
        mask = -1U >> (window_first_col + STAGE_WINDOW);
    } else if (window_first_col == 0) {
        mask = 0;
    } else {
        mask = -1 << (STAGE_CELLS - window_first_col);
    }
    for (i = 0; i < STAGE_CELLS; i++) {
        if (i >= window_first_row && i < window_first_row + STAGE_WINDOW) {
            row_masks[i] = mask;
        } else {
            row_masks[i] = -1;
        }
    }

    for (row = 0; row < STAGE_CELLS; row++) {
        column_bits = row_masks[row];
        if (column_bits != 0) {
            for (col = 0; col < STAGE_CELLS; col++) {
                if (column_bits < 0) {
                    run = g_stage_cell_runs[row][col];
                    if (run >= 0) {
                        cell_flags =
                            g_stage_cell_flags[row * STAGE_CELLS + col];
                        do {
                            run_entry = g_stage_run_entries[run++];
                            record_id = run_entry & RUN_ENTRY_NUMBER;
                            if (record_id < g_stage_tile_count) {
                                tile = &g_stage_tiles[record_id];
                                tile->detail = cell_flags & STAGE_CELL_DETAIL;
                                if ((cell_flags & STAGE_CELL_OPEN_ALL) ||
                                    ((cell_flags & STAGE_CELL_OPEN) &&
                                     (tile->draw_flags &
                                      STAGE_TILE_CAN_OPEN))) {
                                    tile->draw_flags |= STAGE_TILE_OPEN;
                                } else {
                                    tile->draw_flags &= ~STAGE_TILE_OPEN;
                                }
                                queue = g_render_queue_end;
                                g_render_queue_end = queue + 1;
                                *queue = tile;
                            } else {
                                piece = &g_stage_nodes[record_id -
                                                       g_stage_tile_count];
                                if (!piece->is_collected) {
                                    *seen = piece;
                                    seen += 1;
                                    piece->is_collected = 1;
                                }
                            }
                        } while (!(run_entry & RUN_ENTRY_LAST));
                    }
                }
                column_bits = column_bits << 1;
            }
        }
    }

    return seen;
}

/* Reads the cells of a stage file, a stream of halfwords in three parts,
 * each ended by -1:
 *  - the tiles: the number of a cell (row * 32 + column) and a mesh, with
 *    bit 15 set if the tile can be opened;
 *  - the pieces: a mesh, a place (three words) and three angles;
 *  - for each cell that has anything, its number and its list of tiles and
 *    pieces, the last with bit 15 set.
 * `block_id` is the pointer table the meshes are in. Also sets the view's
 * tilt to none. */
void stage_ReadCells(u16 *stream, s32 block_id) {
    StageTile *tile;
    StageNode *piece;
    s32 i;
    s32 j;
    s16 count;
    s16 word; /* the halfword just read */
    s16 x;
    s16 z;
    u16 mesh_word;

    count = 0;
    while ((word = *stream++) != -1) {
        tile = &g_stage_tiles[count++];
        mesh_word = *stream++;
        tile->mesh = mesh_word;
        if (mesh_word & FILE_TILE_CAN_OPEN) {
            tile->mesh = mesh_word & FILE_TILE_MESH;
            tile->draw_flags = STAGE_TILE_CAN_OPEN;
        } else {
            tile->draw_flags = 0;
        }
        tile->kind = STAGE_TILE_KIND;
        tile->detail = 0;
        tile->mesh_block = block_id;
        tile->x = (word % STAGE_CELLS) * STAGE_CELL_SIZE - STAGE_HALF_SIZE;
        tile->z = (word / STAGE_CELLS) * STAGE_CELL_SIZE - STAGE_HALF_SIZE;
    }
    g_stage_tile_count = count;
    count = 0;
    while ((word = *stream++) != -1) {
        piece = &g_stage_nodes[count++];
        piece->node.draw_flags = 0;
        piece->node.parent = NULL;
        piece->node.rotation_order = ROTATION_ORDER_ZYX;
        piece->node.light_setup_index = LIGHT_SCENERY;
        piece->node.mesh = word;
        piece->node.mesh_block = block_id;
        piece->node.world.t[0] = *stream++;
        piece->node.world.t[0] |= *stream++ << 16;
        piece->node.world.t[1] = *stream++;
        piece->node.world.t[1] |= *stream++ << 16;
        piece->node.world.t[2] = *stream++;
        piece->node.world.t[2] |= *stream++ << 16;
        piece->node.rot.vx = *stream++;
        piece->node.rot.vy = *stream++;
        piece->node.rot.vz = *stream++;
        /* The original's behaviour: this compares the third angle with
         * whether the first two are equal. Whether all three angles are
         * zero was surely what was meant. */
        if (piece->node.rot.vz != (piece->node.rot.vx == piece->node.rot.vy)) {
            piece->node.kind = NODE_KIND_UNROTATED;
        } else {
            piece->node.kind = NODE_KIND_ROTATED;
        }
        g_math_rot_matrix_funcs[piece->node.rotation_order](
            &piece->node.rot, &piece->node.world);
        piece->is_collected = 0;
    }
    for (i = 0; i < STAGE_CELLS; i++) {
        for (j = 0; j < STAGE_CELLS; j++) {
            g_stage_cell_runs[i][j] = -1;
        }
    }
    /* `i` goes on to count the list entries */
    i = 0;
    while ((word = *stream++) != -1) {
        x = word % STAGE_CELLS;
        z = word / STAGE_CELLS;
        g_stage_cell_runs[z][x] = i;
        do {
            word = *stream++;
            g_stage_run_entries[i++] = word;
        } while (!(word & RUN_ENTRY_LAST));
    }
    g_stage_run_entry_peak =
        (g_stage_run_entry_peak >= i) ? g_stage_run_entry_peak : i;
    if (g_stage_run_entry_peak >= STAGE_MAX_RUN_ENTRIES) {
        sys_Trap(g_stage_run_entry_peak);
    }
    g_render_view_tilt.vx = 0;
    g_render_view_tilt.vy = 0;
    g_render_view_tilt.vz = 0;
}

/* Runs the hook that sets up what the loaded stage has of its own. */
void stage_RunInitHook(void) {
    if (g_stage_hooks[resource_GetStage()].init) {
        g_stage_hooks[resource_GetStage()].init();
    }
}

/* Nothing calls it. */
static s32 stage_GetViewYaw(void) { return g_stage_view_yaw; }

StageNode *stage_GetNodes(void) { return g_stage_nodes; }

/* The far view on or off (stage.h: both of its bits go on), and with it how
 * the renderer scales what is far away. */
void stage_SetFarView(s32 on) {
    if (on) {
        g_scene_settings.far_view.value = 3;
        g_scene_settings.far_shift = 2;
    } else {
        g_scene_settings.far_view.value = 0;
        g_scene_settings.far_shift = 1;
    }
}

/* The first-person view on (1) or off (0). Leaving it also leaves the far
 * view. */
void stage_SetFirstPerson(s32 on) {
    if (on < 0 || on >= 2) {
        return;
    }
    if (on == g_stage_first_person) {
        return;
    }
    g_stage_first_person = on;
    if (!on) {
        stage_SetFarView(0);
    }
    g_scene_settings.first_person = g_stage_first_person;
}

s32 stage_IsFirstPerson(void) { return g_stage_first_person; }

/* Clears every cell's flags and gives the cells around the camera their
 * detail from the pattern (g_stage_detail_pattern: 16 rows of 16 cells, two
 * bits a cell, the camera in the middle). */
void stage_ResetCellFlags(void) {
    s32 i, j;
    s32 view_cell_x, view_cell_z;
    s32 row_pattern;
    s32 row_offset;
    s32 col_offset;
    s32 *flags_cursor = (s32 *)g_stage_cell_flags;

    for (i = 0; i < STAGE_CELLS * STAGE_CELLS / 4; i++) {
        *flags_cursor++ = 0;
    }

    stage_IsFirstPerson(); /* the answer is not used */

    view_cell_x = (g_node_camera_root_ptr->local.t[0] + STAGE_HALF_SIZE) /
                  STAGE_CELL_SIZE;
    view_cell_z = (g_node_camera_root_ptr->local.t[2] + STAGE_HALF_SIZE) /
                  STAGE_CELL_SIZE;

    for (i = 0; i < STAGE_DETAIL_WINDOW; i++) {
        row_offset = i - STAGE_DETAIL_WINDOW / 2;
        {
            u32 cell_z = view_cell_z + row_offset;
            if (cell_z < STAGE_CELLS) {
                row_pattern = g_stage_detail_pattern[i];
                for (j = 0; j < STAGE_DETAIL_WINDOW; j++) {
                    col_offset = j - STAGE_DETAIL_WINDOW / 2;
                    {
                        u32 cell_x = view_cell_x + col_offset;
                        if (cell_x < STAGE_CELLS) {
                            /* two bits a cell, the first cell highest */
                            s32 bits = (row_pattern >>
                                        ((STAGE_DETAIL_WINDOW - 1 - j) * 2)) &
                                       STAGE_CELL_DETAIL;
                            g_stage_cell_flags[cell_z * STAGE_CELLS + cell_x] |=
                                bits;
                        }
                    }
                }
            }
        }
    }
}

/* `cell_pos` is a cell's column and row from the middle of the grid, in
 * its first and third halfwords. */
static void stage_OpenCell(s16 *cell_pos) {
    s32 cell_x = cell_pos[0] + STAGE_CELLS / 2;
    s32 cell_z = cell_pos[2] + STAGE_CELLS / 2;
    if (cell_x >= 0 && cell_x < STAGE_CELLS && cell_z >= 0 &&
        cell_z < STAGE_CELLS) {
        g_stage_cell_flags[cell_z * STAGE_CELLS + cell_x] |= STAGE_CELL_OPEN;
    }
}

void stage_OpenCellFully(s16 *cell_pos) {
    s32 cell_x = cell_pos[0] + STAGE_CELLS / 2;
    s32 cell_z = cell_pos[2] + STAGE_CELLS / 2;
    if (cell_x >= 0 && cell_x < STAGE_CELLS && cell_z >= 0 &&
        cell_z < STAGE_CELLS) {
        g_stage_cell_flags[cell_z * STAGE_CELLS + cell_x] |=
            STAGE_CELL_OPEN_ALL;
    }
}

/* Gives the cell at (x, z) and the three nearest to that point detail 2,
 * and no other flag. Nothing calls it. */
static void stage_SetDetailAround(s32 x, s32 z) {
    s32 cell_x, cell_z, x_side;
    s32 z_side;
    x += STAGE_HALF_SIZE;
    z += STAGE_HALF_SIZE;
    cell_x = x / STAGE_CELL_SIZE;
    x_side = x - cell_x * STAGE_CELL_SIZE;
    z_side = z / STAGE_CELL_SIZE;
    cell_z = z_side;
    z_side = z - cell_z * STAGE_CELL_SIZE;
    if (x_side < STAGE_CELL_SIZE / 2) {
        x_side = -1;
    } else {
        x_side = 1;
    }
    if (z_side < STAGE_CELL_SIZE / 2) {
        z_side = -1;
    } else {
        z_side = 1;
    }
    stage_SetCellDetail(cell_x, cell_z, 2);
    stage_SetCellDetail(cell_x + x_side, cell_z, 2);
    stage_SetCellDetail(cell_x, cell_z + z_side, 2);
    stage_SetCellDetail(cell_x + x_side, cell_z + z_side, 2);
}

static void stage_SetCellDetail(s32 cell_x, s32 cell_z, s32 detail) {
    g_stage_cell_flags[cell_z * STAGE_CELLS + cell_x] =
        detail & STAGE_CELL_DETAIL;
}

/* Nothing calls it. */
static u32 stage_GetCellFlags(s32 cell_x, s32 cell_z) {
    return g_stage_cell_flags[cell_z * STAGE_CELLS + cell_x];
}

/* Small data. */
static s32 g_stage_fogged_model = -1;
static s32 g_stage_first_person = 0;
/* The most list entries a stage has had; STAGE_MAX_RUN_ENTRIES or more stop
 * the game. */
static s32 g_stage_run_entry_peak = 0;
