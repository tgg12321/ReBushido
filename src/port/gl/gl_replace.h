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

/* Which of a replacement's pixels mix with what is under them where the
 * primitive is half-transparent. The console asks each texel's top bit;
 * a mod's picture also draws where the game's texture had no texel, so
 * where every texel the game drew agrees, the picture is taken to agree
 * all over (docs/MODS.md, "How it is drawn"). */
enum {
    PORT_GL_MIX_TEXEL = 0, /* the game's texel under each pixel says */
    PORT_GL_MIX_ALL = 1,   /* every pixel mixes */
    PORT_GL_MIX_NONE = 2   /* none does */
};

/* A replacement, for one primitive: its texture, the rectangle of
 * texture points of the primitive's page it stands for, and how it
 * mixes. */
typedef struct {
    u32 texture;
    s32 u;
    s32 v;
    s32 w;
    s32 h;
    s32 mix;  /* PORT_GL_MIX_... */
    u32 tint; /* its colours times this, a fade's (port_TextureFadeOf,
               * port_mods.h): PORT_MOD_TINT_SAME for none */
} PortGlReplacement;

/* Looks up the GL functions it uses: NULL, or why it cannot. */
extern const char *port_GlReplaceStart(void *(*getproc)(const char *name));
/* The replacement of a textured primitive: 1 and `out`, or 0 to draw it
 * from video memory as ever. The first time a texture is seen with no
 * replacement it is written out, where that was asked for. `is_on_show`:
 * the primitive is drawn over the picture on show (a loading screen),
 * or while the display is off, where nothing clears it before it is seen, so
 * the first time its replacement is asked for it is read at once: read beside
 * the game, the game's own texture drawn meanwhile would stay under it (the
 * group battle's card, 2026-11-20). */
extern s32 port_GlReplaceFind(
    const PortRemasterPrim *prim, s32 is_on_show, PortGlReplacement *out);
/* The replacement of the rectangle of video memory the game has just
 * written (a fill, a copy, a load): its texture, or 0, and in `tint` the
 * tint of its fade. `display_x` and `display_w` are the columns the
 * display shows: what is written there and is not of one colour is a
 * picture, and is written out as one. `display_w` is -1 while the display
 * is the movie's, in the 24-bit mode: then only a picture of a whole
 * screen is looked up (the title picture), not the movie's frames. While
 * nothing is shown (`is_dark`: the display off, or the movie's) a picture
 * of a whole screen written anywhere is one to be seen as well. */
extern u32 port_GlReplaceFindPicture(s32 x, s32 y, s32 w, s32 h, s32 display_x,
                                     s32 display_w, s32 is_dark, u32 *tint);

#endif /* GL_REPLACE_H */
