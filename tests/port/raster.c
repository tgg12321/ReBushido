/* The port's GPU: what it draws, pixel by pixel, in cases made up here. */
/* The PlayStation's GPU in C (src/port/raster.c), Sony's libgpu on it
 * (src/port/gpu.c) and the pictures it writes (src/port/picture.c). Every
 * expected pixel is worked out here, by hand or by a second, plainer way
 * of saying the same rule; none was taken from what the rasteriser drew.
 * The primitives, textures and colours are made up; nothing is the
 * game's. */
#include "common.h"
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>
#include "src/main/psxsdk/libetc/vmode.c"
#include "src/port/raster.c"
#include "src/port/gpu.c"
#include "src/port/picture.c"
#include "runtime/runtime.h"

/* ---- what those files want of the rest of the port ---- */
/* The remastered renderer is not in this test: what the GPU is given
 * goes nowhere else (tests/port/remaster.c holds that side). */
void port_RemasterCommand(const u32 *words, s32 count) {
    (void)words;
    (void)count;
}

void port_RemasterLoadedVram(s32 x, s32 y, s32 w, s32 h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void port_RemasterNewFrame(void) {}

void port_RemasterSetPrim(PortAddress prim) { (void)prim; }


void port_Fail(const char *format, ...) {
    test_Count(0, __FILE__, __LINE__, "the port stopped itself: %s", format);
    for (;;) {
    }
}

/* What picture.c writes goes here. */
#define WRITTEN_MAX 0x110000
static u8 g_written[WRITTEN_MAX];
static u32 g_written_count;

s32 port_HostAppend(s32 file, const void *bytes, u32 count) {
    if (g_written_count + count > WRITTEN_MAX) {
        return 0;
    }
    test_Copy(g_written + g_written_count, bytes, count);
    g_written_count += count;
    return 1;
}

/* ---- writing commands ---- */

#define XY(x, y) ((((u32)(y) & 0xFFFF) << 16) | ((u32)(x) & 0xFFFF))
#define RGB(r, g, b) (((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))
#define CODE(code) ((u32)(code) << 24)
/* A pixel of three five-bit channels. */
#define PIXEL(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))
#define WHITE RGB(255, 255, 255)
#define WHITE_PIXEL 0x7FFF

static void gp0(u32 word) { port_WriteGp0(word); }

static u16 pixel_at(s32 x, s32 y) { return g_port_vram[y * 1024 + x]; }

static void set_area(s32 left, s32 top, s32 right, s32 bottom) {
    gp0(0xE3000000 | ((u32)top << 10) | (u32)left);
    gp0(0xE4000000 | ((u32)bottom << 10) | (u32)right);
}

static void set_offset(s32 x, s32 y) {
    gp0(0xE5000000 | (((u32)y & 0x7FF) << 11) | ((u32)x & 0x7FF));
}

/* Video memory black, the GPU as at power on, and all of video memory
 * the drawing area. */
static void start_over(void) {
    s32 i;

    for (i = 0; i < 1024 * 512; i++) {
        g_port_vram[i] = 0;
    }
    port_WriteGp1(0);
    set_area(0, 0, 1023, 511);
    port_SetGpuLikeEmulator(0);
}

/* How many pixels of a rectangle are not black. */
static s32 count_drawn(s32 left, s32 top, s32 w, s32 h) {
    s32 count = 0;
    s32 x;
    s32 y;

    for (y = top; y < top + h; y++) {
        for (x = left; x < left + w; x++) {
            count += pixel_at(x, y) != 0;
        }
    }
    return count;
}

static void rectangle(u32 code, u32 color, s32 x, s32 y, s32 w, s32 h) {
    gp0(CODE(code) | color);
    gp0(XY(x, y));
    gp0(XY(w, h));
}

/* The fill, which only starts on multiples of 16. */
static void fill(u32 color, s32 x, s32 y, s32 w, s32 h) {
    gp0(CODE(0x02) | color);
    gp0(XY(x, y));
    gp0(XY(w, h));
}

/* Pixels into video memory, `count` of them, by the load command. */
static void load(s32 x, s32 y, s32 w, s32 h, const u16 *pixels) {
    s32 count = w * h;
    s32 i;

    gp0(CODE(0xA0));
    gp0(XY(x, y));
    gp0(XY(w, h));
    for (i = 0; i < count; i += 2) {
        gp0(pixels[i] | (i + 1 < count ? (u32)pixels[i + 1] << 16 : 0));
    }
}

/* ---- the fill rule ---- */

/* The rule, said the other way round from how the rasteriser works: a
 * pixel is drawn if it is on the inner side of all three edges, or on an
 * edge whose inside is to its right, or below it if it lies flat. */
static s32 inside_by_rule(const s32 *corners, s32 x, s32 y) {
    s32 area = (corners[2] - corners[0]) * (corners[5] - corners[1]) -
               (corners[4] - corners[0]) * (corners[3] - corners[1]);
    s32 i;

    if (area == 0) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        s32 from_x = corners[i * 2];
        s32 from_y = corners[i * 2 + 1];
        s32 to_x = corners[(i + 1) % 3 * 2];
        s32 to_y = corners[(i + 1) % 3 * 2 + 1];
        s32 side =
            (to_x - from_x) * (y - from_y) - (to_y - from_y) * (x - from_x);
        /* which way is inwards from the edge: across, and down */
        s32 inwards_x = -(to_y - from_y);
        s32 inwards_y = to_x - from_x;

        if (area < 0) {
            side = -side;
            inwards_x = -inwards_x;
            inwards_y = -inwards_y;
        }
        if (side < 0) {
            return 0;
        }
        if (side == 0 &&
            !(inwards_x > 0 || (inwards_x == 0 && inwards_y > 0))) {
            return 0;
        }
    }
    return 1;
}

static u32 g_seed = 12345;

/* A made-up number from 0 to `below` - 1. */
static s32 next_number(s32 below) {
    g_seed = g_seed * 1103515245 + 12345;
    return (s32)((g_seed >> 8) % (u32)below);
}

static void test_FillRule(void) {
    s32 corners[8];
    s32 wrong;
    s32 twice;
    s32 x;
    s32 y;
    s32 i;
    s32 round;

    /* by hand: the triangle (0,0) (4,0) (0,4) is the ten pixels with
     * x + y < 4. Its top and left edges are drawn, the slanted one not */
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(0, 0));
    gp0(XY(4, 0));
    gp0(XY(0, 4));
    wrong = 0;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            wrong += (pixel_at(x, y) == WHITE_PIXEL) != (x + y < 4);
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(count_drawn(0, 0, 16, 16), 10);
    /* the same corners the other way round: the same pixels */
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(0, 4));
    gp0(XY(4, 0));
    gp0(XY(0, 0));
    CHECK_EQ(count_drawn(0, 0, 16, 16), 10);
    CHECK_EQ(pixel_at(3, 0), WHITE_PIXEL);
    CHECK_EQ(pixel_at(4, 0), 0);
    CHECK_EQ(pixel_at(0, 4), 0);
    /* a triangle with no area draws nothing */
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(0, 0));
    gp0(XY(4, 4));
    gp0(XY(8, 8));
    CHECK_EQ(count_drawn(0, 0, 16, 16), 0);

    /* made-up triangles, each held to the rule above, pixel by pixel,
     * inside a drawing area of 40 by 40 that they reach out of */
    wrong = 0;
    for (round = 0; round < 300; round++) {
        start_over();
        set_area(0, 0, 39, 39);
        for (i = 0; i < 6; i++) {
            corners[i] = next_number(56) - 8;
        }
        gp0(CODE(0x20) | WHITE);
        gp0(XY(corners[0], corners[1]));
        gp0(XY(corners[2], corners[3]));
        gp0(XY(corners[4], corners[5]));
        for (y = 0; y < 48; y++) {
            for (x = 0; x < 48; x++) {
                s32 want = x < 40 && y < 40 && inside_by_rule(corners, x, y);

                wrong += (pixel_at(x, y) != 0) != want;
            }
        }
    }
    CHECK_EQ(wrong, 0);

    /* made-up four-sided primitives, drawn by adding 1 to what is there:
     * the two triangles, the first three corners and the last three,
     * leave no pixel of their shared edge out and draw none twice */
    wrong = 0;
    twice = 0;
    for (round = 0; round < 300; round++) {
        start_over();
        gp0(0xE1000000 | (1 << 5)); /* what is there and what is drawn, added */
        for (i = 0; i < 8; i++) {
            corners[i] = next_number(40);
        }
        gp0(CODE(0x2A) | RGB(8, 8, 8));
        for (i = 0; i < 4; i++) {
            gp0(XY(corners[i * 2], corners[i * 2 + 1]));
        }
        for (y = 0; y < 40; y++) {
            for (x = 0; x < 40; x++) {
                s32 want = inside_by_rule(corners, x, y) +
                           inside_by_rule(corners + 2, x, y);

                wrong += pixel_at(x, y) != PIXEL(want, want, want);
                /* a quad that folds over itself is drawn twice where it
                 * does; one that does not is not, anywhere */
                twice += want == 2;
            }
        }
    }
    CHECK_EQ(wrong, 0);
    /* a square is not folded: every pixel once, its right and bottom
     * edges left to whatever is beside it */
    start_over();
    gp0(0xE1000000 | (1 << 5));
    gp0(CODE(0x2A) | RGB(8, 8, 8));
    gp0(XY(2, 2));
    gp0(XY(10, 2));
    gp0(XY(2, 10));
    gp0(XY(10, 10));
    wrong = 0;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            s32 want = x >= 2 && x < 10 && y >= 2 && y < 10;

            wrong += pixel_at(x, y) != PIXEL(want, want, want);
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK(twice > 0); /* some of the made-up ones were folded */
}

/* ---- sizes, the drawing area, the offset ---- */

static void test_Limits(void) {
    /* 1023 across is drawn, 1024 is not */
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(-1, 0));
    gp0(XY(1022, 0));
    gp0(XY(-1, 1));
    CHECK_EQ(pixel_at(0, 0), WHITE_PIXEL);
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(-1, 0));
    gp0(XY(1023, 0));
    gp0(XY(-1, 1));
    CHECK_EQ(count_drawn(0, 0, 1024, 2), 0);
    /* 511 down is drawn, 512 is not */
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(0, 0));
    gp0(XY(1, 0));
    gp0(XY(0, 511));
    CHECK_EQ(pixel_at(0, 0), WHITE_PIXEL);
    start_over();
    gp0(CODE(0x20) | WHITE);
    gp0(XY(0, -1));
    gp0(XY(1, -1));
    gp0(XY(0, 511));
    CHECK_EQ(count_drawn(0, 0, 4, 512), 0);
    /* a line likewise */
    start_over();
    gp0(CODE(0x40) | WHITE);
    gp0(XY(-1, 3));
    gp0(XY(1023, 3));
    CHECK_EQ(count_drawn(0, 0, 1024, 8), 0);

    /* the drawing area keeps its last pixels and nothing past them */
    start_over();
    set_area(2, 2, 5, 5);
    rectangle(0x60, WHITE, 0, 0, 8, 8);
    CHECK_EQ(count_drawn(0, 0, 16, 16), 16);
    CHECK_EQ(pixel_at(2, 2), WHITE_PIXEL);
    CHECK_EQ(pixel_at(5, 5), WHITE_PIXEL);
    CHECK_EQ(pixel_at(1, 2), 0);
    CHECK_EQ(pixel_at(6, 5), 0);
    /* an area whose end is before its start has no pixel */
    start_over();
    set_area(5, 5, 2, 2);
    rectangle(0x60, WHITE, 0, 0, 8, 8);
    CHECK_EQ(count_drawn(0, 0, 16, 16), 0);

    /* the offset moves what is drawn, either way */
    start_over();
    set_offset(3, 1);
    rectangle(0x60, WHITE, 0, 0, 1, 1);
    CHECK_EQ(pixel_at(3, 1), WHITE_PIXEL);
    set_offset(-2, -1);
    rectangle(0x60, WHITE, 6, 6, 1, 1);
    CHECK_EQ(pixel_at(4, 5), WHITE_PIXEL);
    CHECK_EQ(count_drawn(0, 0, 16, 16), 2);

    /* the three fixed sizes of rectangle, and one of no width */
    start_over();
    gp0(CODE(0x68) | WHITE);
    gp0(XY(40, 60));
    CHECK_EQ(count_drawn(32, 56, 32, 6), 1);
    gp0(CODE(0x70) | WHITE);
    gp0(XY(40, 62));
    CHECK_EQ(count_drawn(32, 62, 32, 10), 64);
    gp0(CODE(0x78) | WHITE);
    gp0(XY(40, 72));
    CHECK_EQ(count_drawn(32, 72, 32, 20), 256);
    rectangle(0x60, WHITE, 100, 100, 0, 5);
    CHECK_EQ(count_drawn(96, 96, 16, 16), 0);
}

/* ---- textures ---- */

/* A 15-bit texture of four pixels at (320, 0): white, nothing, red with
 * the top bit, green. */
static const u16 g_texture_15[4] = {0x7FFF, 0x0000, 0x801F, 0x03E0};
#define PAGE_15 (0x100 | 5)
/* A 4-bit texture at (384, 0): sixteen texels that are their own
 * numbers, and its colour table at (0, 480), where colour `n` is
 * 0x0400 + n. */
static const u16 g_texture_4[4] = {0x3210, 0x7654, 0xBA98, 0xFEDC};
#define PAGE_4 6
#define CLUT 0x7800 /* (0, 480) */
/* An 8-bit texture at (448, 0): texels 1, 2, 3 and 15. */
static const u16 g_texture_8[2] = {0x0201, 0x0F03};
#define PAGE_8 (0x80 | 7)

static void load_textures(void) {
    u16 colors[16];
    s32 i;

    for (i = 0; i < 16; i++) {
        colors[i] = (u16)(0x0400 + i);
    }
    load(320, 0, 4, 1, g_texture_15);
    load(384, 0, 4, 1, g_texture_4);
    load(448, 0, 2, 1, g_texture_8);
    load(0, 480, 16, 1, colors);
}

/* A textured rectangle: `code`, at (x, y), `w` wide and one high, from
 * the texture's first texel. */
static void sprite(u32 code, u32 color, s32 x, s32 y, s32 w) {
    gp0(CODE(code) | color);
    gp0(XY(x, y));
    gp0((u32)CLUT << 16);
    gp0(XY(w, 1));
}

static void test_Textures(void) {
    s32 i;

    start_over();
    fill(RGB(8, 16, 24), 0, 0, 32, 32);
    CHECK_EQ(pixel_at(0, 0), PIXEL(1, 2, 3));
    load_textures();

    /* 15 bits, as they are: a texel of nothing is not drawn */
    gp0(0xE1000000 | PAGE_15);
    sprite(0x65, 0, 10, 10, 4);
    CHECK_EQ(pixel_at(10, 10), 0x7FFF);
    CHECK_EQ(pixel_at(11, 10), PIXEL(1, 2, 3));
    CHECK_EQ(pixel_at(12, 10), 0x801F);
    CHECK_EQ(pixel_at(13, 10), 0x03E0);
    /* times the colour, 128 being one: a quarter, one, and nearly two,
     * which stops at 31. The top bit is the texel's */
    sprite(0x64, RGB(64, 128, 255), 10, 11, 4);
    CHECK_EQ(pixel_at(10, 11), PIXEL(15, 31, 31));
    CHECK_EQ(pixel_at(11, 11), PIXEL(1, 2, 3));
    CHECK_EQ(pixel_at(12, 11), 0x8000 | PIXEL(15, 0, 0));
    CHECK_EQ(pixel_at(13, 11), PIXEL(0, 31, 0));

    /* 4 bits: four texels a word, the lowest first, through the table */
    gp0(0xE1000000 | PAGE_4);
    sprite(0x65, 0, 10, 12, 16);
    for (i = 0; i < 16; i++) {
        CHECK_EQ(pixel_at(10 + i, 12), 0x0400 + i);
    }
    /* 8 bits: two a word */
    gp0(0xE1000000 | PAGE_8);
    sprite(0x65, 0, 10, 14, 4);
    CHECK_EQ(pixel_at(10, 14), 0x0401);
    CHECK_EQ(pixel_at(11, 14), 0x0402);
    CHECK_EQ(pixel_at(12, 14), 0x0403);
    CHECK_EQ(pixel_at(13, 14), 0x040F);

    /* the texture window: eight texels wide, starting at the eighth, so
     * the first eight coordinates read the second eight texels, and the
     * second eight read them again */
    gp0(0xE1000000 | PAGE_4);
    gp0(0xE2000000 | (1 << 10) | 1);
    sprite(0x65, 0, 10, 16, 16);
    for (i = 0; i < 16; i++) {
        CHECK_EQ(pixel_at(10 + i, 16), 0x0408 + (i & 7));
    }
    gp0(0xE2000000);

    /* a textured triangle brings its own page, which stays the GPU's,
     * and its coordinates run with its corners: eight texels over eight
     * pixels */
    gp0(0xE1000000 | PAGE_15);
    gp0(CODE(0x25));
    gp0(XY(20, 20));
    gp0((u32)CLUT << 16);
    gp0(XY(28, 20));
    gp0(((u32)PAGE_4 << 16) | 8);
    gp0(XY(20, 28));
    gp0(0);
    for (i = 0; i < 8; i++) {
        CHECK_EQ(pixel_at(20 + i, 20), 0x0400 + i);
    }
    CHECK_EQ(port_GetGpuStatus() & 0x1FF, PAGE_4);
    /* a rectangle's coordinates go round after 255 */
    gp0(CODE(0x65));
    gp0(XY(40, 20));
    gp0(((u32)CLUT << 16) | 254);
    gp0(XY(4, 1));
    CHECK_EQ(pixel_at(42, 20), 0x0400);
    CHECK_EQ(pixel_at(43, 20), 0x0401);
}

/* ---- half-transparency and the mask ---- */

static void test_Blending(void) {
    /* what is there: 20, 10, 31. What is drawn: 8, 24, 4 */
    static const u16 want[4] = {
        PIXEL(14, 17, 17), /* half of each */
        PIXEL(28, 31, 31), /* added, stopping at 31 */
        PIXEL(12, 0, 27),  /* taken away, stopping at 0 */
        PIXEL(22, 16, 31), /* a quarter added */
    };
    s32 mode;

    start_over();
    fill(RGB(160, 80, 248), 0, 0, 16, 16);
    CHECK_EQ(pixel_at(0, 0), PIXEL(20, 10, 31));
    for (mode = 0; mode < 4; mode++) {
        gp0(0xE1000000 | ((u32)mode << 5));
        rectangle(0x62, RGB(64, 192, 32), mode, 0, 1, 1);
        CHECK_EQ(pixel_at(mode, 0), want[mode]);
        /* and not half-transparent: what is drawn */
        rectangle(0x60, RGB(64, 192, 32), mode, 1, 1, 1);
        CHECK_EQ(pixel_at(mode, 1), PIXEL(8, 24, 4));
    }
    /* a polygon takes its mix from its own page word */
    gp0(0xE1000000);
    gp0(CODE(0x26) | RGB(128, 128, 128));
    gp0(XY(0, 8));
    gp0(0);
    gp0(XY(4, 8));
    gp0((u32)(PAGE_15 | (2 << 5)) << 16);
    gp0(XY(0, 12));
    gp0(0);

    /* textured: only the texels with the top bit are mixed */
    load_textures();
    gp0(0xE1000000 | PAGE_15 | (1 << 5));
    sprite(0x67, 0, 0, 2, 4);
    CHECK_EQ(pixel_at(0, 2), 0x7FFF);
    CHECK_EQ(pixel_at(1, 2), PIXEL(20, 10, 31));
    CHECK_EQ(pixel_at(2, 2), 0x8000 | PIXEL(31, 10, 31));
    CHECK_EQ(pixel_at(3, 2), 0x03E0);
}

static void test_Mask(void) {
    static const u16 one[1] = {0x0001};

    start_over();
    /* drawn pixels are given the bit */
    gp0(0xE6000001);
    rectangle(0x60, WHITE, 0, 4, 1, 1);
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    load(2, 4, 1, 1, one);
    CHECK_EQ(pixel_at(2, 4), 0x8001);
    CHECK_EQ((port_GetGpuStatus() >> 11) & 3, 1);
    /* pixels that have it are left alone */
    gp0(0xE6000002);
    rectangle(0x60, RGB(8, 8, 8), 0, 4, 2, 1);
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    CHECK_EQ(pixel_at(1, 4), PIXEL(1, 1, 1));
    gp0(CODE(0x20) | RGB(16, 16, 16));
    gp0(XY(0, 4));
    gp0(XY(3, 4));
    gp0(XY(0, 5));
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    CHECK_EQ(pixel_at(1, 4), PIXEL(2, 2, 2));
    CHECK_EQ(pixel_at(2, 4), 0x8001);
    gp0(CODE(0x40) | RGB(24, 24, 24));
    gp0(XY(0, 4));
    gp0(XY(2, 4));
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    CHECK_EQ(pixel_at(1, 4), PIXEL(3, 3, 3));
    load(0, 4, 1, 1, one);
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    /* a copy inside video memory too */
    gp0(CODE(0x80));
    gp0(XY(1, 4));
    gp0(XY(0, 4));
    gp0(XY(1, 1));
    CHECK_EQ(pixel_at(0, 4), 0xFFFF);
    /* but the fill looks at nothing and sets nothing */
    gp0(0xE6000003);
    fill(RGB(8, 8, 8), 0, 4, 16, 1);
    CHECK_EQ(pixel_at(0, 4), PIXEL(1, 1, 1));
    CHECK_EQ(pixel_at(2, 4), PIXEL(1, 1, 1));
}

/* ---- dithering and shading ---- */

/* The chip's pattern, typed here a second time. */
static const s8 g_pattern[4][4] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2},
};

/* A colour byte as five bits at a pixel, dithered. */
static s32 dithered(s32 value, s32 x, s32 y) {
    value += g_pattern[y & 3][x & 3];
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return value >> 3;
}

/* A triangle over the sixteen pixels from (0, 32): shaded with the same
 * colour at each corner, flat, or flat and textured with one white
 * texel. */
static void dither_triangle(u32 code, s32 value) {
    u32 color = RGB(value, value, value);
    s32 i;

    for (i = 0; i < 3; i++) {
        if (i == 0 || (code & 0x10)) {
            gp0((i == 0 ? CODE(code) : 0) | color);
        }
        gp0(XY(i == 1 ? 8 : 0, i == 2 ? 40 : 32));
        if (code & 0x04) {
            gp0(i == 1 ? (u32)PAGE_15 << 16 : 0);
        }
    }
}

/* How many of the sixteen pixels are not `want(x, y)`. */
static s32 dither_wrong(s32 value, s32 with_pattern) {
    s32 wrong = 0;
    s32 x;
    s32 y;

    for (y = 32; y < 36; y++) {
        for (x = 0; x < 4; x++) {
            s32 channel = with_pattern ? dithered(value, x, y) : value >> 3;

            wrong += pixel_at(x, y) != PIXEL(channel, channel, channel);
        }
    }
    return wrong;
}

static void test_Dither(void) {
    static const s32 values[] = {6, 99, 2, 254};
    u32 i;
    s32 x;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        /* shaded and dithered */
        start_over();
        gp0(0xE1000200);
        dither_triangle(0x30, values[i]);
        CHECK_EQ(dither_wrong(values[i], 1), 0);
        /* shaded and not: the low three bits dropped */
        start_over();
        dither_triangle(0x30, values[i]);
        CHECK_EQ(dither_wrong(values[i], 0), 0);
        /* flat: never dithered */
        start_over();
        gp0(0xE1000200);
        dither_triangle(0x20, values[i]);
        CHECK_EQ(dither_wrong(values[i], 0), 0);
    }
    /* the pattern does something: at 6 some pixels are 0 and some 1 */
    start_over();
    gp0(0xE1000200);
    dither_triangle(0x30, 6);
    CHECK_EQ(pixel_at(0, 32), 0);
    CHECK_EQ(pixel_at(0, 33), PIXEL(1, 1, 1));
    /* a rectangle is not dithered */
    start_over();
    gp0(0xE1000200);
    rectangle(0x60, RGB(6, 6, 6), 0, 32, 4, 4);
    CHECK_EQ(count_drawn(0, 32, 4, 4), 0);

    /* a flat textured triangle whose texture is multiplied by the colour
     * is dithered: a white texel times 6 is 11 as a byte */
    start_over();
    load_textures();
    gp0(0xE1000200);
    dither_triangle(0x24, 6);
    CHECK_EQ(dither_wrong((31 * 6) >> 4, 1), 0);
    CHECK_EQ(pixel_at(0, 32), 0);
    CHECK_EQ(pixel_at(1, 32), PIXEL(1, 1, 1));
    /* ... but not when the port is told to do as the emulator does */
    start_over();
    load_textures();
    port_SetGpuLikeEmulator(1);
    gp0(0xE1000200);
    dither_triangle(0x24, 6);
    CHECK_EQ(dither_wrong((31 * 6) >> 4, 0), 0);
    /* (a shaded textured one still is) */
    dither_triangle(0x34, 6);
    CHECK_EQ(dither_wrong((31 * 6) >> 4, 1), 0);
    port_SetGpuLikeEmulator(0);
    /* and a texture drawn as it is never is */
    start_over();
    load_textures();
    gp0(0xE1000200);
    dither_triangle(0x25, 6);
    for (x = 0; x < 4; x++) {
        CHECK_EQ(pixel_at(x, 32 + x), 0x7FFF);
    }
    /* a line is, flat or shaded */
    start_over();
    gp0(0xE1000200);
    gp0(CODE(0x40) | RGB(6, 6, 6));
    gp0(XY(0, 33));
    gp0(XY(3, 33));
    for (x = 0; x < 4; x++) {
        CHECK_EQ(pixel_at(x, 33) & 31, dithered(6, x, 33));
    }
}

static void test_Shading(void) {
    s32 x;

    /* red from 0 to 128 over eight pixels, the same all the way down: 16
     * more a pixel, which is 2 more of the pixel's five bits */
    start_over();
    gp0(CODE(0x30));
    gp0(XY(0, 48));
    gp0(RGB(128, 0, 0));
    gp0(XY(8, 48));
    gp0(0);
    gp0(XY(0, 56));
    for (x = 0; x < 8; x++) {
        CHECK_EQ(pixel_at(x, 48), PIXEL(2 * x, 0, 0));
    }
    for (x = 0; x < 5; x++) {
        CHECK_EQ(pixel_at(x, 51), PIXEL(2 * x, 0, 0));
    }
    /* green from 0 to 248 down 31 lines, the same all the way across: 8
     * more a line, which is 1 more of the five bits; blue the other way.
     * The corners are given bottom first */
    start_over();
    gp0(CODE(0x30) | RGB(0, 248, 0));
    gp0(XY(100, 131));
    gp0(RGB(0, 0, 248));
    gp0(XY(100, 100));
    gp0(RGB(0, 0, 248));
    gp0(XY(131, 100));
    for (x = 0; x < 31; x++) {
        CHECK_EQ(pixel_at(100, 100 + x), PIXEL(0, x, 31 - x));
    }
    CHECK_EQ(pixel_at(110, 105), PIXEL(0, 5, 26));
    /* a four-sided one: each half from its own three corners */
    start_over();
    gp0(CODE(0x38));
    gp0(XY(0, 0));
    gp0(RGB(128, 0, 0));
    gp0(XY(8, 0));
    gp0(RGB(0, 128, 0));
    gp0(XY(0, 8));
    gp0(RGB(128, 128, 0));
    gp0(XY(8, 8));
    CHECK_EQ(pixel_at(0, 0), 0);
    CHECK_EQ(pixel_at(4, 0), PIXEL(8, 0, 0));
    CHECK_EQ(pixel_at(0, 4), PIXEL(0, 8, 0));
    CHECK_EQ(pixel_at(7, 7), PIXEL(14, 14, 0));
    CHECK_EQ(pixel_at(4, 4), PIXEL(8, 8, 0));
}

/* The twelve bits of fraction show on a triangle 1023 across whose
 * texture coordinate runs from 0 to 255: a pixel's step is 255/1023,
 * which is 1020.997 of 4096, and the chip takes 1020. At pixel 1001 that
 * is (2048 + 1020 * 1001) / 4096 = 249.77, texel 249, where the true
 * 255 * 1001 / 1023 + 1/2 = 250.02 is texel 250. */
static void test_Precision(void) {
    u16 texels[256];
    s32 mode;
    s32 i;

    for (i = 0; i < 256; i++) {
        texels[i] = (u16)(0x0400 + i);
    }
    for (mode = 0; mode < 2; mode++) {
        start_over();
        port_SetGpuLikeEmulator(mode);
        load(320, 0, 256, 1, texels);
        gp0(CODE(0x25));
        gp0(XY(0, 100));
        gp0(0);
        gp0(XY(1023, 100));
        gp0(((u32)PAGE_15 << 16) | 255);
        gp0(XY(0, 101));
        gp0(0);
        CHECK_EQ(pixel_at(0, 100), 0x0400);
        CHECK_EQ(pixel_at(4, 100), 0x0401);
        /* the chip's way, and with no fraction lost, as the emulator */
        CHECK_EQ(pixel_at(1001, 100), 0x0400 + (mode ? 250 : 249));
        CHECK_EQ(pixel_at(1022, 100), 0x0400 + 255);
        CHECK_EQ(count_drawn(0, 100, 1024, 1), 1023);
    }
    port_SetGpuLikeEmulator(0);
}

/* ---- lines ---- */

static void test_Lines(void) {
    s32 i;

    start_over();
    /* both ends are drawn */
    gp0(CODE(0x40) | WHITE);
    gp0(XY(1, 60));
    gp0(XY(5, 60));
    CHECK_EQ(count_drawn(0, 60, 16, 1), 5);
    CHECK_EQ(pixel_at(1, 60), WHITE_PIXEL);
    CHECK_EQ(pixel_at(5, 60), WHITE_PIXEL);
    /* backwards: the same pixels */
    gp0(CODE(0x40) | RGB(8, 8, 8));
    gp0(XY(5, 60));
    gp0(XY(1, 60));
    CHECK_EQ(pixel_at(1, 60), PIXEL(1, 1, 1));
    CHECK_EQ(pixel_at(5, 60), PIXEL(1, 1, 1));
    CHECK_EQ(pixel_at(0, 60), 0);
    CHECK_EQ(pixel_at(6, 60), 0);
    /* a diagonal */
    gp0(CODE(0x40) | WHITE);
    gp0(XY(1, 62));
    gp0(XY(4, 65));
    for (i = 0; i < 4; i++) {
        CHECK_EQ(pixel_at(1 + i, 62 + i), WHITE_PIXEL);
    }
    CHECK_EQ(count_drawn(0, 62, 16, 4), 4);
    /* one across in four down: the step is taken halfway */
    gp0(CODE(0x40) | WHITE);
    gp0(XY(10, 60));
    gp0(XY(11, 64));
    CHECK_EQ(pixel_at(10, 60), WHITE_PIXEL);
    CHECK_EQ(pixel_at(10, 61), WHITE_PIXEL);
    CHECK_EQ(pixel_at(11, 62), WHITE_PIXEL);
    CHECK_EQ(pixel_at(11, 63), WHITE_PIXEL);
    CHECK_EQ(pixel_at(11, 64), WHITE_PIXEL);
    CHECK_EQ(count_drawn(10, 60, 2, 5), 5);
    /* a point */
    gp0(CODE(0x40) | WHITE);
    gp0(XY(20, 60));
    gp0(XY(20, 60));
    CHECK_EQ(count_drawn(16, 56, 8, 8), 1);
    /* shaded: red from 0 to 128 over eight steps */
    gp0(CODE(0x50));
    gp0(XY(0, 66));
    gp0(RGB(128, 0, 0));
    gp0(XY(8, 66));
    for (i = 0; i <= 8; i++) {
        CHECK_EQ(pixel_at(i, 66), PIXEL(2 * i, 0, 0));
    }
    /* half-transparent, added to what is there */
    gp0(0xE1000000 | (1 << 5));
    gp0(CODE(0x42) | RGB(8, 8, 8));
    gp0(XY(1, 60));
    gp0(XY(2, 60));
    CHECK_EQ(pixel_at(1, 60), PIXEL(2, 2, 2));
    gp0(0xE1000000);

    /* a line of many points ends at its ending word, and the command
     * after it is a command */
    gp0(CODE(0x48) | WHITE);
    gp0(XY(0, 70));
    gp0(XY(3, 70));
    gp0(XY(3, 73));
    gp0(0x55555555);
    rectangle(0x60, WHITE, 6, 70, 1, 1);
    CHECK_EQ(count_drawn(0, 70, 5, 5), 7);
    CHECK_EQ(pixel_at(3, 73), WHITE_PIXEL);
    CHECK_EQ(pixel_at(6, 70), WHITE_PIXEL);
    /* shaded: a colour before each point, the ending word where a
     * colour would be */
    gp0(CODE(0x58));
    gp0(XY(0, 75));
    gp0(RGB(128, 0, 0));
    gp0(XY(8, 75));
    gp0(RGB(0, 0, 0));
    gp0(XY(8, 83));
    gp0(0x50005000);
    rectangle(0x60, WHITE, 12, 75, 1, 1);
    CHECK_EQ(pixel_at(4, 75), PIXEL(8, 0, 0));
    CHECK_EQ(pixel_at(8, 75), PIXEL(16, 0, 0));
    CHECK_EQ(pixel_at(8, 79), PIXEL(8, 0, 0));
    CHECK_EQ(pixel_at(8, 83), 0);
    CHECK_EQ(pixel_at(12, 75), WHITE_PIXEL);
}

/* ---- video memory as a whole ---- */

static void test_Memory(void) {
    static const u16 six[6] = {1, 2, 3, 4, 5, 6};
    s32 i;

    /* the fill: from a multiple of 16, a multiple of 16 wide, whatever
     * the drawing area and the offset */
    start_over();
    set_area(0, 0, 0, 0);
    set_offset(100, 100);
    fill(WHITE, 17, 100, 1, 2);
    CHECK_EQ(count_drawn(0, 96, 64, 8), 32);
    CHECK_EQ(pixel_at(16, 100), WHITE_PIXEL);
    CHECK_EQ(pixel_at(31, 101), WHITE_PIXEL);
    /* past the right edge it goes on at the left */
    fill(WHITE, 1008, 200, 32, 1);
    CHECK_EQ(pixel_at(1023, 200), WHITE_PIXEL);
    CHECK_EQ(pixel_at(15, 200), WHITE_PIXEL);
    CHECK_EQ(pixel_at(16, 200), 0);

    /* in, across, and out again */
    start_over();
    load(100, 200, 3, 2, six);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(pixel_at(100 + i % 3, 200 + i / 3), six[i]);
    }
    CHECK_EQ(count_drawn(96, 196, 16, 16), 6);
    gp0(CODE(0x80));
    gp0(XY(100, 200));
    gp0(XY(200, 300));
    gp0(XY(3, 2));
    for (i = 0; i < 6; i++) {
        CHECK_EQ(pixel_at(200 + i % 3, 300 + i / 3), six[i]);
    }
    gp0(CODE(0xC0));
    gp0(XY(200, 300));
    gp0(XY(3, 2));
    CHECK_EQ(port_ReadGpu(), 0x00020001);
    CHECK_EQ(port_ReadGpu(), 0x00040003);
    CHECK_EQ(port_ReadGpu(), 0x00060005);
    /* an odd number of pixels: the last word is half used */
    gp0(CODE(0xC0));
    gp0(XY(200, 300));
    gp0(XY(3, 1));
    CHECK_EQ(port_ReadGpu(), 0x00020001);
    CHECK_EQ(port_ReadGpu(), 0x00000003);
    /* and the word after a load of an odd number is a command */
    load(300, 300, 1, 1, six);
    rectangle(0x60, WHITE, 310, 300, 1, 1);
    CHECK_EQ(pixel_at(300, 300), 1);
    CHECK_EQ(pixel_at(310, 300), WHITE_PIXEL);
    /* a load past the right edge goes on at the left */
    load(1022, 310, 3, 1, six);
    CHECK_EQ(pixel_at(1023, 310), 2);
    CHECK_EQ(pixel_at(0, 310), 3);

    /* what GP1 can be asked: the area and the offset as they were set */
    set_area(3, 4, 500, 400);
    set_offset(-7, 9);
    port_WriteGp1(0x10000003);
    CHECK_EQ(port_ReadGpu(), (4 << 10) | 3);
    port_WriteGp1(0x10000004);
    CHECK_EQ(port_ReadGpu(), (400 << 10) | 500);
    port_WriteGp1(0x10000005);
    CHECK_EQ(port_ReadGpu(), (9 << 11) | 0x7F9);
    /* a reset puts them back, and leaves video memory */
    port_WriteGp1(0);
    port_WriteGp1(0x10000004);
    CHECK_EQ(port_ReadGpu(), 0);
    CHECK_EQ(pixel_at(300, 300), 1);
}

/* ---- Sony's libgpu on it ---- */

/* An ordering table and its primitives, in one piece of memory that the
 * port is told primitives may be in. */
static u32 g_arena[64];

static void test_Library(void) {
    u32 *ot = g_arena;
    TILE *far_tile = (TILE *)&g_arena[8];
    TILE *near_tile = (TILE *)&g_arena[16];
    TILE *nearer_tile = (TILE *)&g_arena[24];
    DR_OFFSET *offset = (DR_OFFSET *)&g_arena[32];
    DRAWENV draw;
    DISPENV disp;
    PortDisplay display;
    RECT rect;
    u32 pixels[4];
    u32 back[4];
    u16 ofs[2];
    s32 i;

    start_over();
    CHECK_EQ(ResetGraph(0), 0);
    port_AddLinkRegion((u32)g_arena, sizeof(g_arena));

    /* a drawing environment that clears its area, which is a multiple of
     * 64 across: the fast fill. Its primitive is in the environment */
    draw.clip.x = 0, draw.clip.y = 240, draw.clip.w = 640, draw.clip.h = 240;
    draw.ofs[0] = 0, draw.ofs[1] = 240;
    draw.tw.x = 0, draw.tw.y = 0, draw.tw.w = 0, draw.tw.h = 0;
    draw.tpage = 10, draw.dtd = 1, draw.dfe = 0, draw.isbg = 1;
    draw.r0 = 8, draw.g0 = 16, draw.b0 = 24;
    draw.dr_env.tag = 0;
    CHECK(PutDrawEnv(&draw) == &draw);
    CHECK_EQ(draw.dr_env.tag, 0x09FFFFFF);
    CHECK_EQ(draw.dr_env.code[0], 0xE3000000 | (240 << 10));
    CHECK_EQ(draw.dr_env.code[1], 0xE4000000 | (479 << 10) | 639);
    CHECK_EQ(draw.dr_env.code[2], 0xE5000000 | (240 << 11));
    CHECK_EQ(draw.dr_env.code[3], 0xE1000000 | 0x200 | 10);
    CHECK_EQ(draw.dr_env.code[4], 0xE2000000);
    CHECK_EQ(draw.dr_env.code[5], 0xE6000000);
    CHECK_EQ(draw.dr_env.code[6], 0x02000000 | RGB(8, 16, 24));
    CHECK_EQ(draw.dr_env.code[7], XY(0, 240));
    CHECK_EQ(draw.dr_env.code[8], XY(640, 240));
    CHECK_EQ(pixel_at(0, 240), PIXEL(1, 2, 3));
    CHECK_EQ(pixel_at(639, 479), PIXEL(1, 2, 3));
    CHECK_EQ(pixel_at(0, 239), 0);
    CHECK_EQ(pixel_at(640, 240), 0);
    CHECK_EQ(count_drawn(0, 0, 1024, 512), 640 * 240);
    CHECK_EQ(port_GetGpuStatus() & 0x7FF, 0x200 | 10);

    /* an ordering table of four entries, drawn from its last: the far
     * tile first, then the offset that an entry between carries, then
     * the two near ones, the one added later first */
    ClearOTagR(ot, 4);
    setTile(far_tile);
    setRGB0(far_tile, 255, 0, 0);
    setXY0(far_tile, 0, 0);
    setWH(far_tile, 4, 4);
    addPrim(&ot[3], far_tile);
    ofs[0] = 100, ofs[1] = 300;
    SetDrawOffset(offset, ofs);
    addPrim(&ot[2], offset);
    setTile(near_tile);
    setRGB0(near_tile, 0, 255, 0);
    setXY0(near_tile, 2, 2);
    setWH(near_tile, 4, 4);
    addPrim(&ot[1], near_tile);
    setTile(nearer_tile);
    setRGB0(nearer_tile, 0, 0, 255);
    setXY0(nearer_tile, 0, 0);
    setWH(nearer_tile, 4, 4);
    addPrim(&ot[1], nearer_tile);
    DrawOTag(&ot[3]);
    /* the far one at the environment's offset */
    CHECK_EQ(pixel_at(0, 240), PIXEL(31, 0, 0));
    CHECK_EQ(pixel_at(3, 243), PIXEL(31, 0, 0));
    CHECK_EQ(pixel_at(4, 244), PIXEL(1, 2, 3));
    /* the near ones at the table's, green over blue where they meet */
    CHECK_EQ(pixel_at(100, 300), PIXEL(0, 0, 31));
    CHECK_EQ(pixel_at(101, 301), PIXEL(0, 0, 31));
    CHECK_EQ(pixel_at(102, 302), PIXEL(0, 31, 0));
    CHECK_EQ(pixel_at(105, 305), PIXEL(0, 31, 0));
    CHECK_EQ(pixel_at(106, 306), PIXEL(1, 2, 3));
    /* a table of nothing draws nothing and comes back */
    ClearOTagR(ot, 4);
    DrawOTag(&ot[3]);
    CHECK_EQ(pixel_at(106, 306), PIXEL(1, 2, 3));

    /* an environment whose area is not a multiple of 64 across clears
     * it with a rectangle, drawn from its own offset */
    draw.clip.x = 10, draw.clip.y = 0, draw.clip.w = 100, draw.clip.h = 50;
    draw.ofs[0] = 10, draw.ofs[1] = 0;
    draw.r0 = 248, draw.g0 = 0, draw.b0 = 0;
    PutDrawEnv(&draw);
    CHECK_EQ(draw.dr_env.code[6], 0x60000000 | RGB(248, 0, 0));
    CHECK_EQ(draw.dr_env.code[7], XY(0, 0));
    CHECK_EQ(draw.dr_env.code[8], XY(100, 50));
    CHECK_EQ(count_drawn(0, 0, 640, 240), 100 * 50);
    CHECK_EQ(pixel_at(10, 0), PIXEL(31, 0, 0));
    CHECK_EQ(pixel_at(109, 49), PIXEL(31, 0, 0));
    /* and one that does not clear is six words */
    draw.isbg = 0;
    PutDrawEnv(&draw);
    CHECK_EQ(draw.dr_env.tag, 0x06FFFFFF);

    /* ClearImage: the fast fill where it can ... */
    rect.x = 0, rect.y = 0, rect.w = 640, rect.h = 480;
    CHECK_EQ(ClearImage(&rect, 0, 0, 0), 0);
    CHECK_EQ(count_drawn(0, 0, 1024, 512), 0);
    /* ... a drawn rectangle where it cannot, anywhere in video memory,
     * with the area and the offset put back after */
    rect.x = 700, rect.y = 400, rect.w = 10, rect.h = 5;
    ClearImage(&rect, 255, 255, 255);
    CHECK_EQ(count_drawn(0, 0, 1024, 512), 50);
    CHECK_EQ(pixel_at(700, 400), WHITE_PIXEL);
    CHECK_EQ(pixel_at(709, 404), WHITE_PIXEL);
    rectangle(0x60, WHITE, 0, 0, 200, 1);
    CHECK_EQ(count_drawn(0, 0, 640, 240), 100); /* from 10 to 109 */

    /* pixels in, out, and across */
    for (i = 0; i < 4; i++) {
        pixels[i] = 0x11110000 * (u32)(i + 1) + (u32)i + 1;
        back[i] = 0;
    }
    rect.x = 700, rect.y = 10, rect.w = 3, rect.h = 2;
    CHECK_EQ(LoadImage(&rect, pixels), 0);
    CHECK_EQ(pixel_at(700, 10), 0x0001);
    CHECK_EQ(pixel_at(701, 10), 0x1111);
    CHECK_EQ(pixel_at(702, 11), 0x3333);
    CHECK_EQ(StoreImage(&rect, back), 0);
    for (i = 0; i < 3; i++) {
        CHECK_EQ(back[i], pixels[i]);
    }
    CHECK_EQ(back[3], 0); /* six pixels are three words */
    CHECK_EQ(MoveImage(&rect, 710, 20), 0);
    CHECK_EQ(pixel_at(710, 20), 0x0001);
    CHECK_EQ(pixel_at(712, 21), 0x3333);
    rect.w = 0;
    CHECK_EQ(MoveImage(&rect, 720, 30), -1);
    CHECK_EQ(LoadImage(&rect, pixels), -1);

    /* the display: nothing on show until it is asked for */
    port_GetDisplay(&display);
    CHECK_EQ(display.is_on, 0);
    disp.disp.x = 0, disp.disp.y = 240, disp.disp.w = 640, disp.disp.h = 240;
    disp.screen.x = 0, disp.screen.y = 0, disp.screen.w = 0, disp.screen.h = 0;
    disp.isinter = 0, disp.isrgb24 = 0, disp.pad0 = 9, disp.pad1 = 0;
    CHECK(PutDispEnv(&disp) == &disp);
    CHECK_EQ(disp.pad0, 0); /* the video mode: NTSC */
    SetDispMask(1);
    port_GetDisplay(&display);
    CHECK_EQ(display.is_on, 1);
    CHECK_EQ(display.x, 0);
    CHECK_EQ(display.y, 240);
    CHECK_EQ(display.w, 640);
    CHECK_EQ(display.h, 240);
    CHECK_EQ(display.is_rgb24, 0);
    disp.disp.x = 16, disp.disp.y = 0, disp.disp.w = 320;
    PutDispEnv(&disp);
    port_GetDisplay(&display);
    CHECK_EQ(display.x, 16);
    CHECK_EQ(display.y, 0);
    CHECK_EQ(display.w, 320);
    CHECK_EQ(display.h, 240);
    disp.disp.w = 512, disp.disp.h = 480, disp.isinter = 1;
    PutDispEnv(&disp);
    port_GetDisplay(&display);
    CHECK_EQ(display.w, 512);
    CHECK_EQ(display.h, 480);
    SetDispMask(0);
    port_GetDisplay(&display);
    CHECK_EQ(display.is_on, 0);
    CHECK_EQ(DrawSync(0), 0);
}

/* ---- pictures ---- */

/* A PNG's sum over a chunk, a bit at a time. */
static u32 plain_crc(const u8 *bytes, u32 count) {
    u32 crc = 0xFFFFFFFF;
    u32 i;
    s32 bit;

    for (i = 0; i < count; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

static u32 big_endian(const u8 *bytes) {
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
           ((u32)bytes[2] << 8) | bytes[3];
}

/* The colour of the test's picture at (x, y) of video memory. */
static u16 picture_pixel(s32 x, s32 y) {
    return (u16)(((x * 7 + y * 13) & 0x7FFF) | ((x & 1) << 15));
}

/* Reads the PNG in g_written: its three chunks, their sums, the stored
 * blocks of its data and their sum, and every pixel, which is the
 * display's `w` by `h` from (left, top), each line `repeat` times. */
static void check_png(s32 left, s32 top, s32 w, s32 h, s32 repeat) {
    static const u8 signature[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    const u8 *at = g_written;
    const u8 *data;
    u32 length;
    u32 low = 1;
    u32 high = 0;
    u32 row_bytes = 1 + (u32)w * 3;
    u32 seen = 0;
    s32 wrong = 0;
    s32 last_blocks = 0;

    CHECK(test_BytesEqual(at, signature, 8));
    at += 8;
    CHECK_EQ(big_endian(at), 13);
    CHECK(test_BytesEqual(at + 4, "IHDR", 4));
    CHECK_EQ(big_endian(at + 8), w);
    CHECK_EQ(big_endian(at + 12), h * repeat);
    CHECK_EQ(at[16], 8); /* bits a channel */
    CHECK_EQ(at[17], 2); /* red, green, blue */
    CHECK_EQ(at[18] | at[19] | at[20], 0);
    CHECK_EQ(big_endian(at + 21), plain_crc(at + 4, 17));
    at += 25;
    length = big_endian(at);
    CHECK(test_BytesEqual(at + 4, "IDAT", 4));
    CHECK_EQ(big_endian(at + 8 + length), plain_crc(at + 4, 4 + length));
    data = at + 8;
    /* a zlib stream: its two bytes say "deflate" and divide by 31 */
    CHECK_EQ(data[0], 0x78);
    CHECK_EQ(((u32)data[0] * 256 + data[1]) % 31, 0);
    data += 2;
    while (seen < row_bytes * (u32)(h * repeat) && !last_blocks) {
        u32 size = data[1] | ((u32)data[2] << 8);
        u32 i;

        last_blocks += data[0] == 1;
        CHECK(data[0] <= 1); /* stored, and whether it is the last */
        CHECK_EQ(size ^ (data[3] | ((u32)data[4] << 8)), 0xFFFF);
        data += 5;
        for (i = 0; i < size; i++, seen++) {
            u32 column = seen % row_bytes;
            s32 line = (s32)(seen / row_bytes) / repeat;
            u32 want = 0; /* a line begins with "no filter" */

            if (column != 0) {
                u16 pixel =
                    picture_pixel(left + (s32)(column - 1) / 3, top + line);

                want = ((pixel >> ((column - 1) % 3 * 5)) & 31) * 255 / 31;
            }
            wrong += data[i] != want;
            low = (low + data[i]) % 65521;
            high = (high + low) % 65521;
        }
        data += size;
    }
    CHECK_EQ(last_blocks, 1);
    CHECK_EQ(seen, row_bytes * (u32)(h * repeat));
    CHECK_EQ(wrong, 0);
    CHECK_EQ(big_endian(data), (high << 16) | low);
    CHECK(data + 4 == at + 8 + length);
    at += 12 + length;
    CHECK_EQ(big_endian(at), 0);
    CHECK(test_BytesEqual(at + 4, "IEND", 4));
    CHECK_EQ(big_endian(at + 8), plain_crc(at + 4, 4));
    CHECK(at + 12 == g_written + g_written_count);
}

static void test_Pictures(void) {
    s32 wrong = 0;
    s32 x;
    s32 y;
    u32 i;

    start_over();
    for (y = 0; y < 512; y++) {
        for (x = 0; x < 1024; x++) {
            g_port_vram[y * 1024 + x] = picture_pixel(x, y);
        }
    }
    /* 320 by 240 from (16, 32): more than one stored block */
    port_WriteGp1(0x08000001);
    port_WriteGp1(0x05000000 | (32 << 10) | 16);
    port_WriteGp1(0x07000000 | (0x100 << 10) | 0x10);
    g_written_count = 0;
    CHECK(port_WriteDisplayPng(0));
    CHECK(g_written_count > 320 * 240 * 3);
    check_png(16, 32, 320, 240, 1);
    /* 640 by 4: each line twice */
    port_WriteGp1(0x08000003);
    port_WriteGp1(0x07000000 | (0x14 << 10) | 0x10);
    g_written_count = 0;
    CHECK(port_WriteDisplayPng(0));
    check_png(16, 32, 640, 4, 2);

    /* the same rectangle as its pixels, a row after a row */
    g_written_count = 0;
    CHECK(port_WriteDisplayPixels(0));
    CHECK_EQ(g_written_count, 640 * 4 * 2);
    for (i = 0; i < 640 * 4; i++) {
        u16 pixel = (u16)(g_written[i * 2] | (g_written[i * 2 + 1] << 8));

        wrong +=
            pixel != picture_pixel(16 + (s32)(i % 640), 32 + (s32)(i / 640));
    }
    CHECK_EQ(wrong, 0);
    /* and all of video memory */
    g_written_count = 0;
    CHECK(port_WriteVram(0));
    CHECK_EQ(g_written_count, 1024 * 512 * 2);
    CHECK(test_BytesEqual(g_written, g_port_vram, 1024 * 512 * 2));
}

void test_Main(void) {
    test_FillRule();
    test_Limits();
    test_Textures();
    test_Blending();
    test_Mask();
    test_Dither();
    test_Shading();
    test_Precision();
    test_Lines();
    test_Memory();
    test_Library();
    test_Pictures();
}
