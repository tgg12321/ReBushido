#ifndef _LIBAPI_H_
#define _LIBAPI_H_

/* The kernel's entry points (Sony's libapi.h; in Run-time Library Release 4.0
 * these prototypes are at the end of kernel.h). Most are jumps into the BIOS.
 * Where a prototype is not Sony's, a note says what Sony's is and why this
 * one differs. */

#include "common.h"
#include <psxsdk/kernel.h>
#include <psxsdk/sys/file.h>

/* Sony: (unsigned long, unsigned short, long). The game passes a target of
 * -1 as a whole word. */
extern s32 SetRCnt(u32 spec, s32 target, s32 mode);
extern s32 GetRCnt(u32 spec);
extern s32 ResetRCnt(u32 spec);
extern s32 StartRCnt(u32 spec);
extern s32 StopRCnt(u32 spec);

extern s32 OpenEvent(u32 desc, s32 spec, s32 mode, s32 (*func)());
extern s32 CloseEvent(s32 event);
extern s32 WaitEvent(s32 event);
extern s32 TestEvent(s32 event);
extern s32 EnableEvent(s32 event);
extern s32 DisableEvent(s32 event);
extern void DeliverEvent(u32 desc, u32 spec);

extern s32 open(char *name, u32 mode);
extern s32 close(s32 fd);
extern s32 read(s32 fd, void *buf, s32 n);
extern s32 write(s32 fd, void *buf, s32 n);
extern struct DIRENTRY *firstfile(char *name, struct DIRENTRY *dir);
extern struct DIRENTRY *nextfile(struct DIRENTRY *dir);
extern s32 format(char *fs);

extern s32 Exec(struct EXEC *exec, s32 argc, char **argv);

extern s32 InitPAD(char *bufA, s32 lenA, char *bufB, s32 lenB);
extern s32 StartPAD(void);
extern void StopPAD(void);
extern void EnablePAD(void);
extern void DisablePAD(void);

extern void FlushCache(void);
extern void ReturnFromException(void);
extern s32 EnterCriticalSection(void);
extern void ExitCriticalSection(void);

extern u32 SetSp(u32 sp);
extern void SetMem(s32 megabytes);

extern void ChangeClearPAD(s32 val);

/* The memory card. _new_card takes no argument, but _card_clear passes its
 * channel along, so it has no prototype. */
extern void InitCARD(s32 val);
extern s32 StartCARD(void);
extern s32 StopCARD(void);
extern void _bu_init(void);
extern s32 _card_info(s32 chan);
extern s32 _card_clear(s32 chan);
extern s32 _card_load(s32 chan);
extern void _new_card();
extern s32 _card_write(s32 chan, s32 block, u8 *buf);

/* Not in Sony's header: what the libraries call among themselves. */
struct DCB;
extern void AddDrv(struct DCB *dcb);
extern void DelDrv(const char *name);
extern void ResetEntryInt(void);
extern void HookEntryInt(s32 *jmp_buf);
extern void ChangeClearRCnt(s32 counter, s32 clear);
extern s32 ReadInitPadFlag(void);
extern void InitCARD2(s32 val);
extern void StartCARD2(void);
extern void StopCARD2(void);

/* A serial port's registers: the controller and memory card port, and the
 * link cable's. */
typedef struct {
    u8 data;
    u8 unk1[3]; /* not a register */
    u16 stat;
    u16 unk6; /* not a register */
    u16 mode;
    u16 ctrl;
    u16 misc;
    u16 baud;
} SioRegs;

#endif /* _LIBAPI_H_ */
