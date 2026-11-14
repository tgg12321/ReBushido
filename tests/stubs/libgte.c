/* Stand-ins for the routines of Sony's libgte that are assembly driving the
 * GTE (src/main/psxsdk/libgte/mtx_*.c, msc*.c, smp_*.c keep them as
 * listings), written out as the arithmetic the GTE does: products of
 * numbers with twelve fraction bits, summed and shifted down by twelve.
 * Only the ones a test calls. Sony's routines that are C (rsin, rcos,
 * ratan2) are not here: a test includes the real ones.
 * Included by a test after the game's file. */

static s32 libgte_Saturate16(s32 value) {
    if (value > 0x7FFF) {
        return 0x7FFF;
    }
    if (value < -0x8000) {
        return -0x8000;
    }
    return value;
}

/* Sony's ApplyMatrix: a short vector through a matrix's rotation, as three
 * words. */
VECTOR *ApplyMatrix(MATRIX *matrix, SVECTOR *vector, VECTOR *out) {
    s32 in[3];
    s32 i;
    s32 *result = &out->vx;

    in[0] = vector->vx;
    in[1] = vector->vy;
    in[2] = vector->vz;
    for (i = 0; i < 3; i++) {
        result[i] = (matrix->m[i][0] * in[0] + matrix->m[i][1] * in[1] +
                     matrix->m[i][2] * in[2]) >>
                    12;
    }
    return out;
}

/* Sony's MulMatrix: first = first * second, each number cut to 16 bits. */
MATRIX *MulMatrix(MATRIX *first, MATRIX *second) {
    s16 product[3][3];
    s32 row, column;

    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            product[row][column] = libgte_Saturate16(
                (first->m[row][0] * second->m[0][column] +
                 first->m[row][1] * second->m[1][column] +
                 first->m[row][2] * second->m[2][column]) >>
                12);
        }
    }
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            first->m[row][column] = product[row][column];
        }
    }
    return first;
}

/* The square root of a number of up to 44 bits, rounded down. */
static u32 libgte_Root(u64 value) {
    u32 root = 0;
    u32 bit;

    for (bit = 0x200000; bit != 0; bit >>= 1) {
        if ((u64)(root + bit) * (root + bit) <= value) {
            root += bit;
        }
    }
    return root;
}

/* Sony's SquareRoot0: the square root of a whole number. (The real one
 * reads a table and is a little less exact.) */
s32 SquareRoot0(s32 value) { return libgte_Root((u32)value); }

/* Sony's SquareRoot12: the same for a number with twelve fraction bits,
 * which the root has too. */
s32 SquareRoot12(s32 value) { return libgte_Root((u64)(u32)value << 12); }

/* Sony's Square12: each part of a vector times itself, the parts having
 * twelve fraction bits. */
VECTOR *Square12(VECTOR *vector, VECTOR *out) {
    out->vx = (vector->vx * vector->vx) >> 12;
    out->vy = (vector->vy * vector->vy) >> 12;
    out->vz = (vector->vz * vector->vz) >> 12;
    return out;
}

/* The matrix the GTE has been given: Sony's SetRotMatrix and
 * SetTransMatrix load its rotation and its translation. */
static MATRIX g_libgte_matrix;

void SetRotMatrix(MATRIX *matrix) {
    s32 row, column;

    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            g_libgte_matrix.m[row][column] = matrix->m[row][column];
        }
    }
}

void SetTransMatrix(MATRIX *matrix) {
    g_libgte_matrix.t[0] = matrix->t[0];
    g_libgte_matrix.t[1] = matrix->t[1];
    g_libgte_matrix.t[2] = matrix->t[2];
}

/* Sony's RotTrans: a point through the GTE's matrix, rotation and
 * translation. (`flag` is the GTE's note of what overflowed: none here.) */
void RotTrans(SVECTOR *point, VECTOR *out, s32 *flag) {
    VECTOR turned;

    ApplyMatrix(&g_libgte_matrix, point, &turned);
    out->vx = turned.vx + g_libgte_matrix.t[0];
    out->vy = turned.vy + g_libgte_matrix.t[1];
    out->vz = turned.vz + g_libgte_matrix.t[2];
    *flag = 0;
}
