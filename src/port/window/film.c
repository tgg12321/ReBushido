/* A replay as a film (film.h; docs/PORT.md, "A replay as a film"): what
 * a film is whichever its kind, the AVI the port writes itself (Motion
 * JPEG and the samples as they are, RIFF's AVI 1.0 with its index), and
 * a film read back for a test. Only the build with a window has this
 * file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "film.h"

#define STBI_NO_STDIO
#include "../vendor/stb_image.h"

/* a picture's JPEG at this quality (film_jpeg.c) */
#define JPEG_QUALITY 90
/* the room a JPEG is given, a picture's pixels in bytes and this */
#define JPEG_ROOM_MORE 4096
/* AVI 1.0's index counts from the movie's list in 32 bits, and readers
 * take a file of 2 GB at the most: the film ends short of it */
#define AVI_MOST_BYTES 2000000000U
/* the headers' bytes: RIFF's, the list of headers', the main header,
 * the two streams' lists and the movie's list's own */
#define AVI_HEADER_BYTES (12U + 12U + 64U + 124U + 102U + 12U)
#define AVI_INDEX_ENTRY 16
#define AVI_KEYFRAME 0x10
#define AVI_HAS_INDEX 0x10
#define AVI_INTERLEAVED 0x100
#define BYTES_PER_PIXEL 4
#define BYTES_PER_SAMPLE_FRAME (PORT_FILM_CHANNELS * 2)
#define MICROSECONDS 1000000ULL
#define HUNDRED_NANOSECONDS 10000000ULL
#define TAP_MAGIC 0x52464252U /* "RBFR", as --tap-window's files */
#define PATH_BYTES 1024
#define WANTED_MOST 10000000U

typedef struct {
    u32 tag;
    u32 offset; /* from the movie's list's own tag */
    u32 size;
} AviEntry;

typedef struct {
    FILE *file;
    s32 w;
    s32 h;
    u32 movi_at; /* where the movie's list's tag is */
    u32 at;      /* where the next chunk goes */
    AviEntry *index;
    u32 entries;
    u32 entries_room;
    u8 *jpeg; /* the last picture's JPEG */
    u32 jpeg_bytes;
    u32 jpeg_room;
    u32 *last;  /* ... and its pixels: a picture the same is not encoded */
    s16 *sound; /* the samples since the last picture */
    u32 sound_frames;
    u32 sound_room;
    u32 biggest;
    s32 full;
} Avi;

struct PortFilm {
    s32 kind;
    s32 w;
    s32 h;
    char path[PATH_BYTES];
    u32 pictures;
    u64 samples;
    Avi *avi;
    PortMf *mf;
};

static u32 port_Tag(const char *four) {
    return (u32)(u8)four[0] | ((u32)(u8)four[1] << 8) |
           ((u32)(u8)four[2] << 16) | ((u32)(u8)four[3] << 24);
}

static void port_Word(u8 *out, u32 *at, u32 value) {
    out[(*at)++] = (u8)value;
    out[(*at)++] = (u8)(value >> 8);
    out[(*at)++] = (u8)(value >> 16);
    out[(*at)++] = (u8)(value >> 24);
}

static void port_Half(u8 *out, u32 *at, u32 value) {
    out[(*at)++] = (u8)value;
    out[(*at)++] = (u8)(value >> 8);
}

static u32 port_ReadWord(const u8 *at) {
    return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) |
           ((u32)at[3] << 24);
}

s32 port_FilmKindOf(const char *path) {
    size_t length = strlen(path);
    const char *end = length >= 4 ? path + length - 4 : path;

    if (length >= 4 && (strcmp(end, ".mp4") == 0 || strcmp(end, ".MP4") == 0)) {
        return PORT_FILM_MP4;
    }
    if (length >= 4 && (strcmp(end, ".avi") == 0 || strcmp(end, ".AVI") == 0)) {
        return PORT_FILM_AVI;
    }
    return PORT_FILM_NONE;
}

s32 port_FilmCanMake(s32 kind) {
    return kind == PORT_FILM_AVI || (kind == PORT_FILM_MP4 && port_MfCan());
}

/* ---- the AVI ---- */

/* The headers, with what is known so far: written as the film opens and
 * again as it closes, the same size. */
static void port_AviHeader(const Avi *avi, u32 pictures, u64 samples) {
    u8 out[AVI_HEADER_BYTES + 8];
    u32 at = 0;
    u32 movi_bytes = avi->at - avi->movi_at;
    u32 index_bytes = avi->entries * AVI_INDEX_ENTRY;
    u32 rate_bytes = PORT_FILM_SAMPLE_RATE * BYTES_PER_SAMPLE_FRAME;
    u32 picture_bytes = (u32)(avi->w * avi->h * 3);

    port_Word(out, &at, port_Tag("RIFF"));
    port_Word(out, &at, avi->movi_at - 8 + movi_bytes + 8 + index_bytes);
    port_Word(out, &at, port_Tag("AVI "));
    port_Word(out, &at, port_Tag("LIST"));
    port_Word(
        out, &at, 4 + 8 + 56 + (12 + 8 + 56 + 8 + 40) + (12 + 8 + 56 + 8 + 18));
    port_Word(out, &at, port_Tag("hdrl"));
    /* the main header */
    port_Word(out, &at, port_Tag("avih"));
    port_Word(out, &at, 56);
    port_Word(out, &at,
              (u32)(MICROSECONDS * PORT_FILM_RATE_DENOMINATOR /
                    PORT_FILM_RATE_NUMERATOR));
    port_Word(out, &at,
              (u32)((u64)avi->biggest * PORT_FILM_RATE_NUMERATOR /
                        PORT_FILM_RATE_DENOMINATOR +
                    rate_bytes));
    port_Word(out, &at, 0);
    port_Word(out, &at, AVI_HAS_INDEX | AVI_INTERLEAVED);
    port_Word(out, &at, pictures);
    port_Word(out, &at, 0);
    port_Word(out, &at, 2);
    port_Word(out, &at, avi->biggest);
    port_Word(out, &at, (u32)avi->w);
    port_Word(out, &at, (u32)avi->h);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    /* the pictures' stream: a frame a blank */
    port_Word(out, &at, port_Tag("LIST"));
    port_Word(out, &at, 4 + 8 + 56 + 8 + 40);
    port_Word(out, &at, port_Tag("strl"));
    port_Word(out, &at, port_Tag("strh"));
    port_Word(out, &at, 56);
    port_Word(out, &at, port_Tag("vids"));
    port_Word(out, &at, port_Tag("MJPG"));
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, PORT_FILM_RATE_DENOMINATOR);
    port_Word(out, &at, PORT_FILM_RATE_NUMERATOR);
    port_Word(out, &at, 0);
    port_Word(out, &at, pictures);
    port_Word(out, &at, avi->biggest);
    port_Word(out, &at, 0xFFFFFFFFU);
    port_Word(out, &at, 0);
    port_Half(out, &at, 0);
    port_Half(out, &at, 0);
    port_Half(out, &at, (u32)avi->w);
    port_Half(out, &at, (u32)avi->h);
    port_Word(out, &at, port_Tag("strf"));
    port_Word(out, &at, 40);
    port_Word(out, &at, 40);
    port_Word(out, &at, (u32)avi->w);
    port_Word(out, &at, (u32)avi->h);
    port_Half(out, &at, 1);
    port_Half(out, &at, 24);
    port_Word(out, &at, port_Tag("MJPG"));
    port_Word(out, &at, picture_bytes);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    /* the sound's stream: 44,100 frames of two samples of 16 bits */
    port_Word(out, &at, port_Tag("LIST"));
    port_Word(out, &at, 4 + 8 + 56 + 8 + 18);
    port_Word(out, &at, port_Tag("strl"));
    port_Word(out, &at, port_Tag("strh"));
    port_Word(out, &at, 56);
    port_Word(out, &at, port_Tag("auds"));
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, BYTES_PER_SAMPLE_FRAME);
    port_Word(out, &at, rate_bytes);
    port_Word(out, &at, 0);
    port_Word(out, &at, (u32)samples);
    port_Word(out, &at, rate_bytes / 10);
    port_Word(out, &at, 0xFFFFFFFFU);
    port_Word(out, &at, BYTES_PER_SAMPLE_FRAME);
    port_Word(out, &at, 0);
    port_Word(out, &at, 0);
    port_Word(out, &at, port_Tag("strf"));
    port_Word(out, &at, 18);
    port_Half(out, &at, 1); /* PCM */
    port_Half(out, &at, PORT_FILM_CHANNELS);
    port_Word(out, &at, PORT_FILM_SAMPLE_RATE);
    port_Word(out, &at, rate_bytes);
    port_Half(out, &at, BYTES_PER_SAMPLE_FRAME);
    port_Half(out, &at, 16);
    port_Half(out, &at, 0);
    /* the movie's list, as long as it is so far */
    port_Word(out, &at, port_Tag("LIST"));
    port_Word(out, &at, movi_bytes - 8);
    port_Word(out, &at, port_Tag("movi"));
    fseek(avi->file, 0, SEEK_SET);
    fwrite(out, 1, at, avi->file);
}

static s32 port_AviChunk(
    Avi *avi, const char *tag, const void *bytes, u32 size) {
    u8 head[8];
    u32 at = 0;
    static const u8 g_pad = 0;

    if (avi->full) {
        return FALSE;
    }
    if ((u64)avi->at + 8 + size + 1 +
            (u64)(avi->entries + 2) * AVI_INDEX_ENTRY >
        AVI_MOST_BYTES) {
        avi->full = TRUE;
        return FALSE;
    }
    if (avi->entries == avi->entries_room) {
        u32 room = avi->entries_room != 0 ? avi->entries_room * 2 : 4096;
        AviEntry *grown = realloc(avi->index, (size_t)room * sizeof(AviEntry));

        if (grown == NULL) {
            return FALSE;
        }
        avi->index = grown;
        avi->entries_room = room;
    }
    port_Word(head, &at, port_Tag(tag));
    port_Word(head, &at, size);
    fseek(avi->file, (long)avi->at, SEEK_SET);
    if (fwrite(head, 1, sizeof(head), avi->file) != sizeof(head) ||
        (size != 0 && fwrite(bytes, 1, size, avi->file) != size) ||
        ((size & 1) != 0 && fwrite(&g_pad, 1, 1, avi->file) != 1)) {
        return FALSE;
    }
    avi->index[avi->entries].tag = port_Tag(tag);
    avi->index[avi->entries].offset = avi->at - (avi->movi_at + 8);
    avi->index[avi->entries].size = size;
    avi->entries++;
    avi->at += 8 + size + (size & 1);
    if (size > avi->biggest) {
        avi->biggest = size;
    }
    return TRUE;
}

static void port_AviFree(Avi *avi) {
    if (avi->file != NULL) {
        fclose(avi->file);
    }
    free(avi->index);
    free(avi->jpeg);
    free(avi->last);
    free(avi->sound);
    free(avi);
}

static Avi *port_AviOpen(const char *path, s32 w, s32 h, char *why, u32 room) {
    Avi *avi = calloc(1, sizeof(Avi));
    size_t pixels = (size_t)w * (size_t)h;

    if (avi == NULL) {
        snprintf(why, room, "no memory for the film");
        return NULL;
    }
    avi->w = w;
    avi->h = h;
    avi->jpeg_room = (u32)(pixels * BYTES_PER_PIXEL + JPEG_ROOM_MORE);
    avi->jpeg = malloc(avi->jpeg_room);
    avi->last = malloc(pixels * BYTES_PER_PIXEL);
    avi->file = fopen(path, "wb");
    if (avi->jpeg == NULL || avi->last == NULL || avi->file == NULL) {
        snprintf(
            why, room,
            avi->file == NULL ? "%s cannot be made" : "no memory for the film",
            path);
        port_AviFree(avi);
        return NULL;
    }
    avi->movi_at = AVI_HEADER_BYTES - 12;
    avi->at = AVI_HEADER_BYTES;
    port_AviHeader(avi, 0, 0);
    return avi;
}

/* the samples kept until the next picture, which they go before */
static s32 port_AviSound(Avi *avi, const s16 *samples, u32 frames) {
    if (avi->sound_frames + frames > avi->sound_room) {
        u32 grown_room = (avi->sound_frames + frames) * 2;
        s16 *grown =
            realloc(avi->sound, (size_t)grown_room * BYTES_PER_SAMPLE_FRAME);

        if (grown == NULL) {
            return FALSE;
        }
        avi->sound = grown;
        avi->sound_room = grown_room;
    }
    memcpy(avi->sound + avi->sound_frames * PORT_FILM_CHANNELS, samples,
           (size_t)frames * BYTES_PER_SAMPLE_FRAME);
    avi->sound_frames += frames;
    return TRUE;
}

static s32 port_AviFlushSound(Avi *avi) {
    s32 ok = TRUE;

    if (avi->sound_frames != 0) {
        ok = port_AviChunk(avi, "01wb", avi->sound,
                           avi->sound_frames * BYTES_PER_SAMPLE_FRAME);
        avi->sound_frames = 0;
    }
    return ok;
}

static s32 port_AviPicture(Avi *avi, const u32 *pixels, u32 index) {
    size_t bytes = (size_t)avi->w * (size_t)avi->h * BYTES_PER_PIXEL;

    if (!port_AviFlushSound(avi)) {
        return FALSE;
    }
    /* a picture the same as the last (the game's frame is two blanks)
     * is its JPEG again */
    if (index == 0 || memcmp(pixels, avi->last, bytes) != 0) {
        avi->jpeg_bytes = port_JpegEncode(
            pixels, avi->w, avi->h, JPEG_QUALITY, avi->jpeg, avi->jpeg_room);
        if (avi->jpeg_bytes == 0) {
            return FALSE;
        }
        memcpy(avi->last, pixels, bytes);
    }
    return port_AviChunk(avi, "00dc", avi->jpeg, avi->jpeg_bytes);
}

static s32 port_AviClose(Avi *avi, u32 pictures, u64 samples) {
    u8 head[8];
    u32 at = 0;
    u32 i;
    s32 ok;

    port_AviFlushSound(avi);
    /* the index after the movie's list */
    port_Word(head, &at, port_Tag("idx1"));
    port_Word(head, &at, avi->entries * AVI_INDEX_ENTRY);
    fseek(avi->file, (long)avi->at, SEEK_SET);
    ok = fwrite(head, 1, sizeof(head), avi->file) == sizeof(head);
    for (i = 0; ok && i < avi->entries; i++) {
        u8 entry[AVI_INDEX_ENTRY];

        at = 0;
        port_Word(entry, &at, avi->index[i].tag);
        port_Word(entry, &at, AVI_KEYFRAME);
        port_Word(entry, &at, avi->index[i].offset);
        port_Word(entry, &at, avi->index[i].size);
        ok = fwrite(entry, 1, sizeof(entry), avi->file) == sizeof(entry);
    }
    port_AviHeader(avi, pictures, samples);
    ok = ok && fflush(avi->file) == 0;
    return ok;
}

/* ---- a film ---- */

PortFilm *port_FilmOpen(
    const char *path, s32 kind, s32 w, s32 h, char *why, u32 room) {
    PortFilm *film;

    if (w <= 0 || h <= 0 || (w & 1) != 0 || (h & 1) != 0) {
        snprintf(why, room,
                 "a film is an even number of pixels each way, "
                 "not %d by %d",
                 w, h);
        return NULL;
    }
    if (!port_FilmCanMake(kind)) {
        snprintf(why, room, "%s",
                 kind == PORT_FILM_MP4 ? "this Windows has no Media "
                                         "Foundation to make an MP4 with "
                                         "(an .avi can be made)"
                                       : "a film is an .mp4 or an .avi");
        return NULL;
    }
    film = calloc(1, sizeof(PortFilm));
    if (film == NULL) {
        snprintf(why, room, "no memory for the film");
        return NULL;
    }
    film->kind = kind;
    film->w = w;
    film->h = h;
    snprintf(film->path, sizeof(film->path), "%s", path);
    if (kind == PORT_FILM_MP4) {
        film->mf = port_MfOpen(path, w, h, why, room);
    } else {
        film->avi = port_AviOpen(path, w, h, why, room);
    }
    if (film->mf == NULL && film->avi == NULL) {
        free(film);
        return NULL;
    }
    return film;
}

s32 port_FilmPicture(PortFilm *film, const u32 *pixels) {
    s32 ok = film->mf != NULL
                 ? port_MfPicture(film->mf, pixels, film->pictures)
                 : port_AviPicture(film->avi, pixels, film->pictures);

    if (ok) {
        film->pictures++;
    }
    return ok;
}

s32 port_FilmSound(PortFilm *film, const s16 *samples, u32 frames) {
    s32 ok = film->mf != NULL
                 ? port_MfSound(film->mf, samples, frames, film->samples)
                 : port_AviSound(film->avi, samples, frames);

    if (ok) {
        film->samples += frames;
    }
    return ok;
}

u32 port_FilmPictures(const PortFilm *film) { return film->pictures; }

u64 port_FilmSamples(const PortFilm *film) { return film->samples; }

s32 port_FilmClose(PortFilm *film, s32 keep, char *why, u32 room) {
    s32 ok;

    if (film->mf != NULL) {
        ok = port_MfClose(film->mf, why, room);
    } else {
        ok = port_AviClose(film->avi, film->pictures, film->samples);
        if (!ok) {
            snprintf(why, room, "%s could not be written whole", film->path);
        }
        port_AviFree(film->avi);
    }
    if (!keep || !ok) {
        remove(film->path);
    }
    free(film);
    return ok;
}

/* ---- a film read back, for a test ---- */

s32 port_FilmWriteRaw(
    const char *folder, u32 index, const u32 *pixels, s32 w, s32 h) {
    char path[PATH_BYTES];
    u32 head[4];
    FILE *file;
    s32 ok;

    snprintf(path, sizeof(path), "%s/frame-%06u.raw", folder, index);
    file = fopen(path, "wb");
    if (file == NULL) {
        return FALSE;
    }
    head[0] = TAP_MAGIC;
    head[1] = (u32)w;
    head[2] = (u32)h;
    head[3] = 1;
    ok = fwrite(head, sizeof(head), 1, file) == 1 &&
         fwrite(pixels, BYTES_PER_PIXEL, (size_t)w * (size_t)h, file) ==
             (size_t)w * (size_t)h;
    fclose(file);
    return ok;
}

s32 port_FilmWriteWav(const char *path, const s16 *samples, u64 frames) {
    u8 head[44];
    u32 at = 0;
    u32 bytes = (u32)(frames * BYTES_PER_SAMPLE_FRAME);
    FILE *file = fopen(path, "wb");
    s32 ok;

    if (file == NULL) {
        return FALSE;
    }
    port_Word(head, &at, port_Tag("RIFF"));
    port_Word(head, &at, 36 + bytes);
    port_Word(head, &at, port_Tag("WAVE"));
    port_Word(head, &at, port_Tag("fmt "));
    port_Word(head, &at, 16);
    port_Half(head, &at, 1);
    port_Half(head, &at, PORT_FILM_CHANNELS);
    port_Word(head, &at, PORT_FILM_SAMPLE_RATE);
    port_Word(head, &at, PORT_FILM_SAMPLE_RATE * BYTES_PER_SAMPLE_FRAME);
    port_Half(head, &at, BYTES_PER_SAMPLE_FRAME);
    port_Half(head, &at, 16);
    port_Word(head, &at, port_Tag("data"));
    port_Word(head, &at, bytes);
    ok = fwrite(head, 1, sizeof(head), file) == sizeof(head) &&
         (bytes == 0 || fwrite(samples, 1, bytes, file) == bytes);
    fclose(file);
    return ok;
}

/* "10,200,1500" as a map of the frames wanted: its length, or 0 */
static u8 *port_FilmWanted(const char *at, u32 *count) {
    const char *word;
    u32 most = 0;
    u8 *wanted;

    for (word = at; *word != 0;) {
        u32 value = (u32)strtoul(word, NULL, 10);

        most = value + 1 > most ? value + 1 : most;
        word = strchr(word, ',');
        if (word == NULL) {
            break;
        }
        word++;
    }
    if (most == 0 || most > WANTED_MOST) {
        return NULL;
    }
    wanted = calloc(most, 1);
    if (wanted == NULL) {
        return NULL;
    }
    for (word = at; *word != 0;) {
        wanted[strtoul(word, NULL, 10)] = 1;
        word = strchr(word, ',');
        if (word == NULL) {
            break;
        }
        word++;
    }
    *count = most;
    return wanted;
}

/* An AVI of this port's read back: each picture's JPEG decoded by
 * stb_image (gl_movie_pack.c compiles it). */
static s32 port_AviRead(const char *path, const char *folder, const u8 *wanted,
                        u32 wanted_count, char *why, u32 room) {
    char list[PATH_BYTES];
    FILE *file = fopen(path, "rb");
    FILE *times;
    u8 *data;
    long size;
    u32 at;
    u32 end;
    u32 picture = 0;
    s16 *sound = NULL;
    u64 sound_frames = 0;
    s32 ok = TRUE;

    if (file == NULL) {
        snprintf(why, room, "%s cannot be read", path);
        return FALSE;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(data);
        snprintf(why, room, "%s cannot be read", path);
        return FALSE;
    }
    fclose(file);
    snprintf(list, sizeof(list), "%s/frames.txt", folder);
    times = fopen(list, "w");
    /* the movie's list, after the headers' */
    for (at = 12; at + 12 <= (u32)size;) {
        u32 tag = port_ReadWord(data + at);
        u32 bytes = port_ReadWord(data + at + 4);

        if (tag == port_Tag("LIST") &&
            port_ReadWord(data + at + 8) == port_Tag("movi")) {
            break;
        }
        at += 8 + bytes + (bytes & 1);
    }
    end = at + 8 + port_ReadWord(data + at + 4);
    for (at += 12; ok && at + 8 <= end && at + 8 <= (u32)size;) {
        u32 tag = port_ReadWord(data + at);
        u32 bytes = port_ReadWord(data + at + 4);

        if (tag == port_Tag("01wb")) {
            s16 *grown = realloc(sound, (size_t)(sound_frames * 4 + bytes));

            if (grown == NULL) {
                ok = FALSE;
                break;
            }
            sound = grown;
            memcpy((u8 *)sound + sound_frames * BYTES_PER_SAMPLE_FRAME,
                   data + at + 8, bytes);
            sound_frames += bytes / BYTES_PER_SAMPLE_FRAME;
        } else if (tag == port_Tag("00dc")) {
            if (times != NULL) {
                fprintf(times, "%u %llu\n", picture,
                        (unsigned long long)((u64)picture *
                                             PORT_FILM_RATE_DENOMINATOR *
                                             HUNDRED_NANOSECONDS /
                                             PORT_FILM_RATE_NUMERATOR));
            }
            if (picture < wanted_count && wanted[picture]) {
                s32 w = 0;
                s32 h = 0;
                s32 n = 0;
                u8 *rgba = stbi_load_from_memory(
                    data + at + 8, (int)bytes, &w, &h, &n, 4);
                u32 *pixels;
                s32 i;

                if (rgba == NULL) {
                    snprintf(why, room,
                             "frame %u is not a JPEG stb_image reads", picture);
                    ok = FALSE;
                    break;
                }
                pixels = (u32 *)(void *)rgba;
                for (i = 0; i < w * h; i++) {
                    const u8 *p = rgba + i * 4;

                    pixels[i] = 0xFF000000U | ((u32)p[0] << 16) |
                                ((u32)p[1] << 8) | p[2];
                }
                ok = port_FilmWriteRaw(folder, picture, pixels, w, h);
                stbi_image_free(rgba);
            }
            picture++;
        }
        at += 8 + bytes + (bytes & 1);
    }
    if (times != NULL) {
        fclose(times);
    }
    snprintf(list, sizeof(list), "%s/sound.wav", folder);
    ok = ok && port_FilmWriteWav(list, sound, sound_frames);
    free(sound);
    free(data);
    if (!ok && why[0] == 0) {
        snprintf(why, room, "%s could not be read back whole", path);
    }
    return ok;
}

s32 port_FilmRead(
    const char *path, const char *folder, const char *at, char *why, u32 room) {
    u32 wanted_count = 0;
    u8 *wanted = port_FilmWanted(at, &wanted_count);
    s32 ok;

    why[0] = 0;
    if (wanted == NULL) {
        snprintf(why, room, "--film-at takes frames, as 10,200,1500");
        return FALSE;
    }
    if (port_FilmKindOf(path) == PORT_FILM_MP4) {
        ok = port_MfRead(path, folder, wanted, wanted_count, why, room);
    } else {
        ok = port_AviRead(path, folder, wanted, wanted_count, why, room);
    }
    free(wanted);
    return ok;
}
