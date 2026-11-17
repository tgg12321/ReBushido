#ifndef PORT_GL_FIGHTER_FILE_H
#define PORT_GL_FIGHTER_FILE_H

/* A fighter's model as a file: glTF 2.0's binary form (.glb), read into
 * plain arrays for the card (gl_fighter.c). Nothing here draws or talks to
 * GL, so the host tests can run it.
 *
 * What a file must be (tools/fighter_export.py writes such files, and
 * tools/fighter_conform.py makes one of whatever Blender or another tool
 * wrote):
 *   - one skin of the 21 bones, its joints named `bone00` to `bone20`,
 *     with an inverse bind matrix for each, in the game's own units and
 *     axes (y down): a joint's matrix is the game's world matrix of that
 *     bone times its inverse bind matrix;
 *   - one mesh, of any number of primitives of triangles, each with
 *     POSITION, NORMAL, TEXCOORD_0, JOINTS_0 and WEIGHTS_0, indexed, and
 *     a material whose base colour is a PNG in the file;
 *   - `extras.rb_fighter` 1 at the top.
 * A vertex whose normal is nought is not lit (the game's unlit faces). */

#include "common.h"

#define PORT_FIGHTER_FILE_JOINTS 21
/* The most primitives (pieces with a texture of their own) a model has. */
#define PORT_FIGHTER_FILE_PARTS 16

typedef struct {
    float place[3];
    float normal[3];
    float uv[2];
    float weight[4];
    u8 joint[4]; /* the game's bone numbers */
} PortFighterVertex;

typedef struct {
    s32 first; /* into the indices */
    s32 count;
    s32 texture; /* into the textures, or -1 */
} PortFighterPart;

typedef struct {
    s32 w;
    s32 h;
    u8 *rgba;
} PortFighterTexture;

typedef struct {
    s32 vertex_count;
    PortFighterVertex *vertices;
    s32 index_count;
    u32 *indices;
    s32 part_count;
    PortFighterPart part[PORT_FIGHTER_FILE_PARTS];
    s32 texture_count;
    PortFighterTexture texture[PORT_FIGHTER_FILE_PARTS];
    /* each bone's inverse bind matrix, a column after a column, and
     * whether the file has it */
    float inverse_bind[PORT_FIGHTER_FILE_JOINTS][16];
    s32 has_joint[PORT_FIGHTER_FILE_JOINTS];
} PortFighterFile;

/* Reads the file at `path`. NULL, or why it could not; what was read is
 * freed either way on failure. */
extern const char *port_FighterFileRead(const char *path, PortFighterFile *out);
/* ... the same of a file already in memory */
extern const char *port_FighterFileParse(
    const u8 *bytes, u32 size, PortFighterFile *out);
extern void port_FighterFileFree(PortFighterFile *file);

/* A PNG into eight-bit red, green, blue and alpha (malloc'd): 8-bit
 * grey, grey and alpha, colour, colour and alpha, and a palette; not
 * interlaced. NULL, or why it could not. */
extern const char *port_FighterPngDecode(
    const u8 *bytes, u32 size, s32 *w, s32 *h, u8 **rgba);

#endif /* PORT_GL_FIGHTER_FILE_H */
