/* The native port's sound, kept (docs/PORT.md, "Sound"): with --wav
 * every sample the chip makes is kept in memory, and written as a WAV
 * file when the run ends. There is no sound card in this: a later stage
 * plays the same samples as they are made.
 *
 * They are kept and not written as they come because a WAV file says
 * how long it is at its start, and the host's files are only added to
 * (port_host.h). A run of three minutes is 32 MB. */
#include "common.h"
#include "port_host.h"
#include "port_spu.h"
#include "bb2.h"

/* A megabyte of samples at a time. */
#define CHUNK_FRAMES 0x40000U
#define CHANNELS 2
#define SAMPLE_BYTES 2
#define SAMPLE_BITS 16
#define FRAME_BYTES (CHANNELS * SAMPLE_BYTES)

/* A WAV file's 44 bytes before its samples, for whole numbers of 16
 * bits: every number the low byte first, as a PC has them. */
typedef struct {
    char riff[4];
    u32 riff_bytes; /* the file's, after this number */
    char wave[4];
    char format[4];
    u32 format_bytes;
    u16 kind; /* 1: whole numbers */
    u16 channels;
    u32 rate;
    u32 bytes_per_second;
    u16 frame_bytes;
    u16 sample_bits;
    char data[4];
    u32 data_bytes;
} PortWavHeader;

#define WAV_KIND_PCM 1
#define WAV_FORMAT_BYTES 16
#define WAV_HEADER_BYTES 44
/* The header's first two members, which its count of bytes leaves out. */
#define WAV_UNCOUNTED_BYTES 8

typedef struct PortWavChunk {
    struct PortWavChunk *next;
    u32 frames;
    s16 samples[CHUNK_FRAMES * CHANNELS];
} PortWavChunk;

static const char *g_port_wav_path;
static PortWavChunk *g_port_wav_first;
static PortWavChunk *g_port_wav_last;
static u32 g_port_wav_frames;
static s32 g_port_wav_full;

void port_StartWav(const char *path) { g_port_wav_path = path; }

s32 port_IsWavOn(void) { return g_port_wav_path != NULL; }

/* Keeps `frames` more samples, in a megabyte of memory at a time; when
 * the system has no more, the file ends there. */
void port_AppendWav(const s16 *samples, u32 frames) {
    u32 i;

    if (g_port_wav_path == NULL || g_port_wav_full) {
        return;
    }
    for (i = 0; i < frames; i++) {
        PortWavChunk *chunk = g_port_wav_last;

        if (chunk == NULL || chunk->frames == CHUNK_FRAMES) {
            chunk = port_HostAllocate(sizeof(PortWavChunk));
            if (chunk == NULL) {
                port_Log("rebushido: no memory for more of --wav: the file "
                         "will end here\n");
                g_port_wav_full = 1;
                return;
            }
            chunk->next = NULL;
            chunk->frames = 0;
            if (g_port_wav_last != NULL) {
                g_port_wav_last->next = chunk;
            } else {
                g_port_wav_first = chunk;
            }
            g_port_wav_last = chunk;
        }
        chunk->samples[chunk->frames * CHANNELS] = samples[i * CHANNELS];
        chunk->samples[chunk->frames * CHANNELS + 1] =
            samples[i * CHANNELS + 1];
        chunk->frames++;
        g_port_wav_frames++;
    }
}

/* The file: the header, which says how much follows, and everything
 * kept. */
void port_WriteWav(void) {
    static const PortWavHeader start = {
        {'R', 'I', 'F', 'F'},
        0,
        {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        WAV_FORMAT_BYTES,
        WAV_KIND_PCM,
        CHANNELS,
        PORT_SPU_RATE,
        PORT_SPU_RATE * FRAME_BYTES,
        FRAME_BYTES,
        SAMPLE_BITS,
        {'d', 'a', 't', 'a'},
        0,
    };
    PortWavHeader header = start;
    const PortWavChunk *chunk;
    s32 file;
    s32 written;

    if (g_port_wav_path == NULL) {
        return;
    }
    header.data_bytes = g_port_wav_frames * FRAME_BYTES;
    header.riff_bytes =
        header.data_bytes + WAV_HEADER_BYTES - WAV_UNCOUNTED_BYTES;
    file = port_HostCreate(g_port_wav_path);
    if (file < 0) {
        port_Log("rebushido: %s cannot be made\n", g_port_wav_path);
        return;
    }
    written = port_HostAppend(file, &header, WAV_HEADER_BYTES);
    for (chunk = g_port_wav_first; written && chunk != NULL;
         chunk = chunk->next) {
        written =
            port_HostAppend(file, chunk->samples, chunk->frames * FRAME_BYTES);
    }
    port_HostClose(file);
    if (!written) {
        port_Log("rebushido: %s could not be written whole\n", g_port_wav_path);
    }
    port_Print("sound wav %s frames %u\n", g_port_wav_path, g_port_wav_frames);
}
