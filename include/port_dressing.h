#ifndef PORT_DRESSING_H
#define PORT_DRESSING_H

/* A stage's dressing (ROADMAP 17; docs/MODS.md, "Dressing"; docs/PORT.md,
 * "The dressing"): what a Remastered Classic stage has that nobody
 * touches -- grass, bushes and reeds stirred by the wind, the fighters and
 * the slashes; water that reflects and ripples; dust, leaves, embers,
 * snow and petals; scenery meshes that stand in for a piece's look. A
 * stage's files of every mod, `dressing/stage_NN.txt`, layered in the mods'
 * order as the light files are.
 *
 * This is the data and the parser. What makes a picture of it is the
 * remastered renderer's (src/port/gl/gl_dressing.c). Nothing here is the
 * game's state (config/port_state.txt: outside): nothing the game computes
 * reads any of it, so a fight, a replay and a session online are the same
 * with it and without it. */

#include "common.h"

/* The format a dressing file says it is (`format = 1`). */
#define PORT_DRESSING_FORMAT 1
/* The most blocks a stage's files give, all the mods together. */
#define PORT_DRESSING_MOST 48
/* The most rectangles a block's `area` and `avoid` lines give, each. */
#define PORT_DRESSING_AREAS 8
#define PORT_DRESSING_NAME_SIZE 32
/* What a fraction is kept in: thousandths (`sway = 1.5` is 1500). */
#define PORT_DRESSING_ONE 1000
/* The most of one block: tufts, bushes, reeds, particles. */
#define PORT_DRESSING_COUNT_MOST 20000
/* The most of the stage's lights a shafts block names (`light`). */
#define PORT_DRESSING_LIGHTS 4

/* A block's kind: `[grass NAME]` and the rest. */
enum {
    PORT_DRESS_GRASS = 0,     /* tufts of blades, short, pushed aside */
    PORT_DRESS_BUSH = 1,      /* a mound of leaves; looks solid unless said */
    PORT_DRESS_REEDS = 2,     /* tall thin blades with a head, pushed aside */
    PORT_DRESS_FLOWERS = 3,   /* grass with a bloom on each blade */
    PORT_DRESS_WATER = 4,     /* a level surface that reflects and ripples */
    PORT_DRESS_PARTICLES = 5, /* dust, leaves, embers, snow, petals */
    PORT_DRESS_MESH = 6,      /* a scenery mesh, a file of the mods' */
    PORT_DRESS_WIND = 7,      /* the stage's wind (one block) */
    PORT_DRESS_SHAFTS = 8, /* light through a door or a window, and its motes */
    PORT_DRESS_KINDS
};

/* What a particles block makes (`kind =`). */
enum {
    PORT_PARTICLES_DUST = 0,      /* kicked up by the fighters' steps */
    PORT_PARTICLES_LEAVES = 1,    /* falling and drifting over an area */
    PORT_PARTICLES_EMBERS = 2,    /* rising from the stage's flames */
    PORT_PARTICLES_SNOW = 3,      /* falling over an area */
    PORT_PARTICLES_PETALS = 4,    /* falling and turning over an area */
    PORT_PARTICLES_FIREFLIES = 5, /* drifting and blinking over an area */
    PORT_PARTICLES_RAIN = 6,      /* streaks falling, splashing on the ground */
    PORT_PARTICLES_SMOKE = 7,     /* rising and spreading from the flames */
    PORT_PARTICLES_MOTES = 8,     /* hanging in the air, seen in the light */
    PORT_PARTICLES_SPRAY = 9,     /* thrown up where the waves break */
    PORT_PARTICLES_MIST = 10,     /* banks drifting low with the wind */
    PORT_PARTICLES_GULLS = 11,    /* circling over an area, flapping, gliding */
    PORT_PARTICLES_KINDS
};

/* Which ground a foliage block grows on (`on =`), by the colour of the
 * stage's own texture under each place: any, green (grass and moss),
 * earth (browns), sand (pale warm), stone (greys), snow (near white), blue
 * (water drawn on the ground: a water block with `on` is laid on the ground's
 * own faces of that colour, `y` above them). */
enum {
    PORT_DRESS_ON_ANY = 0,
    PORT_DRESS_ON_GREEN = 1,
    PORT_DRESS_ON_EARTH = 2,
    PORT_DRESS_ON_SAND = 3,
    PORT_DRESS_ON_STONE = 4,
    PORT_DRESS_ON_SNOW = 5,
    PORT_DRESS_ON_BLUE = 6, /* water's own colours: a stream's bed */
    PORT_DRESS_ON_KINDS
};

/* The times of day a block is shown at: the light files' bits
 * (port_lights.h, PORT_LIGHT_TIME_...); 0 at every time. */

typedef struct {
    char name[PORT_DRESSING_NAME_SIZE];
    s32 kind;    /* PORT_DRESS_... */
    s32 subkind; /* a particles block's PORT_PARTICLES_... */
    /* where, in the world's units: rectangles of x and z, the least
     * first (`area = X0 Z0 X1 Z1`), and rectangles left out (`avoid`) */
    s32 area_count;
    s32 area[PORT_DRESSING_AREAS][4];
    s32 avoid_count;
    s32 avoid[PORT_DRESSING_AREAS][4];
    /* how many: tufts, bushes, particles (`count`) */
    s32 count;
    /* how tall, the least and the most, in world units (`height`); a
     * particle's size (`size`) is height[0] */
    s32 height[2];
    s32 width; /* thousandths of the kind's own (`width`) */
    /* its colour (`color`): has_color 0 takes the ground's under it */
    s32 has_color;
    u8 color[3];
    u8 times; /* PORT_LIGHT_TIME_... bits, 0 always (`times`) */
    u8 pad;
    s32 tint[3]; /* thousandths of each channel (`tint`), 1000 none */
    s32 on;      /* PORT_DRESS_ON_... (`on`) */
    s32 level;   /* thousandths: how level the ground must be (`level`) */
    /* only ground between these heights (`heights = Y0 Y1`, y down) */
    s32 has_heights;
    s32 heights[2];
    s32 solid; /* looks solid: held off where a fighter can go (`solid`) */
    s32 sway;  /* thousandths: how much the wind moves it (`sway`) */
    /* a mesh's or a particles block's place, a water's height (`at`, `y`) */
    s32 at[3];
    s32 turn;  /* a mesh's turn about the upright, degrees (`turn`) */
    s32 scale; /* thousandths (`scale`) */
    s32 piece; /* the stage's piece a mesh stands in for, or -1 (`piece`) */
    /* a mesh set on the ground under its place, `at`'s y above it (`sit`) */
    s32 sit;
    char file[PORT_DRESSING_NAME_SIZE]; /* a mesh's file's key (`file`) */
    /* water: how much of what is under it shows, the size of its waves,
     * in thousandths (`clarity`, `waves`) */
    s32 clarity;
    s32 waves;
    /* particles: how high above the ground they begin (`top`); embers on
     * the stage's flames (`on = flames`) */
    s32 top;
    s32 on_flames;
    /* wind: its strength in thousandths, the way it blows in degrees
     * about the upright, how gusty, in thousandths */
    s32 strength;
    s32 direction;
    s32 gusts;
    /* water: how much of its area is puddles (0 all of it), how much rain
     * rings it, how much foam where it meets the shore, how much it mirrors
     * the stage and the fighters, in thousandths (`puddles`, `rain`,
     * `foam`, `reflect`, `sky`); the way it flows, degrees, and how fast, world
     * units a second (`flow`); its colour where it is shallow (`shallow`,
     * has_shallow 0: worked out from `color`) */
    s32 puddles;
    s32 rain;
    s32 foam;
    s32 reflect;
    s32 sky; /* how much of the sky it mirrors, thousandths (`sky`): none
              * under a roof */
    s32 flow[2];
    s32 has_shallow;
    u8 shallow[3];
    u8 pad2;
    /* shafts: the stage's lights it is the light of, by name (`light`), and
     * how bright it is, in thousandths (`glow`; particles too) */
    s32 light_count;
    char light[PORT_DRESSING_LIGHTS][PORT_DRESSING_NAME_SIZE];
    s32 glow;
} PortDressDef;

typedef struct {
    s32 count;
    PortDressDef block[PORT_DRESSING_MOST];
} PortDressSet;

/* A block as a file starts it, of `kind`, named by `length` letters of
 * `name`. */
extern void port_DressDefault(
    PortDressDef *block, s32 kind, const char *name, u32 length);

/* One dressing file's `text`, of `length` bytes, applied over `set` (the
 * mods before it gave what it holds): its blocks added, or a block of the
 * same kind and name changed; `clear` forgets every block so far, `remove
 * NAME` one. Returns how many lines it could not use, the first of them
 * told in `problem` (`room` bytes with the 0); a line not understood
 * changes nothing. */
extern s32 port_DressingParse(
    const char *text, u32 length, PortDressSet *set, char *problem, u32 room);

/* The key of stage `stage`'s dressing files: `stage_NN`. */
extern void port_DressingStageKey(s32 stage, char *key, u32 room);

/* Every mod's dressing file of the stage, in the mods' order, made into one
 * set, kept until another stage is asked for; problems go to the run's log.
 * NULL for no stage, a custom arena, or a stage fought Classic
 * (port_StageIsDressed). */
extern const PortDressSet *port_DressingOfStage(s32 stage);

/* The kind's word (`grass`, ...), and a particles kind's. */
extern const char *port_DressKindName(s32 kind);
extern const char *port_ParticlesKindName(s32 kind);

#endif /* PORT_DRESSING_H */
