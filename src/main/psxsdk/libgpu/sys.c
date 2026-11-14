/* LIBGPU SYS: the functions that drive the GPU ($Id: sys.c,v 1.129). What
 * a program calls is on top; underneath is a queue of jobs for the GPU, run
 * one after another as each DMA transfer ends. */
#include "common.h"
#include <psxsdk/libgpu.h>
#include "psx.h"
#include <psxsdk/libc.h>
#include <psxsdk/libetc.h>

/* The functions underneath, called through a table. */
typedef struct GpuDevTable {
    const char *rcsid;
    void (*addque)();
    s32 (*addque2)();
    s32 (*clr)();
    void (*ctl)();
    s32 (*cwb)();
    void (*cwc)();
    s32 (*drs)();
    s32 (*dws)();
    s32 (*exeque)();
    u32 (*getctl)();
    s32 (*otc)();
    u32 (*param)();
    s32 (*reset)();
    u32 (*status)();
    s32 (*sync)();
} GpuDevTable;

extern GpuDevTable *gpu;

/* What the library knows of the GPU. ResetGraph clears it. */
typedef struct {
    u8 type;        /* the kind of GPU: _version's answer */
    u8 queue_mode;  /* SetGraphQueue: 1, jobs wait in the queue */
    u8 debug_level; /* SetGraphDebug */
    u8 reverse;     /* SetGraphReverse */
    s16 width;      /* the frame buffer's size */
    s16 height;
    /* a job was queued since the DrawSyncCallback was last called (volatile:
     * the interrupt clears it) */
    volatile s32 drawing;
    void (*drawsync_cb)();
    DRAWENV draw_env; /* the environments last put */
    /* volatile by PutDispEnv's code, which reads each member again at every
     * use */
    volatile DISPENV disp_env;
} GpuCtx;

extern GpuCtx gpu_info;

/* The queue: 64 jobs, each a function, its argument (copied here when it is
 * small) and a second argument. The interrupt runs them. */
typedef struct GpuQueueItem {
    s32 (*func)(s32 *, s32);
    s32 *arg;
    s32 cb_arg;
    s32 data[21];
} GpuQueueItem;

volatile GpuQueueItem _que[64];

/* The primitives _clr clears a rectangle with. */
static DR_ENV clr_prims;
static s32 clr_prims_unused; /* no code names it */

const char gpu_rcsid[52] =
    "$Id: sys.c,v 1.129 1996/12/25 03:36:20 noda Exp $\0\0\0";

s32 get_mode(s32, s32, s32);
s32 get_cs(s16, s16);
s32 get_ce(s16, s16);
s32 get_ofs(s32, s32);
s32 get_tw(RECT *);
void SetDrawEnv2(DR_ENV *, DRAWENV *);
s32 _reset(s32);
extern void GPU_cw(u32);
void memset();

extern volatile u32 *GPU_STATUS;
extern volatile u32 *GPU_DATA;
extern volatile u32 *DMA2_MADR;
extern volatile u32 *DMA2_BCR;
extern volatile u32 *DMA2_CHCR;
static u8 ctlbuf[260];
extern s32 gpu_alarm_time;
extern s32 gpu_alarm_count;

extern GpuDevTable _gpucb;
extern s32 gpu_width_tbl[];
extern s32 gpu_height_tbl[];

/* mode 0: resets the GPU and everything here. 1: cancels what is being
 * drawn. 3: resets the library, leaving the display as it is. 5 is 0
 * without the message. */
s32 ResetGraph(s32 mode) {
    switch (mode & 7) {
    case 0:
    case 3:
        printf("ResetGraph:jtb=%08x,env=%08x\n", &_gpucb, &gpu_info);
        /* fallthrough */
    case 5:
        memset(&gpu_info, 0, sizeof(gpu_info));
        ResetCallback();
        GPU_cw((u32)gpu & 0xFFFFFF);
        gpu_info.type = _reset(mode);
        gpu_info.queue_mode = 1;
        gpu_info.width = gpu_width_tbl[gpu_info.type];
        gpu_info.height = gpu_height_tbl[gpu_info.type];
        memset(&gpu_info.draw_env, -1, sizeof(DRAWENV));
        memset(&gpu_info.disp_env, -1, sizeof(DISPENV));
        return gpu_info.type;
    default:
        if (gpu_info.debug_level >= 2) {
            GPU_printf("ResetGraph(%d)...\n", mode);
        }
        gpu->reset(1);
        break;
    }
}

/* Mirrors the display left to right. */
s32 SetGraphReverse(s32 mode) {
    u32 old = gpu_info.reverse;
    if (gpu_info.debug_level >= 2) {
        GPU_printf("SetGraphReverse(%d)...\n", mode);
    }
    gpu_info.reverse = mode;
    gpu->ctl(0x08000000 | (gpu_info.reverse ? 0x80 : 0) | gpu->getctl(8));
    if (gpu_info.type == 2) {
        gpu->ctl(0x20000000 | (gpu_info.reverse ? 0x501 : 0x504));
    }
    return old;
}

s32 SetGraphDebug(s32 level) {
    u32 old = gpu_info.debug_level;
    gpu_info.debug_level = level;
    if (gpu_info.debug_level) {
        GPU_printf("SetGraphDebug:level:%d,type:%d reverse:%d\n",
                   gpu_info.debug_level, gpu_info.type, gpu_info.reverse);
    }
    return old;
}

/* mode 0: every job is run at once, the caller waiting. 1: they queue. */
s32 SetGraphQueue(s32 mode) {
    u32 old = gpu_info.queue_mode;
    if (gpu_info.debug_level >= 2) {
        GPU_printf("SetGrapQue(%d)...\n", mode);
    }
    if (mode != gpu_info.queue_mode) {
        gpu->reset(1);
        gpu_info.queue_mode = mode;
        DMACallback(2, 0);
    }
    return old;
}

s32 GetGraphType(void) { return gpu_info.type; }

s32 GetGraphDebug(void) { return gpu_info.debug_level; }

/* Sets the function called when the queue runs empty. */
u32 DrawSyncCallback(void (*func)()) {
    void (*old)();
    if (gpu_info.debug_level >= 2) {
        GPU_printf("DrawSyncCallback(%08x)...\n", func);
    }
    old = gpu_info.drawsync_cb;
    gpu_info.drawsync_cb = func;
    return (u32)old;
}

void SetDispMask(s32 mask) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("SetDispMask(%d)...\n", mask);
    }
    if (!mask) {
        memset(&gpu_info.disp_env, -1, sizeof(DISPENV));
    }
    gpu->ctl(mask ? 0x03000000 : 0x03000001);
}

/* mode 0: waits for the queue to empty. Otherwise: how many jobs are left. */
s32 DrawSync(s32 mode) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("DrawSync(%d)...\n", mode);
    }
    return gpu->sync(mode);
}

/* At debug level 1, complains of a rectangle that is not inside the frame
 * buffer; at level 2 prints every one. */
void checkRECT(char *str, RECT *rect) {
    switch (gpu_info.debug_level) {
    case 1:
        if (rect->w > gpu_info.width || rect->w + rect->x > gpu_info.width ||
            rect->y > gpu_info.height || rect->y + rect->h > gpu_info.height ||
            rect->w <= 0 || rect->x < 0 || rect->y < 0 || rect->h <= 0) {
            GPU_printf("%s:bad RECT", str);
            GPU_printf("(%d,%d)-(%d,%d)\n", rect->x, rect->y, rect->w, rect->h);
        }
        break;
    case 2:
        GPU_printf("%s:", str);
        GPU_printf("(%d,%d)-(%d,%d)\n", rect->x, rect->y, rect->w, rect->h);
        break;
    }
}

s32 ClearImage(RECT *rect, u8 r, u8 g, u8 b) {
    checkRECT("ClearImage", rect);
    return gpu->addque2(
        gpu->clr, rect, sizeof(RECT), ((u32)b << 16) | ((u32)g << 8) | (u32)r);
}

s32 ClearImage2(RECT *rect, u8 r, u8 g, u8 b) {
    checkRECT("ClearImage", rect);
    return gpu->addque2(gpu->clr, rect, sizeof(RECT),
                        0x80000000 | ((u32)b << 16) | ((u32)g << 8) | (u32)r);
}

s32 LoadImage(RECT *rect, u32 *p) {
    checkRECT("LoadImage", rect);
    return gpu->addque2(gpu->dws, rect, sizeof(RECT), p);
}

s32 StoreImage(RECT *rect, u32 *p) {
    checkRECT("StoreImage", rect);
    return gpu->addque2(gpu->drs, rect, sizeof(RECT), p);
}

extern u32 move_image[5];

s32 MoveImage(RECT *rect, s32 x, s32 y) {
    s32 packed;

    checkRECT("MoveImage", rect);
    if (rect->w == 0 || rect->h == 0) {
        return -1;
    }
    packed = (u16)y << 16 | (u16)x;
    move_image[2] = *(u32 *)&rect->x;
    move_image[3] = packed;
    move_image[4] = *(u32 *)&rect->w;
    return gpu->addque2(gpu->cwc, move_image, sizeof(move_image), 0);
}

extern u32 ot_terminator[5];

u32 *ClearOTag(u32 *ot, s32 n) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("ClearOTag(%08x,%d)...\n", ot, n);
    }
    while (--n) {
        setlen(ot, 0);
        setaddr(ot, ot + 1);
        ot++;
    }
    *ot = (u32)&ot_terminator & 0xFFFFFF;
    return ot;
}

u32 *ClearOTagR(u32 *ot, s32 n) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("ClearOTagR(%08x,%d)...\n", ot, n);
    }
    gpu->otc(ot, n);
    *ot = ((u32)&ot_terminator) & 0xFFFFFF;
    return ot;
}

void DrawPrim(void *p) {
    u32 size = getlen(p);
    gpu->sync(0);
    gpu->cwb((u32 *)p + 1, size);
}

void DrawOTag(u32 *p) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("DrawOTag(%08x)...\n", p);
    }
    gpu->addque2(gpu->cwc, p, 0, 0);
}

DRAWENV *PutDrawEnv(DRAWENV *env) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("PutDrawEnv(%08x)...\n", env);
    }
    SetDrawEnv2(&env->dr_env, env);
    env->dr_env.tag |= 0xFFFFFF;
    gpu->addque2(gpu->cwc, &env->dr_env, sizeof(DR_ENV), 0);
    gpu_info.draw_env = *env;
    return env;
}

/* Draws an ordering table with the environment put in front of it. */
void DrawOTagEnv(u32 *p, DRAWENV *env) {
    if (gpu_info.debug_level >= 2) {
        GPU_printf("DrawOTagEnv(%08x,&08x)...\n", p, env);
    }
    SetDrawEnv2(&env->dr_env, env);
    env->dr_env.tag = (env->dr_env.tag & 0xFF000000) | ((u32)p & 0xFFFFFF);
    gpu->addque2(gpu->cwc, &env->dr_env, sizeof(DR_ENV), 0);
    gpu_info.draw_env = *env;
}

DRAWENV *GetDrawEnv(DRAWENV *env) {
    memcpy((u8 *)env, (u8 *)&gpu_info.draw_env, sizeof(DRAWENV));
    return env;
}

s32 get_dx(DISPENV *env);

DISPENV *PutDispEnv(DISPENV *env) {
    s32 h_start, h_end;
    s32 v_start, v_end;
    s32 mode;

    mode = 0x08000000;
    if (gpu_info.debug_level >= 2) {
        GPU_printf("PutDispEnv(%08x)...\n", env);
    }
    gpu->ctl(
        gpu_info.type == 1 || gpu_info.type == 2
            ? ((env->disp.y & 0xFFF) << 12) | (get_dx(env) & 0xFFF) | 0x05000000
            : ((env->disp.y & 0x3FF) << 10) | (env->disp.x & 0x3FF) |
                  0x05000000);
    /* the screen area, when it has changed */
    if (!(gpu_info.disp_env.screen.x == env->screen.x &&
          gpu_info.disp_env.screen.y == env->screen.y &&
          gpu_info.disp_env.screen.w == env->screen.w &&
          gpu_info.disp_env.screen.h == env->screen.h)) {
        env->pad0 = GetVideoMode();
        h_start = env->screen.x * 10 + 0x260;
        v_start = env->screen.y + (env->pad0 ? 0x13 : 0x10);
        h_end = h_start + (env->screen.w ? env->screen.w * 10 : 2560);
        v_end = v_start + (env->screen.h ? env->screen.h : 240);
        h_start = h_start < 500 ? 500 : (h_start > 3290 ? 3290 : h_start);
        h_end = h_end < h_start + 0x50 ? h_start + 0x50
                                       : (h_end > 3290 ? 3290 : h_end);
        v_start =
            v_start < 0x10
                ? 0x10
                : (v_start > (env->pad0 ? 310 : 256) ? (env->pad0 ? 310 : 256)
                                                     : v_start);
        v_end = v_end < v_start + 2
                    ? v_start + 2
                    : (v_end > (env->pad0 ? 312 : 258) ? (env->pad0 ? 312 : 258)
                                                       : v_end);
        gpu->ctl(((h_end & 0xFFF) << 12) | 0x06000000 | (h_start & 0xFFF));
        gpu->ctl(((v_end & 0x3FF) << 10) | 0x07000000 | (v_start & 0x3FF));
    }
    /* the display mode, when it or the displayed area has changed (the four
       flag bytes are compared as one word) */
    if (*(s32 *)&gpu_info.disp_env.isinter != *(s32 *)&env->isinter ||
        !(gpu_info.disp_env.disp.x == env->disp.x &&
          gpu_info.disp_env.disp.y == env->disp.y &&
          gpu_info.disp_env.disp.w == env->disp.w &&
          gpu_info.disp_env.disp.h == env->disp.h)) {
        env->pad0 = GetVideoMode();
        if (env->pad0 == 1) {
            mode |= 0x8;
        }
        if (env->isrgb24) {
            mode |= 0x10;
        }
        if (env->isinter) {
            mode |= 0x20;
        }
        if (gpu_info.reverse) {
            mode |= 0x80;
        }
        if (env->disp.w > 280) {
            if (env->disp.w <= 352) {
                mode |= 1;
            } else if (env->disp.w <= 400) {
                mode |= 0x40;
            } else if (env->disp.w <= 560) {
                mode |= 2;
            } else {
                mode |= 3;
            }
        }
        /* FAKE: if (env->disp.h > ...) mode |= 0x24; */
        if (env->disp.h <= (env->pad0 ? 288 : 256)) {
        } else {
            mode |= 0x24;
        }
        gpu->ctl(mode);
    }
    memcpy((u8 *)&gpu_info.disp_env, (u8 *)env, sizeof(DISPENV));
    return env;
}

DISPENV *GetDispEnv(DISPENV *env) {
    memcpy((u8 *)env, (u8 *)&gpu_info.disp_env, sizeof(DISPENV));
    return env;
}

/* Which field of an interlaced screen is being displayed. */
s32 GetODE(void) { return gpu->status() >> 31; }

void SetTexWindow(DR_TWIN *p, RECT *tw) {
    setlen(p, 2);
    p->code[0] = get_tw(tw);
    p->code[1] = 0;
}

void SetDrawArea(DR_AREA *p, RECT *r) {
    setlen(p, 2);
    p->code[0] = get_cs(r->x, r->y);
    p->code[1] = get_ce(r->x + r->w - 1, r->y + r->h - 1);
}

/* The offsets are read as signed, whatever the prototype says. */
void SetDrawOffset(DR_OFFSET *p, u16 *ofs) {
    setlen(p, 2);
    p->code[0] = get_ofs(((s16 *)ofs)[0], ((s16 *)ofs)[1]);
    p->code[1] = 0;
}

/* The mask bit: pbc, pixels whose mask bit is set are not drawn over; pbw,
 * what is drawn has it set. */
void SetPriority(DR_PRIO *p, s32 pbc, s32 pbw) {
    u32 v0;
    setlen(p, 2);
    v0 = 0xE6000000;
    if (pbc) {
        v0 = 0xE6000002;
    }
    if (pbw) {
        v0 |= 1;
    }
    p->code[0] = v0;
    p->code[1] = 0;
}

void SetDrawMode(DR_MODE *p, s32 dfe, s32 dtd, s32 tpage, RECT *tw) {
    setlen(p, 2);
    p->code[0] = get_mode(dfe, dtd, (u16)tpage);
    p->code[1] = get_tw(tw);
}

/* Makes the primitive that sets a drawing environment: the clip area, the
 * offset, the mode, the texture window, and when the environment asks for
 * it a rectangle that clears the area. */
void SetDrawEnv(DR_ENV *out, DRAWENV *r) {
    DR_ENV *o = out; /* FAKE: out itself */
    RECT clip;
    s32 len;
    o->code[0] = get_cs(r->clip.x, r->clip.y);
    o->code[1] = get_ce(r->clip.w + r->clip.x - 1, r->clip.y + r->clip.h - 1);
    o->code[2] = get_ofs(r->ofs[0], r->ofs[1]);
    o->code[3] = get_mode(r->dfe, r->dtd, r->tpage);
    o->code[4] = get_tw(&r->tw);
    o->code[5] = 0xE6000000;
    len = 7;
    if (r->isbg != 0) {
        clip.x = r->clip.x;
        clip.y = r->clip.y;
        clip.w = r->clip.w;
        clip.h = r->clip.h;
        clip.w = clip.w < 0                    ? 0
                 : clip.w > gpu_info.width - 1 ? gpu_info.width - 1
                                               : clip.w;
        clip.h = clip.h < 0                     ? 0
                 : clip.h > gpu_info.height - 1 ? gpu_info.height - 1
                                                : clip.h;
        clip.x -= r->ofs[0];
        clip.y -= r->ofs[1];
        *((u32 *)o + len++) = 0x60000000 | r->b0 << 16 | r->g0 << 8 | r->r0;
        *((u32 *)o + len++) = *(u32 *)&clip.x;
        *((u32 *)o + len++) = *(u32 *)&clip.w;
        clip.x += r->ofs[0];
        clip.y += r->ofs[1];
    }
    setlen(o, len - 1);
}

/* PutDrawEnv's: the same, but the clearing is a fast fill when the area is
 * a multiple of 64 pixels wide. */
void SetDrawEnv2(DR_ENV *out, DRAWENV *r) {
    DR_ENV *o = out; /* FAKE: out itself */
    RECT clip;
    s32 len;
    o->code[0] = get_cs(r->clip.x, r->clip.y);
    o->code[1] = get_ce(r->clip.w + r->clip.x - 1, r->clip.y + r->clip.h - 1);
    o->code[2] = get_ofs(r->ofs[0], r->ofs[1]);
    o->code[3] = get_mode(r->dfe, r->dtd, r->tpage);
    o->code[4] = get_tw(&r->tw);
    o->code[5] = 0xE6000000;
    len = 7;
    if (r->isbg != 0) {
        clip.x = r->clip.x;
        clip.y = r->clip.y;
        clip.w = r->clip.w;
        clip.h = r->clip.h;
        clip.w = clip.w < 0                    ? 0
                 : clip.w > gpu_info.width - 1 ? gpu_info.width - 1
                                               : clip.w;
        clip.h = clip.h < 0                     ? 0
                 : clip.h > gpu_info.height - 1 ? gpu_info.height - 1
                                                : clip.h;
        if ((clip.x & 0x3F) || (clip.w & 0x3F)) {
            clip.x -= r->ofs[0];
            clip.y -= r->ofs[1];
            *((u32 *)o + len++) = 0x60000000 | r->b0 << 16 | r->g0 << 8 | r->r0;
            *((u32 *)o + len++) = *(u32 *)&clip.x;
            *((u32 *)o + len++) = *(u32 *)&clip.w;
            clip.x += r->ofs[0];
            clip.y += r->ofs[1];
        } else {
            *((u32 *)o + len++) = 0x02000000 | r->b0 << 16 | r->g0 << 8 | r->r0;
            *((u32 *)o + len++) = *(u32 *)&clip.x;
            *((u32 *)o + len++) = *(u32 *)&clip.w;
        }
    }
    setlen(o, len - 1);
}

/* The GPU commands of an environment: the drawing mode, the top left and
 * bottom right of the clip area, the offset, the texture window. The two
 * older kinds of GPU pack them differently. */
s32 get_mode(s32 dfe, s32 dtd, s32 tpage) {
    s32 cmd;
    s32 bits;

    if (gpu_info.type == 1 || gpu_info.type == 2) {
        cmd = 0xE1000000;
        if (dtd != 0) {
            cmd = 0xE1000800;
        }
        if (dfe != 0) {
            bits = (tpage & 0x27FF) | 0x1000;
        } else {
            bits = tpage & 0x27FF;
        }
    } else {
        cmd = 0xE1000000;
        if (dtd != 0) {
            cmd = 0xE1000200;
        }
        bits = tpage & 0x9FF;
        if (dfe != 0) {
            bits |= 0x400;
        }
    }
    return cmd | bits;
}

s32 get_cs(s16 x, s16 y) {
    x = x < 0 ? 0 : (x > gpu_info.width - 1 ? gpu_info.width - 1 : x);
    y = y < 0 ? 0 : (y > gpu_info.height - 1 ? gpu_info.height - 1 : y);
    if ((u32)(gpu_info.type - 1) < 2U) {
        return 0xE3000000 | ((y & 0xFFF) << 12) | (x & 0xFFF);
    } else {
        return 0xE3000000 | ((y & 0x3FF) << 10) | (x & 0x3FF);
    }
}

s32 get_ce(s16 x, s16 y) {
    x = x < 0 ? 0 : (x > gpu_info.width - 1 ? gpu_info.width - 1 : x);
    y = y < 0 ? 0 : (y > gpu_info.height - 1 ? gpu_info.height - 1 : y);
    if ((u32)(gpu_info.type - 1) < 2U) {
        return 0xE4000000 | ((y & 0xFFF) << 12) | (x & 0xFFF);
    } else {
        return 0xE4000000 | ((y & 0x3FF) << 10) | (x & 0x3FF);
    }
}

s32 get_ofs(s32 x, s32 y) {
    if ((u32)(gpu_info.type - 1) < 2U) {
        return 0xE5000000 | ((y & 0xFFF) << 12) | (x & 0xFFF);
    } else {
        return 0xE5000000 | ((y & 0x7FF) << 11) | (x & 0x7FF);
    }
}

s32 get_tw(RECT *tw) {
    u32 pad[4];

    if (tw != 0) {
        pad[0] = (tw->x & 0xFF) >> 3;
        pad[2] = (-tw->w & 0xFF) >> 3;
        pad[1] = (tw->y & 0xFF) >> 3;
        pad[3] = (-tw->h & 0xFF) >> 3;
        return 0xE2000000 | (pad[1] << 0xF) | (pad[0] << 0xA) | (pad[3] << 5) |
               pad[2];
    }
    return 0;
}

/* Where the display starts across, on the older kinds of GPU. */
s32 get_dx(DISPENV *env) {
    switch (gpu_info.type) {
    case 1:
        return gpu_info.reverse ? 0x400 - env->disp.x - env->disp.w
                                : env->disp.x;
    case 2:
        return gpu_info.reverse ? 0x400 - env->disp.x - env->disp.w / 2
                                : env->disp.x / 2;
    default:
        return env->disp.x;
    }
}

u32 _status(void) { return *GPU_STATUS; }

extern void set_alarm(void);
extern s32 get_alarm();
extern volatile s32 *DMA6_MADR;
extern volatile s32 *DMA6_BCR;
extern volatile s32 *DMA6_CHCR;
extern volatile s32 *DPCR;

/* ClearOTagR: the DMA controller itself links an ordering table, last entry
 * first. */
s32 _otc(s32 ot, s32 n) {
    *DPCR |= 0x08000000;
    *DMA6_CHCR = 0;
    *DMA6_MADR = (ot - 4) + (n * 4);
    *DMA6_BCR = n;
    *DMA6_CHCR = 0x11000002;
    set_alarm();
    while (*DMA6_CHCR & 0x01000000) {
        if (get_alarm() != 0) {
            return -1;
        }
    }
    return n;
}

void _cwc(u32 a0);
u32 _param(u32 a0);

/* ClearImage's job: a fast fill when the rectangle is a multiple of 64
 * pixels wide; otherwise a drawn rectangle, with the drawing environment
 * taken off for it and put back after. */
s32 _clr(RECT *rect, u32 color) {
    u32 ptr;

    rect->w =
        rect->w < 0
            ? 0
            : (rect->w > gpu_info.width - 1 ? gpu_info.width - 1 : rect->w);
    rect->h =
        rect->h < 0
            ? 0
            : (rect->h > gpu_info.height - 1 ? gpu_info.height - 1 : rect->h);
    if (rect->x & 0x3F || rect->w & 0x3F) {
        ptr = (u32)&clr_prims.code[8];
        clr_prims.tag = (ptr & 0xFFFFFF) | 0x08000000;
        clr_prims.code[0] = 0xE3000000;
        clr_prims.code[1] = 0xE4FFFFFF;
        clr_prims.code[2] = 0xE5000000;
        clr_prims.code[3] = 0xE6000000;
        clr_prims.code[4] =
            0xE1000000 | *GPU_STATUS & 0x7FF | (color >> 0x1F) << 10;
        clr_prims.code[5] = (color & 0xFFFFFF) | 0x60000000;
        clr_prims.code[6] = *(s32 *)&rect->x;
        clr_prims.code[7] = *(s32 *)&rect->w;
        clr_prims.code[8] = 0xFFFFFF | 0x03000000;
        clr_prims.code[9] = _param(3) | 0xE3000000;
        clr_prims.code[10] = _param(4) | 0xE4000000;
        clr_prims.code[11] = _param(5) | 0xE5000000;
    } else {
        clr_prims.tag = 0xFFFFFF | 0x05000000;
        clr_prims.code[0] = 0xE6000000;
        clr_prims.code[1] =
            0xE1000000 | *GPU_STATUS & 0x7FF | (color >> 0x1F) << 10;
        clr_prims.code[2] = (color & 0xFFFFFF) | 0x02000000;
        clr_prims.code[3] = *(s32 *)&rect->x;
        clr_prims.code[4] = *(s32 *)&rect->w;
    }
    _cwc((u32)&clr_prims);
    return 0;
}

/* LoadImage's job: sends the copy command and the words that do not fill a
 * block of sixteen by hand, and the rest by DMA. */
s32 _dws(RECT *rect, s32 *data) {
    s32 to_write;
    s32 size;
    s32 rest;
    s32 mode;

    mode = 0;
    set_alarm();
    rect->w =
        rect->w < 0 ? 0 : (rect->w > gpu_info.width ? gpu_info.width : rect->w);
    rect->h = rect->h < 0
                  ? 0
                  : (rect->h > gpu_info.height ? gpu_info.height : rect->h);
    to_write = (rect->w * rect->h + 1) / 2;
    if (to_write <= 0) {
        return -1;
    }
    rest = to_write % 16;
    size = to_write / 16;

    while (!(*GPU_STATUS & 0x04000000)) {
        if (get_alarm() != 0) {
            return -1;
        }
    }

    *GPU_STATUS = 0x04000000;
    *GPU_DATA = 0x01000000;
    *GPU_DATA = mode ? 0xB0000000 : 0xA0000000;
    *GPU_DATA = *(s32 *)&rect->x;
    *GPU_DATA = *(s32 *)&rect->w;

    while (--rest != -1) {
        *GPU_DATA = *data++;
    }

    if (size) {
        *GPU_STATUS = 0x04000002;
        *DMA2_MADR = (u32)data;
        *DMA2_BCR = size << 16 | 0x10;
        *DMA2_CHCR = 0x01000201;
    }
    return 0;
}

/* StoreImage's job: the same, from the frame buffer to memory. */
s32 _drs(RECT *rect, s32 *data) {
    s32 to_read;
    s32 size;
    s32 rest;

    set_alarm();
    rect->w =
        rect->w < 0 ? 0 : (rect->w > gpu_info.width ? gpu_info.width : rect->w);
    rect->h = rect->h < 0
                  ? 0
                  : (rect->h > gpu_info.height ? gpu_info.height : rect->h);
    to_read = (rect->w * rect->h + 1) / 2;
    if (to_read <= 0) {
        return -1;
    }
    rest = to_read % 16;
    size = to_read / 16;

    while (!(*GPU_STATUS & 0x04000000)) {
        if (get_alarm() != 0) {
            return -1;
        }
    }

    *GPU_STATUS = 0x04000000;
    *GPU_DATA = 0x01000000;
    *GPU_DATA = 0xC0000000;
    *GPU_DATA = *(s32 *)&rect->x;
    *GPU_DATA = *(s32 *)&rect->w;

    while (!(*GPU_STATUS & 0x08000000)) {
        if (get_alarm() != 0) {
            return -1;
        }
    }

    while (--rest != -1) {
        *data++ = *GPU_DATA;
    }

    if (size) {
        *GPU_STATUS = 0x04000003;
        *DMA2_MADR = (u32)data;
        *DMA2_BCR = size << 16 | 0x10;
        *DMA2_CHCR = 0x01000200;
    }
    return 0;
}

/* Sends a control command, and remembers the last of each kind. */
void _ctl(u32 com) {
    *GPU_STATUS = com;
    ctlbuf[com >> 24] = com;
}

u32 _getctl(s32 com) { return ctlbuf[com]; }

/* Sends `n` words to the GPU by hand. */
s32 _cwb(u32 *p, s32 n) {
    s32 i;
    *GPU_STATUS = GP1_DMA_DIR;
    for (i = n - 1; i != -1; i--) {
        *GPU_DATA = *p++;
    }
    return 0;
}

/* Starts the DMA transfer of a linked list of primitives. */
void _cwc(u32 p) {
    *GPU_STATUS = GP1_DMA_DIR_FIFO;
    *DMA2_MADR = p;
    *DMA2_BCR = 0;
    *DMA2_CHCR = DMA_GPU_LINKED_LIST;
}

/* Asks the GPU for one of its settings. */
u32 _param(u32 index) {
    *GPU_STATUS = index | GP1_GPU_INFO;
    return *GPU_DATA & OT_ADDR_MASK;
}

s32 _addque2();

void _addque(s32 func, s32 arg, s32 cb_arg) { _addque2(func, arg, 0, cb_arg); }

extern volatile s32 _qin;
extern volatile s32 _qout;

s32 _exeque(void);
s32 get_alarm();
s32 SetIntrMask(s32);
extern volatile s32 _qlog[];
extern s32 addque_imask;
extern s32 exeque_imask;

/* Gives the GPU a job: func(arg, cb_arg). It is run at once when the GPU is
 * idle or nothing is to queue; otherwise it joins the queue, with a copy of
 * the `len` bytes at `arg` if len is not 0. Returns the jobs left, or -1
 * when the queue stayed full. */
s32 _addque2(s32 (*func)(s32 *, s32), s32 *arg, s32 len, s32 cb_arg) {
    s32 i;

    set_alarm();
    while (((_qin + 1) & 0x3F) == _qout) {
        if (get_alarm() != 0) {
            return -1;
        }
        _exeque();
    }
    addque_imask = SetIntrMask(0);
    gpu_info.drawing = 1;
    if (gpu_info.queue_mode == 0 ||
        (_qin == _qout && !(*DMA2_CHCR & 0x01000000) &&
         gpu_info.drawsync_cb == 0)) {
        while (!(*GPU_STATUS & 0x04000000)) {
        }
        func(arg, cb_arg);
        _qlog[0] = (s32)func;
        _qlog[1] = (s32)arg;
        _qlog[2] = cb_arg;
        SetIntrMask(addque_imask);
        return 0;
    }
    DMACallback(2, (void (*)())_exeque);
    if (len != 0) {
        for (i = 0; i < len / 4; i++) {
            _que[_qin].data[i] = arg[i];
        }
        _que[_qin].arg = (s32 *)_que[_qin].data;
    } else {
        _que[_qin].arg = arg;
    }
    _que[_qin].cb_arg = cb_arg;
    _que[_qin].func = func;
    _qin = (_qin + 1) & 0x3F;
    SetIntrMask(addque_imask);
    _exeque();
    return (_qin - _qout) & 0x3F;
}

/* Runs the queued jobs until one starts a DMA transfer; the end of that
 * transfer calls this again. When the queue is empty the DrawSyncCallback is
 * called. */
s32 _exeque(void) {
    if (*DMA2_CHCR & 0x01000000) {
        return 1;
    }
    exeque_imask = SetIntrMask(0);
    while (_qin != _qout && !(*DMA2_CHCR & 0x01000000)) {
        if (((_qout + 1) & 0x3F) == _qin && gpu_info.drawsync_cb == 0) {
            DMACallback(2, NULL);
        }
        while (!(*GPU_STATUS & 0x04000000)) {
        }
        _que[_qout].func(_que[_qout].arg, _que[_qout].cb_arg);
        _qlog[0] = (s32)_que[_qout].func;
        _qlog[1] = (s32)_que[_qout].arg;
        _qlog[2] = _que[_qout].cb_arg;
        _qout = (_qout + 1) & 0x3F;
    }
    SetIntrMask(exeque_imask);
    if (_qin == _qout && !(*DMA2_CHCR & 0x01000000) && gpu_info.drawing != 0 &&
        gpu_info.drawsync_cb != 0) {
        gpu_info.drawing = 0;
        gpu_info.drawsync_cb();
    }
    return (_qin - _qout) & 0x3F;
}

extern s32 _version(s32);
extern s32 reset_imask;

/* Empties the queue and stops the GPU; mode 0 (and 5) resets it as well and
 * finds out which kind it is. */
s32 _reset(s32 mode) {
    reset_imask = SetIntrMask(0);
    _qout = 0;
    _qin = _qout;
    switch (mode & 7) {
    case 5:
    case 0:
        *DMA2_CHCR = 0x401;
        *DPCR |= 0x800;
        *GPU_STATUS = 0;
        memset(ctlbuf, 0, 0x100);
        memset((u8 *)_que, 0, 0x1800);
        break;
    case 1:
    case 3:
        *DMA2_CHCR = 0x401;
        *DPCR |= 0x800;
        *GPU_STATUS = 0x02000000;
        *GPU_STATUS = 0x01000000;
        break;
    }
    SetIntrMask(reset_imask);
    if (mode & 7) {
        return 0;
    }
    return _version(mode);
}

/* DrawSync. */
s32 _sync(s32 mode) {
    s32 queued;
    s32 ret;

    if (mode == 0) {
        set_alarm();
        while (_qin != _qout) {
            _exeque();
            if (get_alarm() != 0)
                return -1;
        }
        while ((*DMA2_CHCR & 0x01000000) || !(*GPU_STATUS & 0x04000000)) {
            if (get_alarm() != 0)
                return -1;
        }
        return 0;
    }
    queued = (_qin - _qout) & 0x3F;
    if (queued != 0) {
        _exeque();
    }
    if (!(*DMA2_CHCR & 0x01000000) && (*GPU_STATUS & 0x04000000)) {
        ret = queued;
    } else {
        if (queued != 0) {
            ret = queued;
        } else {
            return 1;
        }
    }
    return ret;
}

/* The time limit on a wait: four seconds. When it runs out the queue is
 * thrown away and the GPU reset. */
void set_alarm(void) {
    gpu_alarm_time = VSync(-1) + 0xF0;
    gpu_alarm_count = 0;
}

s32 get_alarm(void) {
    if (gpu_alarm_time < VSync(-1) || gpu_alarm_count++ > 0xF0000) {
        *GPU_STATUS;
        printf("GPU timeout:que=%d,stat=%08x,chcr=%08x,madr=%08x,",
               (_qin - _qout) & 0x3F, *GPU_STATUS, *DMA2_CHCR, *DMA2_MADR);
        printf("func=(%08x)(%08x,%08x)\n", _qlog[0], _qlog[1], _qlog[2]);
        reset_imask = SetIntrMask(0);
        _qin = _qout = 0;
        *DMA2_CHCR = 0x401;
        *DPCR |= 0x800;
        *GPU_STATUS = 0x02000000;
        *GPU_STATUS = 0x01000000;
        SetIntrMask(reset_imask);
        return -1;
    }
    return 0;
}

/* Which GPU this is: 0 for the one in the console, 1 and 2 for the older
 * kinds found in development and arcade boards (2 with mode bit 3 asking for
 * its high resolution), 3 and 4 likewise for the newer. */
s32 _version(s32 mode) {
    *GPU_STATUS = 0x10000007;
    if ((*GPU_DATA & 0xFFFFFF) != 2) {
        *GPU_DATA = (*GPU_STATUS & 0x3FFF) | 0xE1001000;
        (void)*GPU_DATA;
        if (!(*GPU_STATUS & 0x1000)) {
            return 0;
        }
        if (!(mode & 8)) {
            return 1;
        }
        *GPU_STATUS = 0x20000504;
        return 2;
    }
    if (!(mode & 8)) {
        return 3;
    }
    *GPU_STATUS = 0x09000001;
    return 4;
}

void memset(ptr, value, num) u8 *ptr;
u8 value;
s32 num;
{
    s32 i;
    for (i = num - 1; i != -1; i--) {
        *ptr++ = value;
    }
}

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _gpu_stamp[2] = {0x15077350, 0x0040899C};

GpuDevTable _gpucb = {
    gpu_rcsid, _addque, _addque2, _clr, _ctl,   _cwb,   _cwc,    _drs,
    _dws,      _exeque, _getctl,  _otc, _param, _reset, _status, _sync,
};

GpuDevTable *gpu = &_gpucb;

void (*GPU_printf)() = printf;

GpuCtx gpu_info = {0};

/* The frame buffer's size on each kind of GPU. */
s32 gpu_width_tbl[5] = {1024, 1024, 1024, 1024, 1024};
s32 gpu_height_tbl[5] = {512, 1024, 1024, 512, 1024};

/* MoveImage's primitive. */
u32 move_image[5] = {0x04FFFFFF, 0x80000000, 0, 0, 0};

/* What the last entry of an ordering table links to: an empty primitive that
 * ends the list. */
u32 ot_terminator[5] = {0x04FFFFFF, 0, 0, 0, 0};

volatile u32 *GPU_DATA = (u32 *)0x1F801810;
volatile u32 *GPU_STATUS = (u32 *)0x1F801814;
volatile u32 *DMA2_MADR = (u32 *)0x1F8010A0;
volatile u32 *DMA2_BCR = (u32 *)0x1F8010A4;
volatile u32 *DMA2_CHCR = (u32 *)0x1F8010A8;
volatile s32 *DMA6_MADR = (s32 *)0x1F8010E0;
volatile s32 *DMA6_BCR = (s32 *)0x1F8010E4;
volatile s32 *DMA6_CHCR = (s32 *)0x1F8010E8;
volatile s32 *DPCR = (s32 *)0x1F8010F0;

/* The last job run, for the timeout's message; and the queue's two ends. */
volatile s32 _qlog[4] = {0};
volatile s32 _qin = 0;
volatile s32 _qout = 0;
/* The interrupt mask, kept while _addque2, _exeque and _reset have the
 * interrupts off. */
s32 addque_imask = 0;
s32 exeque_imask = 0;
s32 reset_imask = 0;
s32 gpu_alarm_time = 0;  /* when the wait runs out, in vertical blanks */
s32 gpu_alarm_count = 0; /* how many times it has been asked */
