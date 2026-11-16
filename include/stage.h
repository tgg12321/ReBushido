#ifndef STAGE_H
#define STAGE_H

/* The stage: a grid of 32 x 32 cells, 2000 units square, with the origin of
 * the world in the middle. stage.c works out which cells the camera sees
 * and queues what stands on them, and makes the distance fog;
 * stage_hooks.c has what single stages have of their own; stage_props.c the
 * props that fall when cut. */

#include "common.h"
#include <psxsdk/libgpu.h>
#include "node.h"

/* The stages are numbered from 0. */
#define STAGE_COUNT 38

/* The stages. The first 32 are the ones a fight can be on, and each has the
 * name the game shows for it: the story's stage card of stage n is the disc's
 * STGnn.BIN (U_PIC, and TIM2D for the last two), whose first picture is the
 * name written out. The names here are those pictures', read off them. The
 * rest are the modes' own stages and have no card: they are named here for what
 * they are. */
enum StageIndex {
    STAGE_MAIN_TATARA_SHRINE = 0,
    STAGE_MEIKYOKAN_DOJO = 1,
    STAGE_SHAINTO_HQ = 2,
    STAGE_TATARA_SHRINE = 3,
    /* the one card in Japanese only: "... basutei-mae", in front of a bus
     * stop (the stop's name is three characters whose reading is not
     * settled here). A road by night with a white fence along it and two
     * signs on posts */
    STAGE_BUS_STOP = 4,
    STAGE_LOST_BRIDGE = 5,
    STAGE_TATARA_SHRINE_PATH = 6,
    STAGE_BAMBOO_THICKET = 7,
    STAGE_OUTSIDE_MEIKYOKAN = 8, /* "Outside the Meikyokan" */
    STAGE_FUTURE_DUMP_SITE = 9,
    STAGE_SNAKE_CLIFF = 10,
    STAGE_KIMIGAURA_COAST = 11,
    STAGE_ORANGE_TREE_FIELDS = 12,
    STAGE_CASTLE_ROOFTOP = 13,
    /* "Under the Lost Bridge": a stream runs between the two start places */
    STAGE_UNDER_LOST_BRIDGE = 14,
    STAGE_TERRACED_FIELDS = 15,
    STAGE_UNDER_RAILWAY = 16,
    STAGE_WATER_TOWER_DAM = 17,
    STAGE_LUMBERYARD = 18,
    STAGE_LIGHTHOUSE = 19,
    STAGE_KABUKI_STAGE = 20,
    STAGE_ABANDONED_SHIP = 21,
    STAGE_FERRY_DOCK = 22,
    STAGE_SHAINTO_GATE = 23,
    STAGE_PURPLE_CAVERN = 24,
    STAGE_ABANDONED_STATION = 25,
    STAGE_RAILWAY = 26,
    STAGE_ABANDONED_TUNNEL = 27,
    STAGE_ABANDONED_SHIPYARD = 28,
    STAGE_BUILDING_ROOFTOP = 29,
    STAGE_BUILDING_PARKING_LOT = 30,
    STAGE_SHAINTO_CEMETERY = 31,
    /* The modes' own stages. Stage 37 has no name: no card, and nothing in
     * the code says what it is. */
    /* team battle: a court marked out in white lines. The first when the
     * first side is Narukagami (a dark hall), the second when it is Shainto
     * (a sand yard under a red wall with a crest) */
    STAGE_TEAM_1 = 32,
    STAGE_TEAM_2 = 33,
    STAGE_CHANBARA = 34,   /* a wooden hall */
    STAGE_PRACTICE_1 = 35, /* practice with the ninja: the game's logo is
                            * painted on the boards */
    STAGE_PRACTICE_2 = 36, /* a practice round: bare boards */
};

#define STAGE_CELLS 32       /* along each side */
#define STAGE_CELL_SIZE 2000 /* world units */
/* From a world coordinate to the corner the cells are counted from. */
#define STAGE_HALF_SIZE (STAGE_CELLS / 2 * STAGE_CELL_SIZE)

/* ---- what stands on the cells ---- */

/* A tile: the piece of ground of one cell. It is queued for drawing as it
 * is, and the renderer knows the layout (its kind, 0xC, is in the low byte
 * of the first halfword, where a node has it). */
typedef struct {
    s16 kind;       /* +0x00 STAGE_TILE_KIND */
    s16 mesh_block; /* +0x02 */
    u16 mesh;       /* +0x04 */
    u8 detail;      /* +0x06 the cell's STAGE_CELL_DETAIL this frame */
    u8 draw_flags;  /* +0x07 STAGE_TILE_... */
    s32 x;          /* +0x08 the cell's corner in the world */
    s32 z;          /* +0x0C */
} StageTile;        /* 0x10 bytes */

#define STAGE_TILE_KIND 12

/* StageTile.draw_flags. A reading of the code, not seen in the game: a
 * tile that can hide a fighter from the camera is drawn opened up while it
 * is in the way. */
#define STAGE_TILE_OPEN 1     /* this frame: draw it opened */
#define STAGE_TILE_CAN_OPEN 8 /* from the stage file */

/* A piece of the stage that is not a tile: a node of its own, and whether
 * this frame has already queued it for drawing. A piece can be reached from
 * several cells. */
typedef struct {
    SceneNode node;   /* +0x00 */
    u8 is_collected;  /* +0x58 */
    u8 unused59[0xF]; /* +0x59 never read: fills the record out */
} StageNode;          /* 0x68 bytes */

/* What is known about each cell this frame (g_stage_cell_flags). */
#define STAGE_CELL_DETAIL 3 /* two bits: how near the camera it is */
/* between the camera and a fighter: its tiles that can be are opened */
#define STAGE_CELL_OPEN 4
/* the camera has asked for it: all its tiles are opened */
#define STAGE_CELL_OPEN_ALL 8

/* Reads the cells of a stage file: the tiles, the pieces, and for each cell
 * the list of what stands on it. `block_id` is the pointer table their meshes
 * are in. */
extern void stage_ReadCells(u16 *stream, s32 block_id);
/* The pieces, in the order of the file. */
extern StageNode *stage_GetNodes(void);
/* The stage's tiles and how many there are, for the remastered picture's
 * one mesh of the ground (port_remaster.h, src/port/gl/gl_stage.c);
 * `count` may be NULL. Only the port has it. */
extern StageTile *stage_GetTiles(s32 *count);

/* Queues what stands on the cells the camera sees, runs the stage's own
 * hook and draws the stage's flames. */
extern void stage_Draw(s32 eye_distance);
/* Every cell's flags for a new frame: only its detail, by how far from the
 * camera it is. */
extern void stage_ResetCellFlags(void);
/* Marks the cells on the line from the camera to (end_x, end_z), or between two
 * points, as STAGE_CELL_OPEN. */
extern void stage_OpenCellsTo(s32 end_x, s32 end_z);
/* Marks a cell as STAGE_CELL_OPEN_ALL. `cell_pos` is its column and row from
 * the middle of the grid, in the first and third of four halfwords. */
extern void stage_OpenCellFully(s16 *cell_pos);

/* ---- the view ---- */

/* The far view, for when the camera has pulled a long way back: every cell
 * in front of the camera is drawn, not only the near ones, and the fog
 * stays as it was when the view began. Bit 0 is all that is read. */
typedef union {
    s16 value;

    struct {
        s16 on : 1;
        s16 bit1 : 1;
        s16 : 14;
    } bits;
} StageFarView;

extern void stage_SetFarView(s32 on);

/* The first-person view: other cells are seen, things are drawn nearer to
 * the camera, and the fog is nearer. */
extern void stage_SetFirstPerson(s32 on);
extern s32 stage_IsFirstPerson(void);

/* ---- fog ----
 * The far end of a stage fades into a colour. The stage's colour tables are
 * one row of video memory; when the stage is loaded, 30 more rows are made
 * from it, each a step nearer the fog colour. While a frame is drawn, far
 * things first, the row the textures use is overwritten again and again
 * with a less fogged one: the copies are primitives put into the ordering
 * table at evenly spaced depths. One fighter's model can be given the same
 * treatment, with a block of colour tables of its own. */

/* How far the scene reaches, in world units: where the fog is complete. The
 * first-person view's scene ends nearer. */
#define STAGE_FAR 26000
#define STAGE_FAR_FIRST_PERSON 22000

#define STAGE_FOG_LEVELS 31       /* rows, the plain one among them */
#define STAGE_MODEL_FOG_LEVELS 19 /* blocks */
/* A model's colour tables in video memory, when it is fogged: a block beside
 * the stage's, with the plain copies under it and the fogged ones then in
 * further columns (model.h has where a model's are otherwise). */
#define STAGE_FOG_MODEL_X 320
#define STAGE_FOG_MODEL_Y 480
#define STAGE_FOG_MODEL_WIDTH 64
#define STAGE_FOG_MODEL_HEIGHT 8

/* Where the nearest copy went into the ordering table this frame, for the
 * renderer; STAGE_FOG_NONE if there is no fog. */
#define STAGE_FOG_NONE 0xFFFF
extern u32 g_stage_fog_ot_index;
/* The far view's fog: how deep it is and where it starts, kept from the
 * frame the view began. */
extern s32 g_stage_far_fog_span;
extern s32 g_stage_far_fog_offset;
extern s16 g_stage_fog_last_far_view;
/* By stage: how deep the fog can be; and whether the stage has fog, then
 * whether its farthest copy is drawn before everything else. */
extern s32 g_stage_fog_spans[STAGE_COUNT];
extern s16 g_stage_fog_settings[STAGE_COUNT + 1][2];

/* Sets the copies up. Once, when the engine starts. */
extern void stage_InitFog(void);
/* Makes the fogged rows of the stage that was just loaded, and those of a
 * model's block. */
extern void stage_BuildFog(void);
extern void stage_BuildModelFog(void);
/* Queues this frame's copies for stage `stage`. */
extern void stage_QueueFog(s32 stage, s32 eye_distance);
extern u32 stage_GetFogOtIndex(void);
/* Has the model in slot `slot_index` fogged: its colour tables are moved into
 * the fog's block and its meshes pointed at them. One model at a time;
 * stage_UnfogModel puts it back. -1: none. */
extern void stage_FogModel(s32 slot_index);
extern void stage_UnfogModel(void);
extern s32 stage_GetFoggedModel(void);

/* ---- the tables of what is drawn (stage_data.c; see stage_Draw) ---- */

#define STAGE_WINDOW 31    /* the cells around the camera, each way */
#define STAGE_VIEW_YAWS 64 /* the ways the camera can look */
/* The cells the camera can see for each way it can look, in the usual view
 * and in the first-person view, and the cells near the camera. */
extern s32 g_stage_view_masks[STAGE_VIEW_YAWS + 1][STAGE_CELLS];
extern s32 g_stage_first_person_masks[STAGE_VIEW_YAWS + 1][STAGE_CELLS];
extern s32 g_stage_near_mask[STAGE_WINDOW];
/* The pattern of detail around the camera. */
#define STAGE_DETAIL_WINDOW 16
extern s32 g_stage_detail_pattern[STAGE_CELLS];

/* ---- loading ---- */

/* Loads stage `stage` through `staging_addr`, sets its lighting and makes
 * its fog. */
extern void stage_Load(s32 stage, s32 staging_addr);

/* ---- a stage's own hooks (stage_hooks.c) ----
 * What a stage has of its own (falling props, the surf, a scrolling
 * texture, a backdrop): set up when the stage is loaded, stepped once a
 * frame when the stage is drawn. */
typedef struct {
    void (*init)(void);
    void (*update)(void);
} StageHooks;

extern StageHooks g_stage_hooks[STAGE_COUNT];
extern void stage_RunInitHook(void);

extern void stage_InitBusStop(void);
extern void stage_UpdateBusStop(void);
extern void stage_InitLostBridge(void);
extern void stage_UpdateLostBridge(void);
extern void stage_InitBamboo(void);
extern void stage_UpdateBamboo(void);
extern void stage_InitBackdrop(void);
extern void stage_DrawBackdrop(void);
extern void stage_InitUnderLostBridge(void);
extern void stage_UpdateUnderLostBridge(void);
extern void stage_InitLumber(void);
extern void stage_UpdateLumber(void);
/* The Shainto gate's, and the ones the stage outside the Meikyokan, the
 * purple cavern and team battle's second court share: they do nothing. */
extern void stage_InitShaintoGate(void);
extern void stage_UpdateShaintoGate(void);
extern void stage_InitNothing(void);
extern void stage_UpdateNothing(void);

/* The camera's pitch and yaw, worked out from its matrix. */
extern s16 *stage_CalcViewAngles(void);

/* ---- props (stage_props.c) ---- */

/* A prop: something standing on the stage that a blade can cut through
 * (the bamboo of the bamboo thicket, the posts of the lumberyard). While it
 * stands it is one of the stage's pieces. When it is cut that piece turns
 * into what remains, and this node is the part that falls: it plays a short
 * recorded motion, lies there, and fades away. */
typedef struct {
    SceneNode node; /* +0x00 */
    s32 frame;      /* +0x58 of the motion, or a PROP_... state */
    s32 yaw;        /* +0x5C the way it falls */
    s16 fade;       /* +0x60 ONE once fallen, down to 0 */
    s8 pad62[6];    /* +0x62 */
} StageProp;        /* 0x68 bytes */

/* StageProp.frame when it is not counting frames */
#define PROP_STANDING (-1)
#define PROP_GONE (-2)

/* The props of the stage that is loaded. */
typedef struct {
    s16 mesh_block;
    s16 frame_count; /* length of the falling motion */
    s16 stage;
    s16 count;
    s32 frames;  /* the motion: per frame a position and three angles (s16) */
    s32 props;   /* StageProp[count] */
    s32 remains; /* the stage pieces they stand as: StageNode[count] */
    s32 unk14;   /* no code reads or writes it */
} StageProps;

/* Called while the stage file is loaded: takes the falling motion from the
 * file and room for the props at `buffer`. Returns the end of that room. */
extern s32 stage_SetUpProps(s16 *motion, s16 mesh_block, s32 buffer);
extern void stage_InitProps(void);
/* Moves the props that are falling and queues the ones that have fallen. */
extern void stage_UpdateProps(void);
/* Prop `index` has been cut and falls towards `yaw`. */
extern void stage_CutProp(s32 index, s32 yaw);
/* The stage's block moved by `delta` bytes. */
extern void stage_RebaseProps(s32 delta);

#endif /* STAGE_H */
