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

static struct {
    const char *shots;
    u32 every;
    s32 compares;
    s32 writes_original;
    u32 blanks;
    /* how far the two pictures were apart, over the whole run */
    u32 compared;        /* pictures compared */
    u32 not_shown;       /* ... and blanks where the display was off */
    u32 pixels;          /* pixels compared */
    u32 different;       /* ... that are not the same */
    u32 close;           /* ... and are within one step of a channel */
    u32 worst;           /* the largest difference of a channel, 0 to 255 */
    u64 hardware_time;   /* microseconds */
    u64 software_frames; /* frames the software GPU drew */
} g_run;

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
    "  --gl-exact     draw as the console: the corners where it held\n"
    "                 them, the texture flat, five bits a channel with\n"
    "                 the chip's dither. For the comparison at scale 1\n"
    "  --gl-compare   count the pixels the two pictures differ in\n"
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
    "                 16:9 for a wider view of the same scene\n";

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
    wanted =
        (g_run.shots != NULL || g_run.compares) && blank % g_run.every == 0;
    started = port_Microseconds();
    port_RemasterShow();
    port_GlFinishFrame();
    g_run.hardware_time += port_Microseconds() - started;
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
        } else if (g_run.compares) {
            /* the display is blanked, or in the mode of three bytes a
             * pixel, which the hardware path leaves to the software
             * GPU: there is nothing to hold to anything */
            g_run.not_shown++;
        }
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
            "queue_free %d ot_farthest %u tiles %u nodes %u side_full %u\n",
            limits->frames, limits->prim_bytes, limits->prim_free,
            limits->queue_size, limits->queue_free, limits->ot_farthest,
            limits->tiles, limits->nodes, limits->side_full);

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
    fprintf(stderr,
            "gl run with_depth %u without_depth %u points %u faces %u "
            "attached %u misses %u full %u masked %u texture_from_drawn %u\n",
            counts->with_depth, counts->without_depth, counts->points,
            counts->faces, counts->attached, counts->point_misses, counts->full,
            counts->masked, counts->texture_from_drawn);
    fprintf(stderr, "gl run mixed half %u add %u subtract %u quarter %u\n",
            counts->mixed[0], counts->mixed[1], counts->mixed[2],
            counts->mixed[3]);
    if (g_run.compared != 0) {
        fprintf(stderr,
                "gl run compared %u pictures %u pixels different %u close %u "
                "worst %u\n",
                g_run.compared, g_run.pixels, g_run.different, g_run.close,
                g_run.worst);
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
    g_run.every = 60;
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
        } else if (!strcmp(word, "--filter")) {
            options.is_bilinear = !strcmp(argv[++i], "linear");
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
    if (problem != NULL) {
        fprintf(stderr, "rebushido-gl: %s\n", problem);
        return PORT_EXIT_FAILED;
    }
    g_port_blank_hook = port_GlBlank;
    g_port_quit_hook = port_GlQuit;
    return port_Main(count, (char **)passed);
}
