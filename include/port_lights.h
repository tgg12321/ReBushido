#ifndef PORT_LIGHTS_H
#define PORT_LIGHTS_H

/* Where the remastered picture's lights come from (docs/MODS.md,
 * "Lights"; docs/PORT.md, "Where the lights come from"): a stage's file of
 * lights that every mod can add to, and what the game itself lights up --
 * its stage's flames, the flash where blades meet, the glow of a spark.
 *
 * This is the data and the parser, and the notes the game's effects make
 * as they run (src/main/eff.c, under RB_REMASTER_VIEW). What turns them
 * into a frame's list of lights for the renderer is the picture's
 * (src/port/gl/gl_light_list.c, into gl_lights.h). Nothing here is the
 * game's state (config/port_state.txt: outside): the notes are read and
 * never put back, and nothing the game computes reads any of it. */

#include "common.h"

/* The format a light file says it is (`format = 1`). */
#define PORT_LIGHTS_FORMAT 1
/* The most lights a stage's files give, all the mods together. */
#define PORT_LIGHTS_MOST 64
#define PORT_LIGHT_NAME_SIZE 32

/* What a light is fastened to (`on = ...`). Its `at` is then in that
 * thing's own space: the stage's is the world; a fighter's is his bone's
 * (or, with no bone, the middle of his body, in the world's axes); a
 * weapon's and a piece's are their own, turned with them; a flame's is the
 * world's axes from the flame's place. */
enum {
    PORT_LIGHT_ON_STAGE = 0,
    PORT_LIGHT_ON_FIGHTER = 1, /* index 0 or 1: the first or second fighter */
    PORT_LIGHT_ON_WEAPON = 2,  /* index: the n-th weapon drawn this frame */
    PORT_LIGHT_ON_PIECE = 3,   /* index: the stage's piece (gl_stage.c) */
    PORT_LIGHT_ON_FLAME = 4,   /* index: the stage's flame; -1 every one */
    PORT_LIGHT_ON_KINDS
};

/* A light's kind (the same numbers as gl_lights.h's). */
enum { PORT_LIGHT_POINT = 0, PORT_LIGHT_SPOT = 1 };

/* What a light keeps of the file's numbers. A fraction is in thousandths:
 * `intensity = 1.5` is 1500. */
#define PORT_LIGHT_ONE 1000

typedef struct {
    char name[PORT_LIGHT_NAME_SIZE];
    s32 kind;     /* PORT_LIGHT_POINT or PORT_LIGHT_SPOT */
    s32 on;       /* PORT_LIGHT_ON_... */
    s32 on_index; /* which fighter, weapon, piece or flame */
    s32 on_bone;  /* a fighter's bone, or -1 for the middle of his body */
    s32 at[3];    /* where, in world units, y down */
    u8 color[3];  /* red, green, blue, as a screen shows them (0..255) */
    u8 pad;
    s32 intensity;    /* thousandths: 1000 is the stage's sun at full colour */
    s32 radius;       /* world units: how far it reaches */
    s32 direction[3]; /* a spot's: which way it points */
    s32 cone_inner;   /* a spot's half-angles, degrees */
    s32 cone_outer;
    s32 flicker; /* thousandths: how much of it flickers */
    s32 shadow;  /* whether it asks to cast shadows */
} PortLightDef;

typedef struct {
    s32 count;
    PortLightDef light[PORT_LIGHTS_MOST];
} PortLightSet;

/* A light as a file starts it: a white point light of intensity 0.5 and
 * radius 3,000 at the stage's 0, 0, 0, a spot's pointing down. */
extern void port_LightDefault(
    PortLightDef *light, const char *name, u32 length);

/* One light file's `text`, of `length` bytes, applied over `set` (the
 * mods before it gave what it holds): its lights added, or a light of
 * the same name changed; `clear` forgets every light so far, `remove
 * NAME` one. Returns how many lines it could not use, the first of them
 * told in `problem` (`room` bytes with the 0); a line not understood
 * changes nothing. */
extern s32 port_LightsParse(
    const char *text, u32 length, PortLightSet *set, char *problem, u32 room);

/* The key of stage `stage`'s light files: `stage_NN` (as the skies'). */
extern void port_LightsStageKey(s32 stage, char *key, u32 room);

/* A custom arena standing in for a stage names a light file of its own
 * (`lights = KEY`): port_ArenaLightsKey() of include/port_arenas.h. */

/* Every mod's light file of the stage (or of the arena standing in for
 * it), in the mods' order, made into one
 * set, which is kept until another stage is asked for; problems go to
 * the run's log. NULL for no stage (stage < 0). */
extern const PortLightSet *port_LightsOfStage(s32 stage);

/* ---- what the game lights up (src/main/eff.c) ---- */

/* The stage's flames (eff.h, Flame): at most five. */
#define PORT_LIGHTS_FLAMES 5
/* The effects' single quads that glow (eff.h, enum Flash). */
#define PORT_LIGHTS_GLOWS 18

typedef struct {
    s32 at[3];
    s32 kind; /* FLAME_FIRE, FLAME_TORCH_TALL, FLAME_TORCH, FLAME_FIRE_SMALLER
               */
} PortLightFlame;

typedef struct {
    s32 flash; /* which (enum Flash) */
    s32 at[3];
} PortLightGlow;

typedef struct {
    /* bumped by every frame of the effects (eff_Update) */
    u32 serial;
    /* the stage's flames, the stage they were noted on (-1 none) */
    s32 flame_stage;
    s32 flame_count;
    PortLightFlame flame[PORT_LIGHTS_FLAMES];
    /* the game's own point light, which lights the fighters for a few
     * frames after a clash, a hit, a lock or a shot (eff.c, EFF_LIT): on,
     * where, its colour 0xRRGGBB, the frames it has left, and a number
     * that changes with each new one */
    s32 flash_on;
    s32 flash_at[3];
    s32 flash_color;
    s32 flash_frames;
    u32 flash_event;
    /* the glowing quads drawn in the frame */
    s32 glow_count;
    PortLightGlow glow[PORT_LIGHTS_GLOWS];
} PortLightsGame;

/* A frame of the effects begins: the glows of the last are forgotten. */
extern void port_LightsNoteEffects(void);
/* The game's point light at the end of the frame's effects: `frames`
 * left of it (0 when it is out). */
extern void port_LightsNoteFlash(const void *position, s32 color, s32 frames);
/* A glowing quad of the effects (eff_DrawFlash): which, and where. */
extern void port_LightsNoteGlow(s32 flash, const void *position);
/* The stage's flames, the game's list (a Flame each, ending at one whose
 * x has its low bit clear), as they are drawn. */
extern void port_LightsNoteFlames(const void *flames);

extern const PortLightsGame *port_LightsGame(void);

#endif /* PORT_LIGHTS_H */
