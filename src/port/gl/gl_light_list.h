#ifndef PORT_GL_LIGHT_LIST_H
#define PORT_GL_LIGHT_LIST_H

/* A frame's lights made from where they come from, for the remastered
 * picture (docs/PORT.md, "Where the lights come from"): the stage's light
 * files and the game's own flames (port_lights.h), each placed on what it
 * is fastened to -- the stage, a fighter's bone, a weapon, a piece of the
 * stage, a flame -- and the flashes and glows of the fight's effects;
 * handed to the renderer's list (gl_lights.h) once a frame.
 *
 * Nothing here changes a value the game computes. */

#include "common.h"

/* The settings (`stage_lights`, `effect_lights`): the stage's own lights
 * (its files, and its flames) and the effects' (the clash's flash, the
 * glows), each on or off. On as the program starts. */
extern void port_GlLightListSetStageOn(s32 on);
extern void port_GlLightListSetEffectsOn(s32 on);

/* A frame has been built (gl_draw.c, the backend's `begin`): its lights
 * are made and handed over. */
extern void port_GlLightListFrame(void);

/* For tools/port_lights.py alone: where the stage's mesh was built
 * (gl_stage.c), its `count` vertices of `stride` bytes, the first
 * `pieces_from` of them the ground's and the rest its pieces'. With
 * `--dump-textures FOLDER`, written with all of video memory into
 * FOLDER/meshes/stage_NN.bin, once a stage; nothing otherwise. */
extern void port_GlLightListDumpMesh(
    const void *vertices, s32 stride, s32 count, s32 pieces_from);

#endif /* PORT_GL_LIGHT_LIST_H */
