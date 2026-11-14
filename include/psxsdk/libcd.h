#ifndef _LIBCD_H_
#define _LIBCD_H_

/* Sony's libcd.h (Run-time Library Release 4.0): the CD-ROM subsystem. The
 * streaming library and the file search, which the game does not link, are
 * left out. Where a prototype is not Sony's, a note says what Sony's is.
 *
 * CD-ROM Primitive Command list:
 *
 *      Symbol          type    Contents
 *      ------------------------------------------------------
 *      CdlNop          B       NOP
 *      CdlSetloc       B       Set position
 *      CdlPlay         B       CD-DA Play
 *      CdlForward      B       Forward
 *      CdlBackward     B       Backward
 *      CdlReadN        B       Read with retry
 *      CdlStanby       N       Standby
 *      CdlStop         N       Stop
 *      CdlPause        N       Pause
 *      CdlMute         B       Mute on
 *      CdlDemute       B       Mute off
 *      CdlSetfilter    B       Set SubHeader filter
 *      CdlSetmode      B       Set mode
 *      CdlGetlocL      B       Get logical position
 *      CdlGetlocP      B       Get phisycal position
 *      CdlSeekL        N       Logical Seek
 *      CdlSeekP        N       Phisical Seek
 *      CdlReadS        B       Read without retry
 *      ------------------------------------------------------
 *                      B: Blocking, N: Non-Blocking operation
 *
 *      Symbol          arg             result
 *      --------------------------------------------------------------
 *      CdlNop          -               status
 *      CdlSetloc       min,sec,sector  status
 *      CdlPlay         -               status
 *      CdlForward      -               status
 *      CdlBackward     -               status
 *      CdlReadN        -               status
 *      CdlStanby       -               status
 *      CdlStop         -               status
 *      CdlPause        -               status
 *      CdlMute         -               status
 *      CdlDemute       -               status
 *      CdlSetfilter    file,chan       status
 *      CdlSetmode      mode            status
 *      CdlGetlocL      -               min,sec,sector,mode,file, chan
 *      CdlGetlocP      -               track,index,min,sec,frame,
 *                                                      amin,asec,aframe
 *      CdlSeekL        -               status
 *      CdlSeekP        -               status
 *      CdlReadS        -               status
 *      --------------------------------------------------------------
 */

#include "common.h"

/*
 * CD-ROM Mode (used int CdlSetmode)
 */
#define CdlModeStream 0x100  /* Normal Streaming                     */
#define CdlModeStream2 0x120 /* SUB HEADER information includes      */
#define CdlModeSpeed 0x80    /* 0: normal speed 1: double speed */
#define CdlModeRT 0x40       /* 0: ADPCM off            1: ADPCM on     */
#define CdlModeSize1 0x20    /* 0: 2048 byte            1: 2340byte     */
#define CdlModeSize0 0x10    /* 0: -                    1: 2328byte     */
#define CdlModeSF 0x08       /* 0: Channel off  1: Channel on   */
#define CdlModeRept 0x04     /* 0: Report off   1: Report on    */
#define CdlModeAP 0x02       /* 0: AutoPause off        1: AutoPause on */
#define CdlModeDA 0x01       /* 0: CD-DA off            1: CD-DA on     */

/*
 * Status Contents
 */
#define CdlStatPlay 0x80      /* playing CD-DA */
#define CdlStatSeek 0x40      /* seeking */
#define CdlStatRead 0x20      /* reading data sectors */
#define CdlStatShellOpen 0x10 /* once shell open */
#define CdlStatSeekError 0x04 /* seek error detected */
#define CdlStatStandby 0x02   /* spindle motor rotating */
#define CdlStatError 0x01     /* command error detected */

/*
 * Macros for CdGetDiskType()
 */
#define CdlStatNoDisk 0
#define CdlOtherFormat 1
#define CdlCdromFormat 2

/*
 * CD-ROM Primitive Commands
 */
#define CdlNop 0x01
#define CdlSetloc 0x02
#define CdlPlay 0x03
#define CdlForward 0x04
#define CdlBackward 0x05
#define CdlReadN 0x06
#define CdlStandby 0x07
#define CdlStop 0x08
#define CdlPause 0x09
#define CdlMute 0x0b
#define CdlDemute 0x0c
#define CdlSetfilter 0x0d
#define CdlSetmode 0x0e
#define CdlGetparam 0x0f
#define CdlGetlocL 0x10
#define CdlGetlocP 0x11
#define CdlGetTN 0x13
#define CdlGetTD 0x14
#define CdlSeekL 0x15
#define CdlSeekP 0x16
#define CdlReadS 0x1B

/*
 * Interrupts
 */
#define CdlNoIntr 0x00      /* No interrupt */
#define CdlDataReady 0x01   /* Data Ready */
#define CdlComplete 0x02    /* Command Complete */
#define CdlAcknowledge 0x03 /* Acknowledge (reserved) */
#define CdlDataEnd 0x04     /* End of Data Detected */
#define CdlDiskError 0x05   /* Error Detected */

/*
 * Library Macros
 */
#define btoi(b) ((b) / 16 * 10 + (b) % 16) /* BCD to u_char */
#define itob(i) ((i) / 10 * 16 + (i) % 10) /* u_char to BCD */

#define CdSeekL(p) CdControl(CdlSeekL, (u8 *)p, 0)
#define CdSeekP(p) CdControl(CdlSeekP, (u8 *)p, 0)
#define CdStandby() CdControl(CdlStandby, 0, 0)
#define CdPause() CdControl(CdlPause, 0, 0)
#define CdStop() CdControl(CdlStop, 0, 0)
#define CdMute() CdControl(CdlMute, 0, 0)
#define CdDeMute() CdControl(CdlDemute, 0, 0)
#define CdForward() CdControl(CdlForward, 0, 0)
#define CdBackward() CdControl(CdlBackward, 0, 0)

/*
 *      Position
 */
#define CdlMAXTOC 100

/*
 *      Callback
 */
typedef void (*CdlCB)(u8, u8 *);

/*
 *      Location
 */
typedef struct {
    u8 minute; /* minute (BCD) */
    u8 second; /* second (BCD) */
    u8 sector; /* sector (BCD) */
    u8 track;  /* track (void) */
} CdlLOC;

/*
 *      ADPCM Filter
 */
typedef struct {
    u8 file; /* file ID (always 1) */
    u8 chan; /* channel ID */
    u16 pad;
} CdlFILTER;

/*
 *      Attenuator
 */
typedef struct {
    u8 val0; /* volume for CD(L) -> SPU (L) */
    u8 val1; /* volume for CD(L) -> SPU (R) */
    u8 val2; /* volume for CD(R) -> SPU (L) */
    u8 val3; /* volume for CD(R) -> SPU (R) */
} CdlATV;

/*
 *      Prototypes
 */
void CdFlush(void);
CdlLOC *CdIntToPos(s32 i, CdlLOC *p);
char *CdComstr(u8 com);
char *CdIntstr(u8 intr);
s32 CdControl(u8 com, u8 *param, u8 *result);
s32 CdControlB(u8 com, u8 *param, u8 *result);
s32 CdControlF(u8 com, u8 *param);
s32 CdGetSector(void *madr, s32 size);
s32 CdGetSector2(void *madr, s32 size);
s32 CdDataSync(s32 mode);
s32 CdMix(CdlATV *vol);
s32 CdPosToInt(CdlLOC *p);
s32 CdRead(s32 sectors, u32 *buf, s32 mode);
s32 CdReadSync(s32 mode, u8 *result);
s32 CdReady(s32 mode, u8 *result);
s32 CdSetDebug(s32 level);
s32 CdSync(s32 mode, u8 *result);
void (*CdDataCallback(void (*func)()))();
CdlCB CdReadCallback(CdlCB func);
CdlCB CdReadyCallback(CdlCB func);
CdlCB CdSyncCallback(CdlCB func);
s32 CdInit(void);
s32 CdReset(s32 mode);
s32 CdStatus(void);
s32 CdLastCom(void);
CdlLOC *CdLastPos(void);
s32 CdMode(void);
void CdReadBreak(void);
s32 CdReadMode(s32 mode);

#endif /* _LIBCD_H_ */
