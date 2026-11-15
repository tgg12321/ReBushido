/* Textures (tex.h): loading a file's TIM images into video memory, the
 * black and white option, and tinting a colour table. Compiled with small
 * data.
 *
 * A texture file starts with a table of offsets to its groups. A group is
 * a count and that many 12-byte entries: the offset of a TIM in the file
 * (its low two bits are not part of it), then where its picture and its
 * colour table go in video memory, two halfwords each. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static void tex_GrayscalePixels(
    u16 *src_pixels, s32 pixel_count, u16 *write_cursor);

/* Defined further down. */
static void tex_LoadTim();
static void tex_LoadTimClut();
static void tex_ReadTim();

/* ALIGN4_SIGNED on an offset kept signed: the shift is an arithmetic one. */
#define ALIGN4_SIGNED(x) (((x) >> 2) << 2)

/* A TIM is a word that begins with TIM_ID, a word of flags, and then its
 * colour table, if it has one, and its picture. Each of the two is a block:
 * its length in bytes, its place in video memory (two halfwords, which the
 * game does not use), its width and height, and the pixels. */
#define TIM_ID 0x10      /* the first byte of a TIM */
#define TIM_MODE_MASK 7  /* in its flags: how many bits a pixel is */
#define TIM_HAS_CLUT 8   /* in its flags */
#define TIM_BLOCK_SIZE 8 /* from a block's start to its width and height */

/* The size of a group's entry, in bytes. */
#define TEX_ENTRY_SIZE 12

/* Room for the largest colour table made gray: 256 colours, twice over. */
#define GRAY_BUFFER_COLORS 512
/* The most colours tex_TintClut tints at once. */
#define TINT_BUFFER_COLORS 256

/* Loads every TIM of group `group_index` where its entry says. `cursor`
 * comes in as the file and then goes through the group. */
void tex_LoadGroup(u32 *cursor, s32 group_index) {
    u32 unused[8]; /* nothing uses it; it only takes room in the frame */
    s32 file;
    s32 count;
    u32 group_offset;
    file = (s32)cursor;
    cursor += (s16)group_index;
    group_offset = *cursor;
    cursor = (u32 *)ADDR(file + ((group_offset >> 2) << 2));
    count = *(cursor++);
    if (count == 0) {
        return;
    }
    while (count-- != 0) {
        {
            u32 tim_offset;
            s16 image_x;
            s16 image_y;
            s16 clut_x;
            s16 clut_y;
            s32 tim_addr;
            tim_offset = *cursor++;
            image_x = *((u16 *)cursor)++;
            image_y = *((u16 *)cursor)++;
            clut_x = *((u16 *)cursor)++;
            tim_addr = file + ALIGN4_SIGNED(tim_offset);
            clut_y = *((u16 *)cursor)++;
            tex_LoadTim(tim_addr, image_x, image_y, clut_x, clut_y);
        }
    }
}

/* The same, with everything moved by (x_offset, y_offset). */
void tex_LoadGroupAt(u32 *cursor, s32 group_index, s16 x_offset, s16 y_offset) {
    u32 unused[8]; /* nothing uses it; it only takes room in the frame */
    s32 file;
    s32 count;
    s32 tim_addr;
    file = (s32)cursor;
    cursor += (s16)group_index;
    cursor = (u32 *)ADDR(file + (((*cursor) >> 2) << 2));
    count = *(cursor++);
    if (count == 0) {
        return;
    }
    while (count-- != 0) {
        u32 tim_offset;
        s16 image_x;
        s16 image_y;
        s16 clut_x;
        s16 clut_y;
        tim_offset = *cursor++;
        image_x = *((u16 *)cursor)++;
        image_y = *((u16 *)cursor)++;
        tim_addr = file + ((tim_offset >> 2) << 2);
        clut_x = *((u16 *)cursor)++;
        clut_y = *((u16 *)cursor)++;
        tex_LoadTim(tim_addr, image_x + x_offset, image_y + y_offset,
                    clut_x + x_offset, clut_y + y_offset);
    }
}

/* The same, the pictures moved by one offset and the colour tables by
 * another. */
void tex_LoadGroupAtSplit(
    u32 *cursor, s32 group_index, s16 image_x_offset, s16 image_y_offset,
    s16 clut_x_offset, s16 clut_y_offset) {
    u32 unused[8]; /* nothing uses it; it only takes room in the frame */
    s32 file;
    s32 count;
    s32 tim_addr;
    file = (s32)cursor;
    cursor += (s16)group_index;
    cursor = (u32 *)ADDR(file + (((*cursor) >> 2) << 2));
    count = *(cursor++);
    if (count == 0) {
        return;
    }
    while (count-- != 0) {
        u32 tim_offset;
        s16 image_x;
        s16 image_y;
        s16 clut_x;
        s16 clut_y;
        tim_offset = *cursor++;
        image_x = *((u16 *)cursor)++;
        image_y = *((u16 *)cursor)++;
        tim_addr = file + ((tim_offset >> 2) << 2);
        clut_x = *((u16 *)cursor)++;
        clut_y = *((u16 *)cursor)++;
        tex_LoadTim(
            tim_addr, image_x + image_x_offset, image_y + image_y_offset,
            clut_x + clut_x_offset, clut_y + clut_y_offset);
    }
}

/* A stage's textures. Its colour tables left of x 640 go one row lower than
 * the file says: the row above them is the one the fog rewrites while the
 * frame is drawn (stage.h). */
void tex_LoadStageGroup(u32 *cursor, s16 group_index) {
    u32 unused[8]; /* nothing uses it; it only takes room in the frame */
    u32 *file;
    s32 count;
    s32 tim_addr;
    file = cursor;
    cursor += group_index;
    cursor = (u32 *)ADDR((s32)file + (((*cursor) >> 2) << 2));
    count = *(cursor++);
    if (count == 0) {
        return;
    }
    while (count-- != 0) {
        u32 tim_offset;
        s16 image_x;
        s16 image_y;
        s16 clut_x;
        s16 clut_y;
        tim_offset = *cursor++;
        image_x = *((u16 *)cursor)++;
        image_y = *((u16 *)cursor)++;
        clut_x = *((u16 *)cursor)++;
        clut_y = *(u16 *)cursor;
        tim_addr = (s32)file + ALIGN4_SIGNED(tim_offset);
        ((u16 *)cursor)++;
        if (clut_x < DISP_WIDTH) {
            clut_y += 1;
        }
        tex_LoadTim(tim_addr, image_x, image_y, clut_x, clut_y);
    }
}

/* Loads one TIM: its picture at (image_x, image_y) and, if it has one, its
 * colour table at (clut_x, clut_y), made gray first when the black and white
 * option is on. Defined in the old style: the callers pass ints with no
 * prototype in sight. */
static void tex_LoadTim(tim, image_x, image_y, clut_x, clut_y) u8 *tim;
s16 image_x;
s16 image_y;
s16 clut_x;
u16 clut_y;
{
    RECT rect;
    u16 gray[GRAY_BUFFER_COLORS];
    u8 *clut_block;
    s32 has_clut;
    u32 size;
    u32 clut_size;
    s32 id = tim[0];
    tim += 4;
    if (id != TIM_ID) {
        return;
    }
    has_clut = *(s32 *)tim & TIM_HAS_CLUT;
    tim += 4;
    if (has_clut) {
        /* the colour table's block comes first; its length leads to the
         * picture's */
        clut_block = tim;
        tim = clut_block + ((*(u32 *)clut_block >> 2) << 2);
        clut_block += 4;
    }
    tim += TIM_BLOCK_SIZE;
    rect.x = image_x;
    rect.y = image_y;
    size = *(u32 *)tim;
    rect.h = size >> 16;
    rect.w = size;
    LoadImage(&rect, (u32 *)(tim + 4));
    if (!has_clut) {
        return;
    }
    clut_block += 4;
    rect.x = clut_x;
    rect.y = clut_y;
    clut_size = *(u32 *)clut_block;
    clut_block += 4;
    rect.h = clut_size >> 16;
    rect.w = clut_size;
    if (tex_IsGrayscale()) {
        tex_GrayscalePixels((u16 *)clut_block, rect.w, gray);
        LoadImage(&rect, (u32 *)gray);
        DrawSync(0);
        return;
    }
    LoadImage(&rect, (u32 *)clut_block);
}

/* Loads only the colour tables of a group, moved by an offset. The entries
 * are read as an offset, a skipped word and the colour table's place.
 * Nothing calls it. */
static void tex_LoadGroupCluts(
    s32 cursor, s16 group_index, s16 clut_x_offset, s16 clut_y_offset) {
    s32 file;
    s32 count;
    s32 group_offset;
    file = cursor;
    group_offset = ((s32 *)ADDR(cursor))[group_index];
    cursor += group_offset;
    count = *(s32 *)ADDR(cursor);
    cursor += 4;
    if (count == 0) {
        return;
    }
    while (count-- != 0) {
        s32 tim;
        u32 raw_x;
        u32 raw_y;
        s32 clut_x;
        s32 clut_y;
        tim = file + *(s32 *)ADDR(cursor);
        cursor += 8; /* past the offset and the picture's place */
        raw_x = *(u16 *)ADDR(cursor);
        cursor += 2;
        raw_y = *(u16 *)ADDR(cursor);
        cursor += 2;
        clut_x = ((s32)(raw_x << 16)) >> 16;
        clut_y = ((s32)(raw_y << 16)) >> 16;
        tex_LoadTimClut(tim, clut_x + clut_x_offset, clut_y + clut_y_offset);
    }
}

/* Loads the colour table of one TIM at (clut_x, clut_y). Defined in the
 * old style, as tex_LoadTim is. */
static void tex_LoadTimClut(tim, clut_x, clut_y) u8 *tim;
s16 clut_x;
s16 clut_y;
{
    RECT rect;
    u16 gray[GRAY_BUFFER_COLORS];
    u32 size;
    s32 flags;
    if (tim[0] != TIM_ID) {
        return;
    }
    tim += 4;
    flags = *(s32 *)tim;
    tim += 4;
    if (!(flags & TIM_HAS_CLUT)) {
        return;
    }
    tim += TIM_BLOCK_SIZE;
    rect.x = clut_x;
    rect.y = clut_y;
    size = *(u32 *)tim;
    tim += 4;
    rect.h = size >> 16;
    rect.w = size;
    if (tex_IsGrayscale()) {
        tex_GrayscalePixels((u16 *)tim, rect.w, gray);
        LoadImage(&rect, (u32 *)gray);
        return;
    }
    LoadImage(&rect, (u32 *)tim);
}

/* Describes entry `entry_index` of a group in the TimInfo at `info`,
 * loading nothing. Returns the group's count, or -1 if it has no such
 * entry. Nothing calls it. */
static s32 tex_ReadGroupEntry(
    s32 cursor, s32 group_index, u32 entry_index, s32 info) {
    s32 file, count, tim, image_x, image_y, clut_x, clut_y, group_offset;
    group_offset = ((s32 *)ADDR(cursor))[group_index];
    file = cursor;
    cursor = group_offset + file;
    count = *(s32 *)ADDR(cursor);
    cursor += 4;
    if (entry_index >= count) {
        return -1;
    }
    cursor += entry_index * TEX_ENTRY_SIZE;
    tim = *(s32 *)ADDR(cursor);
    cursor += 4;
    image_x = *(u16 *)ADDR(cursor);
    cursor += 2;
    image_y = *(u16 *)ADDR(cursor);
    cursor += 2;
    clut_x = *(u16 *)ADDR(cursor);
    cursor += 2;
    clut_y = *(u16 *)ADDR(cursor);
    tim += file;
    tex_ReadTim(
        tim, info, (s16)image_x, (s16)image_y, (s16)clut_x, (s16)clut_y);
    return count;
}

/* What a TIM holds and where it would go: Sony's TIM_IMAGE with the places
 * filled in, and the texture page and colour-table id that primitives using
 * it would carry. */
typedef struct {
    s16 mode;      /* +0x00 bits a pixel: 0 four, 1 eight, 2 sixteen */
    s16 x;         /* +0x02 */
    s16 y;         /* +0x04 */
    s16 w;         /* +0x06 */
    s16 h;         /* +0x08 */
    s16 cx;        /* +0x0A */
    s16 cy;        /* +0x0C */
    s16 cw;        /* +0x0E */
    s16 ch;        /* +0x10 */
    u16 tpage;     /* +0x12 */
    u16 clut;      /* +0x14 */
    u32 *pixdata;  /* +0x18 */
    u32 *clutdata; /* +0x1C */
} TimInfo;

/* Fills in `info` for the TIM at `tim`, were its picture loaded at (x, y)
 * and its colour table at (cx, cy). Defined in the old style, as
 * tex_LoadTim is. */
static void tex_ReadTim(tim, info, x, y, cx, cy) u32 *tim;
TimInfo *info;
s16 x, y;
u16 cx, cy;
{
    u32 flags;
    u32 *block;

    if (*(u8 *)tim++ == TIM_ID) {
        flags = *tim++;
        info->mode = flags & TIM_MODE_MASK;
        if (flags & TIM_HAS_CLUT) {
            u32 length;

            block = tim;
            length = *block;
            info->cx = cx;
            info->cy = cy;
            tim = block + (length >> 2);
            block += 2;
            info->ch = ((u16 *)block)[1];
            info->cw = *block++;
            info->clutdata = block;
            info->clut = GetClut(info->cx, info->cy);
        } else {
            info->clut = 0;
        }
        /* FAKE: block = tim + 2, after the two stores */
        block = tim;
        info->x = x;
        info->y = y;
        block += 2;
        info->h = ((u16 *)block)[1];
        info->w = *block++;
        info->pixdata = block;
        /* the page is named by its corner */
        info->tpage =
            GetTPage(info->mode, 0, info->x & (u16) ~(TPAGE_WIDTH - 1),
                     info->y & (u16) ~(TPAGE_HEIGHT - 1));
    }
}

/* ---- black and white ---- */

/* Whether the option is on, asking the display each time. */
s32 tex_IsGrayscale(void) {
    if (disp_IsGrayscale()) {
        g_scene_settings.grayscale = 1;
    } else {
        g_scene_settings.grayscale = 0;
    }
    return g_scene_settings.grayscale;
}

/* Nothing calls it, and the next question overwrites what it sets. */
static void tex_SetGrayscale(s32 grayscale) {
    if (grayscale) {
        g_scene_settings.grayscale = 1;
    } else {
        g_scene_settings.grayscale = 0;
    }
}

/* The gray of a colour: 0x547, 0x800 and 0x2B8 are the weights of red,
 * green and blue in 4096ths (0.33, 0.5, 0.17). Green's is a shift. */
#define GRAY_RED 0x547
#define GRAY_GREEN_SHIFT 11
#define GRAY_BLUE 0x2B8

/* Makes `pixel_count` 15-bit pixels gray, keeping each one's top bit. */
static void tex_GrayscalePixels(
    u16 *src_pixels, s32 pixel_count, u16 *write_cursor) {
    s32 gray;
    s32 shifted;
    s32 countdown;
    u16 *read_cursor;
    s32 source_pixel;
    read_cursor = src_pixels;
    for (countdown = pixel_count - 1; countdown != -1; countdown--) {
        s32 b;
        s32 g;
        s32 weighted_blue;
        source_pixel = *read_cursor++;
        /* the pixel in the top half of a word: the shifts below start from
         * there */
        shifted = source_pixel << PIXEL_BITS;
        gray = source_pixel & PIXEL_PART_MASK;
        gray = gray * GRAY_RED;
        b = (shifted >> (PIXEL_BITS + PIXEL_BLUE_SHIFT)) & PIXEL_PART_MASK;
        /* green, left where its weight's shift would put it */
        g = (shifted >> (PIXEL_BITS + PIXEL_GREEN_SHIFT - GRAY_GREEN_SHIFT)) &
            (PIXEL_PART_MASK << GRAY_GREEN_SHIFT);
        weighted_blue = b * GRAY_BLUE;
        gray = ((gray + g + weighted_blue) >> 12) & PIXEL_PART_MASK;
        *write_cursor++ =
            (((source_pixel & ~(PIXEL_TOP_BIT - 1)) +
              (gray << PIXEL_BLUE_SHIFT)) +
             (gray << PIXEL_GREEN_SHIFT)) +
            gray;
    }
}

/* The gray of a colour given as its three parts. */
s32 math_Grayscale3(s32 red, s32 green, s32 blue) {
    red = red * GRAY_RED;
    green = green << GRAY_GREEN_SHIFT;
    blue = blue * GRAY_BLUE;
    return (red + green + blue) >> 12;
}

/* ---- tinting ---- */

/* Reads the `w` colours at (sx, sy) in video memory, multiplies them by
 * (mr, mg, mb) and writes them at (dx, dy). In TEX_TINT_SCALE each part is
 * scaled by its own multiplier, ONE leaving it as it is; in TEX_TINT_GRAY
 * the colour's brightness is. Colour 0, the transparent one, is left alone.
 * A part is worked on as eight bits: the shifts by 15 are the multiplier's
 * 12 and the three back down to five. */
static void tex_TintClut(
    s32 mode, s32 sx, s32 sy, s32 w, s32 mr, s32 mg, s32 mb, s32 dx, s32 dy) {
    u16 buf[TINT_BUFFER_COLORS];
    u16 out[TINT_BUFFER_COLORS];
    RECT rect;
    u16 *src;
    u16 *dst;
    s32 i;
    u16 pixel;
    s32 r, g, b, top_bit;

    DrawSync(0);
    rect.x = sx;
    rect.y = sy;
    rect.w = w;
    rect.h = 1;
    StoreImage(&rect, (u32 *)buf);
    DrawSync(0);
    src = buf;
    dst = out;
    for (i = 0; i < w; i++) {
        pixel = *src;
        if (pixel == 0) {
            *dst++ = *src++;
            continue;
        }
        r = (pixel & PIXEL_PART_MASK) << 3;
        g = ((pixel >> PIXEL_GREEN_SHIFT) & PIXEL_PART_MASK) << 3;
        b = ((pixel >> PIXEL_BLUE_SHIFT) & PIXEL_PART_MASK) << 3;
        top_bit = pixel & PIXEL_TOP_BIT;
        src++;
        switch (mode) {
        case TEX_TINT_SCALE:
            r = (r * mr) >> 15;
            g = (g * mg) >> 15;
            b = (b * mb) >> 15;
            break;
        case TEX_TINT_GRAY:
            /* The original's behaviour: green and blue are scaled from the
             * red that has just been scaled, not from the gray. Nothing
             * asks for this mode. */
            r = r * GRAY_RED;
            g = g << GRAY_GREEN_SHIFT;
            b = b * GRAY_BLUE;
            r = (r + g + b) >> 15;
            r = (r * mr) >> 12;
            g = (r * mg) >> 12;
            b = (r * mb) >> 12;
            break;
        }
        r &= PIXEL_PART_MASK;
        g &= PIXEL_PART_MASK;
        b &= PIXEL_PART_MASK;
        *dst++ =
            top_bit | r | (g << PIXEL_GREEN_SHIFT) | (b << PIXEL_BLUE_SHIFT);
    }
    rect.x = dx;
    rect.y = dy;
    LoadImage(&rect, (u32 *)out);
    DrawSync(0);
}

/* The same in TEX_TINT_SCALE, written back where it was read. */
void tex_TintClutInPlace(
    s32 x, s32 y, s32 width, s32 red, s32 green, s32 blue) {
    tex_TintClut(TEX_TINT_SCALE, x, y, width, red, green, blue, x, y);
}
