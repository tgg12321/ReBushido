/* The native port's zlib streams written (port_deflate.h): LZ77 by a
 * hash of three bytes and a chain of earlier places in the 32 KB window,
 * then each block's own codes (dynamic Huffman), or the block stored
 * where that is shorter. One stream at a time: its tables are this
 * file's. */
#include "common.h"
#include "port_deflate.h"
#include "bb2.h"

#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define WINDOW_BYTES 32768
#define WINDOW_MASK (WINDOW_BYTES - 1)
#define HASH_BITS 16
#define HASH_SIZE (1 << HASH_BITS)
#define HASH_MULTIPLIER 2654435761U
#define WORD_BITS 32
#define MATCH_MIN 3
#define MATCH_MAX 258
/* how far down a chain a match is looked for, and a match long enough to
 * stop looking */
#define CHAIN_MAX 48
#define GOOD_ENOUGH 128
/* a block's symbols: a literal, or a match (MATCH_BIT, the length above
 * LENGTH_SHIFT, the distance below) */
#define BLOCK_SYMBOLS 16384
/* the distances whose codes are found by themselves, and the others by
 * their 128ths */
#define DISTANCE_NEAR 256
#define DISTANCE_FAR_SHIFT 7
#define MATCH_BIT 0x80000000U
#define LENGTH_SHIFT 16
#define DISTANCE_MASK 0xFFFF
/* the alphabets */
#define LITERALS 286
#define END_OF_BLOCK 256
#define FIRST_LENGTH_CODE 257
#define LENGTH_CODES 29
#define DISTANCES 30
#define CODE_LENGTHS 19
#define CODE_BITS_MAX 15
#define CODE_LENGTH_BITS_MAX 7
#define LIT_COUNT_MIN 257
#define DIST_COUNT_MIN 1
#define CL_COUNT_MIN 4
/* the code lengths' own codes: a length again 3 to 6 times, zeros 3 to
 * 10 times, zeros 11 to 138 times */
#define CL_REPEAT 16
#define CL_ZEROS 17
#define CL_ZEROS_LONG 18
#define REPEAT_MIN 3
#define REPEAT_MAX 6
#define ZEROS_MAX 10
#define ZEROS_LONG_MIN 11
#define ZEROS_LONG_MAX 138
#define REPEAT_EXTRA 2
#define ZEROS_EXTRA 3
#define ZEROS_LONG_EXTRA 7
#define HLIT_BITS 5
#define HDIST_BITS 5
#define HCLEN_BITS 4
#define CL_LENGTH_BITS 3
/* a block's head: whether it is the last, and its kind */
#define BLOCK_STORED 0
#define BLOCK_DYNAMIC 2
#define BLOCK_HEAD_BITS 3
#define STORED_MAX 65535
#define STORED_HEAD_BYTES 4
/* zlib's head (deflate, a 32 KB window, no dictionary) and its sum */
#define ZLIB_CMF 0x78
#define ZLIB_FLAGS_BYTE 0x01
#define ADLER_BASE 65521U
#define ADLER_RUN 5552
#define TRAILER_BYTES 4
#define HALF_BITS 16

static const u16 g_length_base[LENGTH_CODES] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 g_length_extra[LENGTH_CODES] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 g_distance_base[DISTANCES] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 g_distance_extra[DISTANCES] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const u8 g_code_length_order[CODE_LENGTHS] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* The one stream's tables: the newest place of each hash (one past it,
 * 0 for none), the place before each in the window, and the block. */
static u32 g_head[HASH_SIZE];
static u32 g_prev[WINDOW_BYTES];
static u32 g_symbols[BLOCK_SYMBOLS];
static u32 g_symbol_count;
/* the codes of each length and distance (port_DeflateTables) */
static u8 g_length_codes[MATCH_MAX + 1];
static u8
    g_distance_codes[DISTANCE_NEAR + (WINDOW_BYTES >> DISTANCE_FAR_SHIFT)];
static s32 g_tables_made;
static u32 g_block_start;
static u32 g_hashed_to;

u32 port_DeflateBound(u32 size) {
    return size + (size / STORED_MAX + 1) * (STORED_HEAD_BYTES + 1) + 2 +
           TRAILER_BYTES + BITS_PER_BYTE;
}

/* ---- bits out ---- */

static void port_DeflateByte(PortDeflate *z, u32 byte) {
    if (z->out_at >= z->out_room) {
        z->failed = TRUE;
        return;
    }
    z->out[z->out_at++] = (u8)byte;
}

/* `count` bits of `value`, the lowest first. */
static void port_DeflateBits(PortDeflate *z, u32 value, s32 count) {
    z->bits |= value << z->bit_count;
    z->bit_count += count;
    while (z->bit_count >= BITS_PER_BYTE) {
        port_DeflateByte(z, z->bits & BYTE_MASK);
        z->bits >>= BITS_PER_BYTE;
        z->bit_count -= BITS_PER_BYTE;
    }
}

static void port_DeflateAlign(PortDeflate *z) {
    if (z->bit_count > 0) {
        port_DeflateBits(z, 0, BITS_PER_BYTE - z->bit_count);
    }
}

/* ---- codes ---- */

/* Code lengths for `count` symbols of these frequencies, none longer
 * than `most` bits: Huffman's, then the longest cut to `most` and the
 * others lengthened until the code is whole again (as miniz does). A
 * symbol never seen has no code; where only one is, it has one bit. */
static void port_DeflateLengths(
    const u32 *freqs, s32 count, s32 most, u8 *lengths) {
    static u32 weight[2 * LITERALS];
    static s32 parent[2 * LITERALS];
    static s32 order[LITERALS];
    s32 counts[WORD_BITS + 1];
    s32 used = 0;
    s32 leaf;
    s32 node;
    s32 next_node;
    s32 i;
    s32 len;
    u32 total;

    for (i = 0; i < count; i++) {
        lengths[i] = 0;
        if (freqs[i] != 0) {
            order[used++] = i;
        }
    }
    if (used == 0) {
        return;
    }
    if (used == 1) {
        lengths[order[0]] = 1;
        return;
    }
    /* the used symbols, least frequent first (an insertion sort: there are
     * at most 286) */
    for (i = 1; i < used; i++) {
        s32 symbol = order[i];
        s32 at = i;

        while (at > 0 && freqs[order[at - 1]] > freqs[symbol]) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = symbol;
    }
    /* Huffman's tree by two queues: the leaves in that order, and the
     * nodes as they are made, which come out in order too */
    for (i = 0; i < used; i++) {
        weight[i] = freqs[order[i]];
    }
    leaf = 0;
    node = used;
    next_node = used;
    while (next_node < 2 * used - 1) {
        s32 pick[2] = {0, 0};
        s32 k;

        for (k = 0; k < 2; k++) {
            if (leaf < used &&
                (node >= next_node || weight[leaf] <= weight[node])) {
                pick[k] = leaf++;
            } else {
                pick[k] = node++;
            }
        }
        weight[next_node] = weight[pick[0]] + weight[pick[1]];
        parent[pick[0]] = next_node;
        parent[pick[1]] = next_node;
        next_node++;
    }
    /* each leaf's depth, from the root down */
    for (i = 0; i <= WORD_BITS; i++) {
        counts[i] = 0;
    }
    parent[2 * used - 2] = -1;
    weight[2 * used - 2] = 0;
    for (i = 2 * used - 3; i >= 0; i--) {
        weight[i] = weight[parent[i]] + 1;
    }
    for (i = 0; i < used; i++) {
        len = (s32)weight[i];
        counts[len > WORD_BITS ? WORD_BITS : len]++;
    }
    /* none longer than `most` */
    for (len = most + 1; len <= WORD_BITS; len++) {
        counts[most] += counts[len];
        counts[len] = 0;
    }
    total = 0;
    for (len = most; len > 0; len--) {
        total += (u32)counts[len] << (most - len);
    }
    while (total != 1U << most) {
        counts[most]--;
        for (len = most - 1; len > 0; len--) {
            if (counts[len] != 0) {
                counts[len]--;
                counts[len + 1] += 2;
                break;
            }
        }
        total--;
    }
    /* the shortest codes to the most frequent */
    i = used - 1;
    for (len = 1; len <= most; len++) {
        s32 k;

        for (k = counts[len]; k > 0; k--) {
            lengths[order[i--]] = (u8)len;
        }
    }
}

/* The canonical codes of those lengths, each with its bits reversed, as
 * the stream takes a code's first bit first. */
static void port_DeflateCodes(const u8 *lengths, s32 count, u16 *codes) {
    s32 counts[CODE_BITS_MAX + 1];
    u32 next[CODE_BITS_MAX + 1];
    u32 code = 0;
    s32 i;

    for (i = 0; i <= CODE_BITS_MAX; i++) {
        counts[i] = 0;
    }
    for (i = 0; i < count; i++) {
        counts[lengths[i]]++;
    }
    counts[0] = 0;
    for (i = 1; i <= CODE_BITS_MAX; i++) {
        code = (code + (u32)counts[i - 1]) << 1;
        next[i] = code;
    }
    for (i = 0; i < count; i++) {
        s32 len = lengths[i];
        u32 forward;
        u32 reversed = 0;
        s32 bit;

        if (len == 0) {
            codes[i] = 0;
            continue;
        }
        forward = next[len]++;
        for (bit = 0; bit < len; bit++) {
            reversed = (reversed << 1) | ((forward >> bit) & 1);
        }
        codes[i] = (u16)reversed;
    }
}

/* Each length's code, and each distance's by itself up to 256 and by
 * its 128ths above (as zlib's tables are), worked out once. */
static void port_DeflateTables(void) {
    s32 code = 0;
    u32 value;

    if (g_tables_made) {
        return;
    }
    for (value = MATCH_MIN; value <= MATCH_MAX; value++) {
        while (code + 1 < LENGTH_CODES && g_length_base[code + 1] <= value) {
            code++;
        }
        g_length_codes[value] = (u8)code;
    }
    code = 0;
    for (value = 1; value <= WINDOW_BYTES; value++) {
        while (code + 1 < DISTANCES && g_distance_base[code + 1] <= value) {
            code++;
        }
        if (value <= DISTANCE_NEAR) {
            g_distance_codes[value - 1] = (u8)code;
        } else {
            g_distance_codes[DISTANCE_NEAR +
                             ((value - 1) >> DISTANCE_FAR_SHIFT)] = (u8)code;
        }
    }
    g_tables_made = TRUE;
}

static s32 port_LengthCode(u32 length) { return g_length_codes[length]; }

static s32 port_DistanceCode(u32 distance) {
    return distance <= DISTANCE_NEAR
               ? g_distance_codes[distance - 1]
               : g_distance_codes[DISTANCE_NEAR +
                                  ((distance - 1) >> DISTANCE_FAR_SHIFT)];
}

/* ---- a block ---- */

/* The code lengths of both alphabets, run-length coded: the symbols of
 * the code lengths' code (with their extra bits above LENGTH_SHIFT), and
 * their count. */
static s32 port_DeflateRuns(const u8 *all, s32 count, u32 *runs) {
    s32 out = 0;
    s32 i = 0;

    while (i < count) {
        s32 run = 1;

        while (i + run < count && all[i + run] == all[i]) {
            run++;
        }
        if (all[i] == 0 && run >= REPEAT_MIN) {
            s32 left = run;

            while (left >= REPEAT_MIN) {
                if (left >= ZEROS_LONG_MIN) {
                    s32 take = left > ZEROS_LONG_MAX ? ZEROS_LONG_MAX : left;

                    runs[out++] =
                        CL_ZEROS_LONG |
                        ((u32)(take - ZEROS_LONG_MIN) << LENGTH_SHIFT);
                    left -= take;
                } else {
                    s32 take = left > ZEROS_MAX ? ZEROS_MAX : left;

                    runs[out++] =
                        CL_ZEROS | ((u32)(take - REPEAT_MIN) << LENGTH_SHIFT);
                    left -= take;
                }
            }
            while (left-- > 0) {
                runs[out++] = 0;
            }
        } else if (run > REPEAT_MIN) {
            s32 left = run - 1;

            runs[out++] = all[i];
            while (left >= REPEAT_MIN) {
                s32 take = left > REPEAT_MAX ? REPEAT_MAX : left;

                runs[out++] =
                    CL_REPEAT | ((u32)(take - REPEAT_MIN) << LENGTH_SHIFT);
                left -= take;
            }
            while (left-- > 0) {
                runs[out++] = all[i];
            }
        } else {
            s32 k;

            for (k = 0; k < run; k++) {
                runs[out++] = all[i];
            }
        }
        i += run;
    }
    return out;
}

static s32 port_RunExtraBits(u32 symbol) {
    return symbol == CL_REPEAT       ? REPEAT_EXTRA
           : symbol == CL_ZEROS      ? ZEROS_EXTRA
           : symbol == CL_ZEROS_LONG ? ZEROS_LONG_EXTRA
                                     : 0;
}

/* The bytes [g_block_start, end) stored, in pieces of at most 64 KB. */
static void port_DeflateStored(PortDeflate *z, u32 end, s32 is_last) {
    u32 at = g_block_start;

    do {
        u32 size = end - at > STORED_MAX ? STORED_MAX : end - at;
        s32 last_piece = at + size == end;
        u32 i;

        port_DeflateBits(z, (u32)(is_last && last_piece) | (BLOCK_STORED << 1),
                         BLOCK_HEAD_BITS);
        port_DeflateAlign(z);
        port_DeflateByte(z, size & BYTE_MASK);
        port_DeflateByte(z, size >> BITS_PER_BYTE);
        port_DeflateByte(z, ~size & BYTE_MASK);
        port_DeflateByte(z, (~size >> BITS_PER_BYTE) & BYTE_MASK);
        for (i = 0; i < size; i++) {
            port_DeflateByte(z, z->in[at + i]);
        }
        at += size;
    } while (at < end);
}

/* The block's symbols out, with codes of their own or stored, whichever
 * is shorter; the block then ends at `end`. */
static void port_DeflateFlush(PortDeflate *z, u32 end, s32 is_last) {
    static u32 lit_freqs[LITERALS];
    static u32 dist_freqs[DISTANCES];
    static u8 lengths[LITERALS + DISTANCES];
    static u16 lit_codes[LITERALS];
    static u16 dist_codes[DISTANCES];
    static u32 runs[LITERALS + DISTANCES];
    u32 cl_freqs[CODE_LENGTHS];
    u8 cl_lengths[CODE_LENGTHS];
    u16 cl_codes[CODE_LENGTHS];
    s32 lit_count = LIT_COUNT_MIN;
    s32 dist_count = DIST_COUNT_MIN;
    s32 cl_count = CL_COUNT_MIN;
    s32 run_count;
    u32 bits;
    u32 stored_bits;
    u32 i;

    for (i = 0; i < LITERALS; i++) {
        lit_freqs[i] = 0;
    }
    for (i = 0; i < DISTANCES; i++) {
        dist_freqs[i] = 0;
    }
    for (i = 0; i < g_symbol_count; i++) {
        u32 symbol = g_symbols[i];

        if (symbol & MATCH_BIT) {
            lit_freqs[FIRST_LENGTH_CODE +
                      port_LengthCode((symbol & ~MATCH_BIT) >> LENGTH_SHIFT)]++;
            dist_freqs[port_DistanceCode(symbol & DISTANCE_MASK)]++;
        } else {
            lit_freqs[symbol]++;
        }
    }
    lit_freqs[END_OF_BLOCK] = 1;
    port_DeflateLengths(lit_freqs, LITERALS, CODE_BITS_MAX, lengths);
    port_DeflateLengths(
        dist_freqs, DISTANCES, CODE_BITS_MAX, lengths + LITERALS);
    for (i = 0; i < LITERALS; i++) {
        if (lengths[i] != 0) {
            lit_count = (s32)i + 1 > lit_count ? (s32)i + 1 : lit_count;
        }
    }
    for (i = 0; i < DISTANCES; i++) {
        if (lengths[LITERALS + i] != 0) {
            dist_count = (s32)i + 1;
        }
    }
    if (lengths[LITERALS] == 0 && dist_count == DIST_COUNT_MIN) {
        /* no match: one distance code all the same, as zlib writes */
        lengths[LITERALS] = 1;
    }
    port_DeflateCodes(lengths, LITERALS, lit_codes);
    port_DeflateCodes(lengths + LITERALS, DISTANCES, dist_codes);
    /* both alphabets' lengths, one after the other, run-length coded */
    for (i = 0; i < (u32)dist_count; i++) {
        lengths[lit_count + (s32)i] = lengths[LITERALS + i];
    }
    run_count = port_DeflateRuns(lengths, lit_count + dist_count, runs);
    for (i = 0; i < CODE_LENGTHS; i++) {
        cl_freqs[i] = 0;
    }
    for (i = 0; i < (u32)run_count; i++) {
        cl_freqs[runs[i] & DISTANCE_MASK]++;
    }
    port_DeflateLengths(
        cl_freqs, CODE_LENGTHS, CODE_LENGTH_BITS_MAX, cl_lengths);
    port_DeflateCodes(cl_lengths, CODE_LENGTHS, cl_codes);
    for (i = 0; i < CODE_LENGTHS; i++) {
        if (cl_lengths[g_code_length_order[i]] != 0) {
            cl_count = (s32)i + 1 > cl_count ? (s32)i + 1 : cl_count;
        }
    }
    /* what the block costs with its codes, and stored */
    bits = BLOCK_HEAD_BITS + HLIT_BITS + HDIST_BITS + HCLEN_BITS +
           (u32)cl_count * CL_LENGTH_BITS;
    for (i = 0; i < (u32)run_count; i++) {
        u32 symbol = runs[i] & DISTANCE_MASK;

        bits += cl_lengths[symbol] + (u32)port_RunExtraBits(symbol);
    }
    for (i = 0; i < LITERALS; i++) {
        bits += lit_freqs[i] * lengths[i];
    }
    for (i = 0; i < g_symbol_count; i++) {
        u32 symbol = g_symbols[i];

        if (symbol & MATCH_BIT) {
            s32 lc = port_LengthCode((symbol & ~MATCH_BIT) >> LENGTH_SHIFT);
            s32 dc = port_DistanceCode(symbol & DISTANCE_MASK);

            bits += g_length_extra[lc] + lengths[lit_count + dc] +
                    g_distance_extra[dc];
        }
    }
    stored_bits =
        (end - g_block_start +
         ((end - g_block_start) / STORED_MAX + 1) * (STORED_HEAD_BYTES + 1)) *
        BITS_PER_BYTE;
    if (stored_bits < bits) {
        port_DeflateStored(z, end, is_last);
        g_symbol_count = 0;
        g_block_start = end;
        return;
    }
    port_DeflateBits(z, (u32)is_last | (BLOCK_DYNAMIC << 1), BLOCK_HEAD_BITS);
    port_DeflateBits(z, (u32)(lit_count - LIT_COUNT_MIN), HLIT_BITS);
    port_DeflateBits(z, (u32)(dist_count - DIST_COUNT_MIN), HDIST_BITS);
    port_DeflateBits(z, (u32)(cl_count - CL_COUNT_MIN), HCLEN_BITS);
    for (i = 0; i < (u32)cl_count; i++) {
        port_DeflateBits(z, cl_lengths[g_code_length_order[i]], CL_LENGTH_BITS);
    }
    for (i = 0; i < (u32)run_count; i++) {
        u32 symbol = runs[i] & DISTANCE_MASK;

        port_DeflateBits(z, cl_codes[symbol], cl_lengths[symbol]);
        if (port_RunExtraBits(symbol) != 0) {
            port_DeflateBits(
                z, runs[i] >> LENGTH_SHIFT, port_RunExtraBits(symbol));
        }
    }
    for (i = 0; i < g_symbol_count; i++) {
        u32 symbol = g_symbols[i];

        if (symbol & MATCH_BIT) {
            u32 length = (symbol & ~MATCH_BIT) >> LENGTH_SHIFT;
            u32 distance = symbol & DISTANCE_MASK;
            s32 lc = port_LengthCode(length);
            s32 dc = port_DistanceCode(distance);

            port_DeflateBits(z, lit_codes[FIRST_LENGTH_CODE + lc],
                             lengths[FIRST_LENGTH_CODE + lc]);
            port_DeflateBits(z, length - g_length_base[lc], g_length_extra[lc]);
            port_DeflateBits(z, dist_codes[dc], lengths[lit_count + dc]);
            port_DeflateBits(
                z, distance - g_distance_base[dc], g_distance_extra[dc]);
        } else {
            port_DeflateBits(z, lit_codes[symbol], lengths[symbol]);
        }
    }
    port_DeflateBits(z, lit_codes[END_OF_BLOCK], lengths[END_OF_BLOCK]);
    g_symbol_count = 0;
    g_block_start = end;
}

/* ---- matches ---- */

static u32 port_DeflateHash(const u8 *at) {
    u32 word = (u32)at[0] | ((u32)at[1] << BITS_PER_BYTE) |
               ((u32)at[2] << (2 * BITS_PER_BYTE));

    return (word * HASH_MULTIPLIER) >> (WORD_BITS - HASH_BITS);
}

/* The place `at` into the chains. */
static void port_DeflateInsert(PortDeflate *z, u32 at) {
    u32 hash = port_DeflateHash(z->in + at);

    g_prev[at & WINDOW_MASK] = g_head[hash];
    g_head[hash] = at + 1;
}

/* The longest match for the bytes at `at` among the window's: its
 * length (0 for none shorter than MATCH_MIN) and its distance. */
static u32 port_DeflateMatch(PortDeflate *z, u32 at, u32 *distance) {
    const u8 *here = z->in + at;
    u32 most = z->in_size - at > MATCH_MAX ? MATCH_MAX : z->in_size - at;
    u32 best = 0;
    u32 candidate = g_head[port_DeflateHash(here)];
    s32 chain = CHAIN_MAX;

    while (candidate != 0 && chain-- > 0) {
        u32 from = candidate - 1;
        const u8 *there;
        u32 length = 0;

        if (at - from > WINDOW_BYTES - 1 || from >= at) {
            break;
        }
        there = z->in + from;
        if (there[best] == here[best]) {
            while (length < most && there[length] == here[length]) {
                length++;
            }
            if (length > best) {
                best = length;
                *distance = at - from;
                if (best >= GOOD_ENOUGH || best == most) {
                    break;
                }
            }
        }
        candidate = g_prev[from & WINDOW_MASK];
    }
    return best >= MATCH_MIN ? best : 0;
}

/* ---- the stream ---- */

static void port_DeflateSum(PortDeflate *z, u32 from, u32 to) {
    while (from < to) {
        u32 run = to - from > ADLER_RUN ? ADLER_RUN : to - from;
        u32 i;

        for (i = 0; i < run; i++) {
            z->adler_a += z->in[from + i];
            z->adler_b += z->adler_a;
        }
        z->adler_a %= ADLER_BASE;
        z->adler_b %= ADLER_BASE;
        from += run;
    }
}

/* The stream begun: its head, and the window's chains emptied. */
void port_DeflateBegin(
    PortDeflate *z, const void *in, u32 in_size, void *out, u32 out_room) {
    u32 i;

    z->in = in;
    z->in_size = in_size;
    z->in_at = 0;
    z->out = out;
    z->out_room = out_room;
    z->out_at = 0;
    z->bits = 0;
    z->bit_count = 0;
    z->adler_a = 1;
    z->adler_b = 0;
    z->failed = FALSE;
    for (i = 0; i < HASH_SIZE; i++) {
        g_head[i] = 0;
    }
    port_DeflateTables();
    g_symbol_count = 0;
    g_block_start = 0;
    g_hashed_to = 0;
    port_DeflateByte(z, ZLIB_CMF);
    port_DeflateByte(z, ZLIB_FLAGS_BYTE);
}

/* The next of the input matched and put into blocks; at its end the
 * last block, and the sum (port_deflate.h). */
s32 port_DeflateSome(PortDeflate *z, u32 budget) {
    u32 start = z->in_at;
    u32 stop = z->in_size - start > budget ? start + budget : z->in_size;
    u32 at = start;

    while (at < stop) {
        u32 distance = 0;
        u32 length = 0;
        u32 end;

        if (z->in_size - at >= MATCH_MIN) {
            length = port_DeflateMatch(z, at, &distance);
        }
        end = at + (length != 0 ? length : 1);
        if (length != 0) {
            g_symbols[g_symbol_count++] =
                MATCH_BIT | (length << LENGTH_SHIFT) | distance;
        } else {
            g_symbols[g_symbol_count++] = z->in[at];
        }
        /* every place passed into the chains */
        while (at < end) {
            if (z->in_size - at >= MATCH_MIN) {
                port_DeflateInsert(z, at);
            }
            at++;
        }
        if (g_symbol_count == BLOCK_SYMBOLS) {
            port_DeflateFlush(z, at, FALSE);
        }
    }
    port_DeflateSum(z, start, at);
    z->in_at = at;
    if (at < z->in_size) {
        return FALSE;
    }
    port_DeflateFlush(z, at, TRUE);
    port_DeflateAlign(z);
    port_DeflateByte(z, z->adler_b >> BITS_PER_BYTE);
    port_DeflateByte(z, z->adler_b & BYTE_MASK);
    port_DeflateByte(z, z->adler_a >> BITS_PER_BYTE);
    port_DeflateByte(z, z->adler_a & BYTE_MASK);
    return TRUE;
}
