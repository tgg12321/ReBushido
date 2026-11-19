/* A fighter's model read from a glTF binary file (gl_fighter_file.h): the
 * file's JSON, its accessors and its PNG pictures, into plain arrays. It
 * has no library of its own to lean on, so it carries the little of JSON,
 * of zlib's inflate and of PNG that such a file needs. Nothing here draws
 * and nothing here is the game's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_fighter_file.h"

/* ---- JSON: the file's description, as a list of tokens ---- */

enum { JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_NUMBER, JSON_WORD };

typedef struct {
    s32 kind;
    s32 start; /* where its text is: a string without its quotes */
    s32 end;
    s32 size; /* an object's keys, an array's items */
    s32 next; /* the token after it and everything inside it */
} JsonToken;

typedef struct {
    const char *text;
    s32 length;
    JsonToken *token;
    s32 count;
    s32 room;
} Json;

static s32 port_JsonSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static s32 port_JsonAdd(Json *json, s32 kind, s32 start) {
    if (json->count == json->room) {
        s32 room = json->room ? json->room * 2 : 1024;
        JsonToken *more = realloc(json->token, (size_t)room * sizeof(*more));

        if (more == NULL) {
            return -1;
        }
        json->token = more;
        json->room = room;
    }
    json->token[json->count].kind = kind;
    json->token[json->count].start = start;
    json->token[json->count].end = start;
    json->token[json->count].size = 0;
    json->token[json->count].next = 0;
    return json->count++;
}

/* One value from `*at` on, and everything inside it. The index of its
 * token, or -1. */
static s32 port_JsonValue(Json *json, s32 *at, s32 depth) {
    const char *t = json->text;
    s32 i = *at;
    s32 me;

    while (i < json->length && port_JsonSpace(t[i])) {
        i++;
    }
    if (i >= json->length || depth > 64) {
        return -1;
    }
    if (t[i] == '{' || t[i] == '[') {
        s32 is_object = t[i] == '{';
        char close = is_object ? '}' : ']';

        me = port_JsonAdd(json, is_object ? JSON_OBJECT : JSON_ARRAY, i);
        if (me < 0) {
            return -1;
        }
        i++;
        for (;;) {
            while (i < json->length && port_JsonSpace(t[i])) {
                i++;
            }
            if (i < json->length && t[i] == close) {
                i++;
                break;
            }
            if (is_object) {
                if (port_JsonValue(json, &i, depth + 1) < 0) {
                    return -1;
                }
                while (i < json->length && port_JsonSpace(t[i])) {
                    i++;
                }
                if (i >= json->length || t[i] != ':') {
                    return -1;
                }
                i++;
            }
            if (port_JsonValue(json, &i, depth + 1) < 0) {
                return -1;
            }
            json->token[me].size++;
            while (i < json->length && port_JsonSpace(t[i])) {
                i++;
            }
            if (i < json->length && t[i] == ',') {
                i++;
            } else if (i >= json->length || t[i] != close) {
                return -1;
            }
        }
    } else if (t[i] == '"') {
        me = port_JsonAdd(json, JSON_STRING, i + 1);
        if (me < 0) {
            return -1;
        }
        i++;
        while (i < json->length && t[i] != '"') {
            i += t[i] == '\\' ? 2 : 1;
        }
        if (i >= json->length) {
            return -1;
        }
        json->token[me].end = i;
        i++;
        json->token[me].next = json->count;
        *at = i;
        return me;
    } else {
        s32 kind = (t[i] == '-' || (t[i] >= '0' && t[i] <= '9'))
                       ? JSON_NUMBER
                       : JSON_WORD;

        me = port_JsonAdd(json, kind, i);
        if (me < 0) {
            return -1;
        }
        while (i < json->length && !port_JsonSpace(t[i]) && t[i] != ',' &&
               t[i] != '}' && t[i] != ']' && t[i] != ':') {
            i++;
        }
    }
    json->token[me].end = i;
    json->token[me].next = json->count;
    *at = i;
    return me;
}

static s32 port_JsonIs(const Json *json, s32 token, const char *word) {
    s32 length = (s32)strlen(word);

    return token >= 0 && json->token[token].kind == JSON_STRING &&
           json->token[token].end - json->token[token].start == length &&
           memcmp(json->text + json->token[token].start, word,
                  (size_t)length) == 0;
}

/* The value of an object's key, or -1. */
static s32 port_JsonGet(const Json *json, s32 object, const char *key) {
    s32 i;
    s32 at;

    if (object < 0 || json->token[object].kind != JSON_OBJECT) {
        return -1;
    }
    at = object + 1;
    for (i = 0; i < json->token[object].size; i++) {
        s32 value = json->token[at].next;

        if (port_JsonIs(json, at, key)) {
            return value;
        }
        at = json->token[value].next;
    }
    return -1;
}

/* An array's item, or -1. */
static s32 port_JsonItem(const Json *json, s32 array, s32 index) {
    s32 at;
    s32 i;

    if (array < 0 || json->token[array].kind != JSON_ARRAY || index < 0 ||
        index >= json->token[array].size) {
        return -1;
    }
    at = array + 1;
    for (i = 0; i < index; i++) {
        at = json->token[at].next;
    }
    return at;
}

static double port_JsonNumber(const Json *json, s32 token, double otherwise) {
    char text[64];
    s32 length;

    if (token < 0 || json->token[token].kind != JSON_NUMBER) {
        return otherwise;
    }
    length = json->token[token].end - json->token[token].start;
    if (length <= 0 || length >= (s32)sizeof(text)) {
        return otherwise;
    }
    memcpy(text, json->text + json->token[token].start, (size_t)length);
    text[length] = 0;
    return strtod(text, NULL);
}

static s32 port_JsonInt(const Json *json, s32 token, s32 otherwise) {
    return (s32)port_JsonNumber(json, token, (double)otherwise);
}

/* ---- inflate (RFC 1951), for the PNG pictures ---- */

typedef struct {
    const u8 *in;
    u32 size;
    u32 at;
    u32 bits;
    s32 bit_count;
    u8 *out;
    u32 out_size;
    u32 out_room;
    s32 failed;
} Inflate;

typedef struct {
    u16 count[16];
    u16 symbol[320];
} Huffman;

static s32 port_InflateBit(Inflate *z, s32 n) {
    s32 value;

    while (z->bit_count < n) {
        if (z->at >= z->size) {
            z->failed = TRUE;
            return 0;
        }
        z->bits |= (u32)z->in[z->at++] << z->bit_count;
        z->bit_count += 8;
    }
    value = (s32)(z->bits & ((1U << n) - 1));
    z->bits >>= n;
    z->bit_count -= n;
    return value;
}

static void port_InflatePut(Inflate *z, u8 byte) {
    if (z->out_size == z->out_room) {
        u32 room = z->out_room ? z->out_room * 2 : 65536;
        u8 *more = realloc(z->out, room);

        if (more == NULL) {
            z->failed = TRUE;
            return;
        }
        z->out = more;
        z->out_room = room;
    }
    z->out[z->out_size++] = byte;
}

static void port_HuffmanMake(Huffman *h, const u8 *lengths, s32 n) {
    u16 offset[16];
    s32 i;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    offset[1] = 0;
    for (i = 1; i < 15; i++) {
        offset[i + 1] = (u16)(offset[i] + h->count[i]);
    }
    for (i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offset[lengths[i]]++] = (u16)i;
        }
    }
}

static s32 port_HuffmanRead(Inflate *z, const Huffman *h) {
    s32 code = 0;
    s32 first = 0;
    s32 index = 0;
    s32 len;

    for (len = 1; len < 16; len++) {
        s32 count;

        code |= port_InflateBit(z, 1);
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (z->failed) {
            return -1;
        }
    }
    z->failed = TRUE;
    return -1;
}

static const u16 g_length_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 g_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 g_distance_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 g_distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void port_InflateCodes(
    Inflate *z, const Huffman *lengths, const Huffman *distances) {
    for (;;) {
        s32 symbol = port_HuffmanRead(z, lengths);

        if (z->failed || symbol < 0) {
            return;
        }
        if (symbol < 256) {
            port_InflatePut(z, (u8)symbol);
        } else if (symbol == 256) {
            return;
        } else {
            s32 length;
            s32 distance;

            symbol -= 257;
            if (symbol >= 29) {
                z->failed = TRUE;
                return;
            }
            length = g_length_base[symbol] +
                     port_InflateBit(z, g_length_extra[symbol]);
            symbol = port_HuffmanRead(z, distances);
            if (symbol < 0 || symbol >= 30) {
                z->failed = TRUE;
                return;
            }
            distance = g_distance_base[symbol] +
                       port_InflateBit(z, g_distance_extra[symbol]);
            if ((u32)distance > z->out_size) {
                z->failed = TRUE;
                return;
            }
            while (length-- > 0 && !z->failed) {
                port_InflatePut(z, z->out[z->out_size - (u32)distance]);
            }
        }
    }
}

static void port_InflateFixed(Inflate *z) {
    static Huffman lengths;
    static Huffman distances;
    static s32 made;

    if (!made) {
        u8 l[288];
        s32 i;

        for (i = 0; i < 144; i++) {
            l[i] = 8;
        }
        for (; i < 256; i++) {
            l[i] = 9;
        }
        for (; i < 280; i++) {
            l[i] = 7;
        }
        for (; i < 288; i++) {
            l[i] = 8;
        }
        port_HuffmanMake(&lengths, l, 288);
        for (i = 0; i < 30; i++) {
            l[i] = 5;
        }
        port_HuffmanMake(&distances, l, 30);
        made = TRUE;
    }
    port_InflateCodes(z, &lengths, &distances);
}

static void port_InflateDynamic(Inflate *z) {
    static const u8 order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huffman lengths;
    Huffman distances;
    u8 l[320];
    s32 nlen = port_InflateBit(z, 5) + 257;
    s32 ndist = port_InflateBit(z, 5) + 1;
    s32 ncode = port_InflateBit(z, 4) + 4;
    s32 i;

    memset(l, 0, sizeof(l));
    for (i = 0; i < ncode; i++) {
        l[order[i]] = (u8)port_InflateBit(z, 3);
    }
    port_HuffmanMake(&lengths, l, 19);
    memset(l, 0, sizeof(l));
    i = 0;
    while (i < nlen + ndist && !z->failed) {
        s32 symbol = port_HuffmanRead(z, &lengths);
        s32 repeat;
        u8 value = 0;

        if (symbol < 0) {
            return;
        }
        if (symbol < 16) {
            l[i++] = (u8)symbol;
            continue;
        }
        if (symbol == 16) {
            if (i == 0) {
                z->failed = TRUE;
                return;
            }
            value = l[i - 1];
            repeat = 3 + port_InflateBit(z, 2);
        } else if (symbol == 17) {
            repeat = 3 + port_InflateBit(z, 3);
        } else {
            repeat = 11 + port_InflateBit(z, 7);
        }
        if (i + repeat > nlen + ndist) {
            z->failed = TRUE;
            return;
        }
        while (repeat-- > 0) {
            l[i++] = value;
        }
    }
    port_HuffmanMake(&lengths, l, nlen);
    port_HuffmanMake(&distances, l + nlen, ndist);
    port_InflateCodes(z, &lengths, &distances);
}

/* A zlib stream (the two bytes before it and the four after it are not
 * checked) into a buffer of its own. */
static u8 *port_Inflate(const u8 *in, u32 size, u32 *out_size) {
    Inflate z;
    s32 last;

    memset(&z, 0, sizeof(z));
    if (size < 2) {
        return NULL;
    }
    z.in = in + 2;
    z.size = size - 2;
    do {
        s32 kind;

        last = port_InflateBit(&z, 1);
        kind = port_InflateBit(&z, 2);
        if (kind == 0) {
            u32 length;

            z.bits = 0;
            z.bit_count = 0;
            if (z.at + 4 > z.size) {
                z.failed = TRUE;
                break;
            }
            length = (u32)z.in[z.at] | ((u32)z.in[z.at + 1] << 8);
            z.at += 4;
            if (z.at + length > z.size) {
                z.failed = TRUE;
                break;
            }
            while (length-- > 0) {
                port_InflatePut(&z, z.in[z.at++]);
            }
        } else if (kind == 1) {
            port_InflateFixed(&z);
        } else if (kind == 2) {
            port_InflateDynamic(&z);
        } else {
            z.failed = TRUE;
        }
    } while (!last && !z.failed);
    if (z.failed) {
        free(z.out);
        return NULL;
    }
    *out_size = z.out_size;
    return z.out;
}

/* ---- PNG ---- */

static u32 port_Big32(const u8 *at) {
    return ((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) | at[3];
}

static s32 port_Paeth(s32 a, s32 b, s32 c) {
    s32 p = a + b - c;
    s32 pa = p > a ? p - a : a - p;
    s32 pb = p > b ? p - b : b - p;
    s32 pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

const char *port_FighterPngDecode(
    const u8 *bytes, u32 size, s32 *out_w, s32 *out_h, u8 **out_rgba) {
    static const u8 magic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    u8 palette[256 * 4];
    u8 *packed = NULL;
    u32 packed_size = 0;
    u8 *raw;
    u32 raw_size = 0;
    u8 *rgba;
    s32 w = 0;
    s32 h = 0;
    s32 kind = -1;
    s32 channels;
    s32 stride;
    s32 x;
    s32 y;
    u32 at = 8;

    memset(palette, 255, sizeof(palette));
    if (size < 8 || memcmp(bytes, magic, 8) != 0) {
        return "a picture is not a PNG";
    }
    while (at + 12 <= size) {
        u32 length = port_Big32(bytes + at);
        const u8 *kind_at = bytes + at + 4;
        const u8 *data = bytes + at + 8;

        if (at + 12 + length > size) {
            break;
        }
        if (!memcmp(kind_at, "IHDR", 4) && length >= 13) {
            w = (s32)port_Big32(data);
            h = (s32)port_Big32(data + 4);
            if (data[8] != 8 || data[12] != 0) {
                free(packed);
                return "a PNG is not of 8 bits a channel, or is interlaced";
            }
            kind = data[9];
        } else if (!memcmp(kind_at, "PLTE", 4)) {
            u32 i;

            for (i = 0; i < length / 3 && i < 256; i++) {
                palette[i * 4] = data[i * 3];
                palette[i * 4 + 1] = data[i * 3 + 1];
                palette[i * 4 + 2] = data[i * 3 + 2];
            }
        } else if (!memcmp(kind_at, "tRNS", 4)) {
            u32 i;

            for (i = 0; i < length && i < 256; i++) {
                palette[i * 4 + 3] = data[i];
            }
        } else if (!memcmp(kind_at, "IDAT", 4)) {
            u8 *more = realloc(packed, packed_size + length + 1);

            if (more == NULL) {
                free(packed);
                return "no memory for a picture";
            }
            packed = more;
            memcpy(packed + packed_size, data, length);
            packed_size += length;
        } else if (!memcmp(kind_at, "IEND", 4)) {
            break;
        }
        at += 12 + length;
    }
    switch (kind) {
    case 0:
        channels = 1;
        break;
    case 2:
        channels = 3;
        break;
    case 3:
        channels = 1;
        break;
    case 4:
        channels = 2;
        break;
    case 6:
        channels = 4;
        break;
    default:
        free(packed);
        return "a PNG of a kind not read";
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || packed == NULL) {
        free(packed);
        return "a PNG has no picture";
    }
    raw = port_Inflate(packed, packed_size, &raw_size);
    free(packed);
    stride = w * channels;
    if (raw == NULL || raw_size < (u32)(h * (stride + 1))) {
        free(raw);
        return "a PNG's picture does not unpack";
    }
    /* each line's filter undone in place: the line's first byte says
     * which, and a byte is worked out from the one before it, the one
     * above it and the one above that */
    for (y = 0; y < h; y++) {
        u8 *line = raw + y * (stride + 1);
        u8 filter = line[0];
        u8 *cur = line + 1;
        const u8 *up = y > 0 ? raw + (y - 1) * (stride + 1) + 1 : NULL;

        for (x = 0; x < stride; x++) {
            s32 a = x >= channels ? cur[x - channels] : 0;
            s32 b = up != NULL ? up[x] : 0;
            s32 c = (up != NULL && x >= channels) ? up[x - channels] : 0;

            switch (filter) {
            case 1:
                cur[x] = (u8)(cur[x] + a);
                break;
            case 2:
                cur[x] = (u8)(cur[x] + b);
                break;
            case 3:
                cur[x] = (u8)(cur[x] + ((a + b) >> 1));
                break;
            case 4:
                cur[x] = (u8)(cur[x] + port_Paeth(a, b, c));
                break;
            default:
                break;
            }
        }
    }
    rgba = malloc((size_t)w * (size_t)h * 4);
    if (rgba == NULL) {
        free(raw);
        return "no memory for a picture";
    }
    for (y = 0; y < h; y++) {
        const u8 *cur = raw + y * (stride + 1) + 1;
        u8 *out = rgba + (size_t)y * (size_t)w * 4;

        for (x = 0; x < w; x++) {
            const u8 *p = cur + x * channels;

            switch (kind) {
            case 0:
                out[0] = out[1] = out[2] = p[0];
                out[3] = 255;
                break;
            case 2:
                out[0] = p[0];
                out[1] = p[1];
                out[2] = p[2];
                out[3] = 255;
                break;
            case 3:
                memcpy(out, palette + p[0] * 4, 4);
                break;
            case 4:
                out[0] = out[1] = out[2] = p[0];
                out[3] = p[1];
                break;
            default:
                memcpy(out, p, 4);
                break;
            }
            out += 4;
        }
    }
    free(raw);
    *out_w = w;
    *out_h = h;
    *out_rgba = rgba;
    return NULL;
}

/* ---- glTF ---- */

#define GLB_MAGIC 0x46546C67U
#define GLB_JSON 0x4E4F534AU
#define GLB_BIN 0x004E4942U

#define GLTF_BYTE 5120
#define GLTF_UNSIGNED_BYTE 5121
#define GLTF_SHORT 5122
#define GLTF_UNSIGNED_SHORT 5123
#define GLTF_UNSIGNED_INT 5125
#define GLTF_FLOAT 5126

typedef struct {
    const Json *json;
    const u8 *bin;
    u32 bin_size;
} Gltf;

static u32 port_Little32(const u8 *at) {
    return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) |
           ((u32)at[3] << 24);
}

static s32 port_GltfComponents(const Json *json, s32 type) {
    if (port_JsonIs(json, type, "SCALAR")) {
        return 1;
    }
    if (port_JsonIs(json, type, "VEC2")) {
        return 2;
    }
    if (port_JsonIs(json, type, "VEC3")) {
        return 3;
    }
    if (port_JsonIs(json, type, "VEC4")) {
        return 4;
    }
    if (port_JsonIs(json, type, "MAT4")) {
        return 16;
    }
    return 0;
}

static s32 port_GltfSize(s32 component) {
    switch (component) {
    case GLTF_BYTE:
    case GLTF_UNSIGNED_BYTE:
        return 1;
    case GLTF_SHORT:
    case GLTF_UNSIGNED_SHORT:
        return 2;
    case GLTF_UNSIGNED_INT:
    case GLTF_FLOAT:
        return 4;
    default:
        return 0;
    }
}

/* An accessor's values as floats, `want` of them to an element (fewer in
 * the file are left 0), into `out`, which has room for `count` elements.
 * A normalised integer is made 0 to 1, as glTF says. The number of
 * elements, or -1. */
static s32 port_GltfRead(
    const Gltf *g, s32 index, float *out, s32 room, s32 want) {
    const Json *json = g->json;
    s32 accessor =
        port_JsonItem(json, port_JsonGet(json, 0, "accessors"), index);
    s32 view;
    s32 component;
    s32 components;
    s32 count;
    s32 size;
    s32 stride;
    s32 normalised;
    u32 offset;
    s32 i;
    s32 k;

    if (accessor < 0) {
        return -1;
    }
    view = port_JsonItem(
        json, port_JsonGet(json, 0, "bufferViews"),
        port_JsonInt(json, port_JsonGet(json, accessor, "bufferView"), -1));
    component =
        port_JsonInt(json, port_JsonGet(json, accessor, "componentType"), 0);
    components =
        port_GltfComponents(json, port_JsonGet(json, accessor, "type"));
    count = port_JsonInt(json, port_JsonGet(json, accessor, "count"), -1);
    size = port_GltfSize(component);
    normalised =
        port_JsonGet(json, accessor, "normalized") >= 0 &&
        json->text[json->token[port_JsonGet(json, accessor, "normalized")]
                       .start] == 't';
    if (view < 0 || size == 0 || components == 0 || count < 0 || count > room) {
        return -1;
    }
    offset =
        (u32)port_JsonInt(json, port_JsonGet(json, view, "byteOffset"), 0) +
        (u32)port_JsonInt(json, port_JsonGet(json, accessor, "byteOffset"), 0);
    stride = port_JsonInt(json, port_JsonGet(json, view, "byteStride"), 0);
    if (stride == 0) {
        stride = size * components;
    }
    if (count > 0 && (u64)offset + (u64)stride * (u64)(count - 1) +
                             (u64)(size * components) >
                         g->bin_size) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const u8 *at = g->bin + offset + (u32)(i * stride);

        for (k = 0; k < want; k++) {
            float value = 0.0f;

            if (k < components) {
                const u8 *p = at + k * size;

                switch (component) {
                case GLTF_FLOAT: {
                    u32 word = port_Little32(p);

                    memcpy(&value, &word, sizeof(value));
                    break;
                }
                case GLTF_UNSIGNED_BYTE:
                    value = normalised ? p[0] / 255.0f : (float)p[0];
                    break;
                case GLTF_BYTE:
                    value = normalised ? (s8)p[0] / 127.0f : (float)(s8)p[0];
                    break;
                case GLTF_UNSIGNED_SHORT: {
                    u16 half = (u16)(p[0] | (p[1] << 8));

                    value = normalised ? half / 65535.0f : (float)half;
                    break;
                }
                case GLTF_SHORT: {
                    s16 half = (s16)(p[0] | (p[1] << 8));

                    value = normalised ? half / 32767.0f : (float)half;
                    break;
                }
                default:
                    value = (float)port_Little32(p);
                    break;
                }
            }
            out[i * want + k] = value;
        }
    }
    return count;
}

/* An accessor of whole numbers (indices, joints) read exactly. */
static s32 port_GltfReadIndices(const Gltf *g, s32 index, u32 *out, s32 room) {
    const Json *json = g->json;
    s32 accessor =
        port_JsonItem(json, port_JsonGet(json, 0, "accessors"), index);
    s32 view;
    s32 component;
    s32 count;
    s32 size;
    s32 stride;
    u32 offset;
    s32 i;

    if (accessor < 0) {
        return -1;
    }
    view = port_JsonItem(
        json, port_JsonGet(json, 0, "bufferViews"),
        port_JsonInt(json, port_JsonGet(json, accessor, "bufferView"), -1));
    component =
        port_JsonInt(json, port_JsonGet(json, accessor, "componentType"), 0);
    count = port_JsonInt(json, port_JsonGet(json, accessor, "count"), -1);
    size = port_GltfSize(component);
    if (view < 0 || size == 0 || component == GLTF_FLOAT || count < 0 ||
        count > room) {
        return -1;
    }
    offset =
        (u32)port_JsonInt(json, port_JsonGet(json, view, "byteOffset"), 0) +
        (u32)port_JsonInt(json, port_JsonGet(json, accessor, "byteOffset"), 0);
    stride = port_JsonInt(json, port_JsonGet(json, view, "byteStride"), 0);
    if (stride == 0) {
        stride = size;
    }
    if (count > 0 && (u64)offset + (u64)stride * (u64)(count - 1) + (u64)size >
                         g->bin_size) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const u8 *p = g->bin + offset + (u32)(i * stride);

        out[i] = size == 1   ? p[0]
                 : size == 2 ? (u32)(p[0] | (p[1] << 8))
                             : port_Little32(p);
    }
    return count;
}

static s32 port_GltfCount(const Gltf *g, s32 index) {
    s32 accessor =
        port_JsonItem(g->json, port_JsonGet(g->json, 0, "accessors"), index);

    return port_JsonInt(g->json, port_JsonGet(g->json, accessor, "count"), -1);
}

/* The picture a material's texture (`base`, a textureInfo) is, decoded,
 * among the file's textures; -1 if there is none. */
static s32 port_GltfImage(const Gltf *g, s32 base, s32 is_map,
                          PortFighterFile *out, const char **problem);

static s32 port_GltfTexture(
    const Gltf *g, s32 material, PortFighterFile *out, const char **problem) {
    const Json *json = g->json;
    s32 pbr = port_JsonGet(json, material, "pbrMetallicRoughness");

    return port_GltfImage(
        g, port_JsonGet(json, pbr, "baseColorTexture"), FALSE, out, problem);
}

/* A remade model's maps (rb_remade): the normal map, and the packed
 * occlusion, roughness and metal. */
static void port_GltfMaps(const Gltf *g, s32 material, PortFighterPart *part,
                          PortFighterFile *out, const char **problem) {
    const Json *json = g->json;
    s32 pbr = port_JsonGet(json, material, "pbrMetallicRoughness");
    s32 orm = port_JsonGet(json, pbr, "metallicRoughnessTexture");

    part->normal_map = port_GltfImage(
        g, port_JsonGet(json, material, "normalTexture"), TRUE, out, problem);
    if (*problem != NULL) {
        return;
    }
    if (orm < 0) {
        orm = port_JsonGet(json, material, "occlusionTexture");
    }
    part->orm_map = port_GltfImage(g, orm, TRUE, out, problem);
}

static s32 port_GltfImage(const Gltf *g, s32 base, s32 is_map,
                          PortFighterFile *out, const char **problem) {
    const Json *json = g->json;
    s32 texture = port_JsonItem(
        json, port_JsonGet(json, 0, "textures"),
        port_JsonInt(json, port_JsonGet(json, base, "index"), -1));
    s32 image = port_JsonItem(
        json, port_JsonGet(json, 0, "images"),
        port_JsonInt(json, port_JsonGet(json, texture, "source"), -1));
    s32 view = port_JsonItem(
        json, port_JsonGet(json, 0, "bufferViews"),
        port_JsonInt(json, port_JsonGet(json, image, "bufferView"), -1));
    u32 offset;
    u32 length;
    PortFighterTexture *t;
    const char *why;

    if (view < 0 || out->texture_count >= PORT_FIGHTER_FILE_TEXTURES) {
        return -1;
    }
    offset = (u32)port_JsonInt(json, port_JsonGet(json, view, "byteOffset"), 0);
    length = (u32)port_JsonInt(json, port_JsonGet(json, view, "byteLength"), 0);
    if ((u64)offset + length > g->bin_size) {
        *problem = "a picture is outside the file";
        return -1;
    }
    t = &out->texture[out->texture_count];
    why =
        port_FighterPngDecode(g->bin + offset, length, &t->w, &t->h, &t->rgba);
    if (why != NULL) {
        *problem = why;
        return -1;
    }
    out->is_map[out->texture_count] = (u8)is_map;
    return out->texture_count++;
}

/* Which of the game's bones each joint of the skin is, by its node's
 * name, `boneNN`; -1 for one that is none of them. */
static s32 port_GltfBoneOf(const Json *json, s32 node) {
    s32 name = port_JsonGet(json, node, "name");
    const char *t;
    s32 length;

    if (name < 0 || json->token[name].kind != JSON_STRING) {
        return -1;
    }
    t = json->text + json->token[name].start;
    length = json->token[name].end - json->token[name].start;
    if (length != 6 || memcmp(t, "bone", 4) != 0 || t[4] < '0' || t[4] > '9' ||
        t[5] < '0' || t[5] > '9') {
        return -1;
    }
    return (t[4] - '0') * 10 + (t[5] - '0');
}

void port_FighterFileFree(PortFighterFile *file) {
    s32 i;

    free(file->vertices);
    free(file->indices);
    for (i = 0; i < file->texture_count; i++) {
        free(file->texture[i].rgba);
    }
    free(file->index.rgba);
    memset(file, 0, sizeof(*file));
}

/* The terms of a primitive's vertices (gl_fighter_file.h): the file's own
 * _RB_TERM0 to _RB_TERM7, or the linear blend's made into terms. */
static const char *port_GltfTerms(
    const Gltf *g, s32 attributes, const s32 *joint_bone, s32 joint_count,
    PortFighterFile *out, s32 first, s32 count) {
    static const char * const names[PORT_FIGHTER_TERMS] = {
        "_RB_TERM0", "_RB_TERM1", "_RB_TERM2", "_RB_TERM3",
        "_RB_TERM4", "_RB_TERM5", "_RB_TERM6", "_RB_TERM7"};
    const Json *json = g->json;
    s32 half;
    s32 joints =
        port_JsonInt(json, port_JsonGet(json, attributes, "_RB_TJOINTS"), -1);
    float *scratch;
    s32 k;
    s32 j;

    if (joints < 0) {
        for (k = 0; k < count; k++) {
            PortFighterVertex *v = &out->vertices[first + k];

            for (j = 0; j < 4; j++) {
                const float *ibm = out->inverse_bind[v->joint[j]];
                s32 r;

                /* the inverse bind matrix is a column after a column */
                for (r = 0; r < 3; r++) {
                    v->term[j][r] =
                        v->weight[j] *
                        (ibm[r] * v->place[0] + ibm[4 + r] * v->place[1] +
                         ibm[8 + r] * v->place[2] + ibm[12 + r]);
                }
                v->term[j][3] = v->weight[j];
                v->term_joint[j] = v->joint[j];
            }
        }
        return NULL;
    }
    scratch = malloc((size_t)count * 4 * sizeof(float));
    if (scratch == NULL) {
        return "no memory";
    }
    /* four bones to each of _RB_TJOINTS and _RB_TJOINTS1, a file having
     * the first four alone if no vertex needs more */
    for (half = 0; half < 2 && joints >= 0; half++) {
        if (port_GltfRead(g, joints, scratch, count, 4) != count) {
            free(scratch);
            return "_RB_TJOINTS does not read";
        }
        for (k = 0; k < count; k++) {
            for (j = 0; j < 4; j++) {
                s32 joint = (s32)scratch[k * 4 + j];
                s32 bone =
                    joint >= 0 && joint < joint_count ? joint_bone[joint] : -1;

                out->vertices[first + k].term_joint[half * 4 + j] =
                    (u8)(bone >= 0 && bone < PORT_FIGHTER_FILE_JOINTS ? bone
                                                                      : 0);
            }
        }
        for (j = half * 4; j < half * 4 + 4; j++) {
            s32 term = port_JsonInt(
                json, port_JsonGet(json, attributes, names[j]), -1);

            if (term < 0 ||
                port_GltfRead(g, term, scratch, count, 4) != count) {
                free(scratch);
                return "_RB_TJOINTS without its four _RB_TERMs";
            }
            for (k = 0; k < count; k++) {
                memcpy(out->vertices[first + k].term[j], scratch + k * 4,
                       4 * sizeof(float));
            }
        }
        joints = port_JsonInt(
            json, port_JsonGet(json, attributes, "_RB_TJOINTS1"), -1);
    }
    free(scratch);
    return NULL;
}

/* extras.rb_cloth (gl_fighter_file.h): a cloth that does not read is left
 * out, and the game's own faces are drawn for it. */
static void port_GltfCloth(const Json *json, s32 extras, PortFighterFile *out) {
    s32 list = port_JsonGet(json, extras, "rb_cloth");
    s32 i;

    out->cloth_count = 0;
    if (list < 0 || json->token[list].kind != JSON_ARRAY) {
        return;
    }
    for (i = 0; i < json->token[list].size &&
                out->cloth_count < PORT_FIGHTER_FILE_CLOTHS;
         i++) {
        s32 item = port_JsonItem(json, list, i);
        s32 faces = port_JsonGet(json, item, "faces");
        s32 uvs = port_JsonGet(json, item, "uvs");
        PortFighterFileCloth *cloth = &out->cloth[out->cloth_count];
        s32 is_whole = TRUE;
        s32 f;

        if (faces < 0 || uvs < 0 || json->token[faces].kind != JSON_ARRAY ||
            json->token[uvs].kind != JSON_ARRAY ||
            json->token[faces].size <= 0 ||
            json->token[faces].size > PORT_FIGHTER_FILE_CLOTH_FACES ||
            json->token[uvs].size != json->token[faces].size) {
            continue;
        }
        cloth->mesh = port_JsonInt(json, port_JsonGet(json, item, "mesh"), -1);
        cloth->point_count =
            port_JsonInt(json, port_JsonGet(json, item, "points"), 0);
        cloth->face_count = json->token[faces].size;
        for (f = 0; f < cloth->face_count && is_whole; f++) {
            s32 face = port_JsonItem(json, faces, f);
            s32 uv = port_JsonItem(json, uvs, f);
            s32 n = face >= 0 ? json->token[face].size : 0;
            s32 c;

            if ((n != 3 && n != 4) || uv < 0 || json->token[uv].size != 2 * n) {
                is_whole = FALSE;
                break;
            }
            cloth->corners[f] = n;
            for (c = 0; c < n; c++) {
                cloth->face[f][c] =
                    port_JsonInt(json, port_JsonItem(json, face, c), -1);
                cloth->uv[f][c][0] = (float)port_JsonNumber(
                    json, port_JsonItem(json, uv, 2 * c), 0.0);
                cloth->uv[f][c][1] = (float)port_JsonNumber(
                    json, port_JsonItem(json, uv, 2 * c + 1), 0.0);
                if (cloth->face[f][c] < 0 ||
                    cloth->face[f][c] >= cloth->point_count) {
                    is_whole = FALSE;
                }
            }
        }
        if (is_whole && cloth->mesh >= 0) {
            out->cloth_count++;
        }
    }
}

/* extras.rb_palette and extras.rb_weapon (gl_fighter_file.h). */
static const char *port_GltfPalette(
    const Gltf *g, s32 extras, PortFighterFile *out) {
    const Json *json = g->json;
    s32 palette = port_JsonGet(json, extras, "rb_palette");
    s32 cluts = port_JsonGet(json, palette, "cluts");
    s32 colors = port_JsonGet(json, palette, "colors");
    s32 image = port_JsonItem(
        json, port_JsonGet(json, 0, "images"),
        port_JsonInt(json, port_JsonGet(json, palette, "image"), -1));
    s32 view;
    u32 offset;
    u32 length;
    s32 i;
    s32 k;

    out->weapon =
        port_JsonInt(json, port_JsonGet(json, extras, "rb_weapon"), -1);
    port_GltfCloth(json, extras, out);
    if (cluts < 0 || colors < 0 || image < 0) {
        return NULL;
    }
    for (i = 0; i < json->token[cluts].size && i < PORT_FIGHTER_FILE_CLUTS;
         i++) {
        s32 clut = port_JsonItem(json, cluts, i);
        s32 color = port_JsonItem(json, colors, i);
        PortFighterClut *c = &out->clut[i];

        c->x = port_JsonInt(json, port_JsonItem(json, clut, 0), 0);
        c->y = port_JsonInt(json, port_JsonItem(json, clut, 1), 0);
        c->entries = port_JsonInt(json, port_JsonItem(json, clut, 2), 0);
        if (c->entries != 16 && c->entries != 256) {
            return "rb_palette: a table of neither 16 nor 256 entries";
        }
        for (k = 0; k < c->entries; k++) {
            c->color[k] =
                (u16)port_JsonInt(json, port_JsonItem(json, color, k), 0);
        }
    }
    out->clut_count = i;
    view = port_JsonItem(
        json, port_JsonGet(json, 0, "bufferViews"),
        port_JsonInt(json, port_JsonGet(json, image, "bufferView"), -1));
    offset = (u32)port_JsonInt(json, port_JsonGet(json, view, "byteOffset"), 0);
    length = (u32)port_JsonInt(json, port_JsonGet(json, view, "byteLength"), 0);
    if (view < 0 || (u64)offset + length > g->bin_size) {
        return "rb_palette: its picture is outside the file";
    }
    return port_FighterPngDecode(g->bin + offset, length, &out->index.w,
                                 &out->index.h, &out->index.rgba);
}

static const char *port_FighterFileOf(const Gltf *g, PortFighterFile *out) {
    const Json *json = g->json;
    s32 extras = port_JsonGet(json, 0, "extras");
    s32 skin = port_JsonItem(json, port_JsonGet(json, 0, "skins"), 0);
    s32 joints = port_JsonGet(json, skin, "joints");
    s32 nodes = port_JsonGet(json, 0, "nodes");
    s32 mesh = port_JsonItem(json, port_JsonGet(json, 0, "meshes"), 0);
    s32 primitives = port_JsonGet(json, mesh, "primitives");
    s32 joint_bone[256];
    float *ibm;
    s32 joint_count;
    s32 total_vertices = 0;
    s32 total_indices = 0;
    const char *problem = NULL;
    s32 p;
    s32 i;

    if (port_JsonInt(json, port_JsonGet(json, extras, "rb_fighter"), 0) != 1) {
        return "not a fighter's model (no extras.rb_fighter: "
               "tools/fighter_conform.py)";
    }
    if (skin < 0 || joints < 0 || primitives < 0) {
        return "no skin or no mesh";
    }
    joint_count = json->token[joints].size;
    if (joint_count <= 0 || joint_count > 256) {
        return "a skin of no joints, or too many";
    }
    for (i = 0; i < joint_count; i++) {
        s32 node = port_JsonItem(
            json, nodes,
            port_JsonInt(json, port_JsonItem(json, joints, i), -1));

        joint_bone[i] = port_GltfBoneOf(json, node);
    }
    ibm = malloc((size_t)joint_count * 16 * sizeof(float));
    if (ibm == NULL) {
        return "no memory";
    }
    if (port_GltfRead(
            g,
            port_JsonInt(
                json, port_JsonGet(json, skin, "inverseBindMatrices"), -1),
            ibm, joint_count, 16) != joint_count) {
        free(ibm);
        return "the skin has no inverse bind matrices";
    }
    for (i = 0; i < joint_count; i++) {
        s32 bone = joint_bone[i];

        if (bone >= 0 && bone < PORT_FIGHTER_FILE_JOINTS) {
            memcpy(out->inverse_bind[bone], ibm + i * 16, 16 * sizeof(float));
            out->has_joint[bone] = TRUE;
        }
    }
    free(ibm);
    for (p = 0; p < json->token[primitives].size; p++) {
        s32 prim = port_JsonItem(json, primitives, p);
        s32 attributes = port_JsonGet(json, prim, "attributes");

        total_vertices += port_GltfCount(
            g,
            port_JsonInt(json, port_JsonGet(json, attributes, "POSITION"), -1));
        total_indices += port_GltfCount(
            g, port_JsonInt(json, port_JsonGet(json, prim, "indices"), -1));
    }
    if (total_vertices <= 0 || total_indices <= 0) {
        return "a primitive with no vertices or no indices";
    }
    out->vertices = calloc((size_t)total_vertices, sizeof(*out->vertices));
    out->indices = calloc((size_t)total_indices, sizeof(*out->indices));
    if (out->vertices == NULL || out->indices == NULL) {
        return "no memory";
    }
    for (p = 0; p < json->token[primitives].size && p < PORT_FIGHTER_FILE_PARTS;
         p++) {
        s32 prim = port_JsonItem(json, primitives, p);
        s32 attributes = port_JsonGet(json, prim, "attributes");
        s32 first_vertex = out->vertex_count;
        s32 count = port_GltfCount(
            g,
            port_JsonInt(json, port_JsonGet(json, attributes, "POSITION"), -1));
        s32 mode = port_JsonInt(json, port_JsonGet(json, prim, "mode"), 4);
        float *scratch;
        u32 *whole;
        s32 n;
        s32 k;

        if (mode != 4) {
            return "a primitive that is not triangles";
        }
        scratch = malloc((size_t)count * 4 * sizeof(float));
        whole = malloc((size_t)count * 4 * sizeof(u32));
        if (scratch == NULL || whole == NULL) {
            free(scratch);
            free(whole);
            return "no memory";
        }
#define ATTRIBUTE(name)                                                        \
    port_JsonInt(json, port_JsonGet(json, attributes, name), -1)
        if (port_GltfRead(g, ATTRIBUTE("POSITION"), scratch, count, 3) !=
            count) {
            problem = "no positions";
        }
        for (k = 0; k < count && problem == NULL; k++) {
            memcpy(out->vertices[first_vertex + k].place, scratch + k * 3,
                   3 * sizeof(float));
        }
        if (problem == NULL && ATTRIBUTE("NORMAL") >= 0) {
            if (port_GltfRead(g, ATTRIBUTE("NORMAL"), scratch, count, 3) !=
                count) {
                problem = "the normals do not read";
            }
            for (k = 0; k < count && problem == NULL; k++) {
                memcpy(out->vertices[first_vertex + k].normal, scratch + k * 3,
                       3 * sizeof(float));
            }
        }
        if (problem == NULL && ATTRIBUTE("TEXCOORD_0") >= 0) {
            if (port_GltfRead(g, ATTRIBUTE("TEXCOORD_0"), scratch, count, 2) !=
                count) {
                problem = "the texture points do not read";
            }
            for (k = 0; k < count && problem == NULL; k++) {
                memcpy(out->vertices[first_vertex + k].uv, scratch + k * 2,
                       2 * sizeof(float));
            }
        }
        if (problem == NULL &&
            (port_GltfRead(g, ATTRIBUTE("WEIGHTS_0"), scratch, count, 4) !=
                 count ||
             port_GltfReadIndices(g, ATTRIBUTE("JOINTS_0"), whole, count * 4) <
                 0)) {
            problem = "a primitive is not skinned (JOINTS_0, WEIGHTS_0)";
        }
        /* JOINTS_0 is read as one number an element: read again as four */
        if (problem == NULL) {
            s32 accessor =
                port_JsonItem(json, port_JsonGet(json, 0, "accessors"),
                              ATTRIBUTE("JOINTS_0"));
            s32 joints_kind = port_JsonInt(
                json, port_JsonGet(json, accessor, "componentType"), 0);
            s32 view = port_JsonItem(
                json, port_JsonGet(json, 0, "bufferViews"),
                port_JsonInt(
                    json, port_JsonGet(json, accessor, "bufferView"), -1));
            s32 size = port_GltfSize(joints_kind);
            s32 stride =
                port_JsonInt(json, port_JsonGet(json, view, "byteStride"), 0);
            u32 offset =
                (u32)port_JsonInt(
                    json, port_JsonGet(json, view, "byteOffset"), 0) +
                (u32)port_JsonInt(
                    json, port_JsonGet(json, accessor, "byteOffset"), 0);

            if (stride == 0) {
                stride = size * 4;
            }
            if ((size != 1 && size != 2) ||
                (u64)offset + (u64)stride * (u64)count > g->bin_size) {
                problem = "JOINTS_0 is not of bytes or halfwords";
            }
            for (k = 0; k < count && problem == NULL; k++) {
                s32 j;
                float sum = 0.0f;

                for (j = 0; j < 4; j++) {
                    const u8 *at =
                        g->bin + offset + (u32)(k * stride + j * size);
                    s32 joint = size == 1 ? at[0] : (at[0] | (at[1] << 8));
                    s32 bone = joint < joint_count ? joint_bone[joint] : -1;
                    PortFighterVertex *v = &out->vertices[first_vertex + k];

                    v->weight[j] = scratch[k * 4 + j];
                    if (bone < 0 || bone >= PORT_FIGHTER_FILE_JOINTS) {
                        bone = 0;
                        v->weight[j] = 0.0f;
                    }
                    v->joint[j] = (u8)bone;
                    sum += v->weight[j];
                }
                if (sum > 0.0f) {
                    for (j = 0; j < 4; j++) {
                        out->vertices[first_vertex + k].weight[j] /= sum;
                    }
                }
            }
        }
        if (problem == NULL) {
            problem = port_GltfTerms(g, attributes, joint_bone, joint_count,
                                     out, first_vertex, count);
        }
#undef ATTRIBUTE
        free(scratch);
        free(whole);
        if (problem != NULL) {
            return problem;
        }
        out->vertex_count += count;
        n = port_GltfReadIndices(
            g, port_JsonInt(json, port_JsonGet(json, prim, "indices"), -1),
            out->indices + out->index_count, total_indices - out->index_count);
        if (n < 0) {
            return "the indices do not read";
        }
        for (k = 0; k < n; k++) {
            if (out->indices[out->index_count + k] >= (u32)count) {
                return "an index past the vertices";
            }
            out->indices[out->index_count + k] += (u32)first_vertex;
        }
        out->part[out->part_count].first = out->index_count;
        out->part[out->part_count].count = n;
        out->part[out->part_count].texture = port_GltfTexture(
            g,
            port_JsonItem(
                json, port_JsonGet(json, 0, "materials"),
                port_JsonInt(json, port_JsonGet(json, prim, "material"), -1)),
            out, &problem);
        if (problem != NULL) {
            return problem;
        }
        port_GltfMaps(
            g,
            port_JsonItem(
                json, port_JsonGet(json, 0, "materials"),
                port_JsonInt(json, port_JsonGet(json, prim, "material"), -1)),
            &out->part[out->part_count], out, &problem);
        if (problem != NULL) {
            return problem;
        }
        out->part_count++;
        out->index_count += n;
    }
    return port_GltfPalette(g, extras, out);
}

const char *port_FighterFileParse(
    const u8 *bytes, u32 size, PortFighterFile *out) {
    Json json;
    Gltf g;
    u32 json_length;
    u32 bin_at;
    const char *problem;
    s32 at = 0;

    memset(out, 0, sizeof(*out));
    if (size < 20 || port_Little32(bytes) != GLB_MAGIC ||
        port_Little32(bytes + 4) != 2) {
        return "not a glTF binary file of version 2";
    }
    json_length = port_Little32(bytes + 12);
    if (port_Little32(bytes + 16) != GLB_JSON || 20 + json_length > size) {
        return "a glTF binary file without its JSON";
    }
    memset(&json, 0, sizeof(json));
    json.text = (const char *)bytes + 20;
    json.length = (s32)json_length;
    if (port_JsonValue(&json, &at, 0) != 0 ||
        json.token[0].kind != JSON_OBJECT) {
        free(json.token);
        return "the file's JSON does not read";
    }
    bin_at = 20 + ((json_length + 3) & ~3U);
    g.json = &json;
    g.bin = NULL;
    g.bin_size = 0;
    if (bin_at + 8 <= size && port_Little32(bytes + bin_at + 4) == GLB_BIN) {
        g.bin = bytes + bin_at + 8;
        g.bin_size = port_Little32(bytes + bin_at);
        if (bin_at + 8 + g.bin_size > size) {
            g.bin_size = size - bin_at - 8;
        }
    }
    if (g.bin == NULL) {
        free(json.token);
        return "a glTF binary file without its binary part";
    }
    problem = port_FighterFileOf(&g, out);
    free(json.token);
    if (problem != NULL) {
        port_FighterFileFree(out);
    }
    return problem;
}

const char *port_FighterFileRead(const char *path, PortFighterFile *out) {
    FILE *f = fopen(path, "rb");
    u8 *bytes;
    long size;
    const char *problem;

    memset(out, 0, sizeof(*out));
    if (f == NULL) {
        return "no such file";
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 256L * 1024 * 1024) {
        fclose(f);
        return "the file is empty or too large";
    }
    bytes = malloc((size_t)size);
    if (bytes == NULL || fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return "the file does not read";
    }
    fclose(f);
    problem = port_FighterFileParse(bytes, (u32)size, out);
    free(bytes);
    return problem;
}
