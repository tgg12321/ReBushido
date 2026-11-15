/* Effects: blood, sparks, flashes, dust, splashes, footprints and flames,
 * all drawn as flat textured quads. See eff.h for how the system works.
 *
 * The file runs: the system (starting an effect, running the living ones
 * each frame, the light); the requests the fight makes; the stage's flames;
 * then each effect's handlers: the muzzle flash, splashes, footprints, dust,
 * the single-quad flashes, and the particles. */
#include "common.h"
#include <psxsdk/inline_c.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static s32 g_eff_light_frames;
static void eff_SetViewMatrix(
    s16 *view_angles, s32 *view_origin, MATRIX *matrix_out);
static u32 g_eff_flame_frame;
static u8 eff_PlaceFootprint(s32 fighter);
static s32 eff_DrawFootprints(s32 fighter);
static void eff_BuildFootprintMatrices(
    s32 *count, MATRIX *matrices, s32 fighter);
static u8 eff_DrawFlash(s32 flash);
static u8 eff_SpawnParticles(s32 kind, s32 set, s32 half);
static u8 eff_SetUpParticles(s32 kind, s32 set);
static void eff_UpdateParticles(s32 kind, s32 set);
static u8 eff_LinkParticleQuads(s32 kind, s32 set);

/* The effects' own state. */
static Particle g_eff_particles[PARTICLE_SETS][PARTICLE_COUNT];
static SVECTOR g_eff_particle_directions[PARTICLE_SETS];
/* Waves a kind lets go, one entry a kind (PARTICLES_END of them). */
static s16 g_eff_particle_wave_counts[8];
static s16 g_eff_flash_timers[FLASH_COUNT];
static s16 g_eff_dust_ages[DUST_COUNT];
static s16 g_eff_splash_ages[SPLASH_COUNT];
static s16 g_eff_muzzle_flash_ages[MUZZLE_FLASH_COUNT];
/* Where each wave of a set was let go. */
static Vec3 g_eff_particle_wave_positions[PARTICLE_SETS][PARTICLE_WAVES];
static Vec3 g_eff_flash_positions[FLASH_COUNT];
static Vec3 g_eff_dust_positions[DUST_COUNT];
static Vec3 g_eff_splash_positions[SPLASH_COUNT];
static Vec3 g_eff_footprint_positions[2][FOOTPRINT_COUNT];
static Vec3 g_eff_muzzle_flash_positions[MUZZLE_FLASH_COUNT];
static SVECTOR g_eff_footprint_angles[2][FOOTPRINT_COUNT];
static SVECTOR g_eff_muzzle_flash_angles[MUZZLE_FLASH_COUNT];
/* How far along each effect is: an index from its first handler. */
static s32 g_eff_stage[EFF_COUNT];
static VECTOR g_eff_light_position;
static u8 g_eff_alive[EFF_COUNT];
static EffRequest g_eff_start_request;
static Flame g_eff_flames[6]; /* five, and the record that ends the list */

/* The effects' textures, by mode (TIM2D/GAME*.BNK). They differ only below
 * the effects' own part. */
enum {
    EFF_FILE_OTHER = CDROM_DATA_GAME_BNK,
    EFF_FILE_CHANBARA = CDROM_DATA_GAMECHAN_BNK,
    EFF_FILE_PRACTICE = CDROM_DATA_GAMETRA_BNK,
    EFF_FILE_TEAM = CDROM_DATA_GAMEDAN_BNK,
    EFF_FILE_STORY = CDROM_DATA_GAMESTO_BNK,
};

/* The two texture pages the effects are in, at x 896 and 960 of video
 * memory. A quad is drawn half and half over what is behind it, or with
 * EFF_PAGE_ADD added to it. */
#define EFF_PAGE_LEFT 14
#define EFF_PAGE_RIGHT 15
#define EFF_PAGE_ADD 0x20

/* The effects' strip of video memory, loaded in two parts: all of its width
 * down to the mode's part, and under that the mode's, which is narrower. */
#define EFF_VRAM_X 896
#define EFF_VRAM_WIDTH 128
#define EFF_VRAM_HEIGHT 476
#define EFF_VRAM_MODE_WIDTH 112
#define EFF_VRAM_MODE_HEIGHT 36

/* A POLY_FT4's code and its length in words after the tag, where the
 * function writes them itself. */
#define FT4_CODE 0x2C
#define FT4_SEMI_TRANS 2
#define FT4_RAW_TEX 1 /* the texture's colours as they are */
#define FT4_LEN 9
/* The two parts of a primitive's tag: its length, and the address of the
 * next. */
#define PRIM_TAG_LEN 0xFF000000
#define PRIM_TAG_ADDR 0xFFFFFF

/* A quad's colour as the word a primitive has it in, under its code. */
#define EFF_RGB(r, g, b) ((r) | (g) << 8 | (b) << 16)
/* The colour that leaves a texture as it is. */
#define EFF_RGB_PLAIN EFF_RGB(0x80, 0x80, 0x80)

/* ---- what each effect looks like ----
 * Sizes are a quad's in the world, across and high, before the view scales
 * it; a texture's are in texels; times are frames. */

/* Thrown dirt and water leave up and forward at this speed each way, as
 * particles count speed (a thirtieth of it a frame). */
#define THROWN_SPEED 2560

/* The stage's flames. g_eff_flame_texs has the fire's frames, the torch's,
 * then a grey palette for each. A flame flickers up to a fifth taller. */
#define FLAME_FIRE_WIDTH 450
#define FLAME_FIRE_HEIGHT 225
#define FLAME_FIRE_SMALLER_WIDTH 337
#define FLAME_FIRE_SMALLER_HEIGHT 168
#define FLAME_TORCH_WIDTH 80
#define FLAME_TORCH_HEIGHT 60
#define FLAME_TORCH_TALL_WIDTH 100
#define FLAME_TORCH_TALL_HEIGHT 120
#define FLAME_FIRE_FRAMES 6
#define FLAME_TORCH_FRAMES 4
#define FLAME_TORCH_TEX FLAME_FIRE_FRAMES
#define FLAME_FIRE_GRAY_TEX 10
#define FLAME_TORCH_GRAY_TEX 11
#define FLAME_FIRE_TEX_SIZE 32
#define FLAME_TORCH_TEX_WIDTH 16
#define FLAME_TORCH_TEX_HEIGHT 20

/* A muzzle flash: three quads, fire for its first frames and smoke after,
 * gone after the last. The quad across the barrel's end has a square
 * texture and the two along it one twice as wide. */
#define MUZZLE_FLASH_QUADS 3
#define MUZZLE_FLASH_FIRE_FRAMES 3
#define MUZZLE_FLASH_FRAMES 6
#define MUZZLE_FLASH_TEX_SIZE 32
#define MUZZLE_FLASH_SIDE_TEX_WIDTH 64

/* A splash: a crown from the first frame; drops from the third, which are
 * drawn black (so not seen) until they begin to fade, and have another
 * texture toward the end. A quad is at least SPLASH_LEAST_SIZE on screen
 * each way. */
#define SPLASH_WIDTH 192
#define SPLASH_HEIGHT 96
#define SPLASH_TEX_SIZE 64
#define SPLASH_FRAMES 16
#define SPLASH_DROPS_FRAME 3
#define SPLASH_DROPS_FADE_FRAME 9
#define SPLASH_DROPS_LATE_FRAME 11
#define SPLASH_CROWN_NARROW_FRAMES 5 /* in which the crown narrows a little */
#define SPLASH_CROWN_RISE_FRAMES 10  /* in which it rises */
#define SPLASH_LEAST_SIZE 8

/* A footprint is drawn this much nearer than it is, so that the ground
 * does not hide it. */
#define FOOTPRINT_TEX_WIDTH 8
#define FOOTPRINT_TEX_HEIGHT 16
#define FOOTPRINT_DEPTH_BIAS 50

/* Dust: puffs in groups of four slots, scattered this far each way from
 * where the dust was raised; a puff's seven frames; how much of its height
 * is above its place and how much below. */
#define DUST_WIDTH 64
#define DUST_HEIGHT 32
#define DUST_TEX_WIDTH 64
#define DUST_TEX_HEIGHT 32
#define DUST_GROUP 4
#define DUST_SCATTER 0x7F
#define DUST_FRAMES 7
#define DUST_LEAST_SIZE 8
#define DUST_ABOVE(height) ((height) * 28 / 32)
#define DUST_BELOW(height) ((height) / 8)

/* The flashes' timers: each goes up by its step a frame, and the flash is
 * over when it is past its end.
 *
 * The lock flashes' is an angle: a pulse is a turn, an eighth of one a
 * frame. The rated one goes a step, two or three a frame, and after its
 * pulse waits a second turn, unseen, before it is over. */
#define LOCK_FLASH_STEP 0x1FF
#define LOCK_FLASH_END ANGLE_TURN
#define LOCK_FLASH_RATED_END (2 * ANGLE_TURN)
/* A spark's is the angle it has spun through, a tenth of its life a
 * frame. */
#define SPARK_LIFE 4551
#define SPARK_STEP 454
/* The hit flash's is its colour's green; it has another texture late on. */
#define HIT_FLASH_START 64
#define HIT_FLASH_STEP 25
#define HIT_FLASH_END 200
#define HIT_FLASH_LATE 150
/* A streak's fades its colour; the streak has another texture late on, and
 * a clash streak's glow one later still. */
#define STREAK_STEP 50
#define STREAK_END 255
#define STREAK_LATE 150
#define STREAK_GLOW_LATE 200
/* The blunt hit's counts frames: a burst, a second burst, then a star. */
#define BLUNT_HIT_FRAMES 10
#define BLUNT_HIT_MID_FRAME 5
#define BLUNT_HIT_STAR_FRAME 8

/* The flashes' sizes. A streak's texture is turned a quarter turn onto its
 * quad, which is tall and narrow; in the first-person view it is a square
 * burst. A spark far from the view (by four times its depth) has a texture
 * of its own. */
#define FLASH_SIZE 32
#define FLASH_TEX_SIZE 64
#define HIT_FLASH_SIZE 192
#define STREAK_WIDTH 32
#define STREAK_HEIGHT 128
#define STREAK_TEX_WIDTH 64
#define STREAK_TEX_HEIGHT 16
#define STREAK_CLOSE_SIZE 128
#define CLASH_GLOW_SIZE 256
#define SPARK_FAR_DEPTH 4000

/* Particles. One is over this far from where it was let go, or up to as
 * far again (it is a little random). It falls at 9800 a second, each
 * second: the world's unit would be a millimetre (a guess from that
 * number). Its size is worked out in 512ths with the projection distance
 * times PARTICLE_SCALE. */
#define THROW_RANDOMS 6 /* the random numbers a particle's throw takes */
#define PARTICLE_REACH 3000
#define PARTICLE_GRAVITY 9800
#define PARTICLE_SCALE 200
#define BLOOD_WIDTH 64
#define BLOOD_HEIGHT 32
#define DEBRIS_WIDTH 32
#define DEBRIS_HEIGHT 16
#define DIRT_PUFF_WIDTH 192
#define DIRT_PUFF_HEIGHT 48
#define WATER_DROP_WIDTH 192
#define WATER_DROP_HEIGHT 96
#define DROP_TEX_SIZE 8
#define WATER_TEX_SIZE 16
#define DIRT_PUFF_TEX_SIZE 32
#define DIRT_PUFF_FRAMES 4

/* A texture record's palette as a primitive's clut word. */
#define TEX_CLUT(tex) ((((tex)->clut_x >> 4) & 0x3F) + ((tex)->clut_y << 6))

/* Links the quad at g_eff_prim_tag in at the head of the ordering table
 * entry g_eff_ot_entry (Sony's addPrim, on the two scratchpad pointers). */
#define EFF_ADD_PRIM()                                                         \
    {                                                                          \
        *g_eff_prim_tag = (*g_eff_prim_tag & PRIM_TAG_LEN) |                   \
                          (*g_eff_ot_entry & PRIM_TAG_ADDR);                   \
        *g_eff_ot_entry = ((u32)g_eff_prim_tag & PRIM_TAG_ADDR) |              \
                          (*g_eff_ot_entry & PRIM_TAG_LEN);                    \
    }

/* The two quad buffers, one a frame. */
static POLY_FT4 *g_eff_first_prim_buffer;
static POLY_FT4 *g_eff_second_prim_buffer;
/* Sixteen bytes that no code names. How many variables they were is not
 * known: two of eight bytes here, as a larger one would not be among the
 * small ones. */
static s32 g_eff_unused_words_a[2];
static s32 g_eff_unused_words_b[2];
/* Waves each set of particles has let go. */
static s16 g_eff_particle_waves_out[PARTICLE_SETS];
static s16 g_eff_lock_flash_rate; /* enum LockFlashRate */
/* A bit for each slot in use. */
static s32 g_eff_dust_live;
static s32 g_eff_splash_live;
/* Each fighter's footprints: how many (10 and up once all ten are used,
 * when the count goes round 10..19 to say which is trodden over next), a
 * bit for each in use, and which texture. */
static u32 g_eff_footprint_counts[2];
static s32 g_eff_footprint_live[2];
static s16 g_eff_footprint_shapes[2];
static s32 g_eff_muzzle_flash_live;
static s32 g_eff_light_color; /* 0xRRGGBB */
/* The request being served, and for an effect being started where it is and
 * which way it points (the request's copies). */
static EffRequest *g_eff_request;
/* The view this frame, and its matrix. */
static s16 *g_eff_view_angles;
static s32 *g_eff_view_origin;
static MATRIX *g_eff_view_matrix;
static s16 *g_eff_direction;
static s32 *g_eff_position;
/* The rest point into the scratchpad (EffScratch, eff_BindScratch). */
static s32 *g_eff_footprint_shape;
static s32 *g_eff_flash_rate;
static TexRec *g_eff_tex;
static TexRec *g_eff_clut_tex;
static s32 *g_eff_tpage;
static s32 *g_eff_clut;
static u16 *g_eff_u0;
static u16 *g_eff_u1;
static u16 *g_eff_v0;
static u16 *g_eff_v1;
static s16 *g_eff_base_width;
static s16 *g_eff_base_height;
static s32 *g_eff_screen_dist;
static s32 *g_eff_scale;
static s32 *g_eff_screen_xy;
static s16 *g_eff_left;
static s16 *g_eff_right;
static s32 *g_eff_top;
static s32 *g_eff_bottom;
static s32 *g_eff_rtp_flag;
static s32 *g_eff_depth;
static u16 *g_eff_uv0;
static u16 *g_eff_uv1;
static u16 *g_eff_uv2;
static u16 *g_eff_uv3;
static u32 *g_eff_ot_entry;
static u32 *g_eff_prim_tag;
static EffWork *g_eff_work;
/* Whether each of the two dirt effects is grass. A guess from the colours:
 * nothing ever sets it. */
static s16 g_eff_dirt_is_grass[2];

#if RB_PORT
/* (The native port.) Runs handler `index` of g_eff_handlers and says
 * whether its effect lives on, as the PlayStation has it. Eleven first
 * handlers, the flashes', return nothing, and what they are called for is
 * stored as that all the same: on the PlayStation it is what their last
 * instructions left in the result's register, which in each of the eleven
 * is the 1 they have just put in g_eff_stage. So those effects live. A
 * PC's register holds something else, and its low byte is what would be
 * stored: a clash's spark, a blunt hit's flash would be over before they
 * were drawn (docs/PORT.md, "What the PlayStation's compiler decided"). */
static s32 eff_RunHandler(s32 index) {
    void *handler = g_eff_handlers[index];

    if (handler == eff_SpawnLockFlash || handler == eff_SpawnClashSpark0 ||
        handler == eff_SpawnClashSpark1 || handler == eff_SpawnClashStreak0 ||
        handler == eff_SpawnClashStreak1 ||
        handler == eff_SpawnMatchedStreak0 ||
        handler == eff_SpawnMatchedStreak1 || handler == eff_SpawnBluntHit0 ||
        handler == eff_SpawnBluntHit1 || handler == eff_SpawnArmorSpark0 ||
        handler == eff_SpawnArmorSpark1) {
        ((void (*)(void))handler)();
        return 1;
    }
    return ((EffHandler)handler)();
}
#endif

/* Starts the effect g_eff_request asks for: gives its first handler the
 * place and direction, and lights the light if the request wants it. */
static void eff_Start(void) {
    s32 alive;

    g_eff_stage[g_eff_request->effect.id] = 0;
    g_eff_request->position[0] = g_eff_request->position_src[0];
    g_eff_request->position[1] = g_eff_request->position_src[1];
    g_eff_request->position[2] = g_eff_request->position_src[2];
#if RB_PORT
    /* (An effect that is given no direction, as a hit's flash is, reads
     * one all the same: wherever the request's last one was, and before
     * any, at address 0, where the console has its kernel's memory. The
     * port has no memory there, and reads zeroes.) */
    if (g_eff_request->direction_src == NULL) {
        g_eff_request->direction_src = (s16 *)g_port_low_memory;
    }
#endif
    g_eff_request->vector[0] = g_eff_request->direction_src[0];
    g_eff_request->vector[1] = g_eff_request->direction_src[1];
    g_eff_direction = g_eff_request->vector;
    g_eff_request->vector[2] = g_eff_request->direction_src[2];
    g_eff_position = g_eff_request->position;

#if RB_PORT
    alive = eff_RunHandler(g_eff_first_handler[g_eff_request->effect.id] +
                           g_eff_stage[g_eff_request->effect.id]);
#else
    alive = ((EffHandler)
                 g_eff_handlers[g_eff_first_handler[g_eff_request->effect.id] +
                                g_eff_stage[g_eff_request->effect.id]])();
#endif
    *g_eff_request->alive_dest = alive;

    if (g_eff_request->effect.word & EFF_LIT) {
        g_eff_light_frames = EFF_LIGHT_FRAMES;
    }
}

/* Runs the handler of the scratchpad request's effect for this frame, with
 * the view matrix set. */
static void eff_Step(void) {
    EffRequest *request;
    s16 *angles;
    s32 *origin;
    u16 effect;
    s32 alive;

    request = g_eff_request;
    angles = g_eff_view_angles;
    angles[0] = request->view_angles_src[0];
    angles[1] = request->view_angles_src[1];
    angles[2] = request->view_angles_src[2];

    origin = g_eff_view_origin;
    origin[0] = request->view_position_src[0];
    origin[1] = request->view_position_src[1];
    origin[2] = request->view_position_src[2];
    eff_SetViewMatrix(angles, origin, g_eff_view_matrix);

    effect = g_eff_request->effect.id;
#if RB_PORT
    alive = eff_RunHandler(g_eff_first_handler[effect] + g_eff_stage[effect]);
#else
    alive = ((EffHandler)g_eff_handlers[g_eff_first_handler[effect] +
                                        g_eff_stage[effect]])();
#endif

    *g_eff_request->alive_dest = alive;
}

/* Ends every effect at once. */
static void eff_Reset(void) {
    s32 effect;

    for (effect = 0; effect < EFF_COUNT; effect++) {
        g_eff_stage[effect] = 0;
        g_eff_alive[effect] = 0;
    }
    g_eff_footprint_shapes[1] = 0;
    g_eff_footprint_shapes[0] = 0;
    g_eff_footprint_live[1] = 0;
    g_eff_footprint_live[0] = 0;
    g_eff_footprint_counts[1] = 0;
    g_eff_footprint_counts[0] = 0;
    g_eff_muzzle_flash_live = 0;
    g_eff_dust_live = 0;
    g_eff_splash_live = 0;
}

/* Reads the effect textures of the mode being played into `buffer` and
 * loads them into video memory (the right-hand strip, at 896 across), then
 * takes two buffers of EFF_PRIM_COUNT quads at `quads`. Returns their end. */
s32 eff_LoadTextures(s32 buffer, s32 quads) {
    /* FAKE: `buffer`, copied */
    u32 buf;
    RECT rect;
    s32 mode;
    s32 second_buffer;
    buf = buffer;
    cdrom_WaitIdle();
    mode = g_game_settings.menu_mode;
    if (mode == FIGHT_MODE_STORY) {
        cdrom_StartRead(
            cdrom_GetFileIndex(CDROM_GROUP_DATA, EFF_FILE_STORY), buffer);
    } else if (mode == FIGHT_MODE_CHANBARA) {
        cdrom_StartRead(
            cdrom_GetFileIndex(CDROM_GROUP_DATA, EFF_FILE_CHANBARA), buf);
    } else if (mode == FIGHT_MODE_PRACTICE) {
        cdrom_StartRead(
            cdrom_GetFileIndex(CDROM_GROUP_DATA, EFF_FILE_PRACTICE), buf);
    } else if (mode == FIGHT_MODE_TEAM) {
        cdrom_StartRead(
            cdrom_GetFileIndex(CDROM_GROUP_DATA, EFF_FILE_TEAM), buf);
    } else {
        cdrom_StartRead(
            cdrom_GetFileIndex(CDROM_GROUP_DATA, EFF_FILE_OTHER), buf);
    }
    cdrom_WaitIdle();
    rect.x = EFF_VRAM_X;
    rect.y = 0;
    rect.w = EFF_VRAM_WIDTH;
    rect.h = EFF_VRAM_HEIGHT;
    DrawSync(0);
    LoadImage(&rect, (u32 *)buf);
    DrawSync(0);
    rect.w = EFF_VRAM_MODE_WIDTH;
    rect.y = EFF_VRAM_HEIGHT;
    rect.h = EFF_VRAM_MODE_HEIGHT;
    LoadImage(
        &rect, (u32 *)(buf + EFF_VRAM_WIDTH * EFF_VRAM_HEIGHT * sizeof(u16)));
    DrawSync(0);
    eff_Reset();
    srand(rand());
    second_buffer = quads + EFF_PRIM_COUNT * sizeof(POLY_FT4);
    g_eff_first_prim_buffer = (POLY_FT4 *)quads;
    g_eff_second_prim_buffer = (POLY_FT4 *)second_buffer;
    return second_buffer + EFF_PRIM_COUNT * sizeof(POLY_FT4);
}

void eff_SelectPrimBuffer(s32 second) {
    g_eff_next_prim =
        second ? g_eff_second_prim_buffer : g_eff_first_prim_buffer;
    g_eff_prims = g_eff_next_prim;
}

/* Points the file's globals at the scratchpad and notes the view. Whatever
 * else used the scratchpad since last time has left it in any state. */
static void eff_BindScratch(s16 *view_angles, s32 *view_position) {
    g_eff_request = &EFF_SPAD->request;
    g_eff_view_angles = EFF_SPAD->request.vector;
    g_eff_view_origin = EFF_SPAD->request.position;
    g_eff_view_matrix = &EFF_SPAD->view_matrix;
    g_eff_tex = &EFF_SPAD->first_tex;
    g_eff_tpage = &EFF_SPAD->tpage;
    g_eff_clut = &EFF_SPAD->clut;
    g_eff_u0 = &EFF_SPAD->u0;
    g_eff_u1 = &EFF_SPAD->u1;
    g_eff_v0 = &EFF_SPAD->v0;
    g_eff_v1 = &EFF_SPAD->v1;
    g_eff_base_width = &EFF_SPAD->base_width;
    g_eff_base_height = &EFF_SPAD->base_height;
    g_eff_screen_dist = &EFF_SPAD->screen_dist;
    g_eff_scale = &EFF_SPAD->scale;
    g_eff_screen_xy = EFF_SPAD->screen_xy;
    g_eff_left = &EFF_SPAD->left;
    g_eff_right = &EFF_SPAD->right;
    g_eff_top = &EFF_SPAD->top;
    g_eff_bottom = &EFF_SPAD->bottom;
    g_eff_rtp_flag = &EFF_SPAD->rtp_flag;
    g_eff_depth = EFF_SPAD->depth;
    g_eff_uv0 = &EFF_SPAD->uv0;
    g_eff_uv1 = &EFF_SPAD->uv1;
    g_eff_uv2 = &EFF_SPAD->uv2;
    g_eff_uv3 = &EFF_SPAD->uv3;
    g_eff_ot_entry = &EFF_SPAD->first_ot_entry;
    g_eff_prim_tag = &EFF_SPAD->first_prim_tag;
    g_eff_footprint_shape = &EFF_SPAD->footprint_shape;
    g_eff_flash_rate = &EFF_SPAD->flash_rate;
    g_eff_clut_tex = &EFF_SPAD->first_clut_tex;
    g_eff_work = &EFF_SPAD->work;
    EFF_SPAD->request.view_angles_src = view_angles;
    EFF_SPAD->request.view_position_src = view_position;
}

/* A frame of the effects: runs the handler of every one that is alive, seen
 * from `view_position` at `view_angles`, counts the light down and puts it
 * out at the end, and says so if the frame's quads ran out. */
void eff_Update(s16 *view_angles, s32 *view_position) {
    s32 quad_count;
    s32 effect;
    eff_BindScratch(view_angles, view_position);
    for (effect = 0; effect < EFF_COUNT; effect++) {
        g_eff_request->alive_dest = &g_eff_alive[effect];
        if (g_eff_alive[effect]) {
            g_eff_request->effect.word = effect;
            eff_Step();
        }
    }
    if (g_eff_light_frames >= 2) {
        g_eff_light_frames -= 1;
        light_SetPointLight(&g_eff_light_position, g_eff_light_color);
    } else if (g_eff_light_frames == 1) {
        light_DisablePointLight();
        g_eff_light_frames = 0;
    }
    quad_count = g_eff_next_prim - g_eff_prims;
    if (quad_count >= EFF_PRIM_COUNT) {
        printf("eff prim over :%d \n", quad_count - EFF_PRIM_COUNT);
    }
}

void eff_Stop(void) {
    eff_Reset();
    light_DisablePointLight();
    g_eff_light_frames = 0;
}

/* ---- what the fight asks for (see eff.h) ----
 * Each fills in the request, starts the effect, and puts the light where
 * the effect is, in the effect's colour. */

/* A gun fires. */
void eff_StartMuzzleFlash(s32 *position, s16 *src_angles) {
    s16 angles[3];

    angles[0] = src_angles[0];
    angles[1] = src_angles[1];
    g_eff_request = &g_eff_start_request;
    angles[2] = src_angles[2];
    g_eff_start_request.direction_src = angles;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_MUZZLE_FLASH];
    g_eff_start_request.effect.word = EFF_MUZZLE_FLASH | EFF_NEW | EFF_LIT;
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFFFFEF; /* white */
}

/* The effects from here to eff_StartArmorSpark come in pairs, so both
 * fighters can have one: the first free of the two is used, and if neither
 * is free both are ended and the first used. */

/* A blade goes into a body. */
void eff_StartHitFlash(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    if (g_eff_alive[EFF_HIT_FLASH_0] && g_eff_alive[EFF_HIT_FLASH_1]) {
        g_eff_alive[EFF_HIT_FLASH_1] = 0;
        g_eff_alive[EFF_HIT_FLASH_0] = 0;
    }
    if (!g_eff_alive[EFF_HIT_FLASH_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_HIT_FLASH_0];
        g_eff_request->effect.word = EFF_HIT_FLASH_0 | EFF_NEW | EFF_LIT;
    } else if (!g_eff_alive[EFF_HIT_FLASH_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_HIT_FLASH_1];
        g_eff_start_request.effect.word = EFF_HIT_FLASH_1 | EFF_NEW | EFF_LIT;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFF0060; /* rose */
}

/* Locked blades grind: a flash, and the frames to wait before the next. */
s32 eff_StartLockFlash(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_LOCK_FLASH];
    g_eff_start_request.effect.word = EFF_LOCK_FLASH | EFF_NEW | EFF_LIT;
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x8080FF; /* pale blue */
    return 1;
}

/* The three rated lock flashes share one effect, so a new one replaces the
 * last. */
s32 eff_StartLockFlashSlow(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_LOCK_FLASH_RATED];
    g_eff_start_request.effect.word =
        EFF_LOCK_FLASH_RATED | EFF_NEW | EFF_LIT | EFF_RATE(LOCK_FLASH_SLOW);
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x8080FF; /* pale blue */
    return 16;                    /* frames to the next */
}

/* The same, played twice as fast. */
s32 eff_StartLockFlashMedium(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_LOCK_FLASH_RATED];
    g_eff_start_request.effect.word =
        EFF_LOCK_FLASH_RATED | EFF_NEW | EFF_LIT | EFF_RATE(LOCK_FLASH_MEDIUM);
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x8080FF; /* pale blue */
    return 8;                     /* frames to the next */
}

/* And three times as fast. */
s32 eff_StartLockFlashFast(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_LOCK_FLASH_RATED];
    g_eff_start_request.effect.word =
        EFF_LOCK_FLASH_RATED | EFF_NEW | EFF_LIT | EFF_RATE(LOCK_FLASH_FAST);
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x8080FF; /* pale blue */
    return 5;                     /* frames to the next */
}

/* Two weapons touch. */
void eff_StartClashSpark(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    if (g_eff_alive[EFF_CLASH_SPARK_0] && g_eff_alive[EFF_CLASH_SPARK_1]) {
        g_eff_alive[EFF_CLASH_SPARK_1] = 0;
        g_eff_alive[EFF_CLASH_SPARK_0] = 0;
    }
    if (!g_eff_alive[EFF_CLASH_SPARK_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_CLASH_SPARK_0];
        g_eff_request->effect.word = EFF_CLASH_SPARK_0 | EFF_NEW | EFF_LIT;
    } else if (!g_eff_alive[EFF_CLASH_SPARK_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_CLASH_SPARK_1];
        g_eff_start_request.effect.word = EFF_CLASH_SPARK_1 | EFF_NEW | EFF_LIT;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFF8080; /* pale red */
}

/* Two attacks meet: the fighter's streak, which replaces the one it had. */
void eff_StartClashStreak(s32 *position, s32 fighter) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    switch (fighter) {
    case 0:
        g_eff_alive[EFF_CLASH_STREAK_0] = 0;
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_CLASH_STREAK_0];
        g_eff_start_request.effect.word =
            EFF_CLASH_STREAK_0 | EFF_NEW | EFF_LIT;
        break;
    case 1:
        g_eff_alive[EFF_CLASH_STREAK_1] = 0;
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_CLASH_STREAK_1];
        g_eff_start_request.effect.word =
            EFF_CLASH_STREAK_1 | EFF_NEW | EFF_LIT;
        break;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x10FFFF; /* cyan */
}

/* Two attacks of the same kind meet: the fighter's streak, likewise. */
void eff_StartMatchedStreak(s32 *position, s32 fighter) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    switch (fighter) {
    case 0:
        g_eff_alive[EFF_MATCHED_STREAK_0] = 0;
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_MATCHED_STREAK_0];
        g_eff_start_request.effect.word =
            EFF_MATCHED_STREAK_0 | EFF_NEW | EFF_LIT;
        break;
    case 1:
        g_eff_alive[EFF_MATCHED_STREAK_1] = 0;
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_MATCHED_STREAK_1];
        g_eff_start_request.effect.word =
            EFF_MATCHED_STREAK_1 | EFF_NEW | EFF_LIT;
        break;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0x10FF10; /* green */
}

/* A wooden sword lands. */
void eff_StartBluntHit(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    if (g_eff_alive[EFF_BLUNT_HIT_0] && g_eff_alive[EFF_BLUNT_HIT_1]) {
        g_eff_alive[EFF_BLUNT_HIT_1] = 0;
        g_eff_alive[EFF_BLUNT_HIT_0] = 0;
    }
    if (!g_eff_alive[EFF_BLUNT_HIT_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_BLUNT_HIT_0];
        g_eff_request->effect.word = EFF_BLUNT_HIT_0 | EFF_NEW | EFF_LIT;
    } else if (!g_eff_alive[EFF_BLUNT_HIT_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_BLUNT_HIT_1];
        g_eff_start_request.effect.word = EFF_BLUNT_HIT_1 | EFF_NEW | EFF_LIT;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xC06013; /* orange */
}

/* Blood from a wound, thrown along `direction`. */
void eff_StartBlood(s32 *position, s16 *direction) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.direction_src = direction;
    if (g_eff_alive[EFF_BLOOD_0] && g_eff_alive[EFF_BLOOD_1]) {
        g_eff_alive[EFF_BLOOD_1] = 0;
        g_eff_alive[EFF_BLOOD_0] = 0;
    }
    if (!g_eff_alive[EFF_BLOOD_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_BLOOD_0];
        g_eff_request->effect.word = EFF_BLOOD_0 | EFF_NEW | EFF_LIT;
    } else if (!g_eff_alive[EFF_BLOOD_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_BLOOD_1];
        g_eff_start_request.effect.word = EFF_BLOOD_1 | EFF_NEW | EFF_LIT;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFF0000; /* red */
}

void eff_StartSplash(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_SPLASH];
    g_eff_start_request.effect.word = EFF_SPLASH | EFF_NEW;
    eff_Start();
}

void eff_StartDust(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.alive_dest = &g_eff_alive[EFF_DUST];
    g_eff_start_request.effect.word = EFF_DUST | EFF_NEW;
    eff_Start();
}

/* A footprint of the fighter's, turned to `yaw`, with the texture
 * `shape_bits` says. No light. */
void eff_AddFootprint(s32 *position, s16 yaw, s32 shape_bits, s32 fighter) {
    s16 angles[4];

    g_eff_request = &g_eff_start_request;
    angles[2] = 0;
    angles[0] = 0;
    angles[1] = yaw;
    g_eff_start_request.position_src = position;
    g_eff_start_request.direction_src = angles;
    if (fighter == 0) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_FOOTPRINTS_0];
        g_eff_start_request.effect.word =
            shape_bits + (EFF_FOOTPRINTS_0 | EFF_NEW);
    } else {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_FOOTPRINTS_1];
        g_eff_start_request.effect.word =
            shape_bits + (EFF_FOOTPRINTS_1 | EFF_NEW);
    }
    eff_Start();
}

/* A bullet lands on a surface whose normal is `normal`: debris thrown every
 * way, and a puff of dust. */
void eff_StartBulletImpact(s32 *position, s16 *normal) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    g_eff_start_request.direction_src = normal;
    if (g_eff_alive[EFF_IMPACT_0] && g_eff_alive[EFF_IMPACT_1]) {
        g_eff_alive[EFF_IMPACT_1] = 0;
        g_eff_alive[EFF_IMPACT_0] = 0;
    }
    if (!g_eff_alive[EFF_IMPACT_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_IMPACT_0];
        g_eff_request->effect.word = EFF_IMPACT_0 | EFF_NEW | EFF_LIT;
        eff_Start();
        g_eff_request->alive_dest = &g_eff_alive[EFF_DUST];
        g_eff_request->effect.word = EFF_DUST | EFF_NEW;
    } else if (!g_eff_alive[EFF_IMPACT_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_IMPACT_1];
        g_eff_request->effect.word = EFF_IMPACT_1 | EFF_NEW | EFF_LIT;
        eff_Start();
        g_eff_request->alive_dest = &g_eff_alive[EFF_DUST];
        g_eff_request->effect.word = EFF_DUST | EFF_NEW;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFF8080; /* pale red */
}

/* Thrown up and forward from `position`, along `yaw`. `grass` is not
 * passed by the caller (see eff.h). */
void eff_StartThrownDirt(s32 *position, s32 yaw, s32 grass) {
    SVECTOR throw;
    SVECTOR angles;
    VECTOR turned;
    MATRIX yaw_matrix;
    s32 gte_flags;

    g_eff_request = &g_eff_start_request;
    if (grass != 1) {
        grass = 0;
    }
    throw.vy = -THROWN_SPEED;
    throw.vx = 0;
    throw.vz = THROWN_SPEED;
    turned.vz = 0;
    turned.vy = 0;
    turned.vx = 0;
    angles.vy = yaw;
    angles.vz = 0;
    angles.vx = 0;
    RotMatrix(&angles, &yaw_matrix);
    yaw_matrix.t[2] = 0;
    yaw_matrix.t[1] = 0;
    yaw_matrix.t[0] = 0;
    SetRotMatrix(&yaw_matrix);
    SetTransMatrix(&yaw_matrix);
    RotTrans(&throw, &turned, &gte_flags);
    throw.vx = turned.vx;
    throw.vy = turned.vy;
    throw.vz = turned.vz;
    g_eff_request->position_src = position;
    g_eff_request->direction_src = &throw.vx;
    if (g_eff_alive[EFF_DIRT_0] && g_eff_alive[EFF_DIRT_1]) {
        g_eff_alive[EFF_DIRT_1] = 0;
        g_eff_alive[EFF_DIRT_0] = 0;
    }
    if (!g_eff_alive[EFF_DIRT_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_DIRT_0];
        g_eff_request->effect.word = EFF_DIRT_0 | EFF_NEW;
        g_eff_dirt_is_grass[0] = grass;
    } else if (!g_eff_alive[EFF_DIRT_1]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_DIRT_1];
        g_eff_request->effect.word = EFF_DIRT_1 | EFF_NEW;
        g_eff_dirt_is_grass[1] = grass;
    }
    eff_Start();
}

/* Water thrown up and forward from `position`, along `yaw`, the same
 * way. */
void eff_StartThrownWater(s32 *position, s16 yaw) {
    SVECTOR throw;
    SVECTOR angles;
    VECTOR turned;
    MATRIX yaw_matrix;
    s32 gte_flags;

    g_eff_request = &g_eff_start_request;
    throw.vy = -THROWN_SPEED;
    throw.vx = 0;
    throw.vz = THROWN_SPEED;
    turned.vz = 0;
    turned.vy = 0;
    turned.vx = 0;
    angles.vz = 0;
    angles.vy = yaw;
    angles.vx = 0;
    RotMatrix(&angles, &yaw_matrix);
    yaw_matrix.t[2] = 0;
    yaw_matrix.t[1] = 0;
    yaw_matrix.t[0] = 0;
    SetRotMatrix(&yaw_matrix);
    SetTransMatrix(&yaw_matrix);
    RotTrans(&throw, &turned, &gte_flags);
    throw.vx = turned.vx;
    throw.vy = turned.vy;
    throw.vz = turned.vz;
    g_eff_request->position_src = position;
    g_eff_request->direction_src = &throw.vx;
    if (g_eff_alive[EFF_WATER_0] && g_eff_alive[EFF_WATER_1]) {
        g_eff_alive[EFF_WATER_1] = 0;
        g_eff_alive[EFF_WATER_0] = 0;
    }
    if (!g_eff_alive[EFF_WATER_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_WATER_0];
        g_eff_request->effect.word = EFF_WATER_0 | EFF_NEW;
    } else if (!g_eff_alive[EFF_WATER_1]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_WATER_1];
        g_eff_request->effect.word = EFF_WATER_1 | EFF_NEW;
    }
    eff_Start();
}

/* A hit that does nothing to an armoured fighter. */
void eff_StartArmorSpark(s32 *position) {
    g_eff_request = &g_eff_start_request;
    g_eff_start_request.position_src = position;
    if (g_eff_alive[EFF_ARMOR_SPARK_0] && g_eff_alive[EFF_ARMOR_SPARK_1]) {
        g_eff_alive[EFF_ARMOR_SPARK_1] = 0;
        g_eff_alive[EFF_ARMOR_SPARK_0] = 0;
    }
    if (!g_eff_alive[EFF_ARMOR_SPARK_0]) {
        g_eff_request->alive_dest = &g_eff_alive[EFF_ARMOR_SPARK_0];
        g_eff_request->effect.word = EFF_ARMOR_SPARK_0 | EFF_NEW | EFF_LIT;
    } else if (!g_eff_alive[EFF_ARMOR_SPARK_1]) {
        g_eff_start_request.alive_dest = &g_eff_alive[EFF_ARMOR_SPARK_1];
        g_eff_start_request.effect.word = EFF_ARMOR_SPARK_1 | EFF_NEW | EFF_LIT;
    }
    eff_Start();
    g_eff_light_position.vx = position[0];
    g_eff_light_position.vy = position[1];
    g_eff_light_position.vz = position[2];
    g_eff_light_color = 0xFF00FF; /* magenta */
}

/* Makes the view matrix from the view's angles, squashed by
 * g_eff_view_scale, and gives it to the GTE. `view_origin` is not used. */
static void eff_SetViewMatrix(
    s16 *view_angles, s32 *view_origin, MATRIX *matrix_out) {
    SVECTOR *angles = &g_eff_work->view_angles;
    angles->vx = view_angles[0];
    angles->vy = view_angles[1];
    angles->vz = view_angles[2];
    RotMatrix(angles, matrix_out);
    matrix_out->t[2] = 0;
    matrix_out->t[1] = 0;
    matrix_out->t[0] = 0;
    ScaleMatrixL(matrix_out, &g_eff_view_scale);
    SetRotMatrix(matrix_out);
}

/* Copies the stage file's list of flames. */
void eff_SetFlames(Flame *flames) {
    s32 i;

    g_eff_flame_frame = 0;
    for (i = 0; flames[i].x_bits & FLAME_PRESENT; i++) {
        g_eff_flames[i].x_bits = flames[i].x_bits;
        g_eff_flames[i].y_bits = flames[i].y_bits;
        g_eff_flames[i].z = flames[i].z;
    }
    g_eff_flames[i].x_bits = g_eff_flames[i].y_bits = g_eff_flames[i].z = 0;
}

/* Draws the stage's flames: each a quad standing on its point and facing
 * the view, sized by its distance and flickering in height, showing the
 * next frame of its animation every call. */
void eff_DrawFlames(s16 *view_angles, s32 *view_position) {
    FlameWork *work;
    s16 *half_width;
    s16 *flame_height;
    VECTOR *rotated_position;
    VECTOR *relative;
    SVECTOR *zero;
    s32 *depth_cue_out;
    u32 *ot_index;
    POLY_FT4 *prim;
    EffRequest *request;
    s16 *angles;
    s32 *origin;
    MATRIX *matrix;
    s16 i;
    s32 scaled_width;
    s32 scaled_height;
    s16 width;
    s16 height;

    eff_BindScratch(view_angles, view_position);
    request = g_eff_request;
    angles = g_eff_view_angles;
    angles[0] = request->view_angles_src[0];
    prim = g_eff_next_prim;
    angles[1] = request->view_angles_src[1];
    angles[2] = request->view_angles_src[2];
    origin = g_eff_view_origin;
    matrix = g_eff_view_matrix;
    origin[0] = request->view_position_src[0];
    work = &g_eff_work->flames;
    origin[1] = request->view_position_src[1];
    origin[2] = request->view_position_src[2];
    g_eff_flame_frame++;
    eff_SetViewMatrix(angles, origin, matrix);
    SetRotMatrix(g_eff_view_matrix);
    half_width = &work->half_width;
    flame_height = &work->height;
    rotated_position = &work->rotated_position;
    relative = &work->view_relative_position;
    zero = &work->zero_vector;
    depth_cue_out = &work->depth_cue;
    ot_index = &work->ot_index;
    zero->vz = 0;
    zero->vy = 0;
    zero->vx = 0;
    /* in 256ths, as the sizes on screen are worked out */
    *g_eff_screen_dist = ReadGeomScreen() << 8;
    *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_RIGHT;
    for (i = 0; g_eff_flames[i].x_bits & FLAME_PRESENT; i++) {
        switch (FLAME_KIND(g_eff_flames[i])) {
        case FLAME_FIRE_SMALLER:
            *g_eff_base_width = FLAME_FIRE_SMALLER_WIDTH;
            *g_eff_base_height = FLAME_FIRE_SMALLER_HEIGHT;
            goto fire;
        case FLAME_FIRE:
            *g_eff_base_width = FLAME_FIRE_WIDTH;
            *g_eff_base_height = FLAME_FIRE_HEIGHT;
        fire:
            g_eff_clut_tex = g_eff_tex =
                &g_eff_flame_texs[g_eff_flame_frame % FLAME_FIRE_FRAMES];
            if (g_game_settings.grayscale) {
                g_eff_clut_tex = &g_eff_flame_texs[FLAME_FIRE_GRAY_TEX];
            }
            *g_eff_u1 = g_eff_tex->u + (FLAME_FIRE_TEX_SIZE - 1);
            *g_eff_v1 = g_eff_tex->v + (FLAME_FIRE_TEX_SIZE - 1);
            break;
        case FLAME_TORCH:
            *g_eff_base_width = FLAME_TORCH_WIDTH;
            *g_eff_base_height = FLAME_TORCH_HEIGHT;
            goto torch;
        case FLAME_TORCH_TALL:
            *g_eff_base_width = FLAME_TORCH_TALL_WIDTH;
            *g_eff_base_height = FLAME_TORCH_TALL_HEIGHT;
        torch:
            g_eff_clut_tex = g_eff_tex =
                &g_eff_flame_texs[FLAME_TORCH_TEX + (g_eff_flame_frame &
                                                     (FLAME_TORCH_FRAMES - 1))];
            if (g_game_settings.grayscale) {
                g_eff_clut_tex = &g_eff_flame_texs[FLAME_TORCH_GRAY_TEX];
            }
            *g_eff_u1 = g_eff_tex->u + (FLAME_TORCH_TEX_WIDTH - 1);
            *g_eff_v1 = g_eff_tex->v + (FLAME_TORCH_TEX_HEIGHT - 1);
            break;
        }
        /* x is above its one bit */
        relative->vx = g_eff_flames[i].x_bits / 2 - g_eff_view_origin[0];
        /* and y above the three of the kind (see Flame) */
        relative->vy = g_eff_flames[i].y_bits / 8 - g_eff_view_origin[1];
        relative->vz = g_eff_flames[i].z - g_eff_view_origin[2];
        ApplyRotMatrixLV(relative, rotated_position);
        /* no matrix: only the translation is read (see eff.h) */
        SetTransMatrix((MATRIX *)((u8 *)rotated_position - 0x14));
        RotTransPers(zero, g_eff_screen_xy, depth_cue_out, g_eff_rtp_flag);
        gte_stsz(g_eff_depth);
        *ot_index = render_DepthToOtShifted(*g_eff_depth, 0);
        /* not drawn nearer than a sixteenth of the projection distance */
        if (*ot_index < EFF_OT_END &&
            (*g_eff_screen_dist / 256 >> 4) < *ot_index) {
            *g_eff_clut = (u16)TEX_CLUT(g_eff_clut_tex);
            *g_eff_u0 = g_eff_tex->u;
            *g_eff_v0 = g_eff_tex->v;
            *g_eff_depth = *g_eff_depth ? *g_eff_depth : 1;
            *g_eff_scale = *g_eff_screen_dist / *g_eff_depth;
            scaled_width = *g_eff_base_width * *g_eff_scale;
            /* at least 2 across and high */
            width = scaled_width > 2 << 8 ? scaled_width >> 8 : 2;
            *half_width = width / 2;
            scaled_height = *g_eff_base_height * *g_eff_scale;
            height = scaled_height > 2 << 8 ? scaled_height >> 8 : 2;
            *flame_height = height;
            /* the flicker: up to a fifth taller */
            *flame_height += (*flame_height * rand() / 10) >> 14;
            SetPolyFT4(prim);
            prim->tpage = *g_eff_tpage;
            prim->clut = *g_eff_clut;
            setRGB0(prim, 0xFF, 0x80, 0x80); /* not seen: see SetShadeTex */
            prim->x0 = *g_eff_screen_xy - *half_width;
            prim->y0 = (*g_eff_screen_xy >> 16) - *flame_height;
            prim->x1 = *half_width + *g_eff_screen_xy;
            prim->y1 = (*g_eff_screen_xy >> 16) - *flame_height;
            prim->x2 = *g_eff_screen_xy - *half_width;
            prim->y2 = *g_eff_screen_xy >> 16;
            prim->x3 = *half_width + *g_eff_screen_xy;
            prim->y3 = *g_eff_screen_xy >> 16;
            prim->u0 = *g_eff_u0;
            prim->v0 = *g_eff_v0;
            prim->u1 = *g_eff_u1;
            prim->v1 = *g_eff_v0;
            prim->u2 = *g_eff_u0;
            prim->v2 = *g_eff_v1;
            prim->u3 = *g_eff_u1;
            prim->v3 = *g_eff_v1;
            SetShadeTex(prim, 1);
            SetSemiTrans(prim, 1);
            if (prim - g_eff_prims < EFF_PRIM_COUNT - 1) {
                g_eff_ot_entry = g_disp_ot + *ot_index;
                g_eff_prim_tag = &prim->tag;
                EFF_ADD_PRIM();
                prim++;
            }
        }
    }
    g_eff_next_prim = prim;
}

/* Takes the first free muzzle flash slot. */
s32 eff_SpawnMuzzleFlash(void) {
    s32 *position;
    s16 *angles;
    s32 slot_index;
    s32 slot_bit;

    g_eff_stage[EFF_MUZZLE_FLASH] = 1;
    position = g_eff_position;
    angles = g_eff_direction;
    for (slot_index = 0; slot_index < MUZZLE_FLASH_COUNT; slot_index++) {
        slot_bit = 1 << slot_index;
        if (!(g_eff_muzzle_flash_live & slot_bit)) {
            g_eff_muzzle_flash_positions[slot_index].x = position[0];
            g_eff_muzzle_flash_positions[slot_index].y = position[1];
            g_eff_muzzle_flash_positions[slot_index].z = position[2];
            g_eff_muzzle_flash_angles[slot_index].vx = angles[0];
            g_eff_muzzle_flash_angles[slot_index].vy = angles[1];
            g_eff_muzzle_flash_live |= slot_bit;
            g_eff_muzzle_flash_angles[slot_index].vz = angles[2];
            g_eff_muzzle_flash_ages[slot_index] = 0;
            break;
        }
    }
    return 1;
}

/* A muzzle flash is three crossed quads along the barrel, one across its
 * end and two along it, that swell for three frames as fire and then fade
 * as smoke for three more.
 *
 * The work area keeps six ordering-table indices, which is two flashes'
 * quads, and the size and the gray of the flash being made come straight
 * after them. A third flash alive writes its indices over those, which
 * does no harm; with a fourth, the fourth's size is written over the
 * third's indices before the quads are linked, and the third's quads go
 * into the ordering table wherever the size says: the original's
 * behaviour. */
s32 eff_DrawMuzzleFlashes(void) {
    s32 count;
    MATRIX *composite;
    s32 *depth_cue_out;
    u16 *quad_ot_indices;
    s32 *scale;
    s32 *gray;
    MATRIX *matrices;
    MATRIX *matrix;
    s32 *sxy;
    POLY_FT4 *quad;
    s32 slot_index;
    s32 quad_index;
    s32 k;
    s32 swell;
    s32 level;
    s32 slot_bit;
    SVECTOR *quad_vertices;
    POLY_FT4 *quads_end;
#if RB_FIX_MUZZLE_FLASH_INDICES
    /* an index for every quad of every flash */
    u16 rb_ot_indices[MUZZLE_FLASH_COUNT * MUZZLE_FLASH_QUADS];
#endif

    /* FAKE: the work area, held in the quad pointer */
    quad = (POLY_FT4 *)g_eff_work;
    count = 0;
    matrices = ((MuzzleFlashWork *)quad)->matrices;
    composite = &((MuzzleFlashWork *)quad)->composite;
    sxy = ((MuzzleFlashWork *)quad)->sxy;
    depth_cue_out = &((MuzzleFlashWork *)quad)->depth_cue;
    quad_ot_indices = ((MuzzleFlashWork *)quad)->ot_indices;
    scale = ((MuzzleFlashWork *)quad)->scale;
    gray = &((MuzzleFlashWork *)quad)->gray;
#if RB_FIX_MUZZLE_FLASH_INDICES
    /* the work area has room for six indices, two flashes' quads, with
     * the size of the flash being made straight after them */
    if (g_rb_settings.bugfixes) {
        quad_ot_indices = rb_ot_indices;
    }
#endif
    quad = g_eff_next_prim;
    for (slot_index = 0; slot_index < MUZZLE_FLASH_COUNT; slot_index++) {
        slot_bit = 1 << slot_index;
        if (!(g_eff_muzzle_flash_live & slot_bit)) {
            continue;
        }
        matrix = &matrices[slot_index];
        /* its size: a sine that begins at its height and falls away over
         * the flash's life, and the part of its life that has passed */
        swell =
            rsin(((g_eff_muzzle_flash_ages[slot_index] + MUZZLE_FLASH_FRAMES)
                  << 10) /
                 MUZZLE_FLASH_FRAMES);
        scale[0] = scale[1] = scale[2] =
            swell +
            (g_eff_muzzle_flash_ages[slot_index] << 12) / MUZZLE_FLASH_FRAMES;
        RotMatrixZYX(&g_eff_muzzle_flash_angles[slot_index], matrix);
        ScaleMatrix(matrix, (VECTOR *)scale);
        matrix->t[0] =
            g_eff_muzzle_flash_positions[slot_index].x - g_eff_view_origin[0];
        matrix->t[1] =
            g_eff_muzzle_flash_positions[slot_index].y - g_eff_view_origin[1];
        matrix->t[2] =
            g_eff_muzzle_flash_positions[slot_index].z - g_eff_view_origin[2];
        CompMatrix(g_eff_view_matrix, matrix, composite);
        SetRotMatrix(composite);
        SetTransMatrix(composite);
        for (quad_index = 0; quad_index < MUZZLE_FLASH_QUADS; quad_index++) {
            if (g_eff_muzzle_flash_ages[slot_index] <
                MUZZLE_FLASH_FIRE_FRAMES) {
                *(s32 *)&quad->r0 = EFF_RGB_PLAIN;
            } else {
                /* the smoke: a grey that fades from the texture's own to
                 * nothing */
                level =
                    ((MUZZLE_FLASH_FRAMES - g_eff_muzzle_flash_ages[slot_index])
                     << 7) /
                    (MUZZLE_FLASH_FRAMES - MUZZLE_FLASH_FIRE_FRAMES);
                *gray = level;
                *(s32 *)&quad->r0 = level + (level << 8) + (level << 16);
            }
            if (g_eff_muzzle_flash_ages[slot_index] <
                MUZZLE_FLASH_FIRE_FRAMES) {
                g_eff_tex = quad_index ? g_eff_muzzle_flash_side_tex
                                       : g_eff_muzzle_flash_end_tex;
            } else {
                g_eff_tex = quad_index ? g_eff_muzzle_smoke_side_tex
                                       : g_eff_muzzle_smoke_end_tex;
            }
            if (quad_index) {
                *g_eff_u1 = g_eff_tex->u + (MUZZLE_FLASH_SIDE_TEX_WIDTH - 1);
            } else {
                *g_eff_u1 = g_eff_tex->u + (MUZZLE_FLASH_TEX_SIZE - 1);
            }
            *g_eff_v1 = g_eff_tex->v + (MUZZLE_FLASH_TEX_SIZE - 1);
            *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;
            *g_eff_clut = TEX_CLUT(g_eff_tex) << 16;
            *g_eff_tpage = *g_eff_tpage << 16;
            *g_eff_u0 = g_eff_tex->u;
            *g_eff_v0 = g_eff_tex->v;
            *g_eff_uv0 = *g_eff_u0 + (*g_eff_v0 << 8);
            *g_eff_uv1 = *g_eff_u1 + (*g_eff_v0 << 8);
            *g_eff_uv2 = *g_eff_u0 + (*g_eff_v1 << 8);
            *g_eff_uv3 = *g_eff_u1 + (*g_eff_v1 << 8);
            setlen(quad, FT4_LEN);
            quad->code = FT4_CODE | FT4_SEMI_TRANS;
            *(s32 *)&quad->u0 = *g_eff_uv0 + *g_eff_clut;
            *(s32 *)&quad->u1 = *g_eff_uv1 + *g_eff_tpage;
            *(u16 *)&quad->u2 = *g_eff_uv2;
            *(u16 *)&quad->u3 = *g_eff_uv3;
            SetPolyFT4(quad);
            SetShadeTex(quad, 1);
            SetSemiTrans(quad, 1);
            quad_vertices = g_eff_muzzle_flash_vertices[quad_index];
            RotTransPers4(quad_vertices, quad_vertices + 1, quad_vertices + 2,
                          quad_vertices + 3, sxy, sxy + 1, sxy + 2, sxy + 3,
                          depth_cue_out, g_eff_rtp_flag);
            gte_stsz(g_eff_depth);
            *g_eff_depth = render_DepthToOtShifted(*g_eff_depth, 0);
            if (*g_eff_depth == 0) {
                *g_eff_depth = 1;
            }
            if (*g_eff_depth < EFF_OT_END) {
                quad_ot_indices[count] = *g_eff_depth;
                *(s32 *)&quad->x0 = sxy[0];
                *(s32 *)&quad->x1 = sxy[1];
                *(s32 *)&quad->x2 = sxy[2];
                *(s32 *)&quad->x3 = sxy[3];
                count++;
                if (quad - g_eff_prims < EFF_PRIM_COUNT - 1) {
                    quad++;
                }
            }
        }
        g_eff_muzzle_flash_ages[slot_index]++;
        if (g_eff_muzzle_flash_ages[slot_index] > MUZZLE_FLASH_FRAMES) {
            g_eff_muzzle_flash_live &= ~(1 << slot_index);
        }
    }
    if (g_eff_next_prim != quad) {
        /* link them in, from the first */
        quads_end = quad;
        for (quad = g_eff_next_prim, k = 0; quad < quads_end; quad++, k++) {
            AddPrim(g_disp_ot + quad_ot_indices[k], quad);
        }
        g_eff_next_prim = quads_end;
        return 1;
    }
    if (!g_eff_muzzle_flash_live) {
        g_eff_stage[EFF_MUZZLE_FLASH] = 0;
        return 0;
    }
    return g_eff_muzzle_flash_live;
}

/* Takes the first free splash slot. With all twelve taken it writes a
 * thirteenth, over the start of the next table: the original's
 * behaviour. */
s32 eff_SpawnSplash(void) {
    s32 slot_index;
    s32 slot_bit;
    s32 *position;

    g_eff_stage[EFF_SPLASH] = 1;
    for (slot_index = 0; slot_index < SPLASH_COUNT; slot_index++) {
        slot_bit = 1 << slot_index;
        if (!(g_eff_splash_live & slot_bit)) {
            g_eff_splash_live |= slot_bit;
            break;
        }
    }
#if RB_FIX_SPLASH_POOL
    /* no place is free: the thirteenth splash is not made, where it was
     * written over the start of the next table */
    if (g_rb_settings.bugfixes && slot_index == SPLASH_COUNT) {
        return 1;
    }
#endif
    position = g_eff_position;
    g_eff_splash_positions[slot_index].x = position[0];
    g_eff_splash_positions[slot_index].y = position[1];
    g_eff_splash_positions[slot_index].z = position[2];
    g_eff_splash_ages[slot_index] = 0;
    return 1;
}

/* A splash is two quads standing on the water for 16 frames: a crown that
 * rises for ten frames and sinks for six, narrowing all the while, and from
 * the third frame drops that rise and fall back, swelling; they are black,
 * which adds nothing to what is behind them, until the ninth frame, and
 * fade out from there. */
s32 eff_DrawSplashes(void) {
    SplashWork *work;
    POLY_FT4 *prim;
    VECTOR *rotated;
    SVECTOR *relative;
    SVECTOR *zero;
    s32 *depth_cue_out;
    s32 *fade;
    s32 *ot_index;
    s16 slot_index;
    s16 i;
    s32 slot_bit;
    s32 scale;
    s32 red_green_level;

    work = &g_eff_work->splash;
    prim = g_eff_next_prim;
    rotated = &work->rotated_position;
    relative = &work->view_relative_position;
    zero = &work->zero_vector;
    depth_cue_out = &work->depth_cue;
    fade = &work->fade;
    ot_index = &work->ot_index;
    gte_SetRotMatrix(g_eff_view_matrix);
    zero->vz = 0;
    zero->vy = 0;
    zero->vx = 0;
    /* in thousandths */
    *g_eff_screen_dist = ReadGeomScreen() * 1000;
    *g_eff_base_width = SPLASH_WIDTH;
    *g_eff_base_height = SPLASH_HEIGHT;
    *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;
    *g_eff_tpage = *g_eff_tpage << 16;
    for (slot_index = 0; slot_index < SPLASH_COUNT; slot_index++) {
        slot_bit = 1 << slot_index;
        if (!(g_eff_splash_live & slot_bit)) {
            continue;
        }
        for (i = 0; i < 2; i++) {
            if (g_eff_splash_ages[slot_index] < SPLASH_FRAMES) {
                relative->vx =
                    g_eff_splash_positions[slot_index].x - g_eff_view_origin[0];
                relative->vz =
                    g_eff_splash_positions[slot_index].z - g_eff_view_origin[2];
                if (i != 0 &&
                    g_eff_splash_ages[slot_index] >= SPLASH_DROPS_FRAME) {
                    /* the drops go up as far as 26 and come down again, a
                     * sixteenth of a turn of the sine a frame */
                    relative->vy =
                        g_eff_splash_positions[slot_index].y -
                        (rsin((g_eff_splash_ages[slot_index] -
                               SPLASH_DROPS_FRAME)
                              << 8) *
                             329 / ONE -
                         69) /
                            10 -
                        g_eff_view_origin[1];
                } else {
                    relative->vy = g_eff_splash_positions[slot_index].y -
                                   g_eff_view_origin[1];
                }
                ApplyRotMatrix(relative, rotated);
                /* no matrix: only the translation is read (see eff.h) */
                SetTransMatrix((MATRIX *)((u8 *)rotated - 0x14));
                RotTransPers(
                    zero, g_eff_screen_xy, depth_cue_out, g_eff_rtp_flag);
                gte_stsz(g_eff_depth);
                if (i == 0) {
                    g_eff_tex = g_eff_splash_crown_tex;
                } else if (
                    g_eff_splash_ages[slot_index] < SPLASH_DROPS_LATE_FRAME) {
                    g_eff_tex = g_eff_splash_drops_tex;
                } else {
                    g_eff_tex = g_eff_splash_drops_late_tex;
                }
                *g_eff_clut = TEX_CLUT(g_eff_tex) << 16;
                *g_eff_u0 = g_eff_tex->u;
                *g_eff_v0 = g_eff_tex->v;
                *g_eff_u1 = g_eff_tex->u + (SPLASH_TEX_SIZE - 1);
                *g_eff_v1 = g_eff_tex->v + (SPLASH_TEX_SIZE - 1);
                *g_eff_uv0 = *g_eff_u0 + (*g_eff_v0 << 8);
                *g_eff_uv1 = *g_eff_u1 + (*g_eff_v0 << 8);
                *g_eff_uv2 = *g_eff_u0 + (*g_eff_v1 << 8);
                *g_eff_uv3 = *g_eff_u1 + (*g_eff_v1 << 8);
                *ot_index = render_DepthToOtShifted(*g_eff_depth, 0);
                /* not drawn nearer than a sixteenth of the projection
                 * distance */
                if (*ot_index < EFF_OT_END &&
                    *g_eff_screen_dist / 1000 >> 4 < *ot_index) {
                    *g_eff_scale = *g_eff_screen_dist / *g_eff_depth;
                    *g_eff_right = *g_eff_base_width * *g_eff_scale >
                                           SPLASH_LEAST_SIZE << 8
                                       ? *g_eff_base_width * *g_eff_scale >> 8
                                       : SPLASH_LEAST_SIZE;
                    *g_eff_bottom = *g_eff_base_height * *g_eff_scale >
                                            SPLASH_LEAST_SIZE << 8
                                        ? *g_eff_base_height * *g_eff_scale >> 8
                                        : SPLASH_LEAST_SIZE;
                    if (i == 0) {
                        if (g_eff_splash_ages[slot_index] <
                            SPLASH_CROWN_NARROW_FRAMES) {
                            /* the crown's width: from all of it to seven
                             * tenths, a quarter turn of the cosine */
                            *g_eff_right =
                                *g_eff_right *
                                (rcos((g_eff_splash_ages[slot_index] << 10) /
                                      SPLASH_CROWN_NARROW_FRAMES) *
                                     3 / ONE +
                                 7) /
                                10;
                        } else if (
                            g_eff_splash_ages[slot_index] < SPLASH_FRAMES) {
                            /* and from seven tenths to nothing over the
                             * rest */
                            *g_eff_right =
                                *g_eff_right *
                                (rcos(((g_eff_splash_ages[slot_index] -
                                        SPLASH_CROWN_NARROW_FRAMES)
                                       << 10) /
                                      (SPLASH_FRAMES -
                                       SPLASH_CROWN_NARROW_FRAMES)) *
                                 7 / ONE) /
                                10;
                        }
                        if (g_eff_splash_ages[slot_index] <
                            SPLASH_CROWN_RISE_FRAMES) {
                            /* its height: up to 85 hundredths, a quarter
                             * turn of the sine */
                            *g_eff_bottom =
                                *g_eff_bottom *
                                (rsin((g_eff_splash_ages[slot_index] << 10) /
                                      SPLASH_CROWN_RISE_FRAMES) *
                                 85 / ONE) /
                                100;
                        } else if (
                            g_eff_splash_ages[slot_index] < SPLASH_FRAMES) {
                            /* and down again over the rest */
                            *g_eff_bottom =
                                *g_eff_bottom *
                                (rcos(((g_eff_splash_ages[slot_index] -
                                        SPLASH_CROWN_RISE_FRAMES)
                                       << 10) /
                                      (SPLASH_FRAMES -
                                       SPLASH_CROWN_RISE_FRAMES)) *
                                 85 / ONE) /
                                100;
                        }
                        if (g_game_settings.grayscale) {
                            *(s32 *)&prim->r0 = EFF_RGB_PLAIN;
                        } else {
                            /* pale blue */
                            *(s32 *)&prim->r0 = EFF_RGB(0x80, 0x80, 0xFF);
                        }
                    } else {
                        /* the drops' size: in tenths, up to twelve over
                         * twelve frames of the sine and down from there */
                        scale =
                            rsin((g_eff_splash_ages[slot_index] << 10) / 12) *
                            12 / ONE;
                        /* in tenths */
                        *g_eff_right = *g_eff_right * scale / 10;
                        /* in tenths */
                        *g_eff_bottom = *g_eff_bottom * scale / 10;
                        if (g_eff_splash_ages[slot_index] >=
                            SPLASH_DROPS_FADE_FRAME) {
                            /* fade out over the last frames: the colour is
                             * what is left of five frames, a blue whose red
                             * and green are half as bright. It begins six
                             * frames from the end, when the blue is more
                             * than a byte holds and comes out dim, and the
                             * red and green brighter than the texture's
                             * own: the original's behaviour. */
                            *fade = g_eff_splash_ages[slot_index] -
                                    (SPLASH_FRAMES - 1);
#if RB_FIX_SPLASH_FADE
                            /* the fade's first frame is at the full
                             * colour, five frames' worth: six made a
                             * blue that did not fit its byte */
                            if (g_rb_settings.bugfixes && *fade < -5) {
                                *fade = -5; /* five frames from the end */
                            }
#endif
                            /* 128 at five frames from the end */
                            red_green_level = (*fade * -128 / 5) & 0xFF;
                            /* 255 at five frames from the end */
                            *(s32 *)&prim->r0 =
                                red_green_level + (red_green_level << 8) +
                                ((*fade * -255 / 5 & 0xFF) << 16);
                        } else {
                            *(s32 *)&prim->r0 = 0;
                        }
                    }
                    setlen(prim, FT4_LEN);
                    prim->code = FT4_CODE | FT4_SEMI_TRANS;
                    *g_eff_right = *g_eff_right >> 1;
                    *g_eff_left = -*g_eff_right;
                    /* the quad stands on its lower edge, a little short of
                     * its height */
                    *g_eff_top = *g_eff_bottom * 60 / 64;
                    *g_eff_bottom = 0;
                    *(s32 *)&prim->x0 =
                        *g_eff_screen_xy + *g_eff_left - (*g_eff_top << 16);
                    *(s32 *)&prim->x1 =
                        *g_eff_screen_xy + *g_eff_right - (*g_eff_top << 16);
                    *(s32 *)&prim->x2 =
                        *g_eff_screen_xy + *g_eff_left + (*g_eff_bottom << 16);
                    *(s32 *)&prim->x3 =
                        *g_eff_screen_xy + *g_eff_right + (*g_eff_bottom << 16);
                    *(s32 *)&prim->u0 = *g_eff_uv0 + *g_eff_clut;
                    *(s32 *)&prim->u1 = *g_eff_uv1 + *g_eff_tpage;
                    *(u16 *)&prim->u2 = *g_eff_uv2;
                    *(u16 *)&prim->u3 = *g_eff_uv3;
                    g_eff_ot_entry = g_disp_ot + *ot_index;
                    g_eff_prim_tag = &prim->tag;
                    EFF_ADD_PRIM();
                    if (prim - g_eff_prims < EFF_PRIM_COUNT - 1) {
                        prim++;
                    }
                }
            } else {
                g_eff_splash_live &= 0xFFFF - (1 << slot_index);
            }
        }
        g_eff_splash_ages[slot_index]++;
    }
    g_eff_next_prim = prim;
    return g_eff_splash_live != 0;
}

/* The footprints' handlers, a pair for each fighter: the first notes the
 * texture the request asks for and places the print, the second draws the
 * fighter's prints. */
u8 eff_SpawnFootprint0(void) {
    g_eff_stage[EFF_FOOTPRINTS_0] = 1;
    g_eff_footprint_shapes[0] =
        (g_eff_request->effect.word >> EFF_FOOTPRINT_SHAPE_SHIFT) & 3;
    return eff_PlaceFootprint(0);
}

u8 eff_SpawnFootprint1(void) {
    g_eff_stage[EFF_FOOTPRINTS_1] = 1;
    g_eff_footprint_shapes[1] =
        (g_eff_request->effect.word >> EFF_FOOTPRINT_SHAPE_SHIFT) & 3;
    return eff_PlaceFootprint(1);
}

u8 eff_DrawFootprints0(void) {
    *g_eff_footprint_shape = g_eff_footprint_shapes[0];
    return eff_DrawFootprints(0);
}

u8 eff_DrawFootprints1(void) {
    *g_eff_footprint_shape = g_eff_footprint_shapes[1];
    return eff_DrawFootprints(1);
}

/* Records a footprint for `fighter` where the request says. Up to ten are
 * kept; after that each new one replaces the oldest in turn. */
static u8 eff_PlaceFootprint(s32 fighter) {
    s32 slot_bit;
    s32 slot_index;

    if (g_eff_footprint_counts[fighter] < FOOTPRINT_COUNT) {
        g_eff_footprint_counts[fighter]++;
        for (slot_index = 0; slot_index < g_eff_footprint_counts[fighter];
             slot_index++) {
            if (!(g_eff_footprint_live[fighter] &
                  (slot_bit = 1 << slot_index))) {
                g_eff_footprint_live[fighter] |= slot_bit;
                g_eff_footprint_angles[fighter][slot_index].vy =
                    g_eff_direction[1];
                g_eff_footprint_angles[fighter][slot_index].vx =
                    g_eff_footprint_angles[fighter][slot_index].vz = 0;
                g_eff_footprint_positions[fighter][slot_index].x =
                    g_eff_position[0];
                g_eff_footprint_positions[fighter][slot_index].y =
                    g_eff_position[1];
                g_eff_footprint_positions[fighter][slot_index].z =
                    g_eff_position[2];
                break;
            }
        }
    } else {
        g_eff_footprint_counts[fighter] =
            (g_eff_footprint_counts[fighter] + 1) % FOOTPRINT_COUNT +
            FOOTPRINT_COUNT;
        g_eff_footprint_angles[fighter][g_eff_footprint_counts[fighter] -
                                        FOOTPRINT_COUNT]
            .vy = g_eff_direction[1];
        g_eff_footprint_angles[fighter][g_eff_footprint_counts[fighter] -
                                        FOOTPRINT_COUNT]
            .vx =
            g_eff_footprint_angles[fighter][g_eff_footprint_counts[fighter] -
                                            FOOTPRINT_COUNT]
                .vz = 0;
        g_eff_footprint_positions[fighter][g_eff_footprint_counts[fighter] -
                                           FOOTPRINT_COUNT]
            .x = g_eff_position[0];
        g_eff_footprint_positions[fighter][g_eff_footprint_counts[fighter] -
                                           FOOTPRINT_COUNT]
            .y = g_eff_position[1];
        g_eff_footprint_positions[fighter][g_eff_footprint_counts[fighter] -
                                           FOOTPRINT_COUNT]
            .z = g_eff_position[2];
    }
    return 1;
}

/* Draws `fighter`'s footprints, each a quad lying on the ground, turned the
 * way the foot pointed. They do not fade. */
static s32 eff_DrawFootprints(s32 fighter) {
    s32 count;
    MATRIX *matrices;
    MATRIX *composite;
    s32 *sxy;
    u16 *quad_ot_indices;
    u16 *made;
    POLY_FT4 *quad;
    POLY_FT4 *quads_end;
    s32 i;
    s32 k;
    s32 bit;

    /* FAKE: the work area, held in the quad pointer */
    quad = (POLY_FT4 *)g_eff_work;
    matrices = ((FootprintWork *)quad)->matrices;
    composite = &((FootprintWork *)quad)->composite;
    sxy = ((FootprintWork *)quad)->sxy;
    quad_ot_indices = ((FootprintWork *)quad)->ot_indices;
    made = &((FootprintWork *)quad)->made;
    if (g_eff_footprint_counts[fighter] < FOOTPRINT_COUNT) {
        count = g_eff_footprint_counts[fighter];
    } else {
        count = FOOTPRINT_COUNT;
    }
    eff_BuildFootprintMatrices(&count, matrices, fighter);
    g_eff_tex = &g_eff_footprint_texs[*g_eff_footprint_shape];
    quad = g_eff_next_prim;
    *g_eff_tpage = EFF_PAGE_LEFT;
    *g_eff_clut = TEX_CLUT(g_eff_tex) << 16;
    *g_eff_tpage = *g_eff_tpage << 16;
    *g_eff_u0 = g_eff_tex->u;
    *g_eff_v0 = g_eff_tex->v;
    *g_eff_u1 = g_eff_tex->u + (FOOTPRINT_TEX_WIDTH - 1);
    *g_eff_v1 = g_eff_tex->v + (FOOTPRINT_TEX_HEIGHT - 1);
    *g_eff_uv0 = *g_eff_u0 + (*g_eff_v0 << 8);
    *g_eff_uv1 = *g_eff_u1 + (*g_eff_v0 << 8);
    *g_eff_uv2 = *g_eff_u0 + (*g_eff_v1 << 8);
    *g_eff_uv3 = *g_eff_u1 + (*g_eff_v1 << 8);
    *made = 0;
    *g_eff_screen_dist = ReadGeomScreen();
    for (i = 0; i < count; i++) {
        /* FAKE: `bit` is not used again */
        if (!(g_eff_footprint_live[fighter] & (bit = 1 << i))) {
            continue;
        }
        setlen(quad, FT4_LEN);
        quad->code = FT4_CODE | FT4_SEMI_TRANS | FT4_RAW_TEX;
        *(s32 *)&quad->u0 = *g_eff_uv0 + *g_eff_clut;
        *(s32 *)&quad->u1 = *g_eff_uv1 + *g_eff_tpage;
        *(u16 *)&quad->u2 = *g_eff_uv2;
        *(u16 *)&quad->u3 = *g_eff_uv3;
        matrices[i].t[0] =
            g_eff_footprint_positions[fighter][i].x - g_eff_view_origin[0];
        matrices[i].t[1] =
            g_eff_footprint_positions[fighter][i].y - g_eff_view_origin[1];
        matrices[i].t[2] =
            g_eff_footprint_positions[fighter][i].z - g_eff_view_origin[2];
        gte_CompMatrix(g_eff_view_matrix, &matrices[i], composite);
        gte_SetRotMatrix(composite);
        gte_SetTransMatrix(composite);
        gte_ldv3(&g_eff_footprint_vertex_0, &g_eff_footprint_vertex_1,
                 &g_eff_footprint_vertex_2);
        gte_rtpt();
        gte_stsxy3(sxy, sxy + 1, sxy + 2);
        gte_stsz(g_eff_depth);
        if (*g_eff_depth <= 0) {
            continue;
        }
        *g_eff_depth =
            render_DepthToOtShifted(*g_eff_depth - FOOTPRINT_DEPTH_BIAS, 0);
        if (*g_eff_depth >= EFF_OT_END) {
            continue;
        }
        /* nor nearer than a sixteenth of the projection distance */
        if ((*g_eff_screen_dist >> 4) >= *g_eff_depth) {
            continue;
        }
        quad_ot_indices[(*made)++] = *g_eff_depth;
        gte_ldv0(&g_eff_footprint_vertex_3);
        gte_rtps();
        gte_stsxy(sxy + 3);
        *(s32 *)&quad->x0 = sxy[0];
        *(s32 *)&quad->x1 = sxy[1];
        *(s32 *)&quad->x2 = sxy[2];
        *(s32 *)&quad->x3 = sxy[3];
        if (quad - g_eff_prims < EFF_PRIM_COUNT - 1) {
            quad++;
        }
    }
    /* link them in, from the first */
    quads_end = quad;
    for (quad = g_eff_next_prim, k = 0; quad < quads_end; quad++, k++) {
        g_eff_prim_tag = &quad->tag;
        g_eff_ot_entry = g_disp_ot + quad_ot_indices[k];
        EFF_ADD_PRIM();
    }
    g_eff_next_prim = quads_end;
    return 1;
}

/* The rotation of each footprint in use. */
static void eff_BuildFootprintMatrices(
    s32 *count, MATRIX *matrices, s32 fighter) {
    s32 i;
    s32 mask;

    for (i = 0; i < *count; i++) {
        /* FAKE: mask = 1 << i */
        mask = 1;
        mask <<= i;
        if (g_eff_footprint_live[fighter] & mask) {
            RotMatrix(&g_eff_footprint_angles[fighter][i], &matrices[i]);
        }
    }
}

/* Starts up to four puffs of dust around the request's place, one in each
 * group of four slots that has a slot free, each at a random stage of its
 * life. */
s32 eff_SpawnDust(void) {
    s32 group;
    s32 place; /* in the group */
    s32 slot_index;
    s32 slot_bit;

    g_eff_stage[EFF_DUST] = 1;
    for (group = 0; group < DUST_COUNT - 1; group += DUST_GROUP) {
        for (place = 0; place < DUST_GROUP; place++) {
            slot_index = group + place;
            slot_bit = 1 << slot_index;
            if (!(g_eff_dust_live & slot_bit)) {
                g_eff_dust_positions[slot_index].x =
                    g_eff_position[0] + (rand() & (2 * DUST_SCATTER + 1)) -
                    DUST_SCATTER;
                g_eff_dust_positions[slot_index].y =
                    g_eff_position[1] + (rand() & (2 * DUST_SCATTER + 1)) -
                    DUST_SCATTER;
                g_eff_dust_positions[slot_index].z =
                    g_eff_position[2] + (rand() & (2 * DUST_SCATTER + 1)) -
                    DUST_SCATTER;
                /* at one of its first eight ages: in its first frame or
                 * its second */
                g_eff_dust_ages[slot_index] = rand() & 7;
                g_eff_dust_live |= slot_bit;
                break;
            }
        }
    }
    return 1;
}

/* A puff of dust is one quad facing the view that plays seven frames, four
 * of its age a frame, and swells as it goes. Its age goes up twice a call
 * while it is drawn and once while it is out of view. */
s32 eff_DrawDust(void) {
    DustWork *work;
    u32 *ot_index;
    s16 *width;
    s16 *height;
    s32 *frame;
    VECTOR *rotated;
    VECTOR *relative;
    SVECTOR *zero;
    s32 *depth_cue_out;
    POLY_FT4 **quads_end;
    POLY_FT4 *prim;
    u32 *ot_index_out;
    s16 slot_index;
    s32 slot_bit;

    work = &g_eff_work->dust;
    ot_index = work->ot_indices;
    width = &work->width;
    height = &work->height;
    frame = &work->frame;
    rotated = &work->rotated_position;
    relative = &work->view_relative_position;
    zero = &work->zero_vector;
    depth_cue_out = &work->depth_cue;
    quads_end = &work->quads_end;
    prim = g_eff_next_prim;
    gte_SetRotMatrix(g_eff_view_matrix);
    zero->vx = zero->vy = zero->vz = 0;
    /* in thousandths */
    *g_eff_screen_dist = ReadGeomScreen() * 1000;
    *g_eff_base_width = DUST_WIDTH;
    *g_eff_base_height = DUST_HEIGHT;
    *g_eff_tpage = EFF_PAGE_LEFT;
    for (slot_index = 0, ot_index_out = ot_index; slot_index < DUST_COUNT;
         slot_index++) {
        slot_bit = 1 << slot_index;
        if (!(g_eff_dust_live & slot_bit)) {
            continue;
        }
        g_eff_dust_ages[slot_index]++;
        *frame = g_eff_dust_ages[slot_index] / 4;
        if (*frame < DUST_FRAMES) {
            relative->vx =
                g_eff_dust_positions[slot_index].x - g_eff_view_origin[0];
            relative->vy =
                g_eff_dust_positions[slot_index].y - g_eff_view_origin[1];
            relative->vz =
                g_eff_dust_positions[slot_index].z - g_eff_view_origin[2];
            ApplyRotMatrixLV(relative, rotated);
            /* no matrix: only the translation is read (see eff.h) */
            SetTransMatrix((MATRIX *)((u8 *)rotated - 0x14));
            RotTransPers(zero, g_eff_screen_xy, depth_cue_out, g_eff_rtp_flag);
            gte_stsz(g_eff_depth);
            g_eff_tex = &g_eff_dust_texs[*frame];
            *g_eff_clut = (u16)TEX_CLUT(g_eff_tex);
            *g_eff_u0 = g_eff_tex->u;
            *g_eff_v0 = g_eff_tex->v;
            *g_eff_u1 = g_eff_tex->u + (DUST_TEX_WIDTH - 1);
            *g_eff_v1 = g_eff_tex->v + (DUST_TEX_HEIGHT - 1);
            *ot_index_out = render_DepthToOtShifted(*g_eff_depth, 0);
            if (*ot_index_out >= EFF_OT_END) {
                continue;
            }
            /* nor nearer than a sixteenth of the projection distance */
            if ((*g_eff_screen_dist / 1000 >> 4) >= *ot_index_out) {
                continue;
            }
            *g_eff_scale = *g_eff_screen_dist / *g_eff_depth;
            *width = *g_eff_base_width * *g_eff_scale > DUST_LEAST_SIZE << 8
                         ? (*g_eff_base_width * *g_eff_scale) >> 8
                         : DUST_LEAST_SIZE;
            *height = *g_eff_base_height * *g_eff_scale > DUST_LEAST_SIZE << 8
                          ? (*g_eff_base_height * *g_eff_scale) >> 8
                          : DUST_LEAST_SIZE;
            /* it swells: wider by a thirty-second for every age, taller by
             * a sixty-fourth */
            *width += (*width * g_eff_dust_ages[slot_index]) >> 5;
            *height += (*height * (g_eff_dust_ages[slot_index] << 2)) >> 8;
            SetPolyFT4(prim);
            prim->tpage = *g_eff_tpage;
            prim->clut = *g_eff_clut;
            if (g_game_settings.grayscale) {
                setRGB0(prim, 0x80, 0x80, 0x80); /* grey */
            } else {
                setRGB0(prim, 0xA0, 0x8C, 0x50); /* the brown of dust */
            }
            prim->x0 = *g_eff_screen_xy - *width / 2;
            prim->y0 = (*g_eff_screen_xy >> 16) - DUST_ABOVE(*height);
            prim->x1 = *g_eff_screen_xy + *width / 2;
            prim->y1 = (*g_eff_screen_xy >> 16) - DUST_ABOVE(*height);
            prim->x2 = *g_eff_screen_xy - *width / 2;
            prim->y2 = (*g_eff_screen_xy >> 16) + DUST_BELOW(*height);
            prim->x3 = *g_eff_screen_xy + *width / 2;
            prim->y3 = (*g_eff_screen_xy >> 16) + DUST_BELOW(*height);
            prim->u0 = *g_eff_u0;
            prim->v0 = *g_eff_v0;
            prim->u1 = *g_eff_u1;
            prim->v1 = *g_eff_v0;
            prim->u2 = *g_eff_u0;
            prim->v2 = *g_eff_v1;
            prim->u3 = *g_eff_u1;
            prim->v3 = *g_eff_v1;
            SetShadeTex(prim, 0);
            SetSemiTrans(prim, 1);
            if (prim - g_eff_prims < EFF_PRIM_COUNT - 1) {
                prim++;
                ot_index_out++;
            }
            g_eff_dust_ages[slot_index]++;
        } else {
            g_eff_dust_live &= 0xFFFF - slot_bit;
        }
    }
    if (!g_eff_dust_live) {
        return 0;
    }
    *quads_end = prim;
    for (prim = g_eff_next_prim; prim < *quads_end; prim++, ot_index++) {
        g_eff_ot_entry = g_disp_ot + *ot_index;
        g_eff_prim_tag = &prim->tag;
        EFF_ADD_PRIM();
    }
    g_eff_next_prim = *quads_end;
    return 1;
}

/* The flashes' first handlers: each notes where its flash is and starts its
 * timer. */
void eff_SpawnLockFlash(void) {
    g_eff_flash_positions[FLASH_LOCK].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_LOCK].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_LOCK].z = g_eff_position[2];
    g_eff_stage[EFF_LOCK_FLASH] = 1;
    g_eff_flash_timers[FLASH_LOCK] = 0;
}

void eff_SpawnClashSpark0(void) {
    g_eff_flash_positions[FLASH_CLASH_SPARK_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_CLASH_SPARK_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_CLASH_SPARK_0].z = g_eff_position[2];
    g_eff_stage[EFF_CLASH_SPARK_0] = 1;
    g_eff_flash_timers[FLASH_CLASH_SPARK_0] = 0;
}

void eff_SpawnClashSpark1(void) {
    g_eff_flash_positions[FLASH_CLASH_SPARK_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_CLASH_SPARK_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_CLASH_SPARK_1].z = g_eff_position[2];
    g_eff_stage[EFF_CLASH_SPARK_1] = 1;
    g_eff_flash_timers[FLASH_CLASH_SPARK_1] = 0;
}

s32 eff_SpawnHitFlash0(void) {
    g_eff_flash_positions[FLASH_HIT_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_HIT_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_HIT_0].z = g_eff_position[2];
    g_eff_stage[EFF_HIT_FLASH_0] = 1;
    g_eff_flash_timers[FLASH_HIT_0] = HIT_FLASH_START;
    return 1;
}

s32 eff_SpawnHitFlash1(void) {
    g_eff_flash_positions[FLASH_HIT_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_HIT_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_HIT_1].z = g_eff_position[2];
    g_eff_stage[EFF_HIT_FLASH_1] = 1;
    g_eff_flash_timers[FLASH_HIT_1] = HIT_FLASH_START;
    return 1;
}

s32 eff_SpawnLockFlashRated(void) {
    g_eff_flash_positions[FLASH_LOCK_RATED].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_LOCK_RATED].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_LOCK_RATED].z = g_eff_position[2];
    g_eff_stage[EFF_LOCK_FLASH_RATED] = 1;
    g_eff_flash_timers[FLASH_LOCK_RATED] = 0;
    g_eff_lock_flash_rate = (g_eff_request->effect.word >> EFF_RATE_SHIFT) & 3;
    return 1;
}

void eff_SpawnClashStreak0(void) {
    g_eff_flash_positions[FLASH_CLASH_STREAK_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_CLASH_STREAK_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_CLASH_STREAK_0].z = g_eff_position[2];
    g_eff_stage[EFF_CLASH_STREAK_0] = 1;
    g_eff_flash_timers[FLASH_CLASH_STREAK_0] = 0;
}

void eff_SpawnClashStreak1(void) {
    g_eff_flash_positions[FLASH_CLASH_STREAK_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_CLASH_STREAK_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_CLASH_STREAK_1].z = g_eff_position[2];
    g_eff_stage[EFF_CLASH_STREAK_1] = 1;
    g_eff_flash_timers[FLASH_CLASH_STREAK_1] = 0;
}

void eff_SpawnMatchedStreak0(void) {
    g_eff_flash_positions[FLASH_MATCHED_STREAK_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_MATCHED_STREAK_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_MATCHED_STREAK_0].z = g_eff_position[2];
    g_eff_stage[EFF_MATCHED_STREAK_0] = 1;
    g_eff_flash_timers[FLASH_MATCHED_STREAK_0] = 0;
}

void eff_SpawnMatchedStreak1(void) {
    g_eff_flash_positions[FLASH_MATCHED_STREAK_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_MATCHED_STREAK_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_MATCHED_STREAK_1].z = g_eff_position[2];
    g_eff_stage[EFF_MATCHED_STREAK_1] = 1;
    g_eff_flash_timers[FLASH_MATCHED_STREAK_1] = 0;
}

void eff_SpawnBluntHit0(void) {
    g_eff_flash_positions[FLASH_BLUNT_HIT_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_BLUNT_HIT_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_BLUNT_HIT_0].z = g_eff_position[2];
    g_eff_flash_timers[FLASH_BLUNT_HIT_0] = 0;
    g_eff_flash_positions[FLASH_BLUNT_HIT_0_AGAIN].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_BLUNT_HIT_0_AGAIN].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_BLUNT_HIT_0_AGAIN].z = g_eff_position[2];
    g_eff_stage[EFF_BLUNT_HIT_0] = 1;
    g_eff_flash_timers[FLASH_BLUNT_HIT_0_AGAIN] = 0;
}

void eff_SpawnBluntHit1(void) {
    g_eff_flash_positions[FLASH_BLUNT_HIT_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_BLUNT_HIT_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_BLUNT_HIT_1].z = g_eff_position[2];
    g_eff_flash_timers[FLASH_BLUNT_HIT_1] = 0;
    g_eff_flash_positions[FLASH_BLUNT_HIT_1_AGAIN].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_BLUNT_HIT_1_AGAIN].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_BLUNT_HIT_1_AGAIN].z = g_eff_position[2];
    g_eff_stage[EFF_BLUNT_HIT_1] = 1;
    g_eff_flash_timers[FLASH_BLUNT_HIT_1_AGAIN] = 0;
}

void eff_SpawnArmorSpark0(void) {
    g_eff_flash_positions[FLASH_ARMOR_SPARK_0].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_ARMOR_SPARK_0].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_ARMOR_SPARK_0].z = g_eff_position[2];
    g_eff_stage[EFF_ARMOR_SPARK_0] = 1;
    g_eff_flash_timers[FLASH_ARMOR_SPARK_0] = 0;
}

void eff_SpawnArmorSpark1(void) {
    g_eff_flash_positions[FLASH_ARMOR_SPARK_1].x = g_eff_position[0];
    g_eff_flash_positions[FLASH_ARMOR_SPARK_1].y = g_eff_position[1];
    g_eff_flash_positions[FLASH_ARMOR_SPARK_1].z = g_eff_position[2];
    g_eff_stage[EFF_ARMOR_SPARK_1] = 1;
    g_eff_flash_timers[FLASH_ARMOR_SPARK_1] = 0;
}

/* The flashes' second handlers: each draws its flash and moves its timer
 * on, and says the effect is over when the timer passes its end. */
u8 eff_DrawLockFlash(void) {
    u8 alive = eff_DrawFlash(FLASH_LOCK);

    g_eff_flash_timers[FLASH_LOCK] += LOCK_FLASH_STEP;
    if (g_eff_flash_timers[FLASH_LOCK] <= LOCK_FLASH_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawClashSpark0(void) {
    u8 alive = eff_DrawFlash(FLASH_CLASH_SPARK_0);

    g_eff_flash_timers[FLASH_CLASH_SPARK_0] += SPARK_STEP;
    if (g_eff_flash_timers[FLASH_CLASH_SPARK_0] <= SPARK_LIFE) {
        return alive;
    }
    return 0;
}

u8 eff_DrawClashSpark1(void) {
    u8 alive = eff_DrawFlash(FLASH_CLASH_SPARK_1);

    g_eff_flash_timers[FLASH_CLASH_SPARK_1] += SPARK_STEP;
    if (g_eff_flash_timers[FLASH_CLASH_SPARK_1] <= SPARK_LIFE) {
        return alive;
    }
    return 0;
}

u8 eff_DrawHitFlash0(void) {
    u8 alive = eff_DrawFlash(FLASH_HIT_0);

    g_eff_flash_timers[FLASH_HIT_0] += HIT_FLASH_STEP;
    if (g_eff_flash_timers[FLASH_HIT_0] <= HIT_FLASH_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawHitFlash1(void) {
    u8 alive = eff_DrawFlash(FLASH_HIT_1);

    g_eff_flash_timers[FLASH_HIT_1] += HIT_FLASH_STEP;
    if (g_eff_flash_timers[FLASH_HIT_1] <= HIT_FLASH_END) {
        return alive;
    }
    return 0;
}

/* The rated lock flash goes a step, two or three a frame. */
u8 eff_DrawLockFlashRated(void) {
    u8 alive;

    *g_eff_flash_rate = g_eff_lock_flash_rate;
    alive = eff_DrawFlash(FLASH_LOCK_RATED);
    switch ((u32)*g_eff_flash_rate) {
    case LOCK_FLASH_SLOW:
        g_eff_flash_timers[FLASH_LOCK_RATED] += LOCK_FLASH_STEP;
        break;
    case LOCK_FLASH_MEDIUM:
        g_eff_flash_timers[FLASH_LOCK_RATED] += 2 * LOCK_FLASH_STEP;
        break;
    case LOCK_FLASH_FAST:
        g_eff_flash_timers[FLASH_LOCK_RATED] += 3 * LOCK_FLASH_STEP;
        break;
    }
    if (g_eff_flash_timers[FLASH_LOCK_RATED] <= LOCK_FLASH_RATED_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawClashStreak0(void) {
    u8 alive = eff_DrawFlash(FLASH_CLASH_STREAK_0);

    g_eff_flash_timers[FLASH_CLASH_STREAK_0] += STREAK_STEP;
    if (g_eff_flash_timers[FLASH_CLASH_STREAK_0] <= STREAK_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawClashStreak1(void) {
    u8 alive = eff_DrawFlash(FLASH_CLASH_STREAK_1);

    g_eff_flash_timers[FLASH_CLASH_STREAK_1] += STREAK_STEP;
    if (g_eff_flash_timers[FLASH_CLASH_STREAK_1] <= STREAK_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawMatchedStreak0(void) {
    u8 alive = eff_DrawFlash(FLASH_MATCHED_STREAK_0);

    g_eff_flash_timers[FLASH_MATCHED_STREAK_0] += STREAK_STEP;
    if (g_eff_flash_timers[FLASH_MATCHED_STREAK_0] <= STREAK_END) {
        return alive;
    }
    return 0;
}

u8 eff_DrawMatchedStreak1(void) {
    u8 alive = eff_DrawFlash(FLASH_MATCHED_STREAK_1);

    g_eff_flash_timers[FLASH_MATCHED_STREAK_1] += STREAK_STEP;
    if (g_eff_flash_timers[FLASH_MATCHED_STREAK_1] <= STREAK_END) {
        return alive;
    }
    return 0;
}

/* The blunt hit is two flashes, one over the other. */
s32 eff_DrawBluntHit0(void) {
    s32 alive;

    eff_DrawFlash(FLASH_BLUNT_HIT_0);
    g_eff_flash_timers[FLASH_BLUNT_HIT_0]++;
    alive = eff_DrawFlash(FLASH_BLUNT_HIT_0_AGAIN);
    g_eff_flash_timers[FLASH_BLUNT_HIT_0_AGAIN]++;
    alive &= 0xFF;
    if (g_eff_flash_timers[FLASH_BLUNT_HIT_0_AGAIN] > BLUNT_HIT_FRAMES) {
        alive = 0;
    }
    return alive;
}

/* The same, written another way. */
s32 eff_DrawBluntHit1(void) {
    s32 alive;

    eff_DrawFlash(FLASH_BLUNT_HIT_1);
    g_eff_flash_timers[FLASH_BLUNT_HIT_1]++;
    alive = eff_DrawFlash(FLASH_BLUNT_HIT_1_AGAIN);
    g_eff_flash_timers[FLASH_BLUNT_HIT_1_AGAIN]++;
    if (g_eff_flash_timers[FLASH_BLUNT_HIT_1] <= BLUNT_HIT_FRAMES) {
        return alive & 0xFF;
    }
    return 0;
}

u8 eff_DrawArmorSpark0(void) {
    u8 alive = eff_DrawFlash(FLASH_ARMOR_SPARK_0);

    g_eff_flash_timers[FLASH_ARMOR_SPARK_0] += SPARK_STEP;
    if (g_eff_flash_timers[FLASH_ARMOR_SPARK_0] <= SPARK_LIFE) {
        return alive;
    }
    return 0;
}

u8 eff_DrawArmorSpark1(void) {
    u8 alive = eff_DrawFlash(FLASH_ARMOR_SPARK_1);

    g_eff_flash_timers[FLASH_ARMOR_SPARK_1] += SPARK_STEP;
    if (g_eff_flash_timers[FLASH_ARMOR_SPARK_1] <= SPARK_LIFE) {
        return alive;
    }
    return 0;
}

/* A matched streak's colour: green, fading out once the streak is late,
 * and blue fading from the start. */
static inline void eff_SetFadingPairColor(
    POLY_FT4 *prim, s16 *timers, s32 flash) {
    s16 *timer = timers + flash;

    if (*timer > STREAK_LATE) {
        prim->r0 = 0;
        prim->g0 = (STREAK_END - *timer) * 0xFF / (STREAK_END - STREAK_LATE);
    } else {
        prim->r0 = 0;
        prim->g0 = 0xFF;
    }
    prim->b0 = 0x7F - *timer / 2; /* from half, to nothing at the end */
}

/* Draws one flash: a single quad at the flash's place, facing the view.
 * What texture and colour it has, how its size changes with its timer and
 * how its corners are laid out all go by which flash it is:
 * - the lock flashes: a blue star that pulses wide and flat;
 * - clash and armour sparks: a star that spins as it swells and shrinks;
 * - the hit flash: a glow that swells, red turning yellow;
 * - the streaks: a streak that widens and shortens (in the first-person
 *   view, a square burst instead, mirrored for fighter 0);
 *   a clash streak is followed by its glow, drawn by going round again;
 * - the blunt hit: an orange burst that fades to a star. */
static u8 eff_DrawFlash(s32 flash) {
    FlashWork *work;
    POLY_FT4 *prim;
    s32 *depth_cue_out;
    VECTOR *corners;
    VECTOR *relative;
    SVECTOR *vertex;
    s16 *width;
    s16 *height;
    MATRIX *spin;
    s16 *tex_width;
    s16 *tex_height;
    VECTOR *corner;
    s32 brightness;
    s32 w;
    s32 width_scale;
    s32 height_scale;
    s32 h;
    s32 vertex_index;

    work = &g_eff_work->flash;
    prim = g_eff_next_prim;
    depth_cue_out = &work->depth_cue;
    corners = work->corners;
    relative = &work->view_relative_position;
    vertex = &work->vertex;
    width = &work->width;
    height = &work->height;
    spin = &work->spin;
    tex_width = &work->tex_width;
    tex_height = &work->tex_height;

    gte_SetRotMatrix(g_eff_view_matrix);
    relative->vx = g_eff_flash_positions[flash].x - g_eff_view_origin[0];
    relative->vy = g_eff_flash_positions[flash].y - g_eff_view_origin[1];
    relative->vz = g_eff_flash_positions[flash].z - g_eff_view_origin[2];
    ApplyRotMatrixLV(relative, corners);
    gte_SetTransMatrix(work);
    vertex->vz = 0;
    vertex->vy = 0;
    vertex->vx = 0;
    gte_RotTransPers(
        vertex, g_eff_screen_xy, depth_cue_out, g_eff_rtp_flag, g_eff_depth);
    *g_eff_depth = *g_eff_depth ? *g_eff_depth << 2 : 1;

again:
    *g_eff_base_width = FLASH_SIZE;
    *g_eff_base_height = FLASH_SIZE;
    *tex_width = FLASH_TEX_SIZE;
    *tex_height = FLASH_TEX_SIZE;
    *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;
    switch (flash) {
    case FLASH_CLASH_SPARK_0:
    case FLASH_CLASH_SPARK_1:
        if (*g_eff_depth > SPARK_FAR_DEPTH) {
            g_eff_tex = g_eff_spark_far_tex;
        } else {
            g_eff_tex = g_eff_spark_near_tex;
        }
        setRGB0(prim, 0xFF, 0x80, 0x80); /* pale red */
        break;
    case FLASH_HIT_0:
    case FLASH_HIT_1:
        if (g_eff_flash_timers[flash] < HIT_FLASH_LATE) {
            g_eff_tex = g_eff_glow_tex;
        } else {
            g_eff_tex = g_eff_glow_late_tex;
        }
        /* red, turning yellow as the timer goes up */
        setRGB0(prim, 0xFF, g_eff_flash_timers[flash], 0x60);
        *g_eff_base_width = HIT_FLASH_SIZE;
        *g_eff_base_height = HIT_FLASH_SIZE;
        break;
    case FLASH_LOCK_RATED:
        /* the hit flash's timer, which never gets this far: the other
         * texture is never shown (the original's behaviour) */
        if (g_eff_flash_timers[FLASH_HIT_1] > ANGLE_TURN / 2) {
            g_eff_tex = g_eff_lock_flash_alt_tex;
        } else {
            g_eff_tex = g_eff_star_tex;
        }
        setRGB0(prim, 0x80, 0x80, 0xFF); /* pale blue */
        break;
    case FLASH_LOCK:
        g_eff_tex = g_eff_star_tex;
        setRGB0(prim, 0x80, 0x80, 0xFF); /* pale blue */
        break;
    case FLASH_MATCHED_STREAK_0:
    case FLASH_MATCHED_STREAK_1:
        if (g_game_settings.first_person &&
            g_sys_screen != SCREEN_REPLAY_UPDATE) {
            g_eff_tex = g_eff_streak_close_tex;
        } else {
            g_eff_tex = g_eff_streak_late_tex;
        }
        eff_SetFadingPairColor(prim, g_eff_flash_timers, flash);
        goto streak_size;
    case FLASH_CLASH_STREAK_0:
    case FLASH_CLASH_STREAK_1:
        if (g_eff_flash_timers[flash] < STREAK_LATE) {
            if (g_game_settings.first_person &&
                g_sys_screen != SCREEN_REPLAY_UPDATE) {
                g_eff_tex = g_eff_streak_close_tex;
            } else {
                g_eff_tex = g_eff_streak_tex;
            }
        } else if (g_game_settings.first_person &&
                   g_sys_screen != SCREEN_REPLAY_UPDATE) {
            g_eff_tex = g_eff_streak_close_late_tex;
        } else {
            g_eff_tex = g_eff_streak_late_tex;
        }
        /* cyan, its green going out as the timer goes up */
        setRGB0(prim, 0x10, ~g_eff_flash_timers[flash], 0xFF);
    streak_size:
        if (g_game_settings.first_person &&
            g_sys_screen != SCREEN_REPLAY_UPDATE) {
            *g_eff_base_width = STREAK_CLOSE_SIZE;
            *g_eff_base_height = STREAK_CLOSE_SIZE;
            *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_RIGHT;
        } else {
            *g_eff_base_width = STREAK_WIDTH;
            *g_eff_base_height = STREAK_HEIGHT;
            *tex_width = STREAK_TEX_WIDTH;
            *tex_height = STREAK_TEX_HEIGHT;
        }
        break;
    case FLASH_CLASH_GLOW_0:
    case FLASH_CLASH_GLOW_1:
        /* the glow goes by its streak's timer, two flashes back */
        if (g_eff_flash_timers[flash - 2] < STREAK_GLOW_LATE) {
            g_eff_tex = g_eff_glow_tex;
        } else {
            g_eff_tex = g_eff_glow_late_tex;
        }
        /* blue, whitening as the timer goes up */
        setRGB0(prim, g_eff_flash_timers[flash - 2] * 2 / 3,
                g_eff_flash_timers[flash - 2] / 2, 0xFF);
        *g_eff_base_width = CLASH_GLOW_SIZE;
        *g_eff_base_height = CLASH_GLOW_SIZE;
        break;
    case FLASH_BLUNT_HIT_0:
    case FLASH_BLUNT_HIT_1:
    case FLASH_BLUNT_HIT_0_AGAIN:
    case FLASH_BLUNT_HIT_1_AGAIN:
        setRGB0(prim, 0xC0, 0x70, 0x13); /* orange */
        *g_eff_base_width *= 2;
        *g_eff_base_height *= 2;
        if (g_eff_flash_timers[flash] >= BLUNT_HIT_STAR_FRAME) {
            g_eff_tex = g_eff_star_tex;
            brightness = BLUNT_HIT_FRAMES - g_eff_flash_timers[flash];
            /* the orange, by what is left of the star's three frames */
            setRGB0(prim, brightness << 6, brightness * 0x70 / 3,
                    brightness * 0x60 / 15);
            *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;
        } else {
            if (g_eff_flash_timers[flash] >= BLUNT_HIT_MID_FRAME) {
                g_eff_tex = g_eff_blunt_hit_mid_tex;
            } else {
                g_eff_tex = g_eff_blunt_hit_tex;
            }
            *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_RIGHT;
        }
        break;
    case FLASH_ARMOR_SPARK_0:
    case FLASH_ARMOR_SPARK_1:
        g_eff_tex = g_eff_star_tex;
        setRGB0(prim, 0xFF, 0x60, 0xFF); /* magenta */
        break;
    }
    *g_eff_clut = (u16)TEX_CLUT(g_eff_tex);
    *g_eff_u0 = g_eff_tex->u;
    *g_eff_v0 = g_eff_tex->v;
    *g_eff_u1 = *tex_width + g_eff_tex->u - 1;
    *g_eff_v1 = *tex_height + g_eff_tex->v - 1;
    /* in thousandths; the depth was made four times what it is */
    *g_eff_screen_dist = ReadGeomScreen() * 1000;
    *g_eff_scale = *g_eff_screen_dist / *g_eff_depth;
    /* one that would be under 2 across is 6 */
    *width = *g_eff_base_width * *g_eff_scale > 2 << 8
                 ? *g_eff_base_width * *g_eff_scale >> 8
                 : 6;
    /* and high */
    *height = *g_eff_base_height * *g_eff_scale > 2 << 8
                  ? *g_eff_base_height * *g_eff_scale >> 8
                  : 6;
    SetPolyFT4(prim);
    prim->tpage = *g_eff_tpage;
    prim->clut = *g_eff_clut;
    prim->u0 = *g_eff_u0;
    prim->v0 = *g_eff_v0;
    prim->u1 = *g_eff_u1;
    prim->v1 = *g_eff_v0;
    prim->u2 = *g_eff_u0;
    prim->v2 = *g_eff_v1;
    prim->u3 = *g_eff_u1;
    prim->v3 = *g_eff_v1;
    SetShadeTex(prim, 0);
    SetSemiTrans(prim, 1);
    switch (flash) {
    case FLASH_LOCK:
        /* the pulse: from sixteen times as wide to nothing and back in a
         * turn of the cosine */
        *width = (u16)*width * 8 +
                 (*width * rcos(g_eff_flash_timers[flash] & ANGLE_MASK) >> 9);
        /* and from nothing to four times as high and back, half a turn
         * out of step (all but) */
        *height =
            (u16)*height * 2 +
            (*height * rcos((g_eff_flash_timers[flash] + (ANGLE_TURN / 2 - 1)) &
                            ANGLE_MASK) >>
             11);
        prim->x0 = *g_eff_screen_xy - *width / 2;
        prim->y0 = (*g_eff_screen_xy >> 16) - *height / 2;
        prim->x1 = *g_eff_screen_xy + *width / 2;
        prim->y1 = (*g_eff_screen_xy >> 16) - *height / 2;
        prim->x2 = *g_eff_screen_xy - *width / 2;
        prim->y2 = (*g_eff_screen_xy >> 16) + *height / 2;
        prim->x3 = *g_eff_screen_xy + *width / 2;
        prim->y3 = (*g_eff_screen_xy >> 16) + *height / 2;
        break;
    case FLASH_CLASH_SPARK_0:
    case FLASH_CLASH_SPARK_1:
    case FLASH_ARMOR_SPARK_0:
    case FLASH_ARMOR_SPARK_1:
        /* its life is half a turn of the sine: the width goes from nothing
         * up to 25 halves, the sine begun a quarter turn back and raised
         * by one */
        width_scale = (rsin((g_eff_flash_timers[flash] << 11) / SPARK_LIFE -
                            ANGLE_TURN / 4) +
                       ONE) *
                      25;
        *width = (*width * width_scale >> 13) / 2;
        /* and the height up to 15 halves and down again */
        height_scale =
            rsin((g_eff_flash_timers[flash] << 11) / SPARK_LIFE) * 15;
        *height = (*height * height_scale >> 12) / 2;
        vertex->vy = 0;
        vertex->vx = 0;
        vertex->vz = g_eff_flash_timers[flash];
        RotMatrix(vertex, spin);
        SetRotMatrix(spin);
        for (vertex_index = 0, corner = corners; vertex_index < 4;
             vertex_index++, corner++) {
            w = *width;
            if (!(vertex_index & 1)) {
                vertex->vx = -w;
            } else {
                vertex->vx = w;
            }
            h = *height;
            if (!(vertex_index & 2)) {
                vertex->vy = -h;
            } else {
                vertex->vy = h;
            }
            ApplyRotMatrix(vertex, corner);
        }
        prim->x0 = *g_eff_screen_xy + corners[0].vx / 2;
        prim->y0 = (*g_eff_screen_xy >> 16) + corners[0].vy / 4;
        prim->x1 = *g_eff_screen_xy + corners[1].vx / 2;
        prim->y1 = (*g_eff_screen_xy >> 16) + corners[1].vy / 4;
        prim->x2 = *g_eff_screen_xy + corners[2].vx / 2;
        prim->y2 = (*g_eff_screen_xy >> 16) + corners[2].vy / 4;
        prim->x3 = *g_eff_screen_xy + corners[3].vx / 2;
        prim->y3 = (*g_eff_screen_xy >> 16) + corners[3].vy / 4;
        break;
    case FLASH_BLUNT_HIT_0:
    case FLASH_BLUNT_HIT_1:
    case FLASH_BLUNT_HIT_0_AGAIN:
    case FLASH_BLUNT_HIT_1_AGAIN:
        /* it swells to twice its size over its frames: a quarter turn of
         * the sine (all but) */
        *width = *width * rsin((g_eff_flash_timers[flash] *
                                (ANGLE_TURN / 4 - 1) / BLUNT_HIT_FRAMES) &
                               ANGLE_MASK) >>
                 11;
        *height = *height * rsin((g_eff_flash_timers[flash] *
                                  (ANGLE_TURN / 4 - 1) / BLUNT_HIT_FRAMES) &
                                 ANGLE_MASK) >>
                  11;
        goto centered;
    case FLASH_CLASH_GLOW_0:
    case FLASH_CLASH_GLOW_1:
        /* the sine of half the streak's timer: it swells a little */
        *width =
            *width * rsin((g_eff_flash_timers[flash - 2] / 2) & ANGLE_MASK) >>
            11;
        *height =
            *height * rsin((g_eff_flash_timers[flash - 2] / 2) & ANGLE_MASK) >>
            11;
        goto centered;
    case FLASH_HIT_0:
    case FLASH_HIT_1:
        /* the sine of the timer: it swells */
        *width = *width * rsin(g_eff_flash_timers[flash] & ANGLE_MASK) >> 11;
        *height = *height * rsin(g_eff_flash_timers[flash] & ANGLE_MASK) >> 11;
    centered:
        prim->x0 = *g_eff_screen_xy - *width;
        prim->y0 = (*g_eff_screen_xy >> 16) - *height / 2;
        prim->x1 = *width + *g_eff_screen_xy;
        prim->y1 = (*g_eff_screen_xy >> 16) - *height / 2;
        prim->x2 = *g_eff_screen_xy - *width;
        prim->y2 = (*g_eff_screen_xy >> 16) + *height / 2;
        prim->x3 = *width + *g_eff_screen_xy;
        prim->y3 = (*g_eff_screen_xy >> 16) + *height / 2;
        break;
    case FLASH_LOCK_RATED:
        if (g_eff_flash_timers[flash] < ANGLE_TURN) {
            /* the lock flash's pulse, at half the size */
            *width =
                (u16)*width * 8 +
                (*width * rcos(g_eff_flash_timers[flash] & ANGLE_MASK) >> 9);
            *height =
                (u16)*height * 2 +
                (*height *
                     rcos((g_eff_flash_timers[flash] + (ANGLE_TURN / 2 - 1)) &
                          ANGLE_MASK) >>
                 11);
        } else {
            /* the second turn: nothing to see */
            *height = 0;
            *width = 0;
        }
        prim->x0 = *g_eff_screen_xy - *width / 4;
        prim->y0 = (*g_eff_screen_xy >> 16) - *height / 4;
        prim->x1 = *g_eff_screen_xy + *width / 4;
        prim->y1 = (*g_eff_screen_xy >> 16) - *height / 4;
        prim->x2 = *g_eff_screen_xy - *width / 4;
        prim->y2 = (*g_eff_screen_xy >> 16) + *height / 4;
        prim->x3 = *g_eff_screen_xy + *width / 4;
        prim->y3 = (*g_eff_screen_xy >> 16) + *height / 4;
        break;
    case FLASH_CLASH_STREAK_0:
    case FLASH_MATCHED_STREAK_0:
        if (g_game_settings.first_person &&
            g_sys_screen != SCREEN_REPLAY_UPDATE) {
            *width = -*width;
        }
    case FLASH_CLASH_STREAK_1:
    case FLASH_MATCHED_STREAK_1:
        if (g_game_settings.first_person &&
            g_sys_screen != SCREEN_REPLAY_UPDATE) {
            /* the burst swells with the sine of the timer */
            *width =
                *width * rsin(g_eff_flash_timers[flash] & ANGLE_MASK) >> 11;
            *height =
                *height * rsin(g_eff_flash_timers[flash] & ANGLE_MASK) >> 11;
        } else {
            /* the streak widens, to nearly three times: three sixteenths
             * of a turn of the sine over its life */
            *width =
                *width + (*width * rsin(g_eff_flash_timers[flash] *
                                        (3 * ANGLE_TURN / 16) / STREAK_END) >>
                          11);
            /* and shortens: an eighth of a turn of the cosine */
            *height =
                *height * rcos((g_eff_flash_timers[flash] << 9) / STREAK_END) >>
                12;
        }
        if (g_game_settings.first_person &&
            g_sys_screen != SCREEN_REPLAY_UPDATE) {
            prim->x0 = *g_eff_screen_xy - *width;
            prim->y0 = (*g_eff_screen_xy >> 16) - *height / 2;
            prim->x1 = *width + *g_eff_screen_xy;
            prim->y1 = (*g_eff_screen_xy >> 16) - *height / 2;
            prim->x2 = *g_eff_screen_xy - *width;
            prim->y2 = (*g_eff_screen_xy >> 16) + *height / 2;
            prim->x3 = *width + *g_eff_screen_xy;
            prim->y3 = (*g_eff_screen_xy >> 16) + *height / 2;
        } else if (g_eff_flash_timers[flash] < STREAK_LATE) {
            prim->x0 = *g_eff_screen_xy - *width / 2;
            prim->y0 = (*g_eff_screen_xy >> 16) + *height / 2;
            prim->x1 = *g_eff_screen_xy - *width / 2;
            prim->y1 = (*g_eff_screen_xy >> 16) - *height / 2;
            prim->x2 = *g_eff_screen_xy + *width / 2;
            prim->y2 = (*g_eff_screen_xy >> 16) + *height / 2;
            prim->x3 = *g_eff_screen_xy + *width / 2;
            prim->y3 = (*g_eff_screen_xy >> 16) - *height / 2;
        } else {
            prim->x0 = *g_eff_screen_xy;
            prim->y0 = (*g_eff_screen_xy >> 16) + *height / 2;
            prim->x1 = *g_eff_screen_xy;
            prim->y1 = (*g_eff_screen_xy >> 16) - *height / 2;
            prim->x2 = *width + *g_eff_screen_xy;
            prim->y2 = (*g_eff_screen_xy >> 16) + *height / 2;
            prim->x3 = *width + *g_eff_screen_xy;
            prim->y3 = (*g_eff_screen_xy >> 16) - *height / 2;
        }
        if (flash < FLASH_CLASH_GLOW_1) {
            /* a clash streak: round again for its glow */
            AddPrim(g_disp_ot + 1, prim);
            if (prim - g_eff_prims < EFF_PRIM_COUNT - 1) {
                prim++;
            }
            flash += FLASH_CLASH_GLOW_0 - FLASH_CLASH_STREAK_0;
            goto again;
        }
        break;
    }
    AddPrim(g_disp_ot + 1, prim);
    if (prim - g_eff_prims < EFF_PRIM_COUNT - 1) {
        prim++;
    }
    g_eff_next_prim = prim;
    return 1;
}

/* The particle effects' first two handlers: each throws out half of the
 * set's particles, the first also noting where from and which way. */
u8 eff_SpawnBlood0A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_BLOOD, 0, 0);
    g_eff_stage[EFF_BLOOD_0] = 1;
    return alive;
}

u8 eff_SpawnBlood0B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_BLOOD, 0, 1);
    g_eff_stage[EFF_BLOOD_0] = 2;
    return alive;
}

u8 eff_SpawnBlood1A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_BLOOD + 1, 1, 0);
    g_eff_stage[EFF_BLOOD_1] = 1;
    return alive;
}

u8 eff_SpawnBlood1B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_BLOOD + 1, 1, 1);
    g_eff_stage[EFF_BLOOD_1] = 2;
    return alive;
}

u8 eff_SpawnImpact0A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_IMPACT, 1, 0);
    g_eff_stage[EFF_IMPACT_0] = 1;
    return alive;
}

u8 eff_SpawnImpact0B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_IMPACT, 1, 1);
    g_eff_stage[EFF_IMPACT_0] = 2;
    return alive;
}

u8 eff_SpawnImpact1A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_IMPACT + 1, 0, 0);
    g_eff_stage[EFF_IMPACT_1] = 1;
    return alive;
}

u8 eff_SpawnImpact1B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_IMPACT + 1, 0, 1);
    g_eff_stage[EFF_IMPACT_1] = 2;
    return alive;
}

u8 eff_SpawnDirt0A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_DIRT, 2, 0);
    g_eff_stage[EFF_DIRT_0] = 1;
    return alive;
}

u8 eff_SpawnDirt0B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_DIRT, 2, 1);
    g_eff_stage[EFF_DIRT_0] = 2;
    return alive;
}

u8 eff_SpawnDirt1A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_DIRT + 1, 3, 0);
    g_eff_stage[EFF_DIRT_1] = 1;
    return alive;
}

u8 eff_SpawnDirt1B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_DIRT + 1, 3, 1);
    g_eff_stage[EFF_DIRT_1] = 2;
    return alive;
}

u8 eff_SpawnWater0A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_WATER, 3, 0);
    g_eff_stage[EFF_WATER_0] = 1;
    return alive;
}

u8 eff_SpawnWater0B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_WATER, 3, 1);
    g_eff_stage[EFF_WATER_0] = 2;
    return alive;
}

u8 eff_SpawnWater1A(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_WATER + 1, 2, 0);
    g_eff_stage[EFF_WATER_1] = 1;
    return alive;
}

u8 eff_SpawnWater1B(void) {
    u8 alive = eff_SpawnParticles(PARTICLES_WATER + 1, 2, 1);
    g_eff_stage[EFF_WATER_1] = 2;
    return alive;
}

/* Lets go half (`half` 0 or 1) of the particles of `set`: each gets the
 * effect's direction, lengthened by a random amount on each axis and turned
 * by a random angle. Blood is turned only a little, so it leaves as a
 * cone; impact debris is turned any way at all. */
static u8 eff_SpawnParticles(s32 kind, s32 set, s32 half) {
    SVECTOR thrown;
    /* three for the angles and three for the lengths; the last stirs the
     * next particle's */
    s32 random[THROW_RANDOMS];
    MATRIX turn;
    SVECTOR angles;
    VECTOR velocity;
    SVECTOR direction;
    s16 least;
    s16 scale_mask;
    s16 angle_mask;
    s16 angle_offset;
    u8 particle_count;
    s32 particle_index;
    Particle *particle;

    /* a throw is the direction times (least + a random number up to the
     * mask) over the mask */
    if (kind < PARTICLES_DIRT) {
        least = 0x41;      /* blood and debris: one to two times */
        scale_mask = 0x3F; /* a number up to 63 */
    } else {
        least = 0x31; /* dirt and water: one and a half to two and a half */
        scale_mask = 0x1F; /* a number up to 31 */
    }
    g_eff_particle_waves_out[set] = 0;
    gte_SetRotMatrix(g_eff_view_matrix);
    if (half == 0) {
        g_eff_particle_wave_positions[set][0].x = g_eff_position[0];
        g_eff_particle_wave_positions[set][0].y = g_eff_position[1];
        g_eff_particle_wave_positions[set][0].z = g_eff_position[2];
        g_eff_particle_directions[set].vx = g_eff_direction[0];
        g_eff_particle_directions[set].vy = g_eff_direction[1];
        g_eff_particle_directions[set].vz = g_eff_direction[2];
    }
    particle_count = PARTICLE_COUNT;
    random[THROW_RANDOMS - 1] = rand();
    /* how far a throw is turned about each axis: blood a thirty-second of
     * a turn either way, debris any way at all, dirt and water a
     * sixty-fourth */
    if (kind < PARTICLES_IMPACT) {
        angle_mask = ANGLE_TURN / 16 - 1;
        angle_offset = ANGLE_TURN / 32 - 1;
    } else if (kind < PARTICLES_DIRT) {
        angle_mask = ANGLE_MASK;
        angle_offset = 0;
    } else if (kind < PARTICLES_WATER) {
        angle_mask = ANGLE_TURN / 32 - 1;
        angle_offset = ANGLE_TURN / 64 - 1;
    } else if (kind < PARTICLES_END) {
        angle_mask = ANGLE_TURN / 32 - 1;
        angle_offset = ANGLE_TURN / 64 - 1;
    }
    direction.vx = g_eff_particle_directions[set].vx;
    direction.vy = g_eff_particle_directions[set].vy;
    direction.vz = g_eff_particle_directions[set].vz;
    for (particle_index = (particle_count >> 1) * half;
         particle_index < (particle_count >> 1) * (half + 1);
         particle_index++) {
        random[0] = random[THROW_RANDOMS - 1] ^ rand();
        random[1] = random[0] ^ rand();
        random[2] = random[1] ^ rand();
        random[3] = random[2] ^ rand();
        random[4] = random[3] ^ rand();
        random[THROW_RANDOMS - 1] = random[4] ^ rand();
        angles.vy = random[0] = (random[0] & angle_mask) - angle_offset;
        angles.vx = random[1] = (random[1] & angle_mask) - angle_offset;
        angles.vz = random[2] = (random[2] & angle_mask) - angle_offset;
        random[3] &= scale_mask;
        random[4] &= scale_mask;
        random[THROW_RANDOMS - 1] &= scale_mask;
        thrown.vx = direction.vx * (least + random[3]) / scale_mask;
        thrown.vy = direction.vy * (least + random[4]) / scale_mask;
        thrown.vz =
            direction.vz * (least + random[THROW_RANDOMS - 1]) / scale_mask;
        particle = &g_eff_particles[set][particle_index];
        particle->wave = particle_index / PARTICLE_WAVE_SIZE;
        particle->age = 0;
        particle->offset_z = 0;
        particle->offset_y = 0;
        particle->offset_x = 0;
        particle->state = PARTICLE_ACTIVE;
        RotMatrix(&angles, &turn);
        gte_ApplyMatrix(&turn, &thrown, &velocity);
        particle->velocity_x = velocity.vx;
        particle->velocity_y = velocity.vy;
        particle->velocity_z = velocity.vz;
    }
    return 1;
}

/* The particle effects' last handlers. */
u8 eff_DrawBlood0(void) {
    eff_SetUpParticles(PARTICLES_BLOOD, 0);
    eff_UpdateParticles(PARTICLES_BLOOD, 0);
    return eff_LinkParticleQuads(PARTICLES_BLOOD, 0);
}

u8 eff_DrawBlood1(void) {
    eff_SetUpParticles(PARTICLES_BLOOD + 1, 1);
    eff_UpdateParticles(PARTICLES_BLOOD + 1, 1);
    return eff_LinkParticleQuads(PARTICLES_BLOOD + 1, 1);
}

u8 eff_DrawImpact0(void) {
    eff_SetUpParticles(PARTICLES_IMPACT, 1);
    eff_UpdateParticles(PARTICLES_IMPACT, 1);
    return eff_LinkParticleQuads(PARTICLES_IMPACT, 1);
}

u8 eff_DrawImpact1(void) {
    eff_SetUpParticles(PARTICLES_IMPACT + 1, 0);
    eff_UpdateParticles(PARTICLES_IMPACT + 1, 0);
    return eff_LinkParticleQuads(PARTICLES_IMPACT + 1, 0);
}

u8 eff_DrawDirt0(void) {
    eff_SetUpParticles(PARTICLES_DIRT, 2);
    eff_UpdateParticles(PARTICLES_DIRT, 2);
    return eff_LinkParticleQuads(PARTICLES_DIRT, 2);
}

u8 eff_DrawDirt1(void) {
    eff_SetUpParticles(PARTICLES_DIRT + 1, 3);
    eff_UpdateParticles(PARTICLES_DIRT + 1, 3);
    return eff_LinkParticleQuads(PARTICLES_DIRT + 1, 3);
}

u8 eff_DrawWater0(void) {
    eff_SetUpParticles(PARTICLES_WATER, 3);
    eff_UpdateParticles(PARTICLES_WATER, 3);
    return eff_LinkParticleQuads(PARTICLES_WATER, 3);
}

u8 eff_DrawWater1(void) {
    eff_SetUpParticles(PARTICLES_WATER + 1, 2);
    eff_UpdateParticles(PARTICLES_WATER + 1, 2);
    return eff_LinkParticleQuads(PARTICLES_WATER + 1, 2);
}

/* Sets the texture and size for particles of `kind`, and lets the next wave
 * of `set` go from where the effect started. The kinds differ in how many
 * waves they have, so in how many of the 48 particles are ever seen. It
 * returns nothing, though it is declared to return a byte: declared void it
 * is four bytes shorter. */
static u8 eff_SetUpParticles(s32 kind, s32 set) {
    ParticleWork *work = &g_eff_work->particles;
    /* the work area's members, each through a pointer of its own, as in
     * the other drawing functions */
    u16 *tex_width = &work->tex_width;
    u16 *tex_height = &work->tex_height;
    u32 *reach_sq = &work->reach_sq;
    s16 *scan_count = &work->scan_count;
    POLY_FT4 **quad = &work->quad;

    g_eff_particle_random = &work->random;
    *quad = g_eff_next_prim;
    *reach_sq = PARTICLE_REACH * PARTICLE_REACH;
    *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;

    if (kind < PARTICLES_IMPACT) {
        g_eff_tex = g_eff_drop_tex;
        *tex_height = DROP_TEX_SIZE - 1;
        *tex_width = DROP_TEX_SIZE - 1;
        *g_eff_base_width = BLOOD_WIDTH;
        *g_eff_base_height = BLOOD_HEIGHT;
        g_eff_particle_wave_counts[kind] = PARTICLE_WAVES;
    } else if (kind < PARTICLES_DIRT) {
        g_eff_tex = g_eff_drop_tex;
        *tex_height = DROP_TEX_SIZE - 1;
        *tex_width = DROP_TEX_SIZE - 1;
        *g_eff_base_width = DEBRIS_WIDTH;
        *g_eff_base_height = DEBRIS_HEIGHT;
        g_eff_particle_wave_counts[kind] = PARTICLE_WAVES;
    } else if (kind < PARTICLES_WATER) {
        /* the puff shows a frame for eight of its first particle's age,
         * and stays on its last */
        s16 frame = g_eff_particles[set][0].age >> 3;
        work->u.tex_frame = frame;
        if (frame >= DIRT_PUFF_FRAMES) {
            work->u.tex_frame = DIRT_PUFF_FRAMES - 1;
        }
        if (g_eff_dirt_is_grass[kind - PARTICLES_DIRT]) {
            g_eff_tex = &g_eff_grass_texs[work->u.tex_frame];
        } else {
            g_eff_tex = &g_eff_dirt_texs[work->u.tex_frame];
        }
        /* the height is not one less, as the others are: a row more than
         * the frame has is drawn (the original's behaviour) */
        *tex_width = DIRT_PUFF_TEX_SIZE - 1;
#if RB_FIX_DIRT_PUFF_ROW
        if (g_rb_settings.bugfixes) {
            /* the frame's own rows, as the width */
            *tex_height = DIRT_PUFF_TEX_SIZE - 1;
        } else
#endif
        {
            *tex_height = DIRT_PUFF_TEX_SIZE;
        }
        *g_eff_base_width = DIRT_PUFF_WIDTH;
        *g_eff_base_height = DIRT_PUFF_HEIGHT;
        *g_eff_tpage = EFF_PAGE_RIGHT;
        g_eff_particle_wave_counts[kind] = 1;
    } else if (kind < PARTICLES_END) {
        *g_eff_tpage = EFF_PAGE_ADD | EFF_PAGE_LEFT;
        g_eff_tex = g_eff_water_tex;
        *tex_height = WATER_TEX_SIZE - 1;
        *tex_width = WATER_TEX_SIZE - 1;
        *g_eff_base_width = WATER_DROP_WIDTH;
        *g_eff_base_height = WATER_DROP_HEIGHT;
        g_eff_particle_wave_counts[kind] = 2;
    }

    *g_eff_clut = TEX_CLUT(g_eff_tex) << 16;
    *g_eff_tpage <<= 16;
    *g_eff_u0 = g_eff_tex->u;
    *g_eff_v0 = g_eff_tex->v;
    *g_eff_u1 = *tex_width + g_eff_tex->u;
    *g_eff_v1 = *tex_height + g_eff_tex->v;
    *g_eff_uv0 = *g_eff_u0 + (*g_eff_v0 << 8);
    *g_eff_uv1 = *g_eff_u1 + (*g_eff_v0 << 8);
    *g_eff_uv2 = *g_eff_u0 + (*g_eff_v1 << 8);
    *g_eff_uv3 = *g_eff_u1 + (*g_eff_v1 << 8);

    gte_SetRotMatrix(g_eff_view_matrix);

    if (g_eff_particle_waves_out[set] < g_eff_particle_wave_counts[kind]) {
        g_eff_particle_wave_positions[set][g_eff_particle_waves_out[set]].x =
            g_eff_particle_wave_positions[set][0].x;
        g_eff_particle_wave_positions[set][g_eff_particle_waves_out[set]].y =
            g_eff_particle_wave_positions[set][0].y;
        g_eff_particle_wave_positions[set][g_eff_particle_waves_out[set]].z =
            g_eff_particle_wave_positions[set][0].z;
    }

    gte_ReadGeomScreen(g_eff_screen_dist);

    *scan_count = (g_eff_particle_waves_out[set] + 1) * PARTICLE_WAVE_SIZE;
    if (g_eff_particle_waves_out[set] < g_eff_particle_wave_counts[kind] - 1) {
        g_eff_particle_waves_out[set]++;
    }
}

/* Moves every living particle of `set` a frame, pulled down by gravity,
 * and makes its quads: blood and debris are drawn three times along the
 * frame's path near where they were let go and twice further out, as a
 * streak; dirt and water once.
 * A particle is over when its colour has faded out or it has gone too far
 * (a little random, so they do not all stop on one sphere). */
static void eff_UpdateParticles(s32 kind, s32 set) {
    ParticleWork *work = &g_eff_work->particles;
    /* the work area's members, each through a pointer of its own, as in
     * the other drawing functions */
    u32 *reach_sq;
    VECTOR *wave_position;
    s16 *scan_count;
    s16 *index;
    u8 *red;
    u8 *green;
    u8 *blue;
    Vec3 **wave_entry;
    u16 *quad_ot_indices;
    s32 *random;
    s32 *out;
    VECTOR *step;
    SVECTOR *vertices;
    s16 *age;
    s16 *copy;
    POLY_FT4 **quad;
    Particle **particle;
    u32 dist_sq;
    s16 *sxy;

    g_eff_particle_random = &work->random;
    random = &work->random;
    *random = rand();
    reach_sq = &work->reach_sq;
    out = work->matrix.t;
    step = &work->step;
    vertices = work->vertices;
    wave_position = &work->wave_position;
    scan_count = &work->scan_count;
    index = &work->index;
    age = &work->u.age;
    red = &work->red;
    green = &work->green;
    blue = &work->blue;
    copy = &work->copy;
    quad = &work->quad;
    particle = &work->particle;
    wave_entry = &work->wave_position_entry;
    quad_ot_indices = work->ot_indices;

    for (*index = 0; *index < *scan_count; (*index)++) {
        *particle = &g_eff_particles[set][*index];
        if ((*particle)->state != PARTICLE_ACTIVE) {
            continue;
        }
        (*particle)->age++;
        *age = (*particle)->age;
        /* its colour fades with its age: red slowly from full, green from
         * 200 and blue from 100 quickly */
        *red = (*age << 2) < 0xFF ? ~(*age << 2) : 0;
        /* from 200 */
        *green = (*age << 5) < 200 ? 200 - (*age << 5) : 0;
        /* over once it is too dim to see */
        if (*red <= 0x50 && *green <= 0x20) {
            (*particle)->state = PARTICLE_ENDED;
            continue;
        }
        /* from 100 */
        *blue = (*age << 5) < 100 ? 100 - (*age << 5) : 0;

        *wave_entry = &g_eff_particle_wave_positions[set][(*particle)->wave];
        wave_position->vx = (*wave_entry)->x;
        vertices[0].vx = wave_position->vx - g_eff_view_origin[0];
        wave_position->vy = (*wave_entry)->y;
        vertices[0].vy = wave_position->vy - g_eff_view_origin[1];
        wave_position->vz = (*wave_entry)->z;
        vertices[0].vz = wave_position->vz - g_eff_view_origin[2];

        gte_ApplyRotMatrix(vertices, out);
        /* the matrix `out` is the translation of (see eff.h) */
        gte_SetTransMatrix((u8 *)out - 0x14);

        vertices[1].vx = (*particle)->offset_x;
        vertices[1].vz = (*particle)->offset_z;
        vertices[1].vy = (*particle)->offset_y;
        step->vx = (*particle)->velocity_x / FRAMES_PER_SECOND;
        (*particle)->offset_x += step->vx;
        vertices[0].vx = (*particle)->offset_x;
        step->vz = (*particle)->velocity_z / FRAMES_PER_SECOND;
        (*particle)->offset_z += step->vz;
        vertices[0].vz = (*particle)->offset_z;
        step->vy =
            (*particle)->velocity_y / FRAMES_PER_SECOND +
            *age * PARTICLE_GRAVITY / (FRAMES_PER_SECOND * FRAMES_PER_SECOND);
        (*particle)->offset_y += step->vy;
        vertices[0].vy = (*particle)->offset_y;
        vertices[2].vx = vertices[1].vx + (step->vx >> 1);
        vertices[2].vz = vertices[1].vz + (step->vz >> 1);
        vertices[2].vy = vertices[1].vy + (step->vy >> 1);

        gte_ldv3(&vertices[0], &vertices[1], &vertices[2]);
        step->vx = vertices[0].vx;
        step->vz = vertices[0].vz;
        step->vy = vertices[0].vy;
        gte_Square0(step, out);

        *random ^= rand();
        if ((out[0] + out[1] + out[2]) >
            *reach_sq +
                *random * PARTICLE_REACH / (RAND_MAX + 1) * PARTICLE_REACH) {
            (*particle)->state = PARTICLE_ENDED;
            continue;
        }

        gte_rtpt();
        gte_stsxy3(
            &g_eff_screen_xy[0], &g_eff_screen_xy[1], &g_eff_screen_xy[2]);
        gte_stsz(g_eff_depth);

        if (*g_eff_depth < 0) {
            continue;
        }
        g_eff_depth[1] = *g_eff_depth ? *g_eff_depth : 1;
        *g_eff_depth = render_DepthToOtShifted(g_eff_depth[1], 0);
        if (*g_eff_depth == 0) {
            *g_eff_depth = 1;
        }
        *g_eff_rtp_flag = 1;
        if (*g_eff_depth >= EFF_OT_END || !*g_eff_rtp_flag) {
            continue;
        }

        if (kind < PARTICLES_IMPACT) {
            *g_eff_scale = *g_eff_screen_dist * PARTICLE_SCALE / g_eff_depth[1];
            *g_eff_right = *g_eff_base_width * *g_eff_scale > 1 << 9
                               ? *g_eff_base_width * *g_eff_scale >> 9
                               : 1;
            *g_eff_bottom = *g_eff_base_height * *g_eff_scale > 1 << 9
                                ? *g_eff_base_height * *g_eff_scale >> 9
                                : 1;
        } else if (kind < PARTICLES_DIRT) {
            *g_eff_scale = *g_eff_screen_dist * PARTICLE_SCALE / g_eff_depth[1];
            *g_eff_right = *g_eff_base_width * *g_eff_scale > 1 << 9
                               ? *g_eff_base_width * *g_eff_scale >> 9
                               : 1;
            *g_eff_bottom = *g_eff_base_height * *g_eff_scale > 1 << 9
                                ? *g_eff_base_height * *g_eff_scale >> 9
                                : 1;
            /* debris begins nearly four times the size (its age is 1 the
             * first time) and shrinks quickly */
            *g_eff_right += *g_eff_right * 20 / (*age * 6 + 1);
            /* the same */
            *g_eff_bottom += *g_eff_bottom * 20 / (*age * 6 + 1);
        } else if (kind < PARTICLES_WATER) {
            *g_eff_scale = *g_eff_screen_dist * PARTICLE_SCALE / g_eff_depth[1];
            *g_eff_right = *g_eff_base_width * *g_eff_scale > 1 << 9
                               ? *g_eff_base_width * *g_eff_scale >> 9
                               : 1;
            *g_eff_bottom = *g_eff_base_height * *g_eff_scale > 1 << 9
                                ? *g_eff_base_height * *g_eff_scale >> 9
                                : 1;
            /* a puff swells by half its size for every frame of its age */
            *g_eff_right += *g_eff_right * *age / 2;
            *g_eff_bottom += *g_eff_bottom * *age / 2;
        } else if (kind < PARTICLES_END) {
            *g_eff_scale = *g_eff_screen_dist * PARTICLE_SCALE / g_eff_depth[1];
            *g_eff_right = *g_eff_base_width * *g_eff_scale > 1 << 9
                               ? *g_eff_base_width * *g_eff_scale >> 9
                               : 1;
            *g_eff_bottom = *g_eff_base_height * *g_eff_scale > 1 << 9
                                ? *g_eff_base_height * *g_eff_scale >> 9
                                : 1;
            sxy = (s16 *)g_eff_screen_xy;
            /* the drop where it was a frame ago is shaken: up to 20
             * across */
            sxy[2] += rand() * 20 / (RAND_MAX + 1);
            /* and 10 down */
            sxy[3] += rand() * 10 / (RAND_MAX + 1);
        }
        if (*g_eff_right > *g_eff_base_width * 2) {
            *g_eff_right = *g_eff_base_width >> 1;
            *g_eff_bottom = *g_eff_base_height >> 1;
        }
        *g_eff_left = -*g_eff_right;
        *g_eff_top = -*g_eff_bottom;
        *g_eff_bottom <<= 16;
        *g_eff_top <<= 16;

        /* how many times it is drawn, by how far it has got: twice, or
         * three times near where it began. The test for once comes second
         * and cannot pass, a half being more than a third: the original's
         * behaviour. */
        dist_sq = out[0] + out[1] + out[2];
        if (*reach_sq / 3 < dist_sq) {
            *copy = 1;
        } else if (*reach_sq / 2 < dist_sq) {
            *copy = 0;
        } else {
            *copy = 2;
        }
        /* a corner's x and y, and its u, v and palette, written as words */
        for (; *copy >= 0; (*copy)--) {
            quad_ot_indices[*quad - g_eff_next_prim] = *g_eff_depth;
            *(s32 *)&(*quad)->x0 =
                g_eff_screen_xy[*copy] + *g_eff_left + *g_eff_top;
            *(s32 *)&(*quad)->x1 =
                g_eff_screen_xy[*copy] + *g_eff_right + *g_eff_top;
            *(s32 *)&(*quad)->x2 =
                g_eff_screen_xy[*copy] + *g_eff_left + *g_eff_bottom;
            *(s32 *)&(*quad)->x3 =
                g_eff_screen_xy[*copy] + *g_eff_right + *g_eff_bottom;
            *(s32 *)&(*quad)->u0 = *g_eff_uv0 + *g_eff_clut;
            *(s32 *)&(*quad)->u1 = *g_eff_uv1 + *g_eff_tpage;
            *(u16 *)&(*quad)->u2 = *g_eff_uv2;
            *(u16 *)&(*quad)->u3 = *g_eff_uv3;
            setlen(*quad, FT4_LEN);
            if (kind < PARTICLES_IMPACT) {
                *(s32 *)&(*quad)->r0 =
                    *red + (*green << 8) + ((FT4_CODE | FT4_SEMI_TRANS) << 24);
            } else if (kind < PARTICLES_DIRT) {
                *(s32 *)&(*quad)->r0 =
                    *red + ((*red >> 2) << 8) + (*blue << 16) +
                    ((FT4_CODE | FT4_SEMI_TRANS) << 24);
            } else if (kind < PARTICLES_WATER) {
                if (*quad - g_eff_prims >= EFF_PRIM_COUNT - 1) {
                    continue;
                }
                /* a puff is not drawn half and half or added, but as it
                 * is */
                if (g_game_settings.grayscale) {
                    /* dark grey */
                    *(s32 *)&(*quad)++->r0 =
                        EFF_RGB(0x24, 0x24, 0x24) | FT4_CODE << 24;
                    break;
                } else if (g_eff_dirt_is_grass[kind - PARTICLES_DIRT]) {
                    /* a green of 60 and up to 31 more, any amount */
                    *(s32 *)&(*quad)++->r0 =
                        (((rand() * 4 >> 12) + 0x3C) << 8) +
                        (EFF_RGB(0x38, 0, 0x08) | FT4_CODE << 24);
                    break;
                } else {
                    /* brown */
                    *(s32 *)&(*quad)++->r0 =
                        EFF_RGB(0x78, 0x5A, 0x28) | FT4_CODE << 24;
                    break;
                }
            } else if (kind < PARTICLES_END) {
                if (*quad - g_eff_prims >= EFF_PRIM_COUNT - 1) {
                    break;
                }
                /* pale blue */
                *(s32 *)&(*quad)++->r0 = EFF_RGB(0x80, 0x80, 0xFF) |
                                         (FT4_CODE | FT4_SEMI_TRANS) << 24;
                break;
            }
            if (*quad - g_eff_prims < EFF_PRIM_COUNT - 1) {
                (*quad)++;
            }
        }
    }
}

/* Links the quads eff_UpdateParticles made into the ordering table.
 * Returns 0 if there were none, which ends the effect. `kind` and `set` are
 * not used. */
static u8 eff_LinkParticleQuads(s32 kind, s32 set) {
    ParticleWork *work = &g_eff_work->particles;
    s16 *quad_index = &work->index;
    POLY_FT4 **quads_end = &work->quads_end;
    POLY_FT4 **quad = &work->quad;
    u16 *quad_ot_indices = work->ot_indices;
    POLY_FT4 *made_end;
    POLY_FT4 *first_quad;
    s32 quad_count;
    s32 has_quads;

    g_eff_particle_random = &work->random;
    first_quad = g_eff_next_prim;
    made_end = *quad;
    quad_count = made_end - first_quad;

    if (quad_count != 0) {
        *quad = first_quad;
        *quads_end = made_end;
        has_quads = 1;
        *quad_index = 0;

        while (*quad < *quads_end) {
            u32 *prim_tag;

            g_eff_ot_entry = g_disp_ot + quad_ot_indices[*quad_index];
            prim_tag = &(*quad)->tag;
            g_eff_prim_tag = prim_tag;
            *prim_tag =
                (*prim_tag & PRIM_TAG_LEN) | (*g_eff_ot_entry & PRIM_TAG_ADDR);

            *g_eff_ot_entry = ((u32)g_eff_prim_tag & PRIM_TAG_ADDR) |
                              (*g_eff_ot_entry & PRIM_TAG_LEN);

            (*quad)++;
            (*quad_index)++;
        }
        g_eff_next_prim = *quads_end;
    } else {
        has_quads = 0;
    }

    return has_quads;
}

/* Defined last, as the original has them. */
static u32 g_eff_flame_frame = 0;
/* Frames of light left. */
static s32 g_eff_light_frames = 0;
POLY_FT4 *g_eff_prims;
s32 *g_eff_particle_random;
POLY_FT4 *g_eff_next_prim;
