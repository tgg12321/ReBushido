/* A mod's movie: its frames read and decoded (gl_movie.h). Each frame is
 * a JPEG, decoded by stb_image (src/port/vendor/stb_image.h, Sean
 * Barrett's, MIT or public domain), with nothing of it but the baseline
 * JPEG reader compiled. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_movie.h"
#include "port_mods.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "../vendor/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* The most frames a movie may have, and the largest a frame's file may
 * be: the opening has 3,320, each about 45 KB. */
#define PACK_FRAMES_MAX 20000
#define PACK_FRAME_BYTES_MAX (8 * 1024 * 1024)
#define PACK_SIDE_MAX 4096
#define PACK_CHANNELS 4

typedef struct {
    u32 number; /* 0 for none */
    u8 *pixels; /* stb_image's */
    s32 w;
    s32 h;
} PackFrame;

static struct {
    s32 is_tried;
    s32 is_open;
    FILE *file;
    u32 count;
    u32 *places; /* two words a frame: where and how large */
    u8 *bytes;   /* one frame's file */
    PackFrame now;
    PackFrame next;
} g_pack;

static u32 port_PackWord(const u8 *at) {
    return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) |
           ((u32)at[3] << 24);
}

s32 port_GlMoviePackOpen(const char *key) {
    u8 head[PORT_GL_MOVIE_MAGIC_BYTES + PORT_GL_MOVIE_HEAD_WORDS * 4];
    const char *path;
    u8 *index;
    u32 i;

    if (g_pack.is_tried) {
        return g_pack.is_open;
    }
    g_pack.is_tried = TRUE;
    path = port_ModFind(PORT_MOD_MOVIES, key);
    if (path == NULL) {
        return FALSE;
    }
    g_pack.file = fopen(path, "rb");
    if (g_pack.file == NULL || fread(head, sizeof(head), 1, g_pack.file) != 1 ||
        memcmp(head, PORT_GL_MOVIE_MAGIC, PORT_GL_MOVIE_MAGIC_BYTES) != 0) {
        return FALSE;
    }
    g_pack.count = port_PackWord(head + PORT_GL_MOVIE_MAGIC_BYTES + 8);
    if (g_pack.count == 0 || g_pack.count > PACK_FRAMES_MAX) {
        return FALSE;
    }
    index = malloc((size_t)g_pack.count * 8);
    g_pack.places = malloc((size_t)g_pack.count * 2 * sizeof(u32));
    g_pack.bytes = malloc(PACK_FRAME_BYTES_MAX);
    if (index == NULL || g_pack.places == NULL || g_pack.bytes == NULL ||
        fread(index, (size_t)g_pack.count * 8, 1, g_pack.file) != 1) {
        free(index);
        return FALSE;
    }
    for (i = 0; i < g_pack.count * 2; i++) {
        g_pack.places[i] = port_PackWord(index + i * 4);
    }
    free(index);
    g_pack.is_open = TRUE;
    return TRUE;
}

/* Frame `number` into `frame`, what it held before let go. */
static void port_PackDecode(u32 number, PackFrame *frame) {
    u32 place;
    u32 size;
    s32 channels = 0;

    stbi_image_free(frame->pixels);
    frame->pixels = NULL;
    frame->number = number;
    if (number == 0 || number > g_pack.count) {
        return;
    }
    place = g_pack.places[(number - 1) * 2];
    size = g_pack.places[(number - 1) * 2 + 1];
    if (size == 0 || size > PACK_FRAME_BYTES_MAX ||
        fseek(g_pack.file, (long)place, SEEK_SET) != 0 ||
        fread(g_pack.bytes, size, 1, g_pack.file) != 1) {
        return;
    }
    frame->pixels = stbi_load_from_memory(g_pack.bytes, (int)size, &frame->w,
                                          &frame->h, &channels, PACK_CHANNELS);
    if (frame->pixels != NULL &&
        (frame->w <= 0 || frame->h <= 0 || frame->w > PACK_SIDE_MAX ||
         frame->h > PACK_SIDE_MAX)) {
        stbi_image_free(frame->pixels);
        frame->pixels = NULL;
    }
}

const u8 *port_GlMoviePackFrame(u32 number, s32 *w, s32 *h) {
    if (!g_pack.is_open || number == 0 || number > g_pack.count) {
        return NULL;
    }
    if (g_pack.now.number == number) {
        /* the same frame again: the one after it made ready meanwhile */
        if (g_pack.next.number != number + 1 && number < g_pack.count) {
            port_PackDecode(number + 1, &g_pack.next);
        }
    } else if (g_pack.next.number == number) {
        PackFrame one = g_pack.now;

        g_pack.now = g_pack.next;
        g_pack.next = one;
    } else {
        port_PackDecode(number, &g_pack.now);
    }
    *w = g_pack.now.w;
    *h = g_pack.now.h;
    return g_pack.now.pixels;
}
