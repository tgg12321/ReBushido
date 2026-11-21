/* Which frame of the movie each of the player's two display buffers
 * holds, for the window's picture of it (docs/PORT.md, "The movie made
 * better"): nothing of the game's, and nothing the player reads.
 *
 * The player (movovl/src/movie.c) works three frames at once: it pulls
 * frame N out of the ring and expands its bitstream (StGetNext,
 * DecDCTvlc), has the chip decode the frame before it into one of two
 * pictures (DecDCTin, DecDCTout), and meanwhile copies the picture
 * decoded the time before into a display buffer (LoadImage). So the
 * frame on show is followed through the pointers each step is handed:
 * the run-level buffer a frame was expanded into, the picture that
 * buffer was decoded into, the display buffer that picture was copied
 * to. With each comes the frame's quantisation scale, which says how
 * coarse its blocks are.
 *
 * `--dump-movie FOLDER` writes each frame as it is first shown,
 * movie-NNNN.png by its number in the stream: what tools/port_movies.py
 * makes larger. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_movie.h"
#include "bb2.h"

/* The player's two run-level buffers and two pictures. */
#define MOVIE_SLOTS 2
/* A picture of 320 by 240 pixels of three bytes: the most the player
 * decodes. A strip LoadImage copies lies inside it. */
#define MOVIE_PICTURE_BYTES (320 * 240 * 3)
/* the display buffers are at y 0 and y 240 */
#define MOVIE_BUFFER_HEIGHT 240
#define MOVIE_DIGITS 4
#define DECIMAL 10
#define PATH_BYTES 512

typedef struct {
    const u8 *at; /* the buffer, or NULL */
    PortMovieFrame frame;
} PortMovieSlot;

static struct {
    PortMovieFrame pulled; /* the last frame out of the ring */
    PortMovieSlot vlc[MOVIE_SLOTS];
    s32 vlc_next;
    PortMovieFrame decoding; /* the frame the chip was last given */
    s32 decoding_unplaced;   /* DecDCTout has not yet said where */
    PortMovieSlot picture[MOVIE_SLOTS];
    s32 picture_next;
    PortMovieFrame shown[2]; /* by display buffer */
    const char *dump_folder;
    u32 dumped; /* the last frame written */
} g_movie_shown;

/* `frame` kept for the buffer at `at`: in its slot if it has one, or in
 * the next slot round. */
static void port_MovieKeep(
    PortMovieSlot *slots, s32 *next, const void *at, PortMovieFrame frame) {
    s32 i;

    for (i = 0; i < MOVIE_SLOTS; i++) {
        if (slots[i].at == (const u8 *)at) {
            slots[i].frame = frame;
            return;
        }
    }
    slots[*next].at = (const u8 *)at;
    slots[*next].frame = frame;
    *next = (*next + 1) % MOVIE_SLOTS;
}

void port_MovieNotePulled(u32 number) { g_movie_shown.pulled.number = number; }

void port_MovieNoteExpanded(const void *buffer, u32 scale) {
    g_movie_shown.pulled.scale = scale;
    port_MovieKeep(g_movie_shown.vlc, &g_movie_shown.vlc_next, buffer,
                   g_movie_shown.pulled);
}

/* The chip is given the run-level buffer at `buffer`: the frame it was
 * expanded from is the one being decoded. */
void port_MovieNoteDecoding(const void *buffer) {
    s32 i;

    g_movie_shown.decoding.number = 0;
    g_movie_shown.decoding.scale = 0;
    for (i = 0; i < MOVIE_SLOTS; i++) {
        if (g_movie_shown.vlc[i].at == (const u8 *)buffer) {
            g_movie_shown.decoding = g_movie_shown.vlc[i].frame;
        }
    }
    g_movie_shown.decoding_unplaced = TRUE;
}

void port_MovieNoteDecodedInto(const void *picture) {
    if (!g_movie_shown.decoding_unplaced) {
        return; /* the next slice of the same picture */
    }
    g_movie_shown.decoding_unplaced = FALSE;
    port_MovieKeep(g_movie_shown.picture, &g_movie_shown.picture_next, picture,
                   g_movie_shown.decoding);
}

/* A strip of a picture copied into the display buffer at `y`: that
 * buffer now holds the picture's frame. */
void port_MovieNoteCopied(s32 y, const void *pixels) {
    const u8 *at = (const u8 *)pixels;
    s32 i;

    for (i = 0; i < MOVIE_SLOTS; i++) {
        const u8 *start = g_movie_shown.picture[i].at;

        if (start != NULL && at >= start && at < start + MOVIE_PICTURE_BYTES) {
            g_movie_shown.shown[y >= MOVIE_BUFFER_HEIGHT] =
                g_movie_shown.picture[i].frame;
        }
    }
}

PortMovieFrame port_MovieShownFrame(s32 display_y) {
    return g_movie_shown.shown[display_y >= MOVIE_BUFFER_HEIGHT];
}

/* Everything but the dump's folder and the last frame it wrote, which a
 * second showing of the movie does not write again. */
void port_MovieForgetShown(void) {
    u32 dumped = g_movie_shown.dumped;
    const char *folder = g_movie_shown.dump_folder;
    u8 *bytes = (u8 *)&g_movie_shown;
    u32 i;

    for (i = 0; i < sizeof(g_movie_shown); i++) {
        bytes[i] = 0;
    }
    g_movie_shown.dump_folder = folder;
    g_movie_shown.dumped = dumped;
}

void port_MovieSetDumpFolder(const char *folder) {
    g_movie_shown.dump_folder = folder;
}

/* At a blank: the frame on show, if it is one not written yet. */
void port_MovieDumpBlank(void) {
    static const char stem[] = "movie-";
    static const char ending[] = ".png";
    char path[PATH_BYTES];
    PortDisplay display;
    PortMovieFrame frame;
    u32 length = 0;
    u32 scale = 1;
    const char *text;
    s32 file;
    s32 i;

    if (g_movie_shown.dump_folder == NULL || !port_MovieIsPlaying()) {
        return;
    }
    port_GetDisplay(&display);
    if (!display.is_on || !display.is_rgb24) {
        return;
    }
    frame = port_MovieShownFrame(display.y);
    if (frame.number == 0 || frame.number <= g_movie_shown.dumped) {
        return;
    }
    g_movie_shown.dumped = frame.number;
    for (text = g_movie_shown.dump_folder; *text != 0; text++) {
        if (length + sizeof(stem) + MOVIE_DIGITS + sizeof(ending) <
            sizeof(path)) {
            path[length++] = *text;
        }
    }
    for (text = stem; *text != 0; text++) {
        path[length++] = *text;
    }
    for (i = 1; i < MOVIE_DIGITS; i++) {
        scale *= DECIMAL;
    }
    for (i = 0; i < MOVIE_DIGITS; i++) {
        path[length++] = (char)('0' + (frame.number / scale) % DECIMAL);
        scale /= DECIMAL;
    }
    for (text = ending; *text != 0; text++) {
        path[length++] = *text;
    }
    path[length] = 0;
    file = port_HostCreate(path);
    if (file < 0 || !port_WriteDisplayPng(file)) {
        port_Fail("rebushido: the picture %s cannot be written\n", path);
    }
    port_HostClose(file);
}
