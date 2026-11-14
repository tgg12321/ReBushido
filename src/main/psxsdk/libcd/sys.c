/* LIBCD SYS: the CD-ROM functions a program calls, on top of the driver in
 * bios.c. */
#include "common.h"
#include "libcd_internal.h"
#include <psxsdk/libetc.h>

inline s32 CdStatus(void) { return (u8)CD_status; }

s32 CdMode(void) { return CD_mode; }

s32 CdLastCom(void) { return CD_com; }

CdlLOC *CdLastPos(void) { return (CdlLOC *)CD_pos; }

s32 CdReset(s32 mode) {
    if (mode == 2) {
        CD_initintr();
        return 1;
    }
    if (CD_init() != 0) {
        return 0;
    }
    if (mode == 1) {
        if (CD_initvol() != 0) {
            return 0;
        }
    }
    return 1;
}

void CdFlush(void) { CD_flush(); }

s32 CdSetDebug(s32 level) {
    s32 old = CD_debug;
    CD_debug = level;
    return old;
}

char *CdComstr(u8 com) {
    if (com > CdlReadS) {
        return "none";
    }
    return CD_comstr[com];
}

char *CdIntstr(u8 intr) {
    if (intr > 6) {
        return "none";
    }
    return CD_intstr[intr];
}

s32 CdSync(s32 mode, u8 *result) { return CD_sync(mode, result); }

s32 CdReady(s32 mode, u8 *result) { return CD_ready(mode, result); }

CdlCB CdSyncCallback(CdlCB func) {
    CdlCB old = CD_cbsync;
    CD_cbsync = func;
    return old;
}

CdlCB CdReadyCallback(CdlCB func) {
    CdlCB old = CD_cbready;
    CD_cbready = func;
    return old;
}

/* For each command: whether its parameter is a position, to be set with
 * CdlSetloc before the command itself is sent. */
static s32 cd_com_setloc[32] = {0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0};

/* What CdControl, CdControlF and CdControlB share: the command is tried four
 * times. */
static inline s32 cd_cw(u8 com, u8 *param, u8 *result, s32 async) {
    CdlCB old = CD_cbsync;
    s32 count;

    for (count = 3; count != -1; count--) {
        CD_cbsync = 0;
        if (com != CdlNop && (CdStatus() & CdlStatShellOpen)) {
            CD_cw(CdlNop, 0, 0, 0);
        }
        if (param == 0 || cd_com_setloc[com] == 0 ||
            !CD_cw(CdlSetloc, param, result, 0)) {
            CD_cbsync = old;
            if (!CD_cw(com, param, result, async)) {
                return 0;
            }
        }
    }

    CD_cbsync = old;
    return -1;
}

s32 CdControl(u8 com, u8 *param, u8 *result) {
    return cd_cw(com, param, result, 0) == 0;
}

s32 CdControlF(u8 com, u8 *param) { return cd_cw(com, param, 0, 1) == 0; }

s32 CdControlB(u8 com, u8 *param, u8 *result) {
    if (cd_cw(com, param, result, 0)) {
        return 0;
    }
    return CD_sync(0, result) == CdlComplete;
}

s32 CdMix(CdlATV *vol) {
    CD_vol(vol);
    return 1;
}

s32 CdGetSector(void *madr, s32 size) { return CD_getsector(madr, size) == 0; }

s32 CdGetSector2(void *madr, s32 size) {
    return CD_getsector2(madr, size) == 0;
}

void (*CdDataCallback(void (*func)()))() { return DMACallback(3, func); }

s32 CdDataSync(s32 mode) { return CD_datasync(mode); }

CdlLOC *CdIntToPos(s32 i, CdlLOC *p) {
    inline int ENCODE_BCD(n) { return ((n / 10) << 4) + (n % 10); }

    i += 150;
    p->sector = ENCODE_BCD(i % 75);
    p->second = ENCODE_BCD(i / 75 % 60);
    p->minute = ENCODE_BCD(i / 75 / 60);
    return p;
}

s32 CdPosToInt(CdlLOC *p) {
    u8 b0 = p->minute;
    u8 b1 = p->second;
    s32 min, sec, frm;
    min = (b0 >> 4) * 10 + (b0 & 0xF);
    sec = min * 60;
    sec += (b1 >> 4) * 10 + (b1 & 0xF);
    {
        s32 total = sec * 75;
        u8 b2 = p->sector;
        frm = (b2 >> 4) * 10 + (b2 & 0xF);
        total += frm;
        return total - 150;
    }
}
