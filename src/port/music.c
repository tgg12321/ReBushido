/* The native port's music mods (port_music.h; docs/MODS.md, "Music").
 *
 * The game's music is XA sound that the drive plays (xa.c), and the game
 * follows it only by asking where the drive is. So a mod's piece follows
 * the drive: when the drive starts a stream, the stream is named by the
 * disc's file and its channel, and if a mod has music of that key, its
 * piece plays from its start for as long as the drive plays the stream,
 * through the drive's volumes as the disc's sound goes through them (the
 * game's fades and its mute are on the drive). The disc's sound is still
 * decoded and given to the sound chip, which goes on as before; the mod's
 * piece is put in its place only in what is heard (port_RunSpu). With no
 * mod's music the chip's output is what it was, sample for sample.
 *
 * Where a piece is is worked out from the sound's clock, which is the
 * game's, as samples since the stream began: a step run again after a
 * rollback is not heard (g_port_rerun) and moves nothing here, and the
 * step after it goes on from where the clock is, so a rollback neither
 * restarts the piece nor makes it stutter. Nothing here is the game's
 * state (config/port_state.txt). */
#include "common.h"
#include "port_discfiles.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_music.h"
#include "port_spu.h"
#include "bb2.h"

#define CHIP_RATE PORT_SPU_RATE
#define RATE_MIN 8000
#define RATE_MAX 96000
/* the most samples one call mixes: a blank's are fewer */
#define FRAMES_MAX 2048
/* samples of a piece decoded at a time, and kept from where its loop
 * begins */
#define CHUNK_FRAMES 4096
#define HEAD_FRAMES 4096
/* the Ogg decoder's memory */
#define WORK_BYTES (4 * 1024 * 1024)
/* the largest file of music read */
#define FILE_BYTES_MAX (192 * 1024 * 1024)
#define DUMPS_MAX 64
/* a play of a piece is kept for the dump in pieces of memory: to more
 * than eleven minutes */
#define DUMP_PIECE_FRAMES 0x40000
#define DUMP_PIECES 112
#define FRACTION_SHIFT 15
#define FRACTION_ONE (1 << FRACTION_SHIFT)
#define PATH_SIZE 512
#define TEXT_END '\0'

/* ---- a WAV file ---- */

/* The file: RIFF, its length, WAVE; then chunks, each four letters and
 * a length. */
#define WAV_HEADER_BYTES 44
#define WAV_HEAD_BYTES 12
#define WAV_KIND_AT 8
#define WAV_CHUNK_BYTES 8
#define WAV_CHUNK_LENGTH 4
/* "fmt ": the kind, the channels, the rate, the bytes a frame and the
 * bits a sample, at these places */
#define WAV_FORMAT_BYTES 16
#define WAV_FORMAT_CHANNELS 2
#define WAV_FORMAT_RATE 4
#define WAV_FORMAT_FRAME 12
#define WAV_FORMAT_BITS 14
#define WAV_KIND_PCM 1
#define WAV_KIND_EXTENSIBLE 0xFFFE
#define WAV_EXTENSIBLE_BYTES 40
#define WAV_EXTENSIBLE_KIND 24
/* "smpl": how many loops, then a record of each */
#define WAV_LOOPS_AT 28
#define WAV_LOOP_AT 36
#define WAV_LOOP_BYTES 24
#define WAV_LOOP_START 8
#define WAV_LOOP_END 12
#define WAV_BITS_MAX 32
/* a sample of eight bits is unsigned, its middle this */
#define WAV_8_BIT_MIDDLE 0x80
#define BITS_PER_BYTE 8
#define DECIMAL_BASE 10
#define HUNDREDTHS 100
/* the dump's WAVs: 16 bits, two channels */
#define DUMP_SAMPLE_BITS 16
#define DUMP_FRAME_BYTES 4

typedef struct {
    char riff[4];
    u32 riff_bytes; /* the file's, after this number */
    char wave[4];
    char format[4];
    u32 format_bytes;
    u16 kind;
    u16 channels;
    u32 rate;
    u32 bytes_per_second;
    u16 frame_bytes;
    u16 sample_bits;
    char data[4];
    u32 data_bytes;
} PortMusicWavHeader;

enum { SOURCE_NONE, SOURCE_OGG, SOURCE_WAV };

/* The piece playing: its decoder, and samples of it decoded. */
typedef struct {
    s32 kind;
    PortMusicFormat format;
    void *ogg;
    /* a WAV's samples, in the file read whole */
    const u8 *wav;
    u32 frame_bytes;
    u32 bits;
    /* the next sample the decoder gives */
    u32 decoded_to;
    /* samples kept, a left and a right each: from `first`, `count` */
    s16 *chunk; /* CHUNK_FRAMES of them */
    u32 first;
    u32 count;
    /* the loop's first samples, so that going back needs no seek */
    s16 *head; /* HEAD_FRAMES */
    u32 head_count;
} PortMusicSource;

/* What the dump keeps: the play of the stream so far, in pieces of
 * DUMP_PIECE_FRAMES samples; the pieces of music written, each with how
 * long its file is. Asked of the host only for --dump-music. */
typedef struct {
    s16 *pieces[DUMP_PIECES];
    char keys[DUMPS_MAX][PORT_MOD_KEY_SIZE];
    u32 frames[DUMPS_MAX];
    s32 count;
} PortMusicDumps;

static struct {
    s32 is_off;
    const char *dump_folder;
    /* the stream followed: the sector it began at on the disc, its
     * channel, the sample of the sound's clock it began at, and whether
     * the drive still plays it */
    s32 has_stream;
    u32 sector;
    s32 channel;
    u32 since;
    s32 drive_on;
    char key[PORT_MOD_KEY_SIZE];
    /* a mod's piece stands in for it */
    s32 replacing;
    char source_key[PORT_MOD_KEY_SIZE];
    PortMusicSource source;
    /* the next sample of the chip's output, and where it falls in the
     * piece */
    u32 next_out;
    u32 next_frame;
    u32 next_fraction;
    s16 *out; /* FRAMES_MAX */
    /* memory: the decoder's, and the file */
    u8 *work;
    u8 *file;
    u32 file_room;
    /* the dump, the piece it is writing, and whether its file is this
     * play's */
    s16 *unmixed; /* FRAMES_MAX */
    PortMusicDumps *dumps;
    s32 dump_file;
    s32 dump_slot;
    s32 dump_is_this_play;
} g_port_music = {.dump_file = -1, .dump_slot = -1};

static void port_MusicSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static char port_MusicLower(char letter) {
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

static u32 port_MusicHalf(const u8 *bytes) {
    return bytes[0] | (bytes[1] << 8);
}

static u32 port_MusicWord(const u8 *bytes) {
    return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

/* ---- what is worked out ---- */

void port_MusicKey(const char *name, s32 channel, char *key, u32 room) {
    u32 length = 0;
    u32 end;

    /* the name without its version and its ending */
    while (name[length] != TEXT_END && name[length] != ';') {
        length++;
    }
    end = length;
    while (end > 0 && name[end - 1] != '.') {
        end--;
    }
    if (end > 1) {
        length = end - 1;
    }
    for (end = 0; end < length && end + 1 < room; end++) {
        key[end] = port_MusicLower(name[end]);
    }
    key[end] = TEXT_END;
    if (channel >= 0) {
        port_MusicSay(key + end, room - end, "-%d", channel);
    }
}

/* Whether `tag` begins with `name` and `=`, letters of either case: the
 * value after it. */
static const char *port_MusicTagValue(const char *tag, const char *name) {
    u32 i;

    for (i = 0; name[i] != TEXT_END; i++) {
        if (port_MusicLower(tag[i]) != port_MusicLower(name[i])) {
            return NULL;
        }
    }
    return tag[i] == '=' ? tag + i + 1 : NULL;
}

/* A whole number of decimal digits: 1 and its value, or 0. */
static s32 port_MusicNumber(const char *text, u32 *value) {
    u32 number = 0;
    u32 i;

    for (i = 0; text[i] >= '0' && text[i] <= '9'; i++) {
        number = number * DECIMAL_BASE + (u32)(text[i] - '0');
    }
    if (i == 0 || text[i] != TEXT_END) {
        return 0;
    }
    *value = number;
    return 1;
}

/* A tag of an Ogg file, read for the loop (port_music.h). */
s32 port_MusicReadTag(const char *tag, PortMusicFormat *format) {
    const char *value;
    u32 number;

    if ((value = port_MusicTagValue(tag, "LOOPSTART")) != NULL) {
        if (port_MusicNumber(value, &number)) {
            format->loop_start = number;
            if (!format->loops) {
                format->loop_end = format->frames;
            }
            format->loops = 1;
        }
        return 1;
    }
    if ((value = port_MusicTagValue(tag, "LOOPLENGTH")) != NULL) {
        if (port_MusicNumber(value, &number)) {
            format->loop_end = format->loop_start + number;
            format->loops = 1;
        }
        return 1;
    }
    if ((value = port_MusicTagValue(tag, "LOOPEND")) != NULL) {
        if (port_MusicNumber(value, &number)) {
            format->loop_end = number;
            format->loops = 1;
        }
        return 1;
    }
    return 0;
}

/* Whether the four letters at `bytes` are `tag`. */
static s32 port_MusicIsTag(const u8 *bytes, const char *tag) {
    u32 i;

    for (i = 0; tag[i] != TEXT_END; i++) {
        if (bytes[i] != (u8)tag[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* A WAV file's chunks: its format, its samples and its loop
 * (port_music.h). */
s32 port_MusicReadWav(const u8 *bytes, u32 size, PortMusicFormat *format,
                      u32 *data_at, u32 *frame_bytes, u32 *bits) {
    u32 at = WAV_HEAD_BYTES;
    s32 has_format = 0;
    s32 has_data = 0;
    u32 data_bytes = 0;

    if (size < WAV_HEAD_BYTES || !port_MusicIsTag(bytes, "RIFF") ||
        !port_MusicIsTag(bytes + WAV_KIND_AT, "WAVE")) {
        return 0;
    }
    format->loops = 0;
    while (at + WAV_CHUNK_BYTES <= size) {
        const u8 *chunk = bytes + at;
        u32 length = port_MusicWord(chunk + WAV_CHUNK_LENGTH);
        const u8 *body = chunk + WAV_CHUNK_BYTES;
        u32 left = size - at - WAV_CHUNK_BYTES;

        if (port_MusicIsTag(chunk, "fmt ") && length >= WAV_FORMAT_BYTES &&
            left >= WAV_FORMAT_BYTES) {
            u32 kind = port_MusicHalf(body);

            /* an extensible format says its kind again, further on */
            if (kind == WAV_KIND_EXTENSIBLE && length >= WAV_EXTENSIBLE_BYTES &&
                left >= WAV_EXTENSIBLE_BYTES) {
                kind = port_MusicHalf(body + WAV_EXTENSIBLE_KIND);
            }
            if (kind != WAV_KIND_PCM) {
                return 0;
            }
            format->channels = port_MusicHalf(body + WAV_FORMAT_CHANNELS);
            format->rate = port_MusicWord(body + WAV_FORMAT_RATE);
            *frame_bytes = port_MusicHalf(body + WAV_FORMAT_FRAME);
            *bits = port_MusicHalf(body + WAV_FORMAT_BITS);
            has_format = 1;
        } else if (port_MusicIsTag(chunk, "data")) {
            *data_at = at + WAV_CHUNK_BYTES;
            data_bytes = length < left ? length : left;
            has_data = 1;
        } else if (port_MusicIsTag(chunk, "smpl") &&
                   length >= WAV_LOOP_AT + WAV_LOOP_BYTES &&
                   left >= WAV_LOOP_AT + WAV_LOOP_BYTES &&
                   port_MusicWord(body + WAV_LOOPS_AT) > 0) {
            /* the first loop: its start, and its last sample */
            format->loop_start =
                port_MusicWord(body + WAV_LOOP_AT + WAV_LOOP_START);
            format->loop_end =
                port_MusicWord(body + WAV_LOOP_AT + WAV_LOOP_END) + 1;
            format->loops = 1;
        }
        if (length > size) {
            break;
        }
        /* a chunk of an odd length is followed by a byte of nothing */
        at += WAV_CHUNK_BYTES + length + (length & 1);
    }
    if (!has_format || !has_data || format->channels == 0 ||
        *bits < BITS_PER_BYTE || *bits > WAV_BITS_MAX ||
        *bits % BITS_PER_BYTE != 0 ||
        *frame_bytes != format->channels * (*bits / BITS_PER_BYTE)) {
        return 0;
    }
    format->frames = data_bytes / *frame_bytes;
    return 1;
}

/* Where a sample of the chip's falls in the piece (port_music.h). */
u32 port_MusicSourceAt(u32 frame, u32 rate, u32 *fraction) {
    /* whole seconds apart, so that the product fits in 32 bits */
    u32 seconds = frame / CHIP_RATE;
    u32 rest = (frame % CHIP_RATE) * rate;

    *fraction = rest % CHIP_RATE;
    return seconds * rate + rest / CHIP_RATE;
}

u32 port_MusicLoopFrame(const PortMusicFormat *format, u32 frame) {
    if (format->loops && format->loop_end > format->loop_start &&
        frame >= format->loop_end) {
        return format->loop_start + (frame - format->loop_start) %
                                        (format->loop_end - format->loop_start);
    }
    return frame;
}

/* ---- the piece ---- */

/* Up to `frames` samples from where the decoder is: how many. */
static u32 port_MusicDecode(PortMusicSource *source, s16 *out, u32 frames) {
    u32 got = 0;

    if (source->kind == SOURCE_OGG) {
        got = port_OggRead(source->ogg, out, frames);
    } else if (source->kind == SOURCE_WAV) {
        u32 bytes_per = source->bits / BITS_PER_BYTE;
        u32 i;

        if (source->decoded_to < source->format.frames) {
            got = source->format.frames - source->decoded_to;
        }
        if (got > frames) {
            got = frames;
        }
        for (i = 0; i < got; i++) {
            const u8 *at =
                source->wav + (source->decoded_to + i) * source->frame_bytes;
            u32 side;

            for (side = 0; side < 2; side++) {
                /* the second channel, or the only one again */
                const u8 *sample =
                    at +
                    (side < source->format.channels ? side : 0) * bytes_per;
                s32 value;

                if (source->bits == BITS_PER_BYTE) {
                    value = (sample[0] - WAV_8_BIT_MIDDLE) << BITS_PER_BYTE;
                } else {
                    /* the top 16 bits of the sample */
                    value = (s16)(sample[bytes_per - 2] |
                                  (sample[bytes_per - 1] << BITS_PER_BYTE));
                }
                out[i * 2 + side] = (s16)value;
            }
        }
    }
    source->decoded_to += got;
    return got;
}

static void port_MusicSeek(PortMusicSource *source, u32 frame) {
    if (source->kind == SOURCE_OGG && !port_OggSeek(source->ogg, frame)) {
        /* past the end: the decoder gives nothing more */
        source->decoded_to = source->format.frames;
        return;
    }
    source->decoded_to = frame;
}

/* Sample `frame` of the piece, as its loop takes it, into `pair`. */
static void port_MusicFrame(PortMusicSource *source, u32 frame, s16 *pair) {
    u32 at = port_MusicLoopFrame(&source->format, frame);
    const s16 *from = NULL;

    pair[0] = 0;
    pair[1] = 0;
    if (at >= source->format.frames) {
        return;
    }
    if (source->format.loops && at >= source->format.loop_start &&
        at - source->format.loop_start < source->head_count) {
        from = &source->head[(at - source->format.loop_start) * 2];
    } else {
        if (at < source->first || at - source->first >= source->count) {
            if (source->count > 0 && at == source->first + source->count &&
                source->decoded_to == at) {
                /* the next: the last kept is kept, for the sample before */
                source->chunk[0] = source->chunk[(source->count - 1) * 2];
                source->chunk[1] = source->chunk[(source->count - 1) * 2 + 1];
                source->first = at - 1;
                source->count = 1 + port_MusicDecode(source, source->chunk + 2,
                                                     CHUNK_FRAMES - 1);
            } else {
                if (source->decoded_to != at) {
                    port_MusicSeek(source, at);
                }
                source->first = at;
                source->count =
                    port_MusicDecode(source, source->chunk, CHUNK_FRAMES);
            }
        }
        if (at - source->first < source->count) {
            from = &source->chunk[(at - source->first) * 2];
        }
    }
    if (from != NULL) {
        pair[0] = from[0];
        pair[1] = from[1];
    }
}

static void port_MusicClose(void) {
    PortMusicSource *source = &g_port_music.source;

    if (source->kind == SOURCE_OGG) {
        port_OggClose(source->ogg);
    }
    source->kind = SOURCE_NONE;
    g_port_music.source_key[0] = TEXT_END;
}

/* Room for a file of `size` bytes; the room grows, and is never given
 * back. */
static u8 *port_MusicRoom(u32 size) {
    if (size > g_port_music.file_room) {
        g_port_music.file = port_HostAllocate(size);
        g_port_music.file_room = g_port_music.file != NULL ? size : 0;
    }
    return g_port_music.file;
}

/* The mod's piece of `key`, from `path`: 1 if it can be played. */
static s32 port_MusicOpen(const char *key, const char *path) {
    PortMusicSource *source = &g_port_music.source;
    u32 size = port_ModFileSize(path);
    u8 *bytes;
    u32 i;

    port_MusicClose();
    /* the memory a piece is played from, the first time there is one */
    if (g_port_music.out == NULL) {
        g_port_music.out = port_HostAllocate(FRAMES_MAX * 2 * sizeof(s16));
        source->chunk = port_HostAllocate(CHUNK_FRAMES * 2 * sizeof(s16));
        source->head = port_HostAllocate(HEAD_FRAMES * 2 * sizeof(s16));
    }
    if (g_port_music.out == NULL || source->chunk == NULL ||
        source->head == NULL) {
        return 0;
    }
    if (size == 0 || size > FILE_BYTES_MAX ||
        (bytes = port_MusicRoom(size)) == NULL ||
        port_ModReadFile(path, bytes, size) != size) {
        port_Log("music: %s (%s) cannot be read\n", key, path);
        return 0;
    }
    source->format.loops = 0;
    source->format.loop_start = 0;
    source->format.loop_end = 0;
    source->format.problem = 0;
    if (port_TextEndsWith(path, ".ogg")) {
        if (g_port_music.work == NULL) {
            g_port_music.work = port_HostAllocate(WORK_BYTES);
        }
        source->ogg = g_port_music.work != NULL
                          ? port_OggOpen(bytes, size, g_port_music.work,
                                         WORK_BYTES, &source->format)
                          : NULL;
        if (source->ogg == NULL) {
            port_Log("music: %s (%s) is not Ogg Vorbis that can be read "
                     "(stb_vorbis's error %d)\n",
                     key, path, source->format.problem);
            return 0;
        }
        source->kind = SOURCE_OGG;
        for (i = 0;; i++) {
            const char *tag = port_OggTag(source->ogg, i);

            if (tag == NULL) {
                break;
            }
            port_MusicReadTag(tag, &source->format);
        }
    } else {
        u32 data_at;

        if (!port_MusicReadWav(bytes, size, &source->format, &data_at,
                               &source->frame_bytes, &source->bits)) {
            port_Log("music: %s (%s) is not a WAV of whole numbers of 8, "
                     "16, 24 or 32 bits\n",
                     key, path);
            return 0;
        }
        source->wav = bytes + data_at;
        source->kind = SOURCE_WAV;
    }
    if (source->format.rate < RATE_MIN || source->format.rate > RATE_MAX) {
        port_Log("music: %s (%s) has %u samples a second; from %d to %d "
                 "are played\n",
                 key, path, source->format.rate, RATE_MIN, RATE_MAX);
        port_MusicClose();
        return 0;
    }
    if (source->format.loops &&
        (source->format.loop_end > source->format.frames ||
         source->format.loop_end <= source->format.loop_start)) {
        source->format.loops = 0;
    }
    source->decoded_to = 0;
    source->count = 0;
    source->first = 0;
    source->head_count = 0;
    if (source->format.loops) {
        port_MusicSeek(source, source->format.loop_start);
        source->head_count =
            port_MusicDecode(source, source->head, HEAD_FRAMES);
        port_MusicSeek(source, 0);
    }
    port_MusicSay(
        g_port_music.source_key, sizeof(g_port_music.source_key), "%s", key);
    port_Log(
        "music: %s from %s, %u samples a second, %u.%02u seconds%s\n", key,
        path, source->format.rate, source->format.frames / source->format.rate,
        source->format.frames % source->format.rate * HUNDREDTHS /
            source->format.rate,
        source->format.loops ? ", looped" : "");
    return 1;
}

/* ---- the dump ---- */

/* The WAV's header, for `frames` samples. */
static void port_MusicDumpHeader(s32 file, u32 frames) {
    static const PortMusicWavHeader start = {
        {'R', 'I', 'F', 'F'},
        0,
        {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        WAV_FORMAT_BYTES,
        WAV_KIND_PCM,
        2,
        CHIP_RATE,
        CHIP_RATE * DUMP_FRAME_BYTES,
        DUMP_FRAME_BYTES,
        DUMP_SAMPLE_BITS,
        {'d', 'a', 't', 'a'},
        0,
    };
    PortMusicWavHeader header = start;

    header.data_bytes = frames * DUMP_FRAME_BYTES;
    header.riff_bytes = header.data_bytes + WAV_HEADER_BYTES - WAV_CHUNK_BYTES;
    port_HostWriteAt(file, 0, &header, WAV_HEADER_BYTES);
}

/* A new stream: its play is kept as it goes, and becomes the piece's
 * file once it is longer than any play before it. (Two plays of a piece
 * are not the same to the sample: where the disc's sectors reach the
 * decoder moves with the blanks, so each begins a little apart.) */
static void port_MusicDumpStart(void) {
    PortMusicDumps *dumps = g_port_music.dumps;
    s32 slot;

    if (g_port_music.dump_file >= 0) {
        port_HostClose(g_port_music.dump_file);
        g_port_music.dump_file = -1;
    }
    g_port_music.dump_slot = -1;
    g_port_music.dump_is_this_play = 0;
    if (g_port_music.key[0] == TEXT_END) {
        return;
    }
    for (slot = 0; slot < dumps->count; slot++) {
        if (port_TextsEqual(dumps->keys[slot], g_port_music.key)) {
            break;
        }
    }
    if (slot == DUMPS_MAX) {
        return;
    }
    if (slot == dumps->count) {
        port_MusicSay(
            dumps->keys[slot], PORT_MOD_KEY_SIZE, "%s", g_port_music.key);
        dumps->frames[slot] = 0;
        dumps->count++;
        port_Log("music: %s written to %s/%s.wav as it plays\n",
                 g_port_music.key, g_port_music.dump_folder, g_port_music.key);
    }
    g_port_music.dump_slot = slot;
}

/* Samples `first` to `last` of the play into the open file, at their
 * places. */
static void port_MusicDumpWrite(u32 first, u32 last) {
    while (first < last) {
        u32 piece = first / DUMP_PIECE_FRAMES;
        u32 at = first % DUMP_PIECE_FRAMES;
        u32 count = DUMP_PIECE_FRAMES - at;

        if (count > last - first) {
            count = last - first;
        }
        port_HostWriteAt(
            g_port_music.dump_file, WAV_HEADER_BYTES + first * DUMP_FRAME_BYTES,
            g_port_music.dumps->pieces[piece] + at * 2,
            count * DUMP_FRAME_BYTES);
        first += count;
    }
}

/* The disc's sound of this call, before the drive's volumes, to its
 * place in the play; and the play to the piece's file, if it is the
 * longest yet. */
static void port_MusicDump(u32 frames) {
    char path[PATH_SIZE];
    PortMusicDumps *dumps = g_port_music.dumps;
    s32 slot = g_port_music.dump_slot;
    u32 at;
    u32 i;

    if (slot < 0 || !g_port_music.drive_on || g_port_music.unmixed == NULL) {
        return;
    }
    at = port_GetSampleCount() - g_port_music.since;
    for (i = 0; i < frames; i++) {
        u32 piece = (at + i) / DUMP_PIECE_FRAMES;
        s16 *to;

        if (piece >= DUMP_PIECES) {
            return;
        }
        if (dumps->pieces[piece] == NULL) {
            dumps->pieces[piece] =
                port_HostAllocate(DUMP_PIECE_FRAMES * DUMP_FRAME_BYTES);
            if (dumps->pieces[piece] == NULL) {
                return;
            }
        }
        to = dumps->pieces[piece] + (at + i) % DUMP_PIECE_FRAMES * 2;
        to[0] = g_port_music.unmixed[i * 2];
        to[1] = g_port_music.unmixed[i * 2 + 1];
    }
    if (at + frames <= dumps->frames[slot]) {
        return;
    }
    if (!g_port_music.dump_is_this_play) {
        port_MusicSay(path, sizeof(path), "%s/%s.wav", g_port_music.dump_folder,
                      g_port_music.key);
        g_port_music.dump_file = port_HostCreate(path);
        if (g_port_music.dump_file < 0) {
            port_Log("music: %s cannot be made\n", path);
            g_port_music.dump_slot = -1;
            return;
        }
        g_port_music.dump_is_this_play = 1;
        port_MusicDumpWrite(0, at + frames);
    } else {
        port_MusicDumpWrite(at, at + frames);
    }
    dumps->frames[slot] = at + frames;
    port_MusicDumpHeader(g_port_music.dump_file, at + frames);
}

/* ---- following the drive ---- */

/* What the drive plays now: a new stream is named, and its piece found. */
static void port_MusicFollow(void) {
    u32 sector;
    s32 channel;
    u32 since;
    const PortDiscFile *file;
    const char *path;

    g_port_music.drive_on = port_GetCdStream(&sector, &channel, &since);
    if (!g_port_music.drive_on) {
        return;
    }
    if (g_port_music.has_stream && sector == g_port_music.sector &&
        channel == g_port_music.channel && since == g_port_music.since) {
        return;
    }
    g_port_music.has_stream = 1;
    g_port_music.sector = sector;
    g_port_music.channel = channel;
    g_port_music.since = since;
    g_port_music.next_out = 0;
    g_port_music.next_frame = 0;
    g_port_music.next_fraction = 0;
    file = port_DiscFileAt(sector);
    g_port_music.key[0] = TEXT_END;
    if (file != NULL) {
        port_MusicKey(file->path + file->name_at, channel, g_port_music.key,
                      sizeof(g_port_music.key));
    }
    if (g_port_music.dump_folder != NULL) {
        port_MusicDumpStart();
    }
    path = g_port_music.key[0] != TEXT_END
               ? port_ModFind(PORT_MOD_MUSIC, g_port_music.key)
               : NULL;
    if (path == NULL) {
        g_port_music.replacing = 0;
        return;
    }
    if (port_TextsEqual(g_port_music.source_key, g_port_music.key)) {
        /* the same piece again: from its start, as the stream is */
        g_port_music.replacing = 1;
        return;
    }
    g_port_music.replacing = port_MusicOpen(g_port_music.key, path);
}

/* The piece's next `frames` samples into g_port_music.out, before the
 * drive's volumes: from the sample of it the stream has reached. */
static void port_MusicRender(u32 frames) {
    PortMusicSource *source = &g_port_music.source;
    u32 rate = source->format.rate;
    u32 at = port_GetSampleCount() - g_port_music.since;
    u32 frame;

    for (frame = 0; frame < frames; frame++) {
        s16 now[2];
        s16 next[2];
        s32 between;
        u32 side;

        if (!g_port_music.drive_on || source->kind == SOURCE_NONE) {
            g_port_music.out[frame * 2] = 0;
            g_port_music.out[frame * 2 + 1] = 0;
            continue;
        }
        if (at + frame != g_port_music.next_out) {
            g_port_music.next_frame = port_MusicSourceAt(
                at + frame, rate, &g_port_music.next_fraction);
        }
        port_MusicFrame(source, g_port_music.next_frame, now);
        port_MusicFrame(source, g_port_music.next_frame + 1, next);
        /* the line between the two, the fraction in 32,768ths so that
         * the product fits */
        between = (s32)(g_port_music.next_fraction * FRACTION_ONE / CHIP_RATE);
        for (side = 0; side < 2; side++) {
            s32 value = now[side] + (((next[side] - now[side]) * between) >>
                                     FRACTION_SHIFT);

            g_port_music.out[frame * 2 + side] = (s16)value;
        }
        g_port_music.next_fraction += rate;
        while (g_port_music.next_fraction >= CHIP_RATE) {
            g_port_music.next_fraction -= CHIP_RATE;
            g_port_music.next_frame++;
        }
        g_port_music.next_out = at + frame + 1;
    }
}

/* ---- the run's music (port_music.h) ---- */

void port_MusicSetOn(s32 is_on) { g_port_music.is_off = !is_on; }

void port_MusicSetDumpFolder(const char *folder) {
    g_port_music.dump_folder = folder;
}

/* The mods' music said, and the dump made ready (port_music.h). */
void port_MusicStart(void) {
    if (port_ModsCount(PORT_MOD_MUSIC) > 0) {
        port_Log(
            "music: the mods have %d pieces%s\n",
            port_ModsCount(PORT_MOD_MUSIC),
            g_port_music.is_off ? ", which music_mods = off leaves out" : "");
    }
    if (g_port_music.dump_folder != NULL) {
        port_HostMakeFolder(g_port_music.dump_folder);
        g_port_music.unmixed = port_HostAllocate(FRAMES_MAX * 2 * sizeof(s16));
        g_port_music.dumps = port_HostAllocate(sizeof(PortMusicDumps));
        if (g_port_music.unmixed == NULL || g_port_music.dumps == NULL) {
            g_port_music.dump_folder = NULL;
            return;
        }
        g_port_xa_unmixed = g_port_music.unmixed;
    }
}

/* The mod's music in place of the disc's sound, or NULL
 * (port_music.h). */
const s16 *port_PlayMusic(u32 frames) {
    /* a step run again is not heard, and moves nothing here */
    if (g_port_rerun || frames > FRAMES_MAX) {
        return NULL;
    }
    port_MusicFollow();
    if (g_port_music.dump_folder != NULL) {
        port_MusicDump(frames);
    }
    if (!g_port_music.replacing || g_port_music.is_off) {
        return NULL;
    }
    port_MusicRender(frames);
    port_MixCdAudio(g_port_music.out, frames);
    return g_port_music.out;
}
