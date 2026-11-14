/* LIBCOMB COMB: the link cable. It is a kernel device, "sio:", whose reads
 * and writes go through the second serial port, in the background when
 * asked; _comb_control sets the port up and reports on it. */
#include "common.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libcomb.h>

/* A transfer in progress: one for writing and one for reading. */
typedef struct {
    s32 flag;
    u8 *buf;
    s32 len;
    s32 dsr;
} SioReq;

/* The kernel's file control block, which it hands to a device's routines. */
typedef struct {
    s32 flags;
    s32 unit;
    u8 *addr;
    s32 count;
    s32 pos;
    s32 dev_flags;
    s32 error;
} Fcb;

/* An entry in the kernel's queue of interrupt handlers: on an interrupt the
 * kernel calls `verify`, and `handler` if that did not return 0. */
typedef struct {
    void *next;
    s32 (*handler)();
    s32 (*verify)();
    s32 unkC; /* not used here */
} IntRP;

/* A kernel device: its name, flags, block size and description, then its
 * sixteen routines. */
struct DCB {
    const char *name;
    s32 flags;
    s32 ssize;
    const char *desc;
    s32 (*fn[16])();
};

/* The port's settings as last given, the function a waiting transfer calls,
 * and the two transfers. */
static volatile SioRegs regs;
static s32 (*CombWaitCallback)(s32, s32);
static volatile SioReq sen;
static volatile SioReq rcv;

extern s32 ResetGraph(s32);
/* No header declares these two. */
extern s32 SysEnqIntRP();
extern s32 SysDeqIntRP();

const char sio_desc[12] = "SIO console";
const char sio_name[4] = "sio";

static s32 EvalpSio(void);
static s32 HandleSio(void);
static s32 r_sioinit(void);
static s32 r_sioopen(void);
static s32 r_sioclose(void);
static s32 r_sioremove(void);
static s32 r_siostrategy(Fcb *fcb, s32 mode);
static s32 __nulldev(void);

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _comb_stamp[2] = {0x15057350, 0x0040879C};
/* The second serial port, and its data register as a whole word. */
volatile SioRegs *sio1 = (volatile SioRegs *)0x1F801050;
volatile u32 *sio1_word = (volatile u32 *)0x1F801050;

IntRP sio_handler = {0, HandleSio, EvalpSio, 0};

/* The interrupt status register; [1] is the mask. */
volatile u32 *comb_intr = (volatile u32 *)0x1F801070;

/* The control register's bits for a packet of each size, and back. */
s16 comb_rx_bits[10] = {-1, 0, 0x100, -1, 0x200, -1, -1, -1, 0x300, 0};

s16 comb_rx_size[4] = {1, 2, 4, 8};

struct DCB sio_dcb = {
    sio_name,
    3,
    1,
    sio_desc,
    {r_sioinit, r_sioopen, r_siostrategy, r_sioclose, __nulldev, __nulldev,
     __nulldev, __nulldev, __nulldev, __nulldev, __nulldev, __nulldev,
     __nulldev, __nulldev, r_sioremove, __nulldev},
};

void AddCOMB(void) {
    s32 intr;

    intr = EnterCriticalSection();
    AddDrv(&sio_dcb);
    if (intr == 1) {
        ExitCriticalSection();
    }
}

void DelCOMB(void) {
    s32 intr;

    intr = EnterCriticalSection();
    DelDrv(sio_name);
    FlushCache();
    if (intr == 1) {
        ExitCriticalSection();
    }
}

void ChangeClearSIO(s32 val) {}

static s32 SioAnsyncRead(u8 *buf, s32 len) {
    if (rcv.flag) {
        return -1;
    }
    rcv.len = len;
    rcv.buf = buf;
    rcv.flag = 1;
    sio1->ctrl |= 0x800;
    sio1->ctrl |= 0x20;
    return 0;
}

static s32 SioSyncroRead(u8 *buf, s32 len) {
    s32 i;
    s32 cnt = 0;
    s32 size;
    u16 ctrl;

    if (rcv.flag) {
        return -1;
    }
    size = comb_rx_size[(regs.ctrl & 0x300) >> 8];
    rcv.len = len;
    rcv.buf = buf;
    rcv.flag = 0;
    sio1->ctrl |= 0x20;
    i = 0;
    while (rcv.len) {
        if (sio1->stat & 0x38) {
            ctrl = sio1->ctrl;
            sio1->ctrl = 0x50;
            sio1->mode = regs.mode;
            sio1->baud = regs.baud;
            sio1->ctrl |= 0x10;
            sio1->ctrl = ctrl;
            sio1->ctrl &= ~0x20;
            DeliverEvent(0xF000000B, 0x8000);
            return len - rcv.len;
        }
        while (!(sio1->stat & 2)) {
            if (CombWaitCallback && (*CombWaitCallback)(1, cnt++) == 0) {
                sio1->ctrl &= ~0x20;
                DeliverEvent(0xF000000B, 0x100);
                return len - rcv.len;
            }
        }
        *rcv.buf = sio1->data;
        rcv.buf++;
        rcv.len--;
        if (++i == size) {
            i = 0;
            sio1->ctrl ^= 2;
        }
    }
    sio1->ctrl &= ~0x20;
    return len - rcv.len;
}

static s32 SioAnsyncWrite(u8 *buf, s32 len) {
    if (sen.flag) {
        return -1;
    }
    sen.len = len;
    sen.buf = buf;
    sen.flag = 1;
    sen.dsr = sio1->stat & 0x80;
    sio1->ctrl |= 0x400;
    return 0;
}

static s32 SioSyncroWrite(u8 *buf, s32 len) {
    s32 i;
    s32 cnt = 0;
    s32 size;

    if (sen.flag) {
        return -1;
    }
    size = comb_rx_size[(regs.ctrl & 0x300) >> 8];
    sen.len = len;
    sen.buf = buf;
    i = 0;
    while (sen.len) {
        while ((sio1->stat & 5) != 5) {
            if (CombWaitCallback && (*CombWaitCallback)(2, cnt++) == 0) {
                DeliverEvent(0xF000000B, 0x100);
                return len - sen.len;
            }
        }
        if (i == 0) {
            sen.dsr = sio1->stat & 0x80;
        }
        sio1->data = *sen.buf;
        sen.buf++;
        sen.len--;
        if (++i == size) {
            while ((sio1->stat & 0x80) == sen.dsr) {
                if (CombWaitCallback && (*CombWaitCallback)(2, cnt++) == 0) {
                    DeliverEvent(0xF000000B, 0x100);
                    return len - sen.len - 1;
                }
            }
            i = 0;
        }
    }
    return len - sen.len;
}

s32 _comb_control(u32 cmd, u32 arg, u32 param) {
    s32 ret = 0;

    switch (cmd) {
    case 0:
        switch (arg) {
        case 0:
            ret = sio1->stat;
            break;
        case 1:
            ret = (sio1->ctrl >> 1) & 1;
            if (sio1->ctrl & 0x20) {
                ret |= 2;
            }
            break;
        case 2:
            ret = regs.mode;
            break;
        case 3:
            ret = 2073600 / regs.baud;
            break;
        case 4:
            ret = 1;
            break;
        case 5:
            if (param == 0) {
                ret = sen.len;
            } else {
                ret = rcv.len;
            }
            break;
        case 6:
            ret = (param == 0 ? sen.flag : rcv.flag) != 0;
            break;
        }
        break;
    case 1:
        switch (arg) {
        case 0:
            break;
        case 1:
            regs.ctrl = sio1->ctrl & ~0x22;
            regs.ctrl |= ((param & 1) ? 2 : 0) | ((param & 2) ? 0x20 : 0);
            sio1->ctrl = regs.ctrl;
            break;
        case 2:
            regs.mode = param;
            sio1->mode = regs.mode;
            break;
        case 3:
            if (2073600 % param) {
                ret = -1;
                break;
            }
            regs.baud = 2073600 / param;
            sio1->baud = regs.baud;
            sio1->ctrl |= 0x10;
            break;
        case 4:
            if (param != 0 && param < 9) {
                ret = comb_rx_bits[param];
                if (ret >= 0) {
                    regs.ctrl &= ~0x300;
                    regs.ctrl |= ret;
                    sio1->ctrl = regs.ctrl;
                }
            }
            break;
        }
        break;
    case 2:
        switch (arg) {
        case 0: {
            s32 intr = EnterCriticalSection();

            sio1->ctrl |= 0x50;
            sio1->mode = regs.mode;
            sio1->ctrl = regs.ctrl;
            sio1->baud = regs.baud;
            sio1->ctrl |= 0x10;
            ret = 0;
            if (intr == 1) {
                ExitCriticalSection();
            }
            break;
        }
        case 1:
            sio1->ctrl |= 0x10;
            ret = 0;
            break;
        case 2: {
            s32 intr = EnterCriticalSection();

            sio1->ctrl &= ~0x400;
            sen.flag = 0;
            sen.len = 0;
            if (intr == 1) {
                ExitCriticalSection();
            }
            break;
        }
        case 3: {
            s32 intr = EnterCriticalSection();

            sio1->ctrl &= ~0x820;
            rcv.flag = 0;
            rcv.len = 0;
            if (intr == 1) {
                ExitCriticalSection();
            }
            break;
        }
        }
        break;
    case 3:
        switch (arg) {
        case 0:
            if (param) {
                sio1->ctrl |= 0x20;
            } else {
                sio1->ctrl &= ~0x20;
            }
            break;
        case 1:
            if (sio1->stat & 0x100) {
                ret = 1;
            }
            break;
        }
        break;
    case 4:
        if (arg == 0) {
            /* FAKE: ret = CombWaitCallback; CombWaitCallback = param; */
            s32 (**slot)(s32, s32) = &CombWaitCallback;

            ret = (s32)*slot;
            *slot = (s32(*)(s32, s32))param;
        }
        break;
    case 5:
        if (arg == 0) {
            ResetGraph(5);
        }
        break;
    }
    return ret;
}

static s32 EvalpSio(void) {
    if (!(comb_intr[0] & comb_intr[1] & 0x100)) {
        return 0;
    }
    if (sen.flag && (sio1->stat & 1)) {
        sio1->ctrl &= ~0x400;
    }
    return 1;
}

static s32 HandleSio(void) {
    s32 size;

    if (*comb_intr & 0x100) {
        *comb_intr = ~0x100;
    }
    if (sio1->stat & 0x38) {
        sio1->ctrl &= ~0x820;
        rcv.len = 0;
        rcv.flag = 0;
        sio1->ctrl;
        sio1->ctrl = 0x50;
        sio1->mode = regs.mode;
        sio1->baud = regs.baud;
        sio1->ctrl |= 0x10;
        DeliverEvent(0xF000000B, 0x8000);
        return 0;
    }
    if (rcv.flag && rcv.len && (sio1->stat & 2)) {
        size = comb_rx_size[(regs.ctrl & 0x300) >> 8];
        switch (size) {
        case 1:
            rcv.buf[0] = sio1->data;
            break;
        case 2:
            rcv.buf[0] = sio1->data;
            rcv.buf[1] = sio1->data;
            break;
        case 4:
            ((u32 *)rcv.buf)[0] = *sio1_word;
            break;
        case 8:
            ((u32 *)rcv.buf)[0] = *sio1_word;
            ((u32 *)rcv.buf)[1] = *sio1_word;
            break;
        }
        rcv.buf += size;
        rcv.len -= size;
        if (rcv.len == 0) {
            rcv.flag = 0;
            sio1->ctrl &= ~0x820;
            DeliverEvent(0xF000000B, 0x400);
        }
        sio1->ctrl ^= 2;
    }
    if (sen.flag && (sio1->stat & 1) && (sio1->stat & 0x80) == sen.dsr) {
        if (sen.len == 0) {
            sen.flag = 0;
            DeliverEvent(0xF000000B, 0x800);
        } else {
            sen.dsr ^= 0x80;
            sio1->data = *sen.buf;
            sen.buf++;
            sen.len--;
            sio1->ctrl |= 0x400;
        }
    }
    sio1->ctrl |= 0x10;
    return 0;
}

static s32 r_sioinit(void) {
    regs.ctrl = 5;
    regs.mode = 0xCE;
    regs.baud = 0xD8;
    sio1->ctrl = 0x50;
    sio1->mode = regs.mode;
    sio1->baud = regs.baud;
    sio1->ctrl |= 0x10;
    sio1->ctrl = regs.ctrl;
    SysDeqIntRP(3, &sio_handler);
    SysEnqIntRP(3, &sio_handler);
    comb_intr[1] |= 0x100;
    sen.flag = rcv.flag = 0;
    sen.buf = rcv.buf = 0;
    CombWaitCallback = 0;
    sen.len = rcv.len = 0;
    return 0;
}

static s32 r_sioopen(void) { return 0; }

static s32 r_sioclose(void) { return 0; }

static s32 r_sioremove(void) {
    comb_intr[1] &= ~0x100;
    sio1->ctrl |= 0x50;
    sio1->ctrl = regs.ctrl;
    SysDeqIntRP(3, &sio_handler);
    sen.flag = rcv.flag = 0;
    sen.len = rcv.len = 0;
    return 0;
}

static s32 r_siostrategy(Fcb *fcb, s32 mode) {
    fcb->error = 0;
    if (mode == 1) {
        s32 n;

        if (fcb->flags & 0x8000) {
            if (SioAnsyncRead(fcb->addr, fcb->count) != 0) {
                fcb->error = 0x10;
                return -1;
            }
            return 0;
        }
        n = SioSyncroRead(fcb->addr, fcb->count);
        if (n < 0) {
            fcb->error = 5;
        }
        return n;
    }
    if (mode == 2) {
        s32 n;

        if (fcb->flags & 0x8000) {
            if (SioAnsyncWrite(fcb->addr, fcb->count) != 0) {
                fcb->error = 0x10;
                return -1;
            }
            return 0;
        }
        n = SioSyncroWrite(fcb->addr, fcb->count);
        if (n < 0) {
            fcb->error = 5;
        }
        return n;
    }
    fcb->error = 0x16;
    return -1;
}

static s32 __nulldev(void) { return 0; }
