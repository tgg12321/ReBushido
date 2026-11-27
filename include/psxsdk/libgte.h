#ifndef _LIBGTE_H_
#define _LIBGTE_H_

/* Sony's libgte.h (Run-time Library Release 4.0): the geometry library's
 * vectors and matrices, and its functions as far as the game links them.
 * The GTE instruction macros are Sony's files inline_c.h, inline_o.h and
 * gtemac.h, kept as shipped. */

#include "common.h"

/*
 * Geometry Structures:
 */
#define ONE 4096 /* GTE regards 4096 as 1.0 */

typedef struct {
    s16 m[3][3]; /* 3x3 rotation matrix */
    s32 t[3];    /* transfer vector */
} MATRIX;

typedef struct { /* long word type 3D vector */
    s32 vx, vy;
    s32 vz, pad;
} VECTOR;

typedef struct { /* short word type 3D vector */
    s16 vx, vy;
    s16 vz, pad;
} SVECTOR;

typedef struct { /* color type vector */
    u8 r, g, b, cd;
} CVECTOR;

typedef struct { /* 2D short vector */
    s16 vx, vy;
} DVECTOR;

/*
 *      ProtoTypes
 */
extern void InitGeom();

extern MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2);
extern MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1);
extern MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1);

extern VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1);
extern VECTOR *ApplyRotMatrix(SVECTOR *v0, VECTOR *v1);
extern VECTOR *ApplyRotMatrixLV(VECTOR *v0, VECTOR *v1);
extern VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1);
extern MATRIX *RotMatrix(SVECTOR *r, MATRIX *m);
extern MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m);
extern MATRIX *RotMatrixX(s32 r, MATRIX *m);
extern MATRIX *RotMatrixY(s32 r, MATRIX *m);
extern MATRIX *RotMatrixZ(s32 r, MATRIX *m);
extern MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v);
extern MATRIX *ScaleMatrixL(MATRIX *m, VECTOR *v);
extern MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2);

extern void SetRotMatrix(MATRIX *m);
extern void SetColorMatrix(MATRIX *m);
extern void SetTransMatrix(MATRIX *m);

extern void SetBackColor(s32 rbk, s32 gbk, s32 bbk);
extern void SetFarColor(s32 rfc, s32 gfc, s32 bfc);
extern void SetGeomOffset(s32 ofx, s32 ofy);
extern void SetGeomScreen(s32 h);
extern void ReadSZfifo3(s32 *sz0, s32 *sz1, s32 *sz2);
extern s32 ReadGeomScreen();

extern s32 RotTransPers(SVECTOR *v0, s32 *sxy, s32 *p, s32 *flag);
extern s32 RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, s32 *sxy0,
                         s32 *sxy1, s32 *sxy2, s32 *p, s32 *flag);
extern void RotTrans(SVECTOR *v0, VECTOR *v1, s32 *flag);
extern void LightColor(VECTOR *v0, VECTOR *v1);
extern void DpqColorLight(VECTOR *v0, CVECTOR *v1, s32 p, CVECTOR *v2);
extern void DpqColor3(CVECTOR *v0, CVECTOR *v1, CVECTOR *v2, s32 p, CVECTOR *v3,
                      CVECTOR *v4, CVECTOR *v5);
extern void Intpl(VECTOR *v0, s32 p, CVECTOR *v1);
extern VECTOR *Square12(VECTOR *v0, VECTOR *v1);
extern VECTOR *Square0(VECTOR *v0, VECTOR *v1);
extern s32 AverageZ3(s32 sz0, s32 sz1, s32 sz2);
extern s32 AverageZ4(s32 sz0, s32 sz1, s32 sz2, s32 sz3);
extern void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2);
extern void OuterProduct0(VECTOR *v0, VECTOR *v1, VECTOR *v2);
extern s32 Lzc(s32 data);

extern s32 RotTransPers4(
    SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, s32 *sxy0, s32 *sxy1,
    s32 *sxy2, s32 *sxy3, s32 *p, s32 *flag);

extern void LoadAverage12(VECTOR *v0, VECTOR *v1, s32 p0, s32 p1, VECTOR *v2);
extern void LoadAverageShort12(
    SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2);
extern void LoadAverage0(VECTOR *v0, VECTOR *v1, s32 p0, s32 p1, VECTOR *v2);
extern void LoadAverageShort0(
    SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2);
extern void LoadAverageByte(u8 *v0, u8 *v1, s32 p0, s32 p1, u8 *v2);
extern void LoadAverageCol(u8 *v0, u8 *v1, s32 p0, s32 p1, u8 *v2);

extern s32 SquareRoot0(s32 a);
extern s32 SquareRoot12(s32 a);

extern s32 rcos(s32 a);
extern s32 rsin(s32 a);
/* their table of the first quarter turn (SINCOS) */
extern s16 rsin_tbl[];

extern s32 ratan2(s32 y, s32 x);

#endif /* _LIBGTE_H_ */
