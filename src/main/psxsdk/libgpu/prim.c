/* LIBGPU PRIM: the functions that do what libgpu.h's macros do: setting up a
 * primitive, linking it into an ordering table, printing an environment. */
#include "common.h"
#include <psxsdk/libgpu.h>
#include "psx.h"

u16 GetTPage(s32 tp, s32 abr, s32 x, s32 y) { return getTPage(tp, abr, x, y); }

u16 GetClut(s32 x, s32 y) { return getClut(x, y); }

void DumpTPage(u16 tpage) { dumpTPage(tpage); }

void DumpClut(u16 clut) { dumpClut(clut); }

void *NextPrim(void *p) { return nextPrim(p); }

s32 IsEndPrim(void *p) { return isendprim(p); }

void AddPrim(void *ot, void *p) { addPrim(ot, p); }

void AddPrims(void *ot, void *p0, void *p1) { addPrims(ot, p0, p1); }

void CatPrim(void *p0, void *p1) { catPrim(p0, p1); }

void TermPrim(void *p) { termPrim(p); }

void SetSemiTrans(void *p, s32 abe) { setSemiTrans(p, abe); }

void SetShadeTex(void *p, s32 tge) { setShadeTex(p, tge); }

void SetPolyF3(POLY_F3 *p) { setPolyF3(p); }

void SetPolyFT3(POLY_FT3 *p) { setPolyFT3(p); }

void SetPolyG3(POLY_G3 *p) { setPolyG3(p); }

void SetPolyGT3(POLY_GT3 *p) { setPolyGT3(p); }

void SetPolyF4(POLY_F4 *p) { setPolyF4(p); }

void SetPolyFT4(POLY_FT4 *p) { setPolyFT4(p); }

void SetPolyG4(POLY_G4 *p) { setPolyG4(p); }

void SetPolyGT4(POLY_GT4 *p) { setPolyGT4(p); }

void SetSprt8(SPRT_8 *p) { setSprt8(p); }

void SetSprt16(SPRT_16 *p) { setSprt16(p); }

void SetSprt(SPRT *p) { setSprt(p); }

void SetTile1(TILE_1 *p) { setTile1(p); }

void SetTile8(TILE_8 *p) { setTile8(p); }

void SetTile16(TILE_16 *p) { setTile16(p); }

void SetTile(TILE *p) { setTile(p); }

void SetLineF2(LINE_F2 *p) { setLineF2(p); }

void SetLineG2(LINE_G2 *p) { setLineG2(p); }

void SetLineF3(LINE_F3 *p) { setLineF3(p); }

void SetLineG3(LINE_G3 *p) { setLineG3(p); }

void SetLineF4(LINE_F4 *p) { setLineF4(p); }

void SetLineG4(LINE_G4 *p) { setLineG4(p); }

void SetDrawTPage(DR_TPAGE *p, s32 dfe, s32 dtd, s32 tpage) {
    setDrawTPage(p, dfe, dtd, tpage);
}

/* A primitive that copies a rectangle of the frame buffer to (x, y). The
 * rectangle's position and its size are each copied as one word. */
void SetDrawMove(DR_MOVE *p, RECT *rect, s32 x, s32 y) {
    s32 size = 5;
    if (rect->w == 0 || rect->h == 0) {
        size = 0;
    }
    p->code[0] = OT_TERMINATOR;
    p->code[1] = OT_TAG_BASE;
    setlen(p, size);
    p->code[2] = *(s32 *)&rect->x;
    p->code[3] = (y << 16) | (x & 0xFFFF);
    p->code[4] = *(s32 *)&rect->w;
}

/* A primitive that loads up to twelve words of pixels into a rectangle of
 * the frame buffer. */
void SetDrawLoad(DR_LOAD *p, RECT *rect) {
    u32 nwords;
    s32 size;
    nwords = (rect->w * rect->h + 1) / 2;
    size = nwords + 4;
    if (nwords >= 13) {
        size = 0;
    }
    setlen(p, size);
    p->code[0] = GP0_COPY_RECT_C2V;
    p->code[1] = *(u32 *)&rect->x;
    p->code[2] = *(u32 *)&rect->w;
    p->p[size - 4] = OT_TERMINATOR;
}

s32 MargePrim(void *p0, void *p1) {
    s32 size;
    size = getlen(p0) + getlen(p1) + 1;
    if (size >= 17) {
        return -1;
    }
    setlen(p0, size);
    *(u32 *)p1 = 0;
    return 0;
}

void DumpDrawEnv(DRAWENV *env) {
    GPU_printf("clip (%3d,%3d)-(%d,%d)\n", env->clip.x, env->clip.y,
               env->clip.w, env->clip.h);
    GPU_printf("ofs  (%3d,%3d)\n", env->ofs[0], env->ofs[1]);
    GPU_printf(
        "tw   (%d,%d)-(%d,%d)\n", env->tw.x, env->tw.y, env->tw.w, env->tw.h);
    GPU_printf("dtd   %d\n", env->dtd);
    GPU_printf("dfe   %d\n", env->dfe);
    dumpTPage(env->tpage);
}

void DumpDispEnv(DISPENV *env) {
    GPU_printf("disp   (%3d,%3d)-(%d,%d)\n", env->disp.x, env->disp.y,
               env->disp.w, env->disp.h);
    GPU_printf("screen (%3d,%3d)-(%d,%d)\n", env->screen.x, env->screen.y,
               env->screen.w, env->screen.h);
    GPU_printf("isinter %d\n", env->isinter);
    GPU_printf("isrgb24 %d\n", env->isrgb24);
}
