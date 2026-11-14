/* The rotation matrices of math.c against matrices worked out in floats. */
#include "src/main/math.c"
#include "src/main/math_sin.c"
#include "src/main/psxsdk/libgte/sincos.c"
#include "src/main/psxsdk/libgte/geo_00.c"
#include "src/main/psxsdk/libgte/geo_01.c"
#include "src/main/psxsdk/libgte/ratan.c"
#include "runtime/runtime.h"
#include "stubs/libgte.c"

/* The angles tried, in every combination of three: the quarter turns, some
 * in between, one negative and one of more than a turn. */
static const s16 g_test_angles[] = {
    0, 1, 100, 512, 1024, 1500, 2048, 3000, 4095, -700, 5000,
};
#define TEST_ANGLE_COUNT (s32)(sizeof(g_test_angles) / sizeof(g_test_angles[0]))

static void test_PrintMatrix(
    const char *label, SVECTOR *angles, MATRIX *matrix) {
    test_Print(
        "%s %d %d %d  %d %d %d  %d %d %d  %d %d %d\n", label, angles->vx,
        angles->vy, angles->vz, matrix->m[0][0], matrix->m[0][1],
        matrix->m[0][2], matrix->m[1][0], matrix->m[1][1], matrix->m[1][2],
        matrix->m[2][0], matrix->m[2][1], matrix->m[2][2]);
}

/* Prints, for each three angles, the matrix each order of axes makes of
 * them, and the matrix made again from the angles math_MatrixToAnglesYXZ
 * reads back out of the YXZ one; rotation.py compares all four with its
 * own, and then a point turned in the plane by math_Rotate2D. Here:
 * math_Transpose swaps rows and columns. */
void test_Main(void) {
    s32 i, j, k;
    s32 row, column;
    SVECTOR angles;
    SVECTOR read_back;
    MATRIX matrix;
    MATRIX original;

    angles.pad = 0;
    for (i = 0; i < TEST_ANGLE_COUNT; i++) {
        for (j = 0; j < TEST_ANGLE_COUNT; j++) {
            for (k = 0; k < TEST_ANGLE_COUNT; k++) {
                angles.vx = g_test_angles[i];
                angles.vy = g_test_angles[j];
                angles.vz = g_test_angles[k];

                math_RotMatrixZXY(&angles, &matrix);
                test_PrintMatrix("ZXY", &angles, &matrix);
                math_RotMatrixXYZ(&angles, &matrix);
                test_PrintMatrix("XYZ", &angles, &matrix);
                math_RotMatrixYXZ(&angles, &matrix);
                test_PrintMatrix("YXZ", &angles, &matrix);

                original = matrix;
                math_Transpose(&matrix);
                for (row = 0; row < 3; row++) {
                    for (column = 0; column < 3; column++) {
                        CHECK_EQ(
                            matrix.m[row][column], original.m[column][row]);
                    }
                }

                /* the angles of the matrix, and the matrix of those */
                matrix = original;
                math_MatrixToAnglesYXZ(&matrix, &read_back);
                math_RotMatrixYXZ(&read_back, &matrix);
                test_PrintMatrix("back", &angles, &matrix);
            }
        }
    }

    /* a point in the plane, turned */
    for (i = 0; i < TEST_ANGLE_COUNT; i++) {
        s32 x = 1000;
        s32 y = -250;

        math_Rotate2D(&x, &y, g_test_angles[i]);
        test_Print("2D 1000 -250 %d %d %d\n", g_test_angles[i], x, y);
    }
}
