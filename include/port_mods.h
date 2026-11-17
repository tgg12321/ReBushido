#ifndef PORT_MODS_H
#define PORT_MODS_H

/* The native port's mods (docs/MODS.md): folders beside the program, each
 * a mod with a small manifest, whose files stand in for the game's own.
 * One table answers "is there a mod's file for this?" for every kind of
 * thing a mod can replace: replacement textures are its first user
 * (src/port/gl/gl_replace.c), and models, music and stages are to look
 * their files up the same way. The mods are read once, as the program
 * starts; none of this is the game's state. The PlayStation builds never
 * see this header. */

#include "common.h"

/* ---- what a mod holds ---- */

/* The kinds of file, each a folder of a mod's own: `textures/` and so
 * on. A file's key is its name without its ending, in small letters,
 * wherever it is under that folder. */
enum {
    PORT_MOD_TEXTURES, /* PNG pictures that replace the game's textures */
    PORT_MOD_MODELS,   /* the fighters' models, .glb (gl_fighter.h) */
    PORT_MOD_MUSIC,    /* .ogg or .wav pieces of music (port_music.h) */
    PORT_MOD_STAGES,   /* a stage's files (port_discfiles.h) */
    PORT_MOD_SKIES,    /* PNG pictures of a stage's sky (gl_sky.h) */
    PORT_MOD_WEAPONS,  /* a weapon's numbers, .txt (port_weapons.h) */
    PORT_MOD_MOTIONS,  /* a motion file, .bbm (port_discfiles.h) */
    PORT_MOD_KINDS
};

#define PORT_MOD_NAME_SIZE 64
#define PORT_MOD_VERSION_SIZE 32
#define PORT_MOD_KEY_SIZE 64
/* The manifest's file in a mod's folder. A folder without one is a mod
 * all the same, named for its folder. */
#define PORT_MOD_MANIFEST "mod.txt"

/* A mod's manifest: lines of `NAME = VALUE`, `#` beginning a comment. */
typedef struct {
    char name[PORT_MOD_NAME_SIZE];
    char version[PORT_MOD_VERSION_SIZE];
    /* Where it comes in the order: a mod later in it overrides an earlier
     * one's file of the same key. 0 if it does not say; mods of the same
     * order go by their folders' names. */
    s32 order;
    s32 is_enabled; /* `enabled = no` leaves it out */
} PortModManifest;

/* The manifest `text` of `length` bytes into `manifest`, over what it
 * held (the caller sets the defaults): how many lines it did not
 * understand, the first of them told in `problem` (`room` bytes, the 0
 * included). A name the manifest does not know (`author`, `about`) is
 * not counted. */
extern s32 port_ModParseManifest(
    const char *text, u32 length, PortModManifest *manifest, char *problem,
    u32 room);

/* ---- the run's mods ---- */

/* The folder the mods are in, from the command line (`--mods DIR`), or
 * `off` for none. With nothing said, `mods/` beside the program. */
extern void port_ModsSetFolder(const char *folder);
/* The mod in this folder of the mods' folder is read and left out, as if
 * its manifest said `enabled = no`: the window's `english = off` and
 * `ui = off` for the mods it ships (docs/MODS.md). Each call adds one. */
extern void port_ModsLeaveOut(const char *folder);
/* Where a texture of the game's is written as it is first drawn, for a
 * replacement to be made from (`--dump-textures DIR`); NULL for none. */
extern void port_ModsSetDumpFolder(const char *folder);
extern const char *port_ModsGetDumpFolder(void);
/* Reads every mod in the folder, in its order, and says what it found in
 * the run's log. `program` is the program's own path. */
extern void port_ModsStart(const char *program);
/* How many files of a kind the mods give, after the overriding. */
extern s32 port_ModsCount(s32 kind);
/* The path of the file of this kind and key, from the last mod in the
 * order that has one, or NULL. The key is as above: small letters, no
 * ending. */
extern const char *port_ModFind(s32 kind, const char *key);
/* The `n`th file of a kind, from 0, in the order the mods were read:
 * TRUE and its key and path, or FALSE past the last. */
extern s32 port_ModFileOf(
    s32 kind, s32 n, const char **key, const char **path);
/* The files of the kinds that change how the game plays (stages, weapons,
 * motions), each its kind, its key and every byte of it, as one number
 * that does not depend on the order they were found in: two netplay
 * peers must have the same (port_WeaponsSessionHash). 0 with none. */
extern u64 port_ModsGameplayHash(void);
/* A file's size, and up to `room` bytes of it into `dest`: how many were
 * read (0 where it cannot be). */
extern u32 port_ModFileSize(const char *path);
extern u32 port_ModReadFile(const char *path, void *dest, u32 room);

/* ---- the key of a texture ---- */

/* The texels a primitive reads, as the GPU reads them: the page's corner
 * and its depth, the colour table, the rectangle of texture points, and
 * the texture window (a point is anded with the one and ored with the
 * other, each way). A picture the game copied into video memory itself
 * is the 16-bit texture of its own rectangle, its page at its corner. */
typedef struct {
    s32 bits; /* PORT_MOD_TEXELS_... */
    s32 page_x;
    s32 page_y;
    s32 clut_x;
    s32 clut_y;
    s32 u;
    s32 v;
    s32 w;
    s32 h;
    s32 and_u;
    s32 or_u;
    s32 and_v;
    s32 or_v;
} PortTextureRegion;

/* A texel's depth, as the drawing mode numbers it. */
enum { PORT_MOD_TEXELS_4 = 0, PORT_MOD_TEXELS_8 = 1, PORT_MOD_TEXELS_16 = 2 };

/* The key: a 64-bit hash of what the region looks like, the texels' own
 * values in order and then the colour table they are seen through (16
 * colours or 256), with the size and the depth. It is not of where any of
 * it is in video memory, so a texture keeps its key wherever the game
 * puts it and whichever table slot it gets. */
extern u64 port_TextureKey(const u16 *vram, const PortTextureRegion *region);
/* The key as a file's name has it, without an ending:
 * `WxH-D-HHHHHHHHHHHHHHHH`, D the depth (4, 8 or 16), the hash in 16
 * small hexadecimal digits. Needs PORT_MOD_KEY_SIZE bytes. */
extern void port_TextureKeyName(
    u64 key, const PortTextureRegion *region, char *out, u32 room);
/* The key of the texels alone, the colour table left out: the same for
 * a texture whatever its table is doing. Not a file's name; it finds a
 * texture faded through its table (port_TextureFadeOf). */
extern u64 port_TextureTexelKey(
    const u16 *vram, const PortTextureRegion *region);

/* A fade (docs/MODS.md, "Fades"): `count` colours of 15 bits now, each
 * its colour of `base` with each part multiplied by one number of its
 * own, as the game's fades write them (tex_TintClut, the boot logo's
 * pictures). Then TRUE, and in `tint` the three numbers, red in the low
 * byte, PORT_MOD_TINT_ONE each for as it was; else FALSE. A colour of
 * nothing (0) stays nothing, and a texel's top bit stays as it was. */
extern s32 port_TextureFadeOf(
    const u16 *base, const u16 *now, u32 count, u32 *tint);
#define PORT_MOD_TINT_ONE 128
#define PORT_MOD_TINT_MAX 255
#define PORT_MOD_TINT_SHIFT 8
#define PORT_MOD_TINT_SAME                                                     \
    (PORT_MOD_TINT_ONE | (PORT_MOD_TINT_ONE << PORT_MOD_TINT_SHIFT) |          \
     (PORT_MOD_TINT_ONE << (2 * PORT_MOD_TINT_SHIFT)))
#define PORT_MOD_PARTS 3
#define PORT_MOD_PART_BITS 5
#define PORT_MOD_PART_MAX 31
#define PORT_MOD_MIX_BIT 0x8000
/* how many of a part's 31 steps a faded colour may be from its base's
 * times the scale: the game rounds each step down */
#define PORT_MOD_FADE_SLACK 2

/* The colour the GPU would draw texel (u, v) of the region in, counted
 * from the region's own corner, the window applied: a 16-bit pixel of
 * video memory, 0 for nothing drawn. */
extern u16 port_TextureTexel(
    const u16 *vram, const PortTextureRegion *region, s32 u, s32 v);

/* ---- the key of a page of the scene ---- */

/* A whole texture page of the stage seen through one colour table
 * (docs/MODS.md, "The scene's textures by page"): its corner, its depth,
 * the table, and which of video memory's pixels are the stage's own --
 * one byte a pixel, PORT_VRAM_WIDTH by PORT_VRAM_HEIGHT, not 0 for one --
 * or NULL for every pixel. */
typedef struct {
    s32 bits; /* PORT_MOD_TEXELS_... */
    s32 page_x;
    s32 page_y;
    s32 clut_x;
    s32 clut_y;
    const u8 *own;
} PortTexturePage;

/* A page is 256 texels each way. */
#define PORT_MOD_PAGE_SIDE 256

/* Whether the page lies inside video memory, not wrapping round its
 * right or bottom edge: only such a page has a key. */
extern s32 port_TexturePageFits(const PortTexturePage *page);
/* Its key: FNV-1a over the depth's code, the page's width and height
 * (256 each, two bytes, the low first), every texel row by row -- its
 * index of one byte, or its pixel of two bytes, the low first; 0 where the
 * texel's pixel of video memory is not the stage's own -- and then the
 * colour table, 16 colours or 256 of two bytes (none for 16 bits). */
extern u64 port_TexturePageKey(const u16 *vram, const PortTexturePage *page);
/* ... as a file's name has it, without an ending:
 * `page-256x256-D-HHHHHHHHHHHHHHHH`. Needs PORT_MOD_KEY_SIZE bytes. */
extern void port_TexturePageKeyName(
    u64 key, const PortTexturePage *page, char *out, u32 room);
/* The pixel of video memory texel `u` of a row of the page is in, counted
 * from the page's corner: a texel of 4 bits is a quarter of one. */
extern s32 port_TexturePagePixel(const PortTexturePage *page, s32 u);

#endif /* PORT_MODS_H */
