/* The C beside the assembly against real maths and cases by hand. */
#include "src/main/math_asm.c"
#include "src/main/math_sin.c"
#include "src/main/render_asm.c"
#include "runtime/runtime.h"

/* sys_Trap's end of the line, counted. */
static s32 g_test_traps;

void port_Trap(void) { g_test_traps++; }

static void test_PrintMatrix(const char *what, const MATRIX *matrix) {
    test_Print(
        "%s %d %d %d %d %d %d %d %d %d\n", what, matrix->m[0][0],
        matrix->m[0][1], matrix->m[0][2], matrix->m[1][0], matrix->m[1][1],
        matrix->m[1][2], matrix->m[2][0], matrix->m[2][1], matrix->m[2][2]);
}

/* A number from a sequence, for the cases the Python side works out. */
static u32 g_test_seed = 0x2545F491;

static s32 test_Random(s32 bits) {
    g_test_seed ^= g_test_seed << 13;
    g_test_seed ^= g_test_seed >> 17;
    g_test_seed ^= g_test_seed << 5;
    return (s32)(g_test_seed << (32 - bits)) >> (32 - bits);
}

/* ---- roots and lengths ---- */

static void test_Roots(void) {
    u32 x;

    /* every number to 0x3000, then steps of one part in 64 to the
     * largest; the Python side takes the roots itself */
    for (x = 0; x < 0x80000000U; x += (x < 0x3000) ? 1 : x >> 6) {
        test_Print("root %u %d %d %d\n", x, math_Sqrt((s32)x),
                   SquareRoot0((s32)x), SquareRoot12((s32)x));
    }
    CHECK_EQ(math_Sqrt(0), 0);
    CHECK_EQ(math_Sqrt(1), 1);
    CHECK_EQ(math_Sqrt(144), 12);
    CHECK_EQ(math_Sqrt(0x1000000), 0x1000);
    CHECK_EQ(SquareRoot0(0), 0);
    CHECK_EQ(SquareRoot0(144), 12);
    CHECK_EQ(SquareRoot12(4 * ONE), 2 * ONE);
    /* no root of a negative number: the port gives 0 (the assembly
     * reads outside its table) */
    CHECK_EQ(math_Sqrt(-4), 0);
    CHECK_EQ(SquareRoot0(-4), 0);
    CHECK_EQ(SquareRoot12(-4), 0);

    /* 3, 4, 12 is 13 long; the GTE squares 16 bits of each number, so
     * 0x10003 is 3 to it */
    CHECK_EQ(math_Length(3, 4, 12), 13);
    CHECK_EQ(math_LengthSquared(3, 4, 12), 169);
    CHECK_EQ(math_LengthSquared(0x10003, -4, 12), 169);
    CHECK_EQ(math_Length(ONE, 0, 0), ONE);
    CHECK_EQ(Lzc(1), 31);
    CHECK_EQ(Lzc(-1), 32);
}

/* ---- rotations ---- */

static void test_Rotations(void) {
    static MATRIX matrix;
    static MATRIX turned;
    SVECTOR angles;
    s32 i;

    for (i = 0; i < 200; i++) {
        angles.vx = (s16)test_Random(13);
        angles.vy = (s16)test_Random(13);
        angles.vz = (s16)test_Random(13);
        test_Print("angles %d %d %d\n", angles.vx, angles.vy, angles.vz);
        RotMatrix(&angles, &matrix);
        test_PrintMatrix("xyz", &matrix);
        RotMatrixZYX(&angles, &matrix);
        test_PrintMatrix("zyx", &matrix);
        math_RotMatrixZYX(&angles, &turned);
        test_PrintMatrix("game", &turned);
        /* one axis at a time, onto the matrix just made */
        turned = matrix;
        RotMatrixX(angles.vy, &turned);
        test_PrintMatrix("x", &turned);
        turned = matrix;
        RotMatrixY(angles.vz, &turned);
        test_PrintMatrix("y", &turned);
        turned = matrix;
        RotMatrixZ(angles.vx, &turned);
        test_PrintMatrix("z", &turned);
    }

    /* No turn at all is the matrix of ones; a quarter turn about z
     * takes x to y. */
    angles.vx = angles.vy = angles.vz = 0;
    RotMatrix(&angles, &matrix);
    CHECK_EQ(matrix.m[0][0], ONE);
    CHECK_EQ(matrix.m[1][1], ONE);
    CHECK_EQ(matrix.m[2][2], ONE);
    CHECK_EQ(matrix.m[0][1], 0);
    CHECK_EQ(matrix.m[1][2], 0);
    angles.vz = ANGLE_TURN / 4;
    RotMatrix(&angles, &matrix);
    CHECK_EQ(matrix.m[0][0], 0);
    CHECK_EQ(matrix.m[1][0], ONE);
    CHECK_EQ(matrix.m[0][1], -ONE);
    math_RotMatrixZYX(&angles, &matrix);
    CHECK_EQ(matrix.m[0][0], 0);
    CHECK_EQ(matrix.m[1][0], ONE);
    CHECK_EQ(matrix.m[0][1], -ONE);
    CHECK_EQ(matrix.m[2][2], ONE);
}

/* ---- products ---- */

static void test_MakeMatrix(MATRIX *matrix, s32 bits) {
    s32 i;

    for (i = 0; i < 9; i++) {
        ((s16 *)matrix->m)[i] = (s16)test_Random(bits);
    }
    ((u16 *)matrix)[9] = 0x5A5A;
    for (i = 0; i < 3; i++) {
        matrix->t[i] = test_Random(14);
    }
}

static void test_Products(void) {
    static const MATRIX identity = {
        {{ONE, 0, 0}, {0, ONE, 0}, {0, 0, ONE}}, {0, 0, 0}};
    static MATRIX a;
    static MATRIX b;
    static MATRIX product;
    static MATRIX again;
    VECTOR scale;
    VECTOR point;
    VECTOR result;
    SVECTOR small;
    s32 i;

    for (i = 0; i < 100; i++) {
        test_MakeMatrix(&a, 13);
        test_MakeMatrix(&b, 13);
        test_PrintMatrix("a", &a);
        test_PrintMatrix("b", &b);
        test_Print("translations %d %d %d %d %d %d\n", a.t[0], a.t[1], a.t[2],
                   b.t[0], b.t[1], b.t[2]);
        test_PrintMatrix("product", MulMatrix0(&a, &b, &product));
        /* the same product by the game's routine, by Sony's two that
         * write over an argument, and with the translations */
        math_MulMatrix(&a, &b, &again);
        CHECK(test_BytesEqual(again.m, product.m, sizeof(product.m)));
        again = a;
        MulMatrix(&again, &b);
        CHECK(test_BytesEqual(again.m, product.m, sizeof(product.m)));
        again = b;
        MulMatrix2(&a, &again);
        CHECK(test_BytesEqual(again.m, product.m, sizeof(product.m)));
        CompMatrix(&a, &b, &again);
        CHECK(test_BytesEqual(again.m, product.m, sizeof(product.m)));
        test_Print("composed %d %d %d\n", again.t[0], again.t[1], again.t[2]);

        scale.vx = test_Random(14);
        scale.vy = test_Random(14);
        scale.vz = test_Random(14);
        test_Print("scale %d %d %d\n", scale.vx, scale.vy, scale.vz);
        again = a;
        test_PrintMatrix("columns", ScaleMatrix(&again, &scale));
        again = a;
        test_PrintMatrix("rows", ScaleMatrixL(&again, &scale));

        /* a 32-bit vector through `a`, and a 16-bit one */
        point.vx = test_Random(26);
        point.vy = test_Random(26);
        point.vz = test_Random(26);
        ApplyMatrixLV(&a, &point, &result);
        test_Print("long %d %d %d %d %d %d\n", point.vx, point.vy, point.vz,
                   result.vx, result.vy, result.vz);
        small.vx = (s16)test_Random(12);
        small.vy = (s16)test_Random(12);
        small.vz = (s16)test_Random(12);
        ApplyMatrix(&a, &small, &result);
        test_Print("short %d %d %d %d %d %d\n", small.vx, small.vy, small.vz,
                   result.vx, result.vy, result.vz);
        /* the game's, which adds the translation */
        math_Transform(&a, &small, &point);
        CHECK_EQ(point.vx, result.vx + a.t[0]);
        CHECK_EQ(point.vy, result.vy + a.t[1]);
        CHECK_EQ(point.vz, result.vz + a.t[2]);
    }

    /* Sony's products store their ninth number as a word: the two bytes
     * after the matrix's numbers get its sign. The game's does not. */
    a = identity;
    b = identity;
    b.m[2][2] = -ONE;
    test_MakeMatrix(&product, 13);
    MulMatrix0(&a, &b, &product);
    CHECK_EQ(product.m[2][2], -ONE);
    CHECK_EQ(((u16 *)&product)[9], 0xFFFF);
    test_MakeMatrix(&product, 13);
    math_MulMatrix(&a, &b, &product);
    CHECK_EQ(product.m[2][2], -ONE);
    CHECK_EQ(((u16 *)&product)[9], 0x5A5A);
    /* It leaves the first matrix in the GTE, with no translation. */
    CHECK_EQ(port_GteReadControl(PORT_GTE_ROTATION), ONE);
    CHECK_EQ(port_GteReadControl(PORT_GTE_TRX), 0);

    /* (1, 2, 3) x (4, 5, 6) = (-3, 6, -3); the rotation's diagonal,
     * which the GTE borrows for it, is as it was. */
    port_GteWriteControl(PORT_GTE_ROTATION, 0x12345678);
    port_GteWriteControl(PORT_GTE_ROTATION + 2, 0x0BCD0ABC);
    scale.vx = 1;
    scale.vy = 2;
    scale.vz = 3;
    point.vx = 4;
    point.vy = 5;
    point.vz = 6;
    OuterProduct0(&scale, &point, &result);
    CHECK_EQ(result.vx, -3);
    CHECK_EQ(result.vy, 6);
    CHECK_EQ(result.vz, -3);
    CHECK_EQ(port_GteReadControl(PORT_GTE_ROTATION), 0x12345678);
    CHECK_EQ(port_GteReadControl(PORT_GTE_ROTATION + 2), 0x0BCD0ABC);
}

/* ---- blends ---- */

static void test_Blends(void) {
    static MATRIX first;
    static MATRIX second;
    static MATRIX blend;
    SVECTOR angles;
    SVECTOR from;
    SVECTOR to;
    SVECTOR between;
    VECTOR a;
    VECTOR b;
    VECTOR average;
    s32 weight;
    s32 i;

    /* Two rotations blended are a rotation: the Python side checks the
     * rows' lengths and that they are square to each other. */
    for (i = 0; i < 100; i++) {
        angles.vx = (s16)test_Random(13);
        angles.vy = (s16)test_Random(13);
        angles.vz = (s16)test_Random(13);
        RotMatrix(&angles, &first);
        angles.vx += (s16)test_Random(10);
        angles.vy += (s16)test_Random(10);
        angles.vz += (s16)test_Random(10);
        RotMatrix(&angles, &second);
        weight = test_Random(13) & 0xFFF;
        math_LerpRotation(&first, &second, weight, &blend);
        test_Print("weight %d\n", weight);
        test_PrintMatrix("first", &first);
        test_PrintMatrix("second", &second);
        test_PrintMatrix("blend", &blend);
    }

    /* A quarter of the way from (0, 100, -100) to (400, 100, 300). */
    from.vx = 0;
    from.vy = 100;
    from.vz = -100;
    to.vx = 400;
    to.vy = 100;
    to.vz = 300;
    math_LerpSVector(&from, &to, ONE / 4, &between);
    CHECK_EQ(between.vx, 100);
    CHECK_EQ(between.vy, 100);
    CHECK_EQ(between.vz, 0);
    /* Sony's: 3 of the one and half of the other. */
    a.vx = 10;
    a.vy = -20;
    a.vz = 30;
    b.vx = 100;
    b.vy = 200;
    b.vz = -300;
    LoadAverage12(&a, &b, 3 * ONE, ONE / 2, &average);
    CHECK_EQ(average.vx, 80);
    CHECK_EQ(average.vy, 40);
    CHECK_EQ(average.vz, -60);
    /* Two colours, half of each. */
    a.vx = 0x00402000;
    b.vx = 0x00C06080;
    average.vx = 0x77777777;
    LoadAverageCol(
        (u8 *)&a.vx, (u8 *)&b.vx, ONE / 2, ONE / 2, (u8 *)&average.vx);
    CHECK_EQ(average.vx, 0x77804040);
}

/* ---- the rest ---- */

static void test_Others(void) {
    u32 words[12];
    s16 frame[9] = {10, 20, 30, ONE, 0, 0, 0, 0, ONE};
    CVECTOR color;
    s32 u;
    s32 v;
    s32 i;

    /* Three words moved one word up: the last is moved first. */
    for (i = 0; i < 12; i++) {
        words[i] = 100 + i;
    }
    mem_MoveWords((s32)&words[2], (s32)&words[3], 12);
    CHECK_EQ(words[2], 102);
    CHECK_EQ(words[3], 102);
    CHECK_EQ(words[4], 103);
    CHECK_EQ(words[5], 104);
    CHECK_EQ(words[6], 106);
    /* And two words back down again; 5 bytes are two words. */
    mem_MoveWords((s32)&words[4], (s32)&words[3], 5);
    CHECK_EQ(words[2], 102);
    CHECK_EQ(words[3], 103);
    CHECK_EQ(words[4], 104);
    CHECK_EQ(words[5], 104);
    /* Nothing for no bytes. */
    mem_MoveWords((s32)&words[0], (s32)&words[8], 0);
    CHECK_EQ(words[8], 108);

    /* A depth below 0x800 is its own entry; above, the entries are
     * twice as far apart with every doubling. `shift` is 3 for the
     * depth as it is, less for a depth with fewer bits. */
    CHECK_EQ(render_DepthToOtShifted(0, 3), 0);
    CHECK_EQ(render_DepthToOtShifted(0x7FF, 3), 0x7FF);
    CHECK_EQ(render_DepthToOtShifted(0x800, 3), 0x800);
    CHECK_EQ(render_DepthToOtShifted(0x802, 3), 0x801);
    CHECK_EQ(render_DepthToOtShifted(0x1000, 3), 0xC00);
    CHECK_EQ(render_DepthToOtShifted(0x4000, 0), 0x800);

    /* A frame at (10, 20, 30) whose axes are x and z: the point's place
     * on it, a quarter of each. */
    collision_ProjectOnFace((s32)frame, 110, 20, 430);
    collision_ReadProjection(&u, &v);
    CHECK_EQ(u, 25);
    CHECK_EQ(v, 100);

    SetFarColor(0x12, 0x34, 0x56);
    gte_ReadFarColor(&color);
    CHECK_EQ(color.r, 0x12);
    CHECK_EQ(color.g, 0x34);
    CHECK_EQ(color.b, 0x56);
    SetBackColor(1, 2, 3);
    CHECK_EQ(port_GteReadControl(PORT_GTE_GBK), 2 << 4);
    SetGeomOffset(160, 120);
    CHECK_EQ(port_GteReadControl(PORT_GTE_OFY), 120 << 16);
    SetGeomScreen(500);
    CHECK_EQ(ReadGeomScreen(), 500);

    /* What the library starts the GTE with. */
    InitGeom();
    CHECK_EQ(port_GteReadControl(PORT_GTE_ZSF3), 0x155);
    CHECK_EQ(port_GteReadControl(PORT_GTE_ZSF4), 0x100);
    CHECK_EQ(ReadGeomScreen(), 1000);
    CHECK_EQ(port_GteReadControl(PORT_GTE_DQA), -0x1062);
    CHECK_EQ(port_GteReadControl(PORT_GTE_DQB), 0x1400000);
    CHECK_EQ(port_GteReadControl(PORT_GTE_OFX), 0);
    CHECK_EQ(port_GteReadControl(PORT_GTE_OFY), 0);
    CHECK_EQ(AverageZ3(300, 300, 300), 0x155 * 900 >> 12);

    CHECK_EQ(g_test_traps, 0);
    sys_Trap();
    CHECK_EQ(g_test_traps, 1);
}

void test_Main(void) {
    test_Roots();
    test_Rotations();
    test_Products();
    test_Blends();
    test_Others();
}
