/* The port's music mods: keys, loops, WAV and Ogg files, and the player. */
/* A piece of music's key from the disc's file and channel, its loop from
 * an Ogg file's tags or a WAV's `smpl` chunk, where a sample of the chip
 * falls in a piece of another rate, the WAV reader on files made here and
 * by tests/port/music.py, the Ogg decoder (stb_vorbis, src/port/vorbis.c)
 * on a file that test made with the reference encoder, and the player
 * (src/port/music.c) following a made-up drive: from the stream's start,
 * through a step run again after a rollback (nothing heard, nothing
 * moved, and on from where the clock is after), another stream with no
 * piece, the same piece again, the drive stopped, the setting off and
 * on, a loop and an end. Nothing here is the game's. */
#include "common.h"
#include "port_discfiles.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_music.h"
#include "port_spu.h"
#include "src/port/text.c"
#include "src/port/vorbis.c"
#include "src/port/music.c"
#include "runtime/runtime.h"

/* tests/port/music.py's WAVs: so many samples of numbers that say where
 * they are, and the loop of the first */
#define MADE_FRAMES 20000
#define MADE_LOOP_START 1000
#define MADE_LOOP_END 3000
#define MADE_SINCE 5000
#define BLANK 737

/* ---- the port around the player, made up ---- */

s32 g_port_rerun;
s32 g_port_ahead;
s16 *g_port_xa_unmixed;

static u32 g_made_now;
static s32 g_made_streaming;
static u32 g_made_sector;
static s32 g_made_channel;
static u32 g_made_since;
static const char *g_made_looped;
static const char *g_made_once;

u32 port_GetSampleCount(void) { return g_made_now; }

s32 port_GetCdStream(u32 *sector, s32 *channel, u32 *since) {
    if (!g_made_streaming) {
        return 0;
    }
    *sector = g_made_sector;
    *channel = g_made_channel;
    *since = g_made_since;
    return 1;
}

/* the drive's volumes at all of each side: as they are */
void port_MixCdAudio(s16 *samples, u32 frames) {
    (void)samples;
    (void)frames;
}

/* BB2_M1.XA: sectors 1000 to 1999 of the made-up disc */
static PortDiscFile g_made_file = {"LOADSE/BB2_M1.XA", 7, 1000, 2048000};

const PortDiscFile *port_DiscFileAt(u32 sector) {
    return sector >= 1000 && sector < 2000 ? &g_made_file : NULL;
}

/* the mods: bb2_m1-3 is the looped WAV, bb2_m1-5 the other */
const char *port_ModFind(s32 kind, const char *key) {
    if (kind != PORT_MOD_MUSIC) {
        return NULL;
    }
    if (port_TextsEqual(key, "bb2_m1-3")) {
        return g_made_looped;
    }
    if (port_TextsEqual(key, "bb2_m1-5")) {
        return g_made_once;
    }
    return NULL;
}

s32 port_ModsCount(s32 kind) { return kind == PORT_MOD_MUSIC ? 2 : 0; }

u32 port_ModFileSize(const char *path) {
    unsigned size = 0;

    return test_ReadFile(path, &size) != NULL ? size : 0;
}

u32 port_ModReadFile(const char *path, void *dest, u32 room) {
    unsigned size = 0;
    void *bytes = test_ReadFile(path, &size);

    if (bytes == NULL) {
        return 0;
    }
    if (size > room) {
        size = room;
    }
    test_Copy(dest, bytes, size);
    return size;
}

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

/* the dump is not made here */
s32 port_HostCreate(const char *path) {
    (void)path;
    return -1;
}

s32 port_HostOpenReadWrite(const char *path) {
    (void)path;
    return -1;
}

s32 port_HostWriteAt(s32 file, u32 offset, const void *bytes, u32 count) {
    (void)file;
    (void)offset;
    (void)bytes;
    (void)count;
    return 0;
}

void port_HostClose(s32 file) { (void)file; }

s32 port_HostMakeFolder(const char *path) { return path != NULL; }

/* what the player says goes to the log; the test keeps it quiet */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    (void)stream;
    (void)bytes;
    (void)count;
}

void port_Quit(s32 code) {
    test_Print("quit %d\n", code);
    for (;;) {
    }
}

/* ---- keys, tags and places ---- */

static void test_Keys(void) {
    char key[PORT_MOD_KEY_SIZE];

    port_MusicKey("BB2_M1.XA;1", 7, key, sizeof(key));
    CHECK(port_TextsEqual(key, "bb2_m1-7"));
    port_MusicKey("BB2_M2.XA", 0, key, sizeof(key));
    CHECK(port_TextsEqual(key, "bb2_m2-0"));
    port_MusicKey("OPENING.STR", -1, key, sizeof(key));
    CHECK(port_TextsEqual(key, "opening"));
    port_MusicKey("MAR_XA28.XA", 12, key, sizeof(key));
    CHECK(port_TextsEqual(key, "mar_xa28-12"));
    /* a name with no ending keeps all of itself */
    port_MusicKey("KAISYA", 3, key, sizeof(key));
    CHECK(port_TextsEqual(key, "kaisya-3"));
}

static void test_Tags(void) {
    PortMusicFormat format = {44100, 2, 100000, 0, 0, 0, 0};

    CHECK(!port_MusicReadTag("TITLE=LOOPSTART=5", &format));
    CHECK(!format.loops);
    /* the start alone loops to the end */
    CHECK(port_MusicReadTag("LOOPSTART=4410", &format));
    CHECK(format.loops);
    CHECK_EQ(format.loop_start, 4410);
    CHECK_EQ(format.loop_end, 100000);
    CHECK(port_MusicReadTag("looplength=1000", &format));
    CHECK_EQ(format.loop_end, 5410);
    CHECK(port_MusicReadTag("LoopEnd=9000", &format));
    CHECK_EQ(format.loop_end, 9000);
    /* a value that is not a number is a loop tag, and changes nothing */
    CHECK(port_MusicReadTag("LOOPEND=nine", &format));
    CHECK_EQ(format.loop_end, 9000);
    CHECK(!port_MusicReadTag("LOOPSTARTX=1", &format));
}

static void test_Places(void) {
    u32 fraction = 99;
    u32 frame;

    /* the chip's own rate: sample for sample */
    for (frame = 0; frame < 200000; frame += 777) {
        CHECK_EQ(port_MusicSourceAt(frame, 44100, &fraction), frame);
        CHECK_EQ(fraction, 0);
    }
    /* 48,000 a second: 160 samples for each 147 of the chip's, and the
     * fraction in 44,100ths, which are 300ths of a 147th; to an hour and
     * past, which a product of 32 bits would not reach */
    for (frame = 0; frame < 26000000; frame += 99991) {
        u32 at = port_MusicSourceAt(frame, 48000, &fraction);

        CHECK_EQ(at, frame * 160 / 147);
        CHECK_EQ(fraction, frame * 160 % 147 * 300);
    }
    /* 22,050: every other sample of the chip's, half way */
    CHECK_EQ(port_MusicSourceAt(3, 22050, &fraction), 1);
    CHECK_EQ(fraction, 22050);
}

static void test_LoopFrames(void) {
    PortMusicFormat format = {44100, 2, 5000, 1, 1000, 3000, 0};

    CHECK_EQ(port_MusicLoopFrame(&format, 0), 0);
    CHECK_EQ(port_MusicLoopFrame(&format, 2999), 2999);
    CHECK_EQ(port_MusicLoopFrame(&format, 3000), 1000);
    CHECK_EQ(port_MusicLoopFrame(&format, 4999), 2999);
    CHECK_EQ(port_MusicLoopFrame(&format, 5000), 1000);
    CHECK_EQ(port_MusicLoopFrame(&format, 1001000), 1000);
    format.loops = 0;
    CHECK_EQ(port_MusicLoopFrame(&format, 4000), 4000);
    CHECK_EQ(port_MusicLoopFrame(&format, 6000), 6000);
}

/* ---- WAV files made here ---- */

static u32 made_Put(u8 *at, u32 value, u32 bytes) {
    u32 i;

    for (i = 0; i < bytes; i++) {
        at[i] = (u8)(value >> (i * 8));
    }
    return bytes;
}

static u32 made_Tag(u8 *at, const char *tag) {
    u32 i;

    for (i = 0; i < 4; i++) {
        at[i] = (u8)tag[i];
    }
    return 4;
}

/* A WAV: its format chunk (kind, channels, bits; `extensible` says the
 * kind again further on), and four frames of data. */
static u32 made_Wav(u8 *out, u32 kind, u32 channels, u32 bits, s32 extensible) {
    u32 at = 0;
    u32 frame_bytes = channels * bits / 8;
    u32 i;

    at += made_Tag(out + at, "RIFF");
    at += made_Put(out + at, 0, 4);
    at += made_Tag(out + at, "WAVE");
    at += made_Tag(out + at, "fmt ");
    at += made_Put(out + at, extensible ? 40 : 16, 4);
    at += made_Put(out + at, extensible ? 0xFFFE : kind, 2);
    at += made_Put(out + at, channels, 2);
    at += made_Put(out + at, 22050, 4);
    at += made_Put(out + at, 22050 * frame_bytes, 4);
    at += made_Put(out + at, frame_bytes, 2);
    at += made_Put(out + at, bits, 2);
    if (extensible) {
        at += made_Put(out + at, 22, 2);
        at += made_Put(out + at, bits, 2);
        at += made_Put(out + at, 3, 4);
        at += made_Put(out + at, kind, 2);
        for (i = 0; i < 14; i++) {
            out[at++] = 0;
        }
    }
    at += made_Tag(out + at, "data");
    at += made_Put(out + at, 4 * frame_bytes, 4);
    for (i = 0; i < 4 * frame_bytes; i++) {
        out[at++] = (u8)(0x40 + i);
    }
    made_Put(out + 4, at - 8, 4);
    return at;
}

static void test_WavHeaders(void) {
    static u8 file[256];
    PortMusicFormat format;
    u32 data_at = 0;
    u32 frame_bytes = 0;
    u32 bits = 0;
    u32 size;

    size = made_Wav(file, 1, 2, 16, 0);
    CHECK(
        port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    CHECK_EQ(format.rate, 22050);
    CHECK_EQ(format.channels, 2);
    CHECK_EQ(format.frames, 4);
    CHECK_EQ(frame_bytes, 4);
    CHECK_EQ(bits, 16);
    CHECK_EQ(data_at, 44);
    CHECK(!format.loops);
    size = made_Wav(file, 1, 1, 8, 0);
    CHECK(
        port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    CHECK_EQ(format.channels, 1);
    CHECK_EQ(frame_bytes, 1);
    size = made_Wav(file, 1, 2, 24, 1);
    CHECK(
        port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    CHECK_EQ(frame_bytes, 6);
    CHECK_EQ(data_at, 68);
    /* samples of floating point, plain or extensible, are not read */
    size = made_Wav(file, 3, 2, 32, 0);
    CHECK(
        !port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    size = made_Wav(file, 3, 2, 32, 1);
    CHECK(
        !port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    /* nor 12 bits, nor a file that is not RIFF WAVE */
    size = made_Wav(file, 1, 1, 12, 0);
    CHECK(
        !port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
    size = made_Wav(file, 1, 2, 16, 0);
    file[8] = 'X';
    CHECK(
        !port_MusicReadWav(file, size, &format, &data_at, &frame_bytes, &bits));
}

/* tests/port/music.py's WAVs: their samples where they say they are, the
 * loop of the first, none in the second */
static void test_WavFiles(void) {
    PortMusicFormat format;
    u32 data_at = 0;
    u32 frame_bytes = 0;
    u32 bits = 0;
    unsigned size = 0;
    const u8 *bytes = test_ReadFile(g_made_looped, &size);

    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    CHECK(
        port_MusicReadWav(bytes, size, &format, &data_at, &frame_bytes, &bits));
    CHECK_EQ(format.frames, MADE_FRAMES);
    CHECK_EQ(format.rate, 44100);
    CHECK(format.loops);
    CHECK_EQ(format.loop_start, MADE_LOOP_START);
    CHECK_EQ(format.loop_end, MADE_LOOP_END);
    bytes = test_ReadFile(g_made_once, &size);
    CHECK(
        port_MusicReadWav(bytes, size, &format, &data_at, &frame_bytes, &bits));
    CHECK(!format.loops);
}

/* ---- the Ogg decoder on tests/port/music.py's file ---- */

static void test_Ogg(const char *path) {
    static s16 samples[4096 * 2];
    static s16 again[16 * 2];
    PortMusicFormat format = {0, 0, 0, 0, 0, 0, 0};
    unsigned size = 0;
    const u8 *bytes;
    u8 *work;
    void *ogg;
    u32 decoded = 0;
    u32 crossings = 0;
    s32 peak = 0;
    s32 last = 0;
    s16 at_loop[16 * 2];
    u32 i;
    u32 got;

    if (port_TextsEqual(path, "none")) {
        test_Print("ogg none\n");
        return;
    }
    bytes = test_ReadFile(path, &size);
    work = test_Alloc(WORK_BYTES);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    ogg = port_OggOpen(bytes, size, work, WORK_BYTES, &format);
    CHECK(ogg != NULL);
    if (ogg == NULL) {
        return;
    }
    for (i = 0; port_OggTag(ogg, i) != NULL; i++) {
        port_MusicReadTag(port_OggTag(ogg, i), &format);
    }
    /* every sample, the one channel on both sides */
    while ((got = port_OggRead(ogg, samples, 4096)) > 0) {
        for (i = 0; i < got; i++) {
            s32 value = samples[i * 2];

            CHECK_EQ(samples[i * 2 + 1], value);
            if (decoded + i == (u32)format.loop_start) {
                test_Copy(at_loop, &samples[i * 2],
                          (got - i >= 16 ? 16 : got - i) * 4);
            }
            if ((value < 0) != (last < 0) && decoded + i > 0) {
                crossings++;
            }
            if (value > peak) {
                peak = value;
            }
            last = value;
        }
        decoded += got;
    }
    /* a seek gives the samples a read from the start gave there */
    CHECK(port_OggSeek(ogg, format.loop_start));
    CHECK_EQ(port_OggRead(ogg, again, 16), 16);
    CHECK(test_BytesEqual(again, at_loop, sizeof(again)));
    port_OggClose(ogg);
    test_Print("ogg rate %u channels %u frames %u decoded %u loop_start %u "
               "loop_end %u crossings %u peak %d\n",
               format.rate, format.channels, format.frames, decoded,
               format.loop_start, format.loop_end, crossings, peak);
}

/* ---- the player ---- */

/* Sample `frame` of the WAVs, as tests/port/music.py makes them. */
static s16 made_Sample(u32 frame, u32 side) {
    return (s16)(((frame * 37 + side * 11) % 2000) * 16 - 16000);
}

/* Whether `got` is the piece from sample `first` on, `count` of them,
 * as the looped WAV's loop takes them (or the other's end, past which
 * is silence). */
static s32 made_IsPiece(const s16 *got, u32 first, u32 count, s32 looped) {
    PortMusicFormat format = {
        44100, 2, MADE_FRAMES, looped, MADE_LOOP_START, MADE_LOOP_END, 0};
    u32 i;

    for (i = 0; i < count; i++) {
        u32 at = port_MusicLoopFrame(&format, first + i);
        s16 left = at < MADE_FRAMES ? made_Sample(at, 0) : 0;
        s16 right = at < MADE_FRAMES ? made_Sample(at, 1) : 0;

        if (got[i * 2] != left || got[i * 2 + 1] != right) {
            test_Print("sample %u is %d %d, not %d %d\n", first + i, got[i * 2],
                       got[i * 2 + 1], left, right);
            return FALSE;
        }
    }
    return TRUE;
}

static s32 made_IsSilent(const s16 *got, u32 count) {
    u32 i;

    for (i = 0; i < count * 2; i++) {
        if (got[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The drive starts stream `channel` of BB2_M1.XA at `since`. */
static void made_Stream(s32 channel, u32 since) {
    g_made_streaming = 1;
    g_made_sector = 1000;
    g_made_channel = channel;
    g_made_since = since;
    g_made_now = since;
}

static void test_Player(void) {
    s16 *heard = NULL;
    const s16 *got;
    u32 i;

    /* nothing playing: the disc's sound is heard */
    g_made_now = 100;
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
    /* the stream of bb2_m1-3 begins: the piece from its start, blank by
     * blank */
    made_Stream(3, MADE_SINCE);
    for (i = 0; i < 4; i++) {
        got = port_PlayMusic(BLANK, &heard);
        CHECK(got != NULL && made_IsPiece(got, i * BLANK, BLANK, TRUE));
        g_made_now += BLANK;
    }
    /* a rollback: the clock goes back and the steps are run again,
     * quietly; nothing is heard and nothing moves. Then the clock is on
     * where it was, and so is the piece. */
    g_port_rerun = 1;
    g_made_now = MADE_SINCE + BLANK;
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
    g_made_now += BLANK;
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
    g_port_rerun = 0;
    g_made_now = MADE_SINCE + 4 * BLANK;
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsPiece(got, 4 * BLANK, BLANK, TRUE));
    /* well into it: the loop has gone round */
    g_made_now = MADE_SINCE + 7000;
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsPiece(got, 7000, BLANK, TRUE));
    /* the setting off: the disc's sound; on again, the piece where the
     * stream is */
    port_MusicSetOn(0);
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
    port_MusicSetOn(1);
    g_made_now = MADE_SINCE + 9000;
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsPiece(got, 9000, BLANK, TRUE));
    /* the drive stops: silence, not what the disc still had waiting */
    g_made_streaming = 0;
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsSilent(got, BLANK));
    /* a stream no mod has: the disc's sound */
    made_Stream(4, 30000);
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
    /* the first piece again: from its start */
    made_Stream(3, 40000);
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsPiece(got, 0, BLANK, TRUE));
    /* a piece with no loop: to its end, then silence */
    made_Stream(5, 60000);
    g_made_now = 60000 + MADE_FRAMES - 100;
    got = port_PlayMusic(BLANK, &heard);
    CHECK(got != NULL && made_IsPiece(got, MADE_FRAMES - 100, BLANK, FALSE));
    /* a stream on no file of the disc's: nothing to name it by */
    made_Stream(3, 70000);
    g_made_sector = 5000;
    CHECK(port_PlayMusic(BLANK, &heard) == NULL);
}

void test_Main(void) {
    g_made_looped = test_Arg(0);
    g_made_once = test_Arg(1);
    test_Keys();
    test_Tags();
    test_Places();
    test_LoopFrames();
    test_WavHeaders();
    test_WavFiles();
    test_Ogg(test_Arg(2));
    test_Player();
}
