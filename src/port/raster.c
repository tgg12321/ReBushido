/* The PlayStation's GPU in C, for the native port (port_gpu.h;
 * docs/PORT.md, "The picture"): video memory, and what the words written
 * to GP0 and GP1 do to it. It draws as the chip is publicly known to
 * draw, not as fast as it can:
 *
 *   - a triangle covers the pixels whose corner is inside it, and those
 *     on its top and left edges but not on its right and bottom ones, so
 *     two triangles that share an edge draw every pixel along it once; a
 *     four-sided primitive is two triangles, the first three corners and
 *     the last three;
 *   - a colour or a texture coordinate is a plane over the triangle: its
 *     change for a pixel across and for a pixel down, each with twelve
 *     bits of fraction and rounded towards nothing, counted from the
 *     triangle's corner furthest left, where it starts half a step in;
 *   - a texel is 4 bits or 8 through a colour table, or 15 bits; a texel
 *     of all zeroes is not drawn; a textured pixel is the texel times
 *     the colour, 128 being one, or the texel itself;
 *   - a half-transparent primitive mixes with what is there in one of
 *     four ways, and where it is textured only at the texels whose top
 *     bit is set;
 *   - the mask bit: pixels that have it can be left alone, and pixels
 *     drawn can be given it;
 *   - with dithering on, a shaded or a modulated triangle and a line add
 *     the chip's four-by-four pattern to the 8-bit colour before its low
 *     three bits are dropped; a flat triangle, a rectangle and a raw
 *     texture never do;
 *   - nothing is drawn outside the drawing area, and a primitive more
 *     than 1023 across or 511 down is not drawn at all.
 *
 * What is not known for certain, or is left out, is marked "Not sure"
 * or "Not done" where it is. Nothing here is taken from another
 * program's source.
 *
 * One switch makes it draw as PCSX-Redux's software GPU in the two
 * things where that is not the above (port_SetGpuLikeEmulator): the
 * play tests set it to hold the port's picture to the emulator's, and
 * nothing else does. */
#include "common.h"
#include "port_gpu.h"
#include "port_profile.h"
#include "bb2.h"

/* ---- the numbers of the chip ---- */

/* A command's code is the top byte of its first word. */
#define COMMAND_SHIFT 24
#define COMMAND_FILL 0x02
#define COMMAND_POLYGONS 0x20
#define COMMAND_LINES 0x40
#define COMMAND_RECTANGLES 0x60
#define COMMAND_COPY 0x80
#define COMMAND_LOAD 0xA0
#define COMMAND_STORE 0xC0
#define COMMAND_SETTINGS 0xE0
#define COMMAND_KIND_MASK 0xE0
#define SETTING_MODE 0xE1
#define SETTING_WINDOW 0xE2
#define SETTING_AREA_START 0xE3
#define SETTING_AREA_END 0xE4
#define SETTING_OFFSET 0xE5
#define SETTING_MASK 0xE6
/* The bits of a polygon's, a line's and a rectangle's code. */
#define CODE_RAW 0x01       /* a texture is not multiplied by the colour */
#define CODE_HALF 0x02      /* half-transparent */
#define CODE_TEXTURED 0x04  /* not a line */
#define CODE_QUAD 0x08      /* a polygon of four corners */
#define CODE_MANY 0x08      /* a line of many points */
#define CODE_SHADED 0x10    /* a colour for each corner */
#define CODE_SIZE_MASK 0x18 /* a rectangle's size: its own, or one of */
#define CODE_SIZE_SHIFT 3   /* three fixed ones */
#define CODE_SIZE_OWN 0
/* The word that ends a line of many points. */
#define LINE_END_MASK 0xF000F000U
#define LINE_END 0x50005000U
/* The longest command, a shaded textured quad, in words. */
#define COMMAND_WORDS_MAX 12
#define FILL_WORDS 3
#define COPY_WORDS 4
#define TRANSFER_WORDS 3

/* A word of two 16-bit halves: x and y, or a width and a height. */
#define HALF_SHIFT 16
#define HALF_MASK 0xFFFF
/* A position is eleven bits with a sign. */
#define POSITION_BITS 11
#define POSITION_SIGN (1 << (POSITION_BITS - 1))
#define POSITION_MASK ((1 << POSITION_BITS) - 1)
/* The most a primitive may span. */
#define SPAN_X_MAX 1023
#define SPAN_Y_MAX 511
#define VRAM_X_MASK (PORT_VRAM_WIDTH - 1)
#define VRAM_Y_MASK (PORT_VRAM_HEIGHT - 1)
/* A fill starts on a multiple of 16 across and is a multiple of 16
 * wide. */
#define FILL_X_MASK 0x3F0
#define FILL_STEP 16

/* A colour: three bytes in a word, and five bits each in a pixel. */
#define BYTE_MASK 0xFF
#define GREEN_SHIFT 8
#define BLUE_SHIFT 16
#define CHANNEL_BITS 5
#define CHANNEL_MAX 31
#define CHANNEL_LOST_BITS 3 /* of a byte, when it becomes five bits */
#define BYTE_MAX 255
#define PIXEL_GREEN_SHIFT 5
#define PIXEL_BLUE_SHIFT 10
/* A texel times a colour byte, as a byte: 128 is one. */
#define MODULATE_SHIFT 4

/* The drawing mode (E1, and a textured polygon's own page). */
#define MODE_PAGE_X_MASK 0xF /* the page's place, in 64s across */
#define MODE_PAGE_X_STEP 64
#define MODE_PAGE_Y_BIT 0x10 /* ... and 256 down */
#define MODE_PAGE_Y_STEP 256
#define MODE_BLEND_SHIFT 5 /* which of the four mixes */
#define MODE_BLEND_MASK 3
#define MODE_DEPTH_SHIFT 7 /* 4 bits, 8 bits, 15 bits a texel */
#define MODE_DEPTH_MASK 3
#define MODE_DITHER 0x200
#define MODE_SETTABLE 0x7FF /* what E1 sets of GPUSTAT's low bits */
#define MODE_POLYGON 0x1FF  /* what a textured polygon's page sets */
#define DEPTH_4 0
#define DEPTH_8 1
#define BLEND_HALF 0     /* half of each */
#define BLEND_ADD 1      /* the two added */
#define BLEND_SUBTRACT 2 /* what is there less what is drawn */
#define BLEND_QUARTER 3  /* what is there and a quarter of what is drawn */
/* A colour table's place: in 16s across in the low six bits, the line
 * above them. */
#define CLUT_X_MASK 0x3F
#define CLUT_X_STEP 16
#define CLUT_Y_SHIFT 6
/* A 4-bit texel is a quarter of a word, an 8-bit one a half. */
#define TEXELS_4_SHIFT 2
#define TEXELS_4_MASK 3
#define TEXEL_4_BITS 4
#define TEXEL_4_MASK 0xF
#define TEXELS_8_SHIFT 1
#define TEXELS_8_MASK 1
#define TEXEL_8_BITS 8
/* The texture window (E2): four fields of five bits, in steps of 8. */
#define WINDOW_FIELD_BITS 5
#define WINDOW_FIELD_MASK 0x1F
#define WINDOW_STEP_SHIFT 3
#define WINDOW_MASK 0xFFFFF
/* The drawing area's corners (E3, E4): ten bits and ten. */
#define AREA_BITS 10
#define AREA_MASK 0x3FF
#define AREA_WORD_MASK 0xFFFFF
/* The offset (E5): eleven bits and eleven, with signs. */
#define OFFSET_WORD_MASK 0x3FFFFF
/* The mask's settings (E6). */
#define MASK_SET 1
#define MASK_CHECK 2
/* An interpolated value has twelve bits of fraction and starts at a
 * half. */
#define FRACTION_BITS 12
#define FRACTION_ONE (1 << FRACTION_BITS)
#define FRACTION_HALF (1 << (FRACTION_BITS - 1))

/* GP1's commands. */
#define CONTROL_RESET 0x00
#define CONTROL_RESET_COMMANDS 0x01
#define CONTROL_DISPLAY_OFF 0x03
#define CONTROL_DISPLAY_START 0x05
#define CONTROL_RANGE_X 0x06
#define CONTROL_RANGE_Y 0x07
#define CONTROL_DISPLAY_MODE 0x08
#define CONTROL_INFO 0x10
#define CONTROL_COMMAND_MASK 0x3F
#define CONTROL_VALUE_MASK 0xFFFFFF
#define RANGE_X_BITS 12
#define RANGE_X_MASK 0xFFF
#define RANGE_X_RESET 0xC00200 /* from 0x200 to 0xC00 */
#define RANGE_Y_RESET 0x040010 /* from 0x10 to 0x100 */
#define DISPLAY_START_Y_MASK 0x1FF
/* The display mode: two bits of width, or one for the fifth width; the
 * 24-bit mode; and 480 lines, which takes two bits. */
#define DISPLAY_WIDTH_MASK 3
#define DISPLAY_WIDTH_368 0x40
#define DISPLAY_RGB24 0x10
#define DISPLAY_TALL 0x24
#define DISPLAY_MODE_MASK 0x7F
/* What GP1's command 0x10 can be asked for. */
#define INFO_WINDOW 2
#define INFO_AREA_START 3
#define INFO_AREA_END 4
#define INFO_OFFSET 5
#define INFO_MASK 7
/* GPUSTAT: where the mask's settings and the display's are, and the
 * bits that say the GPU is ready for anything. */
#define STATUS_MASK_SHIFT 11
#define STATUS_DISPLAY_OFF 0x00800000
#define STATUS_READY 0x1C000000
#define STATUS_RGB24 0x00200000
#define STATUS_WIDTH_SHIFT 17
#define STATUS_WIDTH_368 0x00010000
#define STATUS_TALL 0x00480000

/* The pattern a dithered colour byte has added to it, by the pixel's
 * place in a four-by-four square of video memory. */
#define DITHER_SIZE 4
#define DITHER_MASK 3
static const s8 g_port_dither[DITHER_SIZE][DITHER_SIZE] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2},
};

/* The widths of the display, by the two bits of its mode. */
static const s16 g_port_display_widths[] = {256, 320, 512, 640};
#define DISPLAY_WIDTH_FIFTH 368

/* ---- the chip's state ---- */

/* A corner of a primitive: where, and what is interpolated from it. */
enum PortAttribute {
    ATTRIBUTE_RED,
    ATTRIBUTE_GREEN,
    ATTRIBUTE_BLUE,
    ATTRIBUTE_U,
    ATTRIBUTE_V,
    ATTRIBUTE_COUNT
};

typedef struct {
    s32 x;
    s32 y;
    s32 values[ATTRIBUTE_COUNT];
} PortCorner;

/* How a primitive's pixels are made. */
typedef struct {
    s32 is_shaded; /* the colour is interpolated */
    s32 is_textured;
    s32 is_raw;  /* the texel as it is */
    s32 is_half; /* half-transparent */
    s32 is_dithered;
    s32 blend;  /* which mix, when it is half-transparent */
    s32 depth;  /* the texture's */
    s32 page_x; /* the texture page's corner */
    s32 page_y;
    s32 clut_x; /* the colour table's first pixel */
    s32 clut_y;
} PortPaint;

/* A copy into or out of video memory that is under way. */
typedef struct {
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    s32 column;
    s32 row;
    s32 is_active;
} PortTransfer;

typedef struct {
    /* drawing */
    u32 mode;         /* E1's bits */
    u32 window;       /* E2's */
    s32 window_and_u; /* a texture coordinate is anded with the one and */
    s32 window_or_u;  /* then ored with the other */
    s32 window_and_v;
    s32 window_or_v;
    u32 area_start; /* E3's and E4's */
    u32 area_end;
    s32 clip_left; /* the drawing area, its last pixels included */
    s32 clip_top;
    s32 clip_right;
    s32 clip_bottom;
    u32 offset; /* E5's */
    s32 offset_x;
    s32 offset_y;
    u32 mask;       /* E6's */
    u16 mask_set;   /* ored into every pixel drawn */
    u16 mask_check; /* a pixel that has this is left alone */
    /* the command being given */
    u32 words[COMMAND_WORDS_MAX];
    s32 count;
    s32 needed;
    /* a line of many points: the last point and its colour, and the
     * colour that came for the next */
    s32 is_in_line;
    s32 line_has_color;
    u32 line_code;
    PortCorner line_from;
    u32 line_color;
    PortTransfer load;
    PortTransfer store;
    u32 read_latch; /* what GPUREAD gives when nothing is being copied */
    /* the display */
    u32 display_start;
    u32 range_x;
    u32 range_y;
    u32 display_mode;
    s32 is_display_off;
    /* not the chip's: it draws as PCSX-Redux's software GPU where the
     * two differ (port_SetGpuLikeEmulator) */
    s32 is_like_emulator;
} PortGpu;

u16 g_port_vram[PORT_VRAM_PIXELS];
static PortGpu g_port_gpu = {
    .range_x = RANGE_X_RESET,
    .range_y = RANGE_Y_RESET,
    .is_display_off = 1,
};

void port_SetGpuLikeEmulator(s32 is_on) { g_port_gpu.is_like_emulator = is_on; }

s32 port_GpuIsLikeEmulator(void) { return g_port_gpu.is_like_emulator; }

/* ---- small things ---- */

static u16 *port_PixelAt(s32 x, s32 y) {
    return &g_port_vram[(y & VRAM_Y_MASK) * PORT_VRAM_WIDTH +
                        (x & VRAM_X_MASK)];
}

/* Eleven bits with a sign. */
static s32 port_SignExtendPosition(s32 value) {
    value &= POSITION_MASK;
    return (value ^ POSITION_SIGN) - POSITION_SIGN;
}

/* A colour's three bytes as a pixel, the low bits of each dropped. */
static u16 port_PixelOfColor(u32 color) {
    u32 r = (color & BYTE_MASK) >> CHANNEL_LOST_BITS;
    u32 g = ((color >> GREEN_SHIFT) & BYTE_MASK) >> CHANNEL_LOST_BITS;
    u32 b = ((color >> BLUE_SHIFT) & BYTE_MASK) >> CHANNEL_LOST_BITS;

    return (u16)(r | (g << PIXEL_GREEN_SHIFT) | (b << PIXEL_BLUE_SHIFT));
}

static s32 port_Clamp(s32 value, s32 least, s32 most) {
    return value < least ? least : (value > most ? most : value);
}

/* The whole number at or above numerator / divisor, for a divisor above
 * nothing. */
static s32 port_DivideUp(s32 numerator, s32 divisor) {
    s32 quotient = numerator / divisor;

    if (numerator % divisor > 0) {
        quotient++;
    }
    return quotient;
}

/* ---- the settings ---- */

static void port_SetWindow(u32 word) {
    PortGpu *gpu = &g_port_gpu;
    s32 mask_u = (s32)(word & WINDOW_FIELD_MASK);
    s32 mask_v = (s32)((word >> WINDOW_FIELD_BITS) & WINDOW_FIELD_MASK);
    s32 offset_u = (s32)((word >> (2 * WINDOW_FIELD_BITS)) & WINDOW_FIELD_MASK);
    s32 offset_v = (s32)((word >> (3 * WINDOW_FIELD_BITS)) & WINDOW_FIELD_MASK);

    gpu->window = word & WINDOW_MASK;
    gpu->window_and_u = ~(mask_u << WINDOW_STEP_SHIFT) & BYTE_MASK;
    gpu->window_or_u = (offset_u & mask_u) << WINDOW_STEP_SHIFT;
    gpu->window_and_v = ~(mask_v << WINDOW_STEP_SHIFT) & BYTE_MASK;
    gpu->window_or_v = (offset_v & mask_v) << WINDOW_STEP_SHIFT;
}

static void port_SetAreaStart(u32 word) {
    g_port_gpu.area_start = word & AREA_WORD_MASK;
    g_port_gpu.clip_left = (s32)(word & AREA_MASK);
    g_port_gpu.clip_top = (s32)((word >> AREA_BITS) & AREA_MASK);
}

static void port_SetAreaEnd(u32 word) {
    g_port_gpu.area_end = word & AREA_WORD_MASK;
    g_port_gpu.clip_right = (s32)(word & AREA_MASK);
    g_port_gpu.clip_bottom = (s32)((word >> AREA_BITS) & AREA_MASK);
}

static void port_SetOffset(u32 word) {
    g_port_gpu.offset = word & OFFSET_WORD_MASK;
    g_port_gpu.offset_x = port_SignExtendPosition((s32)word);
    g_port_gpu.offset_y = port_SignExtendPosition((s32)(word >> POSITION_BITS));
}

static void port_SetMask(u32 word) {
    g_port_gpu.mask = word & (MASK_SET | MASK_CHECK);
    g_port_gpu.mask_set = (word & MASK_SET) ? PORT_PIXEL_MASK_BIT : 0;
    g_port_gpu.mask_check = (word & MASK_CHECK) ? PORT_PIXEL_MASK_BIT : 0;
}

/* ---- a pixel ---- */

/* One channel of a mix: what is there and what is drawn, five bits
 * each. */
static s32 port_BlendChannel(s32 back, s32 front, s32 blend) {
    switch (blend) {
    case BLEND_HALF:
        return (back + front) >> 1;
    case BLEND_ADD:
        return port_Clamp(back + front, 0, CHANNEL_MAX);
    case BLEND_SUBTRACT:
        return port_Clamp(back - front, 0, CHANNEL_MAX);
    default:
        return port_Clamp(back + (front >> 2), 0, CHANNEL_MAX);
    }
}

static u16 port_BlendPixels(u16 back, u16 front, s32 blend) {
    s32 r = port_BlendChannel(back & CHANNEL_MAX, front & CHANNEL_MAX, blend);
    s32 g =
        port_BlendChannel((back >> PIXEL_GREEN_SHIFT) & CHANNEL_MAX,
                          (front >> PIXEL_GREEN_SHIFT) & CHANNEL_MAX, blend);
    s32 b = port_BlendChannel((back >> PIXEL_BLUE_SHIFT) & CHANNEL_MAX,
                              (front >> PIXEL_BLUE_SHIFT) & CHANNEL_MAX, blend);

    return (u16)(r | (g << PIXEL_GREEN_SHIFT) | (b << PIXEL_BLUE_SHIFT));
}

/* Puts a pixel of 15 bits, with whatever top bit it has, where the mask
 * lets it; `is_mixed` mixes it with what is there first. The place is
 * inside the drawing area already. */
static void port_PutPixel(s32 x, s32 y, u16 pixel, s32 is_mixed, s32 blend) {
    u16 *at = port_PixelAt(x, y);
    u16 back = *at;

    if (back & g_port_gpu.mask_check) {
        return;
    }
    if (is_mixed) {
        pixel = (u16)(port_BlendPixels(back, pixel, blend) |
                      (pixel & PORT_PIXEL_MASK_BIT));
    }
    *at = pixel | g_port_gpu.mask_set;
}

/* The texel at (u, v) of a paint's page, through the texture window. */
static u16 port_ReadTexel(const PortPaint *paint, s32 u, s32 v) {
    const PortGpu *gpu = &g_port_gpu;
    s32 y;
    s32 index;

    u = (u & gpu->window_and_u) | gpu->window_or_u;
    v = (v & gpu->window_and_v) | gpu->window_or_v;
    y = paint->page_y + v;
    if (paint->depth == DEPTH_4) {
        u16 word = *port_PixelAt(paint->page_x + (u >> TEXELS_4_SHIFT), y);

        index = (word >> ((u & TEXELS_4_MASK) * TEXEL_4_BITS)) & TEXEL_4_MASK;
        return *port_PixelAt(paint->clut_x + index, paint->clut_y);
    }
    if (paint->depth == DEPTH_8) {
        u16 word = *port_PixelAt(paint->page_x + (u >> TEXELS_8_SHIFT), y);

        index = (word >> ((u & TEXELS_8_MASK) * TEXEL_8_BITS)) & BYTE_MASK;
        return *port_PixelAt(paint->clut_x + index, paint->clut_y);
    }
    return *port_PixelAt(paint->page_x + u, y);
}

/* A colour byte, or a texel's channel times one, as five bits: with the
 * dither's pattern added if there is one. */
static s32 port_ChannelOfByte(s32 value, s32 dither) {
    return port_Clamp(value + dither, 0, BYTE_MAX) >> CHANNEL_LOST_BITS;
}

/* One pixel of a primitive: its colour bytes and its texture coordinates
 * at (x, y), which is inside the drawing area. */
static void port_PaintPixel(
    const PortPaint *paint, s32 x, s32 y, const s32 *values) {
    s32 dither = paint->is_dithered
                     ? g_port_dither[y & DITHER_MASK][x & DITHER_MASK]
                     : 0;
    s32 r = values[ATTRIBUTE_RED];
    s32 g = values[ATTRIBUTE_GREEN];
    s32 b = values[ATTRIBUTE_BLUE];
    u16 pixel;
    s32 is_mixed = paint->is_half;

    if (paint->is_textured) {
        u16 texel = port_ReadTexel(paint, values[ATTRIBUTE_U] & BYTE_MASK,
                                   values[ATTRIBUTE_V] & BYTE_MASK);

        if (texel == 0) {
            return;
        }
        /* only the texels with the top bit are mixed */
        is_mixed = is_mixed && (texel & PORT_PIXEL_MASK_BIT) != 0;
        if (paint->is_raw) {
            port_PutPixel(x, y, texel, is_mixed, paint->blend);
            return;
        }
        r = ((texel & CHANNEL_MAX) * r) >> MODULATE_SHIFT;
        g = (((texel >> PIXEL_GREEN_SHIFT) & CHANNEL_MAX) * g) >>
            MODULATE_SHIFT;
        b = (((texel >> PIXEL_BLUE_SHIFT) & CHANNEL_MAX) * b) >> MODULATE_SHIFT;
        pixel = texel & PORT_PIXEL_MASK_BIT;
    } else {
        pixel = 0;
    }
    pixel |= (u16)(port_ChannelOfByte(r, dither) |
                   (port_ChannelOfByte(g, dither) << PIXEL_GREEN_SHIFT) |
                   (port_ChannelOfByte(b, dither) << PIXEL_BLUE_SHIFT));
    port_PutPixel(x, y, pixel, is_mixed, paint->blend);
}

/* ---- how a primitive is painted ---- */

/* The paint of a primitive of this code, with the drawing mode as it is
 * now. */
static void port_SetUpPaint(PortPaint *paint, u32 code, u32 clut) {
    u32 mode = g_port_gpu.mode;

    paint->is_shaded = (code & CODE_SHADED) != 0;
    paint->is_textured = (code & CODE_TEXTURED) != 0;
    paint->is_raw = (code & CODE_RAW) != 0;
    paint->is_half = (code & CODE_HALF) != 0;
    paint->is_dithered = 0;
    paint->blend = (s32)((mode >> MODE_BLEND_SHIFT) & MODE_BLEND_MASK);
    paint->depth = (s32)((mode >> MODE_DEPTH_SHIFT) & MODE_DEPTH_MASK);
    paint->page_x = (s32)(mode & MODE_PAGE_X_MASK) * MODE_PAGE_X_STEP;
    paint->page_y = (mode & MODE_PAGE_Y_BIT) ? MODE_PAGE_Y_STEP : 0;
    paint->clut_x = (s32)(clut & CLUT_X_MASK) * CLUT_X_STEP;
    paint->clut_y = (s32)((clut >> CLUT_Y_SHIFT) & VRAM_Y_MASK);
}

static void port_SetCornerColor(PortCorner *corner, u32 color) {
    corner->values[ATTRIBUTE_RED] = (s32)(color & BYTE_MASK);
    corner->values[ATTRIBUTE_GREEN] = (s32)((color >> GREEN_SHIFT) & BYTE_MASK);
    corner->values[ATTRIBUTE_BLUE] = (s32)((color >> BLUE_SHIFT) & BYTE_MASK);
}

/* A corner's place from a word of x and y, with the drawing offset. */
static void port_SetCornerPlace(PortCorner *corner, u32 word) {
    corner->x = port_SignExtendPosition(
        port_SignExtendPosition((s32)word) + g_port_gpu.offset_x);
    corner->y = port_SignExtendPosition(
        port_SignExtendPosition((s32)(word >> HALF_SHIFT)) +
        g_port_gpu.offset_y);
}

static void port_SetCornerTexture(PortCorner *corner, u32 word) {
    corner->values[ATTRIBUTE_U] = (s32)(word & BYTE_MASK);
    corner->values[ATTRIBUTE_V] = (s32)((word >> GREEN_SHIFT) & BYTE_MASK);
}

/* ---- a triangle ---- */

/* Where the edge from `from` to `to`, which goes down, crosses the line
 * `y`: the first whole x at or after it. */
static s32 port_EdgeAt(const PortCorner *from, const PortCorner *to, s32 y) {
    s32 down = to->y - from->y;

    return port_DivideUp(
        from->x * down + (y - from->y) * (to->x - from->x), down);
}

/* A value at a pixel with no fraction dropped on the way: where it
 * starts, and how far it has changed, which is `change` over `area`; a
 * half is added and the whole number taken. This is not the chip's way:
 * it is what the port does when it draws as the emulator
 * (port_SetGpuLikeEmulator). */
static s32 port_InterpolateWhole(s32 start, s32 change, s32 area) {
    s32 numerator = 2 * (start * area + change) + area;
    s32 divisor = 2 * area;
    s32 quotient;

    if (divisor < 0) {
        numerator = -numerator;
        divisor = -divisor;
    }
    quotient = numerator / divisor;
    if (numerator % divisor < 0) {
        quotient--;
    }
    return quotient & BYTE_MASK;
}

/* A triangle, line by line from its highest corner to the line above
 * its lowest: on each, from the edge on the left to the pixel before
 * the edge on the right, held to the drawing area.
 * Not sure: the twelve bits of a value's fraction, and that it is
 * counted from the corner furthest left. Both are what is written of
 * the chip; over a polygon hundreds of pixels across they move a
 * texture by a pixel or two, and the emulator has neither. */
static void port_DrawTriangle(const PortCorner *first, const PortCorner *second,
                              const PortCorner *third, const PortPaint *paint) {
    const PortGpu *gpu = &g_port_gpu;
    const PortCorner *top = first;
    const PortCorner *middle = second;
    const PortCorner *bottom = third;
    const PortCorner *swap;
    const PortCorner *origin = first;
    /* twice the triangle's area, with the sign of the way it turns */
    s32 area = (second->x - first->x) * (third->y - first->y) -
               (third->x - first->x) * (second->y - first->y);
    s32 cross_x[ATTRIBUTE_COUNT];
    s32 cross_y[ATTRIBUTE_COUNT];
    s32 across[ATTRIBUTE_COUNT]; /* each value's change for a pixel across */
    s32 down[ATTRIBUTE_COUNT];   /* ... and for a pixel down */
    u32 row_start[ATTRIBUTE_COUNT];
    s32 attribute_first = paint->is_shaded ? ATTRIBUTE_RED : ATTRIBUTE_U;
    s32 attribute_end = paint->is_textured ? ATTRIBUTE_COUNT : ATTRIBUTE_U;
    s32 values[ATTRIBUTE_COUNT];
    s32 leftmost;
    s32 rightmost;
    s32 x;
    s32 y;
    s32 i;

    /* a step run again, or a frame ahead whose picture is the remastered
     * one, paints nothing (port_platform.h) */
    if (PORT_PAINTS_NOTHING()) {
        return;
    }

    if (area == 0) {
        return;
    }
    /* the values are counted from the corner furthest left: of two as far
     * left, the later */
    if (second->x <= first->x) {
        origin = third->x <= second->x ? third : second;
    } else if (third->x < first->x) {
        origin = third;
    }
    /* by height: the one that is higher first, as they came if neither */
    if (middle->y < top->y) {
        swap = top;
        top = middle;
        middle = swap;
    }
    if (bottom->y < middle->y) {
        swap = middle;
        middle = bottom;
        bottom = swap;
        if (middle->y < top->y) {
            swap = top;
            top = middle;
            middle = swap;
        }
    }
    if (bottom->y - top->y > SPAN_Y_MAX) {
        return;
    }
    leftmost = first->x < second->x ? first->x : second->x;
    leftmost = third->x < leftmost ? third->x : leftmost;
    rightmost = first->x > second->x ? first->x : second->x;
    rightmost = third->x > rightmost ? third->x : rightmost;
    if (rightmost - leftmost > SPAN_X_MAX) {
        return;
    }
    for (i = 0; i < ATTRIBUTE_COUNT; i++) {
        values[i] = first->values[i];
        cross_x[i] = 0;
        cross_y[i] = 0;
        across[i] = 0;
        down[i] = 0;
    }
    for (i = attribute_first; i < attribute_end; i++) {
        s32 to_second = second->values[i] - first->values[i];
        s32 to_third = third->values[i] - first->values[i];

        /* the change across and down, each times the area */
        cross_x[i] = to_second * (third->y - first->y) -
                     to_third * (second->y - first->y);
        cross_y[i] = (second->x - first->x) * to_third -
                     (third->x - first->x) * to_second;
        across[i] = (s32)((u32)cross_x[i] << FRACTION_BITS) / area;
        down[i] = (s32)((u32)cross_y[i] << FRACTION_BITS) / area;
    }
    for (y = top->y; y < bottom->y; y++) {
        s32 long_x;
        s32 short_x;
        s32 left;
        s32 right;

        if (y < gpu->clip_top || y > gpu->clip_bottom) {
            continue;
        }
        /* the long edge, and the short one this line is beside */
        long_x = port_EdgeAt(top, bottom, y);
        short_x = y < middle->y ? port_EdgeAt(top, middle, y)
                                : port_EdgeAt(middle, bottom, y);
        left = long_x < short_x ? long_x : short_x;
        right = (long_x < short_x ? short_x : long_x) - 1;
        if (left < gpu->clip_left) {
            left = gpu->clip_left;
        }
        if (right > gpu->clip_right) {
            right = gpu->clip_right;
        }
        if (left > right) {
            continue;
        }
        for (i = attribute_first; i < attribute_end; i++) {
            row_start[i] =
                ((u32)origin->values[i] << FRACTION_BITS) + FRACTION_HALF +
                (u32)across[i] * (u32)(left - origin->x) +
                (u32)down[i] * (u32)(y - origin->y);
        }
        for (x = left; x <= right; x++) {
            for (i = attribute_first; i < attribute_end; i++) {
                values[i] = (s32)(row_start[i] >> FRACTION_BITS) & BYTE_MASK;
                row_start[i] += (u32)across[i];
                if (gpu->is_like_emulator) {
                    values[i] = port_InterpolateWhole(
                        first->values[i],
                        cross_x[i] * (x - first->x) +
                            cross_y[i] * (y - first->y),
                        area);
                }
            }
            port_PaintPixel(paint, x, y, values);
        }
    }
}

/* A polygon's command: three corners or four, each a place and, by the
 * code, a colour and a texture coordinate before or after it. */
static void port_DrawPolygon(const u32 *words) {
    u32 code = words[0] >> COMMAND_SHIFT;
    s32 corner_count = (code & CODE_QUAD) ? 4 : 3;
    PortCorner corners[4];
    PortPaint paint;
    u32 clut = 0;
    s32 at = 0;
    s32 i;

    for (i = 0; i < corner_count; i++) {
        /* the first colour is the command's own word */
        if (i == 0 || (code & CODE_SHADED)) {
            port_SetCornerColor(&corners[i], words[at++]);
        } else {
            port_SetCornerColor(&corners[i], words[0]);
        }
        port_SetCornerPlace(&corners[i], words[at++]);
        port_SetCornerTexture(&corners[i], 0);
        if (code & CODE_TEXTURED) {
            port_SetCornerTexture(&corners[i], words[at]);
            if (i == 0) {
                clut = words[at] >> HALF_SHIFT;
            } else if (i == 1) {
                /* the page and the mix come with the polygon, and stay */
                g_port_gpu.mode = (g_port_gpu.mode & ~(u32)MODE_POLYGON) |
                                  ((words[at] >> HALF_SHIFT) & MODE_POLYGON);
            }
            at++;
        }
    }
    port_SetUpPaint(&paint, code, clut);
    paint.is_dithered =
        (g_port_gpu.mode & MODE_DITHER) != 0 &&
        (paint.is_shaded ||
         (paint.is_textured && !paint.is_raw && !g_port_gpu.is_like_emulator));
    port_DrawTriangle(&corners[0], &corners[1], &corners[2], &paint);
    if (corner_count == 4) {
        port_DrawTriangle(&corners[1], &corners[2], &corners[3], &paint);
    }
}

/* ---- a line ---- */

/* From one point to the next, both ends drawn: as many pixels as the
 * longer of the two distances and one more, each the nearest to the true
 * line, a tie going away from the first point.
 * Not sure: which way the chip takes a tie. */
static void port_DrawLine(
    const PortCorner *from, const PortCorner *to, const PortPaint *paint) {
    const PortGpu *gpu = &g_port_gpu;
    s32 distance_x = to->x - from->x;
    s32 distance_y = to->y - from->y;
    s32 size_x = distance_x < 0 ? -distance_x : distance_x;
    s32 size_y = distance_y < 0 ? -distance_y : distance_y;
    s32 steps = size_x > size_y ? size_x : size_y;
    s32 values[ATTRIBUTE_COUNT];
    s32 step;
    s32 i;

    /* a step run again, or a frame ahead whose picture is the remastered
     * one, paints nothing (port_platform.h) */
    if (PORT_PAINTS_NOTHING()) {
        return;
    }

    if (size_x > SPAN_X_MAX || size_y > SPAN_Y_MAX) {
        return;
    }
    for (step = 0; step <= steps; step++) {
        s32 x = from->x;
        s32 y = from->y;

        if (steps != 0) {
            s32 moved_x = (2 * step * size_x + steps) / (2 * steps);
            s32 moved_y = (2 * step * size_y + steps) / (2 * steps);

            x += distance_x < 0 ? -moved_x : moved_x;
            y += distance_y < 0 ? -moved_y : moved_y;
        }
        if (x < gpu->clip_left || x > gpu->clip_right || y < gpu->clip_top ||
            y > gpu->clip_bottom) {
            continue;
        }
        for (i = ATTRIBUTE_RED; i < ATTRIBUTE_U; i++) {
            values[i] = from->values[i];
            if (paint->is_shaded && steps != 0) {
                s32 change =
                    (to->values[i] - from->values[i]) * FRACTION_ONE / steps;

                values[i] = (from->values[i] * FRACTION_ONE + FRACTION_HALF +
                             change * step) >>
                            FRACTION_BITS;
            }
        }
        values[ATTRIBUTE_U] = 0;
        values[ATTRIBUTE_V] = 0;
        port_PaintPixel(paint, x, y, values);
    }
}

static void port_SetUpLinePaint(PortPaint *paint, u32 code) {
    port_SetUpPaint(paint, code & ~(u32)(CODE_TEXTURED | CODE_RAW), 0);
    paint->is_dithered = (g_port_gpu.mode & MODE_DITHER) != 0;
}

/* ---- a rectangle ---- */

/* A rectangle's command: its corner, its texture's corner if it has
 * one, and its size if that is its own. Its pixels are a texel each,
 * a step across and a step down. */
static void port_DrawRectangle(const u32 *words) {
    static const s16 sizes[] = {0, 1, 8, 16}; /* by the code's two bits */
    const PortGpu *gpu = &g_port_gpu;
    u32 code = words[0] >> COMMAND_SHIFT;
    s32 size = (s32)((code & CODE_SIZE_MASK) >> CODE_SIZE_SHIFT);
    PortCorner corner;
    PortPaint paint;
    s32 values[ATTRIBUTE_COUNT];
    u32 clut = 0;
    s32 at = 1;
    s32 w;
    s32 h;
    s32 x;
    s32 y;

    /* a step run again, or a frame ahead whose picture is the remastered
     * one, paints nothing (port_platform.h) */
    if (PORT_PAINTS_NOTHING()) {
        return;
    }

    port_SetCornerColor(&corner, words[0]);
    port_SetCornerPlace(&corner, words[at++]);
    port_SetCornerTexture(&corner, 0);
    if (code & CODE_TEXTURED) {
        port_SetCornerTexture(&corner, words[at]);
        clut = words[at++] >> HALF_SHIFT;
    }
    if (size == CODE_SIZE_OWN) {
        w = (s32)(words[at] & VRAM_X_MASK);
        h = (s32)((words[at] >> HALF_SHIFT) & VRAM_Y_MASK);
    } else {
        w = sizes[size];
        h = sizes[size];
    }
    port_SetUpPaint(&paint, code & ~(u32)CODE_SHADED, clut);
    values[ATTRIBUTE_RED] = corner.values[ATTRIBUTE_RED];
    values[ATTRIBUTE_GREEN] = corner.values[ATTRIBUTE_GREEN];
    values[ATTRIBUTE_BLUE] = corner.values[ATTRIBUTE_BLUE];
    for (y = 0; y < h; y++) {
        s32 line = corner.y + y;

        if (line < gpu->clip_top || line > gpu->clip_bottom) {
            continue;
        }
        for (x = 0; x < w; x++) {
            s32 column = corner.x + x;

            if (column < gpu->clip_left || column > gpu->clip_right) {
                continue;
            }
            values[ATTRIBUTE_U] = corner.values[ATTRIBUTE_U] + x;
            values[ATTRIBUTE_V] = corner.values[ATTRIBUTE_V] + y;
            port_PaintPixel(&paint, column, line, values);
        }
    }
}

/* ---- video memory as a whole ---- */

/* The fill: not held to the drawing area, and the mask is neither
 * looked at nor set. */
static void port_FillVram(const u32 *words) {
    u16 pixel = port_PixelOfColor(words[0]);
    s32 left = (s32)(words[1] & FILL_X_MASK);
    s32 top = (s32)((words[1] >> HALF_SHIFT) & VRAM_Y_MASK);
    s32 w = (s32)(((words[2] & VRAM_X_MASK) + FILL_STEP - 1) &
                  ~(u32)(FILL_STEP - 1));
    s32 h = (s32)((words[2] >> HALF_SHIFT) & VRAM_Y_MASK);
    s32 x;
    s32 y;

    /* a step run again paints nothing (port_platform.h) */
    if (g_port_rerun) {
        return;
    }

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            *port_PixelAt(left + x, top + y) = pixel;
        }
    }
}

/* A size of a copy: nothing is the most there is. */
static s32 port_TransferSize(u32 value, s32 mask) {
    return (s32)((value - 1) & (u32)mask) + 1;
}

/* From one place of video memory to another, a pixel at a time from the
 * top left, under the mask's settings.
 * Not sure: what the chip gives when the two overlap. */
static void port_CopyVram(const u32 *words) {
    s32 from_x = (s32)(words[1] & VRAM_X_MASK);
    s32 from_y = (s32)((words[1] >> HALF_SHIFT) & VRAM_Y_MASK);
    s32 to_x = (s32)(words[2] & VRAM_X_MASK);
    s32 to_y = (s32)((words[2] >> HALF_SHIFT) & VRAM_Y_MASK);
    s32 w = port_TransferSize(words[3] & HALF_MASK, VRAM_X_MASK);
    s32 h = port_TransferSize(words[3] >> HALF_SHIFT, VRAM_Y_MASK);
    s32 x;
    s32 y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u16 pixel = *port_PixelAt(from_x + x, from_y + y);
            u16 *to = port_PixelAt(to_x + x, to_y + y);

            if ((*to & g_port_gpu.mask_check) == 0) {
                *to = pixel | g_port_gpu.mask_set;
            }
        }
    }
}

static void port_BeginTransfer(PortTransfer *transfer, const u32 *words) {
    transfer->x = (s32)(words[1] & VRAM_X_MASK);
    transfer->y = (s32)((words[1] >> HALF_SHIFT) & VRAM_Y_MASK);
    transfer->w = port_TransferSize(words[2] & HALF_MASK, VRAM_X_MASK);
    transfer->h = port_TransferSize(words[2] >> HALF_SHIFT, VRAM_Y_MASK);
    transfer->column = 0;
    transfer->row = 0;
    transfer->is_active = 1;
}

/* The next pixel of a copy, and whether it was the last. */
static u16 *port_NextTransferPixel(PortTransfer *transfer) {
    u16 *at = port_PixelAt(
        transfer->x + transfer->column, transfer->y + transfer->row);

    if (++transfer->column == transfer->w) {
        transfer->column = 0;
        if (++transfer->row == transfer->h) {
            transfer->is_active = 0;
        }
    }
    return at;
}

/* A word of a copy into video memory: two pixels, the low half first. */
static void port_LoadWord(u32 word) {
    s32 i;

    for (i = 0; i < 2 && g_port_gpu.load.is_active; i++) {
        u16 *at = port_NextTransferPixel(&g_port_gpu.load);

        if ((*at & g_port_gpu.mask_check) == 0) {
            *at = (u16)(word & HALF_MASK) | g_port_gpu.mask_set;
        }
        word >>= HALF_SHIFT;
    }
}

/* ---- GP0 ---- */

/* How many words the command that begins with this code has; a line of
 * many points says its first two. */
static s32 port_CommandWords(u32 code) {
    s32 words = 1;

    switch (code & COMMAND_KIND_MASK) {
    case COMMAND_POLYGONS: {
        s32 corners = (code & CODE_QUAD) ? 4 : 3;

        words += corners;
        if (code & CODE_TEXTURED) {
            words += corners;
        }
        if (code & CODE_SHADED) {
            words += corners - 1;
        }
        break;
    }
    case COMMAND_LINES:
        if (code & CODE_MANY) {
            words = 2;
        } else {
            words = (code & CODE_SHADED) ? 4 : 3;
        }
        break;
    case COMMAND_RECTANGLES:
        words = 2;
        if (code & CODE_TEXTURED) {
            words++;
        }
        if ((code & CODE_SIZE_MASK) >> CODE_SIZE_SHIFT == CODE_SIZE_OWN) {
            words++;
        }
        break;
    case COMMAND_COPY:
        words = COPY_WORDS;
        break;
    case COMMAND_LOAD:
    case COMMAND_STORE:
        words = TRANSFER_WORDS;
        break;
    default:
        if (code == COMMAND_FILL) {
            words = FILL_WORDS;
        }
        break;
    }
    return words;
}

/* One of the six commands that set how drawing is done. */
static void port_RunSetting(u32 word) {
    switch (word >> COMMAND_SHIFT) {
    case SETTING_MODE:
        /* Not done: with the bit that forbids drawing on the display
         * clear, the chip leaves out the lines of the field on show
         * when the display is interlaced. The game's never is. */
        g_port_gpu.mode = word & MODE_SETTABLE;
        break;
    case SETTING_WINDOW:
        port_SetWindow(word);
        break;
    case SETTING_AREA_START:
        port_SetAreaStart(word);
        break;
    case SETTING_AREA_END:
        port_SetAreaEnd(word);
        break;
    case SETTING_OFFSET:
        port_SetOffset(word);
        break;
    case SETTING_MASK:
        port_SetMask(word);
        break;
    default:
        break;
    }
}

/* A line of one piece, or the first point of a line of many. */
static void port_RunLine(const u32 *words) {
    PortGpu *gpu = &g_port_gpu;
    u32 code = words[0] >> COMMAND_SHIFT;
    PortCorner to;
    PortPaint paint;

    port_SetCornerColor(&gpu->line_from, words[0]);
    port_SetCornerPlace(&gpu->line_from, words[1]);
    if (code & CODE_MANY) {
        gpu->is_in_line = 1;
        gpu->line_has_color = 0;
        gpu->line_code = code;
        return;
    }
    port_SetUpLinePaint(&paint, code);
    if (code & CODE_SHADED) {
        port_SetCornerColor(&to, words[2]);
        port_SetCornerPlace(&to, words[3]);
    } else {
        port_SetCornerColor(&to, words[0]);
        port_SetCornerPlace(&to, words[2]);
    }
    port_DrawLine(&gpu->line_from, &to, &paint);
}

/* The next word of a line of many points: a colour, where each point has
 * one, a point, or the word that ends the line. */
static void port_ContinueLine(u32 word) {
    PortGpu *gpu = &g_port_gpu;
    PortCorner to;
    PortPaint paint;

    if ((gpu->line_code & CODE_SHADED) && !gpu->line_has_color) {
        if ((word & LINE_END_MASK) == LINE_END) {
            gpu->is_in_line = 0;
            return;
        }
        gpu->line_color = word;
        gpu->line_has_color = 1;
        return;
    }
    if (!(gpu->line_code & CODE_SHADED)) {
        if ((word & LINE_END_MASK) == LINE_END) {
            gpu->is_in_line = 0;
            return;
        }
        to = gpu->line_from; /* the one colour */
    } else {
        port_SetCornerColor(&to, gpu->line_color);
    }
    port_SetCornerPlace(&to, word);
    port_SetUpLinePaint(&paint, gpu->line_code);
    port_DrawLine(&gpu->line_from, &to, &paint);
    gpu->line_from = to;
    gpu->line_has_color = 0;
}

/* A command whose words have all come. */
static void port_RunCommand(const u32 *words) {
    u32 code = words[0] >> COMMAND_SHIFT;

    switch (code & COMMAND_KIND_MASK) {
    case COMMAND_POLYGONS:
        port_DrawPolygon(words);
        break;
    case COMMAND_LINES:
        port_RunLine(words);
        break;
    case COMMAND_RECTANGLES:
        port_DrawRectangle(words);
        break;
    case COMMAND_COPY:
        port_CopyVram(words);
        break;
    case COMMAND_LOAD:
        port_BeginTransfer(&g_port_gpu.load, words);
        break;
    case COMMAND_STORE:
        port_BeginTransfer(&g_port_gpu.store, words);
        break;
    case COMMAND_SETTINGS:
        port_RunSetting(words[0]);
        break;
    default:
        /* nothing, the cache's clearing and the interrupt among them */
        if (code == COMMAND_FILL) {
            port_FillVram(words);
        }
        break;
    }
}

/* The drawing state as it is (port_gpu.h), for the remastered
 * renderer: it paints a primitive as this file has just painted it. */
void port_GetGpuState(PortGpuState *state) {
    const PortGpu *gpu = &g_port_gpu;

    state->mode = gpu->mode;
    state->window_and_u = gpu->window_and_u;
    state->window_or_u = gpu->window_or_u;
    state->window_and_v = gpu->window_and_v;
    state->window_or_v = gpu->window_or_v;
    state->clip_left = gpu->clip_left;
    state->clip_top = gpu->clip_top;
    state->clip_right = gpu->clip_right;
    state->clip_bottom = gpu->clip_bottom;
    state->offset_x = gpu->offset_x;
    state->offset_y = gpu->offset_y;
    state->mask_set = gpu->mask_set;
    state->mask_check = gpu->mask_check;
}

/* A word to GP0 (port_gpu.h): pixels of a load that is under way, the
 * next word of a line of many points, or a word of a command, which is
 * run when its last has come. The remastered renderer is given each
 * command as well, once this file has drawn it (port_remaster.h): it
 * then sees the drawing mode a textured polygon brought with it, and
 * video memory as the command left it. */
void port_WriteGp0(u32 word) {
    PortGpu *gpu = &g_port_gpu;

    if (gpu->load.is_active) {
        port_LoadWord(word);
        if (!gpu->load.is_active) {
            s32 was = port_ProfileSwitch(PORT_PROFILE_REMASTER);

            port_RemasterLoadedVram(
                gpu->load.x, gpu->load.y, gpu->load.w, gpu->load.h);
            port_ProfileSwitch(was);
        }
        return;
    }
    if (gpu->is_in_line) {
        port_ContinueLine(word);
        return;
    }
    if (gpu->count == 0) {
        gpu->needed = port_CommandWords(word >> COMMAND_SHIFT);
    }
    gpu->words[gpu->count++] = word;
    if (gpu->count == gpu->needed) {
        s32 words = gpu->needed;

        s32 was = port_ProfileSwitch(PORT_PROFILE_RASTER);

        gpu->count = 0;
        port_RunCommand(gpu->words);
        if (!g_port_rerun) {
            port_ProfileSwitch(PORT_PROFILE_REMASTER);
            port_RemasterCommand(gpu->words, words);
        }
        port_ProfileSwitch(was);
    }
}

/* ---- GPUREAD and GPUSTAT ---- */

u32 port_ReadGpu(void) {
    PortGpu *gpu = &g_port_gpu;
    u32 word = 0;
    s32 i;

    if (!gpu->store.is_active) {
        return gpu->read_latch;
    }
    for (i = 0; i < 2 && gpu->store.is_active; i++) {
        word |= (u32)*port_NextTransferPixel(&gpu->store) << (i * HALF_SHIFT);
    }
    return word;
}

/* GPUSTAT, as far as anything reads it (port_gpu.h). */
u32 port_GetGpuStatus(void) {
    const PortGpu *gpu = &g_port_gpu;
    u32 status = (gpu->mode & MODE_SETTABLE) | STATUS_READY;

    status |= gpu->mask << STATUS_MASK_SHIFT;
    status |= (gpu->display_mode & DISPLAY_WIDTH_MASK) << STATUS_WIDTH_SHIFT;
    if (gpu->display_mode & DISPLAY_WIDTH_368) {
        status |= STATUS_WIDTH_368;
    }
    if (gpu->display_mode & DISPLAY_RGB24) {
        status |= STATUS_RGB24;
    }
    if ((gpu->display_mode & DISPLAY_TALL) == DISPLAY_TALL) {
        status |= STATUS_TALL;
    }
    if (gpu->is_display_off) {
        status |= STATUS_DISPLAY_OFF;
    }
    return status;
}

/* ---- GP1 ---- */

/* The settings as at power on. Video memory is not touched. */
static void port_ResetGpu(void) {
    PortGpu *gpu = &g_port_gpu;

    gpu->mode = 0;
    port_SetWindow(0);
    port_SetAreaStart(0);
    port_SetAreaEnd(0);
    port_SetOffset(0);
    port_SetMask(0);
    gpu->count = 0;
    gpu->is_in_line = 0;
    gpu->load.is_active = 0;
    gpu->store.is_active = 0;
    gpu->display_start = 0;
    gpu->range_x = RANGE_X_RESET;
    gpu->range_y = RANGE_Y_RESET;
    gpu->display_mode = 0;
    gpu->is_display_off = 1;
}

/* A word to GP1 (port_gpu.h): the display's settings, the reset, and
 * the question GPUREAD answers. */
void port_WriteGp1(u32 word) {
    PortGpu *gpu = &g_port_gpu;
    u32 value = word & CONTROL_VALUE_MASK;

    switch ((word >> COMMAND_SHIFT) & CONTROL_COMMAND_MASK) {
    case CONTROL_RESET:
        port_ResetGpu();
        break;
    case CONTROL_RESET_COMMANDS:
        gpu->count = 0;
        gpu->is_in_line = 0;
        gpu->load.is_active = 0;
        gpu->store.is_active = 0;
        break;
    case CONTROL_DISPLAY_OFF:
        gpu->is_display_off = (s32)(value & 1);
        break;
    case CONTROL_DISPLAY_START:
        gpu->display_start = value;
        break;
    case CONTROL_RANGE_X:
        gpu->range_x = value;
        break;
    case CONTROL_RANGE_Y:
        gpu->range_y = value;
        break;
    case CONTROL_DISPLAY_MODE:
        gpu->display_mode = value & DISPLAY_MODE_MASK;
        break;
    case CONTROL_INFO:
        switch (value & INFO_MASK) {
        case INFO_WINDOW:
            gpu->read_latch = gpu->window;
            break;
        case INFO_AREA_START:
            gpu->read_latch = gpu->area_start;
            break;
        case INFO_AREA_END:
            gpu->read_latch = gpu->area_end;
            break;
        case INFO_OFFSET:
            gpu->read_latch = gpu->offset;
            break;
        default:
            break;
        }
        break;
    default:
        /* the interrupt's acknowledgement and the DMA's direction: there
         * is neither */
        break;
    }
}

/* The rectangle on show: where the display starts, as wide as its mode
 * says, and as high as its lines, twice that in the 480-line mode. */
void port_GetDisplay(PortDisplay *display) {
    const PortGpu *gpu = &g_port_gpu;
    s32 first_line = (s32)(gpu->range_y & AREA_MASK);
    s32 end_line = (s32)((gpu->range_y >> AREA_BITS) & AREA_MASK);

    display->x = (s32)(gpu->display_start & AREA_MASK);
    display->y =
        (s32)((gpu->display_start >> AREA_BITS) & DISPLAY_START_Y_MASK);
    display->w =
        (gpu->display_mode & DISPLAY_WIDTH_368)
            ? DISPLAY_WIDTH_FIFTH
            : g_port_display_widths[gpu->display_mode & DISPLAY_WIDTH_MASK];
    display->h = end_line > first_line ? end_line - first_line : 0;
    if ((gpu->display_mode & DISPLAY_TALL) == DISPLAY_TALL) {
        display->h *= 2;
    }
    display->is_on = !gpu->is_display_off;
    display->is_rgb24 = (gpu->display_mode & DISPLAY_RGB24) != 0;
}
