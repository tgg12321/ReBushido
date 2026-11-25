/* A film as an MP4 (film.h; docs/PORT.md, "A replay as a film"): H.264
 * and AAC by the encoders of Windows' own Media Foundation, which every
 * Windows 10 and 11 has but its N editions without their media pack. Its
 * two libraries are looked up as the first film is made, not linked: a
 * Windows without them still starts the program, and makes an AVI
 * instead (film.c). Nothing of the encoders is in the program, and no
 * licence of theirs is the program's: they are the system's.
 *
 * The pictures are given to the encoder as NV12 (BT.709, 16 to 235),
 * converted here, so that no converter of the system's chooses the
 * colours; the sound as the samples are. A frame's time is its blank's
 * (PORT_FILM_RATE_...), a sample's its own: the two streams keep the
 * game's time between them. On every other system this file is the
 * three answers that there is no such encoder. Only the build with a
 * window has this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "film.h"

#if defined(_WIN32)
#define COBJMACROS
#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

/* the bits a second the pictures are given, for each pixel of a frame:
 * 10.4 million at 1920 by 1080 */
#define BITS_PER_PIXEL 5
/* the sound's: 192 kilobits a second, the most Windows' AAC encoder
 * takes */
#define AAC_BYTES_PER_SECOND 24000
#define H264_PROFILE_HIGH 100
#define HUNDRED_NANOSECONDS 10000000ULL
#define BYTES_PER_PIXEL 4
#define BYTES_PER_SAMPLE_FRAME (PORT_FILM_CHANNELS * 2)
#define PATH_BYTES 1024

typedef HRESULT(WINAPI *StartupFn)(ULONG, DWORD);
typedef HRESULT(WINAPI *ShutdownFn)(void);
typedef HRESULT(WINAPI *MediaTypeFn)(IMFMediaType **);
typedef HRESULT(WINAPI *SampleFn)(IMFSample **);
typedef HRESULT(WINAPI *BufferFn)(DWORD, IMFMediaBuffer **);
typedef HRESULT(WINAPI *AttributesFn)(IMFAttributes **, UINT32);
typedef HRESULT(WINAPI *SinkWriterFn)(
    LPCWSTR, IMFByteStream *, IMFAttributes *, IMFSinkWriter **);
typedef HRESULT(WINAPI *SourceReaderFn)(
    LPCWSTR, IMFAttributes *, IMFSourceReader **);

static struct {
    s32 tried;
    s32 ok;
    StartupFn startup;
    ShutdownFn shutdown;
    MediaTypeFn media_type;
    SampleFn sample;
    BufferFn buffer;
    AttributesFn attributes;
    SinkWriterFn sink_writer;
    SourceReaderFn source_reader;
} g_mf;

struct PortMf {
    IMFSinkWriter *writer;
    DWORD video;
    DWORD audio;
    s32 w;
    s32 h;
    s32 com;
    char path[PATH_BYTES];
};

static void *port_MfLookUp(HMODULE module, const char *name) {
    return module != NULL ? (void *)(void (*)(void))GetProcAddress(module, name)
                          : NULL;
}

static s32 port_MfLoad(void) {
    HMODULE plat;
    HMODULE readwrite;

    if (g_mf.tried) {
        return g_mf.ok;
    }
    g_mf.tried = TRUE;
    plat = LoadLibraryA("mfplat.dll");
    readwrite = LoadLibraryA("mfreadwrite.dll");
    g_mf.startup = (StartupFn)port_MfLookUp(plat, "MFStartup");
    g_mf.shutdown = (ShutdownFn)port_MfLookUp(plat, "MFShutdown");
    g_mf.media_type = (MediaTypeFn)port_MfLookUp(plat, "MFCreateMediaType");
    g_mf.sample = (SampleFn)port_MfLookUp(plat, "MFCreateSample");
    g_mf.buffer = (BufferFn)port_MfLookUp(plat, "MFCreateMemoryBuffer");
    g_mf.attributes = (AttributesFn)port_MfLookUp(plat, "MFCreateAttributes");
    g_mf.sink_writer =
        (SinkWriterFn)port_MfLookUp(readwrite, "MFCreateSinkWriterFromURL");
    g_mf.source_reader =
        (SourceReaderFn)port_MfLookUp(readwrite, "MFCreateSourceReaderFromURL");
    g_mf.ok = g_mf.startup != NULL && g_mf.shutdown != NULL &&
              g_mf.media_type != NULL && g_mf.sample != NULL &&
              g_mf.buffer != NULL && g_mf.attributes != NULL &&
              g_mf.sink_writer != NULL && g_mf.source_reader != NULL;
    return g_mf.ok;
}

s32 port_MfCan(void) { return port_MfLoad(); }

static void port_MfWide(const char *path, wchar_t *wide, u32 room) {
    if (MultiByteToWideChar(CP_ACP, 0, path, -1, wide, (int)room) == 0) {
        wide[0] = 0;
    }
}

static u64 port_Pair(u32 high, u32 low) { return ((u64)high << 32) | low; }

/* ---- making one ---- */

static IMFMediaType *port_MfVideoType(
    const GUID *subtype, s32 w, s32 h, u32 rate_n, u32 rate_d) {
    IMFMediaType *type = NULL;

    if (FAILED(g_mf.media_type(&type))) {
        return NULL;
    }
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype);
    IMFMediaType_SetUINT32(
        type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE, port_Pair((u32)w, (u32)h));
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE, port_Pair(rate_n, rate_d));
    IMFMediaType_SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, port_Pair(1, 1));
    IMFMediaType_SetUINT32(
        type, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    IMFMediaType_SetUINT32(
        type, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    IMFMediaType_SetUINT32(
        type, &MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    IMFMediaType_SetUINT32(
        type, &MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    return type;
}

static IMFMediaType *port_MfAudioType(const GUID *subtype, u32 bytes) {
    IMFMediaType *type = NULL;

    if (FAILED(g_mf.media_type(&type))) {
        return NULL;
    }
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    IMFMediaType_SetUINT32(
        type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, PORT_FILM_SAMPLE_RATE);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, PORT_FILM_CHANNELS);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes);
    return type;
}

/* The writer's two streams, its pictures at the blank's own rate, or
 * where an encoder takes no such rate, at the nearest a thousandth. */
static HRESULT port_MfStreams(PortMf *mf, u32 rate_n, u32 rate_d) {
    IMFMediaType *out;
    IMFMediaType *in;
    HRESULT hr;

    out = port_MfVideoType(&MFVideoFormat_H264, mf->w, mf->h, rate_n, rate_d);
    if (out == NULL) {
        return E_FAIL;
    }
    IMFMediaType_SetUINT32(
        out, &MF_MT_AVG_BITRATE, (UINT32)(mf->w * mf->h * BITS_PER_PIXEL));
    IMFMediaType_SetUINT32(out, &MF_MT_MPEG2_PROFILE, H264_PROFILE_HIGH);
    hr = IMFSinkWriter_AddStream(mf->writer, out, &mf->video);
    IMFMediaType_Release(out);
    if (FAILED(hr)) {
        return hr;
    }
    in = port_MfVideoType(&MFVideoFormat_NV12, mf->w, mf->h, rate_n, rate_d);
    if (in == NULL) {
        return E_FAIL;
    }
    IMFMediaType_SetUINT32(in, &MF_MT_DEFAULT_STRIDE, (UINT32)mf->w);
    hr = IMFSinkWriter_SetInputMediaType(mf->writer, mf->video, in, NULL);
    IMFMediaType_Release(in);
    if (FAILED(hr)) {
        return hr;
    }
    out = port_MfAudioType(&MFAudioFormat_AAC, AAC_BYTES_PER_SECOND);
    if (out == NULL) {
        return E_FAIL;
    }
    hr = IMFSinkWriter_AddStream(mf->writer, out, &mf->audio);
    IMFMediaType_Release(out);
    if (FAILED(hr)) {
        return hr;
    }
    in = port_MfAudioType(
        &MFAudioFormat_PCM, PORT_FILM_SAMPLE_RATE * BYTES_PER_SAMPLE_FRAME);
    if (in == NULL) {
        return E_FAIL;
    }
    IMFMediaType_SetUINT32(
        in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, BYTES_PER_SAMPLE_FRAME);
    hr = IMFSinkWriter_SetInputMediaType(mf->writer, mf->audio, in, NULL);
    IMFMediaType_Release(in);
    return hr;
}

static HRESULT port_MfWriter(
    PortMf *mf, const wchar_t *wide, s32 hardware, u32 rate_n, u32 rate_d) {
    IMFAttributes *attributes = NULL;
    HRESULT hr = g_mf.attributes(&attributes, 2);

    if (FAILED(hr)) {
        return hr;
    }
    IMFAttributes_SetUINT32(
        attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hardware);
    IMFAttributes_SetGUID(attributes, &MF_TRANSCODE_CONTAINERTYPE,
                          &MFTranscodeContainerType_MPEG4);
    hr = g_mf.sink_writer(wide, NULL, attributes, &mf->writer);
    IMFAttributes_Release(attributes);
    if (SUCCEEDED(hr)) {
        hr = port_MfStreams(mf, rate_n, rate_d);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSinkWriter_BeginWriting(mf->writer);
    }
    if (FAILED(hr) && mf->writer != NULL) {
        IMFSinkWriter_Release(mf->writer);
        mf->writer = NULL;
        DeleteFileW(wide);
    }
    return hr;
}

PortMf *port_MfOpen(const char *path, s32 w, s32 h, char *why, u32 room) {
    static const u32 g_rates[2][2] = {
        {PORT_FILM_RATE_NUMERATOR, PORT_FILM_RATE_DENOMINATOR},
        {59826, 1000},
    };
    wchar_t wide[PATH_BYTES];
    PortMf *mf;
    HRESULT hr = E_FAIL;
    s32 hardware;
    s32 rate;

    if (!port_MfLoad()) {
        snprintf(why, room, "this Windows has no Media Foundation");
        return NULL;
    }
    mf = calloc(1, sizeof(PortMf));
    if (mf == NULL) {
        snprintf(why, room, "no memory for the film");
        return NULL;
    }
    mf->w = w;
    mf->h = h;
    snprintf(mf->path, sizeof(mf->path), "%s", path);
    mf->com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (FAILED(g_mf.startup(MF_VERSION, MFSTARTUP_FULL))) {
        snprintf(why, room, "Media Foundation does not start");
        if (mf->com) {
            CoUninitialize();
        }
        free(mf);
        return NULL;
    }
    port_MfWide(path, wide, PATH_BYTES);
    /* the graphics card's encoder where there is one, then Windows' own;
     * the blank's rate, then the nearest a thousandth */
    for (hardware = 1; hardware >= 0 && mf->writer == NULL; hardware--) {
        for (rate = 0; rate < 2 && mf->writer == NULL; rate++) {
            hr = port_MfWriter(
                mf, wide, hardware, g_rates[rate][0], g_rates[rate][1]);
        }
    }
    if (mf->writer == NULL) {
        snprintf(why, room,
                 "Windows' H.264 encoder does not take the film (0x%08lX)",
                 (unsigned long)hr);
        g_mf.shutdown();
        if (mf->com) {
            CoUninitialize();
        }
        free(mf);
        return NULL;
    }
    return mf;
}

/* A sample of `bytes` filled by `fill`, at `time` for `duration`. */
static s32 port_MfWrite(
    PortMf *mf, DWORD stream, u32 bytes, u64 time, u64 duration,
    void (*fill)(PortMf *, u8 *, const void *), const void *from) {
    IMFMediaBuffer *buffer = NULL;
    IMFSample *sample = NULL;
    BYTE *data = NULL;
    HRESULT hr = g_mf.buffer(bytes, &buffer);

    if (SUCCEEDED(hr)) {
        hr = IMFMediaBuffer_Lock(buffer, &data, NULL, NULL);
    }
    if (SUCCEEDED(hr)) {
        fill(mf, data, from);
        IMFMediaBuffer_Unlock(buffer);
        hr = IMFMediaBuffer_SetCurrentLength(buffer, bytes);
    }
    if (SUCCEEDED(hr)) {
        hr = g_mf.sample(&sample);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSample_AddBuffer(sample, buffer);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSample_SetSampleTime(sample, (LONGLONG)time);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSample_SetSampleDuration(sample, (LONGLONG)duration);
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSinkWriter_WriteSample(mf->writer, stream, sample);
    }
    if (sample != NULL) {
        IMFSample_Release(sample);
    }
    if (buffer != NULL) {
        IMFMediaBuffer_Release(buffer);
    }
    return SUCCEEDED(hr);
}

/* 0xFFRRGGBB to NV12: BT.709's luma and chroma, 16 to 235 and 240, the
 * chroma of each two by two averaged */
static void port_MfFillPicture(PortMf *mf, u8 *out, const void *from) {
    const u32 *pixels = from;
    u8 *chroma = out + (size_t)mf->w * (size_t)mf->h;
    s32 x;
    s32 y;

    for (y = 0; y < mf->h; y++) {
        const u32 *row = pixels + (size_t)y * (size_t)mf->w;
        u8 *luma = out + (size_t)y * (size_t)mf->w;

        for (x = 0; x < mf->w; x++) {
            u32 p = row[x];
            s32 r = (s32)((p >> 16) & 0xFF);
            s32 g = (s32)((p >> 8) & 0xFF);
            s32 b = (s32)(p & 0xFF);

            luma[x] = (u8)(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
        }
    }
    for (y = 0; y < mf->h; y += 2) {
        const u32 *top = pixels + (size_t)y * (size_t)mf->w;
        const u32 *under = top + mf->w;
        u8 *line = chroma + (size_t)(y / 2) * (size_t)mf->w;

        for (x = 0; x < mf->w; x += 2) {
            s32 r = 0;
            s32 g = 0;
            s32 b = 0;
            s32 i;
            u32 four[4];

            four[0] = top[x];
            four[1] = top[x + 1];
            four[2] = under[x];
            four[3] = under[x + 1];
            for (i = 0; i < 4; i++) {
                r += (s32)((four[i] >> 16) & 0xFF);
                g += (s32)((four[i] >> 8) & 0xFF);
                b += (s32)(four[i] & 0xFF);
            }
            /* the sums are four times the average: the shift is 10 */
            line[x] = (u8)(((-26 * r - 86 * g + 112 * b + 512) >> 10) + 128);
            line[x + 1] =
                (u8)(((112 * r - 102 * g - 10 * b + 512) >> 10) + 128);
        }
    }
}

static u32 g_mf_sound_bytes;

static void port_MfFillSound(PortMf *mf, u8 *out, const void *from) {
    (void)mf;
    memcpy(out, from, g_mf_sound_bytes);
}

static u64 port_MfFrameTime(u64 index) {
    return index * PORT_FILM_RATE_DENOMINATOR * HUNDRED_NANOSECONDS /
           PORT_FILM_RATE_NUMERATOR;
}

s32 port_MfPicture(PortMf *mf, const u32 *pixels, u32 index) {
    u64 time = port_MfFrameTime(index);

    return port_MfWrite(
        mf, mf->video, (u32)(mf->w * mf->h * 3 / 2), time,
        port_MfFrameTime((u64)index + 1) - time, port_MfFillPicture, pixels);
}

s32 port_MfSound(PortMf *mf, const s16 *samples, u32 frames, u64 at) {
    u64 time = at * HUNDRED_NANOSECONDS / PORT_FILM_SAMPLE_RATE;
    u64 end = (at + frames) * HUNDRED_NANOSECONDS / PORT_FILM_SAMPLE_RATE;

    if (frames == 0) {
        return TRUE;
    }
    g_mf_sound_bytes = frames * BYTES_PER_SAMPLE_FRAME;
    return port_MfWrite(mf, mf->audio, g_mf_sound_bytes, time, end - time,
                        port_MfFillSound, samples);
}

/* ---- the moov box first ---- */

static u32 port_Big(const u8 *at) {
    return ((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) | at[3];
}

static void port_PutBig(u8 *at, u32 value) {
    at[0] = (u8)(value >> 24);
    at[1] = (u8)(value >> 16);
    at[2] = (u8)(value >> 8);
    at[3] = (u8)value;
}

/* The chunks' places (stco, co64) in a box's boxes moved on by `by`:
 * FALSE where the box is not as an MP4's is. */
static s32 port_MoveChunks(u8 *box, u64 size, u32 by) {
    u64 at = 8;

    while (at + 8 <= size) {
        u8 *child = box + at;
        u32 bytes = port_Big(child);
        u32 i;

        if (bytes < 8 || at + bytes > size) {
            return FALSE;
        }
        if (memcmp(child + 4, "trak", 4) == 0 ||
            memcmp(child + 4, "mdia", 4) == 0 ||
            memcmp(child + 4, "minf", 4) == 0 ||
            memcmp(child + 4, "stbl", 4) == 0) {
            if (!port_MoveChunks(child, bytes, by)) {
                return FALSE;
            }
        } else if (memcmp(child + 4, "stco", 4) == 0 && bytes >= 16) {
            u32 count = port_Big(child + 12);

            for (i = 0; i < count && 16 + i * 4 + 4 <= bytes; i++) {
                u32 place = port_Big(child + 16 + i * 4);

                if (place + by < place) {
                    return FALSE;
                }
                port_PutBig(child + 16 + i * 4, place + by);
            }
        } else if (memcmp(child + 4, "co64", 4) == 0 && bytes >= 16) {
            u32 count = port_Big(child + 12);

            for (i = 0; i < count && 16 + i * 8 + 8 <= bytes; i++) {
                u8 *entry = child + 16 + i * 8;
                u64 place =
                    ((u64)port_Big(entry) << 32 | port_Big(entry + 4)) + by;

                port_PutBig(entry, (u32)(place >> 32));
                port_PutBig(entry + 4, (u32)place);
            }
        }
        at += bytes;
    }
    return TRUE;
}

static s32 port_CopyBytes(FILE *from, FILE *to, u64 start, u64 count) {
    static u8 g_buffer[1 << 16];

    _fseeki64(from, (__int64)start, SEEK_SET);
    while (count > 0) {
        size_t piece =
            count < sizeof(g_buffer) ? (size_t)count : sizeof(g_buffer);

        if (fread(g_buffer, 1, piece, from) != piece ||
            fwrite(g_buffer, 1, piece, to) != piece) {
            return FALSE;
        }
        count -= piece;
    }
    return TRUE;
}

/* Media Foundation writes the index of the film (moov) after its media
 * (mdat): a browser given the film from a server cannot begin it until
 * all of it is there. It is put first, as a web server's films have it
 * (ffmpeg's `faststart`), the chunks' places moved on by its size. Where
 * the file is not as expected it is left as it is, which plays too. */
static void port_MfMoovFirst(const char *path) {
    char part[PATH_BYTES + 8];
    FILE *in = fopen(path, "rb");
    FILE *out;
    u8 head[16];
    u64 at = 0;
    u64 end;
    u64 mdat = 0;
    u64 moov = 0;
    u64 moov_bytes = 0;
    u8 *box;
    s32 ok;

    if (in == NULL) {
        return;
    }
    _fseeki64(in, 0, SEEK_END);
    end = (u64)_ftelli64(in);
    while (at + 8 <= end) {
        u64 bytes;

        _fseeki64(in, (__int64)at, SEEK_SET);
        if (fread(head, 1, 16, in) < 8) {
            break;
        }
        bytes = port_Big(head);
        if (bytes == 1) {
            bytes = (u64)port_Big(head + 8) << 32 | port_Big(head + 12);
        } else if (bytes == 0) {
            bytes = end - at;
        }
        if (bytes < 8) {
            break;
        }
        if (memcmp(head + 4, "mdat", 4) == 0 && mdat == 0) {
            mdat = at;
        } else if (memcmp(head + 4, "moov", 4) == 0) {
            moov = at;
            moov_bytes = bytes;
        }
        at += bytes;
    }
    if (mdat == 0 || moov <= mdat || moov_bytes > 0x10000000U ||
        moov + moov_bytes > end) {
        fclose(in);
        return;
    }
    box = malloc((size_t)moov_bytes);
    _fseeki64(in, (__int64)moov, SEEK_SET);
    if (box == NULL || fread(box, 1, (size_t)moov_bytes, in) != moov_bytes ||
        !port_MoveChunks(box, moov_bytes, (u32)moov_bytes)) {
        free(box);
        fclose(in);
        return;
    }
    snprintf(part, sizeof(part), "%s.part", path);
    out = fopen(part, "wb");
    ok = out != NULL && port_CopyBytes(in, out, 0, mdat) &&
         fwrite(box, 1, (size_t)moov_bytes, out) == moov_bytes &&
         port_CopyBytes(in, out, mdat, moov - mdat) &&
         port_CopyBytes(in, out, moov + moov_bytes, end - moov - moov_bytes);
    if (out != NULL) {
        ok = fclose(out) == 0 && ok;
    }
    fclose(in);
    free(box);
    if (ok && MoveFileExA(part, path, MOVEFILE_REPLACE_EXISTING)) {
        return;
    }
    remove(part);
}

s32 port_MfClose(PortMf *mf, char *why, u32 room) {
    HRESULT hr = IMFSinkWriter_Finalize(mf->writer);

    IMFSinkWriter_Release(mf->writer);
    g_mf.shutdown();
    if (mf->com) {
        CoUninitialize();
    }
    if (SUCCEEDED(hr)) {
        port_MfMoovFirst(mf->path);
    }
    free(mf);
    if (FAILED(hr)) {
        snprintf(
            why, room, "the MP4 was not finished (0x%08lX)", (unsigned long)hr);
        return FALSE;
    }
    return TRUE;
}

/* ---- reading one back, for a test ---- */

/* The film's pictures in order, the wanted ones into `folder`; and each
 * one's time in frames.txt. */
static s32 port_MfReadPictures(IMFSourceReader *reader, const char *folder,
                               const u8 *wanted, u32 wanted_count) {
    char path[PATH_BYTES];
    IMFMediaType *type = NULL;
    UINT64 size = 0;
    UINT32 stride_word = 0;
    s32 stride;
    s32 w;
    s32 h;
    u32 index = 0;
    u32 *pixels;
    FILE *times;
    s32 ok = TRUE;

    if (FAILED(IMFSourceReader_GetCurrentMediaType(
            reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) {
        return FALSE;
    }
    IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size);
    w = (s32)(size >> 32);
    h = (s32)(size & 0xFFFFFFFFU);
    stride = SUCCEEDED(IMFMediaType_GetUINT32(
                 type, &MF_MT_DEFAULT_STRIDE, &stride_word))
                 ? (s32)stride_word
                 : w * BYTES_PER_PIXEL;
    IMFMediaType_Release(type);
    pixels = malloc((size_t)w * (size_t)h * BYTES_PER_PIXEL);
    snprintf(path, sizeof(path), "%s/frames.txt", folder);
    times = fopen(path, "w");
    if (pixels == NULL || times == NULL || w <= 0 || h <= 0) {
        free(pixels);
        if (times != NULL) {
            fclose(times);
        }
        return FALSE;
    }
    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;

        if (FAILED(IMFSourceReader_ReadSample(
                reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL,
                &flags, &time, &sample))) {
            ok = FALSE;
            break;
        }
        if (sample != NULL) {
            fprintf(times, "%u %lld\n", index, (long long)time);
            if (index < wanted_count && wanted[index]) {
                IMFMediaBuffer *buffer = NULL;
                BYTE *data = NULL;
                s32 y;

                if (SUCCEEDED(
                        IMFSample_ConvertToContiguousBuffer(sample, &buffer)) &&
                    SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, NULL))) {
                    for (y = 0; y < h; y++) {
                        const u32 *row =
                            (const u32
                                 *)(const void
                                        *)(stride >= 0
                                               ? data +
                                                     (size_t)y * (size_t)stride
                                               : data + (size_t)(h - 1 - y) *
                                                            (size_t)(-stride));
                        s32 x;

                        for (x = 0; x < w; x++) {
                            pixels[(size_t)y * (size_t)w + (size_t)x] =
                                row[x] | 0xFF000000U;
                        }
                    }
                    IMFMediaBuffer_Unlock(buffer);
                    ok = port_FilmWriteRaw(folder, index, pixels, w, h);
                } else {
                    ok = FALSE;
                }
                if (buffer != NULL) {
                    IMFMediaBuffer_Release(buffer);
                }
            }
            index++;
            IMFSample_Release(sample);
        }
        if (!ok || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
    }
    fclose(times);
    free(pixels);
    return ok;
}

static s32 port_MfReadSound(IMFSourceReader *reader, const char *folder) {
    char path[PATH_BYTES];
    s16 *sound = NULL;
    u64 frames = 0;
    u64 room = 0;
    s32 ok = TRUE;

    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;

        if (FAILED(IMFSourceReader_ReadSample(
                reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL,
                &flags, &time, &sample))) {
            ok = FALSE;
            break;
        }
        if (sample != NULL) {
            IMFMediaBuffer *buffer = NULL;
            BYTE *data = NULL;
            DWORD bytes = 0;

            if (SUCCEEDED(
                    IMFSample_ConvertToContiguousBuffer(sample, &buffer)) &&
                SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &bytes))) {
                u64 more = bytes / BYTES_PER_SAMPLE_FRAME;

                if (frames + more > room) {
                    s16 *grown;

                    room = (frames + more) * 2;
                    grown =
                        realloc(sound, (size_t)room * BYTES_PER_SAMPLE_FRAME);
                    if (grown == NULL) {
                        ok = FALSE;
                    } else {
                        sound = grown;
                    }
                }
                if (ok) {
                    memcpy(sound + frames * PORT_FILM_CHANNELS, data,
                           (size_t)more * BYTES_PER_SAMPLE_FRAME);
                    frames += more;
                }
                IMFMediaBuffer_Unlock(buffer);
            }
            if (buffer != NULL) {
                IMFMediaBuffer_Release(buffer);
            }
            IMFSample_Release(sample);
        }
        if (!ok || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
    }
    snprintf(path, sizeof(path), "%s/sound.wav", folder);
    ok = ok && port_FilmWriteWav(path, sound, frames);
    free(sound);
    return ok;
}

/* A reader of the film with one stream chosen, as `subtype`. */
static IMFSourceReader *port_MfReader(const wchar_t *wide, s32 video) {
    IMFAttributes *attributes = NULL;
    IMFSourceReader *reader = NULL;
    IMFMediaType *type = NULL;
    DWORD stream = video ? (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM
                         : (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;
    HRESULT hr = g_mf.attributes(&attributes, 1);

    if (FAILED(hr)) {
        return NULL;
    }
    IMFAttributes_SetUINT32(
        attributes, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    hr = g_mf.source_reader(wide, attributes, &reader);
    IMFAttributes_Release(attributes);
    if (FAILED(hr)) {
        return NULL;
    }
    IMFSourceReader_SetStreamSelection(
        reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, stream, TRUE);
    hr = g_mf.media_type(&type);
    if (SUCCEEDED(hr)) {
        if (video) {
            IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        } else {
            IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
            IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
            IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            IMFMediaType_SetUINT32(
                type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, PORT_FILM_SAMPLE_RATE);
            IMFMediaType_SetUINT32(
                type, &MF_MT_AUDIO_NUM_CHANNELS, PORT_FILM_CHANNELS);
        }
        hr = IMFSourceReader_SetCurrentMediaType(reader, stream, NULL, type);
        IMFMediaType_Release(type);
    }
    if (FAILED(hr)) {
        IMFSourceReader_Release(reader);
        return NULL;
    }
    return reader;
}

s32 port_MfRead(const char *path, const char *folder, const u8 *wanted,
                u32 wanted_count, char *why, u32 room) {
    wchar_t wide[PATH_BYTES];
    IMFSourceReader *reader;
    s32 com;
    s32 ok = FALSE;

    if (!port_MfLoad()) {
        snprintf(why, room, "this Windows has no Media Foundation");
        return FALSE;
    }
    com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (FAILED(g_mf.startup(MF_VERSION, MFSTARTUP_FULL))) {
        snprintf(why, room, "Media Foundation does not start");
        return FALSE;
    }
    port_MfWide(path, wide, PATH_BYTES);
    reader = port_MfReader(wide, TRUE);
    if (reader != NULL) {
        ok = port_MfReadPictures(reader, folder, wanted, wanted_count);
        IMFSourceReader_Release(reader);
        reader = port_MfReader(wide, FALSE);
        ok = ok && reader != NULL && port_MfReadSound(reader, folder);
        if (reader != NULL) {
            IMFSourceReader_Release(reader);
        }
    }
    if (!ok) {
        snprintf(
            why, room, "%s could not be read back by Media Foundation", path);
    }
    g_mf.shutdown();
    if (com) {
        CoUninitialize();
    }
    return ok;
}

#else

s32 port_MfCan(void) { return FALSE; }

PortMf *port_MfOpen(const char *path, s32 w, s32 h, char *why, u32 room) {
    (void)path;
    (void)w;
    (void)h;
    snprintf(why, room,
             "an MP4 is made by Windows' Media Foundation: here, "
             "an .avi");
    return NULL;
}

s32 port_MfPicture(PortMf *mf, const u32 *pixels, u32 index) {
    (void)mf;
    (void)pixels;
    (void)index;
    return FALSE;
}

s32 port_MfSound(PortMf *mf, const s16 *samples, u32 frames, u64 at) {
    (void)mf;
    (void)samples;
    (void)frames;
    (void)at;
    return FALSE;
}

s32 port_MfClose(PortMf *mf, char *why, u32 room) {
    (void)mf;
    snprintf(why, room, "no MP4 here");
    return FALSE;
}

s32 port_MfRead(const char *path, const char *folder, const u8 *wanted,
                u32 wanted_count, char *why, u32 room) {
    (void)path;
    (void)folder;
    (void)wanted;
    (void)wanted_count;
    snprintf(why, room, "an MP4 is read back by Windows' Media Foundation");
    return FALSE;
}

#endif
