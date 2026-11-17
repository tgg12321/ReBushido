/* The native port's decoder of Ogg Vorbis, for a mod's music (port_music.h;
 * docs/MODS.md, "Music"). It is Sean Barrett's stb_vorbis, version 1.24,
 * in the public domain (or under the MIT licence, as its user chooses:
 * both are at the end of src/port/vendor/stb_vorbis.c), included here as
 * it ships.
 *
 * The port has no C library, so what stb_vorbis takes from one is written
 * here, ahead of it: its memory comes out of one buffer the caller gives,
 * which is stb_vorbis's own way of working without malloc; the arithmetic
 * of its setup (the sines of its window and its transform, a codebook's
 * root) is done in double precision by series; its sort is a shell sort.
 * The headers it includes by name are the port's empty ones
 * (src/port/vendor/libc/, which tools/port_build.py gives this file
 * alone). This is the one file of the port with numbers that are not
 * whole: the 64-bit program turns the processor's vector registers on for
 * it, and none of it is the game's state (config/port_state.txt).
 *
 * The check by a modern compiler's analyser (tools/modern/check.py) is
 * not given those headers and reads none of stb_vorbis: code the project
 * did not write is not held to its checklist. */
#include "common.h"
#include "port_music.h"

#if __has_include(<stdlib.h>)

/* ---- what stb_vorbis takes from a C library ---- */

#define VORBIS_PI 3.14159265358979323846
#define VORBIS_LN2 0.69314718055994530942
/* 2 to the 52nd: a double this large is a whole number already */
#define VORBIS_WHOLE 4503599627370496.0
#define VORBIS_SERIES_TERMS 24
/* the largest element the sort moves */
#define VORBIS_SWAP_BYTES 64
/* a step of the scaling: 2 to the 16th */
#define VORBIS_STEP_BITS 16
#define VORBIS_STEP 65536.0
/* the largest whole power multiplied out */
#define VORBIS_POWER_MAX 64

typedef u64 uint64_t;
typedef s64 int64_t;
typedef s32 (*PortVorbisOrder)(const void *first, const void *second);

/* The shell sort's gaps, Ciura's. */
static const u32 g_port_vorbis_gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};

/* The largest whole number not more than `x`. */
static double port_VorbisFloor(double x) {
    s64 whole;

    if (x >= VORBIS_WHOLE || x <= -VORBIS_WHOLE) {
        return x;
    }
    whole = (s64)x;
    if ((double)whole > x) {
        whole--;
    }
    return (double)whole;
}

/* `x` times 2 to the `exponent`. */
static double port_VorbisScale(double x, s32 exponent) {
    while (exponent >= VORBIS_STEP_BITS) {
        x *= VORBIS_STEP;
        exponent -= VORBIS_STEP_BITS;
    }
    while (exponent <= -VORBIS_STEP_BITS) {
        x /= VORBIS_STEP;
        exponent += VORBIS_STEP_BITS;
    }
    while (exponent > 0) {
        x *= 2.0;
        exponent--;
    }
    while (exponent < 0) {
        x /= 2.0;
        exponent++;
    }
    return x;
}

/* The natural logarithm of `x` (more than 0): `x` as m times 2 to the k,
 * m from 1 to 2, and log m = 2 atanh((m - 1) / (m + 1)) by its series. */
static double port_VorbisLog(double x) {
    s32 power = 0;
    double ratio;
    double square;
    double term;
    double sum;
    s32 i;

    if (x <= 0.0) {
        return -VORBIS_WHOLE;
    }
    while (x >= 2.0) {
        x /= 2.0;
        power++;
    }
    while (x < 1.0) {
        x *= 2.0;
        power--;
    }
    ratio = (x - 1.0) / (x + 1.0);
    square = ratio * ratio;
    term = ratio;
    sum = 0.0;
    for (i = 0; i < VORBIS_SERIES_TERMS; i++) {
        sum += term / (2 * i + 1);
        term *= square;
    }
    return 2.0 * sum + power * VORBIS_LN2;
}

/* e to the `x`: 2 to a whole power times e to what is left, which is less
 * than log 2, by its series. */
static double port_VorbisExp(double x) {
    double power = port_VorbisFloor(x / VORBIS_LN2);
    double rest = x - power * VORBIS_LN2;
    double term = 1.0;
    double sum = 1.0;
    s32 i;

    for (i = 1; i < VORBIS_SERIES_TERMS; i++) {
        term *= rest / i;
        sum += term;
    }
    return port_VorbisScale(sum, (s32)power);
}

/* `x` to the `y`: by multiplying where `y` is whole, as it always is in
 * stb_vorbis, so that a codebook's root is found exactly. */
static double port_VorbisPower(double x, double y) {
    double result = 1.0;
    s32 times;

    if (y == port_VorbisFloor(y) && y >= 0.0 && y < VORBIS_POWER_MAX) {
        for (times = (s32)y; times > 0; times--) {
            result *= x;
        }
        return result;
    }
    return port_VorbisExp(y * port_VorbisLog(x));
}

/* The sine of `x`: brought within a quarter turn of 0, then its series. */
static double port_VorbisSine(double x) {
    double square;
    double term;
    double sum;
    s32 i;

    x -= port_VorbisFloor(x / (2.0 * VORBIS_PI) + 0.5) * (2.0 * VORBIS_PI);
    if (x > VORBIS_PI / 2.0) {
        x = VORBIS_PI - x;
    } else if (x < -VORBIS_PI / 2.0) {
        x = -VORBIS_PI - x;
    }
    square = x * x;
    term = x;
    sum = x;
    for (i = 1; i < VORBIS_SERIES_TERMS / 2; i++) {
        term *= -square / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static double port_VorbisCosine(double x) {
    return port_VorbisSine(x + VORBIS_PI / 2.0);
}

static void *port_VorbisCopy(void *dest, const void *from, u32 count) {
    u8 *to = dest;
    const u8 *bytes = from;

    while (count-- != 0) {
        *to++ = *bytes++;
    }
    return dest;
}

static void *port_VorbisFill(void *dest, s32 value, u32 count) {
    u8 *to = dest;

    while (count-- != 0) {
        *to++ = (u8)value;
    }
    return dest;
}

static s32 port_VorbisCompare(
    const void *first, const void *second, u32 count) {
    const u8 *left = first;
    const u8 *right = second;

    for (; count-- != 0; left++, right++) {
        if (*left != *right) {
            return *left - *right;
        }
    }
    return 0;
}

/* `count` elements of `size` bytes put in order by `order`: a shell sort,
 * which stb_vorbis asks for while it reads a file's codebooks. */
static void port_VorbisSort(
    void *base, u32 count, u32 size, PortVorbisOrder order) {
    u8 *bytes = base;
    u8 held[VORBIS_SWAP_BYTES];
    u32 step;
    u32 i;
    u32 j;

    if (size > VORBIS_SWAP_BYTES) {
        return;
    }
    for (step = 0; step < sizeof(g_port_vorbis_gaps) / sizeof(u32); step++) {
        u32 gap = g_port_vorbis_gaps[step];

        for (i = gap; i < count; i++) {
            port_VorbisCopy(held, bytes + i * size, size);
            for (j = i; j >= gap && order(bytes + (j - gap) * size, held) > 0;
                 j -= gap) {
                port_VorbisCopy(
                    bytes + j * size, bytes + (j - gap) * size, size);
            }
            port_VorbisCopy(bytes + j * size, held, size);
        }
    }
}

/* stb_vorbis is given its buffer every time, so these are never called;
 * they are named because its code names them. */
#define malloc(size) ((void *)0)
#define realloc(old, size) ((void *)0)
#define free(old) ((void)(old))
#define alloca(size) ((void *)0)
#define assert(test) ((void)0)
#define abs(x) ((x) < 0 ? -(x) : (x))
#define memcpy(dest, from, count) port_VorbisCopy(dest, from, (u32)(count))
#define memset(dest, value, count) port_VorbisFill(dest, value, (u32)(count))
#define memcmp(a, b, count) port_VorbisCompare(a, b, (u32)(count))
#define qsort(base, count, size, order)                                        \
    port_VorbisSort(base, (u32)(count), (u32)(size), (PortVorbisOrder)(order))
#define floor(x) port_VorbisFloor(x)
#define ldexp(x, exponent) port_VorbisScale(x, exponent)
#define exp(x) port_VorbisExp(x)
#define log(x) port_VorbisLog(x)
#define pow(x, y) port_VorbisPower(x, y)
#define sin(x) port_VorbisSine(x)
#define cos(x) port_VorbisCosine(x)
#define INT_MAX 0x7FFFFFFF
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX 0xFFFFFFFFU

/* The game's own include/math.h is found before the port's empty one: it
 * is kept out of stb_vorbis and let in again after. */
#define MATH_H
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wold-style-definition"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#pragma GCC diagnostic ignored "-Wbad-function-cast"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wlogical-op"
#pragma GCC diagnostic ignored "-Wduplicated-branches"
#pragma GCC diagnostic ignored "-Wjump-misses-init"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wswitch-default"
#include "src/port/vendor/stb_vorbis.c"
#pragma GCC diagnostic pop
#undef MATH_H
#undef malloc
#undef realloc
#undef free
#undef alloca
#undef assert
#undef abs
#undef memcpy
#undef memset
#undef memcmp
#undef qsort
#undef floor
#undef ldexp
#undef exp
#undef log
#undef pow
#undef sin
#undef cos
#undef INT_MAX
#undef INT_MIN
#undef UINT_MAX

/* ---- the port's way in (port_music.h) ---- */

void *port_OggOpen(const u8 *data, u32 size, u8 *work, u32 work_size,
                   PortMusicFormat *format) {
    stb_vorbis_alloc room;
    stb_vorbis *ogg;
    stb_vorbis_info info;
    s32 error = 0;

    room.alloc_buffer = (char *)work;
    room.alloc_buffer_length_in_bytes = (s32)work_size;
    ogg = stb_vorbis_open_memory(data, (s32)size, &error, &room);
    if (ogg == NULL) {
        format->problem = error;
        return NULL;
    }
    info = stb_vorbis_get_info(ogg);
    format->rate = info.sample_rate;
    format->channels = (u32)info.channels;
    format->frames = stb_vorbis_stream_length_in_samples(ogg);
    return ogg;
}

const char *port_OggTag(void *ogg, u32 index) {
    stb_vorbis_comment tags = stb_vorbis_get_comment((stb_vorbis *)ogg);

    return index < (u32)tags.comment_list_length ? tags.comment_list[index]
                                                 : NULL;
}

u32 port_OggRead(void *ogg, s16 *out, u32 frames) {
    s32 got = stb_vorbis_get_samples_short_interleaved(
        (stb_vorbis *)ogg, 2, out, (s32)(frames * 2));

    return got > 0 ? (u32)got : 0;
}

s32 port_OggSeek(void *ogg, u32 frame) {
    return stb_vorbis_seek((stb_vorbis *)ogg, frame) != 0;
}

void port_OggClose(void *ogg) { stb_vorbis_close((stb_vorbis *)ogg); }

#endif /* the port's empty C library */
#include "bb2.h"
