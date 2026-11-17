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
 * A vertex whose normal is nought is not lit (the game's unlit faces).
 *
 * What a file may have besides, which other tools ignore:
 *   - the attributes _RB_TERM0 to _RB_TERM3 (four floats) and _RB_TJOINTS
 *     (four joints): the vertex exactly as the game builds it, the sum of
 *     each joint's bone's world matrix times (x, y, z, h), h being how
 *     much of the bone's place is in it. Where they are missing, the
 *     terms are worked out of the linear blend (each joint's weight times
 *     its inverse bind matrix times the place), which is the same thing;
 *   - `extras.rb_palette`: `cluts`, the colour tables the texture was
 *     decoded through ([x, y, entries] in video memory as the model's
 *     file loads them), `colors`, their colours then (16-bit words), and
 *     `image`, a picture of the same layout as the texture whose red is
 *     the entry of a texel and green the table's number, alpha 255 where
 *     a texel is of a table. With it the game's own changes to those
 *     tables (a tint, a fade) reach the model (gl_fighter.c);
 *   - `extras.rb_weapon`: the file is a weapon's (enum WeaponModel), drawn
 *     on the weapon's world matrix, which every bone is given. */

#include "common.h"

#define PORT_FIGHTER_FILE_JOINTS 21
/* The most primitives (pieces with a texture of their own) a model has. */
#define PORT_FIGHTER_FILE_PARTS 16

typedef struct {
    float place[3];
    float normal[3];
    float uv[2];
    float weight[4];
    float term[4][4]; /* each joint's vector and its share of the place */
    u8 joint[4];      /* the game's bone numbers */
    u8 term_joint[4]; /* ... of the terms */
} PortFighterVertex;

/* The most colour tables a model's texture was decoded through. */
#define PORT_FIGHTER_FILE_CLUTS 64

typedef struct {
    s32 x; /* where the model's file loads it, in video memory */
    s32 y;
    s32 entries; /* 16 or 256 */
    u16 color[256];
} PortFighterClut;

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
    /* rb_palette: the tables, and the picture of entries (w 0 if none) */
    s32 clut_count;
    PortFighterClut clut[PORT_FIGHTER_FILE_CLUTS];
    PortFighterTexture index;
    /* rb_weapon, or -1 */
    s32 weapon;
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
