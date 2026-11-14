/* Cutscenes (cutscene.h). Starting one: the file is loaded, the scene is
 * placed on the stage and the stage it wants is loaded. Playing it: each
 * frame the camera and the two fighters are placed from the file's streams.
 * The disc calls them MAR (MARcc_mm.DAT, MAR_XAn.XA): "marionation", as the
 * next game's symbols spell it, which is also the engine's name. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static s16 g_cutscene_set_first_file[26];
static s32 g_cutscene_no_fighter_tag[2];

static Cutscene g_cutscene;

/* The field of view cutscenes are shot with, in degrees. */
#define CUTSCENE_FOV 45

/* How far the stage is drawn in a cutscene (scene_DrawStage's distance of
 * the eye). */
#define CUTSCENE_EYE_DISTANCE 10000

/* A frame of the camera's track: where it is, from the scene's place, and
 * its three angles. */
enum {
    CUTSCENE_CAMERA_X,
    CUTSCENE_CAMERA_Y,
    CUTSCENE_CAMERA_Z,
    CUTSCENE_CAMERA_ANGLE_X,
    CUTSCENE_CAMERA_ANGLE_Y,
    CUTSCENE_CAMERA_ANGLE_Z,
};

/* Starts the cutscene in motion file `number` (counted from NDATA_MOTIONS)
 * at the first point of the stage's start pair `start_pair`, turned as its
 * flags say, and loads the stage it names. The fighters carry the given
 * weapon models where the flags let them. Returns the file's size if it was
 * read to `dest_addr`, else 0. */
static s32 cutscene_Start(
    s32 number, s32 start_pair, s32 weapon_1, s32 weapon_2, s32 second_weapon_1,
    s32 second_weapon_2, s32 dest_addr) {
    Cutscene *scene = &g_cutscene;
    s32 file_id = number + NDATA_MOTIONS;
    s32 file_size;
    s16 *start_position;
    Model *model;
    s32 stage;

    if (dest_addr != 0) {
        file_size = ndata_ReadChecked(file_id, dest_addr);
        scene->block_base = dest_addr;
    } else {
        /* make room in the arena at the tiny model's expense */
        if (ndata_GetArenaRoomAfter(file_id) < 0) {
            resource_FreeTinyModel();
            printf("Destruction tiny model.\n");
        }
        scene->block_base = (s32)cutscene_LoadBlock(file_id);
        file_size = 0;
    }
    scene->flags = *(s32 *)(((CutsceneFile *)scene->block_base)->flags_offset +
                            scene->block_base);
    scene->frame_count =
        *(u16 *)(((CutsceneFile *)scene->block_base)->frame_count_offset +
                 scene->block_base);
    scene->current_frame = 0;
    /* a stage has four pairs of start positions; the scene is played at
     * the first of the pair */
    start_position = fight_GetStartPositions();
    start_position += resource_GetStage() * FIGHT_START_STAGE_SIZE +
                      start_pair * FIGHT_START_PAIR_SIZE;
    scene->origin_x = *start_position++;
    scene->origin_y = *start_position++;
    scene->origin_z = *start_position++;
    scene->rotation_x = 0;
    scene->rotation_z = 0;
    scene->weapon_entry_ids[0] = weapon_1;
    scene->weapon_entry_ids[1] = weapon_2;
    scene->second_weapon_entry_ids[0] = second_weapon_1;
    scene->second_weapon_entry_ids[1] = second_weapon_2;
    /* four degrees a step: yaw * 4 * ANGLE_TURN / 360 */
    scene->rotation_y = (CUTSCENE_YAW(scene->flags) << 14) / 360;
    if (!(scene->flags & CUTSCENE_WEAPON_1)) {
        scene->weapon_entry_ids[0] = -1;
    }
    if (!(scene->flags & CUTSCENE_WEAPON_2)) {
        scene->weapon_entry_ids[1] = -1;
    }
    model = model_Get(0);
    if (model) {
        cloth_RequestRebuild(model);
    }
    model = model_Get(1);
    if (model) {
        cloth_RequestRebuild(model);
    }
    scene->start_pair_index = start_pair;
    stage_SetFirstPerson(0);
    SetGeomScreen(math_FovToScreenDist(CUTSCENE_FOV));
    if (scene->flags & CUTSCENE_STAGE) {
        stage = (scene->flags & CUTSCENE_STAGE) - 1;
        if (dest_addr != 0) {
            /* the stage file is staged after the cutscene's */
            dest_addr += file_size;
            stage_Load(stage, dest_addr);
        } else {
            disp_Reset();
            stage_Load(stage, (s32)g_disp_prim_buffers);
        }
    }
    if (scene->flags & CUTSCENE_ALL_AREAS) {
        resource_SelectStageArea(-1);
    }
    return file_size;
}

/* Starts cutscene `number` of set `set`: see cutscene_Start. */
void cutscene_StartFromSet(
    s32 set, s32 number, s32 start_pair, s32 weapon_1, s32 weapon_2,
    s32 second_weapon_1, s32 second_weapon_2, s32 dest_addr) {
    cutscene_Start(
        g_cutscene_set_first_file[set] + number - NDATA_MOTIONS, start_pair,
        weapon_1, weapon_2, second_weapon_1, second_weapon_2, dest_addr);
}

/* The cutscene is over: every area of the stage is shown again and the
 * file's memory is given back. */
void cutscene_End(void) {
    DrawSync(0);
    resource_SelectStageArea(-1);
    cutscene_FreeBlock();
}

/* Turns the x and z of `v` by the scene's yaw. */
#define TURN_BY_SCENE(v)                                                       \
    {                                                                          \
        s32 turn_cos = g_math_sin_table[(scene->rotation_y + ANGLE_TURN / 4) & \
                                        ANGLE_MASK];                           \
        s32 turn_sin = g_math_sin_table[scene->rotation_y & ANGLE_MASK];       \
        rotated_z = ((v).vz * turn_cos - (v).vx * turn_sin) >> 12;             \
        (v).vx = ((v).vz * turn_sin + (v).vx * turn_cos) >> 12;                \
        (v).vz = rotated_z;                                                    \
    }

/* Plays the next frame of the cutscene and queues it for drawing: the camera
 * is placed from its track, each fighter is posed from its motion stream,
 * and both are moved to where on the stage the scene plays and turned the
 * way it faces. Returns 0 once the cutscene is over.
 *
 * The file's addresses are kept as integers: as pointers, the sums here and
 * in cutscene_Rebase and cutscene_Start come out the other way round. */
static s32 cutscene_Advance(void) {
    Cutscene *scene = &g_cutscene;
    VECTOR vec;
    /* a MotionFrame, as signed halfwords */
    s16 pose[MOTION_FRAME_VALUES];
    s16 *camera;
    s32 i;
    Model *model;
    /* FAKE: one local for the camera's turned z and each fighter's */
    s32 rotated_z;

    if (scene->current_frame < 0) {
        return 0;
    }
    if (scene->current_frame == 0) {
        /* the first frame: find the file's streams and open them */
        s32 j;

        scene->camera_stream =
            ((CutsceneFile *)scene->block_base)->camera_stream_offset +
            scene->block_base;
        scene->motion_streams[0] =
            ((CutsceneFile *)scene->block_base)->motion_stream_offsets[0] +
            scene->block_base;
        scene->motion_streams[1] =
            ((CutsceneFile *)scene->block_base)->motion_stream_offsets[1] +
            scene->block_base;
        bitstream_OpenTrack(scene->camera_stream, CUTSCENE_CAMERA_TRACK);
        for (j = 0; j < 2; j++) {
            if (*(s32 *)scene->motion_streams[j] ==
                g_cutscene_no_fighter_tag[0]) {
                scene->motion_streams[j] = 0;
            }
            if (scene->motion_streams[j] != 0) {
                motion_OpenStream(j, (u32 *)scene->motion_streams[j]);
            }
        }
        scene->face_tracks[1] = 0;
        scene->face_tracks[0] = 0;
        if (scene->flags & CUTSCENE_FACES_1) {
            scene->face_tracks[0] =
                ((CutsceneFile *)scene->block_base)->face_track_offsets[0] +
                scene->block_base;
        }
        if (scene->flags & CUTSCENE_FACES_2) {
            scene->face_tracks[1] =
                ((CutsceneFile *)scene->block_base)->face_track_offsets[1] +
                scene->block_base;
        }
        model = model_Get(0);
        if (model) {
            cloth_RequestRebuild(model);
        }
        model = model_Get(1);
        if (model) {
            cloth_RequestRebuild(model);
        }
    }

    /* the camera: the file has x and z the other way, and the camera
     * facing the other way round */
    camera = bitstream_StepTrack(CUTSCENE_CAMERA_TRACK);
    vec.vx = -camera[CUTSCENE_CAMERA_X];
    vec.vy = camera[CUTSCENE_CAMERA_Y];
    vec.vz = -camera[CUTSCENE_CAMERA_Z];
    TURN_BY_SCENE(vec);
    g_node_camera_root.node.local.t[0] = vec.vx + scene->origin_x;
    g_node_camera_root.node.local.t[1] = vec.vy + scene->origin_y;
    g_node_camera_root.node.local.t[2] = vec.vz + scene->origin_z;
    g_node_camera_root.node.rot.vx = camera[CUTSCENE_CAMERA_ANGLE_X];
    g_node_camera_root.node.rot.vy =
        camera[CUTSCENE_CAMERA_ANGLE_Y] + ANGLE_TURN / 2;
    g_node_camera_root.node.rot.vz = camera[CUTSCENE_CAMERA_ANGLE_Z];
    if (scene->rotation_y != 0) {
        MATRIX scene_turn;
        SVECTOR scene_angles;

        scene_angles.vx = 0;
        scene_angles.vz = 0;
        scene_angles.vy = scene->rotation_y;
        g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](&scene_angles, &scene_turn);
        g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](
            &g_node_camera_root.node.rot, &g_node_camera_root.node.local);
        MulMatrix2(&scene_turn, &g_node_camera_root.node.local);
        math_MatrixToAnglesYXZ(
            &g_node_camera_root.node.local, &g_node_camera_root.node.rot);
        math_Transpose(&g_node_camera_root.node.local);
        g_node_camera_root.node.world = g_node_camera_root.node.local;
    } else {
        node_SetInverseRotation(&g_node_camera_root.node);
    }
    scene->view_angles[0] = -g_node_camera_root.node.rot.vx;
    scene->view_angles[1] = -g_node_camera_root.node.rot.vy;
    scene->view_angles[2] = -g_node_camera_root.node.rot.vz;

    light_ResetPointLight();
    light_ApplyPointLightTargets();
    scene_SetSquashedView();
    stage_ResetCellFlags();
    light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_1]);
    light_CalcMatrix(&g_light_setups[LIGHT_FIGHTER_2]);
    light_CalcMatrix(&g_light_setups[LIGHT_SCENERY]);

    for (i = 0; i < 2; i++) {
        if (scene->motion_streams[i] != 0) {
            s32 root_angle;

            model = model_Get(i);
            /* decoded through the scratchpad */
            motion_DecodeFrame(i, scene->current_frame, (MotionFrame *)pose,
                               (u16 *)SCRATCHPAD);
            /* where the body is: the frame's root (its height, scaled to
             * the model; its direction and its distance) */
            vec.vy = pose[0];
            vec.vy *= model->scale;
            vec.vy >>= 12;
            root_angle = pose[1];
            vec.vx =
                (g_math_sin_table[root_angle & ANGLE_MASK] * pose[2]) >> 12;
            vec.vz =
                (g_math_sin_table[(root_angle + ANGLE_TURN / 4) & ANGLE_MASK] *
                 pose[2]) >>
                12;
            vec.vy = -vec.vy;
            vec.vz = -vec.vz;
            TURN_BY_SCENE(vec);
            vec.vy += scene->origin_y;
            vec.vx += scene->origin_x;
            vec.vz += scene->origin_z;
            model_Update(i, MODEL_POSE_FRAME, &vec.vx, &scene->rotation_x, pose,
                         scene->origin_y);
            if (scene->weapon_entry_ids[i] >= 0) {
                weapon_Queue(scene->weapon_entry_ids[i],
                             (i * 2) | WEAPON_DRAW_HELD, 0, 0);
                weapon_QueueWorn(scene->weapon_entry_ids[i], i * 2, 0);
            }
            if (scene->second_weapon_entry_ids[i] >= 0) {
                weapon_QueueWorn(
                    scene->second_weapon_entry_ids[i], (i * 2) | 1, 1);
            }
            if (scene->face_tracks[i] != 0) {
                /* a face for every frame, four bits each */
                model_SetFace(
                    i,
                    (((u32 *)scene->face_tracks[i])[scene->current_frame / 8] >>
                     ((scene->current_frame % 8) * 4)) &
                        0xF);
            }
        }
    }
    scene_DrawStage(CUTSCENE_EYE_DISTANCE);
    scene->current_frame++;
    if (scene->current_frame >= scene->frame_count) {
        scene->current_frame = -1;
    }
    return 1;
}

/* A frame of the cutscene: empties the draw queue, plays the frame and
 * draws it. Returns 0 once the cutscene is over. */
s32 cutscene_RunFrame(void) {
    s32 playing;

    g_render_queue_end = g_render_queue;
    g_scene_count = g_scene_count + 1;
    g_render_ot_base = g_disp_ot;
    g_render_ot = g_disp_ot + 4;
    playing = cutscene_Advance();
    render_FlushCutscene();
    return playing;
}

/* The cutscene's file has moved by `delta` in memory: the addresses kept
 * of it and of its streams move with it. */
void cutscene_Rebase(s32 delta) {
    s32 *block_base = &g_cutscene.block_base;

    *block_base = delta + *block_base;
    g_cutscene.camera_stream = delta + g_cutscene.camera_stream;
    if (g_cutscene.motion_streams[0]) {
        g_cutscene.motion_streams[0] = delta + g_cutscene.motion_streams[0];
    }
    if (g_cutscene.motion_streams[1]) {
        g_cutscene.motion_streams[1] = delta + g_cutscene.motion_streams[1];
    }
    if (g_cutscene.face_tracks[0]) {
        g_cutscene.face_tracks[0] = delta + g_cutscene.face_tracks[0];
    }
    if (g_cutscene.face_tracks[1]) {
        g_cutscene.face_tracks[1] = delta + g_cutscene.face_tracks[1];
    }
}

/* The camera's rotation this frame, negated. */
s16 *cutscene_GetViewAngles(void) { return g_cutscene.view_angles; }

/* The archive entry each set of cutscenes begins at (ndata.c has the
 * archive's groups): a set for each character the story can be played with
 * (-1: it cannot), then, at CUTSCENE_SET_SHARED, the set every fight
 * shares. */
/* clang-format off */
static s16 g_cutscene_set_first_file[26] = {
    305, /* CHARACTER_KANNUKI */
    337, /* CHARACTER_TATSUMI */
    369, /* CHARACTER_MIKADO */
    402, /* CHARACTER_UTSUSEMI */
    429, /* CHARACTER_RED_SHADOW */
    -1,  /* CHARACTER_TSUBAME */
    459, /* CHARACTER_SAZANKA */
    486, /* CHARACTER_SUMINAGASHI */
    516, /* CHARACTER_NIGHTSTALKER */
    546, /* CHARACTER_MATSUMUSHI */
    -1,  /* CHARACTER_VANISHER */
    -1,  /* CHARACTER_SIDEARM_1 */
    577, /* CHARACTER_GENGORO */
    612, /* CHARACTER_KAUN */
    647, /* CHARACTER_JO */
    682, /* CHARACTER_HONGOU */
    -1,  /* CHARACTER_KATZE */
    717, /* CHARACTER_HIGHWAYMAN */
    752, /* CHARACTER_CHIHIRO */
    783, /* CHARACTER_UTAMARU */
    814, /* CHARACTER_TONY_UMEDA */
    849, /* CHARACTER_ISOHACHI */
    884, /* CUTSCENE_SET_SHARED */
    -1,  /* not a set */
    -1,  /* not a set */
    0,   /* not a set */
};
/* clang-format on */

/* "NULL", where a fighter's motion stream would begin. */
static s32 g_cutscene_no_fighter_tag[2] = {0x4C4C554E, 0};
