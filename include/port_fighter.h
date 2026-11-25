#ifndef PORT_FIGHTER_H
#define PORT_FIGHTER_H

/* The fighters' own models, drawn by the remastered picture instead of the
 * game's primitives (docs/PORT.md, "The fighters' own models").
 *
 * The game goes on drawing its fighters exactly as it did: its body is one
 * mesh skinned across thirteen bones (render_DrawSkinnedMesh), its parts
 * are meshes of the bones that carry them, and every face becomes a
 * primitive that the software GPU draws. Nothing of that changes, so the
 * original picture, the traces and the fight are what they were. What the
 * remastered picture can do instead is leave those primitives out and draw
 * a mesh of its own on the same bones, from a glTF file
 * (src/port/gl/gl_fighter.c). For that it needs, once a frame, each
 * fighter's bones as the game worked them out, and the camera and the
 * lights the game drew him with: this is where they are kept.
 *
 * Nothing here is the game's state: it is what the game drew, written down
 * as the renderer comes to it, and read only by the backend. The hitboxes
 * are not the model's: they are the body points and the weapon's points of
 * fight_data.c, placed on the same bones, which nothing here touches. */

#include "common.h"

#ifndef PORT_ADDRESS_DEFINED
#define PORT_ADDRESS_DEFINED
typedef __UINTPTR_TYPE__ PortAddress;
#endif

/* The game's three model slots (MODEL_SLOT_COUNT), and two on two's two
 * copies of the first two's models (port_duo.h, port_DuoViewModel); and
 * the skeleton every model shares (MODEL_BONE_COUNT, g_model_bones). */
#define PORT_FIGHTER_SLOTS 5
#define PORT_FIGHTER_BONES 21
/* What a fighter may hold or wear in a frame, at most. */
#define PORT_FIGHTER_WEAPONS 8

/* The cloth meshes one model can have (MODEL_CLOTH_PIECES), and the most
 * points one of the game's has (29, M102's). */
#define PORT_FIGHTER_CLOTHS 5
#define PORT_FIGHTER_CLOTH_POINTS 64

typedef struct {
    s16 m[9]; /* a MATRIX's rotation, 1.12 */
    s32 t[3];
} PortFighterMatrix;

/* A cloth as the game drew it (render_DrawCloth): its node, which of the
 * model's meshes it is, and each of its points where the game moved it,
 * in the world, in 128ths of a unit (ClothPoint.pos), in the order of the
 * mesh's vertices, which its faces name. */
typedef struct {
    PortAddress node;
    s32 mesh;
    s32 count;
    s32 point[PORT_FIGHTER_CLOTH_POINTS][3];
} PortFighterCloth;

typedef struct {
    /* The model being drawn (Model.model_id), or -1 where the slot's body
     * was not drawn in the frame being built. */
    s32 model_id;
    /* How many frames the renderer had built when this was written: the
     * one a primitive of this frame is drawn in has the same. */
    u32 serial;
    /* Where the model's nodes are: a primitive whose `id.object` lies here,
     * and that is not a shadow, is a drawing of this model's own body or of
     * one of its parts (port_remaster.h); the cloth's nodes are here too, and
     * are told apart by port_FighterIsCloth. */
    PortAddress nodes_from;
    PortAddress nodes_to;
    /* where the body's mesh is (the model's mesh 0), which a primitive's
     * id.group is a record of */
    PortAddress body_mesh;
    /* Each bone's world matrix, as the game drew it */
    PortFighterMatrix bone[PORT_FIGHTER_BONES];
    /* The view the body was drawn with: the camera's rotation and where it
     * is, and the projection (the coprocessor's H, OFX and OFY) */
    s16 view[9];
    s32 camera[3];
    s32 h;
    s32 ofx;
    s32 ofy;
    /* the lights: their matrix and colours, 1.12, and the colour of the
     * dark, a byte each (LightSetup) */
    s16 light[9];
    s16 color[9];
    s32 back[3];
    /* the scale the renderer drew the body at (render_SetupNode), which
     * its depths and its ordering-table slots are worked out in */
    s32 scale_shift;
    /* his cloths drawn in the frame, whether his body was or not */
    s32 cloth_count;
    PortFighterCloth cloth[PORT_FIGHTER_CLOTHS];
} PortFighterView;

/* A weapon or a thing worn, as drawn this frame: the node, which weapon
 * model it is (enum WeaponModel, or -1), the model of a mod's weapon
 * drawn for it (port_WeaponHeldModel: models/KEY.glb, or NULL), and its
 * world matrix. */
typedef struct {
    PortAddress node;
    s32 weapon;
    const char *model_key;
    u32 serial;
    PortFighterMatrix world;
    s16 light[9];
    s16 color[9];
    s32 back[3];
} PortFighterWeapon;

/* The renderer is about to draw a node that is a fighter's own
 * (port_RemasterNoteFighter): where it is the body of a model, the model
 * is written down; where it is a cloth, its points; where it is a weapon,
 * the weapon. Called only with a backend attached. */
extern void port_FighterNoteNode(PortAddress node);

/* The slot's model as the last frame built drew it. */
extern const PortFighterView *port_GetFighterView(s32 slot);
/* The weapons of the last frame built, and how many. */
extern const PortFighterWeapon *port_GetFighterWeapons(s32 *count);
/* How many frames the renderer has built (PortFighterView.serial). */
extern u32 port_FighterSerial(void);

/* All of the above as it is now, kept (gl_record.h): the getters answer
 * from a copy in their place while a record of the step is drawn after
 * it (port_FighterUseViews; NULL: from what is noted again). The version
 * changes with anything noted. */
typedef struct {
    PortFighterView views[PORT_FIGHTER_SLOTS];
    PortFighterView tiny;
    PortFighterWeapon weapons[PORT_FIGHTER_WEAPONS];
    s32 weapon_count;
    u32 serial;
} PortFighterViews;

extern u32 port_FighterViewsVersion(void);
extern void port_FighterKeepViews(PortFighterViews *views);
extern void port_FighterUseViews(const PortFighterViews *views);
/* What the game has loaded, before it draws any of it: the model in a
 * slot (-1 for none), and the place-th of the weapon models loaded
 * (enum WeaponModel; -1 past the last). A load reads them so that a
 * model's own file is read behind the loading picture, not at the
 * fight's first frame (docs/PORT.md, "Quick loading"). */
extern s32 port_FighterLoadedModel(s32 slot);
extern s32 port_FighterLoadedWeapon(s32 place);
/* ... and the tiny model of the slot's model (g_model_tiny_ids), or -1. */
extern s32 port_FighterLoadedTiny(s32 slot);

/* Whether a node is a piece of cloth, which the game moves and draws
 * itself; a model's file may draw it as a smooth surface through the same
 * points (gl_fighter.c, extras.rb_cloth). */
extern s32 port_FighterIsCloth(PortAddress node);

/* The tiny model of the first-person view (model_tiny.c) as the last frame
 * built drew it: `model_id` is its number (g_model_tiny_ids), or -1 where
 * none was drawn; the bones are its fighter's, the view and the camera
 * the figure's own (g_model_tiny_matrix), and the projection about the
 * figure's corner of the screen. */
extern const PortFighterView *port_GetFighterTinyView(void);
/* Whether a node is a part of the tiny model's body, and not the weapon
 * or the thing it holds. */
extern s32 port_FighterIsTinyBody(PortAddress node);

/* A new frame of primitives is about to be built (port_RemasterNewFrame):
 * what the frame before drew is forgotten. */
extern void port_FighterNewFrame(void);

#endif /* PORT_FIGHTER_H */
