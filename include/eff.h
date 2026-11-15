#ifndef EFF_H
#define EFF_H

/* Effects: everything in a fight that is drawn as flat textured quads and
 * is not part of a model. Blood, the sparks and streaks of blades meeting,
 * the flash of a hit, a gun's muzzle flash and where its bullet lands, dust,
 * splashes, thrown dirt and water, footprints in the sand, and a stage's
 * flames.
 *
 * There are 28 effects (enum EffectId), each alive or not. The fight asks
 * for one with an eff_Start... function, which fills in a request and runs
 * the effect's first handler; after that eff_Update runs the handler of
 * every effect that is alive, once a frame, until the handler says it is
 * over. An effect that both fighters can cause has two ids, and the second
 * request while both are alive takes the first one over. Most effects also
 * light the fighters for a few frames (the point light of light.h).
 *
 * Effects are of four kinds: a "flash" is one quad facing the view that
 * grows, spins or fades by a timer; "particles" are 48 small quads thrown
 * out and pulled down; the muzzle flash, splashes, dust and footprints each
 * have their own few slots; flames are listed by the stage and never end.
 *
 * All of it draws into two buffers of quads, one a frame, and keeps what it
 * is working on in the scratchpad (EffScratch). */

#include "common.h"
#include <psxsdk/libgte.h>
#include <psxsdk/libgpu.h>

/* Quads a frame; the last one is drawn over once the buffer is full. */
#define EFF_PRIM_COUNT 450
/* A quad is dropped when its ordering table entry would be this or more. */
#define EFF_OT_END 0x1005

/* The effects: the number one is asked for by, and its place in eff.c's
 * table of the ones alive. */
enum EffectId {
    EFF_FOOTPRINTS_0 = 0, /* one a fighter */
    EFF_FOOTPRINTS_1 = 1,
    EFF_BLOOD_0 = 2,
    EFF_BLOOD_1 = 3,
    EFF_LOCK_FLASH = 4,
    EFF_CLASH_SPARK_0 = 5,
    EFF_CLASH_SPARK_1 = 6,
    EFF_DUST = 7,
    EFF_SPLASH = 8,
    EFF_HIT_FLASH_0 = 9,
    EFF_HIT_FLASH_1 = 10,
    EFF_LOCK_FLASH_RATED = 11,
    EFF_CLASH_STREAK_0 = 12, /* one a fighter */
    EFF_CLASH_STREAK_1 = 13,
    EFF_MATCHED_STREAK_0 = 14, /* one a fighter */
    EFF_MATCHED_STREAK_1 = 15,
    EFF_BLUNT_HIT_0 = 16,
    EFF_BLUNT_HIT_1 = 17,
    EFF_ARMOR_SPARK_0 = 18,
    EFF_ARMOR_SPARK_1 = 19,
    EFF_IMPACT_0 = 20,
    EFF_IMPACT_1 = 21,
    EFF_DIRT_0 = 22,
    EFF_DIRT_1 = 23,
    EFF_WATER_0 = 24,
    EFF_WATER_1 = 25,
    EFF_MUZZLE_FLASH = 26,
    EFF_COUNT = 28, /* 27 is not used */
};

/* A request's word: the effect's id in the low half, and above it: */
#define EFF_NEW 0x10000  /* set by every request; nothing reads it */
#define EFF_LIT 0x200000 /* light the fighters for EFF_LIGHT_FRAMES */
/* which footprint texture (EFF_FOOTPRINTS_ only) */
#define EFF_FOOTPRINT_SHAPE_SHIFT 17
#define EFF_FOOTPRINT_SHAPE(shape) ((shape) << EFF_FOOTPRINT_SHAPE_SHIFT)
/* how fast the flash plays, enum LockFlashRate (EFF_LOCK_FLASH_RATED only) */
#define EFF_RATE_SHIFT 19
#define EFF_RATE(rate) ((rate) << EFF_RATE_SHIFT)

#define EFF_LIGHT_FRAMES 10

/* How fast EFF_LOCK_FLASH_RATED plays (EFF_RATE). */
enum LockFlashRate {
    LOCK_FLASH_SLOW = 0,
    LOCK_FLASH_MEDIUM = 1,
    LOCK_FLASH_FAST = 2,
};

/* Where an effect's texture is in video memory: its palette, and the top
 * left corner in its texture page. */
typedef struct {
    u16 clut_x;
    u16 clut_y;
    u16 u;
    u16 v;
} TexRec;

/* What an effect is asked to do, and with what. Two are used. The fight's
 * requests go through g_eff_start_request: the id and flags, where the
 * effect is, and for some which way it points; eff_Start copies the last two
 * into `position` and `vector`, where the first handler finds them. The one
 * in the scratchpad is the frame's: eff_Update gives it each living effect's
 * id in turn, and it holds the view (where `view_angles_src` and
 * `view_position_src` point, copied to `vector` and `position`). */
typedef struct {
    union {
        s32 word; /* id and flags */
        u16 id;
    } effect;

    PTR32(s16 *) view_angles_src;
    PTR32(s32 *) view_position_src;
    PTR32(s32 *) position_src;
    PTR32(s16 *) direction_src;
    PTR32(u8 *) alive_dest; /* the effect's g_eff_alive entry */
    s16 vector[3];
    s32 position[3];
} EffRequest;

/* A flame of the stage's, from the stage file; the list ends at a record
 * with the low bit of `x_bits` clear. */
typedef struct {
    s32 x_bits; /* x times two, and FLAME_PRESENT */
    s32 y_bits; /* y times eight, and the kind */
    s32 z;
} Flame;

#define FLAME_PRESENT 1
#define FLAME_KIND(flame) ((flame).y_bits & 7)

/* The four looks of a flame (FLAME_KIND). */
enum FlameKind {
    FLAME_FIRE = 0,       /* six frames */
    FLAME_TORCH_TALL = 1, /* four frames */
    FLAME_TORCH = 2,
    FLAME_FIRE_SMALLER = 3,
};

/* The single-quad effects (g_eff_flash_positions, g_eff_flash_timers). */
enum Flash {
    FLASH_LOCK = 0,
    FLASH_CLASH_SPARK_0 = 1,
    FLASH_CLASH_SPARK_1 = 2,
    FLASH_HIT_0 = 3,
    FLASH_HIT_1 = 4,
    FLASH_LOCK_RATED = 5,
    FLASH_CLASH_STREAK_0 = 6,
    FLASH_CLASH_STREAK_1 = 7,
    /* the glow drawn with a clash streak: it has no timer or position of
     * its own and uses the streak's, two back */
    FLASH_CLASH_GLOW_0 = 8,
    FLASH_CLASH_GLOW_1 = 9,
    FLASH_MATCHED_STREAK_0 = 10,
    FLASH_MATCHED_STREAK_1 = 11,
    FLASH_BLUNT_HIT_0 = 12,
    FLASH_BLUNT_HIT_1 = 13,
    FLASH_BLUNT_HIT_0_AGAIN = 14, /* drawn over the first at the same place */
    FLASH_BLUNT_HIT_1_AGAIN = 15,
    FLASH_ARMOR_SPARK_0 = 16,
    FLASH_ARMOR_SPARK_1 = 17,
    FLASH_COUNT = 18,
};

/* Particles. A kind has two instances, one a living effect: PARTICLES_BLOOD
 * and PARTICLES_BLOOD + 1. There are four sets of particles to go round
 * (eff_SpawnParticles's `set`), so two kinds share each. */
enum ParticleKind {
    PARTICLES_BLOOD = 0,  /* thrown in a narrow cone; yellow, turning red */
    PARTICLES_IMPACT = 2, /* thrown every way; pale, shrinking */
    PARTICLES_DIRT = 4,   /* a puff that swells; brown, or green */
    PARTICLES_WATER = 6,  /* a puff of pale blue drops */
    PARTICLES_END = 8,
};

#define PARTICLE_SETS 4
#define PARTICLE_COUNT 48     /* in a set */
#define PARTICLE_WAVE_SIZE 16 /* let go together, a wave a frame */
#define PARTICLE_WAVES 3

/* Particle.state */
enum ParticleState {
    PARTICLE_ACTIVE = 1,
    PARTICLE_ENDED = 3,
};

/* One particle of a set. */
typedef struct {
    s16 offset_x; /* from where its wave was let go */
    s16 offset_y;
    s16 offset_z;
    s16 wave;
    s16 velocity_x; /* a thirtieth of it is added a frame */
    s16 velocity_y;
    s16 velocity_z;
    s16 pad0E; /* a velocity's fourth; not touched */
    s16 state; /* enum ParticleState */
    s16 age;   /* frames */
} Particle;

/* What the drawing functions keep on the scratchpad between them: where
 * the next quad goes, a particle, a wave's place. Each is an address in
 * four bytes (PTR32, common.h): the scratchpad's layout is the
 * PlayStation's. */
typedef PTR32(POLY_FT4 *) EffQuadSlot;
typedef PTR32(Particle *) ParticleSlot;
typedef PTR32(Vec3 *) Vec3Slot;

#define FOOTPRINT_COUNT 10 /* a fighter; the oldest is trodden over */
#define DUST_COUNT 16
#define SPLASH_COUNT 12
#define MUZZLE_FLASH_COUNT 6

/* The scratchpad from 0xB8 up is each drawing function's own. These are
 * their layouts of it; a `pad` is a stretch a function neither reads nor
 * writes. Where a function hands SetTransMatrix "the matrix
 * 0x14 below" a vector, there is no matrix: only the translation is read,
 * and the vector is where a matrix there would have it. The original does
 * the same: it takes 0x14 from the vector's address, and what lies there
 * differs from one layout to the next. */

/* eff_DrawFlames */
typedef struct {
    SVECTOR view_angles; /* eff_SetViewMatrix's */
    u8 pad08[8];         /* not touched */
    s16 half_width;      /* on screen */
    s16 height;
    VECTOR rotated_position;       /* in view space */
    VECTOR view_relative_position; /* in the world, from the view */
    SVECTOR zero_vector;
    s32 depth_cue; /* not used */
    u8 pad40[4];   /* not touched */
    u32 ot_index;
} FlameWork;

/* eff_DrawSplashes */
typedef struct {
    u8 pad00[0x14]; /* not this function's */
    VECTOR rotated_position;
    SVECTOR view_relative_position;
    SVECTOR zero_vector;
    s32 depth_cue; /* not used */
    s32 fade;      /* the drops' age less 15 */
    s32 ot_index;
} SplashWork;

/* eff_DrawDust */
typedef struct {
    u8 pad00[0x10]; /* not this function's */
    s16 width;      /* on screen */
    s16 height;
    s32 frame; /* of the puff's seven */
    VECTOR rotated_position;
    VECTOR view_relative_position;
    SVECTOR zero_vector;
    s32 depth_cue; /* not used */
    EffQuadSlot quads_end;
    u32 ot_indices[DUST_COUNT]; /* of each quad made */
} DustWork;

/* eff_DrawFlash. SetTransMatrix is handed the layout itself, as a matrix
 * whose translation is corners[0]. */
typedef struct {
    u8 pad00[0x10]; /* not this function's */
    s32 depth_cue;  /* not used */
    /* [0] is first the flash's place in view space; then the four corners
     * of a spinning flash, turned */
    VECTOR corners[4];
    VECTOR view_relative_position;
    SVECTOR vertex; /* zero, then a spinning flash's angle and corners */
    /* on screen: a word each, of which the low half is used */
    s16 width;
    u8 pad6E[2];
    s16 height;
    u8 pad72[2];
    MATRIX spin; /* a spinning flash's turn */
    s16 tex_width;
    s16 tex_height;
} FlashWork;

/* eff_DrawMuzzleFlashes */
typedef struct {
    u8 pad00[0x78]; /* not this function's */
    MATRIX matrices[MUZZLE_FLASH_COUNT];
    MATRIX composite; /* the view's and a flash's */
    s32 sxy[4];
    s32 depth_cue;                      /* not used */
    u16 ot_indices[MUZZLE_FLASH_COUNT]; /* of each quad made */
    s32 scale[4];
    s32 gray;
} MuzzleFlashWork;

/* eff_DrawFootprints */
typedef struct {
    u8 pad00[0x28]; /* not this function's */
    MATRIX matrices[FOOTPRINT_COUNT];
    MATRIX composite; /* the view's and a footprint's */
    s32 sxy[5];
    u16 ot_indices[FOOTPRINT_COUNT]; /* of each quad made */
    u16 made;                        /* how many */
} FootprintWork;

/* eff_SetUpParticles, eff_UpdateParticles and eff_LinkParticleQuads, which
 * run one after the other and leave things here for the next. */
typedef struct {
    u16 tex_width; /* less one */
    u16 tex_height;
    /* a particle this far (squared) from where it was let go is over */
    u32 reach_sq;
    u8 pad08[8]; /* not touched */
    /* only its translation: the wave's place in view space, then the
     * squares of a particle's offsets */
    MATRIX matrix;
    u8 pad30[4];         /* not touched */
    VECTOR step;         /* a particle's move this frame; then its offset */
    SVECTOR vertices[3]; /* where it is, where it was, and half way */
    VECTOR wave_position;
    s16 scan_count; /* particles let go so far */
    s16 index;

    union {
        s16 tex_frame; /* eff_SetUpParticles: which of a puff's frames */
        s16 age;       /* eff_UpdateParticles: the particle's */
    } u;

    u8 red;
    u8 green;
    u8 blue;
    u8 pad75[3]; /* not touched */
    s16 copy;    /* which of the three vertices a quad is drawn at */
    EffQuadSlot quads_end;
    EffQuadSlot quad; /* the next to fill */
    ParticleSlot particle;
    Vec3Slot wave_position_entry;
    u16 ot_indices[PARTICLE_COUNT * 3]; /* of each quad made */
    s32 random;                         /* g_eff_particle_random */
} ParticleWork;

/* The drawing functions' part of the scratchpad, under each one's layout. */
typedef union {
    u8 raw[0x400 - 0xB8];
    SVECTOR view_angles; /* eff_SetViewMatrix */
    FlameWork flames;
    SplashWork splash;
    DustWork dust;
    FlashWork flash;
    MuzzleFlashWork muzzle_flashes;
    FootprintWork footprints;
    ParticleWork particles;
} EffWork;

/* The scratchpad as the effects use it (the fight's own layout of it is
 * FightScratch). eff_BindScratch points a global at every member, and the
 * code reaches them only through those: g_eff_tpage is &tpage, and so on.
 * They are the drawing functions' registers: the texture and the quad being
 * worked out. */
typedef struct {
    EffRequest request;
    u8 pad2C[4]; /* not touched */
    MATRIX view_matrix;
    TexRec first_tex; /* what g_eff_tex points at before any texture is set */
    s32 tpage;
    s32 clut;
    u16 u0; /* the texture's left, right, top and bottom */
    u16 u1;
    u16 v0;
    u16 v1;
    s16 base_width; /* the quad's size in the world */
    s16 base_height;
    s32 screen_dist; /* the projection distance, scaled */
    s32 scale;       /* screen_dist over the depth */
    s32 screen_xy[3];
    s16 left; /* the quad's edges on screen, from its center */
    s16 right;
    s32 top;
    s32 bottom;
    s32 rtp_flag;
    s32 depth[2];
    u16 uv0; /* the four corners' u and v, packed as a primitive has them */
    u16 uv1;
    u16 uv2;
    u16 uv3;
    u32 first_ot_entry; /* what g_eff_ot_entry and g_eff_prim_tag point at */
    u32 first_prim_tag; /* before they are set */
    s32 footprint_shape;
    s32 flash_rate;
    TexRec first_clut_tex;
    EffWork work;
} EffScratch;

#define EFF_SPAD ((EffScratch *)SCRATCHPAD)

/* --- eff.c: the system --- */

extern u8 g_eff_first_handler[EFF_COUNT];
/* Handlers of several return types, kept as words and called as this. */
typedef s32 (*EffHandler)(void);
extern void *g_eff_handlers[62];

/* The view matrix is squashed to half height, as the renderer's is. */
extern VECTOR g_eff_view_scale;

/* Loads the mode's effect textures into video memory through `buffer`, and
 * takes the two quad buffers at `quads`; returns where they end. */
extern s32 eff_LoadTextures(s32 buffer, s32 quads);
/* Picks this frame's quad buffer. */
extern void eff_SelectPrimBuffer(s32 second);
/* Runs every living effect for a frame, seen from `view_position` at
 * `view_angles`, and the light. */
extern void eff_Update(s16 *view_angles, s32 *view_position);
/* Ends every effect and the light. */
extern void eff_Stop(void);

/* main.c: loads the textures when they are not in video memory. */
extern void eff_Init(void);
extern void eff_ClearInitFlag(void);

/* --- eff.c: what the fight asks for. `position` is a world point. --- */

/* A blade goes into a body. */
extern void eff_StartHitFlash(s32 *position);
/* Blood from a wound, thrown along `direction`. */
extern void eff_StartBlood(s32 *position, s16 *direction);
/* The hit of a wooden sword, which draws no blood. */
extern void eff_StartBluntHit(s32 *position);
/* A hit that does nothing to an armoured fighter. */
extern void eff_StartArmorSpark(s32 *position);
/* Two weapons touch. */
extern void eff_StartClashSpark(s32 *position);
/* Two attacks meet: `fighter`'s streak, with a glow. */
extern void eff_StartClashStreak(s32 *position, s32 fighter);
/* Two attacks of the same kind meet: `fighter`'s streak. */
extern void eff_StartMatchedStreak(s32 *position, s32 fighter);
/* Locked blades grind. Each returns the frames to wait before the next. */
extern s32 eff_StartLockFlash(s32 *position);
extern s32 eff_StartLockFlashSlow(s32 *position);
extern s32 eff_StartLockFlashMedium(s32 *position);
extern s32 eff_StartLockFlashFast(s32 *position);
/* A gun fires; `src_angles` point along the barrel. */
extern void eff_StartMuzzleFlash(s32 *position, s16 *src_angles);
/* Its bullet lands; `normal` is the surface's. */
extern void eff_StartBulletImpact(s32 *position, s16 *normal);
/* A foot comes down in water, or on dry ground. */
extern void eff_StartSplash(s32 *position);
extern void eff_StartDust(s32 *position);
/* Dirt, or water, thrown forward by a fighter facing `yaw`. The fight calls
 * eff_StartThrownDirt with two arguments, so its third, `grass`, is whatever
 * the register held; it is left without a prototype for that. */
extern void eff_StartThrownDirt();
extern void eff_StartThrownWater(s32 *position, s16 yaw);
/* A footprint of `fighter`'s; `shape_bits` is EFF_FOOTPRINT_SHAPE(). */
extern void eff_AddFootprint(
    s32 *position, s16 yaw, s32 shape_bits, s32 fighter);
/* Which footprint a character leaves, by the class
 * g_fighter_roster_classes gives it. */
extern s32 g_eff_footprint_shape_bits[5];

/* The stage's flames, and a count of the calls to eff_DrawFlames, which
 * picks their frame. */
extern void eff_SetFlames(Flame *flames);
extern void eff_DrawFlames(s16 *view_angles, s32 *view_position);

/* --- eff.c: the handlers, in g_eff_handlers's order. An effect's first
 * (two, for particles, which are thrown out over two frames) starts it; its
 * last draws it and moves it on a frame, and returns 0 when it is over. --- */
extern u8 eff_SpawnFootprint0(void);
extern u8 eff_DrawFootprints0(void);
extern u8 eff_SpawnFootprint1(void);
extern u8 eff_DrawFootprints1(void);
extern u8 eff_SpawnBlood0A(void);
extern u8 eff_SpawnBlood0B(void);
extern u8 eff_DrawBlood0(void);
extern u8 eff_SpawnBlood1A(void);
extern u8 eff_SpawnBlood1B(void);
extern u8 eff_DrawBlood1(void);
extern void eff_SpawnLockFlash(void);
extern u8 eff_DrawLockFlash(void);
extern void eff_SpawnClashSpark0(void);
extern u8 eff_DrawClashSpark0(void);
extern void eff_SpawnClashSpark1(void);
extern u8 eff_DrawClashSpark1(void);
extern s32 eff_SpawnDust(void);
extern s32 eff_DrawDust(void);
extern s32 eff_SpawnSplash(void);
extern s32 eff_DrawSplashes(void);
extern s32 eff_SpawnHitFlash0(void);
extern u8 eff_DrawHitFlash0(void);
extern s32 eff_SpawnHitFlash1(void);
extern u8 eff_DrawHitFlash1(void);
extern s32 eff_SpawnLockFlashRated(void);
extern u8 eff_DrawLockFlashRated(void);
extern void eff_SpawnClashStreak0(void);
extern u8 eff_DrawClashStreak0(void);
extern void eff_SpawnClashStreak1(void);
extern u8 eff_DrawClashStreak1(void);
extern void eff_SpawnMatchedStreak0(void);
extern u8 eff_DrawMatchedStreak0(void);
extern void eff_SpawnMatchedStreak1(void);
extern u8 eff_DrawMatchedStreak1(void);
extern void eff_SpawnBluntHit0(void);
extern s32 eff_DrawBluntHit0(void);
extern void eff_SpawnBluntHit1(void);
extern s32 eff_DrawBluntHit1(void);
extern void eff_SpawnArmorSpark0(void);
extern u8 eff_DrawArmorSpark0(void);
extern void eff_SpawnArmorSpark1(void);
extern u8 eff_DrawArmorSpark1(void);
extern u8 eff_SpawnImpact0A(void);
extern u8 eff_SpawnImpact0B(void);
extern u8 eff_DrawImpact0(void);
extern u8 eff_SpawnImpact1A(void);
extern u8 eff_SpawnImpact1B(void);
extern u8 eff_DrawImpact1(void);
extern u8 eff_SpawnDirt0A(void);
extern u8 eff_SpawnDirt0B(void);
extern u8 eff_DrawDirt0(void);
extern u8 eff_SpawnDirt1A(void);
extern u8 eff_SpawnDirt1B(void);
extern u8 eff_DrawDirt1(void);
extern u8 eff_SpawnWater0A(void);
extern u8 eff_SpawnWater0B(void);
extern u8 eff_DrawWater0(void);
extern u8 eff_SpawnWater1A(void);
extern u8 eff_SpawnWater1B(void);
extern u8 eff_DrawWater1(void);
extern s32 eff_SpawnMuzzleFlash(void);
extern s32 eff_DrawMuzzleFlashes(void);

/* --- the effects' tables (eff_data.c) --- */

extern TexRec g_eff_flame_texs[12]; /* 6 and 4 frames, then their greys */

extern SVECTOR g_eff_muzzle_flash_vertices[3][4];
extern TexRec g_eff_muzzle_flash_end_tex[1];
extern TexRec g_eff_muzzle_flash_side_tex[1];
extern TexRec g_eff_muzzle_smoke_end_tex[1];
extern TexRec g_eff_muzzle_smoke_side_tex[1];

extern TexRec g_eff_splash_crown_tex[1];
extern TexRec g_eff_splash_drops_tex[1];
extern TexRec g_eff_splash_drops_late_tex[1];

extern TexRec g_eff_footprint_texs[4];
extern SVECTOR g_eff_footprint_vertex_0;
extern SVECTOR g_eff_footprint_vertex_1;
extern SVECTOR g_eff_footprint_vertex_2;
extern SVECTOR g_eff_footprint_vertex_3;

extern TexRec g_eff_dust_texs[7];

extern TexRec g_eff_star_tex[1];
extern TexRec g_eff_spark_far_tex[1];
extern TexRec g_eff_spark_near_tex[1];
extern TexRec g_eff_lock_flash_alt_tex[1];
extern TexRec g_eff_glow_tex[1];
extern TexRec g_eff_glow_late_tex[1];
extern TexRec g_eff_streak_tex[1];
extern TexRec g_eff_streak_late_tex[1];
/* the streak as the first-person view shows it */
extern TexRec g_eff_streak_close_tex[1];
extern TexRec g_eff_streak_close_late_tex[1];
extern TexRec g_eff_blunt_hit_tex[1];
extern TexRec g_eff_blunt_hit_mid_tex[2];

extern TexRec g_eff_drop_tex[4];
extern TexRec g_eff_water_tex[3];
extern TexRec g_eff_dirt_texs[4]; /* a puff's four frames */
extern TexRec g_eff_grass_texs[4];

/* Used only inside eff.c. */

/* The quads being filled this frame: the buffer and the next free one.
 * "eff prim" is what the game's message calls them. */
extern POLY_FT4 *g_eff_prims;
extern POLY_FT4 *g_eff_next_prim;

extern s32 *g_eff_particle_random; /* a random number, stirred as it goes */

#endif /* EFF_H */
