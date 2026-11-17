#ifndef GL_REPLACE_H
#define GL_REPLACE_H

/* Replacement textures in the remastered picture (docs/MODS.md): a
 * textured primitive whose texels a mod has a picture for is drawn from
 * that picture, smoothed, at whatever size it was made, instead of from
 * the mirror of video memory; and a picture the game copies into video
 * memory itself (a loading screen, the top menu's) is stretched from a
 * mod's picture instead of from what it copied. Both are found by the
 * key of what is drawn (port_mods.h), so a replacement holds wherever
 * the game puts the texture. With `--dump-textures` every texture that
 * has none is written out once, as it is first drawn, under the name a
 * replacement of it is to have.
 *
 * With no mod's textures and nothing to write out, every function here
 * answers at once and the picture is what it was. */

#include "common.h"
#include "port_remaster.h"

/* The texture unit a replacement is bound to while it is drawn: the
 * mirror of video memory is the first, the ground's slots the second. */
#define PORT_GL_REPLACE_UNIT 2

/* A replacement, for one primitive: its texture, and the rectangle of
 * texture points of the primitive's page it stands for. */
typedef struct {
    u32 texture;
    s32 u;
    s32 v;
    s32 w;
    s32 h;
} PortGlReplacement;

/* Looks up the GL functions it uses: NULL, or why it cannot. */
extern const char *port_GlReplaceStart(void *(*getproc)(const char *name));
/* The replacement of a textured primitive: 1 and `out`, or 0 to draw it
 * from video memory as ever. The first time a texture is seen with no
 * replacement it is written out, where that was asked for. */
extern s32 port_GlReplaceFind(
    const PortRemasterPrim *prim, PortGlReplacement *out);
/* The replacement of the rectangle of video memory the game has just
 * written (a fill, a copy, a load): its texture, or 0. `display_x` and
 * `display_w` are the columns the display shows: what is written there
 * and is not of one colour is a picture, and is written out as one. */
extern u32 port_GlReplaceFindPicture(
    s32 x, s32 y, s32 w, s32 h, s32 display_x, s32 display_w);

#endif /* GL_REPLACE_H */
