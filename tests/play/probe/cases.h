#ifndef PROBE_CASES_H
#define PROBE_CASES_H

/* The cases the port's GTE is held to the emulator's by
 * (tests/play/README.md, "The GTE probe"). Two programs include this file
 * and so make the same cases: the probe, a PlayStation program that runs
 * each case on the emulator's coprocessor, and the host test
 * tests/port/gte_vectors.c, which runs it on the model in C
 * (src/port/gte.c). A case is an operation's instruction word and the 64
 * registers as they are loaded before it; what is compared is the 64
 * registers as they read back after it.
 *
 * Every number here is made up: by a generator, from the case's place in
 * the list. Nothing is the game's. Nothing here divides or uses 64 bits,
 * so that both compilers make the same of it. */

/* How many cases an operation gets. */
#define PROBE_VECTORS 2000

/* The registers of a case: 32 control registers, then 32 data registers,
 * when loaded; the data registers first when read back. */
#define PROBE_REGISTERS 64
#define PROBE_CONTROL 0
#define PROBE_DATA 32

/* The operations: every instruction word the game has (the first 24),
 * then the operations and the variants it does not use. 0 is no operation
 * at all: the registers are loaded and read back. */
#define PROBE_COMMANDS(X)                                                      \
    X(0x00000000) /* the registers alone */                                    \
    X(0x4A180001) /* RTPS */                                                   \
    X(0x4A280030) /* RTPT */                                                   \
    X(0x4B400006) /* NCLIP */                                                  \
    X(0x4B58002D) /* AVSZ3 */                                                  \
    X(0x4B68002E) /* AVSZ4 */                                                  \
    X(0x4A480012) /* MVMVA: rotation * V0 + translation */                     \
    X(0x4A486012) /* MVMVA: rotation * V0 */                                   \
    X(0x4A41E012) /* MVMVA: rotation * IR, the fraction kept */                \
    X(0x4A49E012) /* MVMVA: rotation * IR */                                   \
    X(0x4A4DA412) /* MVMVA: light colour * IR + background, from 0 */          \
    X(0x4AC8041E) /* NCS */                                                    \
    X(0x4AD80420) /* NCT */                                                    \
    X(0x4B08041B) /* NCCS */                                                   \
    X(0x4B98003D) /* GPF */                                                    \
    X(0x4B90003D) /* GPF, the fraction kept */                                 \
    X(0x4BA8003E) /* GPL */                                                    \
    X(0x4BA0003E) /* GPL, the fraction kept */                                 \
    X(0x4AA00428) /* SQR, the fraction kept */                                 \
    X(0x4AA80428) /* SQR */                                                    \
    X(0x4B78000C) /* OP */                                                     \
    X(0x4B70000C) /* OP, the fraction kept */                                  \
    X(0x4AF8002A) /* DPCT */                                                   \
    X(0x4A680029) /* DCPL */                                                   \
    X(0x4A980011) /* INTPL */                                                  \
    X(0x4A780010) /* DPCS: from here on, what the game does not use */         \
    X(0x4AE80413) /* NCDS */                                                   \
    X(0x4AF80416) /* NCDT */                                                   \
    X(0x4B18043F) /* NCCT */                                                   \
    X(0x4B38041C) /* CC */                                                     \
    X(0x4B280414) /* CDP */                                                    \
    X(0x4A100001) /* RTPS, the fraction kept */                                \
    X(0x4A180401) /* RTPS, from 0 */                                           \
    X(0x4A200030) /* RTPT, the fraction kept */                                \
    X(0x4A400012) /* MVMVA: rotation * V0 + translation, fraction kept */      \
    X(0x4A488012) /* MVMVA: rotation * V1 + translation */                     \
    X(0x4A490012) /* MVMVA: rotation * V2 + translation */                     \
    X(0x4A4A6412) /* MVMVA: light * V0, from 0 */                              \
    X(0x4A4C2012) /* MVMVA: light colour * V0 + background */                  \
    X(0x4A484012) /* MVMVA: rotation * V0 + far colour (the broken one) */     \
    X(0x4A4FC012) /* MVMVA: no matrix * IR + far colour */                     \
    X(0x4A4E6012) /* MVMVA: no matrix * V0 */

#define PROBE_COMMAND_WORD(word) word,
static const u32 g_probe_commands[] = {PROBE_COMMANDS(PROBE_COMMAND_WORD)};
#define PROBE_COMMAND_COUNT                                                    \
    ((s32)(sizeof(g_probe_commands) / sizeof(g_probe_commands[0])))

/* The order the data registers are loaded in. Two of them write others:
 * SXYP pushes onto the three screen positions and IRGB sets IR1 to IR3.
 * In most cases those two go first, so that what follows them is what the
 * operation finds; in one case of eight the registers go in by number,
 * and the operation finds what the two made. */
#define PROBE_LOADS_BY_NUMBER(vector) (((vector) & 7) == 7)

/* ---- the generator ---- */

/* The next number of a sequence (a shift-register generator). */
static u32 probe_Random(u32 *state) {
    u32 value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

/* A signed number of `bits` bits. */
static u32 probe_Signed(u32 *state, s32 bits) {
    s32 spare = 32 - bits;

    return (u32)((s32)(probe_Random(state) << spare) >> spare);
}

/* Two signed numbers of `bits` bits in the halves of a word. */
static u32 probe_Halves(u32 *state, s32 bits) {
    u32 low = probe_Signed(state, bits) & 0xFFFF;

    return low | (probe_Signed(state, bits) << 16);
}

/* A number of any width, or now and then one from the edges. */
static u32 probe_Any(u32 *state, s32 extreme) {
    static const u32 edges[8] = {
        0, 1, 0xFFFFFFFF, 0x7FFF, 0xFFFF8000, 0x8000, 0x7FFFFFFF, 0x80000000,
    };
    u32 pick = probe_Random(state);

    if (extreme) {
        if ((pick & 7) == 0) {
            return edges[(pick >> 3) & 7];
        }
        return probe_Random(state);
    }
    return probe_Signed(state, 1 + (s32)((pick >> 8) & 31));
}

/* The 64 registers of case `vector` of operation `command`. The cases go
 * round four kinds: two where the numbers are of the sizes a game has
 * (rotations of 0x1000, coordinates of a few thousand, a screen a few
 * hundred wide), so that most results fit; one where every register is a
 * number of some width; and one where every register is any 32 bits, or a
 * number from the edges. */
static void probe_MakeInput(s32 command, s32 vector, u32 *in) {
    u32 state =
        ((u32)(command + 1) * 0x9E3779B9) ^ ((u32)(vector + 1) * 0x85EBCA6B);
    u32 *control = in + PROBE_CONTROL;
    u32 *data = in + PROBE_DATA;
    s32 kind = vector & 3;
    s32 i;

    if (state == 0) {
        state = 1;
    }
    for (i = 0; i < 4; i++) {
        probe_Random(&state);
    }
    if (kind >= 2) {
        for (i = 0; i < PROBE_REGISTERS; i++) {
            in[i] = probe_Any(&state, kind == 3);
        }
        return;
    }

    /* The three matrices: 13 bits, the size of a rotation's numbers. */
    for (i = 0; i < 24; i += 8) {
        control[i + 0] = probe_Halves(&state, 13);
        control[i + 1] = probe_Halves(&state, 13);
        control[i + 2] = probe_Halves(&state, 13);
        control[i + 3] = probe_Halves(&state, 13);
        control[i + 4] = probe_Signed(&state, 13);
    }
    /* The translation; the background and the far colour. */
    for (i = 5; i < 8; i++) {
        control[i] = probe_Signed(&state, (kind == 0) ? 14 : 17);
        control[i + 8] = probe_Signed(&state, 14);
        control[i + 16] = probe_Signed(&state, 16) & 0xFFFFF;
    }
    /* The translation's z is in front of the eye in the first kind. */
    if (kind == 0) {
        control[7] = (control[7] & 0x1FFF) + 0x400;
    }
    control[24] = probe_Signed(&state, 26); /* the offsets, 16.16 */
    control[25] = probe_Signed(&state, 25);
    control[26] = probe_Random(&state) & 0x3FF; /* the projection plane */
    control[27] = probe_Signed(&state, 15);     /* the depth cue */
    control[28] = probe_Signed(&state, 25);
    control[29] = probe_Random(&state) & 0x7FF; /* the depth scales */
    control[30] = probe_Random(&state) & 0x3FF;
    control[31] = probe_Random(&state); /* FLAG: not all of it is kept */

    for (i = 0; i < 6; i += 2) {
        data[i] = probe_Halves(&state, 12); /* a vector */
        data[i + 1] = probe_Signed(&state, 12);
    }
    data[6] = probe_Random(&state);          /* a colour and a code */
    data[7] = probe_Random(&state) & 0xFFFF; /* OTZ */
    data[8] = probe_Random(&state) & 0x1FFF; /* IR0, a little past one */
    for (i = 9; i < 12; i++) {
        data[i] = probe_Signed(&state, 14); /* IR1 to IR3 */
    }
    for (i = 12; i < 16; i++) {
        data[i] = probe_Halves(&state, 11); /* the screen positions */
    }
    for (i = 16; i < 20; i++) {
        data[i] = probe_Random(&state) & 0xFFFF; /* the depths */
    }
    for (i = 20; i < 24; i++) {
        data[i] = probe_Random(&state); /* the colours; RES1 */
    }
    for (i = 24; i < 28; i++) {
        data[i] = probe_Signed(&state, 21); /* MAC0 to MAC3 */
    }
    data[28] = probe_Random(&state) & 0x7FFF; /* IRGB */
    data[29] = probe_Random(&state);          /* ORGB: read only */
    data[30] = probe_Any(&state, FALSE);      /* LZCS */
    data[31] = probe_Random(&state);          /* LZCR: read only */
}

/* One number for the 64 registers read back. */
static u32 probe_Hash(const u32 *words, s32 count) {
    u32 hash = 0x811C9DC5;
    s32 i;

    for (i = 0; i < count; i++) {
        hash = (hash ^ words[i]) * 0x01000193;
        hash = (hash << 13) | (hash >> 19);
    }
    return hash;
}

#endif /* PROBE_CASES_H */
