/* LIBGPU EXT: loading a texture page or a colour table into the frame
 * buffer, and the default drawing and display environments. */
#include "common.h"
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>

u16 LoadTPage(u32 *pix, s32 tp, s32 abr, s32 x, s32 y, s32 w, s32 h) {
    RECT buf;
    buf.x = x;
    buf.h = h;
    buf.y = y;
    switch (tp) {
    case 0:
        buf.w = w / 4;
        break;
    case 1:
        buf.w = w / 2;
        break;
    case 2:
        buf.w = w;
        break;
    }
    LoadImage(&buf, pix);
    return GetTPage(tp, abr, x, y);
}

u16 LoadClut(u32 *clut, s32 x, s32 y) {
    RECT buf;
    buf.x = x;
    buf.y = y;
    buf.w = 256;
    buf.h = 1;
    LoadImage(&buf, clut);
    return GetClut(x, y);
}

u16 LoadClut2(u32 *clut, s32 x, s32 y) {
    RECT buf;
    buf.x = x;
    buf.y = y;
    buf.w = 16;
    buf.h = 1;
    LoadImage(&buf, clut);
    return GetClut(x, y);
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, s32 x, s32 y, s32 w, s32 h) {
    s32 ret;
    ret = GetVideoMode();
    env->clip.x = x;
    env->clip.y = y;
    env->clip.w = w;
    env->tw.x = 0;
    env->tw.y = 0;
    env->tw.w = 0;
    env->tw.h = 0;
    env->r0 = 0;
    env->g0 = 0;
    env->b0 = 0;
    env->dtd = 1;
    env->clip.h = h;
    /* drawing on the displayed field is allowed unless the screen is
     * interlaced: taller than 288 lines on PAL, 256 on NTSC */
    if (ret) {
        env->dfe = (h < 289);
    } else {
        env->dfe = (h < 257);
    }
    env->ofs[0] = x;
    env->ofs[1] = y;
    env->tpage = 10;
    env->isbg = 0;
    return env;
}

DISPENV *SetDefDispEnv(DISPENV *env, s32 x, s32 y, s32 w, s32 h) {
    env->disp.x = x;
    env->disp.y = y;
    env->disp.w = w;
    env->screen.x = 0;
    env->screen.y = 0;
    env->screen.w = 0;
    env->screen.h = 0;
    env->isrgb24 = 0;
    env->isinter = 0;
    env->pad1 = 0;
    env->pad0 = 0;
    env->disp.h = h;
    return env;
}
