/* LIBCD CDREAD: CdRead, which reads sectors into memory in the background,
 * one for each data-ready interrupt, and starts over when the drive fails. */
#include "common.h"
#include "libcd_internal.h"
#include <psxsdk/libc.h>
#include <psxsdk/libetc.h>

/* The read in progress. */
typedef struct {
    s32 sectors; /* what CdRead was asked for */
    u32 *buf;
    u32 *p;       /* where the next sector goes */
    s32 mode;     /* the drive's mode */
    s32 size;     /* a sector, in words */
    s32 cnt;      /* sectors still to come; -1 after an error */
    s32 t2;       /* when the last sector came, in vertical blanks */
    s32 t1;       /* when the read began */
    s32 pos;      /* the sector expected next */
    CdlCB cbsync; /* the program's callbacks, put back when it is over */
    CdlCB cbready;
    void (*cbdata)();
    s32 tslmode; /* CdReadMode: bit 0, sectors are fetched without waiting */
    u8 *result;
} CdReadState;

extern volatile CdReadState cd_read_state;
extern CdlCB CD_ReadCallbackFunc;

void cb_data(void);
s32 cd_read_retry(s32);

/* The data-ready callback while a read is going: fetches the sector, having
 * checked from its header that it is the one expected. */
static void cb_read(u8 intr, u8 *result) {
    s32 pos[3];

    cd_read_state.result = result;
    if (intr == CdlDataReady) {
        if (cd_read_state.cnt > 0) {
            if (cd_read_state.size == 2048 / 4) {
                if (cd_read_state.tslmode & 1) {
                    CdDataCallback(0);
                    CdGetSector2(pos, 3);
                    CdDataSync(0);
                    CdDataCallback(cb_data);
                } else {
                    CdGetSector(pos, 3);
                }
                if (CdPosToInt((CdlLOC *)pos) != cd_read_state.pos) {
                    puts("CdRead: sector error\n");
                    cd_read_state.cnt = -1;
                }
            }
            if (cd_read_state.tslmode & 1) {
                CdGetSector2(cd_read_state.p, cd_read_state.size);
            } else {
                CdGetSector(cd_read_state.p, cd_read_state.size);
                cd_read_state.p += cd_read_state.size;
                cd_read_state.cnt--;
                cd_read_state.pos++;
            }
        }
    } else {
        cd_read_state.cnt = -1;
    }
    cd_read_state.t2 = VSync(-1);
    if (cd_read_state.cnt < 0) {
        cd_read_retry(1);
    }
    if (VSync(-1) > cd_read_state.t1 + 1200) {
        cd_read_state.cnt = -1;
    }
    if (cd_read_state.cnt != 0 && VSync(-1) <= cd_read_state.t1 + 1200) {
        return;
    }
    CdSyncCallback(cd_read_state.cbsync);
    CdReadyCallback(cd_read_state.cbready);
    if (cd_read_state.tslmode & 1) {
        CdDataCallback(cd_read_state.cbdata);
    }
    CdControlF(CdlPause, 0);
    if (CD_ReadCallbackFunc != 0) {
        CD_ReadCallbackFunc(
            cd_read_state.cnt == 0 ? CdlComplete : CdlDiskError, result);
    }
}

/* When sectors are fetched without waiting: called as each transfer ends, to
 * move on to the next. */
void cb_data(void) {
    cd_read_state.p += cd_read_state.size;
    cd_read_state.cnt--;
    cd_read_state.pos++;
    if (cd_read_state.cnt != 0) {
        return;
    }
    CdSyncCallback(cd_read_state.cbsync);
    CdReadyCallback(cd_read_state.cbready);
    if (cd_read_state.tslmode & 1) {
        CdDataCallback(cd_read_state.cbdata);
    }
    CdControlF(CdlPause, 0);
    if (CD_ReadCallbackFunc != 0) {
        CD_ReadCallbackFunc(CdlComplete, cd_read_state.result);
    }
}

/* Starts the read from the beginning: the first time, and (`retry`) after an
 * error. Returns the sectors to come, or -1. */
s32 cd_read_retry(s32 retry) {
    u8 param;

    CdSyncCallback(0);
    CdReadyCallback(0);
    if (cd_read_state.tslmode & 1) {
        CdDataCallback(0);
    }
    if (CdStatus() & CdlStatShellOpen) {
        if (!(VSync(-1) & 0x3F)) {
            puts("CdRead: Shell open...\n");
        }
        CdControlF(CdlNop, 0);
        cd_read_state.t1 = VSync(-1);
        cd_read_state.cnt = -1;
        return cd_read_state.cnt;
    }
    if (retry != 0) {
        puts("CdRead: retry...\n");
        CdControl(CdlPause, 0, 0);
        if (CdControl(CdlSetloc, (u8 *)CdLastPos(), 0) == 0) {
            return cd_read_state.cnt = -1;
        }
    }
    CdFlush();
    param = cd_read_state.mode;
    if (param != CdMode() || retry != 0) {
        if (CdControl(CdlSetmode, &param, 0) == 0) {
            cd_read_state.cnt = -1;
            return cd_read_state.cnt;
        }
    }
    cd_read_state.pos = CdPosToInt(CdLastPos());
    CdReadyCallback(cb_read);
    if (cd_read_state.tslmode & 1) {
        CdDataCallback(cb_data);
    }
    cd_read_state.p = cd_read_state.buf;
    CdControlF(CdlReadN, 0);
    cd_read_state.cnt = cd_read_state.sectors;
    cd_read_state.t2 = VSync(-1);
    return cd_read_state.cnt;
}

void CdReadBreak(void) {
    if (cd_read_state.tslmode & 1) {
        CdDataSync(0);
    }
    cd_read_state.cnt = 0;
    CdSyncCallback(cd_read_state.cbsync);
    CdReadyCallback(cd_read_state.cbready);
    if (cd_read_state.tslmode & 1) {
        CdDataCallback(cd_read_state.cbdata);
    }
    CdControlF(CdlPause, 0);
}

s32 CdRead(s32 sectors, u32 *buf, s32 mode) {
    cd_read_state.mode = mode;
    switch (cd_read_state.mode & (CdlModeSize0 | CdlModeSize1)) {
    case 0:
        cd_read_state.size = 2048 / 4;
        break;
    case CdlModeSize1:
        cd_read_state.size = 2340 / 4;
        break;
    default:
        cd_read_state.size = 2328 / 4;
        break;
    }
    cd_read_state.mode |= CdlModeSize1;
    cd_read_state.buf = buf;
    cd_read_state.sectors = sectors;
    cd_read_state.cbsync = CdSyncCallback(0);
    cd_read_state.cbready = CdReadyCallback(0);
    if (cd_read_state.tslmode & 1) {
        cd_read_state.cbdata = CdDataCallback(0);
    }
    cd_read_state.t1 = VSync(-1);
    if (CdStatus() & (CdlStatPlay | CdlStatSeek | CdlStatRead)) {
        CdControlB(CdlPause, 0, 0);
    }
    return cd_read_retry(0) > 0;
}

/* Waits for the read to end (or, with `mode`, only looks): the sectors still
 * to come, or -1 once it has taken twenty seconds. A read that has stalled
 * for a second is started over. */
s32 CdReadSync(s32 mode, u8 *result) {
    s32 left;

    while (1) {
        left = -1;
        if (VSync(-1) <= cd_read_state.t1 + 1200) {
            if (cd_read_state.cnt < 0 || VSync(-1) > cd_read_state.t2 + 60) {
                cd_read_retry(1);
                left = cd_read_state.sectors;
            } else {
                left = cd_read_state.cnt;
            }
        }
        if (mode != 0 || left <= 0) {
            CdReady(1, result);
            return left;
        }
    }
}

CdlCB CdReadCallback(CdlCB func) {
    CdlCB old = CD_ReadCallbackFunc;
    CD_ReadCallbackFunc = func;
    return old;
}

s32 CdReadMode(s32 mode) {
    s32 old = cd_read_state.tslmode;
    cd_read_state.tslmode = mode;
    return old;
}

CdlCB CD_ReadCallbackFunc = 0;

volatile CdReadState cd_read_state = {0};
