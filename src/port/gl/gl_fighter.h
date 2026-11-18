#ifndef PORT_GL_FIGHTER_H
#define PORT_GL_FIGHTER_H

/* The fighters drawn from models of their own (docs/PORT.md, "The
 * fighters' own models"): a glTF file for a model, skinned on the 21 bones
 * of the skeleton every model shares, drawn on the bones the game worked
 * out for the frame, in place of the primitives the game made of the
 * body and its parts. The game draws its fighters exactly as before and
 * the software GPU still draws every one of their primitives, so the
 * original picture and the traces are untouched; the backend leaves those
 * primitives out of the remastered picture and draws the model instead.
 *
 * Where it is drawn: the fighters of a frame are drawn together, with a
 * depth buffer of their own so that each hides himself and the other as a
 * solid would, into a layer the size of the picture, at the place of the
 * first of their primitives in the order the game paints in. The layer is
 * then laid over the picture, held against the ground's mesh by the
 * ordering table's slots as a fighter's own primitives are (gl_draw.c,
 * port_GlIsOverGround), and its depth written where the ground's is, so
 * that what the scene paints after it is held against the fighter too.
 *
 * Only what has a file is replaced: a model with none, a cloth its file
 * does not have, the shadows and a weapon with none are the game's own
 * primitives as before. A cloth its file has is drawn in the layer as a
 * smooth surface through the game's own points, kept out of the body
 * (gl_cloth.h); the first-person view's tiny figure is drawn from its own
 * file into a layer of its own. The hitboxes are not the model's and are
 * untouched (port_fighter.h). */

#include "common.h"
#include "port_remaster.h"

/* Makes the shaders. NULL, or why the fighters cannot be drawn this way;
 * the picture goes on without it. */
extern const char *port_GlFighterStart(void *(*getproc)(const char *name));

/* On or off (`fighter_models`): off draws the game's own primitives. */
extern void port_GlFighterSetOn(s32 on);
extern s32 port_GlFighterIsOn(void);
/* Whether each place of a cloth kept out of the body is looked at again
 * to count the ones still inside (PortGlFighterCounts.cloth_left_inside):
 * the prover's log does; a player's picture has no use for it. */
extern void port_GlFighterCountCloth(s32 on);

/* Where the models are looked for: `<folder>/<file>.glb`, the file being
 * the model's own name on the disc (M000 to M306). NULL, which is how it
 * starts, looks in the mods' `models/` folders (port_ModFind, key
 * `m100`; docs/MODS.md). */
extern void port_GlFighterSetFolder(const char *folder);

/* While the game loads: the files of the models it has loaded so far,
 * the fighters' and the weapons', read and given to the card now, behind
 * the loading picture, and not at the first frame that draws them
 * (docs/PORT.md, "Quick loading"). Each is read once. */
extern void port_GlFighterPreload(void);

/* A new frame of primitives is about to come (gl_draw.c, port_GlBegin). */
extern void port_GlFighterNewFrame(void);

/* Whether this primitive is a drawing of a fighter whose own model is
 * drawn instead, in which case it is not to be drawn. The first such
 * primitive of a frame is where the fighters are drawn. */
extern s32 port_GlFighterTakes(const PortRemasterPrim *prim);

/* Whether the primitive port_GlFighterTakes was last given, and did not
 * take, is a cloth of a fighter drawn from a model of his own; the layer
 * is laid at the first of the fighters' primitives or cloths, so it is
 * there before any cloth. The cloth is still the game's, painted by the
 * ordering table, but a pixel of it behind the
 * models is hidden by them (gl_draw.c, `u_fighter_depth`), as one in
 * front of them hides them: the back of a hakama does not show through
 * the legs in front of it. */
extern s32 port_GlFighterHolds(void);

/* The texture of the layer's depth, in the world's units (the far plane
 * where no model is), and where it lies in the picture's buffer (left,
 * bottom, width, height); 0 when no layer is laid in this picture. */
extern s32 port_GlFighterLayerDepth(s32 rect[4]);

/* A picture between two frames (gl_tween.c): the fighters with their
 * bones `phase` of the way (PORT_TWEEN_ONE ths) from the frame on show to
 * the frame after it; -1 when the picture is done. */
extern void port_GlFighterTween(s32 phase);

/* The shadows cast from the models (gl_shadow.c, `shadows cast`): which
 * drawn thing of the frame on show the caster node `object` is, if it is
 * drawn from a model of its own, or -1 (a cloth drawn smooth is its
 * fighter's: his slot's triangles are his body's and then his smooth
 * cloths', numbered on after the body's; a cloth that is the game's own is
 * -1 and casts its own faces); and each triangle of that thing's model as
 * the frame posed it, in the world's own units, given to `emit` with its
 * number. */
extern s32 port_GlFighterCaster(PortAddress object);
/* The fighters' layer of the picture into the alpha of `framebuffer`, the
 * shadows' mask: what a model covers is not the ground, as the game's own
 * faces of a fighter are not (gl_shadow.c). */
extern void port_GlFighterDrawOccluders(u32 framebuffer);
typedef void (*PortGlFighterEmit)(
    void *context, const float p[3][3], s32 index);
extern s32 port_GlFighterCastTriangles(
    s32 caster, PortGlFighterEmit emit, void *context);

/* What a run did, for the prover's log: models loaded, frames the
 * fighters were drawn in, primitives left out, and why the last file
 * could not be read, or NULL. */
typedef struct {
    u32 loaded;
    u32 failed;
    u32 frames;
    u32 left_out;
    u32 triangles;
    u32 tinted; /* drawings whose colour tables the game had changed */
    u32 cloths; /* cloths drawn smooth (gl_cloth.h) */
    /* their surfaces' places, how many the game's moving had inside the
     * model's body, and how many are inside it as drawn */
    u32 cloth_places;
    u32 cloth_inside;
    u32 cloth_left_inside;
} PortGlFighterCounts;

extern const PortGlFighterCounts *port_GlFighterCounts(void);
extern const char *port_GlFighterProblem(void);

/* Each fighter's bones as the last frame drew them, as JSON, into
 * `<folder>/<file>.pose.json`: what tools/fighter_export.py takes as a
 * bind pose (`--pose`). The number written. */
extern s32 port_GlFighterWritePoses(const char *folder);

#endif /* PORT_GL_FIGHTER_H */
