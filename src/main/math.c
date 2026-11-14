/* Fixed-point maths: colour conversion, and rotation matrices from three
 * angles in each order of axes the game uses. Angles are 12 bits to the
 * turn (ANGLE_TURN); 1.0 is 0x1000 (ONE). The sine table is in math_sin.c. */
#include "common.h"
#include "bb2.h"

RotMatrixFunc g_math_rot_matrix_funcs[6];

/* The hue goes round in six parts, from one of red, yellow, green, cyan,
 * blue and magenta to the next. */
#define HUE_SEXTANTS 6

/* Hue, saturation and value to red, green and blue, each 0..ONE. In black
 * and white the saturation is taken as none. In each sextant one part of the
 * colour is the value, one the least there is at this saturation, and the
 * third rises or falls between them. Nothing calls it. */
static void math_HsvToRgb(s32 *hsv_input, s32 *rgb_output) {
    s32 hue = hsv_input[0];
    s32 saturation = hsv_input[1];
    s32 value = hsv_input[2];
    s32 sextant, fraction, least, falling, rising;
    s32 r, g, b;

    if (tex_IsGrayscale()) {
        saturation = 0;
    }
    if (saturation == 0) {
        r = value;
        g = value;
        b = value;
    } else {
        if (hue == ONE) {
            hue = 0;
        }
        least = value * (ONE - saturation);
        least = least >> 12;
        hue = hue * HUE_SEXTANTS;
        fraction = hue & (ONE - 1);
        sextant = hue >> 12;
        falling = value * (ONE - ((saturation * fraction) >> 12));
        falling = falling >> 12;
        rising = value * (ONE - ((saturation * (ONE - fraction)) >> 12));
        rising = rising >> 12;

        switch (sextant) {
        case 0: /* red to yellow */
            r = value;
            g = rising;
            b = least;
            break;
        case 1: /* yellow to green */
            r = falling;
            g = value;
            b = least;
            break;
        case 2: /* green to cyan */
            r = least;
            g = value;
            b = rising;
            break;
        case 3: /* cyan to blue */
            r = least;
            g = falling;
            b = value;
            break;
        case 4: /* blue to magenta */
            r = rising;
            g = least;
            b = value;
            break;
        case 5: /* magenta to red */
            r = value;
            g = least;
            b = falling;
            break;
        }
    }
    rgb_output[0] = r;
    rgb_output[1] = g;
    rgb_output[2] = b;
}

/* Red, green and blue to hue, saturation and value: the inverse of
 * math_HsvToRgb. Nothing calls it. */
static void math_RgbToHsv(s32 *rgb_input, s32 *hsv_output) {
    s32 r, g, b;
    s32 highest, lowest;
    s32 chroma;
    s32 saturation;
    s32 hue;
    /* how far each part is under the highest, as a share of the chroma */
    s32 red_gap, green_gap, blue_gap;
    s32 value;

    r = rgb_input[0];
    g = rgb_input[1];
    b = rgb_input[2];

    /* the highest of the three */
    if (r >= g) {
        if (r >= b) {
            highest = r;
        } else {
            highest = b;
        }
    } else {
        if (g >= b) {
            highest = g;
        } else {
            highest = b;
        }
    }

    /* the lowest */
    if (g >= r) {
        if (b >= r) {
            lowest = r;
        } else {
            lowest = b;
        }
    } else {
        if (b >= g) {
            lowest = g;
        } else {
            lowest = b;
        }
    }

    chroma = highest - lowest;
    value = highest;

    if (value != 0) {
        saturation = (chroma << 12) / value;
    } else {
        saturation = 0;
    }

    if (saturation != 0) {
        red_gap = ((highest - r) << 12) / chroma;
        green_gap = ((highest - g) << 12) / chroma;
        blue_gap = ((highest - b) << 12) / chroma;

        if (r == highest) {
            hue = blue_gap - green_gap;
        } else if (g == highest) {
            /* two sextants on, from green */
            s32 back = blue_gap - 2 * ONE;
            hue = red_gap - back;
        } else if (b == highest) {
            /* four on, from blue */
            s32 back = red_gap - 4 * ONE;
            hue = green_gap - back;
        }

        hue /= HUE_SEXTANTS;

        if (hue < 0) {
            hue += ONE;
        }
    } else {
        hue = 0;
    }

    hsv_output[0] = hue;
    hsv_output[1] = saturation;
    hsv_output[2] = value;
}

/* A flag that nothing sets or reads but these two, which nothing calls. */
static void math_SetUnusedFlag(s32 on) {
    if (on) {
        g_scene_settings.unused_flag = 1;
    } else {
        g_scene_settings.unused_flag = 0;
    }
}

/* A flag (g_scene_settings.unused_flag) with a setter and a getter that
 * nothing calls. */
static s32 math_GetUnusedFlag(void) { return g_scene_settings.unused_flag; }

/* The three functions below each write the rotation part of `matrix` from
 * three angles: about x (a), y (b) and z (c), the three turns multiplied in
 * the order of the name. Every entry is a product of sines and cosines out
 * of the table, worked out in the locals named for them: a product of two is
 * ONE times ONE, and `_12` in a name marks where it has been brought back
 * down. This one is Rz * Rx * Ry: it turns about z, then about x and y as
 * the turns before have left them; a point it is applied to is turned about
 * the fixed y first and the fixed z last. */
void math_RotMatrixZXY(SVECTOR *angles, MATRIX *matrix) {
    u16 angle_a, angle_b;
    s16 sin_a, sin_b, sin_c;
    s16 cos_b, cos_c;
    s32 neg_sin_a_sin_b_12;
    s32 neg_sin_a_sin_b_12_sin_c, cos_b_cos_c;
    s32 sin_a_cos_b;
    s32 sin_a_cos_b_12;
    s32 cos_a_neg_sin_c;
    s32 sin_a_cos_b_12_sin_c, sin_b_cos_c;
    s32 sin_a_sin_b;
    s32 sin_a_sin_b_12;
    s32 sin_a_sin_b_12_cos_c;
    s32 cos_b_sin_c;
    s32 neg_sin_a_cos_b;
    s32 cos_a_cos_c;
    s32 neg_sin_a_cos_b_12;
    s32 neg_sin_a_cos_b_12_cos_c;
    s32 cos_a_neg_sin_b;
    s32 cos_a_cos_b;
    s32 sin_b_sin_c;
    s32 cos_a;
    s16 angle_c;
    s32 index_b;
    s16 cos_a_table;

    angle_a = angles->vx;
    angle_b = angles->vy;

    sin_a = g_math_sin_table[angle_a & ANGLE_MASK];
    sin_b = g_math_sin_table[angle_b & ANGLE_MASK];

    index_b = (s16)angle_b + ANGLE_TURN / 4;

    neg_sin_a_sin_b_12 = (sin_a * -sin_b) >> 12;

    angle_c = angles->vz;
    sin_c = g_math_sin_table[angle_c & ANGLE_MASK];

    neg_sin_a_sin_b_12_sin_c = neg_sin_a_sin_b_12 * sin_c;

    cos_b = g_math_sin_table[index_b & ANGLE_MASK];
    cos_c = g_math_sin_table[(angle_c + ANGLE_TURN / 4) & ANGLE_MASK];

    cos_b_cos_c = cos_b * cos_c;

    sin_a_cos_b = sin_a * cos_b;

    cos_a_table =
        g_math_sin_table[((s16)angle_a + ANGLE_TURN / 4) & ANGLE_MASK];
    matrix->m[2][1] = sin_a;
    cos_a = cos_a_table;

    cos_a_neg_sin_c = cos_a * -sin_c;

    sin_a_cos_b_12 = sin_a_cos_b >> 12;
    sin_a_cos_b_12_sin_c = sin_a_cos_b_12 * sin_c;

    sin_a_sin_b = sin_a * sin_b;
    sin_b_cos_c = sin_b * cos_c;

    sin_a_sin_b_12 = sin_a_sin_b >> 12;
    sin_a_sin_b_12_cos_c = sin_a_sin_b_12 * cos_c;

    cos_b_sin_c = cos_b * sin_c;

    neg_sin_a_cos_b = -sin_a * cos_b;

    cos_a_cos_c = cos_a * cos_c;

    neg_sin_a_cos_b_12 = neg_sin_a_cos_b >> 12;
    neg_sin_a_cos_b_12_cos_c = neg_sin_a_cos_b_12 * cos_c;

    cos_a_neg_sin_b = cos_a * -sin_b;

    cos_a_cos_b = cos_a * cos_b;

    matrix->m[0][1] = cos_a_neg_sin_c >> 12;
    matrix->m[1][1] = cos_a_cos_c >> 12;
    matrix->m[2][0] = cos_a_neg_sin_b >> 12;
    matrix->m[2][2] = cos_a_cos_b >> 12;
    matrix->m[0][0] = (neg_sin_a_sin_b_12_sin_c + cos_b_cos_c) >> 12;
    matrix->m[0][2] = (sin_a_cos_b_12_sin_c + sin_b_cos_c) >> 12;
    matrix->m[1][0] = (sin_a_sin_b_12_cos_c + cos_b_sin_c) >> 12;

    sin_b_sin_c = sin_b * sin_c;
    matrix->m[1][2] = (neg_sin_a_cos_b_12_cos_c + sin_b_sin_c) >> 12;
}

/* About y, then x, then z: Ry * Rx * Rz. */
void math_RotMatrixYXZ(SVECTOR *angles, MATRIX *matrix) {
    u16 angle_a, angle_b;
    s16 sin_a, sin_b, sin_c;
    s16 cos_b, cos_c;
    s32 index_b;
    s32 sin_a_sin_b_12;
    s32 sin_a_sin_b_12_sin_c, sin_a_sin_b_12_cos_c;
    s32 cos_b_cos_c, cos_b_neg_sin_c;
    s32 cos_a_sin_b, cos_a_sin_c, cos_a_cos_c, cos_a_cos_b;
    s32 sin_a_cos_b;
    s32 sin_a_cos_b_12;
    s32 sin_a_cos_b_12_sin_c, neg_sin_b_cos_c;
    s32 sin_a_cos_b_12_cos_c;
    s32 sin_b_sin_c;
    s32 cos_a;
    s16 cos_a_table;
    s16 angle_c;

    angle_a = angles->vx;
    angle_b = angles->vy;

    sin_a = g_math_sin_table[angle_a & ANGLE_MASK];
    sin_b = g_math_sin_table[angle_b & ANGLE_MASK];
    index_b = (s16)angle_b + ANGLE_TURN / 4;

    sin_a_sin_b_12 = (sin_a * sin_b) >> 12;

    angle_c = angles->vz;
    sin_c = g_math_sin_table[angle_c & ANGLE_MASK];

    sin_a_sin_b_12_sin_c = sin_a_sin_b_12 * sin_c;

    cos_b = g_math_sin_table[index_b & ANGLE_MASK];
    cos_c = g_math_sin_table[(angle_c + ANGLE_TURN / 4) & ANGLE_MASK];

    cos_b_cos_c = cos_b * cos_c;
    sin_a_sin_b_12_cos_c = sin_a_sin_b_12 * cos_c;

    cos_a_table =
        g_math_sin_table[((s16)angle_a + ANGLE_TURN / 4) & ANGLE_MASK];
    matrix->m[1][2] = -sin_a;
    cos_a = cos_a_table;

    cos_b_neg_sin_c = cos_b * -sin_c;

    cos_a_sin_b = cos_a * sin_b;
    cos_a_sin_c = cos_a * sin_c;

    sin_a_cos_b = sin_a * cos_b;
    cos_a_cos_c = cos_a * cos_c;

    sin_a_cos_b_12 = sin_a_cos_b >> 12;

    sin_a_cos_b_12_sin_c = sin_a_cos_b_12 * sin_c;
    neg_sin_b_cos_c = -sin_b * cos_c;

    sin_a_cos_b_12_cos_c = sin_a_cos_b_12 * cos_c;

    cos_a_cos_b = cos_a * cos_b;

    matrix->m[0][2] = cos_a_sin_b >> 12;
    matrix->m[1][0] = cos_a_sin_c >> 12;
    matrix->m[1][1] = cos_a_cos_c >> 12;
    matrix->m[2][2] = cos_a_cos_b >> 12;
    matrix->m[0][0] = (sin_a_sin_b_12_sin_c + cos_b_cos_c) >> 12;
    matrix->m[0][1] = (sin_a_sin_b_12_cos_c + cos_b_neg_sin_c) >> 12;
    matrix->m[2][0] = (sin_a_cos_b_12_sin_c + neg_sin_b_cos_c) >> 12;

    sin_b_sin_c = sin_b * sin_c;
    matrix->m[2][1] = (sin_a_cos_b_12_cos_c + sin_b_sin_c) >> 12;
}

/* About x, then y, then z: Rx * Ry * Rz. */
static void math_RotMatrixXYZ(SVECTOR *angles, MATRIX *matrix) {
    u16 angle_a, angle_b, angle_c;
    s16 sin_a, sin_b, sin_c, cos_a;
    s32 cos_b, cos_c;
    s32 cos_b_cos_c, cos_b_neg_sin_c;
    s32 sin_a_sin_b, sin_a_sin_b_12;
    s32 sin_a_sin_b_12_cos_c, sin_a_sin_b_12_neg_sin_c;
    s32 cos_a_sin_c, cos_a_cos_c;
    s32 cos_a_neg_sin_b;
    s32 neg_sin_a_cos_b;
    s32 cos_a_neg_sin_b_12, cos_a_neg_sin_b_12_cos_c;
    s32 cos_a_sin_b;
    s32 sin_a_sin_c;
    s32 cos_a_sin_b_12_sin_c;
    s32 cos_a_cos_b;
    s32 sin_a_cos_c;

    angle_b = angles->vy;
    angle_c = angles->vz;
    sin_b = g_math_sin_table[angle_b & ANGLE_MASK];
    angle_a = angles->vx;
    sin_a = g_math_sin_table[angle_a & ANGLE_MASK];
    sin_c = g_math_sin_table[angle_c & ANGLE_MASK];
    cos_a = g_math_sin_table[((s16)angle_a + ANGLE_TURN / 4) & ANGLE_MASK];

    cos_b = g_math_sin_table[((s16)angle_b + ANGLE_TURN / 4) & ANGLE_MASK];
    cos_c = g_math_sin_table[((s16)angle_c + ANGLE_TURN / 4) & ANGLE_MASK];

    cos_b_cos_c = cos_b * cos_c;
    matrix->m[0][2] = sin_b;
    sin_a_sin_b = sin_a * sin_b;
    cos_b_neg_sin_c = cos_b * -sin_c;
    sin_a_sin_b_12 = sin_a_sin_b >> 12;
    sin_a_sin_b_12_cos_c = sin_a_sin_b_12 * cos_c;
    cos_a_sin_c = cos_a * sin_c;
    sin_a_sin_b_12_neg_sin_c = sin_a_sin_b_12 * -sin_c;
    cos_a_cos_c = cos_a * cos_c;
    cos_a_neg_sin_b = cos_a * -sin_b;
    neg_sin_a_cos_b = -sin_a * cos_b;
    cos_a_neg_sin_b_12 = cos_a_neg_sin_b >> 12;
    cos_a_neg_sin_b_12_cos_c = cos_a_neg_sin_b_12 * cos_c;
    cos_a_sin_b = cos_a * sin_b;
    sin_a_sin_c = sin_a * sin_c;
    cos_a_sin_b_12_sin_c = (cos_a_sin_b >> 12) * sin_c;
    cos_a_cos_b = cos_a * cos_b;

    matrix->m[0][0] = cos_b_cos_c >> 12;
    matrix->m[0][1] = cos_b_neg_sin_c >> 12;
    matrix->m[1][2] = neg_sin_a_cos_b >> 12;
    sin_a_cos_c = sin_a * cos_c;
    matrix->m[2][2] = cos_a_cos_b >> 12;
    matrix->m[1][0] = (sin_a_sin_b_12_cos_c + cos_a_sin_c) >> 12;
    matrix->m[1][1] = (sin_a_sin_b_12_neg_sin_c + cos_a_cos_c) >> 12;
    matrix->m[2][0] = (cos_a_neg_sin_b_12_cos_c + sin_a_sin_c) >> 12;
    matrix->m[2][1] = (cos_a_sin_b_12_sin_c + sin_a_cos_c) >> 12;
}

/* Fills in the table of rotation functions (enum RotationOrder). */
void math_InitRotMatrixFuncs(void) {
    g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX] = math_RotMatrixZYX;
    g_math_rot_matrix_funcs[ROTATION_ORDER_ZXY] = math_RotMatrixZXY;
    g_math_rot_matrix_funcs[ROTATION_ORDER_YXZ] = math_RotMatrixYXZ;
    g_math_rot_matrix_funcs[ROTATION_ORDER_XYZ] = math_RotMatrixXYZ;
}

/* Swaps a matrix's rows and columns, which turns a rotation into its
 * inverse. */
void math_Transpose(MATRIX *matrix) {
    s16 swap;

    swap = matrix->m[2][0];
    matrix->m[2][0] = matrix->m[0][2];
    matrix->m[0][2] = swap;
    swap = matrix->m[1][0];
    matrix->m[1][0] = matrix->m[0][1];
    matrix->m[0][1] = swap;
    swap = matrix->m[2][1];
    matrix->m[2][1] = matrix->m[1][2];
    matrix->m[1][2] = swap;
}

/* Turns the point (*x_ptr, *y_ptr) about the origin by `angle`. Nothing
 * calls it. */
static void math_Rotate2D(s32 *x_ptr, s32 *y_ptr, s32 angle) {
    s16 cosine, sine;
    s32 x, y;
    s32 x_cosine, x_sine, y_cosine, y_sine;
    cosine = g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK];
    x = *x_ptr;
    sine = g_math_sin_table[angle & ANGLE_MASK];
    y = *y_ptr;
    x_cosine = cosine * x;
    x_sine = sine * x;
    y_cosine = cosine * y;
    y_sine = sine * y;
    *y_ptr = (x_sine + y_cosine) >> 12;
    *x_ptr = (x_cosine - y_sine) >> 12;
}

/* Works out the three angles of a rotation matrix: the angles that
 * math_RotMatrixYXZ would make `matrix` from. The matrix is turned back by
 * the first two as they are found, and is changed. */
void math_MatrixToAnglesYXZ(MATRIX *matrix, SVECTOR *angles) {
    SVECTOR work; /* an axis to turn, then the angles to turn back by */
    VECTOR turned;
    MATRIX undo;
    s16 yaw;
    s32 cos_yaw, sin_yaw;
    s16 pitch;
    s32 rotated_z;

    /* where z ends up gives the yaw and the pitch */
    work.vx = 0;
    work.vy = 0;
    work.vz = ONE;
    ApplyMatrix(matrix, &work, &turned);

    yaw = ratan2(turned.vx, turned.vz);

    cos_yaw = rcos(yaw);
    sin_yaw = rsin(yaw);

    rotated_z = (cos_yaw * turned.vz + sin_yaw * turned.vx) >> 12;
    pitch = -ratan2(turned.vy, rotated_z);

    work.vx = -pitch;
    work.vy = -yaw;
    work.vz = 0;
    math_RotMatrixXYZ(&work, &undo);

    MulMatrix(&undo, matrix);

    /* and with those two undone, where y ends up gives the roll */
    work.vx = 0;
    work.vy = ONE;
    work.vz = 0;
    ApplyMatrix(&undo, &work, &turned);

    {
        s16 roll;
        roll = ratan2(turned.vx, turned.vy);
        angles->vx = pitch;
        angles->vy = yaw;
        angles->vz = -roll;
    }
}
