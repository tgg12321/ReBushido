#ifndef MATH_H
#define MATH_H

/* Fixed-point helpers: the sine table, rotation matrices in each order of
 * axes, colour conversion (math.c, math_sin.c), and the helpers that were
 * written in assembly (math_asm.c), and the plain geometry the fight's
 * files have among their own code. Angles are 12 bits to the turn and 1.0
 * is 0x1000 throughout, Sony's ONE. */

#include "common.h"
#include <psxsdk/libgte.h>

/* A whole turn, as angles are counted, and the mask that keeps an angle
 * within one. */
#define ANGLE_TURN 0x1000
#define ANGLE_MASK (ANGLE_TURN - 1)

/* The sine table: one turn in ANGLE_TURN steps.
 * cos(a) is g_math_sin_table[(a + ANGLE_TURN / 4) & ANGLE_MASK]. */
extern s16 g_math_sin_table[ANGLE_TURN];

/* Builds the rotation part of a matrix from three angles, like Sony's
 * RotMatrix. One function per order of axes, in a table indexed by enum
 * RotationOrder (node.h); only the four orders the game uses are filled
 * in. */
typedef void (*RotMatrixFunc)(SVECTOR *angles, MATRIX *matrix);
extern RotMatrixFunc g_math_rot_matrix_funcs[6];
extern void math_InitRotMatrixFuncs(void);

extern void math_RotMatrixZYX(SVECTOR *angles, MATRIX *matrix);
extern void math_RotMatrixZXY(SVECTOR *angles, MATRIX *matrix);
extern void math_RotMatrixYXZ(SVECTOR *angles, MATRIX *matrix);

/* The angles that math_RotMatrixYXZ would turn into `matrix`. */
extern void math_MatrixToAnglesYXZ(MATRIX *matrix, SVECTOR *angles);
extern void math_Transpose(MATRIX *matrix);

/* A colour's part as a byte, 0..255, made a share of ONE. */
#define MATH_BYTE_TO_ONE(byte) (((byte) << 12) / 255)

/* A point between two of a motion frame's points, which have y and z the
 * other way up from the world: `weight` of the second, ONE - `weight` of
 * the first, written to three words at `out_addr`. */
extern void math_LerpFramePoint(
    s16 *first_vector, s16 *second_vector, s32 weight, s32 out_addr);

/* The distance to the projection plane that gives a horizontal field of view
 * of `fov_degrees` on the 640-wide screen (main.c). */
extern s32 math_FovToScreenDist(s32 fov_degrees);

/* The brightness of a colour, each part weighted as the eye sees it
 * (tex.c). */
extern s32 math_Grayscale3(s32 red, s32 green, s32 blue);

/* ---- geometry for the fight (hit_test.c, fighter_pose.c, fighter_hit.c) ----
 */

/* Whether a point is inside a triangle, seen from above. */
extern s32 math_PointInTriangleXZ(
    s32 *vertex_a, s32 *vertex_b, s32 *vertex_c, s32 *test_point);
/* The heading a triangle's normal points along. */
extern s32 math_TriangleHeading(
    VECTOR *first_vertex, VECTOR *second_vertex, VECTOR *third_vertex);
/* The part of a vector along a surface's normal times `along`, the part
 * across it times `across`, both of SCALE_ALONG_ONE. */
#define SCALE_ALONG_ONE 0x100
extern void math_ScaleAlong(
    s32 *vector, s16 *normal, s32 *out, s32 along, s32 across);
extern void math_InvertMatrix(MATRIX *matrix, MATRIX *inverse);
extern void math_MatrixToAngles(MATRIX *matrix, s16 *angles_out);
extern void math_TurnAngles(s16 *angles, s32 z, s32 y, s32 x);
/* The speed across the ground that will carry something thrown up at
 * `lift` from one place to another. */
extern void math_LobVelocity(
    s32 *from, s32 *to, s32 lift, s32 gravity, s32 *velocity_out);
/* A vector's direction turned about, as a unit vector. */
extern void math_ReverseUnit(s32 *vector, s16 *unit_out);

/* ---- the assembly (math_asm.c) ---- */

/* The product of two rotations, like Sony's MulMatrix0: out = a * b. It
 * leaves `a` in the GTE as the rotation, with no translation. */
extern void math_MulMatrix(MATRIX *a, MATRIX *b, MATRIX *out);
/* A blend of two rotations that is a rotation again: `weight` of the second
 * (ONE: all of it). */
extern void math_LerpRotation(
    MATRIX *first, MATRIX *second, s32 weight, MATRIX *out);
/* A point taken through a matrix and its translation, into three words:
 * the point as an SVECTOR, and as three words. */
extern void math_Transform(MATRIX *matrix, SVECTOR *point, VECTOR *out);
extern void math_TransformVector(void *matrix, void *point, void *out);
/* x * x + y * y + z * z. */
extern s32 math_LengthSquared(s32 x, s32 y, s32 z);

/* The GTE's far colour, the colour of the fog, as three bytes. */
extern void gte_ReadFarColor(CVECTOR *color);

/* Moves `size` bytes, rounded up to whole words; the two may overlap. */
extern void mem_MoveWords(s32 src, s32 dest, s32 size);

/* The square root of i, times 8 and rounded down, for i below 0x400
 * (fight_data.c). */
#define MATH_SQRT_TABLE_SIZE 0x400
extern u8 g_math_sqrt8_table[MATH_SQRT_TABLE_SIZE];
/* The root of a larger number: the number shifted down into the table's
 * ten bits by MATH_SQRT_SHIFT (the other 22 of a word) less its leading
 * zeros made even; and the table's byte, shifted up 16, brought down by
 * MATH_SQRT_UNSHIFT (the 16 and the 3 of "times 8") less half as many. */
#define MATH_SQRT_SHIFT 22
#define MATH_SQRT_UNSHIFT 19

#ifdef gte_Lzc
/* The integer square root of `x`. A number below 0x400 comes straight from
 * the table. A larger one is shifted down into the table's range by an even
 * count of bits, found from the GTE's count of its leading zeros (none for
 * a negative number), and the table's root is shifted back up by half as
 * many: such a root is never above the real one and at most a 32nd of it
 * below, and a negative number is rooted as the unsigned number it is. The
 * original has these lines in some twenty-five places; where the
 * number rooted is an expression they are this function, inlined, down to
 * the argument being used again for the table's byte. Defined for a file
 * that includes <psxsdk/inline_o.h> and <psxsdk/gtemac.h> ahead of this
 * header. */
static inline s32 math_TableSqrt(s32 x) {
    if ((u32)x < MATH_SQRT_TABLE_SIZE) {
        return (u32)g_math_sqrt8_table[x] >> 3;
    } else {
        s32 lz;
        s32 lzcr = 0;

        if (x >= 0) {
            gte_Lzc(x, &lz);
            lzcr = lz;
        }
        {
            s32 shift = MATH_SQRT_SHIFT - (lzcr & ~1);

            x = g_math_sqrt8_table[(u32)x >> shift];
            return (u32)(x << 16) >> (MATH_SQRT_UNSHIFT - ((u32)shift >> 1));
        }
    }
}
#endif

/* The same lines as a statement, for the places where the call does not
 * give the original's code (the number rooted is a variable that is used
 * again, or the root goes back into it). Wants <psxsdk/gtemac.h> for
 * gte_Lzc. A bare if/else: it is a whole statement, so do not hang an
 * `else` after it. */
#define MATH_SQRT(out, x)                                                      \
    if ((u32)(x) < MATH_SQRT_TABLE_SIZE) {                                     \
        (out) = (u32)g_math_sqrt8_table[x] >> 3;                               \
    } else {                                                                   \
        s32 lz;                                                                \
        s32 lzcr = 0;                                                          \
        if ((x) >= 0) {                                                        \
            gte_Lzc(x, &lz);                                                   \
            lzcr = lz;                                                         \
        }                                                                      \
        {                                                                      \
            s32 shift = MATH_SQRT_SHIFT - (lzcr & ~1);                         \
            (out) = (u32)(g_math_sqrt8_table[(u32)(x) >> shift] << 16) >>      \
                    (MATH_SQRT_UNSHIFT - ((u32)shift >> 1));                   \
        }                                                                      \
    }

#endif /* MATH_H */
