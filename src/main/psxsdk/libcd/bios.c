/* LIBCD BIOS: the CD-ROM controller's driver ($Id: bios.c,v 1.86). It sends
 * the commands, and in the controller's interrupt reads what comes back and
 * calls the callbacks. */
#include "common.h"
#include "psx.h"
#include <psxsdk/libc.h>
#include <psxsdk/libetc.h>
#include "libcd_internal.h"

/* Sony's stamp (the letters Ps and a build number) at the head of the
 * module's data; nothing reads it. */
u32 _cd_stamp[2] = {0x15047350, 0x0040859C};
CdlCB CD_cbsync = NULL;
CdlCB CD_cbready = NULL;
CdlCB CD_cbread = NULL;
s32 CD_debug = 0;
s32 CD_status = 0;
s32 CD_status1 = 0;
s32 CD_nopen = 0;
u8 CD_pos[4] = {2, 0, 0, 0};
u8 CD_mode = 0;
u8 CD_com = 0;
s32 DS_active = 0;
char *CD_comstr[] = {
    "CdlSync",    "CdlNop",
    "CdlSetloc",  "CdlPlay",
    "CdlForward", "CdlBackward",
    "CdlReadN",   "CdlStandby",
    "CdlStop",    "CdlPause",
    "CdlReset",   "CdlMute",
    "CdlDemute",  "CdlSetfilter",
    "CdlSetmode", "?",
    "CdlGetlocL", "CdlGetlocP",
    "?",          "CdlGetTN",
    "CdlGetTD",   "CdlSeekL",
    "CdlSeekP",   "?",
    "?",          "?",
    "?",          "CdlReadS",
    "?",          "?",
    "?",          "?",
};
char *CD_intstr[] = {
    "NoIntr",  "DataReady", "Complete", "Acknowledge",
    "DataEnd", "DiskError", "?",        "?",
};

/* What each command needs, in four tables of one object. */
static struct {
    /* whether the controller only acknowledges it at first and reports that
     * it is complete later */
    s32 ack_first[32];
    /* whether the command throws away data that was ready */
    s32 clears_ready[32];
    /* whether its acknowledgement carries the drive's status */
    s32 has_status[32];
    /* how many parameter bytes it takes */
    s32 nparam[32];
} cd_com = {
    {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0,
     0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0,
     0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static volatile u8 *cd_reg0 = (u8 *)0x1F801800;
static volatile u8 *cd_reg1 = (u8 *)0x1F801801;
static volatile u8 *cd_reg2 = (u8 *)0x1F801802;
static volatile u8 *cd_reg3 = (u8 *)0x1F801803;
static volatile u32 *com_delay = (u32 *)0x1F801020;
static volatile u16 *cd_spu_regs = (u16 *)0x1F801C00; /* the SPU's registers */

/* What the last interrupts reported, for the waits to pick up: the state of
 * the command, of the data, and the end of the data. */
typedef struct {
    u8 sync;
    u8 ready;
    u8 c;
} CD_intr;

static volatile CD_intr Intr = {0};

/* The result bytes of the last command, of the last data-ready and of the
 * last data-end interrupt: three Result_t in a row. */
typedef char Result_t[8];
static Result_t Result[3];

/* The time limit on a wait: when it runs out, in vertical blanks, how many
 * times it has been asked, and who is waiting. */
typedef struct {
    s32 time;
    s32 count;
    char *name;
} Alarm_t;

static Alarm_t Alarm;

static inline void set_alarm(char *name) {
    Alarm.time = VSync(-1) + 0x3C0;
    Alarm.count = 0;
    Alarm.name = name;
}

static inline s32 get_alarm(void) {
    if (Alarm.time < VSync(-1) || Alarm.count++ > 0x3C0000) {
        puts("CD timeout: ");
        printf("%s:(%s) Sync=%s, Ready=%s\n", Alarm.name, CD_comstr[CD_com],
               CD_intstr[Intr.sync], CD_intstr[Intr.ready]);
        CD_flush();
        return -1;
    }
    return 0;
}

static inline void _memcpy(void *_dst, volatile void *_src, u32 _size) {
    char *pDst = (char *)_dst;
    volatile char *pSrc = _src;
    if (pDst == 0) {
        return;
    }
    while (_size--) {
        *pDst++ = *pSrc++;
    }
}

/* Reads one interrupt from the controller: its cause and result bytes.
 * Returns which callbacks are due (2: sync, 4: ready), 1 for a command that
 * was only acknowledged, 0 when nothing is pending. */
s32 getintr(void) {
    volatile char nReg;
    volatile Result_t buf;
    s32 i, j;
    s32 bHasError;

    *cd_reg0 = 1;

    nReg = *cd_reg3 & 0x7;

    if (nReg == 0) {
        return 0;
    }

    bHasError = 0;

    while (nReg != (*cd_reg3 & 7)) {
        nReg = *cd_reg3 & 0x7;
    }

    for (i = 0; i < 8; i++) {
        if ((*cd_reg0 & 0x20) == 0) {
            break;
        }
        buf[i] = *cd_reg1;
    }
    for (j = i; j < 8; j++) {
        buf[j] = 0;
    }

    *cd_reg0 = 1;
    *cd_reg3 = 7;
    *cd_reg2 = 7;
    if (nReg != CdlAcknowledge || cd_com.has_status[CD_com]) {
        if (!(CD_status & CdlStatShellOpen) && (buf[0] & CdlStatShellOpen)) {
            CD_nopen++;
        }
        CD_status = buf[0];
        CD_status1 = buf[1];
        bHasError = CD_status & 0x1D;
    }
    if (nReg == CdlDiskError) {
        if (CD_debug > 0) {
            printf("DiskError: ");
        }
        if (CD_debug > 0) {
            printf("com=%s,code=(%02x:%02x)\n", CD_comstr[CD_com], CD_status,
                   CD_status1);
        }
    }
    switch (nReg) {
    case CdlAcknowledge:
        if (bHasError) {
            Intr.sync = CdlDiskError;
            _memcpy(Result[0], &buf, sizeof(Result_t));
            return 2;
        }
        if (cd_com.ack_first[CD_com]) {
            Intr.sync = CdlAcknowledge;
            _memcpy(Result[0], &buf, sizeof(Result_t));
            return 1;
        }
        Intr.sync = CdlComplete;
        _memcpy(Result[0], &buf, sizeof(Result_t));
        return 2;
    case CdlComplete:
        Intr.sync = bHasError ? CdlDiskError : CdlComplete;
        _memcpy(Result[0], &buf, sizeof(Result_t));
        return 2;
    case CdlDataReady:
        if (bHasError && i == 1) {
            bHasError = 0;
        }
        Intr.ready = bHasError ? CdlDiskError : CdlDataReady;
        _memcpy(Result[1], &buf, sizeof(Result_t));
        *cd_reg0 = 0;
        *cd_reg3 = 0;
        return 4;
    case CdlDataEnd:
        Intr.ready = Intr.c = CdlDataEnd;
        _memcpy(Result[2], &buf, sizeof(Result_t));
        _memcpy(Result[1], &buf, sizeof(Result_t));
        return 4;
    case CdlDiskError:
        Intr.sync = Intr.ready = CdlDiskError;
        _memcpy(Result[0], &buf, sizeof(Result_t));
        _memcpy(Result[1], &buf, sizeof(Result_t));
        return 6;
    default:
        puts("CDROM: unknown intr");
        printf("(%d)\n", nReg);
        return 0;
    }
}

/* Serves every pending interrupt and calls the callbacks. */
static inline void callback(void) {
    s32 status;
    u8 saved;

    saved = *cd_reg0 & 3;
    while (1) {
        status = getintr();
        if (status == 0) {
            break;
        }
        if ((status & 4) && CD_cbready != 0) {
            CD_cbready(Intr.ready, Result[1]);
        }
        if ((status & 2) && CD_cbsync != 0) {
            CD_cbsync(Intr.sync, Result[0]);
        }
    }
    *cd_reg0 = saved;
}

s32 CD_sync(s32 mode, u8 *result) {
    s32 sync;

    set_alarm("CD_sync");
    while (1) {
        if (get_alarm()) {
            return -1;
        }
        if (CheckCallback()) {
            callback();
        }
        sync = Intr.sync;
        if (sync == CdlComplete || sync == CdlDiskError) {
            Intr.sync = CdlComplete;
            _memcpy(result, Result[0], 8);
            return sync;
        }
        if (mode != 0) {
            return 0;
        }
    }
}

s32 CD_ready(s32 mode, u8 *result) {
    s32 c;
    s32 ready;

    set_alarm("CD_ready");
    while (1) {
        if (get_alarm()) {
            return -1;
        }
        if (CheckCallback()) {
            callback();
        }
        c = Intr.c;
        if (c != 0) {
            Intr.c = 0;
            _memcpy(result, Result[2], 8);
            return c;
        }
        ready = Intr.ready;
        if (ready != 0) {
            Intr.ready = 0;
            _memcpy(result, Result[1], 8);
            return ready;
        }
        if (mode != 0) {
            return 0;
        }
    }
}

/* Sends a command and its parameters, and unless `async` waits for it. */
s32 CD_cw(u8 com, u8 *param, u8 *result, s32 async) {
    s32 i;

    if (CD_debug > 1) {
        printf("%s...\n", CD_comstr[com]);
    }
    if (cd_com.nparam[com] != 0 && param == 0) {
        if (CD_debug > 0) {
            printf("%s: no param\n", CD_comstr[com]);
        }
        return -2;
    }
    CD_sync(0, 0);
    if (com == CdlSetloc) {
        for (i = 0; i < 4; i++) {
            CD_pos[i] = param[i];
        }
    }
    if (com == CdlSetmode) {
        CD_mode = param[0];
    }
    Intr.sync = 0;
    if (cd_com.clears_ready[com]) {
        Intr.ready = 0;
    }
    *cd_reg0 = 0;
    for (i = 0; i < cd_com.nparam[com]; i++) {
        *cd_reg2 = param[i];
    }
    CD_com = com;
    *cd_reg1 = com;
    if (async != 0) {
        return 0;
    }

    set_alarm("CD_cw");

    while (Intr.sync == 0) {
        if (get_alarm()) {
            return -1;
        }
        if (CheckCallback()) {
            callback();
        }
    }

    _memcpy(result, Result[0], 8);
    return -(Intr.sync == CdlDiskError);
}

s32 CD_vol(CdlATV *vol) {
    *cd_reg0 = 2;
    *cd_reg2 = vol->val0;
    *cd_reg3 = vol->val1;
    *cd_reg0 = 3;
    *cd_reg1 = vol->val2;
    *cd_reg2 = vol->val3;
    *cd_reg3 = 0x20;
    return 0;
}

void CD_flush(void) {
    u8 v0;
    *cd_reg0 = 1;
    v0 = *cd_reg3 & 7;
    if (v0 != 0) {
        do {
            *cd_reg0 = 1;
            *cd_reg3 = 7;
            *cd_reg2 = 7;
            v0 = *cd_reg3 & 7;
        } while (v0 != 0);
    }
    Intr.ready = Intr.c = 0;
    Intr.sync = CdlComplete;
    *cd_reg0 = 0;
    *cd_reg3 = 0;
    *com_delay = 0x1325;
}

s32 CD_initvol(void) {
    CdlATV vol;

    if (cd_spu_regs[0xDC] == 0 && cd_spu_regs[0xDD] == 0) {
        cd_spu_regs[0xC0] = 0x3FFF;
        cd_spu_regs[0xC1] = 0x3FFF;
    }

    cd_spu_regs[0xD8] = 0x3FFF;
    cd_spu_regs[0xD9] = 0x3FFF;
    cd_spu_regs[0xD5] = 0xC001;
    vol.val0 = vol.val2 = 0x80;
    vol.val1 = vol.val3 = 0;
    *cd_reg0 = 2;
    *cd_reg2 = vol.val0;
    *cd_reg3 = vol.val1;
    *cd_reg0 = 3;
    *cd_reg1 = vol.val2;
    *cd_reg2 = vol.val3;
    *cd_reg3 = 0x20;
    return 0;
}

void CD_initintr(void) {
    CD_cbready = 0;
    CD_cbsync = 0;
    CD_status1 = 0;
    CD_status = 0;
    ResetCallback();
    InterruptCallback(2, callback);
}

/* Where the module's state is, and its version: CD_init prints this table's
 * address, for a debugger to find them by. */
const volatile void *cd_state_ptrs[] = {
    &Intr,   Result[0],
    &CD_com, &CD_status,
    CD_pos,  "$Id: bios.c,v 1.86 1997/03/28 07:42:42 makoto Exp $",
};

s32 CD_init(void) {
    u8 v0;

    puts("CD_init:");
    printf("addr=%08x\n", &cd_state_ptrs);

    CD_com = 0;
    CD_mode = 0;
    CD_cbready = 0;
    CD_cbsync = 0;
    CD_status1 = 0;
    CD_status = 0;

    ResetCallback();
    InterruptCallback(2, callback);

    *cd_reg0 = 1;
    v0 = *cd_reg3 & 7;
    if (v0 != 0) {
        do {
            *cd_reg0 = 1;
            *cd_reg3 = 7;
            *cd_reg2 = 7;
            v0 = *cd_reg3 & 7;
        } while (v0 != 0);
    }

    Intr.ready = Intr.c = 0;
    Intr.sync = CdlComplete;
    *cd_reg0 = 0;
    *cd_reg3 = 0;
    *com_delay = 0x1325;

    CD_cw(CdlNop, 0, 0, 0);

    if (CD_status & CdlStatShellOpen) {
        CD_cw(CdlNop, 0, 0, 0);
    }

    if (CD_cw(0xA, 0, 0, 0) != 0) { /* the controller's reset */
        return -1;
    }
    if (CD_cw(CdlDemute, 0, 0, 0) != 0) {
        return -1;
    }
    if (CD_sync(0, 0) != CdlComplete) {
        return -1;
    }
    return 0;
}

/* The CD-ROM's bus timing, the DMA control register and DMA channel 3,
 * which is the CD-ROM's. */
static volatile u32 *cdrom_delay = (u32 *)0x1F801018;
static volatile u32 *cd_d_pcr = (u32 *)0x1F8010F0;
static volatile u32 *d3_madr = (u32 *)0x1F8010B0;
static volatile u32 *d3_bcr = (u32 *)0x1F8010B4;
static volatile u32 *d3_chcr = (u32 *)0x1F8010B8;

/* Waits for the data transfer to end (or, with `mode`, only looks). */
s32 CD_datasync(s32 mode) {
    s32 ret;

    set_alarm("CD_datasync");
    while (1) {
        if (get_alarm()) {
            ret = -1;
            break;
        }
        if (!(*d3_chcr & 0x1000000)) {
            ret = 0;
            break;
        }
        if (mode != 0) {
            ret = 1;
            break;
        }
    }
    return ret;
}

/* Reads `size` words of the sector the controller holds, to `madr`. */
s32 CD_getsector(void *madr, s32 size) {
    *cd_reg0 = 0;
    *cd_reg3 = CD_REQ_WANT_DATA;
    *cdrom_delay = 0x20943;
    *com_delay = 0x1323;
    *cd_d_pcr |= DMA_CD_ENABLE;
    *d3_madr = (u32)madr;
    *d3_bcr = size | 0x10000;
    while (!(*cd_reg0 & CD_STAT_DATA_REQ)) {
    }
    *d3_chcr = DMA_CD_TO_RAM;
    while (*d3_chcr & DMA_BUSY) {
    }
    *com_delay = 0x1325;
    return 0;
}

/* The same, without waiting for the transfer to end. */
s32 CD_getsector2(void *madr, s32 size) {
    *cd_reg0 = 0;
    *cd_reg3 = CD_REQ_WANT_DATA;
    *cdrom_delay = 0x21020843;
    *com_delay = 0x1325;
    *cd_d_pcr |= DMA_CD_ENABLE;
    *d3_madr = (u32)madr;
    *d3_bcr = size | 0x10000;
    while (!(*cd_reg0 & CD_STAT_DATA_REQ)) {
    }
    *d3_chcr = DMA_CD_TO_RAM_CHOPPED;
    {
        volatile s32 tmp;
        tmp = *d3_chcr;
    }
    return 0;
}

/* Sets how many parameters the test command (0x19) takes. */
void CD_set_test_parmnum(s32 parmnum) { cd_com.nparam[0x19] = parmnum; }
