/* The point-in-triangle test and the matrix inverse of hit_test.c. */
#include "src/main/hit_test.c"
#include "src/main/math.c"
#include "src/main/math_sin.c"
#include "runtime/runtime.h"
#include "stubs/gte.c"

s32 tex_IsGrayscale(void) { return 0; }

/* Triangles in x and z (y is not looked at), wound each way, of the size
 * of the things the fight tests: a few hundred units. */
static s32 g_test_triangles[][3][3] = {
    {{0, 0, 0}, {900, 0, 0}, {0, 0, 600}},
    {{0, 0, 0}, {0, 0, 600}, {900, 0, 0}},
    {{-400, 50, -300}, {500, -20, -100}, {100, 999, 700}},
    {{100, 0, 700}, {500, 0, -100}, {-400, 0, -300}},
    {{-2000, 0, 1000}, {3000, 0, 1100}, {400, 0, 1050}}, /* a sliver */
};
#define TRIANGLE_COUNT                                                         \
    (s32)(sizeof(g_test_triangles) / sizeof(g_test_triangles[0]))

static const s16 g_test_angles[] = {0, 300, 1024, 1700, 2900, -800};
#define ANGLE_COUNT (s32)(sizeof(g_test_angles) / sizeof(g_test_angles[0]))

/* Prints, for each triangle, whether each point of a grid over and around
 * it is inside; and, for a spread of rotations at three sizes, the matrix
 * and what math_InvertMatrix makes of it. geometry.py judges both with
 * exact arithmetic. */
void test_Main(void) {
    s32 i, j, k, scale;
    s32 point[3];
    SVECTOR angles;
    MATRIX matrix, inverse;

    for (i = 0; i < TRIANGLE_COUNT; i++) {
        s32(*triangle)[3] = g_test_triangles[i];

        test_Print("triangle %d %d  %d %d  %d %d\n", triangle[0][0],
                   triangle[0][2], triangle[1][0], triangle[1][2],
                   triangle[2][0], triangle[2][2]);
        point[1] = 12345; /* the height does not matter */
        for (point[0] = -2500; point[0] <= 3500; point[0] += 50) {
            for (point[2] = -500; point[2] <= 1300; point[2] += 25) {
                test_Print("point %d %d %d\n", point[0], point[2],
                           math_PointInTriangleXZ(
                               triangle[0], triangle[1], triangle[2], point));
            }
        }
    }

    angles.pad = 0;
    for (i = 0; i < ANGLE_COUNT; i++) {
        for (j = 0; j < ANGLE_COUNT; j++) {
            for (k = 0; k < ANGLE_COUNT; k++) {
                /* the rotation as it is, at half size and at one and a
                 * half times */
                for (scale = 2; scale <= 6; scale += 2) {
                    s32 row, column;

                    angles.vx = g_test_angles[i];
                    angles.vy = g_test_angles[j];
                    angles.vz = g_test_angles[k];
                    math_RotMatrixYXZ(&angles, &matrix);
                    test_Print("inverse");
                    for (row = 0; row < 3; row++) {
                        for (column = 0; column < 3; column++) {
                            matrix.m[row][column] =
                                matrix.m[row][column] * scale / 4;
                            test_Print(" %d", matrix.m[row][column]);
                        }
                    }
                    math_InvertMatrix(&matrix, &inverse);
                    for (row = 0; row < 3; row++) {
                        for (column = 0; column < 3; column++) {
                            test_Print(" %d", inverse.m[row][column]);
                        }
                    }
                    test_Print("\n");
                }
            }
        }
    }
}
