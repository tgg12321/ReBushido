#ifndef LIBCD_INTERNAL_H
#define LIBCD_INTERNAL_H

/* What LIBCD's modules share: the driver's state (bios.c) and the entry
 * points the command layer (sys.c) calls. */

#include <psxsdk/libcd.h>

extern s32 CD_status;
extern u8 CD_pos[4];
extern u8 CD_mode;
extern u8 CD_com;
extern CdlCB CD_cbsync;
extern CdlCB CD_cbready;
extern s32 CD_debug;
extern char *CD_comstr[];
extern char *CD_intstr[];

extern s32 CD_init(void);
extern void CD_initintr(void);
extern s32 CD_initvol(void);
extern void CD_flush(void);
extern s32 CD_sync(s32 mode, u8 *result);
extern s32 CD_ready(s32 mode, u8 *result);
extern s32 CD_cw(u8 com, u8 *param, u8 *result, s32 async);
extern s32 CD_vol(CdlATV *vol);
extern s32 CD_getsector(void *madr, s32 size);
extern s32 CD_getsector2(void *madr, s32 size);
extern s32 CD_datasync(s32 mode);

#endif /* LIBCD_INTERNAL_H */
