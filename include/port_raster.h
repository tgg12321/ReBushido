#ifndef PORT_RASTER_H
#define PORT_RASTER_H

/* The software GPU's state (src/port/raster.c): the game's, which the
 * state saves, and the painter's own copy of the settings
 * (src/port/paint.c, outside the state; port_paint.h). */

#include "common.h"

/* The longest command, a shaded textured quad, in words. */
#define COMMAND_WORDS_MAX 12

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
    s32 is_line_painted; /* the game's side: the painter is given it */
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

/* The painter's settings and the command it is given (paint.c): the same
 * kind, outside the state. */
extern PortGpu g_port_paint;

#endif /* PORT_RASTER_H */
