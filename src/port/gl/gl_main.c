/* The remastered renderer proved with nothing shown (docs/PORT.md,
 * "Remastered"): the port's program with an OpenGL 3.3 core context
 * made through EGL with no display, no window and no surface. It draws
 * every frame twice — the software GPU into its video memory, as the
 * port always does, and the hardware into a colour buffer at `scale`
 * times the resolution — writes either as a PNG, and counts how far
 * apart they are.
 *
 *   rebushido-gl [--gl-scale N] [--graphics original|remastered]
 *                [--filter nearest|linear] [--gl-exact]
 *                [--gl-shots FOLDER] [--gl-every N] [--gl-compare]
 *                [--gl-original] [the port's own options]
 *
 * It is a prover, not the game: the window's GL path is the same
 * src/port/gl/gl_draw.c with SDL_GL_GetProcAddress in place of EGL's.
 * Nothing here is in `make port` or in any check. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "gl.h"
#include "gl_fighter.h"
#include "gl_shadow.h"
#include "gl_stage.h"
#include "gl_tween.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_remaster.h"
#include "shared.h"
#include "window.h"

#define PATH_BYTES_MAX 1024
#define ARGUMENTS_MAX 64
/* The largest picture: video memory's 1024 by 512 at the largest scale
 * would be more than this program needs, so the display's rectangle at
 * the scale is what is kept. */
#define PICTURE_PIXELS_MAX (640 * 8 * 512 * 8 / 4)
/* The surfaceless platform of Mesa's EGL: a context with no surface at
 * all, which is what a framebuffer object needs. */
#define EGL_PLATFORM_SURFACELESS 0x31DD

static u32 g_picture[PICTURE_PIXELS_MAX];
static u32 g_software[PORT_PICTURE_WIDTH_MAX * PORT_PICTURE_HEIGHT_MAX];
/* The pictures of one game frame, for the check that each lies between
 * the two it is drawn from (--gl-tween): the first phase's and the
 * last's are kept, and each in between is held to them. */
static u32 *g_first_phase;
static u32 *g_last_phase;

static struct {
    const char *shots;
    u32 every;
    s32 compares;
    s32 blocks;      /* the picture held to the console's, block for block */
    s32 shadow_mask; /* the shadows' mask in the place of the picture */
    s32 guesses;     /* the pictures between frames paired by likeness alone */
    s32 writes_original;
    u32 blanks;
    /* how far the two pictures were apart, over the whole run */
    u32 compared;  /* pictures compared */
    u32 not_shown; /* ... and blanks where the display was off */
    u32 pixels;    /* pixels compared */
    u32 different; /* ... that are not the same */
    u32 close;     /* ... and are within one step of a channel */
    u32 worst;     /* the largest difference of a channel, 0 to 255 */
    /* the blocks of --gl-blocks */
    u32 block_pictures;
    u32 block_count;     /* pixels of the console's picture looked at */
    u32 block_uneven;    /* ... whose block is not of one colour */
    u32 block_different; /* ... whose block is not the console's colour */
    u32 block_far;       /* ... and is more than one step from it */
    u32 block_worst;
    u32 flat_frames;     /* frames in a row with nothing of the scene in them */
    u64 hardware_time;   /* microseconds */
    u64 software_frames; /* frames the software GPU drew */
    /* pictures between the game's frames (gl_tween.h) */
    s32 tween;              /* pictures a game frame: 1 is off */
    s32 strip_at;           /* the first blank of the strip of pictures */
    s32 strip_blanks;       /* how many blanks of it */
    u32 generation;         /* frames the tween has written down */
    u32 tween_drawn;        /* pictures between frames drawn */
    u32 tween_missed;       /* ... asked for and not drawn (a cut) */
    u64 tween_time;         /* microseconds they took */
    u32 checked;            /* game frames whose phases were checked */
    u32 out_of_order;       /* ... whose pictures do not lie between */
    u32 point_out_of_line;  /* phases whose watched point does not */
    u32 identity;           /* frames drawn again at phase 0 */
    u32 identity_pixels;    /* ... pixels held to the game's own picture */
    u32 identity_different; /* ... that are not the same */
    u32 fewer_prims;        /* phases that drew other than every one */
    u32 more_dropped;       /* ... that left a triangle out the ends did not */
    u32 holes;              /* ... with a hole of the clear in the scene */
    u32 hole_pixels;        /* ... and how many such pixels in all */
    /* the narrow count: pixels that are the colour the game cleared the
     * picture to where neither of the two frames is (the owner's black
     * boxes, port_CountBackgroundHoles) */
    u32 background_pixels;
    u32 still;        /* frames whose scene did not move at all */
    u32 still_moved;  /* ... whose pictures in between are not it */
    u32 off_the_line; /* corners not between the two frames' own */
    /* where and at which blank each fighter's bones are written
     * (--gl-fighter-pose) */
    char pose_folder[512];
    u32 pose_at;
} g_run;

/* The most pictures a game frame may be drawn as, for the prover's
 * check: it keeps the sum of the differences of each from the two
 * ends. */
#define PHASES_MAX 16
/* The share of a picture that may be holes before the picture is
 * counted as one with a hole in it: half a thousandth, which is 817
 * pixels of a picture of 1,704 by 960. Below that are the honest few
 * at the edges of what moved. */
#define HOLE_SHARE 2000

static const char g_usage[] =
    "rebushido-gl: the port's remastered renderer, drawn with no window\n"
    "(docs/PORT.md, \"Remastered\")\n"
    "\n"
    "  rebushido-gl [--gl-scale N] [--graphics original|remastered]\n"
    "               [--filter nearest|linear] [--gl-exact] [--gl-compare]\n"
    "               [--gl-shots FOLDER] [--gl-every N] [--gl-original]\n"
    "               [the options of the build without a window]\n"
    "\n"
    "  --gl-scale N   draw the scene at N times video memory's resolution\n"
    "  --gl-tall N    ... and N times down; twice the scale if nothing\n"
    "                 says, which is what makes a pixel square\n"
    "  --graphics W   original: the software GPU alone; remastered: the\n"
    "                 hardware path as well (the default follows the\n"
    "                 game's own options page)\n"
    "  --filter KIND  nearest: a texel is a block; linear: smoothed\n"
    "  --shadows W    original: the game's own black silhouettes; soft:\n"
    "                 the ground under them darkened instead; or how\n"
    "                 much it darkens, in hundredths; cast (cast:N):\n"
    "                 cast from the fighters' own faces (gl_shadow.h)\n"
    "  --stage-mesh W on, off, every: the stage's ground drawn from one\n"
    "                 static mesh instead of the game's tile primitives\n"
    "                 (gl_stage.h). `on` is the stages the mesh is\n"
    "                 measured good on; `every` is every stage\n"
    "  --stage-depth W on, off: the scene held against the ground's\n"
    "                 own depth, so that ground between the camera and\n"
    "                 a fighter veils him (on with the mesh)\n"
    "  --fighter-models F  the fighters drawn from models of their own,\n"
    "                 the .glb files in folder F (gl_fighter.h); off\n"
    "  --gl-fighter-pose F,N  each fighter's bones at blank N, into\n"
    "                 F/<model>.pose.json (tools/fighter_export.py --pose)\n"
    "  --gl-exact     draw as the console: the corners where it held\n"
    "                 them, the texture flat, five bits a channel with\n"
    "                 the chip's dither. For the comparison at scale 1\n"
    "  --gl-compare   count the pixels the two pictures differ in\n"
    "  --gl-shadow-mask  the shadows' mask in the place of the picture:\n"
    "                 red the silhouettes, green what is not the ground\n"
    "  --gl-blocks    hold the picture to the console's own scaled up by\n"
    "                 whole blocks: what the game draws flat must come\n"
    "                 out as the console's picture at every scale\n"
    "  --gl-guess     pair two frames' primitives by likeness alone, as\n"
    "                 before they were named: for the measurement of\n"
    "                 what naming them buys\n"
    "  --gl-shots F   a PNG of the hardware's picture into this folder\n"
    "  --gl-every N   ... every N vertical blanks (60 if nothing says)\n"
    "  --gl-original  the software GPU's picture beside each of them\n"
    "  --draw-distance W   original: the console's disc of stage cells;\n"
    "                 far: the whole wedge the camera looks down; max:\n"
    "                 every cell of the stage, with the fog pushed out\n"
    "                 to match. A run's setting: it chooses the\n"
    "                 primitive buffers before the game starts\n"
    "  --antialiasing N    the picture drawn N times over each way and\n"
    "                 averaged down: off (or 1), 2, 3, 4\n"
    "  --aspect W:H   the shape of the picture: 4:3 as the console,\n"
    "                 16:9 for a wider view of the same scene\n"
    "  --gl-tween N   draw each game frame as N pictures, the first the\n"
    "                 frame on show and the last the frame after it, and\n"
    "                 check that each of the others lies between them\n"
    "                 (gl_tween.h). Not with --gl-compare\n"
    "  --gl-strip B,N every picture of N blanks from blank B as a PNG\n"
    "                 into --gl-shots, a strip of what a screen shows\n";

/* ---- a PNG of 8-bit pixels ---- */

static u32 g_crc_table[256];

static void port_MakeCrcTable(void) {
    u32 i;
    u32 bit;

    for (i = 0; i < 256; i++) {
        u32 value = i;

        for (bit = 0; bit < 8; bit++) {
            value = (value & 1) ? 0xEDB88320U ^ (value >> 1) : value >> 1;
        }
        g_crc_table[i] = value;
    }
}

static u32 port_Crc(u32 crc, const u8 *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        crc = g_crc_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void port_PutBig(u8 *at, u32 value) {
    at[0] = (u8)(value >> 24);
    at[1] = (u8)(value >> 16);
    at[2] = (u8)(value >> 8);
    at[3] = (u8)value;
}

static void port_PutChunk(
    FILE *file, const char *tag, const u8 *bytes, u32 count) {
    u8 head[4];
    u32 crc;

    port_PutBig(head, count);
    fwrite(head, 1, 4, file);
    fwrite(tag, 1, 4, file);
    if (count != 0) {
        fwrite(bytes, 1, count, file);
    }
    crc = port_Crc(0xFFFFFFFFU, (const u8 *)tag, 4);
    crc = port_Crc(crc, bytes, count);
    port_PutBig(head, ~crc);
    fwrite(head, 1, 4, file);
}

/* The picture as a PNG: one zlib stream of stored blocks, as
 * src/port/picture.c writes the port's own. */
static s32 port_WritePng(
    const char *path, const u32 *pixels, s32 w, s32 h, s32 doubles_lines) {
    static const u8 signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    s32 out_h = doubles_lines ? h * 2 : h;
    u32 row_bytes = (u32)w * 3 + 1;
    u32 data_bytes = row_bytes * (u32)out_h;
    u8 *data = malloc(data_bytes + 16);
    u8 *stream;
    u32 stream_bytes;
    u32 adler_a = 1;
    u32 adler_b = 0;
    u32 at = 0;
    u32 blocks;
    u32 left;
    u32 i;
    u8 head[13];
    FILE *file;

    if (data == NULL) {
        return FALSE;
    }
    for (i = 0; i < (u32)out_h; i++) {
        s32 line = doubles_lines ? (s32)i / 2 : (s32)i;
        s32 x;

        data[at++] = 0;
        for (x = 0; x < w; x++) {
            u32 pixel = pixels[line * w + x];

            data[at++] = (u8)(pixel >> 16);
            data[at++] = (u8)(pixel >> 8);
            data[at++] = (u8)pixel;
        }
    }
    for (i = 0; i < data_bytes; i++) {
        adler_a = (adler_a + data[i]) % 65521;
        adler_b = (adler_b + adler_a) % 65521;
    }
    blocks = (data_bytes + 0xFFFEU) / 0xFFFFU;
    if (blocks == 0) {
        blocks = 1;
    }
    stream_bytes = 2 + blocks * 5 + data_bytes + 4;
    stream = malloc(stream_bytes);
    if (stream == NULL) {
        free(data);
        return FALSE;
    }
    stream[0] = 0x78;
    stream[1] = 0x01;
    at = 2;
    left = data_bytes;
    for (i = 0; i < blocks; i++) {
        u32 piece = left > 0xFFFFU ? 0xFFFFU : left;

        stream[at++] = (u8)(i + 1 == blocks ? 1 : 0);
        stream[at++] = (u8)piece;
        stream[at++] = (u8)(piece >> 8);
        stream[at++] = (u8)~piece;
        stream[at++] = (u8)(~piece >> 8);
        memcpy(stream + at, data + (data_bytes - left), piece);
        at += piece;
        left -= piece;
    }
    port_PutBig(stream + at, (adler_b << 16) | adler_a);
    at += 4;
    file = fopen(path, "wb");
    if (file == NULL) {
        free(data);
        free(stream);
        return FALSE;
    }
    fwrite(signature, 1, sizeof(signature), file);
    port_PutBig(head, (u32)w);
    port_PutBig(head + 4, (u32)out_h);
    head[8] = 8; /* bits a channel */
    head[9] = 2; /* three channels */
    head[10] = 0;
    head[11] = 0;
    head[12] = 0;
    port_PutChunk(file, "IHDR", head, sizeof(head));
    port_PutChunk(file, "IDAT", stream, at);
    port_PutChunk(file, "IEND", NULL, 0);
    fclose(file);
    free(data);
    free(stream);
    return TRUE;
}

/* ---- EGL ---- */

static EGLDisplay g_display;

static void *port_GlGetProc(const char *name) {
    return (void *)eglGetProcAddress(name);
}

static const char *port_StartEgl(void) {
    static const EGLint want[] = {
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_NONE};
    static const EGLint context_want[] = {
        EGL_CONTEXT_MAJOR_VERSION,
        3,
        EGL_CONTEXT_MINOR_VERSION,
        3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE};
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform_display;
    EGLConfig config;
    EGLContext context;
    EGLint configs = 0;
    EGLint major;
    EGLint minor;

    platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
        "eglGetPlatformDisplayEXT");
    if (platform_display != NULL) {
        g_display = platform_display(EGL_PLATFORM_SURFACELESS, NULL, NULL);
    }
    if (g_display == EGL_NO_DISPLAY) {
        g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (g_display == EGL_NO_DISPLAY) {
        return "EGL has no display, with or without a surface";
    }
    if (!eglInitialize(g_display, &major, &minor)) {
        return "EGL does not start";
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        return "EGL has no desktop OpenGL (only GL ES)";
    }
    if (!eglChooseConfig(g_display, want, &config, 1, &configs) ||
        configs == 0) {
        return "EGL has no configuration with three 8-bit channels";
    }
    context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, context_want);
    if (context == EGL_NO_CONTEXT) {
        return "EGL will not make an OpenGL 3.3 core context";
    }
    if (!eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        return "EGL will not make the context current without a surface";
    }
    return NULL;
}

/* ---- a blank ---- */

static u32 port_Microseconds(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)(now.tv_sec * 1000000 + now.tv_nsec / 1000);
}

/* The software GPU's picture, as the port's PNGs and the window have
 * it: 0xFFRRGGBB a pixel. */
static void port_SoftwarePicture(const PortDisplay *display, s32 *w, s32 *h) {
    *w = display->w < PORT_PICTURE_WIDTH_MAX ? display->w
                                             : PORT_PICTURE_WIDTH_MAX;
    *h = display->h < PORT_PICTURE_HEIGHT_MAX
             ? display->h
             : PORT_PICTURE_HEIGHT_MAX;
    if (*w <= 0 || *h <= 0) {
        return;
    }
    port_ConvertDisplay(g_port_vram, display->x, display->y, *w, *h,
                        display->is_rgb24, g_software);
}

/* The two pictures, pixel for pixel: how many differ, and by how much.
 * Returns the largest difference of a channel in this one. */
static u32 port_Compare(s32 w, s32 h) {
    u32 picture_worst = 0;
    s32 i;

    g_run.compared++;
    g_run.pixels += (u32)(w * h);
    for (i = 0; i < w * h; i++) {
        u32 one = g_picture[i] & 0xFFFFFF;
        u32 two = g_software[i] & 0xFFFFFF;
        s32 channel;
        u32 worst = 0;

        if (one == two) {
            continue;
        }
        g_run.different++;
        for (channel = 0; channel < 3; channel++) {
            s32 a = (s32)((one >> (channel * 8)) & 0xFF);
            s32 b = (s32)((two >> (channel * 8)) & 0xFF);
            u32 apart = (u32)(a > b ? a - b : b - a);

            if (apart > worst) {
                worst = apart;
            }
        }
        /* a channel of five bits is eight of a byte: within one step */
        if (worst <= 8) {
            g_run.close++;
        }
        if (worst > g_run.worst) {
            g_run.worst = worst;
        }
        if (worst > picture_worst) {
            picture_worst = worst;
        }
    }
    return picture_worst;
}

/* How far apart two colours of three channels are, the worst channel. */
static u32 port_ChannelsApart(u32 one, u32 two) {
    u32 worst = 0;
    s32 channel;

    for (channel = 0; channel < 3; channel++) {
        s32 a = (s32)((one >> (channel * 8)) & 0xFF);
        s32 b = (s32)((two >> (channel * 8)) & 0xFF);
        u32 apart = (u32)(a > b ? a - b : b - a);

        if (apart > worst) {
            worst = apart;
        }
    }
    return worst;
}

/* The remastered picture against the software GPU's own, scaled up by
 * whole blocks (--gl-blocks): a pixel of video memory is `scale` by
 * `tall` pixels of the picture, so everything the game draws flat --
 * the options page, the menus, the select screens -- must come out as
 * the console's own picture, block for block, at every scale and at
 * both scales unlike. Only a picture with no scene in it is asked
 * (`flat_frames`). What is counted for each pixel of the
 * console's picture: whether its block is of one colour at all, and how
 * far that colour is from the console's. The bands beside the console's
 * 640 pixels in a wider picture are the scene's alone and are left out.
 *
 * What a flat screen may still differ in: a pixel that mixes, which the
 * card mixes at eight bits a channel and the chip at five (within one
 * step), and the edge of a flat polygon that is not up and down, which
 * the card draws at the picture's own resolution and not the console's
 * (sharper, and the pixels of the block are then not all of one
 * colour). */
static void port_CompareBlocks(s32 w, s32 h, s32 software_w, s32 software_h,
                               s32 scale_x, s32 scale_y, s32 margin) {
    s32 x;
    s32 y;

    if (scale_x < 1 || scale_y < 1 || software_w <= 0 || software_h <= 0 ||
        (software_w + 2 * margin) * scale_x != w || software_h * scale_y != h) {
        return;
    }
    g_run.block_pictures++;
    for (y = 0; y < software_h; y++) {
        for (x = 0; x < software_w; x++) {
            u32 want = g_software[y * software_w + x] & 0xFFFFFF;
            u32 first = 0;
            u32 worst = 0;
            s32 uneven = 0;
            s32 i;
            s32 j;

            for (j = 0; j < scale_y; j++) {
                for (i = 0; i < scale_x; i++) {
                    u32 got = g_picture[(y * scale_y + j) * w +
                                        (margin + x) * scale_x + i] &
                              0xFFFFFF;
                    u32 apart = port_ChannelsApart(got, want);

                    if (i == 0 && j == 0) {
                        first = got;
                    } else if (got != first) {
                        uneven = 1;
                    }
                    if (apart > worst) {
                        worst = apart;
                    }
                }
            }
            g_run.block_count++;
            if (uneven) {
                g_run.block_uneven++;
            }
            if (worst != 0) {
                g_run.block_different++;
                /* a channel of five bits is eight of a byte */
                if (worst > 8) {
                    g_run.block_far++;
                }
            }
            if (worst > g_run.block_worst) {
                g_run.block_worst = worst;
            }
        }
    }
}

/* **A hole**: a pixel the frame on show and the frame after it agree
 * about and the picture between them does not. A face that is not
 * drawn where it should be in a picture between two frames leaves what
 * is behind it showing through -- on the practice stage, the dark navy
 * of the backdrop in the middle of a wooden floor -- and that is what
 * the owner saw on a real card: strips and rectangles of it beside the
 * painted lines, in another place every frame.
 *
 * Asking the two ends to agree first is what keeps honest motion out
 * of the count. Where anything moved at all, the two ends differ:
 * the edge of a fighter sweeping over the floor, a shadow, a texture
 * sliding. Only a pixel that is the same in both -- the floor a long
 * way from anything that moved -- is asked to be the same in the
 * middle, and a face missing there is the one thing that can change
 * it. (A pixel the ends agree about by chance, with something that
 * passed over it between, is counted as well; there are few, and the
 * same few are counted before and after a change.) */
#define HOLE_STEADY 8     /* the ends agree within this, per channel */
#define HOLE_DIFFERENT 32 /* ... and the middle differs by this */

/* The widest a colour's three channels differ. */
static s32 port_ColorsApart(u32 one, u32 two) {
    s32 widest = 0;
    s32 shift;

    for (shift = 0; shift <= 16; shift += 8) {
        s32 apart = (s32)((one >> shift) & 0xFF) - (s32)((two >> shift) & 0xFF);

        if (apart < 0) {
            apart = -apart;
        }
        if (apart > widest) {
            widest = apart;
        }
    }
    return widest;
}

static u32 port_CountHoles(s32 pixels) {
    u32 count = 0;
    s32 i;

    for (i = 0; i < pixels; i++) {
        if (port_ColorsApart(g_first_phase[i], g_last_phase[i]) <=
                HOLE_STEADY &&
            port_ColorsApart(g_picture[i], g_first_phase[i]) >=
                HOLE_DIFFERENT) {
            count++;
        }
    }
    return count;
}

/* The owner's own defect, counted and nothing else: a pixel of a picture
 * between two frames that **is the colour the game cleared the picture
 * to** where neither of the two frames it lies between is -- a black
 * box, or a navy one, where both frames have floor. It is the narrow
 * part of the count above, which takes in any pixel that changed where
 * the two ends agree, and so counts honest motion passing over a
 * steady pixel as well (docs/PORT.md, "More pictures a second"). */
static u32 port_CountBackgroundHoles(s32 pixels) {
    u32 background = port_GlBackground();
    u32 count = 0;
    s32 i;

    if (background == 0) {
        return 0;
    }
    for (i = 0; i < pixels; i++) {
        if (port_ColorsApart(g_first_phase[i], g_last_phase[i]) <=
                HOLE_STEADY &&
            port_ColorsApart(g_first_phase[i], background) >= HOLE_DIFFERENT &&
            port_ColorsApart(g_picture[i], background) <= HOLE_STEADY) {
            count++;
        }
    }
    return count;
}

/* ---- the pictures between two frames (gl_tween.h) ---- */

/* How far two pictures are apart: every channel of every pixel added
 * up. It is not a measure of anything in itself; what it is for is the
 * order of the pictures of one game frame, which must run evenly from
 * the frame on show to the frame after it. */
static u64 port_PicturesApart(const u32 *one, const u32 *two, s32 pixels) {
    u64 sum = 0;
    s32 i;

    for (i = 0; i < pixels; i++) {
        s32 channel;

        for (channel = 0; channel < 3; channel++) {
            s32 a = (s32)((one[i] >> (channel * 8)) & 0xFF);
            s32 b = (s32)((two[i] >> (channel * 8)) & 0xFF);

            sum += (u64)(a > b ? a - b : b - a);
        }
    }
    return sum;
}

static s32 port_InStrip(u32 blank) {
    return g_run.strip_at >= 0 && blank >= (u32)g_run.strip_at &&
           blank < (u32)(g_run.strip_at + g_run.strip_blanks);
}

/* A game frame drawn as `--gl-tween` pictures: the first is the frame on
 * show drawn again as it is, which must come out as the game's own
 * picture to the pixel, and the last is the frame after it. Each is
 * measured against those two and the point the renderer watched is
 * held to the line between its ends. */
static void port_GlTweenPhases(u32 blank) {
    char path[PATH_BYTES_MAX];
    u64 from_first[PHASES_MAX];
    u64 from_last[PHASES_MAX];
    s32 point_x[PHASES_MAX];
    s32 point_y[PHASES_MAX];
    u32 prims[PHASES_MAX];
    u32 dropped[PHASES_MAX];
    u32 holes[PHASES_MAX];
    s32 w = 0;
    s32 h = 0;
    s32 live_w = 0;
    s32 live_h = 0;
    s32 writes = g_run.shots != NULL && port_InStrip(blank);
    s32 out_of_order = 0;
    s32 key = 0;
    s32 phase;

    if (g_run.tween < 2 || g_run.tween > PHASES_MAX || !port_TweenCanDraw()) {
        return;
    }
    if (g_first_phase == NULL) {
        g_first_phase = malloc(PICTURE_PIXELS_MAX * sizeof(u32));
        g_last_phase = malloc(PICTURE_PIXELS_MAX * sizeof(u32));
        if (g_first_phase == NULL || g_last_phase == NULL) {
            g_run.tween = 1;
            return;
        }
    }
    /* the game's own picture, which the first phase must come out as */
    if (!port_GlReadPicture(
            g_first_phase, PICTURE_PIXELS_MAX, &live_w, &live_h)) {
        return;
    }
    for (phase = 0; phase < g_run.tween; phase++) {
        s32 at = (s32)(((s64)phase * PORT_TWEEN_ONE) / (g_run.tween - 1));
        u32 started = port_Microseconds();
        s32 point = 0;
        u32 dropped_before = port_GlDroppedNow();

        if (!port_TweenDraw(at)) {
            g_run.tween_missed++;
            return;
        }
        g_run.tween_time += port_Microseconds() - started;
        g_run.tween_drawn++;
        if (!port_GlReadPicture(g_picture, PICTURE_PIXELS_MAX, &w, &h) ||
            w != live_w || h != live_h) {
            return;
        }
        /* **Every picture of the frame holds the same primitives.** The
         * count of them is the frame on show's own, and the triangles
         * the chip's rule leaves out are the same ones: a picture that
         * leaves out one more has a hole where that face was. */
        prims[phase] = port_TweenGetCounts()->prims;
        dropped[phase] = port_GlDroppedNow() - dropped_before;

        point = port_TweenTrack(&key, &point_x[phase], &point_y[phase]);
        if (!point) {
            point_x[phase] = 0;
            point_y[phase] = 0;
        }
        if (phase == 0) {
            /* the frame on show, drawn again as it is: the game's own
             * picture, pixel for pixel */
            u32 different = 0;
            s32 i;

            for (i = 0; i < w * h; i++) {
                if ((g_picture[i] & 0xFFFFFF) !=
                    (g_first_phase[i] & 0xFFFFFF)) {
                    different++;
                }
            }
            g_run.identity++;
            g_run.identity_pixels += (u32)(w * h);
            g_run.identity_different += different;
            memcpy(g_first_phase, g_picture, (size_t)(w * h) * sizeof(u32));
            fprintf(stderr, "gl tween blank %u identity %dx%d different %u\n",
                    blank, w, h, different);
        } else if (phase == g_run.tween - 1) {
            memcpy(g_last_phase, g_picture, (size_t)(w * h) * sizeof(u32));
        } else {
            from_first[phase] =
                port_PicturesApart(g_picture, g_first_phase, w * h);
        }
        if (writes) {
            snprintf(path, sizeof(path), "%s/gl-%06u-p%02d.png", g_run.shots,
                     blank, phase);
            port_WritePng(path, g_picture, w, h, FALSE);
        }
    }
    /* the pictures in between, against the two ends */
    from_first[0] = 0;
    from_last[0] = port_PicturesApart(g_first_phase, g_last_phase, w * h);
    from_first[g_run.tween - 1] = from_last[0];
    from_last[g_run.tween - 1] = 0;
    /* the pictures in between were not kept, so each is drawn once more
     * and measured against the last phase as well */
    for (phase = 1; phase < g_run.tween - 1; phase++) {
        s32 at = (s32)(((s64)phase * PORT_TWEEN_ONE) / (g_run.tween - 1));

        if (!port_TweenDraw(at) ||
            !port_GlReadPicture(g_picture, PICTURE_PIXELS_MAX, &w, &h)) {
            return;
        }
        g_run.tween_drawn++;
        from_last[phase] = port_PicturesApart(g_picture, g_last_phase, w * h);
        /* both ends are known now, so the holes can be counted */
        holes[phase] = port_CountHoles(w * h);
        g_run.background_pixels += port_CountBackgroundHoles(w * h);
    }
    for (phase = 1; phase < g_run.tween; phase++) {
        if (from_first[phase] < from_first[phase - 1] ||
            from_last[phase] > from_last[phase - 1]) {
            out_of_order++;
        }
        /* the watched point, on the line between its two ends */
        if (phase < g_run.tween - 1) {
            s32 one = point_x[0];
            s32 two = point_x[g_run.tween - 1];
            s32 here = point_x[phase];
            s32 low = one < two ? one : two;
            s32 high = one < two ? two : one;

            if (here < low - PORT_REMASTER_ONE ||
                here > high + PORT_REMASTER_ONE) {
                g_run.point_out_of_line++;
            }
        }
    }
    g_run.checked++;
    if (out_of_order != 0) {
        g_run.out_of_order++;
    }
    /* the three hard ones: the same primitives, the same triangles left
     * out, and no more of the clear showing through than at the ends */
    for (phase = 1; phase < g_run.tween - 1; phase++) {
        if (prims[phase] != prims[0]) {
            g_run.fewer_prims++;
        }
        if (dropped[phase] != dropped[0]) {
            g_run.more_dropped++;
        }
        g_run.hole_pixels += holes[phase];
        if (holes[phase] > (u32)(w * h) / HOLE_SHARE) {
            g_run.holes++;
        }
    }
    {
        const PortTweenCounts *counts = port_TweenGetCounts();

        g_run.off_the_line += counts->off_the_line;
        /* A scene that did not move at all between the two frames: every
         * picture in between must be that scene, pixel for pixel. A
         * picture that is not says a partner was wrongly found, or that
         * the two halves of video memory were not taken off. */
        if (counts->widest_step == 0) {
            s32 moved = 0;

            g_run.still++;
            for (phase = 1; phase < g_run.tween; phase++) {
                if (from_first[phase] != 0) {
                    moved = 1;
                }
            }
            if (moved) {
                g_run.still_moved++;
            }
        }
    }
    fprintf(stderr, "gl tween blank %u phases %d out_of_order %d key %d\n",
            blank, g_run.tween, out_of_order, key);
    for (phase = 0; phase < g_run.tween; phase++) {
        fprintf(stderr,
                "gl tween blank %u phase %d from_first %llu from_last %llu "
                "point %d %d\n",
                blank, phase, (unsigned long long)from_first[phase],
                (unsigned long long)from_last[phase],
                point_x[phase] >> PORT_REMASTER_FRACTION,
                point_y[phase] >> PORT_REMASTER_FRACTION);
    }
    {
        const PortTweenCounts *counts = port_TweenGetCounts();

        fprintf(
            stderr,
            "gl tween blank %u prims %u mixed %u unpaired %u flat %u "
            "widest_step %u cuts %u overflowed %u named %u searched %u "
            "clashes %u held %u off_the_line %u weighed %u jumped %u\n",
            blank, counts->prims, counts->mixed, counts->unpaired, counts->flat,
            counts->widest_step, counts->cuts, counts->overflowed,
            counts->named, counts->searched, counts->clashes, counts->held,
            counts->off_the_line, counts->weighed, counts->jumped);
    }
}

static void port_GlBlank(u32 blank) {
    PortDisplay display;
    char path[PATH_BYTES_MAX];
    s32 w = 0;
    s32 h = 0;
    s32 software_w = 0;
    s32 software_h = 0;
    s32 wanted;
    u32 started;

    g_run.blanks = blank;
    port_GetDisplay(&display);
    wanted = (g_run.shots != NULL || g_run.compares || g_run.blocks) &&
             blank % g_run.every == 0;
    started = port_Microseconds();
    port_RemasterShow();
    port_GlFinishFrame();
    g_run.hardware_time += port_Microseconds() - started;
    {
        /* which path the stage's ground takes (gl_stage.h) */
        const char *news = port_GlStageNews();

        if (news != NULL) {
            fprintf(stderr, "gl %s\n", news);
        }
    }
    if (wanted && port_RemasterIsOn()) {
        const PortRemasterCounts *counts = port_GetRemasterFrameCounts();

        if (port_GlReadPicture(g_picture, PICTURE_PIXELS_MAX, &w, &h)) {
            PortGlOptions options;

            port_GlGetOptions(&options);
            if (g_run.shots != NULL) {
                snprintf(
                    path, sizeof(path), "%s/gl-%06u.png", g_run.shots, blank);
                /* a line of the console's picture is two of the
                 * screen's: where the picture was not drawn twice as
                 * tall as it is wide, each line is written twice, as
                 * the port's own PNGs do */
                port_WritePng(
                    path, g_picture, w, h, options.tall <= options.scale);
            }
            fprintf(stderr,
                    "gl blank %u picture %dx%d scale %d vertices %u draws %u "
                    "uploaded %u outside %u wide %u dropped %u\n",
                    blank, w, h, options.scale, port_GlGetVertexCount(),
                    port_GlGetDrawCount(), port_GlGetUploadCount(),
                    port_GlGetOutsideCount(), port_GlGetWideCount(),
                    port_GlGetDroppedCount());
            fprintf(stderr,
                    "gl blank %u with_depth %u without_depth %u points %u "
                    "faces %u attached %u misses %u full %u masked %u "
                    "texture_from_drawn %u\n",
                    blank, counts->with_depth, counts->without_depth,
                    counts->points, counts->faces, counts->attached,
                    counts->point_misses, counts->full, counts->masked,
                    counts->texture_from_drawn);
            fprintf(stderr,
                    "gl blank %u mixed half %u add %u subtract %u quarter "
                    "%u\n",
                    blank, counts->mixed[0], counts->mixed[1], counts->mixed[2],
                    counts->mixed[3]);
            {
                s32 kind;

                for (kind = 0; kind < PORT_REMASTER_COUNT_KINDS; kind++) {
                    if (counts->seen[kind] == 0 && counts->drawn[kind] == 0) {
                        continue;
                    }
                    fprintf(stderr, "gl blank %u kind %s seen %u drawn %u\n",
                            blank, port_NameRemasterKind(kind),
                            counts->seen[kind], counts->drawn[kind]);
                }
            }
            port_SoftwarePicture(&display, &software_w, &software_h);
            if (g_run.writes_original && g_run.shots != NULL &&
                software_w > 0) {
                snprintf(path, sizeof(path), "%s/software-%06u.png",
                         g_run.shots, blank);
                port_WritePng(path, g_software, software_w, software_h,
                              software_h <= PORT_PICTURE_UNIT_H);
            }
            if (g_run.compares && w == software_w && h == software_h && w > 0) {
                u32 before = g_run.different;
                u32 picture_worst = port_Compare(w, h);

                fprintf(stderr,
                        "gl blank %u compared %dx%d different %u worst %u "
                        "dropped %u\n",
                        blank, w, h, g_run.different - before, picture_worst,
                        port_GlGetDroppedCount());
            } else if (g_run.compares) {
                fprintf(
                    stderr,
                    "gl blank %u not compared: hardware %dx%d software %dx%d\n",
                    blank, w, h, software_w, software_h);
            }
            /* The blocks are asked of a picture with no scene in it:
             * everything the game draws flat is the console's own
             * picture scaled up, and a scene is not. The frame being
             * drawn and the one before it must both have been flat,
             * since the picture read back is the one on show. */
            if (counts->with_depth == 0) {
                g_run.flat_frames++;
            } else {
                g_run.flat_frames = 0;
            }
            if (g_run.blocks && software_w > 0 && g_run.flat_frames >= 2) {
                u32 before = g_run.block_different;
                u32 uneven = g_run.block_uneven;
                u32 far = g_run.block_far;

                port_CompareBlocks(
                    w, h, software_w, software_h, options.scale, options.tall,
                    port_RemasterMargin(
                        display.w, options.aspect_w, options.aspect_h));
                fprintf(stderr,
                        "gl blank %u blocks %dx%d of %dx%d uneven %u "
                        "different %u far %u\n",
                        blank, software_w, software_h, options.scale,
                        options.tall, g_run.block_uneven - uneven,
                        g_run.block_different - before, g_run.block_far - far);
            }
        } else if (g_run.compares) {
            /* the display is blanked, or in the mode of three bytes a
             * pixel, which the hardware path leaves to the software
             * GPU: there is nothing to hold to anything */
            g_run.not_shown++;
        }
    }
    /* The pictures between the frame on show and the frame the game has
     * just drawn. It comes last because it leaves one of them in the
     * colour buffer, which the game draws over next frame: so
     * `--gl-tween` and `--gl-compare` are not asked for together. */
    /* each fighter's bones of this frame, for the exporter's bind pose
     * (tools/fighter_export.py --pose) */
    if (g_run.pose_folder[0] != 0 && blank == g_run.pose_at) {
        fprintf(stderr, "gl blank %u fighter poses %d\n", blank,
                port_GlFighterWritePoses(g_run.pose_folder));
    }
    if (g_run.tween > 1 && port_TweenGeneration() != g_run.generation) {
        g_run.generation = port_TweenGeneration();
        port_GlTweenPhases(blank);
    }
    fflush(stderr);
}

static void port_GlQuit(s32 code) {
    const PortRemasterCounts *counts = port_GetRemasterCounts();
    const PortRemasterLimits *limits = port_GetRemasterLimits();
    PortRemasterView view;
    s32 kind;

    (void)code;
    port_RemasterGetView(&view);
    fprintf(stderr, "gl run view distance %s antialiasing %d aspect %d:%d\n",
            port_NameRemasterDistance(view.distance), view.supersample,
            view.aspect_w, view.aspect_h);
    /* every store of a fixed size the drawing path fills, and the least
     * room any frame left in it (docs/PORT.md, "Remastered") */
    fprintf(stderr,
            "gl run limits frames %u prim_bytes %d prim_free %d queue_size %d "
            "queue_free %d ot_farthest %u tiles %u tiles_least %u nodes %u "
            "nodes_least %u side_full %u\n",
            limits->frames, limits->prim_bytes, limits->prim_free,
            limits->queue_size, limits->queue_free, limits->ot_farthest,
            limits->tiles, limits->tiles_least, limits->nodes,
            limits->nodes_least, limits->side_full);

    {
        /* the fighters drawn from models of their own (gl_fighter.h) */
        const PortGlFighterCounts *fighters = port_GlFighterCounts();

        fprintf(stderr,
                "gl run fighters loaded %u failed %u frames %u left_out %u "
                "triangles %u tinted %u%s%s\n",
                fighters->loaded, fighters->failed, fighters->frames,
                fighters->left_out, fighters->triangles, fighters->tinted,
                port_GlFighterProblem() != NULL ? " problem " : "",
                port_GlFighterProblem() != NULL ? port_GlFighterProblem() : "");
    }
    fprintf(stderr, "gl run blanks %u hardware_time_us %llu\n", g_run.blanks,
            (unsigned long long)g_run.hardware_time);
    if (g_run.blanks != 0) {
        fprintf(stderr, "gl run per_frame_us %llu\n",
                (unsigned long long)(g_run.hardware_time / (g_run.blanks + 1)));
    }
    for (kind = 0; kind < PORT_REMASTER_COUNT_KINDS; kind++) {
        if (counts->seen[kind] == 0 && counts->drawn[kind] == 0) {
            continue;
        }
        fprintf(stderr, "gl run kind %s seen %u drawn %u\n",
                port_NameRemasterKind(kind), counts->seen[kind],
                counts->drawn[kind]);
    }
    fprintf(
        stderr,
        "gl run with_depth %u without_depth %u with_place %u points %u "
        "faces %u "
        "attached %u misses %u full %u masked %u texture_from_drawn %u\n",
        counts->with_depth, counts->without_depth, counts->with_place,
        counts->points, counts->faces, counts->attached, counts->point_misses,
        counts->full, counts->masked, counts->texture_from_drawn);
    fprintf(stderr,
            "gl run fighter_faces %u fighter_unplaced %u walked_unplaced %u\n",
            counts->fighter_faces, counts->fighter_unplaced,
            counts->walked_unplaced);
    fprintf(stderr, "gl run mixed half %u add %u subtract %u quarter %u\n",
            counts->mixed[0], counts->mixed[1], counts->mixed[2],
            counts->mixed[3]);
    fprintf(stderr,
            "gl run shadows strength %d faces %u last_faces %d lost %d "
            "widest_area %u occluders %d\n",
            port_GlShadowStrength(), counts->shadows, port_GlShadowFaces(),
            port_GlShadowLost(), port_GlShadowArea(), port_GlShadowOccluders());
    if (port_GlShadowIsCast()) {
        fprintf(stderr,
                "gl run shadows cast faces %d orphans %d triangles %d "
                "fallbacks %u unpaired %u lost %u\n",
                port_GlShadowCastFaces(), port_GlShadowCastOrphans(),
                port_GlShadowCastTriangles(), port_GlShadowCastFallbacks(),
                port_GlShadowCastUnpaired(), port_RemasterCasterLost());
    }
    if (g_run.compared != 0) {
        fprintf(stderr,
                "gl run compared %u pictures %u pixels different %u close %u "
                "worst %u\n",
                g_run.compared, g_run.pixels, g_run.different, g_run.close,
                g_run.worst);
    }
    if (g_run.block_pictures != 0) {
        fprintf(stderr,
                "gl run blocks pictures %u pixels %u uneven %u different %u "
                "far %u worst %u\n",
                g_run.block_pictures, g_run.block_count, g_run.block_uneven,
                g_run.block_different, g_run.block_far, g_run.block_worst);
    }
    if (g_run.tween > 1) {
        const PortTweenCounts *tween = port_TweenGetCounts();

        fprintf(stderr,
                "gl run tween pictures %d frames %u drawn %u missed %u "
                "cuts %u overflowed %u time_us %llu\n",
                g_run.tween, tween->frames, g_run.tween_drawn,
                g_run.tween_missed, tween->cuts, tween->overflowed,
                (unsigned long long)g_run.tween_time);
        if (g_run.tween_drawn != 0) {
            fprintf(stderr, "gl run tween per_picture_us %llu\n",
                    (unsigned long long)(g_run.tween_time / g_run.tween_drawn));
        }
        fprintf(
            stderr,
            "gl run tween checked %u out_of_order %u point_out_of_line %u "
            "identity %u identity_pixels %u identity_different %u\n",
            g_run.checked, g_run.out_of_order, g_run.point_out_of_line,
            g_run.identity, g_run.identity_pixels, g_run.identity_different);
        fprintf(stderr,
                "gl run tween scene %u weighed %u jumped %u named %u "
                "searched %u clashes %u "
                "held %u "
                "off_the_line %u still %u still_moved %u fewer_prims %u "
                "more_dropped %u holes %u hole_pixels %u "
                "background_pixels %u\n",
                tween->scene, tween->weighed, tween->jumped, tween->named,
                tween->searched, tween->clashes, tween->held,
                g_run.off_the_line, g_run.still, g_run.still_moved,
                g_run.fewer_prims, g_run.more_dropped, g_run.holes,
                g_run.hole_pixels, g_run.background_pixels);
    }
    fflush(stderr);
}

/* ---- the program ---- */

int main(int argc, char **argv) {
    PortGlOptions options;
    PortRemasterView view;
    const char *passed[ARGUMENTS_MAX];
    const char *problem;
    s32 count = 0;
    s32 i;

    port_RemasterDefaultView(&view);
    memset(&options, 0, sizeof(options));
    options.scale = 4;
    options.tall = 0; /* twice the scale, for square pixels */
    options.is_bilinear = FALSE;
    options.is_exact = FALSE;
    options.is_stage_mesh = 0;
    /* the scene held against the ground's own depth, which is the mesh's
     * and goes with it (gl.h) */
    options.is_stage_depth = TRUE;
    g_run.every = 60;
    g_run.tween = 1;
    g_run.strip_at = -1;
    g_run.strip_blanks = 0;
    port_MakeCrcTable();
    passed[count++] = argv[0];
    for (i = 1; i < argc && count < ARGUMENTS_MAX; i++) {
        const char *word = argv[i];
        /* A value never begins with a dash: so an option of the build
         * without a window that takes none (`--trace`) is handed on by
         * itself, whatever follows it. */
        s32 has_value = i + 1 < argc && argv[i + 1][0] != '-';

        if (!strcmp(word, "--help") || !strcmp(word, "-h")) {
            printf("%s", g_usage);
            passed[count++] = word;
        } else if (!strcmp(word, "--gl-exact")) {
            options.is_exact = TRUE;
        } else if (!strcmp(word, "--gl-compare")) {
            g_run.compares = TRUE;
        } else if (!strcmp(word, "--gl-shadow-mask")) {
            g_run.shadow_mask = TRUE;
        } else if (!strcmp(word, "--gl-blocks")) {
            g_run.blocks = TRUE;
        } else if (!strcmp(word, "--gl-guess")) {
            g_run.guesses = TRUE;
        } else if (!strcmp(word, "--gl-original")) {
            g_run.writes_original = TRUE;
        } else if (!has_value) {
            passed[count++] = word;
        } else if (!strcmp(word, "--gl-scale")) {
            options.scale = atoi(argv[++i]);
        } else if (!strcmp(word, "--gl-tall")) {
            options.tall = atoi(argv[++i]);
        } else if (!strcmp(word, "--gl-every")) {
            g_run.every = (u32)atoi(argv[++i]);
            if (g_run.every == 0) {
                g_run.every = 1;
            }
        } else if (!strcmp(word, "--gl-shots")) {
            g_run.shots = argv[++i];
        } else if (!strcmp(word, "--gl-tween")) {
            g_run.tween = atoi(argv[++i]);
            if (g_run.tween < 1 || g_run.tween > PHASES_MAX) {
                fprintf(stderr, "rebushido-gl: --gl-tween takes 1 to %d\n",
                        PHASES_MAX);
                return PORT_EXIT_USAGE;
            }
        } else if (!strcmp(word, "--gl-strip")) {
            const char *where = argv[++i];
            const char *comma = strchr(where, ',');

            g_run.strip_at = atoi(where);
            g_run.strip_blanks = comma != NULL ? atoi(comma + 1) : 1;
        } else if (!strcmp(word, "--filter")) {
            options.is_bilinear = !strcmp(argv[++i], "linear");
        } else if (!strcmp(word, "--stage-mesh")) {
            const char *how_ground = argv[++i];

            options.is_stage_mesh = !strcmp(how_ground, "every")
                                        ? 2
                                        : (!strcmp(how_ground, "on") ? 1 : 0);
        } else if (!strcmp(word, "--fighter-models")) {
            const char *where = argv[++i];

            /* the fighters' own models (gl_fighter.h): a folder of
             * them, or off */
            options.is_fighter_models = strcmp(where, "off") != 0;
            /* on: the mods' own models/ folders (port_ModFind) */
            if (options.is_fighter_models && strcmp(where, "on") != 0) {
                port_GlFighterSetFolder(where);
            }
        } else if (!strcmp(word, "--gl-fighter-pose")) {
            const char *where = argv[++i];
            const char *comma = strchr(where, ',');

            g_run.pose_at = comma != NULL ? (u32)atoi(comma + 1) : 600;
            snprintf(g_run.pose_folder, sizeof(g_run.pose_folder), "%.*s",
                     comma != NULL ? (int)(comma - where) : (int)strlen(where),
                     where);
        } else if (!strcmp(word, "--stage-depth")) {
            options.is_stage_depth = strcmp(argv[++i], "off") != 0;
        } else if (!strcmp(word, "--shadows")) {
            options.shadows = port_ReadRemasterShadows(argv[++i]);
            if (options.shadows < 0) {
                fprintf(stderr, "rebushido-gl: --shadows takes original, "
                                "soft, cast, cast:N, or 1 to 100\n");
                return PORT_EXIT_USAGE;
            }
        } else if (!strcmp(word, "--draw-distance")) {
            view.distance = port_ReadRemasterDistance(argv[++i]);
            if (view.distance < 0) {
                fprintf(stderr, "rebushido-gl: --draw-distance takes "
                                "original, far or max\n");
                return PORT_EXIT_USAGE;
            }
        } else if (!strcmp(word, "--antialiasing")) {
            const char *how = argv[++i];

            view.supersample = !strcmp(how, "off") ? 1 : atoi(how);
            if (view.supersample < 1 ||
                view.supersample > PORT_SUPERSAMPLE_MAX) {
                fprintf(stderr,
                        "rebushido-gl: --antialiasing takes off, or "
                        "2 to %d\n",
                        PORT_SUPERSAMPLE_MAX);
                return PORT_EXIT_USAGE;
            }
        } else if (!strcmp(word, "--aspect")) {
            const char *shape = argv[++i];
            const char *colon = strchr(shape, ':');

            if (colon == NULL || atoi(shape) <= 0 || atoi(colon + 1) <= 0) {
                fprintf(stderr,
                        "rebushido-gl: --aspect takes W:H, as 4:3 or 16:9\n");
                return PORT_EXIT_USAGE;
            }
            view.aspect_w = atoi(shape);
            view.aspect_h = atoi(colon + 1);
        } else if (!strcmp(word, "--graphics")) {
            const char *which = argv[++i];

            if (!strcmp(which, "original")) {
                port_SetGraphics(PORT_GRAPHICS_ORIGINAL);
            } else if (!strcmp(which, "remastered")) {
                port_SetGraphics(PORT_GRAPHICS_REMASTERED);
            } else {
                fprintf(stderr, "rebushido-gl: --graphics takes original or "
                                "remastered\n");
                return PORT_EXIT_USAGE;
            }
        } else {
            passed[count++] = word;
            passed[count++] = argv[++i];
        }
    }
    if (options.tall == 0) {
        /* a line of the console's picture is two of the screen's, so
         * twice the scale down the way is what makes a pixel square.
         * The comparison with the software GPU needs them the same. */
        options.tall = options.is_exact ? options.scale : 2 * options.scale;
    }
    port_RemasterSetView(&view);
    port_RemasterGetView(&view);
    options.sample = view.supersample;
    options.aspect_w = view.aspect_w;
    options.aspect_h = view.aspect_h;
    problem = port_StartEgl();
    if (problem != NULL) {
        fprintf(stderr, "rebushido-gl: %s\n", problem);
        return PORT_EXIT_FAILED;
    }
    problem = port_GlStart(port_GlGetProc, &options);
    port_GlShadowShowMask(g_run.shadow_mask);
    if (problem == NULL && options.shadows > 0 && !port_GlShadowIsSoft()) {
        fprintf(stderr, "rebushido-gl: the shadows are the game's own: %s\n",
                port_GlShadowProblem());
    }
    if (problem != NULL) {
        fprintf(stderr, "rebushido-gl: %s\n", problem);
        return PORT_EXIT_FAILED;
    }
    if (g_run.tween > 1) {
        problem = port_TweenStart();
        if (problem != NULL) {
            fprintf(stderr, "rebushido-gl: %s\n", problem);
            return PORT_EXIT_FAILED;
        }
        port_TweenGuessOnly(g_run.guesses);
    }
    g_port_blank_hook = port_GlBlank;
    g_port_quit_hook = port_GlQuit;
    return port_Main(count, (char **)passed);
}
