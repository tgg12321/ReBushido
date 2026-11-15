/* Sony's libgpu in the native port, as far as the game calls it and
 * Sony's own C is not compiled (docs/PORT.md, "Sony's libraries in the
 * port"): the module that talks to the GPU (SYS).
 *
 * What only fills in the game's memory is done here as that module does
 * it for the console's ordinary GPU: an ordering table's links, the
 * primitives that change the drawing mode, area and offset. What draws
 * or shows anything is a stub that is counted and dropped: there is no
 * picture until stage 4. Two of those would have read video memory back
 * into the game's (StoreImage): they leave the game's memory as it
 * was. */
#include "common.h"
#include "bb2.h"

/* A primitive's first word: the address of the next in its low 24 bits
 * under its length, and this for the end of the list. */
#define LINK_MASK 0x00FFFFFFU
#define LINK_END 0x00FFFFFFU

/* Video memory is 1024 by 512. */
#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512

/* The GPU's commands that the mode primitives carry. */
#define GPU_DRAW_MODE 0xE1000000U
#define GPU_DRAW_MODE_DITHER 0x200U
#define GPU_DRAW_MODE_ON_DISPLAY 0x400U
#define GPU_DRAW_MODE_TPAGE 0x9FFU
#define GPU_TEXTURE_WINDOW 0xE2000000U
#define GPU_AREA_START 0xE3000000U
#define GPU_AREA_END 0xE4000000U
#define GPU_OFFSET 0xE5000000U
#define GPU_AREA_BITS 10
#define GPU_AREA_MASK 0x3FFU
#define GPU_OFFSET_BITS 11
#define GPU_OFFSET_MASK 0x7FFU

/* ---- what fills in memory ---- */

/* An ordering table of `n` entries, each linked to the one before it and
 * the first ending the list: it is drawn from its last entry. */
u32 *ClearOTagR(u32 *ot, s32 count) {
    s32 i;

    for (i = count - 1; i > 0; i--) {
        ot[i] = (u32)&ot[i - 1] & LINK_MASK;
    }
    if (count > 0) {
        ot[0] = LINK_END;
    }
    return ot;
}

static u32 port_GpuTextureWindow(const RECT *tw) {
    if (tw == NULL) {
        return 0;
    }
    return GPU_TEXTURE_WINDOW | ((((u32)tw->y & 0xFF) >> 3) << 15) |
           ((((u32)tw->x & 0xFF) >> 3) << 10) |
           ((((u32)-tw->h & 0xFF) >> 3) << 5) | (((u32)-tw->w & 0xFF) >> 3);
}

static s32 port_GpuClamp(s32 value, s32 most) {
    return value < 0 ? 0 : (value > most ? most : value);
}

static u32 port_GpuAreaCorner(u32 command, s32 x, s32 y) {
    x = port_GpuClamp(x, VRAM_WIDTH - 1);
    y = port_GpuClamp(y, VRAM_HEIGHT - 1);
    return command | (((u32)y & GPU_AREA_MASK) << GPU_AREA_BITS) |
           ((u32)x & GPU_AREA_MASK);
}

/* The primitive that changes the texture page, the dither, whether the
 * displayed area may be drawn on (dfe), and the texture window. */
void SetDrawMode(DR_MODE *prim, s32 dfe, s32 dtd, s32 tpage, RECT *tw) {
    u32 mode = GPU_DRAW_MODE | ((u32)tpage & GPU_DRAW_MODE_TPAGE);

    if (dtd) {
        mode |= GPU_DRAW_MODE_DITHER;
    }
    if (dfe) {
        mode |= GPU_DRAW_MODE_ON_DISPLAY;
    }
    setlen(prim, 2);
    prim->code[0] = mode;
    prim->code[1] = port_GpuTextureWindow(tw);
}

void SetDrawArea(DR_AREA *prim, RECT *area) {
    setlen(prim, 2);
    prim->code[0] = port_GpuAreaCorner(GPU_AREA_START, area->x, area->y);
    prim->code[1] =
        port_GpuAreaCorner(GPU_AREA_END, (s16)(area->x + area->w - 1),
                           (s16)(area->y + area->h - 1));
}

/* The offsets are read as signed, whatever the prototype says. */
void SetDrawOffset(DR_OFFSET *prim, u16 *ofs) {
    u32 x = (u32)(s32)(s16)ofs[0];
    u32 y = (u32)(s32)(s16)ofs[1];

    setlen(prim, 2);
    prim->code[0] = GPU_OFFSET | ((y & GPU_OFFSET_MASK) << GPU_OFFSET_BITS) |
                    (x & GPU_OFFSET_MASK);
    prim->code[1] = 0;
}

/* ---- what draws or shows: not yet ---- */

s32 ResetGraph(s32 mode) {
    PORT_STUB("ResetGraph (picture)");
    (void)mode;
    return 0; /* the kind of GPU: the ordinary one */
}

s32 SetGraphDebug(s32 level) {
    (void)level;
    return 0;
}

void SetDispMask(s32 mask) {
    PORT_STUB("SetDispMask (picture)");
    (void)mask;
}

/* Waits for the GPU to finish what it was given: here it has nothing. */
s32 DrawSync(s32 mode) {
    (void)mode;
    return 0;
}

s32 ClearImage(RECT *rect, u8 r, u8 g, u8 b) {
    PORT_STUB("ClearImage (picture)");
    (void)rect;
    (void)r;
    (void)g;
    (void)b;
    return 0;
}

s32 LoadImage(RECT *rect, u32 *pixels) {
    PORT_STUB("LoadImage (picture)");
    (void)rect;
    (void)pixels;
    return 0;
}

s32 StoreImage(RECT *rect, u32 *pixels) {
    PORT_STUB("StoreImage (picture: the game's memory is left as it was)");
    (void)rect;
    (void)pixels;
    return 0;
}

s32 MoveImage(RECT *rect, s32 x, s32 y) {
    PORT_STUB("MoveImage (picture)");
    (void)rect;
    (void)x;
    (void)y;
    return 0;
}

DISPENV *PutDispEnv(DISPENV *env) {
    PORT_STUB("PutDispEnv (picture)");
    return env;
}

DRAWENV *PutDrawEnv(DRAWENV *env) {
    PORT_STUB("PutDrawEnv (picture)");
    return env;
}

void DrawOTag(u32 *first) {
    PORT_STUB("DrawOTag (picture)");
    (void)first;
}
