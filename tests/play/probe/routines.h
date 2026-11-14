#ifndef PROBE_ROUTINES_H
#define PROBE_ROUTINES_H

/* The cases the C beside the assembly is held to the assembly by
 * (tests/play/README.md, "The GTE probe"). As with cases.h, two programs
 * include this file: the probe calls the routines themselves, the
 * listings of asm/funcs/ as the game has them, on the emulator; the host
 * test tests/port/asm_vectors.c calls the C that the port has in their
 * place. A case loads the GTE's 64 registers, calls one routine with
 * made-up arguments, and what is compared is everything the routine
 * wrote and the 64 registers afterwards.
 *
 * Wants cases.h, the game's headers (bb2.h) and Sony's libgte.h ahead of
 * it. Written for both compilers: nothing here uses 64 bits.
 *
 * The routines that trap when a sum overflows (the listings' add, addi
 * and sub) are given numbers of the sizes the game has, on which they do
 * not; the others get any numbers at all in half their cases. A routine
 * that reads outside its table for a number it was not made for (a
 * negative root, a vector of zeroes to make unit length) is not given
 * such a number. */

/* How many cases a routine gets, and the words its results may take. */
#define PROBE_ROUTINE_VECTORS 500
#define PROBE_RESULT_WORDS 40

/* The routines of math_asm.c and render_asm.c that no header declares:
 * nothing of the game's C calls them. */
extern s32 math_Sqrt(s32 number);
extern s32 math_Length(s32 x, s32 y, s32 z);
extern void math_LerpSVector(
    SVECTOR *first, SVECTOR *second, s32 weight, SVECTOR *out);
extern void math_LerpMatrix3x3(
    MATRIX *first, MATRIX *second, s32 weight, MATRIX *out);
extern void math_TransformOffset(
    MATRIX *matrix, SVECTOR *offset, SVECTOR *point, VECTOR *out);
extern void gte_LoadMatrix(MATRIX *matrix);
extern void gte_LoadRotation(MATRIX *matrix);

/* ---- made-up arguments ---- */

/* A matrix: numbers of `bits` bits, a translation of `translation_bits`,
 * and a mark in the two bytes between them, which some routines write. */
static void probe_MakeMatrix(
    u32 *state, MATRIX *matrix, s32 bits, s32 translation_bits) {
    s32 i;

    for (i = 0; i < 9; i++) {
        ((s16 *)matrix->m)[i] = (s16)probe_Signed(state, bits);
    }
    ((u16 *)matrix)[9] = 0x5A5A;
    for (i = 0; i < 3; i++) {
        matrix->t[i] = (s32)probe_Signed(state, translation_bits);
    }
}

static void probe_MakeVector(u32 *state, VECTOR *vector, s32 bits) {
    vector->vx = (s32)probe_Signed(state, bits);
    vector->vy = (s32)probe_Signed(state, bits);
    vector->vz = (s32)probe_Signed(state, bits);
    vector->pad = 0x5A5A5A5A;
}

/* An SVECTOR on a word boundary: the routines load one as two words. */
typedef union {
    SVECTOR v;
    u32 words[2];
} ProbeShortVector;

static void probe_MakeShortVector(
    u32 *state, ProbeShortVector *vector, s32 bits) {
    vector->v.vx = (s16)probe_Signed(state, bits);
    vector->v.vy = (s16)probe_Signed(state, bits);
    vector->v.vz = (s16)probe_Signed(state, bits);
    vector->v.pad = 0x5A5A;
}

/* `count` words of a result, after those already in `out`. */
static u32 *probe_Put(u32 *out, const void *words, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        *out++ = ((const u32 *)words)[i];
    }
    return out;
}

/* The widths a case's numbers have: the game's sizes, or anything. */
#define PROBE_MATRIX_BITS(wild) ((wild) ? 16 : 13)
#define PROBE_VECTOR_BITS(wild) ((wild) ? 16 : 12)
#define PROBE_LONG_BITS(wild) ((wild) ? 32 : 20)

/* ---- the cases, one function a routine ---- */

static void probe_Do_SetRotMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    SetRotMatrix(&matrix);
}

static void probe_Do_SetColorMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    SetColorMatrix(&matrix);
}

static void probe_Do_SetTransMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    SetTransMatrix(&matrix);
}

static void probe_Do_SetBackColor(u32 *state, s32 wild, u32 *out) {
    s32 r = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));
    s32 g = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));

    SetBackColor(r, g, (s32)probe_Signed(state, PROBE_LONG_BITS(wild)));
}

static void probe_Do_SetFarColor(u32 *state, s32 wild, u32 *out) {
    s32 r = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));
    s32 g = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));

    SetFarColor(r, g, (s32)probe_Signed(state, PROBE_LONG_BITS(wild)));
}

static void probe_Do_SetGeomOffset(u32 *state, s32 wild, u32 *out) {
    s32 x = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));

    SetGeomOffset(x, (s32)probe_Signed(state, PROBE_LONG_BITS(wild)));
}

static void probe_Do_SetGeomScreen(u32 *state, s32 wild, u32 *out) {
    SetGeomScreen((s32)probe_Signed(state, PROBE_LONG_BITS(wild)));
}

static void probe_Do_ReadGeomScreen(u32 *state, s32 wild, u32 *out) {
    out[0] = (u32)ReadGeomScreen();
}

static void probe_Do_ReadSZfifo3(u32 *state, s32 wild, u32 *out) {
    ReadSZfifo3((s32 *)&out[0], (s32 *)&out[1], (s32 *)&out[2]);
}

static void probe_Do_LightColor(u32 *state, s32 wild, u32 *out) {
    VECTOR in;

    probe_MakeVector(state, &in, PROBE_LONG_BITS(wild));
    LightColor(&in, (VECTOR *)out);
}

static void probe_Do_DpqColorLight(u32 *state, s32 wild, u32 *out) {
    VECTOR light;
    u32 color;
    s32 fade;

    probe_MakeVector(state, &light, PROBE_LONG_BITS(wild));
    color = probe_Random(state);
    fade = (s32)probe_Signed(state, wild ? 32 : 13);
    DpqColorLight(&light, (CVECTOR *)&color, fade, (CVECTOR *)out);
}

static void probe_Do_DpqColor3(u32 *state, s32 wild, u32 *out) {
    u32 colors[3];
    s32 fade;

    colors[0] = probe_Random(state);
    colors[1] = probe_Random(state);
    colors[2] = probe_Random(state);
    fade = (s32)probe_Signed(state, wild ? 32 : 13);
    DpqColor3((CVECTOR *)&colors[0], (CVECTOR *)&colors[1],
              (CVECTOR *)&colors[2], fade, (CVECTOR *)&out[0],
              (CVECTOR *)&out[1], (CVECTOR *)&out[2]);
}

static void probe_Do_Intpl(u32 *state, s32 wild, u32 *out) {
    VECTOR in;
    s32 fade;

    probe_MakeVector(state, &in, PROBE_LONG_BITS(wild));
    fade = (s32)probe_Signed(state, wild ? 32 : 13);
    Intpl(&in, fade, (CVECTOR *)out);
}

static void probe_Do_Square12(u32 *state, s32 wild, u32 *out) {
    VECTOR in;

    probe_MakeVector(state, &in, PROBE_LONG_BITS(wild));
    out[4] = Square12(&in, (VECTOR *)out) == (VECTOR *)out;
}

static void probe_Do_Square0(u32 *state, s32 wild, u32 *out) {
    VECTOR in;

    probe_MakeVector(state, &in, PROBE_LONG_BITS(wild));
    out[4] = Square0(&in, (VECTOR *)out) == (VECTOR *)out;
}

static void probe_Do_AverageZ3(u32 *state, s32 wild, u32 *out) {
    s32 a = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));
    s32 b = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));

    out[0] = (u32)AverageZ3(a, b, (s32)probe_Signed(state, 17));
}

static void probe_Do_AverageZ4(u32 *state, s32 wild, u32 *out) {
    s32 a = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));
    s32 b = (s32)probe_Signed(state, PROBE_LONG_BITS(wild));
    s32 c = (s32)probe_Signed(state, 17);

    out[0] = (u32)AverageZ4(a, b, c, (s32)probe_Signed(state, 16));
}

static void probe_Do_OuterProduct12(u32 *state, s32 wild, u32 *out) {
    VECTOR a;
    VECTOR b;

    probe_MakeVector(state, &a, PROBE_LONG_BITS(wild));
    probe_MakeVector(state, &b, PROBE_LONG_BITS(wild));
    OuterProduct12(&a, &b, (VECTOR *)out);
}

static void probe_Do_OuterProduct0(u32 *state, s32 wild, u32 *out) {
    VECTOR a;
    VECTOR b;

    probe_MakeVector(state, &a, PROBE_LONG_BITS(wild));
    probe_MakeVector(state, &b, PROBE_LONG_BITS(wild));
    OuterProduct0(&a, &b, (VECTOR *)out);
}

static void probe_Do_Lzc(u32 *state, s32 wild, u32 *out) {
    out[0] = (u32)Lzc((s32)probe_Any(state, wild));
}

static void probe_Do_RotTransPers(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector point;

    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    out[3] = (u32)RotTransPers(
        &point.v, (s32 *)&out[0], (s32 *)&out[1], (s32 *)&out[2]);
}

static void probe_Do_RotTransPers3(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector points[3];
    s32 i;

    for (i = 0; i < 3; i++) {
        probe_MakeShortVector(state, &points[i], PROBE_VECTOR_BITS(wild));
    }
    out[5] = (u32)RotTransPers3(
        &points[0].v, &points[1].v, &points[2].v, (s32 *)&out[0],
        (s32 *)&out[1], (s32 *)&out[2], (s32 *)&out[3], (s32 *)&out[4]);
}

static void probe_Do_RotTransPers4(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector points[4];
    s32 i;

    for (i = 0; i < 4; i++) {
        probe_MakeShortVector(state, &points[i], PROBE_VECTOR_BITS(wild));
    }
    out[6] = (u32)RotTransPers4(
        &points[0].v, &points[1].v, &points[2].v, &points[3].v, (s32 *)&out[0],
        (s32 *)&out[1], (s32 *)&out[2], (s32 *)&out[3], (s32 *)&out[4],
        (s32 *)&out[5]);
}

static void probe_Do_RotTrans(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector point;

    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    RotTrans(&point.v, (VECTOR *)out, (s32 *)&out[4]);
}

static void probe_Do_ApplyRotMatrix(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector point;

    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    ApplyRotMatrix(&point.v, (VECTOR *)out);
}

static void probe_Do_ApplyMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector point;

    probe_MakeMatrix(state, &matrix, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    out[4] = ApplyMatrix(&matrix, &point.v, (VECTOR *)out) == (VECTOR *)out;
}

static void probe_Do_ApplyMatrixLV(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    VECTOR point;

    probe_MakeMatrix(state, &matrix, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeVector(state, &point, wild ? 32 : 24);
    out[4] = ApplyMatrixLV(&matrix, &point, (VECTOR *)out) == (VECTOR *)out;
}

static void probe_Do_ApplyRotMatrixLV(u32 *state, s32 wild, u32 *out) {
    VECTOR point;

    probe_MakeVector(state, &point, wild ? 32 : 24);
    out[4] = ApplyRotMatrixLV(&point, (VECTOR *)out) == (VECTOR *)out;
}

static void probe_Do_MulMatrix0(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;
    MATRIX product;

    probe_MakeMatrix(state, &a, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &b, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &product, 16, 32);
    out = probe_Put(out, MulMatrix0(&a, &b, &product), 8);
    out = probe_Put(out, &a, 8);
    probe_Put(out, &b, 8);
}

static void probe_Do_MulMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;

    probe_MakeMatrix(state, &a, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &b, PROBE_MATRIX_BITS(wild), 32);
    out = probe_Put(out, MulMatrix(&a, &b), 8);
    probe_Put(out, &b, 8);
}

static void probe_Do_MulMatrix2(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;

    probe_MakeMatrix(state, &a, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &b, PROBE_MATRIX_BITS(wild), 32);
    out = probe_Put(out, MulMatrix2(&a, &b), 8);
    probe_Put(out, &a, 8);
}

/* CompMatrix adds two translations with an instruction that traps: they
 * are kept small enough. */
static void probe_Do_CompMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;
    MATRIX product;

    probe_MakeMatrix(state, &a, PROBE_MATRIX_BITS(wild), 24);
    probe_MakeMatrix(state, &b, PROBE_MATRIX_BITS(wild), wild ? 24 : 14);
    probe_MakeMatrix(state, &product, 16, 32);
    probe_Put(out, CompMatrix(&a, &b, &product), 8);
}

static void probe_Do_ScaleMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    VECTOR scale;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_MakeVector(state, &scale, wild ? 32 : 14);
    probe_Put(out, ScaleMatrix(&matrix, &scale), 8);
}

static void probe_Do_ScaleMatrixL(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    VECTOR scale;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_MakeVector(state, &scale, wild ? 32 : 14);
    probe_Put(out, ScaleMatrixL(&matrix, &scale), 8);
}

static void probe_Do_RotMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector angles;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_MakeShortVector(state, &angles, 16);
    probe_Put(out, RotMatrix(&angles.v, &matrix), 8);
}

static void probe_Do_RotMatrixZYX(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector angles;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_MakeShortVector(state, &angles, 16);
    probe_Put(out, RotMatrixZYX(&angles.v, &matrix), 8);
}

static void probe_Do_RotMatrixX(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_Put(out, RotMatrixX((s32)probe_Any(state, wild), &matrix), 8);
}

static void probe_Do_RotMatrixY(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_Put(out, RotMatrixY((s32)probe_Any(state, wild), &matrix), 8);
}

static void probe_Do_RotMatrixZ(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_Put(out, RotMatrixZ((s32)probe_Any(state, wild), &matrix), 8);
}

static void probe_Do_LoadAverage12(u32 *state, s32 wild, u32 *out) {
    VECTOR a;
    VECTOR b;
    s32 p0;

    probe_MakeVector(state, &a, PROBE_LONG_BITS(wild));
    probe_MakeVector(state, &b, PROBE_LONG_BITS(wild));
    p0 = (s32)probe_Signed(state, wild ? 32 : 13);
    LoadAverage12(
        &a, &b, p0, (s32)probe_Signed(state, wild ? 32 : 13), (VECTOR *)out);
}

static void probe_Do_LoadAverage0(u32 *state, s32 wild, u32 *out) {
    VECTOR a;
    VECTOR b;
    s32 p0;

    probe_MakeVector(state, &a, PROBE_LONG_BITS(wild));
    probe_MakeVector(state, &b, PROBE_LONG_BITS(wild));
    p0 = (s32)probe_Signed(state, wild ? 32 : 13);
    LoadAverage0(
        &a, &b, p0, (s32)probe_Signed(state, wild ? 32 : 13), (VECTOR *)out);
}

static void probe_Do_LoadAverageShort12(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector a;
    ProbeShortVector b;
    s32 p0;

    probe_MakeShortVector(state, &a, 16);
    probe_MakeShortVector(state, &b, 16);
    p0 = (s32)probe_Signed(state, wild ? 32 : 13);
    LoadAverageShort12(&a.v, &b.v, p0, (s32)probe_Signed(state, wild ? 32 : 13),
                       (SVECTOR *)out);
}

static void probe_Do_LoadAverageShort0(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector a;
    ProbeShortVector b;
    s32 p0;

    probe_MakeShortVector(state, &a, 16);
    probe_MakeShortVector(state, &b, 16);
    p0 = (s32)probe_Signed(state, wild ? 32 : 13);
    LoadAverageShort0(&a.v, &b.v, p0, (s32)probe_Signed(state, wild ? 32 : 13),
                      (SVECTOR *)out);
}

static void probe_Do_LoadAverageByte(u32 *state, s32 wild, u32 *out) {
    u32 a = probe_Random(state);
    u32 b = probe_Random(state);
    s32 p0 = (s32)probe_Signed(state, wild ? 32 : 13);

    LoadAverageByte((u8 *)&a, (u8 *)&b, p0,
                    (s32)probe_Signed(state, wild ? 32 : 13), (u8 *)out);
}

static void probe_Do_LoadAverageCol(u32 *state, s32 wild, u32 *out) {
    u32 a = probe_Random(state);
    u32 b = probe_Random(state);
    s32 p0 = (s32)probe_Signed(state, wild ? 32 : 13);

    LoadAverageCol((u8 *)&a, (u8 *)&b, p0,
                   (s32)probe_Signed(state, wild ? 32 : 13), (u8 *)out);
}

/* A number that is not negative, of any width. */
static s32 probe_Positive(u32 *state) {
    u32 value = probe_Random(state) >> 1;

    return (s32)(value >> (probe_Random(state) & 31));
}

static void probe_Do_SquareRoot0(u32 *state, s32 wild, u32 *out) {
    out[0] = (u32)SquareRoot0(probe_Positive(state));
}

static void probe_Do_SquareRoot12(u32 *state, s32 wild, u32 *out) {
    out[0] = (u32)SquareRoot12(probe_Positive(state));
}

/* ---- the game's own ---- */

/* Up to sixteen words moved a few words up or down among forty, by any
 * number of bytes. */
static void probe_Do_mem_MoveWords(u32 *state, s32 wild, u32 *out) {
    u32 pick = probe_Random(state);
    s32 i;

    for (i = 0; i < PROBE_RESULT_WORDS; i++) {
        out[i] = probe_Random(state);
    }
    mem_MoveWords((s32)&out[8 + (pick & 7)], (s32)&out[8 + ((pick >> 3) & 7)],
                  (s32)((pick >> 6) & 63));
}

static void probe_Do_math_Sqrt(u32 *state, s32 wild, u32 *out) {
    out[0] = (u32)math_Sqrt(probe_Positive(state));
}

static void probe_Do_math_Length(u32 *state, s32 wild, u32 *out) {
    s32 x = (s32)probe_Signed(state, 15);
    s32 y = (s32)probe_Signed(state, 15);

    out[0] = (u32)math_Length(x, y, (s32)probe_Signed(state, 15));
}

static void probe_Do_math_LengthSquared(u32 *state, s32 wild, u32 *out) {
    s32 x = (s32)probe_Signed(state, wild ? 32 : 15);
    s32 y = (s32)probe_Signed(state, wild ? 32 : 15);

    out[0] =
        (u32)math_LengthSquared(x, y, (s32)probe_Signed(state, wild ? 32 : 15));
}

static void probe_Do_math_LerpRotation(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;
    MATRIX blend;

    probe_MakeMatrix(state, &a, 13, 32);
    probe_MakeMatrix(state, &b, 13, 32);
    probe_MakeMatrix(state, &blend, 16, 32);
    math_LerpRotation(&a, &b, (s32)(probe_Random(state) & 0xFFF), &blend);
    probe_Put(out, &blend, 8);
}

static void probe_Do_math_LerpSVector(u32 *state, s32 wild, u32 *out) {
    ProbeShortVector a;
    ProbeShortVector b;
    ProbeShortVector blend;

    probe_MakeShortVector(state, &a, 16);
    probe_MakeShortVector(state, &b, 16);
    probe_MakeShortVector(state, &blend, 16);
    math_LerpSVector(&a.v, &b.v, (s32)(probe_Random(state) & 0xFFF), &blend.v);
    probe_Put(out, &blend.v, 2);
}

static void probe_Do_math_LerpMatrix3x3(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;
    MATRIX blend;

    probe_MakeMatrix(state, &a, 16, 32);
    probe_MakeMatrix(state, &b, 16, 32);
    probe_MakeMatrix(state, &blend, 16, 32);
    math_LerpMatrix3x3(&a, &b, (s32)(probe_Random(state) & 0xFFF), &blend);
    probe_Put(out, &blend, 8);
}

static void probe_Do_math_MulMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX a;
    MATRIX b;
    MATRIX product;

    probe_MakeMatrix(state, &a, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &b, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeMatrix(state, &product, 16, 32);
    math_MulMatrix(&a, &b, &product);
    probe_Put(out, &product, 8);
}

static void probe_Do_math_Transform(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector point;

    probe_MakeMatrix(state, &matrix, PROBE_MATRIX_BITS(wild), wild ? 32 : 16);
    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    math_Transform(&matrix, &point.v, (VECTOR *)out);
}

static void probe_Do_math_TransformVector(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    VECTOR point;

    probe_MakeMatrix(state, &matrix, PROBE_MATRIX_BITS(wild), wild ? 32 : 16);
    probe_MakeVector(state, &point, PROBE_LONG_BITS(wild));
    math_TransformVector(&matrix, &point, out);
}

static void probe_Do_math_TransformOffset(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector offset;
    ProbeShortVector point;

    probe_MakeMatrix(state, &matrix, PROBE_MATRIX_BITS(wild), 32);
    probe_MakeShortVector(state, &offset, 16);
    probe_MakeShortVector(state, &point, PROBE_VECTOR_BITS(wild));
    math_TransformOffset(&matrix, &offset.v, &point.v, (VECTOR *)out);
}

static void probe_Do_gte_LoadMatrix(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    gte_LoadMatrix(&matrix);
}

static void probe_Do_gte_LoadRotation(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;

    probe_MakeMatrix(state, &matrix, 16, 32);
    gte_LoadRotation(&matrix);
}

static void probe_Do_gte_ReadFarColor(u32 *state, s32 wild, u32 *out) {
    gte_ReadFarColor((CVECTOR *)out);
}

static void probe_Do_render_DepthToOtShifted(u32 *state, s32 wild, u32 *out) {
    s32 depth = (s32)probe_Any(state, wild);

    out[0] =
        (u32)render_DepthToOtShifted(depth, (s32)(probe_Random(state) & 3));
}

/* The projection and what reads it back, as the collision test has them:
 * one after the other. The frame's numbers and the point are the sizes
 * the game has; the routine subtracts with an instruction that traps. */
static void probe_Do_collision_ProjectOnFace(u32 *state, s32 wild, u32 *out) {
    s16 frame[10];
    s32 x;
    s32 y;
    s32 i;

    for (i = 0; i < 10; i++) {
        frame[i] = (s16)probe_Signed(state, (i < 3) ? 15 : 13);
    }
    x = (s32)probe_Signed(state, 16);
    y = (s32)probe_Signed(state, 16);
    collision_ProjectOnFace((s32)frame, x, y, (s32)probe_Signed(state, 16));
    collision_ReadProjection((s32 *)&out[0], (s32 *)&out[1]);
}

static void probe_Do_math_RotMatrixZYX(u32 *state, s32 wild, u32 *out) {
    MATRIX matrix;
    ProbeShortVector angles;

    probe_MakeMatrix(state, &matrix, 16, 32);
    probe_MakeShortVector(state, &angles, 16);
    math_RotMatrixZYX(&angles.v, &matrix);
    probe_Put(out, &matrix, 8);
}

/* ---- the list ---- */

/* Sony's 47 first (every routine of libgte that is assembly but
 * InitGeom, which patches the console's kernel), then the game's 19
 * (gte_NormalizeIR is run by math_LerpRotation; collision_ReadProjection
 * by the case of collision_ProjectOnFace; sys_Trap stops the machine). */
#define PROBE_ROUTINES(X)                                                      \
    X(SetRotMatrix)                                                            \
    X(SetColorMatrix)                                                          \
    X(SetTransMatrix)                                                          \
    X(SetBackColor)                                                            \
    X(SetFarColor)                                                             \
    X(SetGeomOffset)                                                           \
    X(SetGeomScreen)                                                           \
    X(ReadGeomScreen)                                                          \
    X(ReadSZfifo3)                                                             \
    X(LightColor)                                                              \
    X(DpqColorLight)                                                           \
    X(DpqColor3)                                                               \
    X(Intpl)                                                                   \
    X(Square12)                                                                \
    X(Square0)                                                                 \
    X(AverageZ3)                                                               \
    X(AverageZ4)                                                               \
    X(OuterProduct12)                                                          \
    X(OuterProduct0)                                                           \
    X(Lzc)                                                                     \
    X(RotTransPers)                                                            \
    X(RotTransPers3)                                                           \
    X(RotTransPers4)                                                           \
    X(RotTrans)                                                                \
    X(ApplyRotMatrix)                                                          \
    X(ApplyMatrix)                                                             \
    X(ApplyMatrixLV)                                                           \
    X(ApplyRotMatrixLV)                                                        \
    X(MulMatrix0)                                                              \
    X(MulMatrix)                                                               \
    X(MulMatrix2)                                                              \
    X(CompMatrix)                                                              \
    X(ScaleMatrix)                                                             \
    X(ScaleMatrixL)                                                            \
    X(RotMatrix)                                                               \
    X(RotMatrixZYX)                                                            \
    X(RotMatrixX)                                                              \
    X(RotMatrixY)                                                              \
    X(RotMatrixZ)                                                              \
    X(LoadAverage12)                                                           \
    X(LoadAverage0)                                                            \
    X(LoadAverageShort12)                                                      \
    X(LoadAverageShort0)                                                       \
    X(LoadAverageByte)                                                         \
    X(LoadAverageCol)                                                          \
    X(SquareRoot0)                                                             \
    X(SquareRoot12)                                                            \
    X(mem_MoveWords)                                                           \
    X(math_Sqrt)                                                               \
    X(math_Length)                                                             \
    X(math_LengthSquared)                                                      \
    X(math_LerpRotation)                                                       \
    X(math_LerpSVector)                                                        \
    X(math_LerpMatrix3x3)                                                      \
    X(math_MulMatrix)                                                          \
    X(math_Transform)                                                          \
    X(math_TransformVector)                                                    \
    X(math_TransformOffset)                                                    \
    X(gte_LoadMatrix)                                                          \
    X(gte_LoadRotation)                                                        \
    X(gte_ReadFarColor)                                                        \
    X(render_DepthToOtShifted)                                                 \
    X(collision_ProjectOnFace)                                                 \
    X(math_RotMatrixZYX)

typedef void (*ProbeRoutine)(u32 *state, s32 wild, u32 *out);

#define PROBE_ROUTINE_ENTRY(name) probe_Do_##name,
static const ProbeRoutine g_probe_routines[] = {
    PROBE_ROUTINES(PROBE_ROUTINE_ENTRY)};
#define PROBE_ROUTINE_COUNT                                                    \
    ((s32)(sizeof(g_probe_routines) / sizeof(g_probe_routines[0])))

/* Case `vector` of routine `routine`: the 64 registers to load before
 * it (cases.h; the routines are numbered after the operations), and then
 * the call, whose results go to `out` (PROBE_RESULT_WORDS words, cleared
 * here). Half the cases have numbers of the game's sizes in the
 * registers and the arguments, half anything. */
static void probe_MakeRoutineInput(s32 routine, s32 vector, u32 *in) {
    probe_MakeInput(PROBE_COMMAND_COUNT + routine, vector, in);
}

static void probe_RunRoutine(s32 routine, s32 vector, u32 *out) {
    u32 state =
        ((u32)(routine + 1) * 0xC2B2AE35) ^ ((u32)(vector + 1) * 0x27D4EB2F);
    s32 i;

    if (state == 0) {
        state = 1;
    }
    for (i = 0; i < 4; i++) {
        probe_Random(&state);
    }
    for (i = 0; i < PROBE_RESULT_WORDS; i++) {
        out[i] = 0;
    }
    g_probe_routines[routine](&state, (vector & 3) >= 2, out);
}

#endif /* PROBE_ROUTINES_H */
