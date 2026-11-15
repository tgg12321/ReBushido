/* Sony's libgpu in the native port, as far as the game calls it and
 * Sony's own C is not compiled (docs/PORT.md, "Sony's libraries in the
 * port"): the module that talks to the GPU (SYS), on the port's GPU
 * (port_gpu.h, src/port/raster.c).
 *
 * Each function here gives the GPU the words Sony's gives the console's
 * (src/main/psxsdk/libgpu/sys.c, for the ordinary kind of GPU), and
 * fills in of the game's memory what Sony's fills in: an ordering
 * table's links, the primitives that change the drawing mode, area and
 * offset, the primitive inside a drawing environment, the video mode in
 * a display environment. What Sony's keeps in a queue until the GPU is
 * free is done here at once: the port's GPU has drawn a thing when the
 * call that asked for it returns, so DrawSync never waits. */
#include "common.h"
#include "port_gpu.h"
#include "bb2.h"

/* A primitive's first word: the address of the next in its low 24 bits
 * under its length, and this for the end of the list. */
#define LINK_MASK 0x00FFFFFFU
#define LINK_END 0x00FFFFFFU
#define LINK_LENGTH_SHIFT 24
/* The places primitives may be, and the primitives of one list: more is
 * a list that has no end. */
#define LINK_REGIONS_MAX 8
#define LINK_PRIMITIVES_MAX 0x100000

/* The GPU's commands that the mode primitives carry. */
#define GPU_DRAW_MODE 0xE1000000U
#define GPU_DRAW_MODE_DITHER 0x200U
#define GPU_DRAW_MODE_ON_DISPLAY 0x400U
#define GPU_DRAW_MODE_TPAGE 0x9FFU
#define GPU_DRAW_MODE_STATUS 0x7FFU /* GPUSTAT's bits that are E1's */
#define GPU_TEXTURE_WINDOW 0xE2000000U
#define GPU_AREA_START 0xE3000000U
#define GPU_AREA_END 0xE4000000U
#define GPU_AREA_WHOLE 0x00FFFFFFU
#define GPU_OFFSET 0xE5000000U
#define GPU_MASK_OFF 0xE6000000U
#define GPU_AREA_BITS 10
#define GPU_AREA_MASK 0x3FFU
#define GPU_OFFSET_BITS 11
#define GPU_OFFSET_MASK 0x7FFU
#define GPU_CLEAR_CACHE 0x01000000U
#define GPU_FILL 0x02000000U
#define GPU_RECTANGLE 0x60000000U
#define GPU_COPY 0x80000000U
#define GPU_LOAD 0xA0000000U
#define GPU_STORE 0xC0000000U
#define GPU_COLOR_MASK 0x00FFFFFFU
#define GREEN_SHIFT 8
#define BLUE_SHIFT 16
/* A rectangle is cleared by the fast fill when it starts on a multiple
 * of 64 and is one wide. */
#define FILL_ALIGN_MASK 0x3F
/* The display's commands. */
#define GPU_RESET 0x00000000U
#define GPU_RESET_COMMANDS 0x01000000U
#define GPU_ACKNOWLEDGE 0x02000000U
#define GPU_DISPLAY_ON 0x03000000U
#define GPU_DISPLAY_OFF 0x03000001U
#define GPU_DISPLAY_START 0x05000000U
#define GPU_RANGE_X 0x06000000U
#define GPU_RANGE_Y 0x07000000U
#define GPU_DISPLAY_MODE 0x08000000U
#define GPU_INFO 0x10000000U
#define GPU_INFO_AREA_START 3
#define GPU_INFO_AREA_END 4
#define GPU_INFO_OFFSET 5
#define GPU_RANGE_X_BITS 12
#define GPU_RANGE_X_MASK 0xFFFU
#define DISPLAY_MODE_PAL 0x08
#define DISPLAY_MODE_RGB24 0x10
#define DISPLAY_MODE_INTERLACED 0x20
#define DISPLAY_MODE_TALL 0x24
#define DISPLAY_MODE_368 0x40
/* The screen's edges in the GPU's units, and how far they may be put. */
#define SCREEN_LEFT 0x260
#define SCREEN_UNITS_PER_PIXEL 10
#define SCREEN_WIDTH_UNITS 2560
#define SCREEN_LEFT_MIN 500
#define SCREEN_RIGHT_MAX 3290
#define SCREEN_WIDTH_MIN 0x50
#define SCREEN_TOP_NTSC 0x10
#define SCREEN_TOP_PAL 0x13
#define SCREEN_LINES 240
#define SCREEN_TOP_MAX_NTSC 256
#define SCREEN_TOP_MAX_PAL 310
#define SCREEN_BOTTOM_MAX_NTSC 258
#define SCREEN_BOTTOM_MAX_PAL 312
/* The widest display of each of four of the five widths (anything
 * wider is 640), and the tallest that is not interlaced. */
#define WIDEST_256 280
#define WIDEST_320 352
#define WIDEST_368 400
#define WIDEST_512 560
#define TALLEST_NTSC 256
#define TALLEST_PAL 288
/* ResetGraph's modes, in the low three bits: 0 resets the GPU and the
 * library, 1 stops what is being drawn, 3 resets the library alone, 5
 * is 0 without Sony's message, which the port does not print. */
#define RESET_MODE_MASK 7
#define RESET_LIBRARY 3
#define RESET_QUIETLY 5
#define ENV_UNKNOWN 0xFF  /* what an environment not yet put is filled with */
#define DR_ENV_SETTINGS 6 /* the six settings of a drawing environment */

typedef struct {
    u32 address;
    u32 size;
} PortLinkRegion;

static PortLinkRegion g_port_link_regions[LINK_REGIONS_MAX];
static s32 g_port_link_region_count;
/* The environments last put, as Sony's keeps them: PutDispEnv sends the
 * screen's edges and the mode only when they are not these. */
static DRAWENV g_port_draw_env;
static DISPENV g_port_disp_env;

/* ---- what fills in memory ---- */

/* An ordering table of `n` entries, each linked to the one before it and
 * the first ending the list: it is drawn from its last entry. */
u32 *ClearOTagR(u32 *ot, s32 count) {
    s32 i;

    for (i = count - 1; i > 0; i--) {
        ot[i] = (u32)ADDR(&ot[i - 1]) & LINK_MASK;
    }
    if (count > 0) {
        ot[0] = LINK_END;
    }
    /* the frame the remastered renderer's side channel is writing down
     * begins here: the game clears this frame's table and then builds
     * its primitives (port_remaster.h) */
    port_RemasterNewFrame();
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
    x = port_GpuClamp(x, PORT_VRAM_WIDTH - 1);
    y = port_GpuClamp(y, PORT_VRAM_HEIGHT - 1);
    return command | (((u32)y & GPU_AREA_MASK) << GPU_AREA_BITS) |
           ((u32)x & GPU_AREA_MASK);
}

static u32 port_GpuDrawMode(s32 dfe, s32 dtd, s32 tpage) {
    u32 mode = GPU_DRAW_MODE | ((u32)tpage & GPU_DRAW_MODE_TPAGE);

    if (dtd) {
        mode |= GPU_DRAW_MODE_DITHER;
    }
    if (dfe) {
        mode |= GPU_DRAW_MODE_ON_DISPLAY;
    }
    return mode;
}

static u32 port_GpuOffset(s32 x, s32 y) {
    return GPU_OFFSET | (((u32)y & GPU_OFFSET_MASK) << GPU_OFFSET_BITS) |
           ((u32)x & GPU_OFFSET_MASK);
}

/* The primitive that changes the texture page, the dither, whether the
 * displayed area may be drawn on (dfe), and the texture window. */
void SetDrawMode(DR_MODE *prim, s32 dfe, s32 dtd, s32 tpage, RECT *tw) {
    setlen(prim, 2);
    prim->code[0] = port_GpuDrawMode(dfe, dtd, tpage);
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
    setlen(prim, 2);
    prim->code[0] = port_GpuOffset((s16)ofs[0], (s16)ofs[1]);
    prim->code[1] = 0;
}

/* A rectangle's two words as the GPU takes them: x under y, the width
 * under the height. */
static u32 port_GpuPlace(const RECT *rect) {
    return ((u32)(u16)rect->y << 16) | (u16)rect->x;
}

static u32 port_GpuSize(const RECT *rect) {
    return ((u32)(u16)rect->h << 16) | (u16)rect->w;
}

/* The primitive inside a drawing environment (Sony's SetDrawEnv2): the
 * area, the offset, the mode, the texture window, the mask off, and
 * when the environment clears its area a fill of it: the fast one where
 * the area starts on a multiple of 64 and is one wide, a drawn
 * rectangle where it does not. Returns how many words it is. */
static s32 port_FillDrawEnv(DRAWENV *env) {
    DR_ENV *prim = &env->dr_env;
    u32 color =
        ((u32)env->b0 << BLUE_SHIFT) | ((u32)env->g0 << GREEN_SHIFT) | env->r0;
    s32 count = DR_ENV_SETTINGS;
    RECT clip;

    prim->code[0] =
        port_GpuAreaCorner(GPU_AREA_START, env->clip.x, env->clip.y);
    prim->code[1] =
        port_GpuAreaCorner(GPU_AREA_END, (s16)(env->clip.w + env->clip.x - 1),
                           (s16)(env->clip.y + env->clip.h - 1));
    prim->code[2] = port_GpuOffset(env->ofs[0], env->ofs[1]);
    prim->code[3] = port_GpuDrawMode(env->dfe, env->dtd, env->tpage);
    prim->code[4] = port_GpuTextureWindow(&env->tw);
    prim->code[DR_ENV_SETTINGS - 1] = GPU_MASK_OFF;
    if (env->isbg != 0) {
        clip.x = env->clip.x;
        clip.y = env->clip.y;
        clip.w = (s16)port_GpuClamp(env->clip.w, PORT_VRAM_WIDTH - 1);
        clip.h = (s16)port_GpuClamp(env->clip.h, PORT_VRAM_HEIGHT - 1);
        if ((clip.x & FILL_ALIGN_MASK) || (clip.w & FILL_ALIGN_MASK)) {
            /* drawn, so from the offset */
            clip.x -= env->ofs[0];
            clip.y -= env->ofs[1];
            prim->code[count++] = GPU_RECTANGLE | color;
        } else {
            prim->code[count++] = GPU_FILL | color;
        }
        prim->code[count++] = port_GpuPlace(&clip);
        prim->code[count++] = port_GpuSize(&clip);
    }
    setlen(prim, count);
    return count;
}

/* ---- ordering tables ---- */

void port_AddLinkRegion(u32 address, u32 size) {
    if (g_port_link_region_count == LINK_REGIONS_MAX) {
        port_Fail("rebushido: more than %d places for primitives\n",
                  LINK_REGIONS_MAX);
    }
    g_port_link_regions[g_port_link_region_count].address = address;
    g_port_link_regions[g_port_link_region_count].size = size;
    g_port_link_region_count++;
}

/* The primitive a link names: the one place among those named whose
 * address has these low 24 bits. */
static const u32 *port_FollowLink(u32 link) {
    const u32 *found = NULL;
    s32 i;

    for (i = 0; i < g_port_link_region_count; i++) {
        const PortLinkRegion *region = &g_port_link_regions[i];
        /* how far the link is past the region's start, in 24 bits */
        u32 offset = (link - region->address) & LINK_MASK;

        if (offset < region->size) {
            if (found != NULL) {
                port_Fail("rebushido: the link 0x%06X is a primitive in two "
                          "places of memory\n",
                          link);
            }
            found = (const u32 *)ADDR(region->address + offset);
        }
    }
    if (found == NULL) {
        port_Fail("rebushido: the link 0x%06X is a primitive in no memory "
                  "the port knows\n",
                  link);
    }
    return found;
}

/* Draws one primitive: its words after the first go to the GPU, and its
 * link is not followed. */
void DrawPrim(void *first) {
    const u32 *prim = first;
    u32 length = prim[0] >> LINK_LENGTH_SHIFT;
    u32 i;

    for (i = 1; i <= length; i++) {
        port_WriteGp0(prim[i]);
    }
}

/* Draws a list of primitives: each one's words after the first go to
 * the GPU, and its link names the next. */
void DrawOTag(u32 *first) {
    const u32 *prim = first;
    s32 count;

    for (count = 0; count < LINK_PRIMITIVES_MAX; count++) {
        u32 tag = prim[0];
        u32 length = tag >> LINK_LENGTH_SHIFT;
        u32 i;

        /* which primitive these words are, for the remastered
         * renderer's side channel (port_remaster.h) */
        port_RemasterSetPrim((PortAddress)prim);
        for (i = 1; i <= length; i++) {
            port_WriteGp0(prim[i]);
        }
        port_RemasterSetPrim(0);
        if ((tag & LINK_MASK) == LINK_END) {
            return;
        }
        prim = port_FollowLink(tag & LINK_MASK);
    }
    port_Fail("rebushido: an ordering table of more than %d primitives: its "
              "links go round\n",
              LINK_PRIMITIVES_MAX);
}

/* ---- the display ---- */

/* 0 and 3 start the library again, 0 the GPU as well; anything else
 * stops what is being drawn. Returns the kind of GPU: the ordinary
 * one. */
s32 ResetGraph(s32 mode) {
    u32 i;

    switch (mode & RESET_MODE_MASK) {
    case 0:
    case RESET_LIBRARY:
    case RESET_QUIETLY:
        if ((mode & RESET_MODE_MASK) != RESET_LIBRARY) {
            port_WriteGp1(GPU_RESET);
        } else {
            port_WriteGp1(GPU_ACKNOWLEDGE);
            port_WriteGp1(GPU_RESET_COMMANDS);
        }
        for (i = 0; i < sizeof(g_port_draw_env); i++) {
            ((u8 *)&g_port_draw_env)[i] = ENV_UNKNOWN;
        }
        for (i = 0; i < sizeof(g_port_disp_env); i++) {
            ((u8 *)&g_port_disp_env)[i] = ENV_UNKNOWN;
        }
        break;
    default:
        port_WriteGp1(GPU_ACKNOWLEDGE);
        port_WriteGp1(GPU_RESET_COMMANDS);
        break;
    }
    return 0;
}

s32 SetGraphDebug(s32 level) {
    (void)level;
    return 0;
}

void SetDispMask(s32 mask) {
    u32 i;

    if (!mask) {
        for (i = 0; i < sizeof(g_port_disp_env); i++) {
            ((u8 *)&g_port_disp_env)[i] = ENV_UNKNOWN;
        }
    }
    port_WriteGp1(mask ? GPU_DISPLAY_ON : GPU_DISPLAY_OFF);
}

/* Waits for the GPU to finish what it was given: here it has. */
s32 DrawSync(s32 mode) {
    (void)mode;
    return 0;
}

/* Where the display starts, and when they are not the last ones put,
 * the screen's edges and the display's mode. */
DISPENV *PutDispEnv(DISPENV *env) {
    const DISPENV *last = &g_port_disp_env;
    u32 mode = GPU_DISPLAY_MODE;
    s32 left;
    s32 right;
    s32 top;
    s32 bottom;
    s32 is_pal;

    port_WriteGp1(GPU_DISPLAY_START |
                  (((u32)env->disp.y & GPU_AREA_MASK) << GPU_AREA_BITS) |
                  ((u32)env->disp.x & GPU_AREA_MASK));
    if (!(last->screen.x == env->screen.x && last->screen.y == env->screen.y &&
          last->screen.w == env->screen.w && last->screen.h == env->screen.h)) {
        env->pad0 = (u8)GetVideoMode();
        is_pal = env->pad0 != 0;
        left = env->screen.x * SCREEN_UNITS_PER_PIXEL + SCREEN_LEFT;
        top = env->screen.y + (is_pal ? SCREEN_TOP_PAL : SCREEN_TOP_NTSC);
        right = left + (env->screen.w ? env->screen.w * SCREEN_UNITS_PER_PIXEL
                                      : SCREEN_WIDTH_UNITS);
        bottom = top + (env->screen.h ? env->screen.h : SCREEN_LINES);
        left = left < SCREEN_LEFT_MIN
                   ? SCREEN_LEFT_MIN
                   : (left > SCREEN_RIGHT_MAX ? SCREEN_RIGHT_MAX : left);
        right = right < left + SCREEN_WIDTH_MIN
                    ? left + SCREEN_WIDTH_MIN
                    : (right > SCREEN_RIGHT_MAX ? SCREEN_RIGHT_MAX : right);
        if (top < SCREEN_TOP_NTSC) {
            top = SCREEN_TOP_NTSC;
        } else if (top > (is_pal ? SCREEN_TOP_MAX_PAL : SCREEN_TOP_MAX_NTSC)) {
            top = is_pal ? SCREEN_TOP_MAX_PAL : SCREEN_TOP_MAX_NTSC;
        }
        if (bottom < top + 2) {
            bottom = top + 2;
        } else if (bottom >
                   (is_pal ? SCREEN_BOTTOM_MAX_PAL : SCREEN_BOTTOM_MAX_NTSC)) {
            bottom = is_pal ? SCREEN_BOTTOM_MAX_PAL : SCREEN_BOTTOM_MAX_NTSC;
        }
        port_WriteGp1(GPU_RANGE_X |
                      (((u32)right & GPU_RANGE_X_MASK) << GPU_RANGE_X_BITS) |
                      ((u32)left & GPU_RANGE_X_MASK));
        port_WriteGp1(
            GPU_RANGE_Y | (((u32)bottom & GPU_AREA_MASK) << GPU_AREA_BITS) |
            ((u32)top & GPU_AREA_MASK));
    }
    if (last->isinter != env->isinter || last->isrgb24 != env->isrgb24 ||
        last->pad0 != env->pad0 || last->pad1 != env->pad1 ||
        !(last->disp.x == env->disp.x && last->disp.y == env->disp.y &&
          last->disp.w == env->disp.w && last->disp.h == env->disp.h)) {
        env->pad0 = (u8)GetVideoMode();
        if (env->pad0 == 1) {
            mode |= DISPLAY_MODE_PAL;
        }
        if (env->isrgb24) {
            mode |= DISPLAY_MODE_RGB24;
        }
        if (env->isinter) {
            mode |= DISPLAY_MODE_INTERLACED;
        }
        if (env->disp.w > WIDEST_256) {
            if (env->disp.w <= WIDEST_320) {
                mode |= 1;
            } else if (env->disp.w <= WIDEST_368) {
                mode |= DISPLAY_MODE_368;
            } else if (env->disp.w <= WIDEST_512) {
                mode |= 2;
            } else {
                mode |= 3;
            }
        }
        if (env->disp.h > (env->pad0 ? TALLEST_PAL : TALLEST_NTSC)) {
            mode |= DISPLAY_MODE_TALL;
        }
        port_WriteGp1(mode);
    }
    g_port_disp_env = *env;
    return env;
}

/* The drawing environment: its primitive is made in the environment
 * itself and drawn. */
DRAWENV *PutDrawEnv(DRAWENV *env) {
    s32 count = port_FillDrawEnv(env);
    s32 i;

    env->dr_env.tag |= LINK_END;
    for (i = 0; i < count; i++) {
        port_WriteGp0(env->dr_env.code[i]);
    }
    g_port_draw_env = *env;
    return env;
}

/* ---- video memory ---- */

/* One of the GPU's settings, asked for and read back. */
static u32 port_AskGpu(u32 setting) {
    port_WriteGp1(GPU_INFO | setting);
    return port_ReadGpu() & GPU_COLOR_MASK;
}

/* A rectangle of one colour. The fast fill where it starts on a
 * multiple of 64 and is one wide; otherwise drawn, with the whole of
 * video memory for a drawing area and no offset, which are then put
 * back as they were. Either way the mask is left off. */
s32 ClearImage(RECT *rect, u8 r, u8 g, u8 b) {
    u32 color = ((u32)b << BLUE_SHIFT) | ((u32)g << GREEN_SHIFT) | r;
    u32 mode = GPU_DRAW_MODE | (port_GetGpuStatus() & GPU_DRAW_MODE_STATUS);
    u32 area_start;
    u32 area_end;
    u32 offset;

    rect->w = (s16)port_GpuClamp(rect->w, PORT_VRAM_WIDTH - 1);
    rect->h = (s16)port_GpuClamp(rect->h, PORT_VRAM_HEIGHT - 1);
    if ((rect->x & FILL_ALIGN_MASK) || (rect->w & FILL_ALIGN_MASK)) {
        area_start = port_AskGpu(GPU_INFO_AREA_START);
        area_end = port_AskGpu(GPU_INFO_AREA_END);
        offset = port_AskGpu(GPU_INFO_OFFSET);
        port_WriteGp0(GPU_AREA_START);
        port_WriteGp0(GPU_AREA_END | GPU_AREA_WHOLE);
        port_WriteGp0(GPU_OFFSET);
        port_WriteGp0(GPU_MASK_OFF);
        port_WriteGp0(mode);
        port_WriteGp0(GPU_RECTANGLE | color);
        port_WriteGp0(port_GpuPlace(rect));
        port_WriteGp0(port_GpuSize(rect));
        port_WriteGp0(GPU_AREA_START | area_start);
        port_WriteGp0(GPU_AREA_END | area_end);
        port_WriteGp0(GPU_OFFSET | offset);
    } else {
        port_WriteGp0(GPU_MASK_OFF);
        port_WriteGp0(mode);
        port_WriteGp0(GPU_FILL | color);
        port_WriteGp0(port_GpuPlace(rect));
        port_WriteGp0(port_GpuSize(rect));
    }
    return 0;
}

/* How many words a rectangle's pixels are, the rectangle held to video
 * memory's size as Sony's holds it; 0 if it has none. */
static s32 port_ImageWords(RECT *rect) {
    rect->w = (s16)port_GpuClamp(rect->w, PORT_VRAM_WIDTH);
    rect->h = (s16)port_GpuClamp(rect->h, PORT_VRAM_HEIGHT);
    return (rect->w * rect->h + 1) / 2;
}

/* Pixels from the game's memory into video memory. */
s32 LoadImage(RECT *rect, u32 *pixels) {
    s32 words = port_ImageWords(rect);
    s32 i;

    if (words <= 0) {
        return -1;
    }
    port_WriteGp0(GPU_CLEAR_CACHE);
    port_WriteGp0(GPU_LOAD);
    port_WriteGp0(port_GpuPlace(rect));
    port_WriteGp0(port_GpuSize(rect));
    for (i = 0; i < words; i++) {
        port_WriteGp0(pixels[i]);
    }
    return 0;
}

/* ... and from video memory into the game's. */
s32 StoreImage(RECT *rect, u32 *pixels) {
    s32 words = port_ImageWords(rect);
    s32 i;

    if (words <= 0) {
        return -1;
    }
    port_WriteGp0(GPU_CLEAR_CACHE);
    port_WriteGp0(GPU_STORE);
    port_WriteGp0(port_GpuPlace(rect));
    port_WriteGp0(port_GpuSize(rect));
    for (i = 0; i < words; i++) {
        pixels[i] = port_ReadGpu();
    }
    return 0;
}

/* From one place of video memory to another. */
s32 MoveImage(RECT *rect, s32 x, s32 y) {
    if (rect->w == 0 || rect->h == 0) {
        return -1;
    }
    port_WriteGp0(GPU_COPY);
    port_WriteGp0(port_GpuPlace(rect));
    port_WriteGp0(((u32)(u16)y << 16) | (u16)x);
    port_WriteGp0(port_GpuSize(rect));
    return 0;
}
